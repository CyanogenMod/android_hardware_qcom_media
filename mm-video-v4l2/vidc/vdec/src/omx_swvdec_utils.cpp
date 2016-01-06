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
 *   omx_swvdec_utils.cpp
 *
 * @brief
 *
 *   OMX software video decoder utility functions source.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <cutils/properties.h>

#include "omx_swvdec_utils.h"

#define OMX_SWVDEC_LOGLEVEL_DEFAULT 2 ///< default OMX SwVdec loglevel

unsigned int g_omx_swvdec_logmask = (1 << OMX_SWVDEC_LOGLEVEL_DEFAULT) - 1;
                              ///< global OMX SwVdec logmask variable definition

/**
 * @brief Initialize OMX SwVdec log level & mask.
 */
void omx_swvdec_log_init()
{
    int omx_swvdec_loglevel = OMX_SWVDEC_LOGLEVEL_DEFAULT;

    char property_value[PROPERTY_VALUE_MAX] = {0};

    if (property_get("omx_swvdec.log.level", property_value, NULL))
    {
        omx_swvdec_loglevel = atoi(property_value);

        if (omx_swvdec_loglevel > 3)
            omx_swvdec_loglevel = 3;
        if (omx_swvdec_loglevel < 0)
            omx_swvdec_loglevel = 0;

        OMX_SWVDEC_LOG_LOW(
            "omx_swvdec.log.level: %d; %s",
            omx_swvdec_loglevel,
            (omx_swvdec_loglevel == 3) ? "error, high, & low logs" :
            ((omx_swvdec_loglevel == 2) ? "error & high logs" :
             ((omx_swvdec_loglevel == 1) ? "error logs" :
              "no logs")));
    }

    g_omx_swvdec_logmask = (unsigned int) ((1 << omx_swvdec_loglevel) - 1);
}

/**
 * @brief OMX SwVdec queue constructor.
 */
omx_swvdec_queue::omx_swvdec_queue()
{
    memset(m_queue, 0, sizeof(m_queue));

    m_count_total  = OMX_SWVDEC_QUEUE_ELEMENTS;
    m_count_filled = 0;
    m_index_write  = 0;
    m_index_read   = 0;

    pthread_mutex_init(&m_mutex, NULL);
}

/**
 * @brief OMX SwVdec queue destructor.
 */
omx_swvdec_queue::~omx_swvdec_queue()
{
    pthread_mutex_destroy(&m_mutex);
}

/**
 * @brief Push event to queue.
 *
 * @param[in] p_event_info: Pointer to event information structure.
 *
 * @retval  true if push successful
 * @retval false if push unsuccessful
 */
bool omx_swvdec_queue::push(OMX_SWVDEC_EVENT_INFO *p_event_info)
{
    bool retval = true;

    pthread_mutex_lock(&m_mutex);

    if (m_count_filled < m_count_total)
    {
        m_queue[m_index_write] = *p_event_info;

        m_index_write = (m_index_write + 1) % m_count_total;
        m_count_filled++;
    }
    else
    {
        retval = false;
    }

    pthread_mutex_unlock(&m_mutex);

    return retval;
}

/**
 * @brief Pop event from queue.
 *
 * @param[in,out] p_event_info: Pointer to event information structure.
 *
 * @retval  true if pop successful
 * @retval false if pop unsuccessful
 */
bool omx_swvdec_queue::pop(OMX_SWVDEC_EVENT_INFO *p_event_info)
{
    bool retval = true;

    pthread_mutex_lock(&m_mutex);

    if (m_count_filled > 0)
    {
        *p_event_info = m_queue[m_index_read];

        memset(&m_queue[m_index_read], 0, sizeof(OMX_SWVDEC_EVENT_INFO));

        m_index_read = (m_index_read + 1) % m_count_total;
        m_count_filled--;
    }
    else
    {
        retval = false;
    }

    pthread_mutex_unlock(&m_mutex);

    return retval;
}

/**
 * @brief OMX SwVdec timestamp list constructor.
 */
omx_swvdec_ts_list::omx_swvdec_ts_list()
{
    reset();

    pthread_mutex_init(&m_mutex, NULL);
}

/**
 * @brief OMX SwVdec timestamp list destructor.
 */
omx_swvdec_ts_list::~omx_swvdec_ts_list()
{
    pthread_mutex_destroy(&m_mutex);
}

/**
 * @brief Reset timestamp list.
 */
void omx_swvdec_ts_list::reset()
{
    memset(m_list, 0, sizeof(m_list));
    m_count_filled = 0;
}

/**
 * @brief Push timestamp to list, keeping lowest-valued timestamp at the end.
 *
 * @param[in] timestamp: Timestamp.
 *
 * @retval  true if push successful
 * @retval false if push unsuccessful
 */
bool omx_swvdec_ts_list::push(long long timestamp)
{
    bool retval = true;

    pthread_mutex_lock(&m_mutex);

    if (m_count_filled < OMX_SWVDEC_TS_LIST_ELEMENTS)
    {
        int index_curr, index_prev;

        long long timestamp_tmp;

        // insert timestamp into list

        m_list[m_count_filled].filled    = true;
        m_list[m_count_filled].timestamp = timestamp;
        m_count_filled++;

        // iterate backwards

        index_curr = m_count_filled - 1;
        index_prev = m_count_filled - 2;

        while ((index_curr > 0) &&
               (m_list[index_curr].timestamp > m_list[index_prev].timestamp))
        {
            // swap timestamps

            timestamp_tmp                = m_list[index_prev].timestamp;
            m_list[index_prev].timestamp = m_list[index_curr].timestamp;
            m_list[index_curr].timestamp = timestamp_tmp;

            index_curr--;
            index_prev--;
        }
    }
    else
    {
        retval = false;
    }

    pthread_mutex_unlock(&m_mutex);

    return retval;
}

/**
 * @brief Pop timestamp from list.
 *
 * @param[in,out] p_timestamp: Pointer to timestamp variable.
 *
 * @retval  true if pop successful
 * @retval false if pop unsuccessful
 */
bool omx_swvdec_ts_list::pop(long long *p_timestamp)
{
    bool retval;

    pthread_mutex_lock(&m_mutex);

    if (m_count_filled)
    {
        *p_timestamp = m_list[m_count_filled - 1].timestamp;
        m_list[m_count_filled - 1].filled = false;
        m_count_filled--;

        retval = true;
    }
    else
    {
        retval = false;
    }

    pthread_mutex_unlock(&m_mutex);

    return retval;
}

/**
 * @brief OMX SwVdec diagnostics class constructor.
 */
omx_swvdec_diag::omx_swvdec_diag():
    m_dump_ip(0),
    m_dump_op(0),
    m_filename_ip(NULL),
    m_filename_op(NULL),
    m_file_ip(NULL),
    m_file_op(NULL)
{
    char property_value[PROPERTY_VALUE_MAX] = {0};

    if (property_get("omx_swvdec.dump.ip", property_value, NULL))
    {
        m_dump_ip = atoi(property_value);
        OMX_SWVDEC_LOG_LOW("omx_swvdec.dump.ip: %d", m_dump_ip);
    }

    if (property_get("omx_swvdec.dump.op", property_value, NULL))
    {
        m_dump_op = atoi(property_value);
        OMX_SWVDEC_LOG_LOW("omx_swvdec.dump.op: %d", m_dump_op);
    }

    if (property_get("omx_swvdec.filename.ip",
                     property_value,
                     DIAG_FILENAME_IP))
    {
        OMX_SWVDEC_LOG_LOW("omx_swvdec.filename.ip: %s", m_filename_ip);

        m_filename_ip =
            (char *) malloc((strlen(property_value) + 1) * sizeof(char));

        if (m_filename_ip == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("failed to allocate %d bytes for "
                                 "input filename string",
                                 (strlen(property_value) + 1) * sizeof(char));
        }
        else
        {
            strncpy(m_filename_ip, property_value, strlen(property_value) + 1);
        }
    }

    if (property_get("omx_swvdec.filename.op",
                     property_value,
                     DIAG_FILENAME_OP))
    {
        OMX_SWVDEC_LOG_LOW("omx_swvdec.filename.op: %s", m_filename_op);

        m_filename_op =
            (char *) malloc((strlen(property_value) + 1) * sizeof(char));

        if (m_filename_op == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("failed to allocate %d bytes for "
                                 "output filename string",
                                 (strlen(property_value) + 1) * sizeof(char));
        }
        else
        {
            strncpy(m_filename_op, property_value, strlen(property_value) + 1);
        }
    }

    if (m_dump_ip && (m_filename_ip != NULL))
    {
        if ((m_file_ip = fopen(m_filename_ip, "rb")) == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("cannot open input file '%s'", m_filename_ip);
            m_dump_ip = 0;
        }
    }
    else
    {
        m_dump_ip = 0;
    }

    if (m_dump_op && (m_filename_op != NULL))
    {
        if ((m_file_op = fopen(m_filename_op, "rb")) == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("cannot open output file '%s'", m_filename_op);
            m_dump_op = 0;
        }
    }
    else
    {
        m_dump_op = 0;
    }
}

/**
 * @brief OMX SwVdec diagnostics class destructor.
 */
omx_swvdec_diag::~omx_swvdec_diag()
{
    if (m_file_op)
    {
        fclose(m_file_op);
        m_file_op = NULL;
    }

    if (m_file_ip)
    {
        fclose(m_file_ip);
        m_file_ip = NULL;
    }

    if (m_filename_op)
    {
        free(m_filename_op);
        m_filename_op = NULL;
    }

    if (m_filename_ip)
    {
        free(m_filename_ip);
        m_filename_ip = NULL;
    }
}

/**
 * @brief Dump input bitstream to file.
 *
 * @param[in] p_buffer:      Pointer to input bitstream buffer.
 * @param[in] filled_length: Bitstream buffer's filled length.
 */
void omx_swvdec_diag::dump_ip(unsigned char *p_buffer,
                              unsigned int   filled_length)
{
    if (m_dump_ip)
    {
        fwrite(p_buffer, sizeof(unsigned char), filled_length, m_file_ip);
    }
}

/**
 * @brief Dump output YUV to file.
 *
 * @param[in] p_buffer:  Pointer to output YUV buffer.
 * @param[in] width:     Frame width.
 * @param[in] height:    Frame height.
 * @param[in] stride:    Frame stride.
 * @param[in] scanlines: Frame scanlines.
 */
void omx_swvdec_diag::dump_op(unsigned char *p_buffer,
                              unsigned int   width,
                              unsigned int   height,
                              unsigned int   stride,
                              unsigned int   scanlines)
{
    if (m_dump_op)
    {
        unsigned char *p_buffer_y;
        unsigned char *p_buffer_uv;

        unsigned int ii;

        p_buffer_y  = p_buffer;
        p_buffer_uv = p_buffer + (stride * scanlines);

        for (ii = 0; ii < height; ii++)
        {
            fwrite(p_buffer_y, sizeof(unsigned char), width, m_file_op);

            p_buffer_y += stride;
        }

        for (ii = 0; ii < (height / 2); ii++)
        {
            fwrite(p_buffer_uv, sizeof(unsigned char), width, m_file_op);

            p_buffer_uv += stride;
        }
    }
}
