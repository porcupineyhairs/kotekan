#include "input_data_stage.h"

input_data_stage::input_data_stage(const char* param_name, Config &param_config, const string &unique_name):
    clCommand(param_name, param_config, unique_name)
{

}

input_data_stage::~input_data_stage()
{

}

void input_data_stage::apply_config(const uint64_t& fpga_seq) {
    clCommand::apply_config(fpga_seq);
}

void input_data_stage::build(device_interface &param_Device)
{
    apply_config(0);
    clCommand::build(param_Device);
    data_staged_event = (cl_event *)malloc(param_Device.getInBuf()->num_frames * sizeof(cl_event));
    CHECK_MEM(data_staged_event);
}

cl_event input_data_stage::execute(int param_bufferID, const uint64_t& fpga_seq, device_interface& param_Device, cl_event param_PrecedeEvent)
{
    clCommand::execute(param_bufferID, 0, param_Device, param_PrecedeEvent);

    // Data transfer to GPU
    CHECK_CL_ERROR( clEnqueueWriteBuffer(param_Device.getQueue(0),
                                            param_Device.getInputBuffer(param_bufferID),
                                            CL_FALSE,
                                            0, //offset
                                            param_Device.getInBuf()->aligned_frame_size,
                                            param_Device.getInBuf()->frames[param_bufferID],
                                            0,
                                            NULL,
                                            &data_staged_event[param_bufferID]) );

    CHECK_CL_ERROR( clEnqueueWriteBuffer(param_Device.getQueue(0),
                                            param_Device.getAccumulateBuffer(param_bufferID),
                                            CL_FALSE,
                                            0,
                                            param_Device.getAlignedAccumulateLen(),
                                            param_Device.getAccumulateZeros(),
                                            1,
                                            &data_staged_event[param_bufferID],
                                            &postEvent[param_bufferID]) );

    return postEvent[param_bufferID];
}

void input_data_stage::cleanMe(int param_BufferID)
{
    clCommand::cleanMe(param_BufferID);

    assert(data_staged_event[param_BufferID] != NULL);

    clReleaseEvent(data_staged_event[param_BufferID]);

}

void input_data_stage::freeMe()
{
    clCommand::freeMe();
    free(data_staged_event);
}

