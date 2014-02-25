#include "network_dna.h"
#include "buffers.h"
#include "pfring.h"
#include "errors.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <memory.h>
#include <unistd.h>
#include <assert.h>

//#define UDP_PACKETSIZE 8256
#define UDP_PACKETSIZE 9296
#define UDP_PAYLOADSIZE 8192

// Count max
#define COUNTER_BITS 30
#define COUNTER_MAX (1ll << COUNTER_BITS) - 1ll

#define SEQ_NUM_EDGE 100000

double e_time(void) {
    static struct timeval now;
    gettimeofday(&now, NULL);
    return (double)(now.tv_sec  + now.tv_usec/1000000.0);
}

void check_if_done(int * total_buffers_filled, struct network_thread_arg * args,
                    long long int total_packets, int32_t total_lost, int32_t total_out_of_order, 
                   int32_t total_duplicate, double start_time) {

    (*total_buffers_filled)++;
    if ( (*total_buffers_filled) * (args->buf->buffer_size / (1024*1024)) >= args->data_limit * 1024) {
        double end_time = e_time();
        printf("Stopping packet capture, ran for ~ %f seconds.\n", end_time - start_time);
        printf("\nStats:\nTotal Packets Captured: %lld\nPackets lost: %d\nOut of order packets: %d\nDuplicate Packets: %d\n", 
                total_packets, total_lost, total_out_of_order, total_duplicate);
        markProducerDone(args->buf, args->link_id);
        int ret = 0;
        pthread_exit((void *) &ret);
    }
}


//enumerations/definitions: don't change
#define GENERATE_DATASET_CONSTANT       1u
#define GENERATE_DATASET_RAMP_UP        2u
#define GENERATE_DATASET_RAMP_DOWN      3u
#define GENERATE_DATASET_RANDOM_SEEDED  4u
#define ALL_FREQUENCIES                -1

//parameters for data generator: you can change these. (Values will be shifted and clipped as needed, so these are signed 4bit numbers for input)
#define GEN_TYPE                        GENERATE_DATASET_CONSTANT
#define GEN_DEFAULT_SEED                42u
#define GEN_DEFAULT_RE                  0u
#define GEN_DEFAULT_IM                  0u
#define GEN_INITIAL_RE                  0
#define GEN_INITIAL_IM                  0
#define GEN_FREQ                        ALL_FREQUENCIES
#define GEN_REPEAT_RANDOM               1u

#define CHECKING_VERBOSE                0u

int offset_and_clip_value(int input_value, int offset_value, int min_val, int max_val){
    int offset_and_clipped = input_value + offset_value;
    if (offset_and_clipped > max_val)
        offset_and_clipped = max_val;
    else if (offset_and_clipped < min_val)
        offset_and_clipped = min_val;
    return(offset_and_clipped);
}


void generate_char_data_set(int generation_Type,
                            int random_seed,
                            int default_real,
                            int default_imaginary,
                            int initial_real,
                            int initial_imaginary,
                            int single_frequency,
                            int num_timesteps,
                            int num_frequencies,
                            int num_elements,
                            unsigned char *packed_data_set){

    //sfmt_t sfmt; //for the Mersenne Twister
    if (single_frequency > num_frequencies || single_frequency < 0)
        single_frequency = ALL_FREQUENCIES;

    //printf("single_frequency: %d \n",single_frequency);
    default_real =offset_and_clip_value(default_real,8,0,15);
    default_imaginary = offset_and_clip_value(default_imaginary,8,0,15);
    initial_real = offset_and_clip_value(initial_real,8,0,15);
    initial_imaginary = offset_and_clip_value(initial_imaginary,8,0,15);
    unsigned char clipped_offset_default_real = (unsigned char) default_real;
    unsigned char clipped_offset_default_imaginary = (unsigned char) default_imaginary;
    unsigned char clipped_offset_initial_real = (unsigned char) initial_real;
    unsigned char clipped_offset_initial_imaginary = (unsigned char) initial_imaginary;

    //printf("clipped_offset_initial_real: %d, clipped_offset_initial_imaginary: %d, clipped_offset_default_real: %d, clipped_offset_default_imaginary: %d\n", clipped_offset_initial_real, clipped_offset_initial_imaginary, clipped_offset_default_real, clipped_offset_default_imaginary);

    if (generation_Type == GENERATE_DATASET_RANDOM_SEEDED){
        //sfmt_init_gen_rand(&sfmt, random_seed);
        srand(random_seed);
    }

    for (int k = 0; k < num_timesteps; k++){
        //printf("k: %d\n",k);
        if (generation_Type == GENERATE_DATASET_RANDOM_SEEDED && GEN_REPEAT_RANDOM){
            //sfmt_init_gen_rand(&sfmt, random_seed);
            srand(random_seed);
        }
        for (int j = 0; j < num_frequencies; j++){
            //printf("j: %d\n",j);
            for (int i = 0; i < num_elements; i++){
                int currentAddress = k*num_frequencies*num_elements + j*num_elements + i;
                unsigned char new_real;
                unsigned char new_imaginary;
                switch (generation_Type){
                    case GENERATE_DATASET_CONSTANT:
                        new_real = clipped_offset_initial_real;
                        new_imaginary = clipped_offset_initial_imaginary;
                        break;
                    case GENERATE_DATASET_RAMP_UP:
                        new_real = (j+clipped_offset_initial_real+i)%16;
                        new_imaginary = (j+clipped_offset_initial_imaginary+i)%16;
                        break;
                    case GENERATE_DATASET_RAMP_DOWN:
                        new_real = 15-((j+clipped_offset_initial_real+i)%16);
                        new_imaginary = 15 - ((j+clipped_offset_initial_imaginary+i)%16);
                        break;
                    case GENERATE_DATASET_RANDOM_SEEDED:
                        new_real = (unsigned char)rand()%16; //to put the pseudorandom value in the range 0-15
                        new_imaginary = (unsigned char)rand()%16;
                        break;
                    default: //shouldn't happen, but in case it does, just assign the default values everywhere
                        new_real = clipped_offset_default_real;
                        new_imaginary = clipped_offset_default_imaginary;
                        break;
                }

                if (single_frequency == ALL_FREQUENCIES){
                    packed_data_set[currentAddress] = ((new_real<<4) & 0xF0) + (new_imaginary & 0x0F);
                }
                else{
                    if (j == single_frequency)
                        packed_data_set[currentAddress] = ((new_real<<4) & 0xF0) + (new_imaginary & 0x0F);
                    else
                        packed_data_set[currentAddress] = ((clipped_offset_default_real<<4) & 0xF0) + (clipped_offset_default_imaginary & 0x0F);
                }
                //printf("%d ",data_set[currentAddress]);
            }
        }
    }

    return;
}


void network_thread(void * arg) {
/*
    struct network_thread_arg * args;
    args = (struct network_thread_arg *) arg;
    int buffer_id = args->link_id;
    int data_id = 0;
    
    
    // Make sure the first buffer is ready to go. (this check is not really needed)
    waitForEmptyBuffer(args->buf, buffer_id);

    setDataID(args->buf, buffer_id, data_id++);
    
    generate_char_data_set(GEN_TYPE,GEN_DEFAULT_SEED,GEN_DEFAULT_RE, GEN_DEFAULT_IM,GEN_INITIAL_RE,GEN_INITIAL_IM,GEN_FREQ, 16*1024, 126, 16, (unsigned char *) args->buf->data[buffer_id]);
    
    for (int i = 0; i < 100; i++) {
        INFO("%X", *(unsigned int *)(&args->buf->data[buffer_id][i*4]) );
    }
    
    markBufferFull(args->buf, buffer_id);
    
    markProducerDone(args->buf, args->link_id);
    int ret = 0;
    pthread_exit((void *) &ret);
    
    */

    struct network_thread_arg * args;
    args = (struct network_thread_arg *) arg;

    // Setup the PF_RING.
    pfring *pd;
    pd = pfring_open(args->ip_address, UDP_PACKETSIZE, PF_RING_PROMISC );
    
    

    if(pd == NULL) {
        ERROR("pfring_open error [%s] (pf_ring not loaded or quick mode is enabled you and have already a socket bound to %s?)\n",
            strerror(errno), args->ip_address);
        exit(EXIT_FAILURE);
    }

    pfring_set_application_name(pd, "gpu_correlator");

    if (pd->dna.dna_mapped_device == 0) {
        ERROR("The device is not in DNA mode.?");
    }

    pfring_set_poll_duration(pd, 1000);
    pfring_set_poll_watermark(pd, 10000);

    if (pfring_enable_ring(pd) != 0) {
        ERROR("Cannot enable the PF_RING.");
        exit(EXIT_FAILURE);
    }

    uint64_t count = 0;
    double last_time = e_time();
    double current_time = e_time();
    int64_t seq = 0;
    int64_t last_seq = -1;
    int64_t diff = 0;
    int64_t total_lost = 0;
    int64_t grand_total_lost = 0;
    int64_t lost = 0;
    int64_t total_out_of_order = 0;
    int64_t total_duplicate = 0;
    long long int total_packets = 0;

    int buffer_location = 0;
    int buffer_id = args->link_id;
    int data_id = 0;
    int total_buffers_filled = 0;

    int64_t out_of_order_event = 0;

    // Make sure the first buffer is ready to go. (this check is not really needed)
    waitForEmptyBuffer(args->buf, buffer_id);

    setDataID(args->buf, buffer_id, data_id++);

    double start_time = -1;

    for (;;) {

        assert (buffer_location <= args->buf->buffer_size);

        // See if we need to get a new buffer
        if (buffer_location == args->buf->buffer_size) {

            // Notify the we have filled a buffer.
            markBufferFull(args->buf, buffer_id);
            pthread_yield();

            // Check if we should stop collecting data.
            check_if_done(&total_buffers_filled, args, total_packets, 
                        grand_total_lost, total_out_of_order, total_duplicate, start_time);

            buffer_id = (buffer_id + args->numLinks) % (args->buf->num_buffers);

            // This call will block if the buffer has not been written out to disk yet.
            waitForEmptyBuffer(args->buf, buffer_id);

            buffer_location = 0;

            setDataID(args->buf, buffer_id, data_id++);
            if (last_seq != -1) {
                // If not the first packet we need to set BufferInfo data.
                set_fpga_seq_num(args->buf, buffer_id, last_seq + 1);
                // TODO This is close, but not perfect timing - but this shouldn't really matter.
                static struct timeval now;
                gettimeofday(&now, NULL);
                set_first_packet_recv_time(args->buf, buffer_id, now);
            }
        }

        struct pfring_pkthdr pf_header;
        u_char *pkt_buf;
        int rc = pfring_recv(pd, &pkt_buf, 0, &pf_header, 1);
        if (rc <= 0) {
            // No packets available.
            if (rc < 0) { fprintf(stderr,"Error in pfring_recv! %d\n", rc); }
            pthread_yield();
            continue;
        }

        if (pf_header.len != UDP_PACKETSIZE) {
            fprintf(stderr,"Got wrong sized packet with len: %d", pf_header.len);
            continue;
        }

        // Do seq number related stuff (location will change.)
        seq = ((((uint32_t *) &pkt_buf[54])[0]) + 0 ) >> 2;
        //INFO("seq: %u", seq);

        // First packet alignment code.
        if (last_seq == -1) {

            if ( !( (seq % SEQ_NUM_EDGE) <= 10 && (seq % SEQ_NUM_EDGE) >= 0 ) ) {
                continue;
            }

            INFO("Got first packet %d", seq);
            // Set the time we got the first packet.
            static struct timeval now;
            gettimeofday(&now, NULL);
            set_first_packet_recv_time(args->buf, buffer_id, now);
            set_fpga_seq_num(args->buf, buffer_id, seq - seq % SEQ_NUM_EDGE);

            // Time for internal counters.
            start_time = e_time();

            // TODO This is only correct with high probability,
            // this should be made deterministic. 
            if (seq % SEQ_NUM_EDGE ==  0) {
                last_seq = seq;
                memcpy(&args->buf->data[buffer_id][buffer_location], pkt_buf + 58, UDP_PAYLOADSIZE);
                count++;
                buffer_location += UDP_PAYLOADSIZE;
            } else {
                // If we have lost the packet on the edge,
                // we set the last_seq to the edge so that the buffer will still be aligned.
                // We also ignore the current packet, and just allow it to be lost for simplicity.
                last_seq = seq - seq % SEQ_NUM_EDGE;
            }
            continue;
        }


        memcpy(&args->buf->data[buffer_id][buffer_location], pkt_buf + 58, UDP_PAYLOADSIZE);

        //INFO("seq_num: %d", seq);

        // If this is the first packet, we don't need to check the later cases,
        // just move the buffer location, and continue.
        if (last_seq == -1) {
            last_seq = seq;
            count++;
            buffer_location += UDP_PAYLOADSIZE;
            continue;
        }

        if (seq == last_seq) {
            total_duplicate++;
            // Do nothing in this case, because if the buffer_location doesn't change, 
            // we over write this duplicate with the next packet.
            // We continue since we don't count this as a reciveved packet.
            continue;
        }

        if ( (seq < last_seq && last_seq - seq > 1ll << (COUNTER_BITS - 1ll) ) 
                    || (seq > last_seq && seq - last_seq < 1ll << (COUNTER_BITS - 1ll) ) ) {
            // See RFC 1982 for above statement details. 
            // Result: seq follows last_seq if above is true.
            // This is the most common case.

            // Compute the true distance between seq numbers, and packet loss (if any).
            diff = seq - last_seq;
            if (diff < 0) {
                diff += COUNTER_MAX + 1ll;
            }

            lost = diff - 1ll;
            total_lost += lost;

            // We have packet loss, we have two cases.
            // Case 1:  There is room in the buffer to move the data we put in the
            // wrong place.  So we move our data, and zero out the missing values.
            // Case 2:  We ended up in a new buffer...  So we need to zero the value
            // we recorded, and zero the values in the next buffer(s), upto the point we want to
            // start writing new data.  Note in this case we zero out even the last packet that we
            // read.  So losses over a buffer edge result in one extra "lost" packet. 
            if ( lost > 0 ) {

                if (lost > 1000000) {
                    ERROR("Packet loss is very high! lost packets: %lld\n", (long long int)lost);
                }

                // The location the packet should have been in.
                int realPacketLocation = buffer_location + lost * UDP_PAYLOADSIZE;
                if ( realPacketLocation < args->buf->buffer_size ) { // Case 1:
                    // Copy the memory in the right location.
                    assert(buffer_id < args->buf->num_buffers);
                    assert(realPacketLocation <= args->buf->buffer_size - UDP_PAYLOADSIZE);
                    assert(buffer_location <= args->buf->buffer_size - UDP_PAYLOADSIZE );
                    assert(buffer_id >= 0);
                    assert(buffer_location >= 0);
                    assert(realPacketLocation >= 0);
                    memcpy((void *) &args->buf->data[buffer_id][realPacketLocation], 
                            (void *) &args->buf->data[buffer_id][buffer_location], UDP_PAYLOADSIZE);

                    // Zero out the lost part of the buffer.
                    for (int i = 0; i < lost; ++i) {
                        memset(&args->buf->data[buffer_id][buffer_location + i*UDP_PAYLOADSIZE], 0x88, UDP_PAYLOADSIZE);
                    }

                    buffer_location = realPacketLocation + UDP_PAYLOADSIZE;

                    //ERROR("Case 1 packet loss event on data_id: %d", data_id);
                } else { // Case 2 (the hard one):

                    // zero out the rest of the current buffer and mark it as full.
                    int i, j;
                    for (i = 0; (buffer_location + i*UDP_PAYLOADSIZE) < args->buf->buffer_size; ++i) {
                        memset(&args->buf->data[buffer_id][buffer_location + i*UDP_PAYLOADSIZE], 0x88, UDP_PAYLOADSIZE);
                    }

                    // Keep track of the last edge seq number in case we need it later.
                    uint32_t last_edge = get_fpga_seq_num(args->buf, buffer_id);

                    // Notify the we have filled a buffer.
                    markBufferFull(args->buf, buffer_id);

                    // Check if we should stop collecting data.
                    check_if_done(&total_buffers_filled, args, total_packets, 
                                grand_total_lost, total_out_of_order, total_duplicate, start_time);

                    // Get the number of lost packets in the new buffer(s).
                    int num_lost_packets_new_buf = (lost+1) - (args->buf->buffer_size - buffer_location)/UDP_PAYLOADSIZE;

                    assert(num_lost_packets_new_buf > 0);

                    i = 0;
                    j = 1;

                    // We may have lost more packets than will fit in a buffer.
                    do {

                        // Get a new buffer.
                        buffer_id = (buffer_id + args->numLinks) % (args->buf->num_buffers);

                        // This call will block if the buffer has not been written out to disk yet
                        // shouldn't be an issue if everything runs correctly.
                        waitForEmptyBuffer(args->buf, buffer_id);

                        setDataID(args->buf, buffer_id, data_id++);
                        uint32_t fpga_seq_number = last_edge + j * (args->buf->buffer_size/UDP_PAYLOADSIZE); // == number of iterations FIXME.

                        set_fpga_seq_num(args->buf, buffer_id, fpga_seq_number);

                        // This really isn't the correct time, but this is the best we can do here.
                        struct timeval now;
                        gettimeofday(&now, NULL);
                        set_first_packet_recv_time(args->buf, buffer_id, now);

                        for (i = 0; num_lost_packets_new_buf > 0 && (i*UDP_PAYLOADSIZE) < args->buf->buffer_size; ++i) {
                            memset(&args->buf->data[buffer_id][i*UDP_PAYLOADSIZE], 0x88, UDP_PAYLOADSIZE);
                            num_lost_packets_new_buf--;
                        }

                        // Check if we need to run another iteration of the loop.
                        // i.e. get another buffer.
                        if (num_lost_packets_new_buf > 0) {

                            // Notify the we have filled a buffer.
                            markBufferFull(args->buf, buffer_id);
                            pthread_yield();

                            // Check if we should stop collecting data.
                            check_if_done(&total_buffers_filled, args, total_packets, 
                                        grand_total_lost, total_out_of_order, total_duplicate, start_time);

                        }
                        j++;

                    } while (num_lost_packets_new_buf > 0);

                    // Update the new buffer location.
                    buffer_location = i*UDP_PAYLOADSIZE;

                    // We need to increase the total number of lost packets since we 
                    // just tossed away the packet we read.
                    total_lost++;
                    printf("Case 2 packet loss event on data_id: %d\n", data_id);

                }
            } else {
                // This is the normal case; a valid packet. 
                buffer_location += UDP_PAYLOADSIZE;
            }

        } else {
            // seq is before last_seq.  We have an out of order packet.

            total_out_of_order++;

            if (out_of_order_event == 0 || out_of_order_event != last_seq) {
                ERROR("Out of order event in data_id: %d\n", data_id);
                out_of_order_event = last_seq;
            }

            // In this case, we could write the out of order packet into the right location,
            // upto buffer edge issues. 
            // However at the moment, we are just ignoring it, since that location will already
            // have been zeroed out as a lost packet, or written if this is a late duplicate.
            // We don't advance the buffer location, so that this location is overwritten.

            // Continue so we don't update last_seq, or count.
            continue;
        }

        last_seq = seq;

        // Compute speed at packet loss every X packets
        count++;
        total_packets++;
        static const int X = 10*39062;
        if (count % (X+1) == 0) {
            current_time = e_time();
            INFO("Receive Speed: %1.3f Gbps %.0f pps\n", (((double)X*UDP_PACKETSIZE*8) / (current_time - last_time)) / (1024*1024*1024), X / (current_time - last_time) );
            last_time = current_time;
            if (total_lost != 0) {
                INFO("Packet loss: %.6f%%\n", ((double)total_lost/(double)X)*100); 
            } else {
                INFO("Packet loss: %.6f%%\n", (double)0.0);
            }
            grand_total_lost += total_lost;
            total_lost = 0;

            INFO("Data received: %.2f GB -- ", ((double)total_buffers_filled * ((double)args->buf->buffer_size / (1024.0*1024.0)))/1024.0);
            INFO("Number of full buffers: %d\n", getNumFullBuffers(args->buf));
        }
    }
    
    
}
