
#include <stdio.h>

#include <math.h>
#include <memory.h>
#include <sys/mman.h>
#include <errno.h>

#include "gpu_thread.h"
#include "errors.h"
#include "buffers.h"

#include "pthread.h"

// TODO This should move to a config file.
#define NUM_CL_FILES        3
#define OPENCL_FILENAME_1   "kernels/test0xB_multifreq.cl"
#define OPENCL_FILENAME_2   "kernels/offsetAccumulator.cl"
#define OPENCL_FILENAME_3   "kernels/preseed_multifreq.cl"

//bad form... remove this
//int saveInputDataCounter;
//


pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

void setupOpenCL(struct OpenCLData * cl_data);

void closeOpenCL(struct OpenCLData * cl_data);

void addQueueSet(struct OpenCLData * cl_data, int buffer_id);

void gpu_thread(void* arg)
{
    struct gpu_thread_args * args = (struct gpu_thread_args *) arg; 

    struct OpenCLData cl_data;

    cl_data.in_buf = args->in_buf;
    cl_data.out_buf = args->out_buf;

    cl_data.block_size = args->block_size;
    cl_data.num_antennas = args->num_antennas;
    cl_data.num_iterations = args->num_iterations;
    cl_data.num_freq = args->num_freq;

    cl_data.accumulate_len = args->num_freq * args->num_antennas * 2 * sizeof(cl_int);

    cl_data.gpu_id = args->gpu_id;

    setupOpenCL(&cl_data);

    // Queue the initial commands in buffer order.
    for (int i = 0; i < args->in_buf->num_buffers; ++i) {
        addQueueSet(&cl_data, i);
    }

    CHECK_ERROR( pthread_mutex_lock(&args->lock) );
    args->started = 1;
    CHECK_ERROR( pthread_mutex_unlock(&args->lock) );

    // Signal consumer (main thread in this case).
    CHECK_ERROR( pthread_cond_broadcast(&args->cond) );

    // Just wait on one buffer.
    int buffer_list[1] = {0};
    int bufferID = 0;

    // Main loop for collecting data from producers.
    for(;;) {

        // Wait for data, this call will block.
        bufferID = getFullBufferFromList(args->in_buf, buffer_list, 1);

        // If buffer id is -1, then all the producers are done.
        if (bufferID == -1) {
            break;
        }

        // Wait for the output buffer to be empty as well.
        // This should almost never block, since the output buffer should clear quickly.
        waitForEmptyBuffer(args->out_buf, bufferID);

        CHECK_CL_ERROR( clSetUserEventStatus(cl_data.host_buffer_ready[buffer_list[0]], CL_SUCCESS) );

        buffer_list[0] = (buffer_list[0] + 1) % args->in_buf->num_buffers;
    }

    DEBUG("Closing OpenCL\n");

    closeOpenCL(&cl_data);

    markProducerDone(args->out_buf, 0);

    int ret;
    pthread_exit((void *) &ret); 
}

void wait_for_gpu_thread_ready(struct gpu_thread_args * args)
{
    CHECK_ERROR( pthread_mutex_lock(&args->lock) );

    while ( args->started == 0 ) {
        pthread_cond_wait(&args->cond, &args->lock);
    }

    CHECK_ERROR( pthread_mutex_unlock(&args->lock) );
}

void CL_CALLBACK readComplete(cl_event event, cl_int status, void *data) {

    //INFO("GPU Kernel Finished!\n");

    struct call_back_data * cb_data = (struct call_back_data *) data;

    // Mark the input buffer as "empty" so that it can be reused.
    markBufferEmpty(cb_data->cl_data->in_buf, cb_data->buffer_id);

    // Copy the information contained in the input buffer
    copy_buffer_info(cb_data->cl_data->in_buf, cb_data->buffer_id, 
                     cb_data->cl_data->out_buf, cb_data->buffer_id);

    // Mark the output buffer as full, so it can be processed.
    markBufferFull(cb_data->cl_data->out_buf, cb_data->buffer_id);

    // TODO move this to the consumer thread for the output data.
    addQueueSet(cb_data->cl_data, cb_data->buffer_id);

}

void addQueueSet(struct OpenCLData * cl_data, int buffer_id)
{
    cl_int err;

    // This function can be called from a call back, so it must be thread safe to avoid
    // having queues out of order.
    pthread_mutex_lock(&queue_lock);

    // Set call back data
    cl_data->cb_data[buffer_id].buffer_id = buffer_id;
    cl_data->cb_data[buffer_id].cl_data = cl_data;

    cl_data->host_buffer_ready[buffer_id] = clCreateUserEvent(cl_data->context, &err);

    CHECK_CL_ERROR(err);

    // Data transfer to GPU
    CHECK_CL_ERROR( clEnqueueWriteBuffer(cl_data->queue[0],
                                            cl_data->device_input_buffer[buffer_id],
                                            CL_FALSE,
                                            0, //offset
                                            cl_data->in_buf->buffer_size,
                                            cl_data->in_buf->data[buffer_id],
                                            1,
                                            &cl_data->host_buffer_ready[buffer_id], // Wait on this user event (network finished).
                                            &cl_data->input_data_written[buffer_id]) );

    CHECK_CL_ERROR( clEnqueueWriteBuffer(cl_data->queue[0],
                                            cl_data->device_accumulate_buffer[buffer_id],
                                            CL_FALSE,
                                            0,
                                            cl_data->accumulate_len,
                                            cl_data->accumulate_zeros,
                                            1,
                                            &cl_data->input_data_written[buffer_id],
                                            &cl_data->accumulate_data_zeroed[buffer_id]) );

    // New section to check input buffer contents
   // if (saveInputDataCounter == 150){
   //     FILE *fp; 
   //     fp = fopen("InputDataDump", "wb");
   //     fwrite(cl_data->in_buf->data[buffer_id],1,cl_data->in_buf->buffer_size, fp);
   //     fclose(fp);
   // }
   // saveInputDataCounter++;

    // not sure it is actually paying attention to our changes
    //printf(".");
    //
    
	
    // The offset accumulate kernel args.
    // Set 2 arguments--input array and zeroed output array
    CHECK_CL_ERROR( clSetKernelArg(cl_data->offset_accumulate_kernel,
                                        0,
                                        sizeof(void*),
                                        (void*) &cl_data->device_input_buffer[buffer_id]) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->offset_accumulate_kernel,
                                        1,
                                        sizeof(void *),
                                        (void*) &cl_data->device_accumulate_buffer[buffer_id]) );

    CHECK_CL_ERROR( clEnqueueNDRangeKernel(cl_data->queue[1],
                                            cl_data->offset_accumulate_kernel,
                                            3,
                                            NULL,
                                            cl_data->gws_accum,
                                            cl_data->lws_accum,
                                            1,
                                            &cl_data->accumulate_data_zeroed[buffer_id],
                                            &cl_data->offset_accumulate_finished[buffer_id]) );

    // The perseed kernel
    // preseed_kernel--set 2 of the 6 arguments (the other 4 stay the same)
    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                        0,
                                        sizeof(void *),
                                        (void*) &cl_data->device_accumulate_buffer[buffer_id]) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                        1,
                                        sizeof(void *),
                                        (void *) &cl_data->device_output_buffer[buffer_id]) );

    CHECK_CL_ERROR( clEnqueueNDRangeKernel(cl_data->queue[1],
                                            cl_data->preseed_kernel,
                                            3, //3d global dimension, also worksize
                                            NULL, //no offsets
                                            cl_data->gws_preseed,
                                            cl_data->lws_preseed,
                                            1,
                                            &cl_data->offset_accumulate_finished[buffer_id],
                                            &cl_data->preseed_finished[buffer_id]) );

    // The correlation kernel.
    CHECK_CL_ERROR( clSetKernelArg(cl_data->corr_kernel,
                                        0,
                                        sizeof(void *),
                                        (void*) &cl_data->device_input_buffer[buffer_id]) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->corr_kernel,
                                        1,
                                        sizeof(void *),
                                        (void*) &cl_data->device_output_buffer[buffer_id]) );

    // The correlation kernel.
    CHECK_CL_ERROR( clEnqueueNDRangeKernel(cl_data->queue[1],
                                            cl_data->corr_kernel,
                                            3, 
                                            NULL,
                                            cl_data->gws_corr,
                                            cl_data->lws_corr,
                                            1,
                                            &cl_data->preseed_finished[buffer_id],
                                            &cl_data->corr_finished[buffer_id]) );

    // Read the results
    CHECK_CL_ERROR( clEnqueueReadBuffer(cl_data->queue[2], 
                                            cl_data->device_output_buffer[buffer_id],
                                            CL_FALSE,
                                            0,
                                            cl_data->output_len*sizeof(cl_int),
                                            cl_data->out_buf->data[buffer_id],
                                            1,
                                            &cl_data->corr_finished[buffer_id],
                                            &cl_data->read_finished[buffer_id]) );

    // Setup call back.
    CHECK_CL_ERROR( clSetEventCallback(cl_data->read_finished[buffer_id],
                                            CL_COMPLETE,
                                            &readComplete,
                                            &cl_data->cb_data[buffer_id]) );

    pthread_mutex_unlock(&queue_lock);
}


void setupOpenCL(struct OpenCLData * cl_data)
{
    //remove this
    //saveInputDataCounter = 0;
    //
    cl_int err;

    // Get a platform.
    CHECK_CL_ERROR( clGetPlatformIDs( 1, &cl_data->platform_id, NULL ) );

    // Find a GPU device..
    CHECK_CL_ERROR( clGetDeviceIDs( cl_data->platform_id, CL_DEVICE_TYPE_GPU, MAX_GPUS, cl_data->device_id, NULL) );

    cl_data->context = clCreateContext( NULL, 1, &cl_data->device_id[cl_data->gpu_id], NULL, NULL, &err);
    CHECK_CL_ERROR(err);

    // TODO Move this into a function for just loading kernels.
    // Load kernels and compile them.
    char *cl_file_names[] = {OPENCL_FILENAME_1, OPENCL_FILENAME_2, OPENCL_FILENAME_3};
    char cl_options[] = "";
    size_t cl_program_size[NUM_CL_FILES];
    FILE *fp;
    char *cl_program_buffer[NUM_CL_FILES];

    for (int i = 0; i < NUM_CL_FILES; i++){
        fp = fopen(cl_file_names[i], "r");
        if (fp == NULL){
            ERROR("error loading file: %s", cl_file_names[i]);
            exit(errno);
        }
        fseek(fp, 0, SEEK_END);
        cl_program_size[i] = ftell(fp);
        rewind(fp);
        cl_program_buffer[i] = (char*)malloc(cl_program_size[i]+1);
        cl_program_buffer[i][cl_program_size[i]] = '\0';
        int sizeRead = fread(cl_program_buffer[i], sizeof(char), cl_program_size[i], fp);
        if (sizeRead < cl_program_size[i])
            ERROR("Error reading the file: %s", cl_file_names[i]);
        fclose(fp);
    }

    INFO("%d", NUM_CL_FILES);
    cl_data->program = clCreateProgramWithSource( cl_data->context, NUM_CL_FILES, (const char**)cl_program_buffer, cl_program_size, &err );
    CHECK_CL_ERROR (err);

    for (int i =0; i < NUM_CL_FILES; i++){
        free(cl_program_buffer[i]);
    }

    CHECK_CL_ERROR ( clBuildProgram( cl_data->program, 1, &cl_data->device_id[cl_data->gpu_id], cl_options, NULL, NULL ) );

    cl_data->corr_kernel = clCreateKernel( cl_data->program, "corr", &err );
    CHECK_CL_ERROR(err);

    cl_data->offset_accumulate_kernel = clCreateKernel( cl_data->program, "offsetAccumulateElements", &err );
    CHECK_CL_ERROR(err);

    cl_data->preseed_kernel = clCreateKernel( cl_data->program, "preseed", &err );
    CHECK_CL_ERROR(err);

    // Create command queues
    for (int i = 0; i < NUM_QUEUES; ++i) {
        cl_data->queue[i] = clCreateCommandQueue( cl_data->context, cl_data->device_id[cl_data->gpu_id], CL_QUEUE_PROFILING_ENABLE, &err );
        CHECK_CL_ERROR(err);
    }

    // TODO create a struct to contain all of these (including events) to make this memory allocation cleaner. 
    // 

    // Setup device input buffers
    cl_data->device_input_buffer = (cl_mem *) malloc(cl_data->in_buf->num_buffers * sizeof(cl_mem));
    CHECK_MEM(cl_data->device_input_buffer);
    for (int i = 0; i < cl_data->in_buf->num_buffers; ++i) {
        cl_data->device_input_buffer[i] = clCreateBuffer(cl_data->context, CL_MEM_READ_WRITE, cl_data->in_buf->buffer_size, NULL, &err);
        CHECK_CL_ERROR(err);
    }

    // Setup device accumulate buffers.
    cl_data->device_accumulate_buffer = (cl_mem *) malloc(cl_data->in_buf->num_buffers * sizeof(cl_mem));
    CHECK_MEM(cl_data->device_accumulate_buffer);
    for (int i = 0; i < cl_data->in_buf->num_buffers; ++i) {
        cl_data->device_accumulate_buffer[i] = clCreateBuffer(cl_data->context, CL_MEM_READ_WRITE, cl_data->accumulate_len, NULL, &err);
        CHECK_CL_ERROR(err);
    }

    // Setup device output buffers.
    cl_data->device_output_buffer = (cl_mem *) malloc(cl_data->out_buf->num_buffers * sizeof(cl_mem));
    CHECK_MEM(cl_data->device_output_buffer);
    for (int i = 0; i < cl_data->out_buf->num_buffers; ++i) {
        cl_data->device_output_buffer[i] = clCreateBuffer(cl_data->context, CL_MEM_WRITE_ONLY, cl_data->out_buf->buffer_size, NULL, &err);
        //INFO("buffer_size: %d", cl_data->out_buf->buffer_size);
        CHECK_CL_ERROR(err);
    }

    cl_data->cb_data = malloc(cl_data->in_buf->num_buffers * sizeof(struct call_back_data));
    CHECK_MEM(cl_data->cb_data);

    cl_data->host_buffer_ready = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->host_buffer_ready);

    cl_data->offset_accumulate_finished = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->offset_accumulate_finished);

    cl_data->preseed_finished = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->preseed_finished); 

    cl_data->corr_finished = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->corr_finished);

    cl_data->input_data_written = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->input_data_written);

    cl_data->accumulate_data_zeroed = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->accumulate_data_zeroed);

    cl_data->read_finished = malloc(cl_data->in_buf->num_buffers * sizeof(cl_event));
    CHECK_MEM(cl_data->read_finished);

    // Create lookup tables 
    // TODO move this out of this function?
    // TODO explain these numbers/formulas.
    cl_data->num_blocks = (cl_data->num_antennas / cl_data->block_size) * (cl_data->num_antennas / cl_data->block_size + 1) / 2.;
    cl_data->output_len = cl_data->num_freq*cl_data->num_blocks*(cl_data->block_size*cl_data->block_size)*2.;

    //upper triangular address mapping --converting 1d addresses to 2d addresses
    unsigned int global_id_x_map[cl_data->num_blocks];
    unsigned int global_id_y_map[cl_data->num_blocks];

    //TODO: p260 OpenCL in Action has a clever while loop that changes 1 D addresses to X & Y indices for an upper triangle.  
    // Time Test kernels using them compared to the lookup tables for NUM_ELEM = 256
    int largest_num_blocks_1D = cl_data->num_antennas/cl_data->block_size;
    int index_1D = 0;
    for (int j = 0; j < largest_num_blocks_1D; j++){
        for (int i = j; i < largest_num_blocks_1D; i++){
            global_id_x_map[index_1D] = i;
            global_id_y_map[index_1D] = j;
            index_1D++;
        }
    }


    cl_mem id_x_map = clCreateBuffer(cl_data->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    cl_data->num_blocks * sizeof(cl_uint), global_id_x_map, &err);
    if (err){
        printf("Error in clCreateBuffer %i\n", err);
    }

    cl_mem id_y_map = clCreateBuffer(cl_data->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                    cl_data->num_blocks * sizeof(cl_uint), global_id_y_map, &err);
    if (err){
        printf("Error in clCreateBuffer %i\n", err);
    }

    //set other parameters that will be fixed for the kernels (changeable parameters will be set in run loops)
    CHECK_CL_ERROR( clSetKernelArg(cl_data->corr_kernel, 
                                   2,
                                   sizeof(id_x_map),
                                   (void*) &id_x_map) ); //this should maybe be sizeof(void *)?

    CHECK_CL_ERROR( clSetKernelArg(cl_data->corr_kernel, 
                                   3,
                                   sizeof(id_y_map),
                                   (void*) &id_y_map) );
    CHECK_CL_ERROR( clSetKernelArg(cl_data->corr_kernel,
                                   4,
                                   8*8*4 * sizeof(cl_uint),
                                   NULL) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                   2,
                                   sizeof(id_x_map),
                                   (void*) &id_x_map) ); //this should maybe be sizeof(void *)?

    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                   3,
                                   sizeof(id_y_map),
                                   (void*) &id_y_map) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                   4,
                                   64* sizeof(cl_uint),
                                   NULL) );

    CHECK_CL_ERROR( clSetKernelArg(cl_data->preseed_kernel,
                                   5,
                                   64* sizeof(cl_uint),
                                   NULL) );

    // Number of compressed accumulations.
    cl_data->num_accumulations = cl_data->num_iterations/256;

    // Accumulation kernel global and local work space sizes.
    cl_data->gws_accum[0] = 64;
    cl_data->gws_accum[1] = (int)ceil(cl_data->num_antennas*cl_data->num_freq/256.0); 
    cl_data->gws_accum[2] = cl_data->num_iterations/1024;

    cl_data->lws_accum[0] = 64;
    cl_data->lws_accum[1] = 1;
    cl_data->lws_accum[2] = 1;

    // Pre-seed kernel global and local work space sizes.
    cl_data->gws_preseed[0] = 8;
    cl_data->gws_preseed[1] = 8*cl_data->num_freq;
    cl_data->gws_preseed[2] = cl_data->num_blocks;

    cl_data->lws_preseed[0] = 8;
    cl_data->lws_preseed[1] = 8;
    cl_data->lws_preseed[2] = 1;

    // Correlation kernel global and local work space sizes.
    cl_data->gws_corr[0] = 8;
    cl_data->gws_corr[1] = 8*cl_data->num_freq;
    cl_data->gws_corr[2] = cl_data->num_blocks*cl_data->num_accumulations;

    cl_data->lws_corr[0] = 8;
    cl_data->lws_corr[1] = 8;
    cl_data->lws_corr[2] = 1;

    // Array used to zero the output memory on the device.
    // TODO should this be in it's own function?
    int aligned_accumulate_len = PAGESIZE_MEM * (ceil((double)cl_data->accumulate_len / (double)PAGESIZE_MEM));
    INFO ("aligned: %d, accumulate_len: %d", aligned_accumulate_len, cl_data->accumulate_len);
    err = posix_memalign((void **) &cl_data->accumulate_zeros, PAGESIZE_MEM, aligned_accumulate_len);

    if ( err != 0 ) {
        ERROR("Error creating aligned memory for accumulate zeros");
        exit(err);
    }

    // Ask that all pages be kept in memory
    err = mlock((void *) cl_data->accumulate_zeros, aligned_accumulate_len);

    if ( err == -1 ) {
        ERROR("Error locking memory - check ulimit -a to check memlock limits");
        exit(errno);
    }

    memset(cl_data->accumulate_zeros, 0, aligned_accumulate_len );
}

void closeOpenCL(struct OpenCLData * cl_data)
{
    CHECK_CL_ERROR( clReleaseKernel(cl_data->corr_kernel) );
    CHECK_CL_ERROR( clReleaseProgram(cl_data->program) );

    for (int i = 0; i < NUM_QUEUES; ++i) {
        CHECK_CL_ERROR( clReleaseCommandQueue(cl_data->queue[i]) );
    }

    for (int i = 0; i < cl_data->in_buf->num_buffers; ++i) {
        CHECK_CL_ERROR( clReleaseMemObject(cl_data->device_input_buffer[i]) );
    }
    free(cl_data->device_input_buffer);

    for (int i = 0; i < cl_data->in_buf->num_buffers; ++i) {
        CHECK_CL_ERROR( clReleaseMemObject(cl_data->device_accumulate_buffer[i]) );
    }
    free(cl_data->device_accumulate_buffer);

    for (int i = 0; i < cl_data->out_buf->num_buffers; ++i) {
        CHECK_CL_ERROR( clReleaseMemObject(cl_data->device_output_buffer[i]) );
    }
    free(cl_data->device_output_buffer);

    free(cl_data->host_buffer_ready);
    free(cl_data->host_buffer_ready);
    free(cl_data->input_data_written);
    free(cl_data->accumulate_data_zeroed);
    free(cl_data->read_finished);
    free(cl_data->corr_finished);
    free(cl_data->offset_accumulate_finished);
    free(cl_data->preseed_finished);

    free(cl_data->cb_data);

    CHECK_CL_ERROR( clReleaseContext(cl_data->context) );
}
