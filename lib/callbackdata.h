/*
 * Copyright (c) 2015 <copyright holder> <email>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef CALLBACKDATA_H
#define CALLBACKDATA_H

#include "gpu_command.h"
#include "buffers.h"
#include "pthread.h"

struct loopCounter {
    loopCounter(){      
        CHECK_ERROR( pthread_mutex_init(&lock, NULL) );
        CHECK_ERROR( pthread_cond_init(&cond, NULL) );
    }
    ~loopCounter() {
        CHECK_ERROR( pthread_mutex_destroy(&lock) );
        CHECK_ERROR( pthread_cond_destroy(&cond) );
    }
    int iteration=0;

    pthread_mutex_t lock;  // Lock for the is_ready function.
    pthread_cond_t cond;
};
class callBackData {
public:
    callBackData();
    callBackData(cl_uint param_NumCommand);
    callBackData(callBackData &cb);
    ~callBackData();
    int buffer_id;
    int numCommands;
    
    gpu_command ** listCommands;

    // Buffer objects
    struct Buffer * in_buf;
    struct Buffer * out_buf;
    
    struct loopCounter * cnt;
};

#endif // CALLBACKDATA_H
