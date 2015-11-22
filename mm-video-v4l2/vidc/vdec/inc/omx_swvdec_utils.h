/**
 * @copyright
 *
 *   Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE DISCLAIMED.
 *   IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *   DAMAGE.
 *
 * @file
 *
 *   omx_swvdec_utils.h
 *
 * @brief
 *
 *   OMX software video decoder utility functions header.
 */

#ifndef _OMX_SWVDEC_UTILS_H_
#define _OMX_SWVDEC_UTILS_H_

#include <pthread.h>

#include <cutils/log.h>

extern unsigned int g_omx_swvdec_logmask;
                      ///< global OMX SwVdec logmask variable extern declaration

void omx_swvdec_log_init();

#define OMX_SWVDEC_LOGMASK_LOW   4 ///< 100: logmask for low priority logs
#define OMX_SWVDEC_LOGMASK_HIGH  2 ///< 010: logmask for high priority logs
#define OMX_SWVDEC_LOGMASK_ERROR 1 ///< 001: logmask for error priority logs

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "OMX_SWVDEC" ///< OMX SwVdec log tag

/// low priority log message
#define OMX_SWVDEC_LOG_LOW(string, ...)                              \
    do {                                                             \
        if (g_omx_swvdec_logmask & OMX_SWVDEC_LOGMASK_LOW)           \
            ALOGD("--- %s(): " string, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

/// high priority log message
#define OMX_SWVDEC_LOG_HIGH(string, ...)                             \
    do {                                                             \
        if (g_omx_swvdec_logmask & OMX_SWVDEC_LOGMASK_HIGH)          \
            ALOGI("--- %s(): " string, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

/// error priority log message
#define OMX_SWVDEC_LOG_ERROR(string, ...)                            \
    do {                                                             \
        if (g_omx_swvdec_logmask & OMX_SWVDEC_LOGMASK_ERROR)         \
            ALOGE("!!! %s(): " string, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

/// high priority log message for OMX SwVdec API calls
#define OMX_SWVDEC_LOG_API(string, ...)                              \
    do {                                                             \
        if (g_omx_swvdec_logmask & OMX_SWVDEC_LOGMASK_HIGH)          \
            ALOGI(">>> %s(): " string, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

/// high priority log message for OMX SwVdec callbacks
#define OMX_SWVDEC_LOG_CALLBACK(string, ...)                         \
    do {                                                             \
        if (g_omx_swvdec_logmask & OMX_SWVDEC_LOGMASK_HIGH)          \
            ALOGI("<<< %s(): " string, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define OMX_SWVDEC_QUEUE_ELEMENTS 100 ///< number of elements in queue

/// OMX SwVdec event information structure
typedef struct {
    unsigned long event_id;     ///< event ID
    unsigned long event_param1; ///< event parameter 1
    unsigned long event_param2; ///< event parameter 2
} OMX_SWVDEC_EVENT_INFO;

/// OMX SwVdec queue class
class omx_swvdec_queue
{
public:
    omx_swvdec_queue();
    ~omx_swvdec_queue();

    bool push(OMX_SWVDEC_EVENT_INFO *p_event_info);
    bool pop(OMX_SWVDEC_EVENT_INFO *p_event_info);

private:
    OMX_SWVDEC_EVENT_INFO m_queue[OMX_SWVDEC_QUEUE_ELEMENTS];
                                          ///< event queue
    unsigned int          m_count_total;  ///< count of total elements
    unsigned int          m_count_filled; ///< count of filled elements
    unsigned int          m_index_write;  ///< queue index for writing
    unsigned int          m_index_read;   ///< queue index for reading
    pthread_mutex_t       m_mutex;        ///< mutex for queue access
};

#define OMX_SWVDEC_TS_LIST_ELEMENTS 100
                                       ///< number of elements in timestamp list

/// OMX SwVdec timestamp element structure.
typedef struct {
    bool      filled;    ///< element filled?
    long long timestamp; ///< timestamp
} OMX_SWVDEC_TS_ELEMENT;

/// OMX SwVdec timestamp list class
class omx_swvdec_ts_list
{
public:
    omx_swvdec_ts_list();
    ~omx_swvdec_ts_list();

    void reset();
    bool push(long long timestamp);
    bool pop(long long *p_timestamp);

private:
    OMX_SWVDEC_TS_ELEMENT m_list[OMX_SWVDEC_TS_LIST_ELEMENTS];
                                          ///< list of timestamp elements
    int                   m_count_filled; ///< count of filled elements
    pthread_mutex_t       m_mutex;        ///< mutex for list access
};

#define DIAG_FILENAME_IP "/data/misc/media/input.bin"  ///<  input filename
#define DIAG_FILENAME_OP "/data/misc/media/output.yuv" ///< output filename

/// OMX SwVdec diagnostics class
class omx_swvdec_diag
{
public:
    omx_swvdec_diag();
    ~omx_swvdec_diag();

    void dump_ip(unsigned char *p_buffer, unsigned int   filled_length);
    void dump_op(unsigned char *p_buffer,
                 unsigned int   width,
                 unsigned int   height,
                 unsigned int   stride,
                 unsigned int   scanlines);

private:
    unsigned int m_dump_ip; ///< dump input bitstream
    unsigned int m_dump_op; ///< dump output YUV

    char *m_filename_ip; ///<  input filename string
    char *m_filename_op; ///< output filename string

    FILE *m_file_ip; ///<  input file handle
    FILE *m_file_op; ///< output file handle
};

#endif // #ifndef _OMX_SWVDEC_UTILS_H_
