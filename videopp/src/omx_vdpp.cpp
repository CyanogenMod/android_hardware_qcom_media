/*---------------------------------------------------------------------------------------
Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

/*============================================================================
                            O p e n M A X   w r a p p e r s
                             O p e n  M A X   C o r e

*//** @file omx_vdpp.cpp
  This module contains the implementation of the OpenMAX video post-processing component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////
#define LOG_NDEBUG 0
#include <string.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "omx_vdpp.h"
#include <fcntl.h>
#include <limits.h>
#include <media/msm_media_info.h>

#ifndef _ANDROID_
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif //_ANDROID_

#ifdef _ANDROID_
#include <cutils/properties.h>
#undef USE_EGL_IMAGE_GPU
#endif

#if  defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
#include <gralloc_priv.h>
#endif

#ifdef INPUT_BUFFER_LOG
int inputBufferFile;
char inputfilename[] = "/sdcard/input-bitstream";
#endif
#ifdef OUTPUT_BUFFER_LOG
int outputBufferFile;
char outputfilename[] = "/sdcard/output.yuv";
#endif
#ifdef OUTPUT_EXTRADATA_LOG
FILE *outputExtradataFile;
char ouputextradatafilename[] = "/data/extradata";
#endif

#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _ANDROID_
    extern "C"{
        #include<utils/Log.h>
    }
#endif//_ANDROID_

#define POLL_TIMEOUT 0x7fffffff //10000//
#define MEM_DEVICE "/dev/ion"
#define MEM_HEAP_ID ION_CP_MM_HEAP_ID

#define DEFAULT_FPS 30
#define MAX_INPUT_ERROR DEFAULT_FPS
#define MAX_SUPPORTED_FPS 120

#define SZ_4K 0x1000

#define Log2(number, power)  { OMX_U32 temp = number; power = 0; while( (0 == (temp & 0x1)) &&  power < 16) { temp >>=0x1; power++; } }
#define Q16ToFraction(q,num,den) { OMX_U32 power; Log2(q,power);  num = q >> power; den = 0x1 << (16 - power); }
#define EXTRADATA_IDX(__num_planes) (__num_planes  - 1)

#define DEFAULT_EXTRADATA (OMX_INTERLACE_EXTRADATA)

#ifndef STUB_VPU
void* async_message_thread (void *input)
{
  int extra_idx = 0;
  int rc = 0;
  OMX_BUFFERHEADERTYPE *buffer;
  struct v4l2_plane plane[VIDEO_MAX_PLANES];
  struct pollfd pfd[2];
  struct v4l2_buffer v4l2_buf;
  struct v4l2_event dqevent;
  omx_vdpp *omx = reinterpret_cast<omx_vdpp*>(input);
  pfd[0].events = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM | POLLRDBAND | POLLPRI;
  pfd[0].fd = omx->drv_ctx.video_vpu_fd;
  pfd[1].events = POLLIN | POLLPRI | POLLERR;
  pfd[1].fd = omx->m_ctrl_in;

  memset((void *)&v4l2_buf,0,sizeof(v4l2_buf));
  DEBUG_PRINT_LOW("omx_vdpp: Async thread start\n");
  prctl(PR_SET_NAME, (unsigned long)"VdppCallBackThread", 0, 0, 0);
  while (1)
  {
    rc = poll(pfd, 2, POLL_TIMEOUT);

    if (!rc) {
      DEBUG_PRINT_HIGH("Poll timedout\n");
      break; // no input buffers EOS reached
    } else if (rc < 0) {
      DEBUG_PRINT_ERROR("Error while polling: %d\n", rc);
      break;
    }
    //DEBUG_PRINT_LOW("async_message_thread 1 POLL_TIMEOUT = 0x%x", POLL_TIMEOUT);

    if (pfd[1].revents & (POLLIN | POLLPRI | POLLERR))
    {
      DEBUG_PRINT_HIGH("pipe event, exit async thread");
      break;
    }

    // output buffer ready for fbd
    if ((pfd[0].revents & POLLIN) || (pfd[0].revents & POLLRDNORM)) {
    //DEBUG_PRINT_LOW("async_message_thread 1\n");
    struct vdpp_msginfo vdpp_msg;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory = V4L2_MEMORY_USERPTR;
    v4l2_buf.length = omx->drv_ctx.output_num_planes;
    v4l2_buf.m.planes = plane;
    while(!ioctl(pfd[0].fd, VIDIOC_DQBUF, &v4l2_buf)) {
        DEBUG_PRINT_LOW("async_message_thread 2\n");
        vdpp_msg.msgcode=VDPP_MSG_RESP_OUTPUT_BUFFER_DONE;
        vdpp_msg.status_code=VDPP_S_SUCCESS;
        vdpp_msg.msgdata.output_frame.client_data=(void*)&v4l2_buf;

        // driver returns ION buffer address, but case VDPP_MSG_RESP_OUTPUT_BUFFER_DONE
        // will pass mmaped address to upper layer, and then driver sets it when returnning
        // DQBUF for output buffers.
        extra_idx = EXTRADATA_IDX(omx->drv_ctx.output_num_planes);
        if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
            // this len is used in fill_buffer_done buffer->nFilledLen
            // is different from FTBProxy plane[0].length
            vdpp_msg.msgdata.output_frame.len= v4l2_buf.m.planes[0].bytesused + v4l2_buf.m.planes[extra_idx].bytesused;
            //DEBUG_PRINT_HIGH("async_message_thread 2.5 omx->drv_ctx.op_buf.buffer_size = %d, plane[0].bytesused = %d, plane[%d].bytesused = %d\n", omx->drv_ctx.op_buf.buffer_size, v4l2_buf.m.planes[0].bytesused, extra_idx, v4l2_buf.m.planes[extra_idx].bytesused);
        }
        else {
            vdpp_msg.msgdata.output_frame.len=v4l2_buf.m.planes[0].bytesused;
            //DEBUG_PRINT_HIGH("async_message_thread 2.5 - 2 plane[0].bytesused = %d\n", v4l2_buf.m.planes[0].bytesused);
        }
        vdpp_msg.msgdata.output_frame.bufferaddr=(void*)v4l2_buf.m.planes[0].m.userptr;

        // currently V4L2 driver just passes timestamp to maple FW, and maple FW
        // pass the timestamp back to OMX
        vdpp_msg.msgdata.output_frame.time_stamp = *(uint64_t *)(&v4l2_buf.timestamp);

        //DEBUG_PRINT_HIGH("async_message_thread 2.6.0 v4l2_buf.timestamp.tv_sec = 0x%08lx, v4l2_buf.timestamp.tv_usec = 0x%08lx\n", v4l2_buf.timestamp.tv_sec, v4l2_buf.timestamp.tv_usec);
        if (omx->async_message_process(input,&vdpp_msg) < 0) {
          DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
          break;
        }
      }
    }
    // input buffer ready for empty buffer done (ebd)
      if((pfd[0].revents & POLLOUT) || (pfd[0].revents & POLLWRNORM)) {
      struct vdpp_msginfo vdpp_msg;
      v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      v4l2_buf.memory = V4L2_MEMORY_USERPTR;
      v4l2_buf.length = omx->drv_ctx.input_num_planes;
      v4l2_buf.m.planes = plane;
      DEBUG_PRINT_LOW("async_message_thread 3\n");
      while(!ioctl(pfd[0].fd, VIDIOC_DQBUF, &v4l2_buf)) {
        vdpp_msg.msgcode=VDPP_MSG_RESP_INPUT_BUFFER_DONE;
        vdpp_msg.status_code=VDPP_S_SUCCESS;
        vdpp_msg.msgdata.input_frame_clientdata=(void*)&v4l2_buf;

        extra_idx = EXTRADATA_IDX(omx->drv_ctx.input_num_planes);
        if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
            vdpp_msg.msgdata.output_frame.len=v4l2_buf.m.planes[0].bytesused + v4l2_buf.m.planes[extra_idx].bytesused; // user doesn't need this for ebd, just set in case is used
            //DEBUG_PRINT_HIGH("async_message_thread 3.5 plane[0].bytesused = %d, plane[extra_idx].bytesused = %d\n", v4l2_buf.m.planes[0].bytesused, v4l2_buf.m.planes[extra_idx].bytesused);
        }
        else {
            vdpp_msg.msgdata.output_frame.len=v4l2_buf.m.planes[0].bytesused;
            //DEBUG_PRINT_HIGH("async_message_thread 3.5 - 2 plane[0].bytesused = %d\n", v4l2_buf.m.planes[0].bytesused);
        }
        if (omx->async_message_process(input,&vdpp_msg) < 0) {
          DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
          break;
        }
      }
    }
    if (pfd[0].revents & POLLPRI){
      DEBUG_PRINT_HIGH("async_message_thread 4\n");
      memset(&dqevent, 0, sizeof(struct v4l2_event));
      rc = ioctl(pfd[0].fd, VIDIOC_DQEVENT, &dqevent);
      if(dqevent.type == VPU_EVENT_HW_ERROR)
      {
        struct vdpp_msginfo vdpp_msg;
        vdpp_msg.msgcode=VDPP_MSG_EVT_HW_ERROR;
        vdpp_msg.status_code=VDPP_S_SUCCESS;
        DEBUG_PRINT_HIGH(" SYS Error Recieved \n");
        if (omx->async_message_process(input,&vdpp_msg) < 0)
        {
          DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
          break;
        }
      }
      else if (dqevent.type == VPU_EVENT_FLUSH_DONE) {
        struct vdpp_msginfo vdpp_msg;
        enum v4l2_buf_type buf_type;
        memcpy(&buf_type, dqevent.u.data, sizeof(buf_type));
        if(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE == buf_type)
        {
            vdpp_msg.msgcode=VDPP_MSG_RESP_FLUSH_INPUT_DONE;
            vdpp_msg.status_code=VDPP_S_SUCCESS;
            DEBUG_PRINT_HIGH("VDPP Input Flush Done Recieved \n");
            if (omx->async_message_process(input,&vdpp_msg) < 0) {
                DEBUG_PRINT_HIGH("\n async_message_thread Exited  \n");
                break;
            }
        }
        else if(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type)
        {
            vdpp_msg.msgcode=VDPP_MSG_RESP_FLUSH_OUTPUT_DONE;
            vdpp_msg.status_code=VDPP_S_SUCCESS;
            DEBUG_PRINT_HIGH("VDPP Output Flush Done Recieved \n");
            if (omx->async_message_process(input,&vdpp_msg) < 0) {
                DEBUG_PRINT_HIGH("\n async_message_thread Exited  \n");
                break;
            }
        }
        else
        {
            DEBUG_PRINT_HIGH(" Wrong buf_type recieved %d\n", buf_type);
        }
      }
      else if(dqevent.type == VPU_EVENT_ACTIVE_REGION_CHANGED)
      {
        DEBUG_PRINT_HIGH(" VPU_EVENT_ACTIVE_REGION_CHANGED\n");
        struct vdpp_msginfo vdpp_msg;
        vdpp_msg.msgcode=VDPP_MSG_EVT_ACTIVE_REGION_DETECTION_STATUS;
        vdpp_msg.status_code=VDPP_S_SUCCESS;

        // get the active region dection result struct from the event associated data
        memcpy(&vdpp_msg.msgdata.ar_result, dqevent.u.data, sizeof(v4l2_rect));
        DEBUG_PRINT_HIGH(" VPU_EVENT_ACTIVE_REGION_CHANGED Recieved \n");
        if(omx->m_ar_callback_setup)
        {
            if (omx->async_message_process(input,&vdpp_msg) < 0)
            {
              DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
              break;
            }
        }
      }
      else
      {
        DEBUG_PRINT_HIGH(" VPU Some Event recieved \n");
        continue;
      }

    }
  }
  DEBUG_PRINT_HIGH("omx_vdpp: Async thread stop\n");
  return NULL;
}
#else // use stub to simulate vpu events for now
void* async_message_thread (void *input)
{
  OMX_BUFFERHEADERTYPE *buffer;
  struct v4l2_plane plane[VIDEO_MAX_PLANES];
  struct pollfd pfd;
  struct v4l2_buffer v4l2_buf;
  memset((void *)&v4l2_buf,0,sizeof(v4l2_buf));
  struct v4l2_event dqevent;
  omx_vdpp *omx = reinterpret_cast<omx_vdpp*>(input);

  pfd.events = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM | POLLRDBAND | POLLPRI;
  pfd.fd = omx->drv_ctx.video_vpu_fd;
  int error_code = 0,rc=0,bytes_read = 0,bytes_written = 0;
  DEBUG_PRINT_HIGH("omx_vdpp: Async thread start\n");
  prctl(PR_SET_NAME, (unsigned long)"VdppCallBackThread", 0, 0, 0);
  while (1)
  {
    DEBUG_PRINT_HIGH("omx_vdpp: Async thread start 0\n");
    sem_wait(&(omx->drv_ctx.async_lock));
    DEBUG_PRINT_HIGH("omx_vdpp: Async thread start pfd.revents = %d\n", pfd.revents);
    if ((omx->drv_ctx.etb_ftb_info.ftb_cnt > 0))
    {
      DEBUG_PRINT_LOW("async_message_thread 1 omx->drv_ctx.etb_ftb_info.ftb_cnt = %d\n", omx->drv_ctx.etb_ftb_info.ftb_cnt);
      struct vdpp_msginfo vdpp_msg;
      unsigned p1 = 0;
      unsigned p2 = 0;
      unsigned ident = 0;
      v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      v4l2_buf.memory = V4L2_MEMORY_USERPTR;
      omx->m_index_q_ftb.pop_entry(&p1,&p2,&ident);
      v4l2_buf.index = ident;
      v4l2_buf.bytesused = omx->drv_ctx.etb_ftb_info.ftb_len;
      omx->drv_ctx.etb_ftb_info.ftb_cnt--;
      DEBUG_PRINT_HIGH("async_message_thread 1.5 omx->drv_ctx.etb_ftb_info.ftb_cnt = %d\n", omx->drv_ctx.etb_ftb_info.ftb_cnt);
      /*while(!ioctl(pfd.fd, VIDIOC_DQBUF, &v4l2_buf)) */{
      DEBUG_PRINT_LOW("async_message_thread 2\n", rc);
        vdpp_msg.msgcode=VDPP_MSG_RESP_OUTPUT_BUFFER_DONE;
        vdpp_msg.status_code=VDPP_S_SUCCESS;
        vdpp_msg.msgdata.output_frame.client_data=(void*)&v4l2_buf;
        vdpp_msg.msgdata.output_frame.len=v4l2_buf.bytesused;
        vdpp_msg.msgdata.output_frame.bufferaddr=(void*)v4l2_buf.m.userptr;

        vdpp_msg.msgdata.output_frame.time_stamp= ((uint64_t)v4l2_buf.timestamp.tv_sec * (uint64_t)1000000) +
          (uint64_t)v4l2_buf.timestamp.tv_usec;
        if (omx->async_message_process(input,&vdpp_msg) < 0) {
          DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
          break;
        }
      }
    }
    if(omx->drv_ctx.etb_ftb_info.etb_cnt > 0) {
      struct vdpp_msginfo vdpp_msg;
      unsigned p1 = 0;
      unsigned p2 = 0;
      unsigned ident = 0;
      v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      v4l2_buf.memory = V4L2_MEMORY_USERPTR;

      omx->m_index_q_etb.pop_entry(&p1,&p2,&ident);
      v4l2_buf.index = ident;
      DEBUG_PRINT_LOW("async_message_thread 3 omx->drv_ctx.etb_ftb_info.etb_cnt = %d\n", omx->drv_ctx.etb_ftb_info.etb_cnt);
      omx->drv_ctx.etb_ftb_info.etb_cnt--;
      DEBUG_PRINT_LOW("async_message_thread 4 omx->drv_ctx.etb_ftb_info.etb_cnt = %d\n", omx->drv_ctx.etb_ftb_info.etb_cnt);

      /*while(!ioctl(pfd.fd, VIDIOC_DQBUF, &v4l2_buf))*/ {
        vdpp_msg.msgcode=VDPP_MSG_RESP_INPUT_BUFFER_DONE;
        vdpp_msg.status_code=VDPP_S_SUCCESS;
        vdpp_msg.msgdata.input_frame_clientdata=(void*)&v4l2_buf;
        if (omx->async_message_process(input,&vdpp_msg) < 0) {
          DEBUG_PRINT_HIGH(" async_message_thread Exited  \n");
          break;
        }
      }
    }

    if(omx->drv_ctx.thread_exit)
    {
        break;
    }
  }
  DEBUG_PRINT_HIGH("omx_vdpp: Async thread stop\n");
  return NULL;
}
#endif

void* message_thread(void *input)
{
  omx_vdpp* omx = reinterpret_cast<omx_vdpp*>(input);
  unsigned char id;
  int n;

  DEBUG_PRINT_LOW("omx_vdpp: message thread start\n");
  prctl(PR_SET_NAME, (unsigned long)"VideoPostProcessingMsgThread", 0, 0, 0);
  while (1)
  {

    n = read(omx->m_pipe_in, &id, 1);

    if(0 == n)
    {
      break;
    }

    if (1 == n)
    {
        omx->process_event_cb(omx, id);
    }
    if ((n < 0) && (errno != EINTR))
    {
      DEBUG_PRINT_ERROR("ERROR: read from pipe failed, ret %d errno %d", n, errno);
      break;
    }
  }
  DEBUG_PRINT_HIGH("omx_vdpp: message thread stop\n");
  return 0;
}

void post_message(omx_vdpp *omx, unsigned char id)
{
      int ret_value;
      //DEBUG_PRINT_LOW("omx_vdpp: post_message %d pipe out 0x%x\n", id,omx->m_pipe_out);
      ret_value = write(omx->m_pipe_out, &id, 1);
      //DEBUG_PRINT_HIGH("post_message to pipe done %d\n",ret_value);
}

// omx_cmd_queue destructor
omx_vdpp::omx_cmd_queue::~omx_cmd_queue()
{
  // Nothing to do
}

// omx cmd queue constructor
omx_vdpp::omx_cmd_queue::omx_cmd_queue(): m_read(0),m_write(0),m_size(0)
{
    memset(m_q,0,sizeof(omx_event)*OMX_CORE_CONTROL_CMDQ_SIZE);
}

// omx cmd queue insert
bool omx_vdpp::omx_cmd_queue::insert_entry(unsigned p1, unsigned p2, unsigned id)
{
  bool ret = true;
  if(m_size < OMX_CORE_CONTROL_CMDQ_SIZE)
  {
    m_q[m_write].id       = id;
    m_q[m_write].param1   = p1;
    m_q[m_write].param2   = p2;
    m_write++;
    m_size ++;
    if(m_write >= OMX_CORE_CONTROL_CMDQ_SIZE)
    {
      m_write = 0;
    }
  }
  else
  {
    ret = false;
    DEBUG_PRINT_ERROR("ERROR: %s()::Command Queue Full\n", __func__);
  }
  return ret;
}

// omx cmd queue pop
bool omx_vdpp::omx_cmd_queue::pop_entry(unsigned *p1, unsigned *p2, unsigned *id)
{
  bool ret = true;
  if (m_size > 0)
  {
    *id = m_q[m_read].id;
    *p1 = m_q[m_read].param1;
    *p2 = m_q[m_read].param2;
    // Move the read pointer ahead
    ++m_read;
    --m_size;
    if(m_read >= OMX_CORE_CONTROL_CMDQ_SIZE)
    {
      m_read = 0;
    }
  }
  else
  {
    ret = false;
  }
  return ret;
}

// Retrieve the first mesg type in the queue
unsigned omx_vdpp::omx_cmd_queue::get_q_msg_type()
{
    return m_q[m_read].id;
}

#ifdef _ANDROID_
omx_vdpp::ts_arr_list::ts_arr_list()
{
  //initialize timestamps array
  memset(m_ts_arr_list, 0, ( sizeof(ts_entry) * MAX_NUM_INPUT_OUTPUT_BUFFERS) );
}
omx_vdpp::ts_arr_list::~ts_arr_list()
{
  //free m_ts_arr_list?
}

bool omx_vdpp::ts_arr_list::insert_ts(OMX_TICKS ts)
{
  bool ret = true;
  bool duplicate_ts = false;
  int idx = 0;

  //insert at the first available empty location
  for ( ; idx < MAX_NUM_INPUT_OUTPUT_BUFFERS; idx++)
  {
    if (!m_ts_arr_list[idx].valid)
    {
      //found invalid or empty entry, save timestamp
      m_ts_arr_list[idx].valid = true;
      m_ts_arr_list[idx].timestamp = ts;
      DEBUG_PRINT_LOW("Insert_ts(): Inserting TIMESTAMP (%lld) at idx (%d)",
                       ts, idx);
      break;
    }
  }

  if (idx == MAX_NUM_INPUT_OUTPUT_BUFFERS)
  {
    DEBUG_PRINT_LOW("Timestamp array list is FULL. Unsuccessful insert");
    ret = false;
  }
  return ret;
}

bool omx_vdpp::ts_arr_list::pop_min_ts(OMX_TICKS &ts)
{
  bool ret = true;
  int min_idx = -1;
  OMX_TICKS min_ts = 0;
  int idx = 0;

  for ( ; idx < MAX_NUM_INPUT_OUTPUT_BUFFERS; idx++)
  {

    if (m_ts_arr_list[idx].valid)
    {
      //found valid entry, save index
      if (min_idx < 0)
      {
        //first valid entry
        min_ts = m_ts_arr_list[idx].timestamp;
        min_idx = idx;
      }
      else if (m_ts_arr_list[idx].timestamp < min_ts)
      {
        min_ts = m_ts_arr_list[idx].timestamp;
        min_idx = idx;
      }
    }

  }

  if (min_idx < 0)
  {
    //no valid entries found
    DEBUG_PRINT_LOW("Timestamp array list is empty. Unsuccessful pop");
    ts = 0;
    ret = false;
  }
  else
  {
    ts = m_ts_arr_list[min_idx].timestamp;
    m_ts_arr_list[min_idx].valid = false;
    DEBUG_PRINT_LOW("Pop_min_ts:Timestamp (%lld), index(%d)",
                     ts, min_idx);
  }

  return ret;

}


bool omx_vdpp::ts_arr_list::reset_ts_list()
{
  bool ret = true;
  int idx = 0;

  DEBUG_PRINT_LOW("reset_ts_list(): Resetting timestamp array list");
  for ( ; idx < MAX_NUM_INPUT_OUTPUT_BUFFERS; idx++)
  {
    m_ts_arr_list[idx].valid = false;
  }
  return ret;
}
#endif

// factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
  return (new omx_vdpp);
}

/* ======================================================================
FUNCTION
  omx_vdpp::omx_vdpp

DESCRIPTION
  Constructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_vdpp::omx_vdpp(): m_ar_callback_setup(false),
    m_error_propogated(false),
	m_state(OMX_StateInvalid),
	m_app_data(NULL),
	m_inp_mem_ptr(NULL),
	m_out_mem_ptr(NULL),
	m_inp_err_count(0),
	input_flush_progress (false),
	output_flush_progress (false),
	input_use_buffer (false),
	output_use_buffer (false),
	ouput_egl_buffers(false),
	m_use_output_pmem(OMX_FALSE),
	m_out_mem_region_smi(OMX_FALSE),
	m_out_pvt_entry_pmem(OMX_FALSE),
	pending_input_buffers(0),
	pending_output_buffers(0),
    input_qbuf_count(0),
    input_dqbuf_count(0),
    output_qbuf_count(0),
    output_dqbuf_count(0),
#ifdef OUTPUT_BUFFER_LOG
    output_buffer_write_counter(0),
    input_buffer_write_counter(0),
#endif
	m_out_bm_count(0),
	m_inp_bm_count(0),
	m_inp_bPopulated(OMX_FALSE),
	m_out_bPopulated(OMX_FALSE),
	m_flags(0),
	m_inp_bEnabled(OMX_TRUE),
	m_out_bEnabled(OMX_TRUE),
	m_in_alloc_cnt(0),
	m_platform_list(NULL),
	m_platform_entry(NULL),
	m_pmem_info(NULL),
	psource_frame (NULL),
	pdest_frame (NULL),
	m_inp_heap_ptr (NULL),
	m_phdr_pmem_ptr(NULL),
	m_heap_inp_bm_count (0),
	prev_ts(LLONG_MAX),
	rst_prev_ts(true),
	frm_int(0),
	in_reconfig(false),
    client_extradata(0),
	m_enable_android_native_buffers(OMX_FALSE),
	m_use_android_native_buffers(OMX_FALSE),
    client_set_fps(false),
    interlace_user_flag(false)
{
  DEBUG_PRINT_LOW("In OMX vdpp Constructor");

  memset(&m_cmp,0,sizeof(m_cmp));
  memset(&m_cb,0,sizeof(m_cb));
  memset (&drv_ctx,0,sizeof(drv_ctx));
  msg_thread_id = 0;
  async_thread_id = 0;
  msg_thread_created = false;
  async_thread_created = false;
#ifdef _ANDROID_ICS_
  memset(&native_buffer, 0 ,(sizeof(struct nativebuffer) * MAX_NUM_INPUT_OUTPUT_BUFFERS));
#endif

  drv_ctx.timestamp_adjust = false;
  drv_ctx.video_vpu_fd = -1;
  pthread_mutex_init(&m_lock, NULL);
  sem_init(&m_cmd_lock,0,0);
  streaming[CAPTURE_PORT] = false;
  streaming[OUTPUT_PORT] = false;

  m_fill_output_msg = OMX_COMPONENT_GENERATE_FTB;

#ifdef STUB_VPU
  drv_ctx.thread_exit = false;
  sem_init(&(drv_ctx.async_lock),0,0);
#endif
}

static OMX_ERRORTYPE subscribe_to_events(int fd)
{
	OMX_ERRORTYPE eRet = OMX_ErrorNone;
	struct v4l2_event_subscription sub;
	int rc;
	if (fd < 0) {
		DEBUG_PRINT_ERROR("Invalid input: %d\n", fd);
		return OMX_ErrorBadParameter;
	}

#ifndef STUB_VPU
      sub.type = V4L2_EVENT_ALL;
	  rc = ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	  if (rc < 0)
      {
		DEBUG_PRINT_ERROR("Failed to subscribe event: 0x%x\n", sub.type);
		eRet = OMX_ErrorNotImplemented;
	  }
#endif

	return eRet;
}


static OMX_ERRORTYPE unsubscribe_to_events(int fd)
{
	OMX_ERRORTYPE eRet = OMX_ErrorNone;
	struct v4l2_event_subscription sub;

	int rc;
	if (fd < 0) {
		DEBUG_PRINT_ERROR("Invalid input: %d\n", fd);
		return OMX_ErrorBadParameter;
	}

#ifndef STUB_VPU
	memset(&sub, 0, sizeof(sub));
	sub.type = V4L2_EVENT_ALL;
	rc = ioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
	if (rc) {
		DEBUG_PRINT_ERROR("Failed to unsubscribe event: 0x%x\n", sub.type);
	}
#endif

	return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::~omx_vdpp

DESCRIPTION
  Destructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_vdpp::~omx_vdpp()
{
  m_pmem_info = NULL;
  DEBUG_PRINT_HIGH("In OMX vdpp Destructor");
  if(m_pipe_in) close(m_pipe_in);
  if(m_pipe_out) close(m_pipe_out);
  m_pipe_in = -1;
  m_pipe_out = -1;
  DEBUG_PRINT_HIGH("Waiting on OMX Msg Thread exit");
  if (msg_thread_created)
    pthread_join(msg_thread_id,NULL);
  DEBUG_PRINT_HIGH("Waiting on OMX Async Thread exit");

#ifdef STUB_VPU
  DEBUG_PRINT_HIGH("drv_ctx.etb_ftb_info.ftb_cnt = %d, drv_ctx.etb_ftb_info.etb_cnt = %d", drv_ctx.etb_ftb_info.ftb_cnt, drv_ctx.etb_ftb_info.etb_cnt);
  drv_ctx.etb_ftb_info.ftb_cnt = 0;
  drv_ctx.etb_ftb_info.etb_cnt = 0;
  sem_post (&(drv_ctx.async_lock));
  drv_ctx.thread_exit = true;
#endif
  // notify async thread to exit
  DEBUG_PRINT_LOW("write control pipe to notify async thread to exit");
  write(m_ctrl_out, "1", 1);

  if (async_thread_created)
    pthread_join(async_thread_id,NULL);
  DEBUG_PRINT_HIGH("async_thread exits");
  unsubscribe_to_events(drv_ctx.video_vpu_fd);
  close(drv_ctx.video_vpu_fd);
  pthread_mutex_destroy(&m_lock);
  sem_destroy(&m_cmd_lock);

#ifdef STUB_VPU
  sem_destroy(&(drv_ctx.async_lock));
#endif
  if(m_ctrl_in) close(m_ctrl_in);
  if(m_ctrl_out) close(m_ctrl_out);
  m_ctrl_in = -1;
  m_ctrl_out = -1;
  DEBUG_PRINT_HIGH("Exit OMX vdpp Destructor");
}

int release_buffers(omx_vdpp* obj, enum vdpp_buffer buffer_type) {
	struct v4l2_requestbuffers bufreq;
	int rc = 0;
#ifndef STUB_VPU
	if (buffer_type == VDPP_BUFFER_TYPE_OUTPUT){
		bufreq.memory = V4L2_MEMORY_USERPTR;
		bufreq.count = 0;
		bufreq.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		rc = ioctl(obj->drv_ctx.video_vpu_fd,VIDIOC_REQBUFS, &bufreq);
	}else if(buffer_type == VDPP_BUFFER_TYPE_INPUT) {
        bufreq.memory = V4L2_MEMORY_USERPTR;
        bufreq.count = 0;
        bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        rc = ioctl(obj->drv_ctx.video_vpu_fd,VIDIOC_REQBUFS, &bufreq);
    }
#endif
	return rc;
}

/* ======================================================================
FUNCTION
  omx_vdpp::process_event_cb

DESCRIPTION
  IL Client callbacks are generated through this routine. The VDPP
  provides the thread context for this routine.

PARAMETERS
  ctxt -- Context information related to the self.
  id   -- Event identifier. This could be any of the following:
          1. Command completion event
          2. Buffer done callback event
          3. Frame done callback event

RETURN VALUE
  None.

========================================================================== */
void omx_vdpp::process_event_cb(void *ctxt, unsigned char id)
{
  signed p1; // Parameter - 1
  signed p2; // Parameter - 2
  unsigned ident;
  unsigned qsize=0; // qsize
  omx_vdpp *pThis = (omx_vdpp *) ctxt;

  if(!pThis)
  {
    DEBUG_PRINT_ERROR("ERROR: %s()::Context is incorrect, bailing out\n",
        __func__);
    return;
  }

  // Protect the shared queue data structure
  do
  {
    /*Read the message id's from the queue*/
    pthread_mutex_lock(&pThis->m_lock);

    // first check command queue
    qsize = pThis->m_cmd_q.m_size;
    if(qsize)
    {
      pThis->m_cmd_q.pop_entry((unsigned *)&p1, (unsigned *)&p2, &ident);
      //DEBUG_PRINT_HIGH("process_event_cb m_cmd_q.pop_entry ident = %d", ident);
    }

    // then check ftb queue
    if (qsize == 0 && pThis->m_state != OMX_StatePause)
    {

      qsize = pThis->m_ftb_q.m_size;
      if (qsize)
      {
        pThis->m_ftb_q.pop_entry((unsigned *)&p1, (unsigned *)&p2, &ident);
        DEBUG_PRINT_HIGH("process_event_cb, p1 = 0x%08x, p2 = 0x%08x, ident = 0x%08x", p1, p2, ident);
      }
    }

    // last check etb queue
    if (qsize == 0 && pThis->m_state != OMX_StatePause)
    {
      qsize = pThis->m_etb_q.m_size;
      if (qsize)
      {
        pThis->m_etb_q.pop_entry((unsigned *)&p1, (unsigned *)&p2, &ident);
      }
    }
    pthread_mutex_unlock(&pThis->m_lock);

    /*process message if we have one*/
    if(qsize > 0)
    {
      id = ident;
      switch (id)
      {
        case OMX_COMPONENT_GENERATE_EVENT:
          if (pThis->m_cb.EventHandler)
          {
            switch (p1)
            {
              case OMX_CommandStateSet:
                pThis->m_state = (OMX_STATETYPE) p2;
                DEBUG_PRINT_HIGH(" OMX_CommandStateSet complete, m_state = %d, pThis->m_cb.EventHandler = %p",
                    pThis->m_state, pThis->m_cb.EventHandler);
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventCmdComplete, p1, p2, NULL);
                break;

              case OMX_EventError:
                if(p2 == OMX_StateInvalid)
                {
                    DEBUG_PRINT_ERROR(" OMX_EventError: p2 is OMX_StateInvalid");
                    pThis->m_state = (OMX_STATETYPE) p2;
                    pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                               OMX_EventError, OMX_ErrorInvalidState, p2, NULL);
                }
                else if (p2 == OMX_ErrorHardware)
                {
                   pThis->omx_report_error();
                }
                else
		        {
                    pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventError, p2, (OMX_U32)NULL, NULL );
                }
                break;

              case OMX_CommandPortDisable:
                DEBUG_PRINT_HIGH(" OMX_CommandPortDisable complete for port [%d], pThis->in_reconfig = %d", p2, pThis->in_reconfig);
                if (BITMASK_PRESENT(&pThis->m_flags,
                    OMX_COMPONENT_OUTPUT_FLUSH_IN_DISABLE_PENDING))
                {
                  BITMASK_SET(&pThis->m_flags, OMX_COMPONENT_DISABLE_OUTPUT_DEFERRED);
                  break;
                }
                if (p2 == OMX_CORE_OUTPUT_PORT_INDEX)
                {
				  OMX_ERRORTYPE eRet = OMX_ErrorNone;
				  pThis->stream_off(OMX_CORE_OUTPUT_PORT_INDEX);
				  if(release_buffers(pThis, VDPP_BUFFER_TYPE_OUTPUT))
					  DEBUG_PRINT_HIGH("Failed to release output buffers\n");
				  OMX_ERRORTYPE eRet1 = pThis->get_buffer_req(&pThis->drv_ctx.op_buf);
				  pThis->in_reconfig = false;
                  if(eRet !=  OMX_ErrorNone)
                  {
                      DEBUG_PRINT_ERROR("set_buffer_req failed eRet = %d",eRet);
                      pThis->omx_report_error();
                      break;
                  }
                }
                if (p2 == OMX_CORE_INPUT_PORT_INDEX)
                {
				  OMX_ERRORTYPE eRet = OMX_ErrorNone;
				  pThis->stream_off(OMX_CORE_INPUT_PORT_INDEX);
				  if(release_buffers(pThis, VDPP_BUFFER_TYPE_INPUT))
					  DEBUG_PRINT_HIGH("Failed to release output buffers\n");
				  OMX_ERRORTYPE eRet1 = pThis->get_buffer_req(&pThis->drv_ctx.ip_buf);
				  pThis->in_reconfig = false;
                  if(eRet !=  OMX_ErrorNone)
                  {
                      DEBUG_PRINT_ERROR("set_buffer_req failed eRet = %d",eRet);
                      pThis->omx_report_error();
                      break;
                  }
                }
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventCmdComplete, p1, p2, NULL );
                break;
              case OMX_CommandPortEnable:
                DEBUG_PRINT_HIGH(" OMX_CommandPortEnable complete for port [%d]", p2);
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,\
                                      OMX_EventCmdComplete, p1, p2, NULL );
                break;

              default:
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                         OMX_EventCmdComplete, p1, p2, NULL );
                break;

            }
          }
          else
          {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL\n", __func__);
          }
          break;
      break;
        case OMX_COMPONENT_GENERATE_ETB:
          if (pThis->empty_this_buffer_proxy((OMX_HANDLETYPE)p1,\
              (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
          {
            DEBUG_PRINT_ERROR(" empty_this_buffer_proxy failure");
            pThis->omx_report_error ();
          }
         break;

        case OMX_COMPONENT_GENERATE_FTB:
            {
              DEBUG_PRINT_HIGH("OMX_COMPONENT_GENERATE_FTB p2 = 0x%08x", p2);
              if ( pThis->fill_this_buffer_proxy((OMX_HANDLETYPE)p1,\
                   (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
              {
                 DEBUG_PRINT_ERROR(" fill_this_buffer_proxy failure");
                 pThis->omx_report_error ();
              }
            }
        break;

        case OMX_COMPONENT_GENERATE_COMMAND:
          pThis->send_command_proxy(&pThis->m_cmp,(OMX_COMMANDTYPE)p1,\
                                    (OMX_U32)p2,(OMX_PTR)NULL);
          break;

        case OMX_COMPONENT_GENERATE_EBD:
          if (p2 != VDPP_S_SUCCESS && p2 != VDPP_S_INPUT_BITSTREAM_ERR)
          {
            DEBUG_PRINT_HIGH(" OMX_COMPONENT_GENERATE_EBD failure");
            pThis->omx_report_error ();
          }
          else
          {
            DEBUG_PRINT_HIGH(" OMX_COMPONENT_GENERATE_EBD 1");
            if (p2 == VDPP_S_INPUT_BITSTREAM_ERR && p1)
            {
              pThis->m_inp_err_count++;
              DEBUG_PRINT_HIGH(" OMX_COMPONENT_GENERATE_EBD 2");
              //pThis->time_stamp_dts.remove_time_stamp(
              //((OMX_BUFFERHEADERTYPE *)p1)->nTimeStamp,
              //(pThis->drv_ctx.interlace != VDPP_InterlaceFrameProgressive)
              //  ?true:false);
            }
            else
            {
              pThis->m_inp_err_count = 0;
            }
            if ( pThis->empty_buffer_done(&pThis->m_cmp,
                 (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone)
            {
               DEBUG_PRINT_ERROR(" empty_buffer_done failure");
               pThis->omx_report_error ();
            }
            DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_EBD 4");
            if(pThis->m_inp_err_count >= MAX_INPUT_ERROR)
            {
               DEBUG_PRINT_ERROR(" Input bitstream error for consecutive %d frames.", MAX_INPUT_ERROR);
               pThis->omx_report_error ();
            }
          }
          break;
        case OMX_COMPONENT_GENERATE_FBD:
          if (p2 != VDPP_S_SUCCESS)
          {
            DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_FBD failure");
            pThis->omx_report_error ();
          }
          else if ( pThis->fill_buffer_done(&pThis->m_cmp,
                  (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone )
          {
            DEBUG_PRINT_ERROR(" fill_buffer_done failure");
            pThis->omx_report_error ();
          }
          break;

        case OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH:
          DEBUG_PRINT_HIGH(" Driver flush i/p Port complete");
          if (!pThis->input_flush_progress)
          {
            DEBUG_PRINT_ERROR(" WARNING: Unexpected flush from driver");
          }
          else
          {
            pThis->execute_input_flush();
            if (pThis->m_cb.EventHandler)
            {
              if (p2 != VDPP_S_SUCCESS)
              {
                DEBUG_PRINT_ERROR("OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH failure");
                pThis->omx_report_error ();
              }
              else
              {
                /*Check if we need generate event for Flush done*/
                if(BITMASK_PRESENT(&pThis->m_flags,
                                   OMX_COMPONENT_INPUT_FLUSH_PENDING))
                {
                  BITMASK_CLEAR (&pThis->m_flags,OMX_COMPONENT_INPUT_FLUSH_PENDING);
                  DEBUG_PRINT_LOW(" Input Flush completed - Notify Client");
                  pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                           OMX_EventCmdComplete,OMX_CommandFlush,
                                           OMX_CORE_INPUT_PORT_INDEX,NULL );
                }
                if (BITMASK_PRESENT(&pThis->m_flags,
                                         OMX_COMPONENT_IDLE_PENDING))
                {
                   if(pThis->stream_off(OMX_CORE_INPUT_PORT_INDEX)) {
                           DEBUG_PRINT_ERROR(" Failed to call streamoff on OUTPUT Port \n");
						   pThis->omx_report_error ();
				   } else {
                       DEBUG_PRINT_HIGH(" Successful to call streamoff on OUTPUT Port \n");
					   pThis->streaming[OUTPUT_PORT] = false;
				   }
                  if (!pThis->output_flush_progress)
                  {
                     DEBUG_PRINT_LOW(" Input flush done hence issue stop");
					 pThis->post_event ((unsigned int)NULL, VDPP_S_SUCCESS,\
							 OMX_COMPONENT_GENERATE_STOP_DONE);
                  }
                }
              }
            }
            else
            {
              DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
            }
          }
          break;

        case OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH:
          DEBUG_PRINT_HIGH(" Driver flush o/p Port complete");
          if (!pThis->output_flush_progress)
          {
            DEBUG_PRINT_ERROR(" WARNING: Unexpected flush from driver");
          }
          else
          {
            pThis->execute_output_flush();
            if (pThis->m_cb.EventHandler)
            {
              if (p2 != VDPP_S_SUCCESS)
              {
                DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH failed");
                pThis->omx_report_error ();
              }
              else
              {
                /*Check if we need generate event for Flush done*/
                if(BITMASK_PRESENT(&pThis->m_flags,
                                   OMX_COMPONENT_OUTPUT_FLUSH_PENDING))
                {
                  DEBUG_PRINT_LOW(" Notify Output Flush done");
                  BITMASK_CLEAR (&pThis->m_flags,OMX_COMPONENT_OUTPUT_FLUSH_PENDING);
                  pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                           OMX_EventCmdComplete,OMX_CommandFlush,
                                           OMX_CORE_OUTPUT_PORT_INDEX,NULL );
                }
                if(BITMASK_PRESENT(&pThis->m_flags,
                       OMX_COMPONENT_OUTPUT_FLUSH_IN_DISABLE_PENDING))
                {
                  DEBUG_PRINT_LOW(" Internal flush complete");
                  BITMASK_CLEAR (&pThis->m_flags,
                                 OMX_COMPONENT_OUTPUT_FLUSH_IN_DISABLE_PENDING);
                  if (BITMASK_PRESENT(&pThis->m_flags,
                          OMX_COMPONENT_DISABLE_OUTPUT_DEFERRED))
                  {
                    pThis->post_event(OMX_CommandPortDisable,
                               OMX_CORE_OUTPUT_PORT_INDEX,
                               OMX_COMPONENT_GENERATE_EVENT);
                    BITMASK_CLEAR (&pThis->m_flags,
                                   OMX_COMPONENT_DISABLE_OUTPUT_DEFERRED);

                  }
                }

                DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH 1  \n");

                if (BITMASK_PRESENT(&pThis->m_flags ,OMX_COMPONENT_IDLE_PENDING))
                {
                   DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH  2 \n");
                   if(pThis->stream_off(OMX_CORE_OUTPUT_PORT_INDEX)) {
                           DEBUG_PRINT_ERROR(" Failed to call streamoff on CAPTURE Port \n");
						   pThis->omx_report_error ();
						   break;
                   }
				   pThis->streaming[CAPTURE_PORT] = false;
                   DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH  3 pThis->input_flush_progress =%d \n", pThis->input_flush_progress);
                  if (!pThis->input_flush_progress)
                  {
                    DEBUG_PRINT_LOW(" Output flush done hence issue stop");
					 pThis->post_event ((unsigned int)NULL, VDPP_S_SUCCESS,\
							 OMX_COMPONENT_GENERATE_STOP_DONE);
                  }
                }
              }
            }
            else
            {
              DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
            }
          }
          break;

        case OMX_COMPONENT_GENERATE_START_DONE:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_START_DONE");

          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDPP_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_START_DONE Failure");
              pThis->omx_report_error ();
            }
            else
            {
              DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_START_DONE Success");
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_EXECUTE_PENDING))
              {
                DEBUG_PRINT_LOW(" Move to executing");
                // Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_EXECUTE_PENDING);
                pThis->m_state = OMX_StateExecuting;
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                       OMX_StateExecuting, NULL);
              }
              else if (BITMASK_PRESENT(&pThis->m_flags,
                                       OMX_COMPONENT_PAUSE_PENDING))
              {
                if (/*ioctl (pThis->drv_ctx.video_vpu_fd,
                           VDPP_IOCTL_CMD_PAUSE,NULL ) < */0)
                {
                  DEBUG_PRINT_ERROR(" VDPP_IOCTL_CMD_PAUSE failed");
                  pThis->omx_report_error ();
                }
              }
            }
          }
          else
          {
            DEBUG_PRINT_LOW(" Event Handler callback is NULL");
          }
          break;

        case OMX_COMPONENT_GENERATE_PAUSE_DONE:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_PAUSE_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDPP_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("OMX_COMPONENT_GENERATE_PAUSE_DONE ret failed");
              pThis->omx_report_error ();
            }
            else
            {
              pThis->complete_pending_buffer_done_cbs();
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_PAUSE_PENDING))
              {
                DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_PAUSE_DONE nofity");
                //Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_PAUSE_PENDING);
                pThis->m_state = OMX_StatePause;
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                       OMX_StatePause, NULL);
              }
            }
          }
          else
          {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }

          break;

        case OMX_COMPONENT_GENERATE_RESUME_DONE:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_RESUME_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDPP_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_RESUME_DONE failed");
              pThis->omx_report_error ();
            }
            else
            {
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_EXECUTE_PENDING))
              {
                DEBUG_PRINT_LOW(" Moving the VDPP to execute state");
                // Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_EXECUTE_PENDING);
                pThis->m_state = OMX_StateExecuting;
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                       OMX_StateExecuting,NULL);
              }
            }
          }
          else
          {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }

          break;

        case OMX_COMPONENT_GENERATE_STOP_DONE:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_STOP_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDPP_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_STOP_DONE ret failed");
              pThis->omx_report_error ();
            }
            else
            {
              pThis->complete_pending_buffer_done_cbs();
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_IDLE_PENDING))
              {
                DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_STOP_DONE Success");
                // Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_IDLE_PENDING);
                pThis->m_state = OMX_StateIdle;
                DEBUG_PRINT_LOW(" Move to Idle State, pThis->m_cb.EventHandler = %p", pThis->m_cb.EventHandler);
                pThis->m_cb.EventHandler(&pThis->m_cmp,pThis->m_app_data,
                                         OMX_EventCmdComplete,OMX_CommandStateSet,
                                         OMX_StateIdle,NULL);
                DEBUG_PRINT_LOW(" OMX_COMPONENT_GENERATE_STOP_DONE cb finished");
              }
            }
          }
          else
          {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }

          break;

        case OMX_COMPONENT_GENERATE_PORT_RECONFIG:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_PORT_RECONFIG");

          if (p2 == OMX_IndexParamPortDefinition) {
            pThis->in_reconfig = true;
          }
          if (pThis->m_cb.EventHandler) {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                OMX_EventPortSettingsChanged, p1, p2, NULL );
          } else {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }

          if (pThis->drv_ctx.interlace != V4L2_FIELD_NONE/*VDPP_InterlaceFrameProgressive*/)
          {
            OMX_INTERLACETYPE format = (OMX_INTERLACETYPE)-1;
            OMX_EVENTTYPE event = (OMX_EVENTTYPE)OMX_EventIndexsettingChanged;
            if (pThis->drv_ctx.interlace == V4L2_FIELD_INTERLACED_TB/*VDPP_InterlaceInterleaveFrameTopFieldFirst*/)
                format = OMX_InterlaceInterleaveFrameTopFieldFirst;
            else if (pThis->drv_ctx.interlace == V4L2_FIELD_INTERLACED_BT/*VDPP_InterlaceInterleaveFrameBottomFieldFirst*/)
                format = OMX_InterlaceInterleaveFrameBottomFieldFirst;
            else //unsupported interlace format; raise a error
                event = OMX_EventError;
            if (pThis->m_cb.EventHandler) {
              pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                  event, format, 0, NULL );
            } else {
              DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
            }
          }
        break;

        case OMX_COMPONENT_GENERATE_EOS_DONE:
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_EOS_DONE");
          if (pThis->m_cb.EventHandler) {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data, OMX_EventBufferFlag,
                            OMX_CORE_OUTPUT_PORT_INDEX, OMX_BUFFERFLAG_EOS, NULL );
          } else {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }
          pThis->prev_ts = LLONG_MAX;
          pThis->rst_prev_ts = true;
          break;

        case OMX_COMPONENT_GENERATE_HARDWARE_ERROR:
          DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_HARDWARE_ERROR");
          pThis->omx_report_error ();
          break;

        case OMX_COMPONENT_GENERATE_UNSUPPORTED_SETTING:
          DEBUG_PRINT_ERROR(" OMX_COMPONENT_GENERATE_UNSUPPORTED_SETTING\n");
          pThis->omx_report_unsupported_setting();
          break;

        case OMX_COMPONENT_GENERATE_INFO_PORT_RECONFIG:
        {
          DEBUG_PRINT_HIGH(" Rxd OMX_COMPONENT_GENERATE_INFO_PORT_RECONFIG");
          if (pThis->m_cb.EventHandler) {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                (OMX_EVENTTYPE)OMX_EventIndexsettingChanged, OMX_CORE_OUTPUT_PORT_INDEX, 0, NULL );
          } else {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }
        }
        // extensions
        case OMX_COMPONENT_GENERATE_ACTIVE_REGION_DETECTION_STATUS:
        {
          struct v4l2_rect * ar_result = (struct v4l2_rect *) p1;
          QOMX_ACTIVEREGIONDETECTION_STATUSTYPE arstatus;
          arstatus.nSize = sizeof(QOMX_ACTIVEREGIONDETECTION_STATUSTYPE);
          arstatus.nPortIndex      = 0;
          arstatus.bDetected = OMX_TRUE;
          memcpy(&arstatus.sDetectedRegion, ar_result, sizeof(QOMX_RECTTYPE));
          DEBUG_PRINT_HIGH(" OMX_COMPONENT_GENERATE_ACTIVE_REGION_DETECTION_STATUS");
          // data2 should be (OMX_INDEXTYPE)OMX_QcomIndexConfigActiveRegionDetectionStatus
          // pdata should be QOMX_ACTIVEREGIONDETECTION_STATUSTYPE
          if (pThis->m_cb.EventHandler) {
             pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                 (OMX_EVENTTYPE)OMX_EventIndexsettingChanged, OMX_CORE_OUTPUT_PORT_INDEX, (OMX_INDEXTYPE)OMX_QcomIndexConfigActiveRegionDetectionStatus, &arstatus);
          } else {
            DEBUG_PRINT_ERROR("ERROR: %s()::EventHandler is NULL", __func__);
          }
        }
        default:
          break;
        }
      }
    pthread_mutex_lock(&pThis->m_lock);
    qsize = pThis->m_cmd_q.m_size;
    if (pThis->m_state != OMX_StatePause)
        qsize += (pThis->m_ftb_q.m_size + pThis->m_etb_q.m_size);
    pthread_mutex_unlock(&pThis->m_lock);
  }
  while(qsize>0);

}

int omx_vdpp::update_resolution(uint32_t width, uint32_t height, uint32_t stride, uint32_t scan_lines)
{
	int format_changed = 0;
	if ((height != drv_ctx.video_resolution_input.frame_height) ||
		(width != drv_ctx.video_resolution_input.frame_width)) {
		DEBUG_PRINT_HIGH("NOTE: W/H %d (%d), %d (%d)\n",
			width, drv_ctx.video_resolution_input.frame_width,
			height,drv_ctx.video_resolution_input.frame_height);
		format_changed = 1;
	}
    drv_ctx.video_resolution_input.frame_height = height;
    drv_ctx.video_resolution_input.frame_width = width;
    drv_ctx.video_resolution_input.scan_lines = scan_lines;
    drv_ctx.video_resolution_input.stride = stride;
    rectangle.nLeft = 0;
    rectangle.nTop = 0;
    rectangle.nWidth = drv_ctx.video_resolution_input.frame_width;
    rectangle.nHeight = drv_ctx.video_resolution_input.frame_height;
    return format_changed;
}

/* ======================================================================
FUNCTION
  omx_vdpp::ComponentInit

DESCRIPTION
  Initialize the component.

PARAMETERS
  ctxt -- Context information related to the self.
  id   -- Event identifier. This could be any of the following:
          1. Command completion event
          2. Buffer done callback event
          3. Frame done callback event

RETURN VALUE
  None.

========================================================================== */
OMX_ERRORTYPE omx_vdpp::component_init(OMX_STRING role)
{

	OMX_ERRORTYPE eRet = OMX_ErrorNone;
	struct v4l2_format fmt;
	int fds[2];
    int fctl[2];
	int ret=0;
    int i = 0;
    int sessionNum = 0;

    errno = 0;

#ifndef STUB_VPU
	drv_ctx.video_vpu_fd = openInput("msm_vpu");
#else
    drv_ctx.video_vpu_fd = 1;
#endif
	DEBUG_PRINT_HIGH(" omx_vdpp::component_init(): Open returned fd %d, errno %d",
			drv_ctx.video_vpu_fd, errno);

	if(drv_ctx.video_vpu_fd == 0){
	    DEBUG_PRINT_ERROR("omx_vdpp:: Got fd as 0 for vpu, Opening again\n");
	    drv_ctx.video_vpu_fd = openInput("msm_vpu");
	}

	if(drv_ctx.video_vpu_fd < 0)
	{
		DEBUG_PRINT_ERROR("omx_vdpp::Comp Init Returning failure, errno %d\n", errno);
		return OMX_ErrorInsufficientResources;
	}

#ifndef STUB_VPU
    // query number of sessions and attach to session #1
    /* Check how many sessions are suported by H/W */
    ret = ioctl(drv_ctx.video_vpu_fd, VPU_QUERY_SESSIONS,
				&drv_ctx.sessionsSupported);
    if (ret < 0)
    {
        DEBUG_PRINT_ERROR("QUERY_SESSIONS: VPU_QUERY_SESSIONS failed.");
        close(drv_ctx.video_vpu_fd);
        drv_ctx.video_vpu_fd = 0;
        return OMX_ErrorInsufficientResources;
    }
    else
    {
        DEBUG_PRINT_HIGH("QUERY_SESSIONS: The number of sessions supported are %d.",
             drv_ctx.sessionsSupported);
    }
#endif

    /* Attach Client to Session. */
    sessionNum = VDPP_SESSION;

#ifndef STUB_VPU
    ret = ioctl(drv_ctx.video_vpu_fd, VPU_ATTACH_TO_SESSION, &sessionNum);
    if (ret < 0)
    {
        if( errno == EINVAL )
            DEBUG_PRINT_ERROR("VPU_ATTACH_TO_SESSION: session %d is out of valid "
			"range.", sessionNum);
        else if( errno == EBUSY)
            DEBUG_PRINT_ERROR("VPU_ATTACH_TO_SESSION: max. allowed number of"
                    "clients attached to session.");
        else
            DEBUG_PRINT_ERROR("VPU_ATTACH_TO_SESSION: failed for unknown reason.");
        return OMX_ErrorUndefined;
    }
    else
    {
        DEBUG_PRINT_HIGH("VPU_ATTACH_TO_SESSION: client successfully attached "
		"to session.");
    }
#endif
    drv_ctx.sessionAttached = sessionNum;

	drv_ctx.frame_rate.fps_numerator = DEFAULT_FPS;
	drv_ctx.frame_rate.fps_denominator = 1;

    ret = subscribe_to_events(drv_ctx.video_vpu_fd);

    /* create control pipes */
    if (!ret)
    {
	    if(pipe(fctl))
	    {
		    DEBUG_PRINT_ERROR("pipe creation failed\n");
		    eRet = OMX_ErrorInsufficientResources;
	    }
	    else
	    {
		    int temp2[2];
		    if(fctl[0] == 0 || fctl[1] == 0)
		    {
			    if (pipe (temp2))
			    {
				    DEBUG_PRINT_ERROR("pipe creation failed\n");
				    return OMX_ErrorInsufficientResources;
			    }
			    fctl[0] = temp2 [0];
			    fctl[1] = temp2 [1];
		    }
		    m_ctrl_in = fctl[0];
		    m_ctrl_out = fctl[1];

            fcntl(m_ctrl_in, F_SETFL, O_NONBLOCK);
            fcntl(m_ctrl_out, F_SETFL, O_NONBLOCK);
        }
    }

    if (!ret) {
      async_thread_created = true;
      ret = pthread_create(&async_thread_id,0,async_message_thread,this);
	}
    if(ret) {
	  DEBUG_PRINT_ERROR(" Failed to create async_message_thread \n");
	  async_thread_created = false;
	  return OMX_ErrorInsufficientResources;
    }

#ifdef INPUT_BUFFER_LOG
	inputBufferFile = open(inputfilename,  O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
    if(inputBufferFile < 0)
    {
	  DEBUG_PRINT_ERROR(" Failed to create inputBufferFile 0, errno = %d\n", errno);
    }

#endif

#ifdef OUTPUT_BUFFER_LOG
	outputBufferFile = open(outputfilename,  O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
    if(outputBufferFile < 0)
    {
	  DEBUG_PRINT_ERROR(" Failed to create outputBufferFile 0 , errno = %d\n", errno);
    }
#endif

#ifdef OUTPUT_EXTRADATA_LOG
	outputExtradataFile = open (ouputextradatafilename, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
    if(outputExtradataFile == -1)
    {
	  DEBUG_PRINT_ERROR(" Failed to create outputExtradataFile , errno = %d\n", errno);
    }
#endif

	// Copy the role information which provides the vdpp kind
	strlcpy(drv_ctx.kind,role,128);

    // Default set to progressive mode for 8084. drv_ctx.interlace will be changed by
    // interlace format filed of OMX buffer header extra data. User can also overwrite
    // this setting by OMX_IndexParamInterlaceFormat
    drv_ctx.interlace = V4L2_FIELD_NONE;

    // Default input pixel format
    output_capability=V4L2_PIX_FMT_NV12;

	if (eRet == OMX_ErrorNone)
	{
        // set default output format to V4L2_PIX_FMT_NV12. User can use SetParam
        // with OMX_IndexParamVideoPortFormat to set vdpp output format
		drv_ctx.output_format = V4L2_PIX_FMT_NV12;
		capture_capability    = V4L2_PIX_FMT_NV12;

		struct v4l2_capability cap;
#ifndef STUB_VPU
		ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_QUERYCAP, &cap);
		if (ret) {
		            DEBUG_PRINT_ERROR("Failed to query capabilities\n");
				    return OMX_ErrorUndefined;
		} else {
		        DEBUG_PRINT_HIGH("Capabilities: driver_name = %s, card = %s, bus_info = %s,"
				" version = %d, capabilities = %x\n", cap.driver, cap.card,
				cap.bus_info, cap.version, cap.capabilities);

            if ((cap.capabilities & V4L2_CAP_STREAMING) == 0)
            {
                DEBUG_PRINT_ERROR("device does not support streaming i/o\n");
				return OMX_ErrorInsufficientResources;
            }
		}
#endif
        // set initial input h/w and pixel format
        update_resolution(640, 480, 640, 480);

		fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fmt.fmt.pix_mp.height = drv_ctx.video_resolution_input.frame_height;
		fmt.fmt.pix_mp.width = drv_ctx.video_resolution_input.frame_width;
        if (V4L2_FIELD_NONE == drv_ctx.interlace)
        {
            fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        }
        else
        {
		fmt.fmt.pix_mp.field = V4L2_FIELD_INTERLACED;
        }
		fmt.fmt.pix_mp.pixelformat = output_capability;

        // NV12 has 2 planes.
        /* Set format for each plane. */
        setFormatParams(output_capability, drv_ctx.input_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
        for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
        {
            fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.input_bytesperpixel[i] * fmt.fmt.pix_mp.height);
            fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.input_bytesperpixel[0]); // NV12 UV plane has the same width as Y, but 1/2 YH
            DEBUG_PRINT_HIGH(" fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n ", i, fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
        }
#ifndef STUB_VPU
		ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);

		if (ret) {
			        DEBUG_PRINT_ERROR("Failed to set format on output port\n");
				    return OMX_ErrorUndefined;
				}
		DEBUG_PRINT_HIGH(" Set Format was successful drv_ctx.interlace = %d\n ", drv_ctx.interlace);
#endif
        // set initial output format
        // initial input/output resolution are the same. portdefinition changes both
        memset(&fmt, 0, sizeof(fmt));

		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.height = drv_ctx.video_resolution_input.frame_height;
		fmt.fmt.pix_mp.width = drv_ctx.video_resolution_input.frame_width;
	    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
		fmt.fmt.pix_mp.pixelformat = capture_capability;
        DEBUG_PRINT_HIGH("VP output frame width = %d, height = %d", fmt.fmt.pix_mp.width,
                fmt.fmt.pix_mp.height);

        setFormatParams(capture_capability, drv_ctx.output_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
        for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
        {
            fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width *
                                                                        drv_ctx.output_bytesperpixel[i] *
                                                                        fmt.fmt.pix_mp.height);
            fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[0]);
            DEBUG_PRINT_HIGH(" fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n ", i, fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
        }
#ifndef STUB_VPU
        ret  = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
        if (ret < 0)
        {
            DEBUG_PRINT_ERROR("VIDIOC_S_FMT setup VP output format error");
            return OMX_ErrorUndefined;
        }
#endif
        // update initial output resolution
        drv_ctx.video_resolution_output.frame_height = fmt.fmt.pix_mp.height;
        drv_ctx.video_resolution_output.frame_width = fmt.fmt.pix_mp.width;
        drv_ctx.video_resolution_output.scan_lines = fmt.fmt.pix_mp.height;
        drv_ctx.video_resolution_output.stride = fmt.fmt.pix_mp.width;

		if (ret) {
			        DEBUG_PRINT_ERROR("Failed to set format on capture port\n");
                    return OMX_ErrorUndefined;
				}
		DEBUG_PRINT_HIGH(" Set Format was successful \n ");

		/*Get the Buffer requirements for input and output ports*/
		drv_ctx.ip_buf.buffer_type = VDPP_BUFFER_TYPE_INPUT;
		drv_ctx.op_buf.buffer_type = VDPP_BUFFER_TYPE_OUTPUT;

		drv_ctx.op_buf.alignment=SZ_4K;
		drv_ctx.ip_buf.alignment=SZ_4K;

        m_state = OMX_StateLoaded;

        eRet=get_buffer_req(&drv_ctx.ip_buf);
        DEBUG_PRINT_HIGH("Input Buffer Size =%d \n ",drv_ctx.ip_buf.buffer_size);
        get_buffer_req(&drv_ctx.op_buf);

        /* create pipes for message thread*/
		if(pipe(fds))
		{
			DEBUG_PRINT_ERROR("pipe creation failed\n");
			eRet = OMX_ErrorInsufficientResources;
		}
		else
		{
			int temp1[2];
			if(fds[0] == 0 || fds[1] == 0)
			{
				if (pipe (temp1))
				{
					DEBUG_PRINT_ERROR("pipe creation failed\n");
					return OMX_ErrorInsufficientResources;
				}
				fds[0] = temp1 [0];
				fds[1] = temp1 [1];
			}
			m_pipe_in = fds[0];
			m_pipe_out = fds[1];
			msg_thread_created = true;
			ret = pthread_create(&msg_thread_id,0,message_thread,this);

			if(ret < 0)
			{
				DEBUG_PRINT_ERROR(" component_init(): message_thread creation failed");
				msg_thread_created = false;
				eRet = OMX_ErrorInsufficientResources;
			}
		}
	}

	if (eRet != OMX_ErrorNone)
	{
		DEBUG_PRINT_ERROR(" Component Init Failed");
	}
	else
	{
		DEBUG_PRINT_HIGH(" omx_vdpp::component_init() success");
	}

	return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::GetComponentVersion

DESCRIPTION
  Returns the component version.

PARAMETERS
  TBD.

RETURN VALUE
  OMX_ErrorNone.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::get_component_version
                                     (
                                      OMX_IN OMX_HANDLETYPE hComp,
                                      OMX_OUT OMX_STRING componentName,
                                      OMX_OUT OMX_VERSIONTYPE* componentVersion,
                                      OMX_OUT OMX_VERSIONTYPE* specVersion,
                                      OMX_OUT OMX_UUIDTYPE* componentUUID
                                      )
{
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Comp Version in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
  /* TBD -- Return the proper version */
  if (specVersion)
  {
    specVersion->nVersion = OMX_SPEC_VERSION;
  }
  return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdpp::SendCommand

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::send_command(OMX_IN OMX_HANDLETYPE hComp,
                                      OMX_IN OMX_COMMANDTYPE cmd,
                                      OMX_IN OMX_U32 param1,
                                      OMX_IN OMX_PTR cmdData
                                      )
{
    DEBUG_PRINT_LOW(" send_command: Recieved a Command from Client cmd = %d", cmd);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("ERROR: Send Command in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if (cmd == OMX_CommandFlush && param1 != OMX_CORE_INPUT_PORT_INDEX
      && param1 != OMX_CORE_OUTPUT_PORT_INDEX && param1 != OMX_ALL)
    {
      DEBUG_PRINT_ERROR(" send_command(): ERROR OMX_CommandFlush "
        "to invalid port: %lu", param1);
      return OMX_ErrorBadPortIndex;
    }
    post_event((unsigned)cmd,(unsigned)param1,OMX_COMPONENT_GENERATE_COMMAND);
    sem_wait(&m_cmd_lock);
    DEBUG_PRINT_LOW(" send_command: Command Processed cmd = %d\n", cmd);
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdpp::SendCommand

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::send_command_proxy(OMX_IN OMX_HANDLETYPE hComp,
                                            OMX_IN OMX_COMMANDTYPE cmd,
                                            OMX_IN OMX_U32 param1,
                                            OMX_IN OMX_PTR cmdData
                                            )
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_STATETYPE eState = (OMX_STATETYPE) param1;
  int bFlag = 1,sem_posted = 0,ret=0;

  DEBUG_PRINT_LOW(" send_command_proxy(): cmd = %d", cmd);
  DEBUG_PRINT_HIGH("end_command_proxy(): Current State %d, Expected State %d",
    m_state, eState);

  if(cmd == OMX_CommandStateSet)
  {
    DEBUG_PRINT_HIGH("send_command_proxy(): OMX_CommandStateSet issued");
    DEBUG_PRINT_HIGH("Current State %d, Expected State %d", m_state, eState);
    /***************************/
    /* Current State is Loaded */
    /***************************/
    if(m_state == OMX_StateLoaded)
    {
      if(eState == OMX_StateIdle)
      {
        //if all buffers are allocated or all ports disabled
        if(allocate_done() ||
          (m_inp_bEnabled == OMX_FALSE && m_out_bEnabled == OMX_FALSE))
        {
          DEBUG_PRINT_LOW("send_command_proxy(): Loaded-->Idle\n");
        }
        else
        {
          DEBUG_PRINT_LOW("send_command_proxy(): Loaded-->Idle-Pending\n");
          BITMASK_SET(&m_flags, OMX_COMPONENT_IDLE_PENDING);
          // Skip the event notification
          bFlag = 0;
        }
      }
      /* Requesting transition from Loaded to Loaded */
      else if(eState == OMX_StateLoaded)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Loaded-->Loaded\n");
        post_event(OMX_EventError,OMX_ErrorSameState,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorSameState;
      }
      /* Requesting transition from Loaded to WaitForResources */
      else if(eState == OMX_StateWaitForResources)
      {
        /* Since error is None , we will post an event
           at the end of this function definition */
        DEBUG_PRINT_LOW("send_command_proxy(): Loaded-->WaitForResources\n");
      }
      /* Requesting transition from Loaded to Executing */
      else if(eState == OMX_StateExecuting)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Loaded-->Executing\n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Loaded to Pause */
      else if(eState == OMX_StatePause)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Loaded-->Pause\n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Loaded to Invalid */
      else if(eState == OMX_StateInvalid)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Loaded-->Invalid\n");
        post_event(OMX_EventError,eState,OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorInvalidState;
      }
      else
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Loaded-->Invalid(%d Not Handled)\n",\
                          eState);
        eRet = OMX_ErrorBadParameter;
      }
    }

    /***************************/
    /* Current State is IDLE */
    /***************************/
    else if(m_state == OMX_StateIdle)
    {
      if(eState == OMX_StateLoaded)
      {
        if(release_done())
        {
          /*
             Since error is None , we will post an event at the end
             of this function definition
          */
          DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Loaded\n");
        }
        else
        {
          DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Loaded-Pending\n");
          BITMASK_SET(&m_flags, OMX_COMPONENT_LOADING_PENDING);
          // Skip the event notification
          bFlag = 0;
        }
      }
      /* Requesting transition from Idle to Executing */
      else if(eState == OMX_StateExecuting)
      {
	    DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Executing\n");
        //BITMASK_SET(&m_flags, OMX_COMPONENT_EXECUTE_PENDING);
        bFlag = 1;
	    m_state=OMX_StateExecuting;
	    DEBUG_PRINT_HIGH("Stream On CAPTURE Was successful\n");
      }
      /* Requesting transition from Idle to Idle */
      else if(eState == OMX_StateIdle)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Idle-->Idle\n");
        post_event(OMX_EventError,OMX_ErrorSameState,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorSameState;
      }
      /* Requesting transition from Idle to WaitForResources */
      else if(eState == OMX_StateWaitForResources)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Idle-->WaitForResources\n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
       /* Requesting transition from Idle to Pause */
       else if(eState == OMX_StatePause)
      {
         /*To pause the Video core we need to start the driver*/
         if (/*ioctl (drv_ctx.video_vpu_fd,VDPP_IOCTL_CMD_START,
                    NULL) < */0)
         {
           DEBUG_PRINT_ERROR(" VDPP_IOCTL_CMD_START FAILED");
           omx_report_error ();
           eRet = OMX_ErrorHardware;
         }
         else
         {
           BITMASK_SET(&m_flags,OMX_COMPONENT_PAUSE_PENDING);
           DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Pause\n");
           bFlag = 0;
         }
      }
      /* Requesting transition from Idle to Invalid */
       else if(eState == OMX_StateInvalid)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Idle-->Invalid\n");
        post_event(OMX_EventError,eState,OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorInvalidState;
      }
      else
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Idle --> %d Not Handled\n",eState);
        eRet = OMX_ErrorBadParameter;
      }
    }

    /******************************/
    /* Current State is Executing */
    /******************************/
    else if(m_state == OMX_StateExecuting)
    {
       DEBUG_PRINT_LOW(" Command Recieved in OMX_StateExecuting");
       /* Requesting transition from Executing to Idle */
       if(eState == OMX_StateIdle)
	   {
		   /* Since error is None , we will post an event
			  at the end of this function definition
			*/
		   DEBUG_PRINT_LOW(" send_command_proxy(): Executing --> Idle \n");
		   BITMASK_SET(&m_flags,OMX_COMPONENT_IDLE_PENDING);
		   if(!sem_posted)
		   {
			   sem_posted = 1;
			   sem_post (&m_cmd_lock);
			   execute_omx_flush(OMX_ALL);
		   }
		   bFlag = 0;
	   }
       /* Requesting transition from Executing to Paused */
       else if(eState == OMX_StatePause)
       {
         DEBUG_PRINT_LOW(" PAUSE Command Issued");
         m_state = OMX_StatePause;
         bFlag = 1;
       }
       /* Requesting transition from Executing to Loaded */
       else if(eState == OMX_StateLoaded)
       {
         DEBUG_PRINT_ERROR(" send_command_proxy(): Executing --> Loaded \n");
         post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorIncorrectStateTransition;
       }
       /* Requesting transition from Executing to WaitForResources */
       else if(eState == OMX_StateWaitForResources)
       {
         DEBUG_PRINT_ERROR(" send_command_proxy(): Executing --> WaitForResources \n");
         post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorIncorrectStateTransition;
       }
       /* Requesting transition from Executing to Executing */
       else if(eState == OMX_StateExecuting)
       {
         DEBUG_PRINT_ERROR(" send_command_proxy(): Executing --> Executing \n");
         post_event(OMX_EventError,OMX_ErrorSameState,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorSameState;
       }
       /* Requesting transition from Executing to Invalid */
       else if(eState == OMX_StateInvalid)
       {
         DEBUG_PRINT_ERROR(" send_command_proxy(): Executing --> Invalid \n");
         post_event(OMX_EventError,eState,OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorInvalidState;
       }
       else
       {
         DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Executing --> %d Not Handled\n",eState);
         eRet = OMX_ErrorBadParameter;
       }
    }
    /***************************/
    /* Current State is Pause  */
    /***************************/
    else if(m_state == OMX_StatePause)
    {
      /* Requesting transition from Pause to Executing */
      if(eState == OMX_StateExecuting)
      {
        DEBUG_PRINT_LOW(" Pause --> Executing \n");
        m_state = OMX_StateExecuting;
        bFlag = 1;
      }
      /* Requesting transition from Pause to Idle */
      else if(eState == OMX_StateIdle)
      {
        /* Since error is None , we will post an event
        at the end of this function definition */
        DEBUG_PRINT_LOW(" Pause --> Idle \n");
         BITMASK_SET(&m_flags,OMX_COMPONENT_IDLE_PENDING);
         if(!sem_posted)
         {
           sem_posted = 1;
           sem_post (&m_cmd_lock);
           execute_omx_flush(OMX_ALL);
         }
         bFlag = 0;
      }
      /* Requesting transition from Pause to loaded */
      else if(eState == OMX_StateLoaded)
      {
        DEBUG_PRINT_ERROR(" Pause --> loaded \n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Pause to WaitForResources */
      else if(eState == OMX_StateWaitForResources)
      {
        DEBUG_PRINT_ERROR(" Pause --> WaitForResources \n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Pause to Pause */
      else if(eState == OMX_StatePause)
      {
        DEBUG_PRINT_ERROR(" Pause --> Pause \n");
        post_event(OMX_EventError,OMX_ErrorSameState,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorSameState;
      }
       /* Requesting transition from Pause to Invalid */
      else if(eState == OMX_StateInvalid)
      {
        DEBUG_PRINT_ERROR(" Pause --> Invalid \n");
        post_event(OMX_EventError,eState,OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorInvalidState;
      }
      else
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Paused --> %d Not Handled\n",eState);
        eRet = OMX_ErrorBadParameter;
      }
    }
     /***************************/
    /* Current State is WaitForResources  */
    /***************************/
    else if(m_state == OMX_StateWaitForResources)
    {
      /* Requesting transition from WaitForResources to Loaded */
      if(eState == OMX_StateLoaded)
      {
        /* Since error is None , we will post an event
        at the end of this function definition */
        DEBUG_PRINT_LOW("send_command_proxy(): WaitForResources-->Loaded\n");
      }
      /* Requesting transition from WaitForResources to WaitForResources */
      else if (eState == OMX_StateWaitForResources)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): WaitForResources-->WaitForResources\n");
        post_event(OMX_EventError,OMX_ErrorSameState,
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorSameState;
      }
      /* Requesting transition from WaitForResources to Executing */
      else if(eState == OMX_StateExecuting)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): WaitForResources-->Executing\n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from WaitForResources to Pause */
      else if(eState == OMX_StatePause)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): WaitForResources-->Pause\n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from WaitForResources to Invalid */
      else if(eState == OMX_StateInvalid)
      {
        DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): WaitForResources-->Invalid\n");
        post_event(OMX_EventError,eState,OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorInvalidState;
      }
      /* Requesting transition from WaitForResources to Loaded -
      is NOT tested by Khronos TS */

    }
    else
    {
      DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): %d --> %d(Not Handled)\n",m_state,eState);
      eRet = OMX_ErrorBadParameter;
    }
  }
  /********************************/
  /* Current State is Invalid */
  /*******************************/
  else if(m_state == OMX_StateInvalid)
  {
    /* State Transition from Inavlid to any state */
    if(eState == (OMX_StateLoaded || OMX_StateWaitForResources
                  || OMX_StateIdle || OMX_StateExecuting
                  || OMX_StatePause || OMX_StateInvalid))
    {
      DEBUG_PRINT_ERROR("ERROR::send_command_proxy(): Invalid -->Loaded\n");
      post_event(OMX_EventError,OMX_ErrorInvalidState,\
                 OMX_COMPONENT_GENERATE_EVENT);
      eRet = OMX_ErrorInvalidState;
    }
  }
  else if (cmd == OMX_CommandFlush)
  {
    DEBUG_PRINT_HIGH(" send_command_proxy(): OMX_CommandFlush issued "
        "with param1: 0x%x", param1);
    if(OMX_CORE_INPUT_PORT_INDEX == param1 || OMX_ALL == param1)
    {   // do not call flush ioctl if there is no buffer queued. Just return callback
#ifndef STUB_VPU // VPU stub doesn't set any streaming flag
        if(!streaming[OUTPUT_PORT])
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,OMX_CommandFlush,
                                               OMX_CORE_INPUT_PORT_INDEX,NULL );
            sem_posted = 1;
            sem_post (&m_cmd_lock);
        }
        else
#endif
        {
            BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_FLUSH_PENDING);
        }
    }
    if(OMX_CORE_OUTPUT_PORT_INDEX == param1 || OMX_ALL == param1)
    {
      BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_FLUSH_PENDING);
    }
    if (!sem_posted){
      sem_posted = 1;
      DEBUG_PRINT_LOW(" Set the Semaphore");
      sem_post (&m_cmd_lock);
      execute_omx_flush(param1);
    }
    bFlag = 0;
  }
  else if ( cmd == OMX_CommandPortEnable)
  {
    DEBUG_PRINT_HIGH(" send_command_proxy(): OMX_CommandPortEnable issued "
        "with param1: %lu", param1);
    if(param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL)
      {
        m_inp_bEnabled = OMX_TRUE;

        if( (m_state == OMX_StateLoaded &&
             !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            || allocate_input_done())
        {
          post_event(OMX_CommandPortEnable,OMX_CORE_INPUT_PORT_INDEX,
                     OMX_COMPONENT_GENERATE_EVENT);
        }
        else
        {
          DEBUG_PRINT_LOW("send_command_proxy(): Disabled-->Enabled Pending\n");
          BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING);
          // Skip the event notification
          bFlag = 0;
        }
      }
      if(param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL)
      {
          DEBUG_PRINT_LOW(" Enable output Port command recieved");
          m_out_bEnabled = OMX_TRUE;

          if( (m_state == OMX_StateLoaded &&
              !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
              || (allocate_output_done()))
          {
             post_event(OMX_CommandPortEnable,OMX_CORE_OUTPUT_PORT_INDEX,
                        OMX_COMPONENT_GENERATE_EVENT);

          }
          else
          {
              DEBUG_PRINT_LOW("send_command_proxy(): Disabled-->Enabled Pending\n");
              BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
              // Skip the event notification
              bFlag = 0;
          }
      }
  }
  else if (cmd == OMX_CommandPortDisable)
  {
      DEBUG_PRINT_HIGH(" send_command_proxy(): OMX_CommandPortDisable issued"
          "with param1: %lu m_state = %d, streaming[OUTPUT_PORT] = %d", param1, m_state, streaming[OUTPUT_PORT]);
      if(param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL)
      {
          m_inp_bEnabled = OMX_FALSE;
          if((m_state == OMX_StateLoaded || m_state == OMX_StateIdle)
              && release_input_done())
          {
             post_event(OMX_CommandPortDisable,OMX_CORE_INPUT_PORT_INDEX,
                        OMX_COMPONENT_GENERATE_EVENT);
             DEBUG_PRINT_LOW("OMX_CommandPortDisable 1");
          }
          else
          {
             BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_DISABLE_PENDING);
             DEBUG_PRINT_LOW("OMX_CommandPortDisable 2");
             if(m_state == OMX_StatePause ||m_state == OMX_StateExecuting)
             {
               if(!sem_posted)
               {
                 sem_posted = 1;
                 sem_post (&m_cmd_lock);
               }
               if(!streaming[OUTPUT_PORT])
               {
                    DEBUG_PRINT_LOW("OMX_CommandPortDisable 3 ");
                    //IL client calls disable port and then free buffers.
                    //from free buffer, disable port done event will be sent
               }
               else
               {
                   execute_omx_flush(OMX_CORE_INPUT_PORT_INDEX);
               }
             }

             // Skip the event notification
             bFlag = 0;
          }
      }
      if(param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL)
      {
          m_out_bEnabled = OMX_FALSE;
          DEBUG_PRINT_LOW(" Disable output Port command recieved m_state = %d", m_state);
          if((m_state == OMX_StateLoaded || m_state == OMX_StateIdle)
              && release_output_done())
          {
             post_event(OMX_CommandPortDisable,OMX_CORE_OUTPUT_PORT_INDEX,\
                        OMX_COMPONENT_GENERATE_EVENT);
          }
          else
         {
            BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
            if(m_state == OMX_StatePause ||m_state == OMX_StateExecuting)
            {
              if (!sem_posted)
              {
                sem_posted = 1;
                sem_post (&m_cmd_lock);
              }
                BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_FLUSH_IN_DISABLE_PENDING);
                execute_omx_flush(OMX_CORE_OUTPUT_PORT_INDEX);
            }
            // Skip the event notification
            bFlag = 0;

         }
      }
  }
  else
  {
    DEBUG_PRINT_ERROR("Error: Invalid Command other than StateSet (%d)\n",cmd);
    eRet = OMX_ErrorNotImplemented;
  }
  if(eRet == OMX_ErrorNone && bFlag)
  {
    post_event(cmd,eState,OMX_COMPONENT_GENERATE_EVENT);
  }
  if(!sem_posted)
  {
    sem_post(&m_cmd_lock);
  }

  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::ExecuteOmxFlush

DESCRIPTION
  Executes the OMX flush.

PARAMETERS
  flushtype - input flush(1)/output flush(0)/ both.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdpp::execute_omx_flush(OMX_U32 flushType)
{
  bool bRet = false;
  struct v4l2_requestbuffers bufreq;
  struct vdpp_msginfo vdpp_msg;
  enum v4l2_buf_type buf_type;
  unsigned i = 0;

  DEBUG_PRINT_LOW("in %s flushType = %d", __func__, flushType);
  memset((void *)&vdpp_msg,0,sizeof(vdpp_msg));

  switch (flushType)
  {
    case OMX_CORE_INPUT_PORT_INDEX:
      input_flush_progress = true;

    break;
    case OMX_CORE_OUTPUT_PORT_INDEX:
      output_flush_progress = true;

    break;
    default:
      input_flush_progress = true;
      output_flush_progress = true;
  }

  DEBUG_PRINT_HIGH("omx_vdpp::execute_omx_flush m_ftb_q.m_size = %d, m_etb_q.m_size = %d\n", m_ftb_q.m_size, m_etb_q.m_size);
  // vpu doesn't have flush right now, simulate flush done from here
#ifdef STUB_VPU
  {
    // dq output
    if(output_flush_progress)
    {
	  vdpp_msg.msgcode=VDPP_MSG_RESP_FLUSH_OUTPUT_DONE;
	  vdpp_msg.status_code=VDPP_S_SUCCESS;
	  DEBUG_PRINT_HIGH("Simulate VDPP Output Flush Done Recieved From Driver\n");
	  if (async_message_process(this,&vdpp_msg) < 0) {
		    DEBUG_PRINT_HIGH(" VDPP Output Flush Done returns < 0  \n");
	  }
    }

    // dq input
    if(input_flush_progress)
    {
	  vdpp_msg.msgcode=VDPP_MSG_RESP_FLUSH_INPUT_DONE;
	  vdpp_msg.status_code=VDPP_S_SUCCESS;
	  DEBUG_PRINT_HIGH("Simulate VDPP Input Flush Done Recieved From Driver \n");
	  if (async_message_process(this,&vdpp_msg) < 0) {
		    DEBUG_PRINT_HIGH(" VDPP Input Flush Done returns < 0  \n");
	  }

    }

  }
#else
    // flush input port
    if(input_flush_progress)
    {
		buf_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		if(ioctl(drv_ctx.video_vpu_fd, VPU_FLUSH_BUFS, &buf_type))
        {
           DEBUG_PRINT_ERROR("VDPP input Flush error! \n");
           return false;
        }
        else
        {
            DEBUG_PRINT_LOW("VDPP input Flush success! \n");
        }
    }

    // flush output port
    if(output_flush_progress)
    {
		buf_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if(ioctl(drv_ctx.video_vpu_fd, VPU_FLUSH_BUFS, &buf_type))
        {
           DEBUG_PRINT_ERROR("VDPP output Flush error! \n");
           return false;
        }
        else
        {
            DEBUG_PRINT_LOW("VDPP output Flush success! \n");
        }
    }
#endif

  return bRet;
}
/*=========================================================================
FUNCTION : execute_output_flush

DESCRIPTION
  Executes the OMX flush at OUTPUT PORT.

PARAMETERS
  None.

RETURN VALUE
  true/false
==========================================================================*/
bool omx_vdpp::execute_output_flush()
{
  unsigned      p1 = 0; // Parameter - 1
  unsigned      p2 = 0; // Parameter - 2
  unsigned      ident = 0;
  bool bRet = true;

  /*Generate FBD for all Buffers in the FTBq*/
  pthread_mutex_lock(&m_lock);
  DEBUG_PRINT_LOW(" Initiate Output Flush, m_ftb_q.m_size = %d", m_ftb_q.m_size);
  while (m_ftb_q.m_size)
  {
    DEBUG_PRINT_LOW(" Buffer queue size %d pending buf cnt %d",
                       m_ftb_q.m_size,pending_output_buffers);
    m_ftb_q.pop_entry(&p1,&p2,&ident);
    DEBUG_PRINT_LOW(" ID(%x) P1(%x) P2(%x)", ident, p1, p2);
    if(ident == m_fill_output_msg )
    {
      m_cb.FillBufferDone(&m_cmp, m_app_data, (OMX_BUFFERHEADERTYPE *)p2);
    }
    else if (ident == OMX_COMPONENT_GENERATE_FBD)
    {
      fill_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p1);
    }
  }
  pthread_mutex_unlock(&m_lock);
  output_flush_progress = false;

  DEBUG_PRINT_HIGH(" OMX flush o/p Port complete PenBuf(%d), output_qbuf_count(%d), output_dqbuf_count(%d)",
      pending_output_buffers, output_qbuf_count, output_dqbuf_count);
  return bRet;
}
/*=========================================================================
FUNCTION : execute_input_flush

DESCRIPTION
  Executes the OMX flush at INPUT PORT.

PARAMETERS
  None.

RETURN VALUE
  true/false
==========================================================================*/
bool omx_vdpp::execute_input_flush()
{
  unsigned       i =0;
  unsigned      p1 = 0; // Parameter - 1
  unsigned      p2 = 0; // Parameter - 2
  unsigned      ident = 0;
  bool bRet = true;

  /*Generate EBD for all Buffers in the ETBq*/
  DEBUG_PRINT_LOW(" Initiate Input Flush \n");

  pthread_mutex_lock(&m_lock);
  DEBUG_PRINT_LOW(" Check if the Queue is empty \n");
  while (m_etb_q.m_size)
  {
    DEBUG_PRINT_LOW(" m_etb_q.m_size = %d \n", m_etb_q.m_size);
    m_etb_q.pop_entry(&p1,&p2,&ident);
    DEBUG_PRINT_LOW("ident = %d \n", ident);
    if (ident == OMX_COMPONENT_GENERATE_ETB_ARBITRARY)
    {
      DEBUG_PRINT_LOW(" Flush Input Heap Buffer %p",(OMX_BUFFERHEADERTYPE *)p2);
      m_cb.EmptyBufferDone(&m_cmp ,m_app_data, (OMX_BUFFERHEADERTYPE *)p2);
    }
    else if(ident == OMX_COMPONENT_GENERATE_ETB)
    {
      pending_input_buffers++;
      DEBUG_PRINT_LOW(" Flush Input OMX_COMPONENT_GENERATE_ETB %p, pending_input_buffers %d",
        (OMX_BUFFERHEADERTYPE *)p2, pending_input_buffers);
      empty_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p2);
    }
    else if (ident == OMX_COMPONENT_GENERATE_EBD)
    {
      DEBUG_PRINT_LOW(" Flush Input OMX_COMPONENT_GENERATE_EBD %p",
        (OMX_BUFFERHEADERTYPE *)p1);
      empty_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p1);
    }
  }

  pthread_mutex_unlock(&m_lock);
  input_flush_progress = false;

  prev_ts = LLONG_MAX;
  rst_prev_ts = true;

#ifdef _ANDROID_
  if (m_debug_timestamp)
  {
    m_timestamp_list.reset_ts_list();
  }
#endif

  DEBUG_PRINT_HIGH(" OMX flush i/p Port complete PenBuf(%d), input_qbuf_count(%d), input_dqbuf_count(%d)",
      pending_input_buffers, input_qbuf_count, input_dqbuf_count);
  return bRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::SendCommandEvent

DESCRIPTION
  Send the event to VDPP pipe.  This is needed to generate the callbacks
  in VDPP thread context.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdpp::post_event(unsigned int p1,
                          unsigned int p2,
                          unsigned int id)
{
  bool bRet = false;

  pthread_mutex_lock(&m_lock);
  //DEBUG_PRINT_LOW("m_fill_output_msg = %d, OMX_COMPONENT_GENERATE_FBD = %d, id = %d", m_fill_output_msg, OMX_COMPONENT_GENERATE_FBD, id);
  if (id == m_fill_output_msg ||
      id == OMX_COMPONENT_GENERATE_FBD)
  {
    //DEBUG_PRINT_LOW(" post_event p2 = 0x%x, id = 0x%x", p2, id);
    m_ftb_q.insert_entry(p1,p2,id);
  }
  else if (id == OMX_COMPONENT_GENERATE_ETB ||
           id == OMX_COMPONENT_GENERATE_EBD ||
           id == OMX_COMPONENT_GENERATE_ETB_ARBITRARY)
  {
	  m_etb_q.insert_entry(p1,p2,id);
  }
  else
  {
    m_cmd_q.insert_entry(p1,p2,id);
  }

  bRet = true;
  //DEBUG_PRINT_LOW(" Value of this pointer in post_event %p, id = %d",this, id);
  post_message(this, id);

  pthread_mutex_unlock(&m_lock);

  return bRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::GetParameter

DESCRIPTION
  OMX Get Parameter method implementation

PARAMETERS
  <TBD>.

RETURN VALUE
  Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::get_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                           OMX_IN OMX_INDEXTYPE paramIndex,
                                           OMX_INOUT OMX_PTR     paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    DEBUG_PRINT_HIGH("get_parameter: \n");
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(paramData == NULL)
    {
        DEBUG_PRINT_ERROR("Get Param in Invalid paramData \n");
        return OMX_ErrorBadParameter;
    }
  //DEBUG_PRINT_HIGH("get_parameter 1 : \n");
  switch((unsigned long)paramIndex)
  {
    case OMX_IndexParamPortDefinition:
    {
      OMX_PARAM_PORTDEFINITIONTYPE *portDefn =
                            (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
      DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamPortDefinition\n");
      eRet = update_portdef(portDefn);
      if (eRet == OMX_ErrorNone)
          m_port_def = *portDefn;
      break;
    }
    case OMX_IndexParamVideoInit:
    {
      OMX_PORT_PARAM_TYPE *portParamType =
                              (OMX_PORT_PARAM_TYPE *) paramData;
      DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoInit\n");

      portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
      portParamType->nSize = sizeof(portParamType);
      portParamType->nPorts           = 2;
      portParamType->nStartPortNumber = 0;
      break;
    }
    case OMX_IndexParamVideoPortFormat:
    {
      OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
                     (OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
      DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoPortFormat\n");

      portFmt->nVersion.nVersion = OMX_SPEC_VERSION;
      portFmt->nSize             = sizeof(portFmt);

      if (OMX_CORE_INPUT_PORT_INDEX == portFmt->nPortIndex)
      {
        if (0 == portFmt->nIndex)
        {
              portFmt->eColorFormat =  (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;//OMX_COLOR_FormatYUV420Planar;//OMX_COLOR_FormatUnused;
              portFmt->eCompressionFormat = OMX_VIDEO_CodingUnused;
        }
        else
        {
          DEBUG_PRINT_ERROR("get_parameter: OMX_IndexParamVideoPortFormat:"\
              " NoMore compression formats\n");
          eRet =  OMX_ErrorNoMore;
        }
      }
      else if (OMX_CORE_OUTPUT_PORT_INDEX == portFmt->nPortIndex)
      {
        portFmt->eCompressionFormat =  OMX_VIDEO_CodingUnused;

        if(0 == portFmt->nIndex)
            portFmt->eColorFormat = (OMX_COLOR_FORMATTYPE)
                QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
        else
        {
           DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoPortFormat:"\
                  " NoMore Color formats\n");
           eRet =  OMX_ErrorNoMore;
        }
	    DEBUG_PRINT_ERROR("returning %d\n", portFmt->eColorFormat);
      }
      else
      {
        DEBUG_PRINT_ERROR("get_parameter: Bad port index %d\n",
                          (int)portFmt->nPortIndex);
        eRet = OMX_ErrorBadPortIndex;
      }
      break;
    }
    /*Component should support this port definition*/
    case OMX_IndexParamAudioInit:
    {
        OMX_PORT_PARAM_TYPE *audioPortParamType =
                                              (OMX_PORT_PARAM_TYPE *) paramData;
        DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamAudioInit\n");
        audioPortParamType->nVersion.nVersion = OMX_SPEC_VERSION;
        audioPortParamType->nSize = sizeof(audioPortParamType);
        audioPortParamType->nPorts           = 0;
        audioPortParamType->nStartPortNumber = 0;
        break;
    }
    /*Component should support this port definition*/
    case OMX_IndexParamImageInit:
    {
        OMX_PORT_PARAM_TYPE *imagePortParamType =
                                              (OMX_PORT_PARAM_TYPE *) paramData;
        DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamImageInit\n");
        imagePortParamType->nVersion.nVersion = OMX_SPEC_VERSION;
        imagePortParamType->nSize = sizeof(imagePortParamType);
        imagePortParamType->nPorts           = 0;
        imagePortParamType->nStartPortNumber = 0;
        break;

    }
    /*Component should support this port definition*/
    case OMX_IndexParamOtherInit:
    {
        DEBUG_PRINT_ERROR("get_parameter: OMX_IndexParamOtherInit %08x\n",
                          paramIndex);
        eRet =OMX_ErrorUnsupportedIndex;
        break;
    }
    case OMX_IndexParamStandardComponentRole:
    {
        OMX_PARAM_COMPONENTROLETYPE *comp_role;
        comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
        comp_role->nVersion.nVersion = OMX_SPEC_VERSION;
        comp_role->nSize = sizeof(*comp_role);

        DEBUG_PRINT_LOW("Getparameter: OMX_IndexParamStandardComponentRole %d\n",
                    paramIndex);
        strlcpy((char*)comp_role->cRole,(const char*)m_cRole,
                    OMX_MAX_STRINGNAME_SIZE);
        break;
    }
    /* Added for parameter test */
    case OMX_IndexParamPriorityMgmt:
        {

            OMX_PRIORITYMGMTTYPE *priorityMgmType =
                                             (OMX_PRIORITYMGMTTYPE *) paramData;
            DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamPriorityMgmt\n");
            priorityMgmType->nVersion.nVersion = OMX_SPEC_VERSION;
            priorityMgmType->nSize = sizeof(priorityMgmType);

            break;
        }
    /* Added for parameter test */
    case OMX_IndexParamCompBufferSupplier:
        {
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType =
                                     (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamCompBufferSupplier\n");

            bufferSupplierType->nSize = sizeof(bufferSupplierType);
            bufferSupplierType->nVersion.nVersion = OMX_SPEC_VERSION;
            if(OMX_CORE_INPUT_PORT_INDEX == bufferSupplierType->nPortIndex)
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            else if (OMX_CORE_OUTPUT_PORT_INDEX == bufferSupplierType->nPortIndex)
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            else
                eRet = OMX_ErrorBadPortIndex;


            break;
        }
#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
    case OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage:
        {
            DEBUG_PRINT_HIGH("get_parameter: OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage");
            GetAndroidNativeBufferUsageParams* nativeBuffersUsage = (GetAndroidNativeBufferUsageParams *) paramData;
            if((nativeBuffersUsage->nPortIndex == OMX_CORE_INPUT_PORT_INDEX) || (nativeBuffersUsage->nPortIndex == OMX_CORE_OUTPUT_PORT_INDEX))
            {
#ifdef USE_ION
#if defined (MAX_RES_720P)
                nativeBuffersUsage->nUsage = (GRALLOC_USAGE_PRIVATE_CAMERA_HEAP | GRALLOC_USAGE_PRIVATE_UNCACHED);
                DEBUG_PRINT_HIGH("ION:720P: nUsage 0x%x",nativeBuffersUsage->nUsage);
#else
                {
                    nativeBuffersUsage->nUsage = (GRALLOC_USAGE_PRIVATE_MM_HEAP |
                                                        GRALLOC_USAGE_PRIVATE_IOMMU_HEAP);
                    DEBUG_PRINT_HIGH("ION:non_secure_mode: nUsage 0x%lx",nativeBuffersUsage->nUsage);
                }
#endif //(MAX_RES_720P)
#else // USE_ION
#if defined (MAX_RES_720P) ||  defined (MAX_RES_1080P_EBI)
                nativeBuffersUsage->nUsage = (GRALLOC_USAGE_PRIVATE_ADSP_HEAP | GRALLOC_USAGE_PRIVATE_UNCACHED);
                DEBUG_PRINT_HIGH("720P/1080P_EBI: nUsage 0x%x",nativeBuffersUsage->nUsage);
#elif MAX_RES_1080P
                nativeBuffersUsage->nUsage = (GRALLOC_USAGE_PRIVATE_SMI_HEAP | GRALLOC_USAGE_PRIVATE_UNCACHED);
                DEBUG_PRINT_HIGH("1080P: nUsage 0x%x",nativeBuffersUsage->nUsage);
#endif
#endif // USE_ION
            } else {
                DEBUG_PRINT_ERROR(" get_parameter: OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage failed!");
                eRet = OMX_ErrorBadParameter;
            }
        }
        break;
#endif

    default:
    {
      DEBUG_PRINT_ERROR("get_parameter: unknown param %08x\n", paramIndex);
      eRet =OMX_ErrorUnsupportedIndex;
    }

  }

  DEBUG_PRINT_LOW(" get_parameter returning input WxH(%d x %d) SxSH(%d x %d)\n",
      drv_ctx.video_resolution_input.frame_width,
      drv_ctx.video_resolution_input.frame_height,
      drv_ctx.video_resolution_input.stride,
      drv_ctx.video_resolution_input.scan_lines);

  DEBUG_PRINT_LOW(" get_parameter returning output WxH(%d x %d) SxSH(%d x %d)\n",
      drv_ctx.video_resolution_output.frame_width,
      drv_ctx.video_resolution_output.frame_height,
      drv_ctx.video_resolution_output.stride,
      drv_ctx.video_resolution_output.scan_lines);

  return eRet;
}

#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
OMX_ERRORTYPE omx_vdpp::use_android_native_buffer(OMX_IN OMX_HANDLETYPE hComp, OMX_PTR data)
{
    DEBUG_PRINT_LOW("Inside use_android_native_buffer");
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    UseAndroidNativeBufferParams *params = (UseAndroidNativeBufferParams *)data;

    if((params == NULL) ||
      (params->nativeBuffer == NULL) ||
      (params->nativeBuffer->handle == NULL) ||
      !m_enable_android_native_buffers)
        return OMX_ErrorBadParameter;
    m_use_android_native_buffers = OMX_TRUE;
    sp<android_native_buffer_t> nBuf = params->nativeBuffer;
    private_handle_t *handle = (private_handle_t *)nBuf->handle;
    if(OMX_CORE_OUTPUT_PORT_INDEX == params->nPortIndex) {  //android native buffers can be used only on Output port
        OMX_U8 *buffer = NULL;

        buffer = (OMX_U8*)mmap(0, handle->size,
            PROT_READ|PROT_WRITE, MAP_SHARED, handle->fd, 0);
        if(buffer == MAP_FAILED) {
            DEBUG_PRINT_ERROR("Failed to mmap pmem with fd = %d, size = %d", handle->fd, handle->size);
            return OMX_ErrorInsufficientResources;
    }
        eRet = use_buffer(hComp,params->bufferHeader,params->nPortIndex,data,handle->size,buffer);
    } else {
        eRet = OMX_ErrorBadParameter;
    }
    return eRet;
}
#endif
/* ======================================================================
FUNCTION
  omx_vdpp::Setparameter

DESCRIPTION
  OMX Set Parameter method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::set_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                           OMX_IN OMX_INDEXTYPE paramIndex,
                                           OMX_IN OMX_PTR        paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    int ret=0;
    int i = 0;
    struct v4l2_format fmt;

    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Set Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(paramData == NULL)
    {
         DEBUG_PRINT_ERROR("Get Param in Invalid paramData \n");
         return OMX_ErrorBadParameter;
    }
    if((m_state != OMX_StateLoaded) &&
          BITMASK_ABSENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING) &&
          (m_out_bEnabled == OMX_TRUE) &&
          BITMASK_ABSENT(&m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING) &&
          (m_inp_bEnabled == OMX_TRUE)) {
        DEBUG_PRINT_ERROR("Set Param in Invalid State \n");
        return OMX_ErrorIncorrectStateOperation;
    }
  switch((unsigned long)paramIndex)
  {
    case OMX_IndexParamPortDefinition:
    {
      OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
      portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

      DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d\n",
             (int)portDefn->format.video.nFrameHeight,
             (int)portDefn->format.video.nFrameWidth);

      if(OMX_CORE_OUTPUT_PORT_INDEX == portDefn->nPortIndex)
      {
          DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition OP port\n");

          unsigned int buffer_size;
          memset(&fmt, 0, sizeof(fmt));

          // set output resolution based on port definition. scan_lines and stride settings need
          //  to match format setting requirement (QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m)
          {
             DEBUG_PRINT_LOW(" SetParam OP: WxH(%lu x %lu)\n",
                           portDefn->format.video.nFrameWidth,
                           portDefn->format.video.nFrameHeight);
             if (portDefn->format.video.nFrameHeight != 0x0 &&
                 portDefn->format.video.nFrameWidth != 0x0)
             {
                drv_ctx.video_resolution_output.frame_height = portDefn->format.video.nFrameHeight;
                drv_ctx.video_resolution_output.frame_width = portDefn->format.video.nFrameWidth;
                drv_ctx.video_resolution_output.scan_lines = paddedFrameWidth32(portDefn->format.video.nFrameHeight);
                drv_ctx.video_resolution_output.stride = paddedFrameWidth128(portDefn->format.video.nFrameWidth);

		        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		        fmt.fmt.pix_mp.height = drv_ctx.video_resolution_output.frame_height;
		        fmt.fmt.pix_mp.width = drv_ctx.video_resolution_output.frame_width;
		        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
		        fmt.fmt.pix_mp.pixelformat = capture_capability;
                DEBUG_PRINT_HIGH("VP output frame width = %d, height = %d, drv_ctx.video_resolution_output.stride = %d, drv_ctx.video_resolution_output.scan_lines = %d", fmt.fmt.pix_mp.width,
                        fmt.fmt.pix_mp.height, drv_ctx.video_resolution_output.stride, drv_ctx.video_resolution_output.scan_lines);
                // NV12 has 2 planes.
                /* Set format for each plane. */
                setFormatParams(capture_capability, drv_ctx.output_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
                for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
                {
                    fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[i] * fmt.fmt.pix_mp.height);
                    fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[0]); // both plane have the same plane stride
			    DEBUG_PRINT_HIGH("before VIDIOC_S_FMT (op) fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);

                }

#ifndef STUB_VPU
                ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
                for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
                {
			    DEBUG_PRINT_HIGH("after VIDIOC_S_FMT (op) fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
                }

                if (ret)
                {
                    DEBUG_PRINT_ERROR(" Set Resolution failed");
                    eRet = OMX_ErrorUnsupportedSetting;
                }
                else
#endif
                {
                    eRet = get_buffer_req(&drv_ctx.op_buf);

                   // eRet = get_buffer_req(&drv_ctx.ip_buf);
                }

              }
           }
           if ( portDefn->nBufferCountActual >= drv_ctx.op_buf.mincount /*||
                    portDefn->nBufferSize >=  drv_ctx.op_buf.buffer_size*/ )
           {
                drv_ctx.op_buf.actualcount = portDefn->nBufferCountActual;
                //drv_ctx.op_buf.buffer_size = portDefn->nBufferSize;
                eRet = set_buffer_req(&drv_ctx.op_buf);
                if (eRet == OMX_ErrorNone)
                    m_port_def = *portDefn;
           }
           else
           {
                DEBUG_PRINT_ERROR("ERROR: OP Requirements(#%d: %u) Requested(#%lu: %lu)\n",
                        drv_ctx.op_buf.mincount, drv_ctx.op_buf.buffer_size,
                        portDefn->nBufferCountActual, portDefn->nBufferSize);
                eRet = OMX_ErrorBadParameter;
           }
      }
      else if(OMX_CORE_INPUT_PORT_INDEX == portDefn->nPortIndex)
      {
        // TODO for 8092 the frame rate code below can be enabled to debug frame rate
#ifdef FRC_ENABLE
        if((portDefn->format.video.xFramerate >> 16) > 0 &&
           (portDefn->format.video.xFramerate >> 16) <= MAX_SUPPORTED_FPS)
        {
            // Frame rate only should be set if this is a "known value" or to
            // activate ts prediction logic (arbitrary mode only) sending input
            // timestamps with max value (LLONG_MAX).
            DEBUG_PRINT_HIGH("set_parameter: frame rate set by omx client : %lu",
                             portDefn->format.video.xFramerate >> 16);
            Q16ToFraction(portDefn->format.video.xFramerate, drv_ctx.frame_rate.fps_numerator,
                          drv_ctx.frame_rate.fps_denominator);
            if(!drv_ctx.frame_rate.fps_numerator)
            {
              DEBUG_PRINT_ERROR("Numerator is zero setting to 30");
              drv_ctx.frame_rate.fps_numerator = 30;
            }
            if(drv_ctx.frame_rate.fps_denominator)
              drv_ctx.frame_rate.fps_numerator = (int)
                  drv_ctx.frame_rate.fps_numerator / drv_ctx.frame_rate.fps_denominator;
              drv_ctx.frame_rate.fps_denominator = 1;
            frm_int = drv_ctx.frame_rate.fps_denominator * 1e6 /
                      drv_ctx.frame_rate.fps_numerator;
            DEBUG_PRINT_HIGH("set_parameter: frm_int(%lu) fps(%.2f)",
                             frm_int, drv_ctx.frame_rate.fps_numerator /
                             (float)drv_ctx.frame_rate.fps_denominator);

            struct v4l2_outputparm oparm;
            /*XXX: we're providing timing info as seconds per frame rather than frames
                * per second.*/
            oparm.timeperframe.numerator = drv_ctx.frame_rate.fps_denominator;
            oparm.timeperframe.denominator = drv_ctx.frame_rate.fps_numerator;

            struct v4l2_streamparm sparm;
            memset(&sparm, 0, sizeof(struct v4l2_streamparm));
            sparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            sparm.parm.output = oparm;
            if (ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_PARM, &sparm)) {
                DEBUG_PRINT_ERROR("Unable to convey fps info to driver, \
                        performance might be affected");
                eRet = OMX_ErrorHardware;
            }
        }
#endif
         memset(&fmt, 0, sizeof(fmt));
         DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition IP port\n");
         if(drv_ctx.video_resolution_input.frame_height !=
               portDefn->format.video.nFrameHeight ||
             drv_ctx.video_resolution_input.frame_width  !=
               portDefn->format.video.nFrameWidth)
         {
             DEBUG_PRINT_LOW(" SetParam IP: WxH(%lu x %lu)\n",
                           portDefn->format.video.nFrameWidth,
                           portDefn->format.video.nFrameHeight);
             if (portDefn->format.video.nFrameHeight != 0x0 &&
                 portDefn->format.video.nFrameWidth != 0x0)
             {
                 update_resolution(portDefn->format.video.nFrameWidth,
                    (portDefn->format.video.nFrameHeight),
					portDefn->format.video.nStride,
					(portDefn->format.video.nSliceHeight));

                // decoder stride information is not used in S_FMT and QBUF, since paddedWidth
                // will ensure the buffer length/size is always aligned with 128 bytes, which
                // has the same effect as stride.
                // output has Width 720, and nStride = 768
		        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		        fmt.fmt.pix_mp.height = drv_ctx.video_resolution_input.frame_height;
		        fmt.fmt.pix_mp.width = drv_ctx.video_resolution_input.frame_width;
                if (V4L2_FIELD_NONE == drv_ctx.interlace)
                {
		        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
                }
                else
                {
		        fmt.fmt.pix_mp.field = V4L2_FIELD_INTERLACED;
                }
		        fmt.fmt.pix_mp.pixelformat = output_capability;

                // NV12 has 2 planes.
                /* Set format for each plane. */
                setFormatParams(output_capability, drv_ctx.input_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
                for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
                {
                    fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.input_bytesperpixel[i] * fmt.fmt.pix_mp.height);
                    fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.input_bytesperpixel[0]); // both plane have the same plane stride
			    DEBUG_PRINT_HIGH("before VIDIOC_S_FMT (ip) fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
                }

#ifndef STUB_VPU
                ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
              //  for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
              //  {
			    //DEBUG_PRINT_HIGH("after VIDIOC_S_FMT (ip) fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
              //  }
                if (ret)
                {
                    DEBUG_PRINT_ERROR(" Set Resolution failed");
                    eRet = OMX_ErrorUnsupportedSetting;
                }
                else
#endif
                {
                    DEBUG_PRINT_HIGH("after VIDIOC_S_FMT (ip) drv_ctx.interlace = %d", drv_ctx.interlace);
                    // set output resolution the same as input
                    drv_ctx.video_resolution_output.frame_height = portDefn->format.video.nFrameHeight;
                    drv_ctx.video_resolution_output.frame_width = portDefn->format.video.nFrameWidth;
                    drv_ctx.video_resolution_output.scan_lines = paddedFrameWidth32(portDefn->format.video.nSliceHeight);
                    drv_ctx.video_resolution_output.stride = paddedFrameWidth128(portDefn->format.video.nStride);

		            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		            fmt.fmt.pix_mp.height = drv_ctx.video_resolution_output.frame_height;
		            fmt.fmt.pix_mp.width = drv_ctx.video_resolution_output.frame_width;
		            fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
		            fmt.fmt.pix_mp.pixelformat = capture_capability;
                    DEBUG_PRINT_HIGH("VP output frame width = %d, height = %d, drv_ctx.video_resolution_output.stride = %d, drv_ctx.video_resolution_output.scan_lines = %d", fmt.fmt.pix_mp.width,
                            fmt.fmt.pix_mp.height, drv_ctx.video_resolution_output.stride, drv_ctx.video_resolution_output.scan_lines);
                    // NV12 has 2 planes.
                    /* Set format for each plane. */
                    setFormatParams(capture_capability, drv_ctx.output_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
                    for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
                    {
                        fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[i] * fmt.fmt.pix_mp.height);
                        fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[0]); // both plane have the same plane stride
				    DEBUG_PRINT_HIGH("before VIDIOC_S_FMT op fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
                    }

    #ifndef STUB_VPU
                    ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
                    for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
                    {
				    DEBUG_PRINT_HIGH("after VIDIOC_S_FMT op fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n",i,fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
                    }

                    if (ret)
                    {
                        DEBUG_PRINT_ERROR(" Set Resolution failed");
                        eRet = OMX_ErrorUnsupportedSetting;
                    }
                    else
    #endif
                    {
                        // get buffer req for input, output buffer size is
                        // determined by output pixel format and output resolution
                        //eRet = get_buffer_req(&drv_ctx.op_buf);
                        eRet = get_buffer_req(&drv_ctx.ip_buf);
                    }
                }
             }
         }

         if (portDefn->nBufferCountActual >= drv_ctx.ip_buf.mincount
             /*|| portDefn->nBufferSize >= drv_ctx.ip_buf.buffer_size*/)
             // only allocate larger size
         {
             DEBUG_PRINT_HIGH("portDefn->nBufferCountActual = %lu portDefn->nBufferSize = %lu, drv_ctx.ip_buf.buffer_size=%d \n", portDefn->nBufferCountActual, portDefn->nBufferSize, drv_ctx.ip_buf.buffer_size);
             vdpp_allocatorproperty *buffer_prop = &drv_ctx.ip_buf;
             drv_ctx.ip_buf.actualcount = portDefn->nBufferCountActual;
             //if(portDefn->nBufferSize >= drv_ctx.ip_buf.buffer_size)
             //{
             //    drv_ctx.ip_buf.buffer_size = (portDefn->nBufferSize + buffer_prop->alignment - 1) &
             //             (~(buffer_prop->alignment - 1));
             //    DEBUG_PRINT_HIGH("drv_ctx.ip_buf.buffer_size = %d, buffer_prop->alignment = %d\n", drv_ctx.ip_buf.buffer_size, buffer_prop->alignment);
             //}
             eRet = set_buffer_req(buffer_prop);
         }
         else
         {
             DEBUG_PRINT_ERROR("ERROR: IP Requirements(#%d: %u) Requested(#%lu: %lu)\n",
               drv_ctx.ip_buf.mincount, drv_ctx.ip_buf.buffer_size,
               portDefn->nBufferCountActual, portDefn->nBufferSize);
             eRet = OMX_ErrorBadParameter;
         }
      }
      else if (portDefn->eDir ==  OMX_DirMax)
      {
          DEBUG_PRINT_ERROR(" Set_parameter: Bad Port idx %d",
                      (int)portDefn->nPortIndex);
          eRet = OMX_ErrorBadPortIndex;
      }
    }
    break;
    case OMX_IndexParamVideoPortFormat:
    {
      OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
                     (OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
      int ret=0;
      struct v4l2_format fmt;
      DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d\n",
              portFmt->eColorFormat);

      if(OMX_CORE_OUTPUT_PORT_INDEX == portFmt->nPortIndex)
      {
            uint32_t op_format;

            memset(&fmt, 0, sizeof(fmt));
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            fmt.fmt.pix_mp.height = drv_ctx.video_resolution_output.frame_height;
            fmt.fmt.pix_mp.width = drv_ctx.video_resolution_output.frame_width;
            fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

          // TODO based on output format
          // update OMX format type for additional output format supported by 8084
          if((portFmt->eColorFormat == (OMX_COLOR_FORMATTYPE)
                      QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m) ||
              (portFmt->eColorFormat == OMX_COLOR_FormatYUV420Planar))
              op_format = (uint32_t)V4L2_PIX_FMT_NV12;
          else if(portFmt->eColorFormat ==
                  (OMX_COLOR_FORMATTYPE)
                  QOMX_COLOR_FormatYVU420SemiPlanar)
              op_format = V4L2_PIX_FMT_NV21;
          else
              eRet = OMX_ErrorBadParameter;

          if(eRet == OMX_ErrorNone)
          {
              drv_ctx.output_format = op_format;
              capture_capability = op_format;
              fmt.fmt.pix_mp.pixelformat = capture_capability;

              DEBUG_PRINT_HIGH("VP output frame width = %d, height = %d", fmt.fmt.pix_mp.width,
                    fmt.fmt.pix_mp.height);

              setFormatParams(capture_capability, drv_ctx.output_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
              for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
              {
                  fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width *
                                                                           drv_ctx.output_bytesperpixel[i]  *
                                                                           fmt.fmt.pix_mp.height);
                  fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[0]);
              }
#ifndef STUB_VPU
              ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
              if(ret)
              {
                  DEBUG_PRINT_ERROR(" Set output format failed");
                  eRet = OMX_ErrorUnsupportedSetting;
              }
              else
#endif
              {
                  eRet = get_buffer_req(&drv_ctx.op_buf);
              }
          }
      }
    }
    break;

     case OMX_IndexParamStandardComponentRole:
     {
          OMX_PARAM_COMPONENTROLETYPE *comp_role;
          comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
          DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamStandardComponentRole %s\n",
                       comp_role->cRole);

          if((m_state == OMX_StateLoaded)&&
              !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
          {
           DEBUG_PRINT_LOW("Set Parameter called in valid state");
          }
          else
          {
             DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
             return OMX_ErrorIncorrectStateOperation;
          }

          // no component role yet
       /*   if(!strncmp(drv_ctx.kind, "OMX.qcom.video.vidpp",OMX_MAX_STRINGNAME_SIZE))
          {
              if(!strncmp((char*)comp_role->cRole,"video.vidpp",OMX_MAX_STRINGNAME_SIZE))
              {
                  strlcpy((char*)m_cRole,"video.vidpp",OMX_MAX_STRINGNAME_SIZE);
              }
              else
              {
                  DEBUG_PRINT_ERROR("Setparameter: unknown Index %s\n", comp_role->cRole);
                  eRet =OMX_ErrorUnsupportedSetting;
              }
          }
          else
          {
               DEBUG_PRINT_ERROR("Setparameter: unknown param %s\n", drv_ctx.kind);
               eRet = OMX_ErrorInvalidComponentName;
          } */
          break;
     }

    case OMX_IndexParamPriorityMgmt:
        {
            if(m_state != OMX_StateLoaded)
            {
               DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
               return OMX_ErrorIncorrectStateOperation;
            }
            OMX_PRIORITYMGMTTYPE *priorityMgmtype = (OMX_PRIORITYMGMTTYPE*) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt %lu\n",
              priorityMgmtype->nGroupID);

            DEBUG_PRINT_LOW("set_parameter: priorityMgmtype %lu\n",
             priorityMgmtype->nGroupPriority);

            m_priority_mgm.nGroupID = priorityMgmtype->nGroupID;
            m_priority_mgm.nGroupPriority = priorityMgmtype->nGroupPriority;

            break;
        }

      case OMX_IndexParamCompBufferSupplier:
      {
          OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier %d\n",
                bufferSupplierType->eBufferSupplier);
             if(bufferSupplierType->nPortIndex == 0 || bufferSupplierType->nPortIndex ==1)
                m_buffer_supplier.eBufferSupplier = bufferSupplierType->eBufferSupplier;

             else

             eRet = OMX_ErrorBadPortIndex;

          break;

      }
#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
      /* Need to allow following two set_parameters even in Idle
       * state. This is ANDROID architecture which is not in sync
       * with openmax standard. */
    case OMX_GoogleAndroidIndexEnableAndroidNativeBuffers:
      {
          EnableAndroidNativeBuffersParams* enableNativeBuffers = (EnableAndroidNativeBuffersParams *) paramData;
          if(enableNativeBuffers) {
              m_enable_android_native_buffers = enableNativeBuffers->enable;
          }
          DEBUG_PRINT_HIGH("OMX_GoogleAndroidIndexEnableAndroidNativeBuffers: enableNativeBuffers %d\n", m_enable_android_native_buffers);
      }
      break;
    case OMX_GoogleAndroidIndexUseAndroidNativeBuffer:
      {
          eRet = use_android_native_buffer(hComp, paramData);
      }
      break;
#endif
    case OMX_QcomIndexParamInterlaceExtraData:
          eRet = enable_extradata(OMX_INTERLACE_EXTRADATA,
                              ((QOMX_ENABLETYPE *)paramData)->bEnable);

      break;
    case OMX_IndexParamInterlaceFormat:
        {
            OMX_INTERLACEFORMATTYPE *interlaceFormat = ( OMX_INTERLACEFORMATTYPE *)paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamInterlaceFormat %ld\n",
                interlaceFormat->nFormat);

            if(OMX_InterlaceInterleaveFrameBottomFieldFirst == (OMX_U32)interlaceFormat->nFormat)
            {
                drv_ctx.interlace = V4L2_FIELD_INTERLACED_BT;
                DEBUG_PRINT_LOW("set_parameter: V4L2_FIELD_INTERLACED_BT");
                interlace_user_flag = true;
            }
            else if(OMX_InterlaceInterleaveFrameTopFieldFirst == (OMX_U32)interlaceFormat->nFormat)
            {
                drv_ctx.interlace = V4L2_FIELD_INTERLACED_TB;
                DEBUG_PRINT_LOW("set_parameter: V4L2_FIELD_INTERLACED_TB");
                interlace_user_flag = true;
            }
            else if(OMX_InterlaceFrameProgressive == (OMX_U32)interlaceFormat->nFormat)
            {
                drv_ctx.interlace = V4L2_FIELD_NONE;
                DEBUG_PRINT_LOW("set_parameter: V4L2_FIELD_NONE");
                interlace_user_flag = true;
            }
            else
            {
                DEBUG_PRINT_ERROR("Setparameter: unknown param %lu\n", interlaceFormat->nFormat);
                eRet = OMX_ErrorBadParameter;
            }
        }
      break;
    default:
    {
      DEBUG_PRINT_ERROR("Setparameter: unknown param %d\n", paramIndex);
      eRet = OMX_ErrorUnsupportedIndex;
    }
  }
  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::GetConfig

DESCRIPTION
  OMX Get Config Method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::get_config(OMX_IN OMX_HANDLETYPE      hComp,
                                        OMX_IN OMX_INDEXTYPE configIndex,
                                        OMX_INOUT OMX_PTR     configData)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;

  if (m_state == OMX_StateInvalid)
  {
     DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
     return OMX_ErrorInvalidState;
  }

  switch ((unsigned long)configIndex)
  {
	case OMX_IndexConfigCommonOutputCrop:
    {
      OMX_CONFIG_RECTTYPE *rect = (OMX_CONFIG_RECTTYPE *) configData;
      memcpy(rect, &rectangle, sizeof(OMX_CONFIG_RECTTYPE));
      break;
    }

    // OMX extensions
      case OMX_QcomIndexConfigActiveRegionDetectionStatus:
          break;

    default:
    {
      DEBUG_PRINT_ERROR("get_config: unknown param %d\n",configIndex);
      eRet = OMX_ErrorBadParameter;
    }

  }

  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::SetConfig

DESCRIPTION
  OMX Set Config method implementation

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdpp::set_config(OMX_IN OMX_HANDLETYPE      hComp,
                                        OMX_IN OMX_INDEXTYPE configIndex,
                                        OMX_IN OMX_PTR        configData)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct vpu_control control;
  int result = 0;

  DEBUG_PRINT_LOW("omx_vdpp::set_config \n");
  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  switch ((unsigned long)configIndex)
  {
      // OMX extensions
      case OMX_QcomIndexConfigActiveRegionDetection:
      {
          struct vpu_ctrl_active_region_param *ard = &control.data.active_region_param;
          memset(&control, 0, sizeof(control));
          control.control_id = VPU_CTRL_ACTIVE_REGION_PARAM;
          mExtensionData.activeRegionDetectionDirtyFlag = true;
          memcpy(&(mExtensionData.activeRegionDetection),
                 configData,
                 sizeof(mExtensionData.activeRegionDetection));

          /* Set control. */
	  ard->enable = mExtensionData.activeRegionDetection.bEnable;
	  ard->num_exclusions = mExtensionData.activeRegionDetection.nNumExclusionRegions;
          memcpy(&(ard->detection_region), &(mExtensionData.activeRegionDetection.sROI), sizeof(QOMX_RECTTYPE));
          if(ard->num_exclusions > 0)
          {
            memcpy(&(ard->detection_region), &(mExtensionData.activeRegionDetection.sExclusionRegions), (ard->num_exclusions * sizeof(QOMX_RECTTYPE)));
          }

	  DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_ACTIVE_REGION_MEASURE : "
				        "top %d left %d width %d height %d",
				        ard->detection_region.top, ard->detection_region.left,
				        ard->detection_region.width, ard->detection_region.height);
#ifndef STUB_VPU
          result = ioctl(drv_ctx.video_vpu_fd, VPU_S_CONTROL, &control);
          if (result < 0)
          {
              DEBUG_PRINT_ERROR("VIDIOC_S_CTRL VPU_S_CTRL_ACTIVE_REGION_MEASURE failed, result = %d", result);
              eRet = OMX_ErrorUnsupportedSetting;
          }
          else
          {
              mExtensionData.activeRegionDetectionDirtyFlag = false;
		  DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_ACTIVE_REGION_MEASURE set to: "
				            "top %d left %d width %d height %d",
				            ard->detection_region.top, ard->detection_region.left,
				            ard->detection_region.width, ard->detection_region.height);
          }
#endif
          break;
      }

      case OMX_QcomIndexConfigScalingMode:
        {
          struct vpu_ctrl_standard *anmph = &control.data.standard;
          memset(&control, 0, sizeof(control));
          control.control_id = VPU_CTRL_ANAMORPHIC_SCALING;
          mExtensionData.scalingModeDirtyFlag = true;
          memcpy(&(mExtensionData.scalingMode),
                 configData,
                 sizeof(mExtensionData.scalingMode));

          /* Set control. */
	  anmph->enable = 1;
	  anmph->value = mExtensionData.scalingMode.eScaleMode;

          DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_ANAMORPHIC_SCALING %d, anmph->enable = %d", anmph->value, anmph->enable);
#ifndef STUB_VPU
          result = ioctl(drv_ctx.video_vpu_fd, VPU_S_CONTROL, &control);
          if (result < 0)
          {
              DEBUG_PRINT_ERROR("VIDIOC_S_CTRL VPU_S_CTRL_ANAMORPHIC_SCALING failed, result = %d", result);
              eRet = OMX_ErrorUnsupportedSetting;
          }
          else
          {
              mExtensionData.scalingModeDirtyFlag = false;
              DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_ANAMORPHIC_SCALING set to %d", anmph->value);
          }
#endif
          break;
        }

      case OMX_QcomIndexConfigNoiseReduction:
        {
          struct vpu_ctrl_auto_manual *nr = &control.data.auto_manual;
          memset(&control, 0, sizeof(control));
          control.control_id = VPU_CTRL_NOISE_REDUCTION;
          mExtensionData.noiseReductionDirtyFlag = true;
          memcpy(&(mExtensionData.noiseReduction),
                 configData,
                 sizeof(mExtensionData.noiseReduction));

          /* Set control. */
	  nr->enable = mExtensionData.noiseReduction.bEnable;
	  nr->auto_mode = mExtensionData.noiseReduction.bAutoMode;
	  nr->value = mExtensionData.noiseReduction.nNoiseReduction;

          DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_NOISE_REDUCTION %d, nr->enable = %d, nr->auto_mode = %d", nr->value, nr->enable, nr->auto_mode);
#ifndef STUB_VPU
          result = ioctl(drv_ctx.video_vpu_fd, VPU_S_CONTROL, &control);
          if (result < 0)
          {
              DEBUG_PRINT_ERROR("VIDIOC_S_CTRL VPU_S_CTRL_NOISE_REDUCTION failed, result = %d", result);
              eRet = OMX_ErrorUnsupportedSetting;
          }
          else
          {
              mExtensionData.noiseReductionDirtyFlag = false;
              DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_NOISE_REDUCTION set to %d", nr->value);
          }
#endif
          break;
        }

      case OMX_QcomIndexConfigImageEnhancement:
        {
          struct vpu_ctrl_auto_manual *ie = &control.data.auto_manual;
          memset(&control, 0, sizeof(control));
          control.control_id = VPU_CTRL_IMAGE_ENHANCEMENT;
          mExtensionData.imageEnhancementDirtyFlag = true;
          memcpy(&(mExtensionData.imageEnhancement),
                 configData,
                 sizeof(mExtensionData.imageEnhancement));

          /* Set control. */
	  ie->enable = mExtensionData.imageEnhancement.bEnable;
	  ie->auto_mode = mExtensionData.imageEnhancement.bAutoMode;
	  ie->value = mExtensionData.imageEnhancement.nImageEnhancement;

          DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_IMAGE_ENHANCEMENT %d, ie->enable = %d, ie->auto_mode = %d", ie->value, ie->enable, ie->auto_mode);
#ifndef STUB_VPU
          result = ioctl(drv_ctx.video_vpu_fd, VPU_S_CONTROL, &control);
          if (result < 0)
          {
              DEBUG_PRINT_ERROR("VIDIOC_S_CTRL VPU_S_CTRL_IMAGE_ENHANCEMENT failed, result = %d", result);
              eRet = OMX_ErrorUnsupportedSetting;
          }
          else
          {
              mExtensionData.imageEnhancementDirtyFlag = false;
              DEBUG_PRINT_HIGH("VIDIOC_S_CTRL: VPU_S_CTRL_IMAGE_ENHANCEMENT set to %d", ie->value);
          }
#endif
          break;
        }
#ifdef FRC_ENABLE
      case OMX_IndexVendorVideoFrameRate:
       {

            OMX_VENDOR_VIDEOFRAMERATE *config = (OMX_VENDOR_VIDEOFRAMERATE *) configData;
            DEBUG_PRINT_HIGH("OMX_IndexVendorVideoFrameRate %d", config->nFps);

            if (config->nPortIndex == OMX_CORE_INPUT_PORT_INDEX) {
                if (config->bEnabled) {
                    if ((config->nFps >> 16) > 0) {
                        DEBUG_PRINT_HIGH("set_config: frame rate set by omx client : %d",
                                config->nFps >> 16);
                        Q16ToFraction(config->nFps, drv_ctx.frame_rate.fps_numerator,
                                drv_ctx.frame_rate.fps_denominator);

                        if (!drv_ctx.frame_rate.fps_numerator) {
                            DEBUG_PRINT_ERROR("Numerator is zero setting to 30");
                            drv_ctx.frame_rate.fps_numerator = 30;
                        }

                        if (drv_ctx.frame_rate.fps_denominator) {
                            drv_ctx.frame_rate.fps_numerator = (int)
                                drv_ctx.frame_rate.fps_numerator / drv_ctx.frame_rate.fps_denominator;
                        }

                        drv_ctx.frame_rate.fps_denominator = 1;
                        frm_int = drv_ctx.frame_rate.fps_denominator * 1e6 /
                            drv_ctx.frame_rate.fps_numerator;

                        struct v4l2_outputparm oparm;
                        /*XXX: we're providing timing info as seconds per frame rather than frames
                         * per second.*/
                        oparm.timeperframe.numerator = drv_ctx.frame_rate.fps_denominator;
                        oparm.timeperframe.denominator = drv_ctx.frame_rate.fps_numerator;

                        struct v4l2_streamparm sparm;
                        memset(&sparm, 0, sizeof(struct v4l2_streamparm));
                        sparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                        sparm.parm.output = oparm;
#ifndef STUB_VPU
                        if (ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_PARM, &sparm)) {
                            DEBUG_PRINT_ERROR("Unable to convey fps info to driver, \
                                    performance might be affected");
                            eRet = OMX_ErrorHardware;
                        }
#endif
                        client_set_fps = true;
                    } else {
                        DEBUG_PRINT_ERROR("Frame rate not supported.");
                        eRet = OMX_ErrorUnsupportedSetting;
                    }
                } else {
                    DEBUG_PRINT_HIGH("set_config: Disabled client's frame rate");
                    client_set_fps = false;
                }
            } else { // 8084 doesn't support FRC (only 8092 does). only input framerate setting is supported.
                DEBUG_PRINT_ERROR(" Set_config: Bad Port idx %d",
                        (int)config->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }

        }
       break;
#endif
      case OMX_IndexConfigCallbackRequest:
       {
            OMX_CONFIG_CALLBACKREQUESTTYPE *callbackRequest = (OMX_CONFIG_CALLBACKREQUESTTYPE *) configData;
            DEBUG_PRINT_HIGH("OMX_IndexConfigCallbackRequest %d", callbackRequest->bEnable);

            if (callbackRequest->nPortIndex == OMX_CORE_INPUT_PORT_INDEX) {
                if ((callbackRequest->bEnable) && (OMX_QcomIndexConfigActiveRegionDetectionStatus == (OMX_QCOM_EXTN_INDEXTYPE)callbackRequest->nIndex))
                {
                    m_ar_callback_setup  = true;
                }
            }
       }
       break;
      case OMX_IndexConfigCommonOutputCrop:
          {
              OMX_CONFIG_RECTTYPE *rect = (OMX_CONFIG_RECTTYPE *) configData;
              memcpy(&rectangle, rect, sizeof(OMX_CONFIG_RECTTYPE));
              break;
          }
      default:
          {
              DEBUG_PRINT_ERROR("set_config: unknown param 0x%08x\n",configIndex);
              eRet = OMX_ErrorBadParameter;
          }
  }

  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::GetExtensionIndex

DESCRIPTION
  OMX GetExtensionIndex method implementaion.  <TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::get_extension_index(OMX_IN OMX_HANDLETYPE      hComp,
                                                OMX_IN OMX_STRING      paramName,
                                                OMX_OUT OMX_INDEXTYPE* indexType)
{
    DEBUG_PRINT_LOW("omx_vdpp::get_extension_index %s\n", paramName);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Extension Index in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
    else if(!strncmp(paramName,"OMX.google.android.index.enableAndroidNativeBuffers", sizeof("OMX.google.android.index.enableAndroidNativeBuffers") - 1)) {
        *indexType = (OMX_INDEXTYPE)OMX_GoogleAndroidIndexEnableAndroidNativeBuffers;
        DEBUG_PRINT_HIGH("OMX.google.android.index.enableAndroidNativeBuffers");
    }
    else if(!strncmp(paramName,"OMX.google.android.index.useAndroidNativeBuffer2", sizeof("OMX.google.android.index.enableAndroidNativeBuffer2") - 1)) {
        *indexType = (OMX_INDEXTYPE)OMX_GoogleAndroidIndexUseAndroidNativeBuffer2;
        DEBUG_PRINT_HIGH("OMX.google.android.index.useAndroidNativeBuffer2");
    }
    else if(!strncmp(paramName,"OMX.google.android.index.useAndroidNativeBuffer", sizeof("OMX.google.android.index.enableAndroidNativeBuffer") - 1)) {
        DEBUG_PRINT_ERROR("Extension: %s is supported\n", paramName);
        *indexType = (OMX_INDEXTYPE)OMX_GoogleAndroidIndexUseAndroidNativeBuffer;
        DEBUG_PRINT_HIGH("OMX.google.android.index.useAndroidNativeBuffer");
    }
    else if(!strncmp(paramName,"OMX.google.android.index.getAndroidNativeBufferUsage", sizeof("OMX.google.android.index.getAndroidNativeBufferUsage") - 1)) {
        *indexType = (OMX_INDEXTYPE)OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage;
        DEBUG_PRINT_HIGH("OMX.google.android.index.getAndroidNativeBufferUsage");
    }
#endif

    /* VIDPP extension
     */
    else if(!strncmp(paramName,
                     OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION_STATUS,
                     sizeof(OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION_STATUS) - 1))
    {
        DEBUG_PRINT_LOW("get_extension_index OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION_STATUS 0x%x \n", OMX_QcomIndexConfigActiveRegionDetectionStatus);
        *indexType = (OMX_INDEXTYPE)OMX_QcomIndexConfigActiveRegionDetectionStatus;
    }
    else if(!strncmp(paramName,
                     OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION,
                     sizeof(OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION) - 1))
    {
        DEBUG_PRINT_LOW("get_extension_index OMX_QCOM_INDEX_CONFIG_ACTIVE_REGION_DETECTION 0x%x \n", OMX_QcomIndexConfigActiveRegionDetection);
        *indexType = (OMX_INDEXTYPE)OMX_QcomIndexConfigActiveRegionDetection;
    }
    else if(!strncmp(paramName,
                     OMX_QCOM_INDEX_CONFIG_SCALING_MODE,
                     sizeof(OMX_QCOM_INDEX_CONFIG_SCALING_MODE) - 1))
    {
        DEBUG_PRINT_LOW("get_extension_index OMX_QCOM_INDEX_CONFIG_SCALING_MODE 0x%x \n", OMX_QcomIndexConfigScalingMode);
        *indexType = (OMX_INDEXTYPE)OMX_QcomIndexConfigScalingMode;
    }
    else if(!strncmp(paramName,
                     OMX_QCOM_INDEX_CONFIG_NOISEREDUCTION,
                     sizeof(OMX_QCOM_INDEX_CONFIG_NOISEREDUCTION) - 1))
    {
        DEBUG_PRINT_LOW("get_extension_index OMX_QCOM_INDEX_CONFIG_NOISEREDUCTION 0x%x \n", OMX_QcomIndexConfigNoiseReduction);
        *indexType = (OMX_INDEXTYPE)OMX_QcomIndexConfigNoiseReduction;
    }
    else if(!strncmp(paramName,
                     OMX_QCOM_INDEX_CONFIG_IMAGEENHANCEMENT,
                     sizeof(OMX_QCOM_INDEX_CONFIG_IMAGEENHANCEMENT) - 1))
    {
        DEBUG_PRINT_LOW("get_extension_index OMX_QCOM_INDEX_CONFIG_IMAGEENHANCEMENT 0x%x \n", OMX_QcomIndexConfigImageEnhancement);
        *indexType = (OMX_INDEXTYPE)OMX_QcomIndexConfigImageEnhancement;
    }

	else {
        DEBUG_PRINT_ERROR("Extension: %s not implemented\n", paramName);
        return OMX_ErrorNotImplemented;
    }
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdpp::GetState

DESCRIPTION
  Returns the state information back to the caller.<TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdpp::get_state(OMX_IN OMX_HANDLETYPE  hComp,
                                       OMX_OUT OMX_STATETYPE* state)
{
  *state = m_state;
  DEBUG_PRINT_LOW("get_state: Returning the state %d\n",*state);
  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdpp::ComponentTunnelRequest

DESCRIPTION
  OMX Component Tunnel Request method implementation. <TBD>

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::component_tunnel_request(OMX_IN OMX_HANDLETYPE                hComp,
                                                     OMX_IN OMX_U32                        port,
                                                     OMX_IN OMX_HANDLETYPE        peerComponent,
                                                     OMX_IN OMX_U32                    peerPort,
                                                     OMX_INOUT OMX_TUNNELSETUPTYPE* tunnelSetup)
{
  DEBUG_PRINT_ERROR("Error: component_tunnel_request Not Implemented\n");
  return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE  omx_vdpp::use_output_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE       *bufHdr= NULL; // buffer header
  unsigned                         i= 0; // Temporary counter
  struct vdpp_setbuffer_cmd setbuffers;
  OMX_PTR privateAppData = NULL;
  private_handle_t *handle = NULL;
  OMX_U8 *buff = buffer;
  struct v4l2_buffer buf;
  struct v4l2_plane plane[VIDEO_MAX_PLANES];
  int extra_idx = 0;

  DEBUG_PRINT_HIGH("Inside omx_vdpp::use_output_buffer buffer = %p, bytes= %lu", buffer, bytes);

  if (!m_out_mem_ptr) {
    DEBUG_PRINT_HIGH("Use_op_buf:Allocating output headers buffer = %p, bytes= %lu", buffer, bytes);
    eRet = allocate_output_headers();
  }

  if (eRet == OMX_ErrorNone) {
    for(i=0; i< drv_ctx.op_buf.actualcount; i++) {
      if(BITMASK_ABSENT(&m_out_bm_count,i))
      {
        break;
      }
    }
  }

  if(i >= drv_ctx.op_buf.actualcount) {
    DEBUG_PRINT_ERROR("Already using %d o/p buffers\n", drv_ctx.op_buf.actualcount);
    eRet = OMX_ErrorInsufficientResources;
  }

  if (eRet == OMX_ErrorNone) {
#if defined(_ANDROID_HONEYCOMB_) || defined(_ANDROID_ICS_)
    if(m_enable_android_native_buffers) {
        DEBUG_PRINT_HIGH("Use_op_buf:m_enable_android_native_buffers 1\n");
        if (m_use_android_native_buffers) {
            DEBUG_PRINT_HIGH("Use_op_buf:m_enable_android_native_buffers 2\n");
            UseAndroidNativeBufferParams *params = (UseAndroidNativeBufferParams *)appData;
            sp<android_native_buffer_t> nBuf = params->nativeBuffer;
            handle = (private_handle_t *)nBuf->handle;
            privateAppData = params->pAppPrivate;
        } else {
            DEBUG_PRINT_HIGH("Use_op_buf:m_enable_android_native_buffers 3\n");
            handle = (private_handle_t *)buff;
            privateAppData = appData;
        }

        if(!handle) {
            DEBUG_PRINT_ERROR("Native Buffer handle is NULL");
            return OMX_ErrorBadParameter;
        }

        if ((OMX_U32)handle->size < drv_ctx.op_buf.buffer_size) {
            DEBUG_PRINT_ERROR("Insufficient sized buffer given for playback,"
                              " expected %u, got %lu",
                              drv_ctx.op_buf.buffer_size, (OMX_U32)handle->size);
            return OMX_ErrorBadParameter;
        }

#if defined(_ANDROID_ICS_)
        native_buffer[i].nativehandle = handle;
        native_buffer[i].privatehandle = handle;
#endif
        drv_ctx.ptr_outputbuffer[i].pmem_fd = handle->fd;
        drv_ctx.ptr_outputbuffer[i].offset = 0;
        drv_ctx.ptr_outputbuffer[i].bufferaddr = NULL;
        drv_ctx.ptr_outputbuffer[i].buffer_len = drv_ctx.op_buf.buffer_size;
        drv_ctx.ptr_outputbuffer[i].mmaped_size = handle->size;
        DEBUG_PRINT_HIGH("Use_op_buf:m_enable_android_native_buffers 5 drv_ctx.ptr_outputbuffer[i].bufferaddr = %p, size1=%d, size2=%d\n",
                drv_ctx.ptr_outputbuffer[i].bufferaddr,
                drv_ctx.op_buf.buffer_size,
                handle->size);
    } else
#endif

    if (!ouput_egl_buffers && !m_use_output_pmem) {
#ifdef USE_ION
        drv_ctx.op_buf_ion_info[i].ion_device_fd = alloc_map_ion_memory(
                drv_ctx.op_buf.buffer_size,drv_ctx.op_buf.alignment,
                &drv_ctx.op_buf_ion_info[i].ion_alloc_data,
                &drv_ctx.op_buf_ion_info[i].fd_ion_data, 0);
        if(drv_ctx.op_buf_ion_info[i].ion_device_fd < 0) {
          DEBUG_PRINT_ERROR("ION device fd is bad %d\n", drv_ctx.op_buf_ion_info[i].ion_device_fd);
          return OMX_ErrorInsufficientResources;
        }
        drv_ctx.ptr_outputbuffer[i].pmem_fd = \
          drv_ctx.op_buf_ion_info[i].fd_ion_data.fd;
#else
        drv_ctx.ptr_outputbuffer[i].pmem_fd = \
          open (MEM_DEVICE,O_RDWR);

        if (drv_ctx.ptr_outputbuffer[i].pmem_fd < 0) {
          DEBUG_PRINT_ERROR("ION/pmem buffer fd is bad %d\n", drv_ctx.ptr_outputbuffer[i].pmem_fd);
          return OMX_ErrorInsufficientResources;
        }

        if(!align_pmem_buffers(drv_ctx.ptr_outputbuffer[i].pmem_fd,
          drv_ctx.op_buf.buffer_size,
          drv_ctx.op_buf.alignment))
        {
          DEBUG_PRINT_ERROR(" align_pmem_buffers() failed");
          close(drv_ctx.ptr_outputbuffer[i].pmem_fd);
          return OMX_ErrorInsufficientResources;
        }
#endif
        {
            drv_ctx.ptr_outputbuffer[i].bufferaddr =
              (unsigned char *)mmap(NULL, drv_ctx.op_buf.buffer_size,
              PROT_READ|PROT_WRITE, MAP_SHARED,
              drv_ctx.ptr_outputbuffer[i].pmem_fd,0);
            if (drv_ctx.ptr_outputbuffer[i].bufferaddr == MAP_FAILED) {
                close(drv_ctx.ptr_outputbuffer[i].pmem_fd);
#ifdef USE_ION
                free_ion_memory(&drv_ctx.op_buf_ion_info[i]);
#endif
              DEBUG_PRINT_ERROR("Unable to mmap output buffer\n");
              return OMX_ErrorInsufficientResources;
            }
        }
        drv_ctx.ptr_outputbuffer[i].offset = 0;
        privateAppData = appData;
     }
     else {

       DEBUG_PRINT_LOW("Use_op_buf: out_pmem=%d",m_use_output_pmem);
        if (!appData || !bytes ) {
          if(!buffer) {
              DEBUG_PRINT_ERROR(" Bad parameters for use buffer in EGL image case");
              return OMX_ErrorBadParameter;
          }
        }

        OMX_QCOM_PLATFORM_PRIVATE_LIST *pmem_list;
        OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pmem_info;
        pmem_list = (OMX_QCOM_PLATFORM_PRIVATE_LIST*) appData;
        if (!pmem_list || !pmem_list->entryList || !pmem_list->entryList->entry ||
            !pmem_list->nEntries ||
            pmem_list->entryList->type != OMX_QCOM_PLATFORM_PRIVATE_PMEM) {
          DEBUG_PRINT_ERROR(" Pmem info not valid in use buffer");
          return OMX_ErrorBadParameter;
        }
        pmem_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                    pmem_list->entryList->entry;
        DEBUG_PRINT_LOW("vdec: use buf: pmem_fd=0x%lx",
                          pmem_info->pmem_fd);
        drv_ctx.ptr_outputbuffer[i].pmem_fd = pmem_info->pmem_fd;
        drv_ctx.ptr_outputbuffer[i].offset = pmem_info->offset;
        drv_ctx.ptr_outputbuffer[i].bufferaddr = buff;
        drv_ctx.ptr_outputbuffer[i].mmaped_size =
        drv_ctx.ptr_outputbuffer[i].buffer_len = drv_ctx.op_buf.buffer_size;
        privateAppData = appData;
     }

     *bufferHdr = (m_out_mem_ptr + i );

     memcpy (&setbuffers.buffer,&drv_ctx.ptr_outputbuffer[i],
             sizeof (vdpp_bufferpayload));

     DEBUG_PRINT_HIGH(" Set the Output Buffer Idx: %d Addr: %p, pmem_fd=0x%x", i,
                       drv_ctx.ptr_outputbuffer[i].bufferaddr,
                       drv_ctx.ptr_outputbuffer[i].pmem_fd );
#ifndef STUB_VPU
    DEBUG_PRINT_LOW("use_output_buffer: i = %d, streaming[CAPTURE_PORT] = %d ", i, streaming[CAPTURE_PORT]);
    // stream on output port
    if (i == (drv_ctx.op_buf.actualcount -1 ) && !streaming[CAPTURE_PORT]) {
	    enum v4l2_buf_type buf_type;
	    buf_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	    if (ioctl(drv_ctx.video_vpu_fd, VIDIOC_STREAMON,&buf_type)) {
		    DEBUG_PRINT_ERROR("V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE STREAMON failed \n ");
		    return OMX_ErrorInsufficientResources;
	    } else {
		    streaming[CAPTURE_PORT] = true;
		    DEBUG_PRINT_HIGH("V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE STREAMON Successful \n ");
	    }
    }
#endif

     (*bufferHdr)->nAllocLen = drv_ctx.op_buf.buffer_size;
     if (m_enable_android_native_buffers) {
       DEBUG_PRINT_LOW("setting pBuffer to private_handle_t %p", handle);
       (*bufferHdr)->pBuffer = (OMX_U8 *)handle;
     } else {
       (*bufferHdr)->pBuffer = buff;
     }
     (*bufferHdr)->pAppPrivate = privateAppData;
     BITMASK_SET(&m_out_bm_count,i);
  }
  return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::use_input_heap_buffers

DESCRIPTION
  OMX Use Buffer Heap allocation method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None , if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::use_input_heap_buffers(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
  OMX_PTR privateAppData = NULL;
#if defined(_ANDROID_HONEYCOMB_) || defined(_ANDROID_ICS_)
  private_handle_t *handle = NULL;
#endif
  OMX_U8 *buff = buffer;

  DEBUG_PRINT_LOW("Inside %s, %p\n", __FUNCTION__, buffer);
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  if(!m_inp_heap_ptr)
  {
  DEBUG_PRINT_LOW("Inside %s 0, %p\n", __FUNCTION__, buffer);
    m_inp_heap_ptr = (OMX_BUFFERHEADERTYPE*)
               calloc( (sizeof(OMX_BUFFERHEADERTYPE)),
               drv_ctx.ip_buf.actualcount);
  }

  if(!m_phdr_pmem_ptr)
  {
  DEBUG_PRINT_LOW("Inside %s 0-1, %p\n", __FUNCTION__, buffer);
    m_phdr_pmem_ptr = (OMX_BUFFERHEADERTYPE**)
               calloc( (sizeof(OMX_BUFFERHEADERTYPE*)),
               drv_ctx.ip_buf.actualcount);
  }
  if(!m_inp_heap_ptr || !m_phdr_pmem_ptr)
  {
    DEBUG_PRINT_ERROR("Insufficent memory");
    eRet = OMX_ErrorInsufficientResources;
  }
  else if (m_in_alloc_cnt < drv_ctx.ip_buf.actualcount)
  {
    DEBUG_PRINT_LOW("Inside %s 2 m_in_alloc_cnt = %lu, drv_ctx.ip_buf.actualcount = %d\n", __FUNCTION__, m_in_alloc_cnt, drv_ctx.ip_buf.actualcount);
    input_use_buffer = true;
    memset(&m_inp_heap_ptr[m_in_alloc_cnt], 0, sizeof(OMX_BUFFERHEADERTYPE));
    // update this buffer for native window buffer in etb
    // OMXNodeInstance::useGraphicBuffer2_l check if pBuffer and pAppPrivate are
    // the same value as passed in. If not, useGraphicBuffer2_l will exit on error
    //
    m_inp_heap_ptr[m_in_alloc_cnt].pBuffer = buffer;
    m_inp_heap_ptr[m_in_alloc_cnt].nAllocLen = bytes;
    m_inp_heap_ptr[m_in_alloc_cnt].pAppPrivate = appData;
    m_inp_heap_ptr[m_in_alloc_cnt].nInputPortIndex = (OMX_U32) OMX_DirInput;
    m_inp_heap_ptr[m_in_alloc_cnt].nOutputPortIndex = (OMX_U32) OMX_DirMax;
    // save mmapped native window buffer address to pPlatformPrivate
    // use this mmaped buffer address in etb_proxy
#if defined(_ANDROID_HONEYCOMB_) || defined(_ANDROID_ICS_)
    /*if(m_enable_android_native_buffers) */{
        if (m_use_android_native_buffers) {
            UseAndroidNativeBufferParams *params = (UseAndroidNativeBufferParams *)appData;
            sp<android_native_buffer_t> nBuf = params->nativeBuffer;
            handle = (private_handle_t *)nBuf->handle;
            privateAppData = params->pAppPrivate;
        } else {
            handle = (private_handle_t *)buff;
            privateAppData = appData;
            //DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 3\n");
        }

        if(!handle) {
            DEBUG_PRINT_ERROR("Native Buffer handle is NULL");
            return OMX_ErrorBadParameter;
        }

        if ((OMX_U32)handle->size < drv_ctx.ip_buf.buffer_size) {
            DEBUG_PRINT_ERROR("Insufficient sized buffer given for playback,"
                              " expected %u, got %lu",
                              drv_ctx.ip_buf.buffer_size, (OMX_U32)handle->size);
            return OMX_ErrorBadParameter;
        }

        if (!m_use_android_native_buffers) {
                buff =  (OMX_U8*)mmap(0, handle->size,
                                      PROT_READ|PROT_WRITE, MAP_SHARED, handle->fd, 0);
                //DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 4 buff = %p\n", buff);
                if (buff == MAP_FAILED) {
                  DEBUG_PRINT_ERROR("Failed to mmap pmem with fd = %d, size = %d", handle->fd, handle->size);
                  return OMX_ErrorInsufficientResources;
                }
        }
        // we only need to copy this buffer (read only), no need to preserver this handle
        // this handle is saved for write-unlock in use_output_buffer case
#if defined(_ANDROID_ICS_)
        //native_buffer[i].nativehandle = handle;
        //native_buffer[i].privatehandle = handle;
#endif
        m_inp_heap_ptr[m_in_alloc_cnt].pPlatformPrivate = buff;
        //DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 5 m_inp_heap_ptr = %p, m_inp_heap_ptr[%lu].pPlatformPrivate = %p, m_inp_heap_ptr[%lu].pBuffer = %p\n",
        //    m_inp_heap_ptr, m_in_alloc_cnt, m_inp_heap_ptr[m_in_alloc_cnt].pPlatformPrivate, m_in_alloc_cnt, m_inp_heap_ptr[m_in_alloc_cnt].pBuffer);
    }
#endif
    *bufferHdr = &m_inp_heap_ptr[m_in_alloc_cnt];
    // user passes buffer in, but we need ION buffer
    //DEBUG_PRINT_LOW("Inside %s 6 *bufferHdr = %p, byts = %lu \n", __FUNCTION__, *bufferHdr, bytes);
    eRet = allocate_input_buffer(hComp, &m_phdr_pmem_ptr[m_in_alloc_cnt], port, appData, bytes);
    DEBUG_PRINT_HIGH(" Heap buffer(%p) Pmem buffer(%p)", *bufferHdr, m_phdr_pmem_ptr[m_in_alloc_cnt]);
    if (!m_input_free_q.insert_entry((unsigned)m_phdr_pmem_ptr[m_in_alloc_cnt],
                (unsigned)NULL, (unsigned)NULL))
    {
      DEBUG_PRINT_ERROR("ERROR:Free_q is full");
      return OMX_ErrorInsufficientResources;
    }
    m_in_alloc_cnt++;
  }
  else
  {
    DEBUG_PRINT_ERROR("All i/p buffers have been set! m_in_alloc_cnt = %lu, drv_ctx.ip_buf.actualcount = %d", m_in_alloc_cnt, drv_ctx.ip_buf.actualcount);
    eRet = OMX_ErrorInsufficientResources;
  }
  return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::use_input_buffers

DESCRIPTION
  OMX Use Buffer method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None , if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::use_input_buffers(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
  OMX_PTR privateAppData = NULL;
  OMX_BUFFERHEADERTYPE *input = NULL;
#if defined(_ANDROID_HONEYCOMB_) || defined(_ANDROID_ICS_)
  private_handle_t *handle = NULL;
#endif
  OMX_U8 *buff = buffer;
  unsigned int  i = 0;

  DEBUG_PRINT_LOW("Inside %s, %p\n", __FUNCTION__, buffer);
  OMX_ERRORTYPE eRet = OMX_ErrorNone;

  if(!m_inp_heap_ptr)
  {
  DEBUG_PRINT_LOW("Inside %s 0, %p\n", __FUNCTION__, buffer);
    m_inp_heap_ptr = (OMX_BUFFERHEADERTYPE*)
               calloc( (sizeof(OMX_BUFFERHEADERTYPE)),
               drv_ctx.ip_buf.actualcount);
  }

  if(!m_phdr_pmem_ptr)
  {
  DEBUG_PRINT_LOW("Inside %s 0-1, %p\n", __FUNCTION__, buffer);
    m_phdr_pmem_ptr = (OMX_BUFFERHEADERTYPE**)
               calloc( (sizeof(OMX_BUFFERHEADERTYPE*)),
               drv_ctx.ip_buf.actualcount);
  }

  if(!m_inp_heap_ptr || !m_phdr_pmem_ptr)
  {
    DEBUG_PRINT_ERROR("Insufficent memory");
    eRet = OMX_ErrorInsufficientResources;
  }
  else if (m_in_alloc_cnt < drv_ctx.ip_buf.actualcount)
  {
    DEBUG_PRINT_LOW("Inside %s 2 m_in_alloc_cnt = %lu, drv_ctx.ip_buf.actualcount = %d\n", __FUNCTION__, m_in_alloc_cnt, drv_ctx.ip_buf.actualcount);
    input_use_buffer = true;
    memset(&m_inp_heap_ptr[m_in_alloc_cnt], 0, sizeof(OMX_BUFFERHEADERTYPE));
    // update this buffer for native window buffer in etb
    // OMXNodeInstance::useGraphicBuffer2_l check if pBuffer and pAppPrivate are
    // the same value as passed in. If not, useGraphicBuffer2_l will exit on error
    //
    m_inp_heap_ptr[m_in_alloc_cnt].pBuffer = buffer;
    m_inp_heap_ptr[m_in_alloc_cnt].nAllocLen = bytes;
    m_inp_heap_ptr[m_in_alloc_cnt].pAppPrivate = appData;
    m_inp_heap_ptr[m_in_alloc_cnt].nInputPortIndex = (OMX_U32) OMX_DirInput;
    m_inp_heap_ptr[m_in_alloc_cnt].nOutputPortIndex = (OMX_U32) OMX_DirMax;
    // save mmapped native window buffer address to pPlatformPrivate
    // use this mmaped buffer address in etb_proxy
#if defined(_ANDROID_HONEYCOMB_) || defined(_ANDROID_ICS_)
    /*if(m_enable_android_native_buffers) */{
        if (m_use_android_native_buffers) {
            UseAndroidNativeBufferParams *params = (UseAndroidNativeBufferParams *)appData;
            sp<android_native_buffer_t> nBuf = params->nativeBuffer;
            handle = (private_handle_t *)nBuf->handle;
            privateAppData = params->pAppPrivate;
        } else {
            handle = (private_handle_t *)buff;
            privateAppData = appData;
            DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 3\n");
        }

        if(!handle) {
            DEBUG_PRINT_ERROR("Native Buffer handle is NULL");
            return OMX_ErrorBadParameter;
        }

        if ((OMX_U32)handle->size < drv_ctx.ip_buf.buffer_size) {
            DEBUG_PRINT_ERROR("Insufficient sized buffer given for playback,"
                              " expected %u, got %lu",
                              drv_ctx.ip_buf.buffer_size, (OMX_U32)handle->size);
            return OMX_ErrorBadParameter;
        }

        if (!m_use_android_native_buffers) {
                buff =  (OMX_U8*)mmap(0, (handle->size - drv_ctx.ip_buf.frame_size),
                                      PROT_READ|PROT_WRITE, MAP_SHARED, handle->fd, drv_ctx.ip_buf.frame_size);

                DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 4 buff = %p, size1=%d, size2=%d\n", buff,
                        drv_ctx.ip_buf.buffer_size,
                                     handle->size);
                if (buff == MAP_FAILED) {
                  DEBUG_PRINT_ERROR("Failed to mmap pmem with fd = %d, size = %d", handle->fd, handle->size);
                  return OMX_ErrorInsufficientResources;
                }
        }
        // we only need to copy this buffer (read only), no need to preserver this handle
        // this handle is saved for write-unlock in use_output_buffer case
#if defined(_ANDROID_ICS_)
        //native_buffer[i].nativehandle = handle;
        //native_buffer[i].privatehandle = handle;
#endif
        m_inp_heap_ptr[m_in_alloc_cnt].pPlatformPrivate = buff;
        //DEBUG_PRINT_LOW("omx_vdpp::use_input_heap_buffers 5 m_inp_heap_ptr = %p, m_inp_heap_ptr[%lu].pPlatformPrivate = %p, m_inp_heap_ptr[%lu].pBuffer = %p\n",
        //    m_inp_heap_ptr, m_in_alloc_cnt, m_inp_heap_ptr[m_in_alloc_cnt].pPlatformPrivate, m_in_alloc_cnt, m_inp_heap_ptr[m_in_alloc_cnt].pBuffer);
    }
#endif
   *bufferHdr = &m_inp_heap_ptr[m_in_alloc_cnt];

  if(!m_inp_mem_ptr)
  {
    DEBUG_PRINT_HIGH(" Allocate i/p buffer Header: Cnt(%d) Sz(%d)",
      drv_ctx.ip_buf.actualcount,
      drv_ctx.ip_buf.buffer_size);

    m_inp_mem_ptr = (OMX_BUFFERHEADERTYPE*) \
    calloc( (sizeof(OMX_BUFFERHEADERTYPE)), drv_ctx.ip_buf.actualcount);

    if (m_inp_mem_ptr == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    drv_ctx.ptr_inputbuffer = (struct vdpp_bufferpayload *) \
    calloc ((sizeof (struct vdpp_bufferpayload)),drv_ctx.ip_buf.actualcount);

    if (drv_ctx.ptr_inputbuffer == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }
  }

  for(i=0; i< drv_ctx.ip_buf.actualcount; i++)
  {
    if(BITMASK_ABSENT(&m_inp_bm_count,i))
    {
      DEBUG_PRINT_LOW(" Free Input Buffer Index %d",i);
      break;
    }
  }

  if(i < drv_ctx.ip_buf.actualcount)
  {
    struct v4l2_buffer buf;
    struct v4l2_plane plane;
    int rc;

    m_phdr_pmem_ptr[m_in_alloc_cnt] = (m_inp_mem_ptr + i);

    drv_ctx.ptr_inputbuffer [i].bufferaddr = buff;
    drv_ctx.ptr_inputbuffer [i].pmem_fd = handle->fd;
    drv_ctx.ptr_inputbuffer [i].buffer_len = drv_ctx.ip_buf.buffer_size;
    drv_ctx.ptr_inputbuffer [i].mmaped_size = handle->size - drv_ctx.ip_buf.frame_size;
    drv_ctx.ptr_inputbuffer [i].offset = 0;

    input = m_phdr_pmem_ptr[m_in_alloc_cnt];
    BITMASK_SET(&m_inp_bm_count,i);
    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer Buffer address %p of pmem",*bufferHdr);

    input->pBuffer           = (OMX_U8 *)buff;
    input->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
    input->nVersion.nVersion = OMX_SPEC_VERSION;
    input->nAllocLen         = drv_ctx.ip_buf.buffer_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    input->pInputPortPrivate = (void *)&drv_ctx.ptr_inputbuffer [i]; // used in empty_this_buffer_proxy

    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer input->pBuffer %p of pmem, input->pInputPortPrivate = %p",input->pBuffer, input->pInputPortPrivate);
    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer memset drv_ctx.ip_buf.buffer_size = %d\n", drv_ctx.ip_buf.buffer_size);

  }
  else
  {
    DEBUG_PRINT_ERROR("ERROR:Input Buffer Index not found");
    eRet = OMX_ErrorInsufficientResources;
  }

  if (!m_input_free_q.insert_entry((unsigned)m_phdr_pmem_ptr[m_in_alloc_cnt],
            (unsigned)NULL, (unsigned)NULL))
  {
    DEBUG_PRINT_ERROR("ERROR:Free_q is full");
    return OMX_ErrorInsufficientResources;
  }
    m_in_alloc_cnt++;
  }
  else
  {
    DEBUG_PRINT_ERROR("All i/p buffers have been set! m_in_alloc_cnt = %lu, drv_ctx.ip_buf.actualcount = %d", m_in_alloc_cnt, drv_ctx.ip_buf.actualcount);
    eRet = OMX_ErrorInsufficientResources;
  }
  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::UseBuffer

DESCRIPTION
  OMX Use Buffer method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None , if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::use_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
  OMX_ERRORTYPE error = OMX_ErrorNone;
  struct vdpp_setbuffer_cmd setbuffers;

  if ((bufferHdr == NULL) || (bytes == 0) || (/*!secure_mode && */buffer == NULL))
  {
      DEBUG_PRINT_ERROR("bad param 0x%p %ld 0x%p",bufferHdr, bytes, buffer);
      return OMX_ErrorBadParameter;
  }
  if(m_state == OMX_StateInvalid)
  {
    DEBUG_PRINT_ERROR("Use Buffer in Invalid State\n");
    return OMX_ErrorInvalidState;
  }
  if(port == OMX_CORE_INPUT_PORT_INDEX)
    //error = use_input_heap_buffers(hComp, bufferHdr, port, appData, bytes, buffer);
    error = use_input_buffers(hComp, bufferHdr, port, appData, bytes, buffer);  // option to use vdec buffer

  else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    error = use_output_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
  else
  {
    DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",(int)port);
    error = OMX_ErrorBadPortIndex;
  }
  DEBUG_PRINT_LOW("Use Buffer: port %lu, buffer %p, eRet %d", port, *bufferHdr, error);
  if(error == OMX_ErrorNone)
  {
    if(allocate_done() && BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
    {
      // Send the callback now
      BITMASK_CLEAR((&m_flags),OMX_COMPONENT_IDLE_PENDING);
      post_event(OMX_CommandStateSet,OMX_StateIdle,
                         OMX_COMPONENT_GENERATE_EVENT);
    }
    if(port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated &&
       BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
    {
      BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
      post_event(OMX_CommandPortEnable,
          OMX_CORE_INPUT_PORT_INDEX,
          OMX_COMPONENT_GENERATE_EVENT);
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX && m_out_bPopulated &&
            BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
    {
      BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
      post_event(OMX_CommandPortEnable,
                 OMX_CORE_OUTPUT_PORT_INDEX,
                 OMX_COMPONENT_GENERATE_EVENT);
    }
  }
  DEBUG_PRINT_LOW("Use Buffer error = %d", error);
  return error;
}

OMX_ERRORTYPE omx_vdpp::free_input_buffer(OMX_BUFFERHEADERTYPE *bufferHdr)
{
  unsigned int index = 0;
  if (bufferHdr == NULL || m_inp_mem_ptr == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  index = bufferHdr - m_inp_mem_ptr;

  // decrease m_in_alloc_cnt so use_input_heap_buffer can be called
  // again after port re-enable
  m_in_alloc_cnt--;
  DEBUG_PRINT_LOW("free_input_buffer Free Input Buffer index = %d, m_in_alloc_cnt = %lu",index, m_in_alloc_cnt);

  if (index < drv_ctx.ip_buf.actualcount && drv_ctx.ptr_inputbuffer) {
      DEBUG_PRINT_LOW("Free Input ION Buffer index = %d",index);
      if (drv_ctx.ptr_inputbuffer[index].pmem_fd > 0) {
          struct vdpp_setbuffer_cmd setbuffers;
          setbuffers.buffer_type = VDPP_BUFFER_TYPE_INPUT;
          memcpy (&setbuffers.buffer,&drv_ctx.ptr_inputbuffer[index],
                  sizeof (vdpp_bufferpayload));
          {
              DEBUG_PRINT_LOW(" unmap the input buffer fd=%d",
                      drv_ctx.ptr_inputbuffer[index].pmem_fd);
              DEBUG_PRINT_LOW(" unmap the input buffer size=%d  address = %p",
                      drv_ctx.ptr_inputbuffer[index].mmaped_size,
                      drv_ctx.ptr_inputbuffer[index].bufferaddr);
              munmap (drv_ctx.ptr_inputbuffer[index].bufferaddr,
                      drv_ctx.ptr_inputbuffer[index].mmaped_size);
          }

          // If drv_ctx.ip_buf_ion_info is NULL then ION buffer is passed from upper layer.
          // don't close fd and free this buffer, leave upper layer close and free this buffer
          drv_ctx.ptr_inputbuffer[index].pmem_fd = -1;
          if(drv_ctx.ip_buf_ion_info != NULL)
          {
              close (drv_ctx.ptr_inputbuffer[index].pmem_fd);
      #ifdef USE_ION
              free_ion_memory(&drv_ctx.ip_buf_ion_info[index]);
      #endif
          }

      }
  }
  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdpp::free_output_buffer(OMX_BUFFERHEADERTYPE *bufferHdr)
{
  unsigned int index = 0;

  if (bufferHdr == NULL || m_out_mem_ptr == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  index = bufferHdr - m_out_mem_ptr;
  DEBUG_PRINT_LOW(" Free output Buffer index = %d",index);

  if (index < drv_ctx.op_buf.actualcount
      && drv_ctx.ptr_outputbuffer)
  {
    DEBUG_PRINT_LOW(" Free output Buffer index = %d addr = %p", index,
                    drv_ctx.ptr_outputbuffer[index].bufferaddr);

    struct vdpp_setbuffer_cmd setbuffers;
    setbuffers.buffer_type = VDPP_BUFFER_TYPE_OUTPUT;
    memcpy (&setbuffers.buffer,&drv_ctx.ptr_outputbuffer[index],
        sizeof (vdpp_bufferpayload));
#ifdef _ANDROID_
    if(m_enable_android_native_buffers) {
        DEBUG_PRINT_LOW(" Free output Buffer android pmem_fd=%d", drv_ctx.ptr_outputbuffer[index].pmem_fd);
        if(drv_ctx.ptr_outputbuffer[index].pmem_fd > 0) {
            DEBUG_PRINT_LOW(" Free output Buffer android 2 bufferaddr=%p, mmaped_size=%d",
                    drv_ctx.ptr_outputbuffer[index].bufferaddr,
                    drv_ctx.ptr_outputbuffer[index].mmaped_size);
            if( NULL != drv_ctx.ptr_outputbuffer[index].bufferaddr)
            {
                munmap(drv_ctx.ptr_outputbuffer[index].bufferaddr,
                        drv_ctx.ptr_outputbuffer[index].mmaped_size);
            }
        }
        drv_ctx.ptr_outputbuffer[index].pmem_fd = -1;
    } else {
#endif
        if (drv_ctx.ptr_outputbuffer[index].pmem_fd > 0 && !ouput_egl_buffers && !m_use_output_pmem)
        {
            {
                DEBUG_PRINT_LOW(" unmap the output buffer fd = %d",
                        drv_ctx.ptr_outputbuffer[index].pmem_fd);
                DEBUG_PRINT_LOW(" unmap the ouput buffer size=%d  address = %p",
                        drv_ctx.ptr_outputbuffer[index].mmaped_size * drv_ctx.op_buf.actualcount,
                        drv_ctx.ptr_outputbuffer[index].bufferaddr);
                munmap (drv_ctx.ptr_outputbuffer[index].bufferaddr,
                        drv_ctx.ptr_outputbuffer[index].mmaped_size * drv_ctx.op_buf.actualcount);
            }
            close (drv_ctx.ptr_outputbuffer[index].pmem_fd);
            drv_ctx.ptr_outputbuffer[index].pmem_fd = -1;
#ifdef USE_ION
            free_ion_memory(&drv_ctx.op_buf_ion_info[index]);
#endif
        }
#ifdef _ANDROID_
    }
#endif
    if (release_output_done()) {
      //free_extradata();
    }
  }

  return OMX_ErrorNone;

}

OMX_ERRORTYPE omx_vdpp::allocate_input_heap_buffer(OMX_HANDLETYPE       hComp,
                                         OMX_BUFFERHEADERTYPE **bufferHdr,
                                         OMX_U32              port,
                                         OMX_PTR              appData,
                                         OMX_U32              bytes)
{
  OMX_BUFFERHEADERTYPE *input = NULL;
  unsigned char *buf_addr = NULL;
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  unsigned   i = 0;

  /* Sanity Check*/
  if (bufferHdr == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  if (m_inp_heap_ptr == NULL)
  {
    m_inp_heap_ptr = (OMX_BUFFERHEADERTYPE*) \
                     calloc( (sizeof(OMX_BUFFERHEADERTYPE)),
                     drv_ctx.ip_buf.actualcount);
    m_phdr_pmem_ptr = (OMX_BUFFERHEADERTYPE**) \
                     calloc( (sizeof(OMX_BUFFERHEADERTYPE*)),
                     drv_ctx.ip_buf.actualcount);

    if ((m_inp_heap_ptr == NULL) || (m_phdr_pmem_ptr == NULL))
    {
      DEBUG_PRINT_ERROR(" m_inp_heap_ptr Allocation failed ");
      return OMX_ErrorInsufficientResources;
    }
  }

  /*Find a Free index*/
  for(i=0; i< drv_ctx.ip_buf.actualcount; i++)
  {
    if(BITMASK_ABSENT(&m_heap_inp_bm_count,i))
    {
      DEBUG_PRINT_LOW(" Free Input Buffer Index %d",i);
      break;
    }
  }

  if (i < drv_ctx.ip_buf.actualcount)
  {
    buf_addr = (unsigned char *)malloc (drv_ctx.ip_buf.buffer_size);

    if (buf_addr == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    *bufferHdr = (m_inp_heap_ptr + i);
    input = *bufferHdr;
    BITMASK_SET(&m_heap_inp_bm_count,i);

    input->pBuffer           = (OMX_U8 *)buf_addr;
    input->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
    input->nVersion.nVersion = OMX_SPEC_VERSION;
    input->nAllocLen         = drv_ctx.ip_buf.buffer_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    DEBUG_PRINT_LOW(" Address of Heap Buffer %p",*bufferHdr );
    eRet = allocate_input_buffer(hComp,&m_phdr_pmem_ptr [i],port,appData,bytes);
    DEBUG_PRINT_LOW(" Address of Pmem Buffer %p",m_phdr_pmem_ptr[i]);
    /*Add the Buffers to freeq*/
    if (!m_input_free_q.insert_entry((unsigned)m_phdr_pmem_ptr[i],
                (unsigned)NULL, (unsigned)NULL))
    {
      DEBUG_PRINT_ERROR("ERROR:Free_q is full");
      return OMX_ErrorInsufficientResources;
    }
  }
  else
  {
    return OMX_ErrorBadParameter;
  }

  return eRet;

}

/* ======================================================================
FUNCTION
  omx_vdpp::AllocateInputBuffer

DESCRIPTION
  Helper function for allocate buffer in the input pin

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::allocate_input_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes)
{

  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct vdpp_setbuffer_cmd setbuffers;
  OMX_BUFFERHEADERTYPE *input = NULL;
  unsigned   i = 0;
  unsigned char *buf_addr = NULL;
  int pmem_fd = -1;

  if(bytes != drv_ctx.ip_buf.buffer_size)
  {
    DEBUG_PRINT_LOW(" Requested Size is wrong %lu expected is %d",
      bytes, drv_ctx.ip_buf.buffer_size);
     return OMX_ErrorBadParameter;
  }

  if(!m_inp_mem_ptr)
  {
    DEBUG_PRINT_HIGH(" Allocate i/p buffer Header: Cnt(%d) Sz(%d)",
      drv_ctx.ip_buf.actualcount,
      drv_ctx.ip_buf.buffer_size);

    m_inp_mem_ptr = (OMX_BUFFERHEADERTYPE*) \
    calloc( (sizeof(OMX_BUFFERHEADERTYPE)), drv_ctx.ip_buf.actualcount);

    if (m_inp_mem_ptr == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    drv_ctx.ptr_inputbuffer = (struct vdpp_bufferpayload *) \
    calloc ((sizeof (struct vdpp_bufferpayload)),drv_ctx.ip_buf.actualcount);

    if (drv_ctx.ptr_inputbuffer == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }
#ifdef USE_ION
    drv_ctx.ip_buf_ion_info = (struct vdpp_ion *) \
    calloc ((sizeof (struct vdpp_ion)),drv_ctx.ip_buf.actualcount);

    if (drv_ctx.ip_buf_ion_info == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }
#endif

    for (i=0; i < drv_ctx.ip_buf.actualcount; i++)
    {
      drv_ctx.ptr_inputbuffer [i].pmem_fd = -1;
#ifdef USE_ION
      drv_ctx.ip_buf_ion_info[i].ion_device_fd = -1;
#endif
    }
  }

  for(i=0; i< drv_ctx.ip_buf.actualcount; i++)
  {
    if(BITMASK_ABSENT(&m_inp_bm_count,i))
    {
      DEBUG_PRINT_LOW(" Free Input Buffer Index %d",i);
      break;
    }
  }

  if(i < drv_ctx.ip_buf.actualcount)
  {
    struct v4l2_buffer buf;
    struct v4l2_plane plane;
    int rc;
    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer Allocate input Buffer, drv_ctx.ip_buf.buffer_size = %d", drv_ctx.ip_buf.buffer_size);
#ifdef USE_ION
 drv_ctx.ip_buf_ion_info[i].ion_device_fd = alloc_map_ion_memory(
                    drv_ctx.ip_buf.buffer_size,drv_ctx.op_buf.alignment,
                    &drv_ctx.ip_buf_ion_info[i].ion_alloc_data,
		    &drv_ctx.ip_buf_ion_info[i].fd_ion_data, 0);
    if(drv_ctx.ip_buf_ion_info[i].ion_device_fd < 0) {
        return OMX_ErrorInsufficientResources;
     }
    pmem_fd = drv_ctx.ip_buf_ion_info[i].fd_ion_data.fd;
#endif

    {
        buf_addr = (unsigned char *)mmap(NULL,
          drv_ctx.ip_buf.buffer_size,
          PROT_READ|PROT_WRITE, MAP_SHARED, pmem_fd, 0);

        if (buf_addr == MAP_FAILED)
        {
            close(pmem_fd);
#ifdef USE_ION
            free_ion_memory(&drv_ctx.ip_buf_ion_info[i]);
#endif
          DEBUG_PRINT_ERROR(" Map Failed to allocate input buffer");
          return OMX_ErrorInsufficientResources;
        }
    }
    *bufferHdr = (m_inp_mem_ptr + i);

    drv_ctx.ptr_inputbuffer [i].bufferaddr = buf_addr;
    drv_ctx.ptr_inputbuffer [i].pmem_fd = pmem_fd;
    drv_ctx.ptr_inputbuffer [i].buffer_len = drv_ctx.ip_buf.buffer_size;
    drv_ctx.ptr_inputbuffer [i].mmaped_size = drv_ctx.ip_buf.buffer_size;
    drv_ctx.ptr_inputbuffer [i].offset = 0;

    input = *bufferHdr;
    BITMASK_SET(&m_inp_bm_count,i);
    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer Buffer address %p of pmem",*bufferHdr);

    input->pBuffer           = (OMX_U8 *)buf_addr;
    input->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
    input->nVersion.nVersion = OMX_SPEC_VERSION;
    input->nAllocLen         = drv_ctx.ip_buf.buffer_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    input->pInputPortPrivate = (void *)&drv_ctx.ptr_inputbuffer [i]; // used in empty_this_buffer_proxy

    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer input->pBuffer %p of pmem, input->pInputPortPrivate = %p",input->pBuffer, input->pInputPortPrivate);
    memset(buf_addr, 0, drv_ctx.ip_buf.buffer_size);
    DEBUG_PRINT_LOW("omx_vdpp::allocate_input_buffer memset drv_ctx.ip_buf.buffer_size = %d\n", drv_ctx.ip_buf.buffer_size);
  }
  else
  {
    DEBUG_PRINT_ERROR("ERROR:Input Buffer Index not found");
    eRet = OMX_ErrorInsufficientResources;
  }
  return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::AllocateOutputBuffer

DESCRIPTION
  Helper fn for AllocateBuffer in the output pin

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything went well.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::allocate_output_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE       *bufHdr= NULL; // buffer header
  unsigned                         i= 0; // Temporary counter
  struct vdpp_setbuffer_cmd setbuffers;
  int extra_idx = 0;
#ifdef USE_ION
  int ion_device_fd =-1;
  struct ion_allocation_data ion_alloc_data;
  struct ion_fd_data fd_ion_data;
#endif
  if(!m_out_mem_ptr)
  {
    DEBUG_PRINT_HIGH(" Allocate o/p buffer Header: Cnt(%d) Sz(%d)",
      drv_ctx.op_buf.actualcount,
      drv_ctx.op_buf.buffer_size);
    int nBufHdrSize        = 0;
    int pmem_fd = -1;
    unsigned char *pmem_baseaddress = NULL;

    DEBUG_PRINT_LOW("Allocating First Output Buffer(%d)\n",
      drv_ctx.op_buf.actualcount);
    nBufHdrSize        = drv_ctx.op_buf.actualcount *
                         sizeof(OMX_BUFFERHEADERTYPE);
#ifdef USE_ION
 ion_device_fd = alloc_map_ion_memory(
                    drv_ctx.op_buf.buffer_size * drv_ctx.op_buf.actualcount,
                    drv_ctx.op_buf.alignment,
                    &ion_alloc_data, &fd_ion_data, 0);
    if (ion_device_fd < 0) {
        return OMX_ErrorInsufficientResources;
    }
    pmem_fd = fd_ion_data.fd;
#endif

   {
        pmem_baseaddress = (unsigned char *)mmap(NULL,
                           (drv_ctx.op_buf.buffer_size *
                            drv_ctx.op_buf.actualcount),
                            PROT_READ|PROT_WRITE,MAP_SHARED,pmem_fd,0);
        if (pmem_baseaddress == MAP_FAILED)
        {
          DEBUG_PRINT_ERROR(" MMAP failed for Size %d",
          drv_ctx.op_buf.buffer_size);
          close(pmem_fd);
#ifdef USE_ION
          free_ion_memory(&drv_ctx.op_buf_ion_info[i]);
#endif
          return OMX_ErrorInsufficientResources;
        }
    }

    m_out_mem_ptr = (OMX_BUFFERHEADERTYPE  *)calloc(nBufHdrSize,1);

    drv_ctx.ptr_outputbuffer = (struct vdpp_bufferpayload *)\
      calloc (sizeof(struct vdpp_bufferpayload),
      drv_ctx.op_buf.actualcount);
    drv_ctx.ptr_respbuffer = (struct vdpp_output_frameinfo  *)\
      calloc (sizeof (struct vdpp_output_frameinfo),
      drv_ctx.op_buf.actualcount);
#ifdef USE_ION
    drv_ctx.op_buf_ion_info = (struct vdpp_ion *)\
      calloc (sizeof(struct vdpp_ion),
      drv_ctx.op_buf.actualcount);

      if (!drv_ctx.op_buf_ion_info) {
          DEBUG_PRINT_ERROR("Failed to alloc drv_ctx.op_buf_ion_info");
          return OMX_ErrorInsufficientResources;
      }
#endif

    if(m_out_mem_ptr /*&& pPtr*/ && drv_ctx.ptr_outputbuffer
       && drv_ctx.ptr_respbuffer)
    {
      drv_ctx.ptr_outputbuffer[0].mmaped_size =
        (drv_ctx.op_buf.buffer_size *
         drv_ctx.op_buf.actualcount);
      bufHdr          =  m_out_mem_ptr;

      DEBUG_PRINT_LOW("Memory Allocation Succeeded for OUT port%p\n",m_out_mem_ptr);

      for(i=0; i < drv_ctx.op_buf.actualcount ; i++)
      {
        bufHdr->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        bufHdr->nVersion.nVersion  = OMX_SPEC_VERSION;
        // Set the values when we determine the right HxW param
        bufHdr->nAllocLen          = bytes;
        bufHdr->nFilledLen         = 0;
        bufHdr->pAppPrivate        = appData;
        bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
        bufHdr->pBuffer            = NULL;
        bufHdr->nOffset            = 0;

        drv_ctx.ptr_outputbuffer[i].pmem_fd = pmem_fd;
#ifdef USE_ION
        drv_ctx.op_buf_ion_info[i].ion_device_fd = ion_device_fd;
        drv_ctx.op_buf_ion_info[i].ion_alloc_data = ion_alloc_data;
        drv_ctx.op_buf_ion_info[i].fd_ion_data = fd_ion_data;
#endif

        /*Create a mapping between buffers*/
        bufHdr->pOutputPortPrivate = &drv_ctx.ptr_respbuffer[i];
        drv_ctx.ptr_respbuffer[i].client_data = (void *)\
                                            &drv_ctx.ptr_outputbuffer[i];
        drv_ctx.ptr_outputbuffer[i].offset = drv_ctx.op_buf.buffer_size*i;
        drv_ctx.ptr_outputbuffer[i].bufferaddr =
          pmem_baseaddress + (drv_ctx.op_buf.buffer_size*i);

        DEBUG_PRINT_LOW(" pmem_fd = %d offset = %d address = %p",
          pmem_fd, drv_ctx.ptr_outputbuffer[i].offset,
          drv_ctx.ptr_outputbuffer[i].bufferaddr);
        // Move the buffer and buffer header pointers
        bufHdr++;

      }
    }
    else
    {
      if(m_out_mem_ptr)
      {
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
      }

      if(drv_ctx.ptr_outputbuffer)
      {
        free(drv_ctx.ptr_outputbuffer);
        drv_ctx.ptr_outputbuffer = NULL;
      }
      if(drv_ctx.ptr_respbuffer)
      {
        free(drv_ctx.ptr_respbuffer);
        drv_ctx.ptr_respbuffer = NULL;
      }
#ifdef USE_ION
    if (drv_ctx.op_buf_ion_info) {
        DEBUG_PRINT_LOW(" Free o/p ion context");
	free(drv_ctx.op_buf_ion_info);
        drv_ctx.op_buf_ion_info = NULL;
    }
#endif
      eRet =  OMX_ErrorInsufficientResources;
    }
  }

  for(i=0; i< drv_ctx.op_buf.actualcount; i++)
  {
    if(BITMASK_ABSENT(&m_out_bm_count,i))
    {
      DEBUG_PRINT_LOW(" Found a Free Output Buffer %d",i);
      break;
    }
  }

  if (eRet == OMX_ErrorNone)
  {
    if(i < drv_ctx.op_buf.actualcount)
    {
      struct v4l2_buffer buf;
      struct v4l2_plane plane[VIDEO_MAX_PLANES];
      int rc;

      drv_ctx.ptr_outputbuffer[i].buffer_len =
        drv_ctx.op_buf.buffer_size;

    *bufferHdr = (m_out_mem_ptr + i );
    drv_ctx.ptr_outputbuffer[i].mmaped_size = drv_ctx.op_buf.buffer_size;

#ifndef STUB_VPU
	  if (i == (drv_ctx.op_buf.actualcount -1 ) && !streaming[CAPTURE_PORT]) {
		enum v4l2_buf_type buf_type;
		buf_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		rc=ioctl(drv_ctx.video_vpu_fd, VIDIOC_STREAMON,&buf_type);
		if (rc) {
			DEBUG_PRINT_ERROR("allocate_output_buffer STREAMON failed \n ");
			return OMX_ErrorInsufficientResources;
		} else {
			streaming[CAPTURE_PORT] = true;
			DEBUG_PRINT_HIGH("allocate_output_buffer STREAMON Successful \n ");
		}
	  }
#endif

      (*bufferHdr)->pBuffer = (OMX_U8*)drv_ctx.ptr_outputbuffer[i].bufferaddr;
      (*bufferHdr)->pAppPrivate = appData;
      BITMASK_SET(&m_out_bm_count,i);
    }
    else
    {
      DEBUG_PRINT_ERROR("All the Output Buffers have been Allocated ; Returning Insufficient \n");
      eRet = OMX_ErrorInsufficientResources;
    }
  }

  return eRet;
}


// AllocateBuffer  -- API Call
/* ======================================================================
FUNCTION
  omx_vdpp::AllocateBuffer

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::allocate_buffer(OMX_IN OMX_HANDLETYPE                hComp,
                                     OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                     OMX_IN OMX_U32                        port,
                                     OMX_IN OMX_PTR                     appData,
                                     OMX_IN OMX_U32                       bytes)
{
    unsigned i = 0;
    OMX_ERRORTYPE eRet = OMX_ErrorNone; // OMX return type

    DEBUG_PRINT_LOW(" Allocate buffer on port %d \n", (int)port);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Allocate Buf in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
        eRet = allocate_input_heap_buffer(hComp,bufferHdr,port,appData,bytes);
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        eRet = allocate_output_buffer(hComp,bufferHdr,port,appData,bytes);
    }
    else
    {
      DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",(int)port);
      eRet = OMX_ErrorBadPortIndex;
    }
    DEBUG_PRINT_LOW("Checking for Output Allocate buffer Done");
    if(eRet == OMX_ErrorNone)
    {
        if(allocate_done()){
            if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                // Send the callback now
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_IDLE_PENDING);
                post_event(OMX_CommandStateSet,OMX_StateIdle,
                                   OMX_COMPONENT_GENERATE_EVENT);
            }
        }
        if(port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated)
        {
          if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
          {
             BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
             post_event(OMX_CommandPortEnable,
                        OMX_CORE_INPUT_PORT_INDEX,
                        OMX_COMPONENT_GENERATE_EVENT);
          }
        }
        if(port == OMX_CORE_OUTPUT_PORT_INDEX && m_out_bPopulated)
            {
          if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
          {
             BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                post_event(OMX_CommandPortEnable,
                           OMX_CORE_OUTPUT_PORT_INDEX,
                           OMX_COMPONENT_GENERATE_EVENT);
            }
        }
    }
    DEBUG_PRINT_LOW("Allocate Buffer exit with ret Code %d\n",eRet);
    return eRet;
}

// Free Buffer - API call
/* ======================================================================
FUNCTION
  omx_vdpp::FreeBuffer

DESCRIPTION

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::free_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                      OMX_IN OMX_U32                 port,
                                      OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    unsigned int nPortIndex;
    DEBUG_PRINT_LOW("In for vdpp free_buffer \n");

    if(m_state == OMX_StateIdle &&
       (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        DEBUG_PRINT_LOW(" free buffer while Component in Loading pending\n");
    }
    else if((m_inp_bEnabled == OMX_FALSE && port == OMX_CORE_INPUT_PORT_INDEX)||
            (m_out_bEnabled == OMX_FALSE && port == OMX_CORE_OUTPUT_PORT_INDEX))
    {
        DEBUG_PRINT_LOW("Free Buffer while port %lu disabled\n", port);
    }
    else if(m_state == OMX_StateExecuting || m_state == OMX_StatePause)
    {
        DEBUG_PRINT_ERROR("Invalid state to free buffer,ports need to be disabled\n");
        post_event(OMX_EventError,
                   OMX_ErrorPortUnpopulated,
                   OMX_COMPONENT_GENERATE_EVENT);

        return OMX_ErrorIncorrectStateOperation;
    }
    else if (m_state != OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Invalid state to free buffer,port lost Buffers\n");
        post_event(OMX_EventError,
                   OMX_ErrorPortUnpopulated,
                   OMX_COMPONENT_GENERATE_EVENT);
    }

    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
      /*Check if arbitrary bytes*/
      if(!input_use_buffer)
        nPortIndex = buffer - m_inp_mem_ptr;
      else
        nPortIndex = buffer - m_inp_heap_ptr;

        DEBUG_PRINT_LOW("free_buffer on i/p port - Port idx %d \n", nPortIndex);
        if(nPortIndex < drv_ctx.ip_buf.actualcount)
        {
         // Clear the bit associated with it.
         BITMASK_CLEAR(&m_inp_bm_count,nPortIndex);
         BITMASK_CLEAR(&m_heap_inp_bm_count,nPortIndex);
         if (input_use_buffer == true)
         {

            DEBUG_PRINT_LOW(" Free pmem Buffer index %d",nPortIndex);
            if(m_phdr_pmem_ptr)
              free_input_buffer(m_phdr_pmem_ptr[nPortIndex]);
         }
         else
         {
             free_input_buffer(buffer);
         }
         m_inp_bPopulated = OMX_FALSE;
         /*Free the Buffer Header*/
          if (release_input_done())
          {
            DEBUG_PRINT_HIGH(" ALL input buffers are freed/released");
            free_input_buffer_header();
          }
        }
        else
        {
            DEBUG_PRINT_ERROR("Error: free_buffer ,Port Index Invalid\n");
            eRet = OMX_ErrorBadPortIndex;
        }

        if(BITMASK_PRESENT((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING)
           && release_input_done())
        {
            DEBUG_PRINT_LOW("MOVING TO DISABLED STATE \n");
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING);
            post_event(OMX_CommandPortDisable,
                       OMX_CORE_INPUT_PORT_INDEX,
                       OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        // check if the buffer is valid
        nPortIndex = buffer - (OMX_BUFFERHEADERTYPE*)m_out_mem_ptr;
        if(nPortIndex < drv_ctx.op_buf.actualcount)
        {
            DEBUG_PRINT_LOW("free_buffer on o/p port - Port idx %d \n", nPortIndex);
            // Clear the bit associated with it.
            BITMASK_CLEAR(&m_out_bm_count,nPortIndex);
            m_out_bPopulated = OMX_FALSE;
            free_output_buffer (buffer);

            if (release_output_done())
            {
              free_output_buffer_header();
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("Error: free_buffer , Port Index Invalid\n");
            eRet = OMX_ErrorBadPortIndex;
        }
        if(BITMASK_PRESENT((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING)
           && release_output_done())
        {
            DEBUG_PRINT_LOW("FreeBuffer : If any Disable event pending,post it\n");

                DEBUG_PRINT_LOW("MOVING TO DISABLED STATE \n");
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
#ifdef _ANDROID_ICS_
                if (m_enable_android_native_buffers)
                {
                    DEBUG_PRINT_LOW("FreeBuffer - outport disabled: reset native buffers");
                    memset(&native_buffer, 0 ,(sizeof(struct nativebuffer) * MAX_NUM_INPUT_OUTPUT_BUFFERS));
                }
#endif

                post_event(OMX_CommandPortDisable,
                           OMX_CORE_OUTPUT_PORT_INDEX,
                           OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    else
    {
        eRet = OMX_ErrorBadPortIndex;
    }
    if((eRet == OMX_ErrorNone) &&
       (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        if(release_done())
        {
            // Send the callback now
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_LOADING_PENDING);
            post_event(OMX_CommandStateSet, OMX_StateLoaded,
                                      OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::EmptyThisBuffer

DESCRIPTION
  This routine is used to push the video frames to VDPP.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::empty_this_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                           OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
  OMX_ERRORTYPE ret1 = OMX_ErrorNone;
  unsigned int nBufferIndex = drv_ctx.ip_buf.actualcount;

  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer buffer = %p, buffer->pBuffer = %p, buffer->pPlatformPrivate = %p, buffer->nFilledLen = %lu\n",
  //    buffer, buffer->pBuffer, buffer->pPlatformPrivate, buffer->nFilledLen);
  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("Empty this buffer in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  if (buffer == NULL)
  {
    DEBUG_PRINT_ERROR("ERROR:ETB Buffer is NULL");
    return OMX_ErrorBadParameter;
  }

  if (!m_inp_bEnabled)
  {
    DEBUG_PRINT_ERROR("ERROR:ETB incorrect state operation, input port is disabled.");
    return OMX_ErrorIncorrectStateOperation;
  }

  if (buffer->nInputPortIndex != OMX_CORE_INPUT_PORT_INDEX)
  {
    DEBUG_PRINT_ERROR("ERROR:ETB invalid port in header %lu", buffer->nInputPortIndex);
    return OMX_ErrorBadPortIndex;
  }

  if (input_use_buffer == true)
  {
       nBufferIndex = buffer - m_inp_heap_ptr;
       m_inp_mem_ptr[nBufferIndex].nFilledLen = m_inp_heap_ptr[nBufferIndex].nFilledLen;
       m_inp_mem_ptr[nBufferIndex].nTimeStamp = m_inp_heap_ptr[nBufferIndex].nTimeStamp;
       m_inp_mem_ptr[nBufferIndex].nFlags = m_inp_heap_ptr[nBufferIndex].nFlags;
       buffer = &m_inp_mem_ptr[nBufferIndex]; //  change heap buffer address to ION buffer address
       //DEBUG_PRINT_LOW("Non-Arbitrary mode - buffer address is: malloc %p, pmem %p in Index %d, buffer %p of size %lu",
       //                  &m_inp_heap_ptr[nBufferIndex], &m_inp_mem_ptr[nBufferIndex],nBufferIndex, buffer, buffer->nFilledLen);
  }
  else{
       nBufferIndex = buffer - m_inp_mem_ptr;
  }

  if (nBufferIndex > drv_ctx.ip_buf.actualcount )
  {
    DEBUG_PRINT_ERROR("ERROR:ETB nBufferIndex is invalid");
    return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_HIGH("[ETB] BHdr(%p) pBuf(%p) nTS(%lld) nFL(%lu)",
    buffer, buffer->pBuffer, buffer->nTimeStamp, buffer->nFilledLen);

  set_frame_rate(buffer->nTimeStamp);
  post_event ((unsigned)hComp,(unsigned)buffer,OMX_COMPONENT_GENERATE_ETB);

  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdpp::empty_this_buffer_proxy

DESCRIPTION
  This routine is used to push the video decoder output frames to
  the VDPP.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::empty_this_buffer_proxy(OMX_IN OMX_HANDLETYPE         hComp,
                                                 OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
  int i=0;
  unsigned nPortIndex = 0;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  struct vdpp_bufferpayload *temp_buffer;
  unsigned p1 = 0;
  unsigned p2 = 0;

  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 1\n");
  /*Should we generate a Aync error event*/
  if (buffer == NULL || buffer->pInputPortPrivate == NULL)
  {
    DEBUG_PRINT_ERROR("ERROR:empty_this_buffer_proxy is invalid");
    return OMX_ErrorBadParameter;
  }

  nPortIndex = buffer-((OMX_BUFFERHEADERTYPE *)m_inp_mem_ptr);
  DEBUG_PRINT_HIGH("omx_vdpp::empty_this_buffer_proxy 2 nPortIndex = %d, buffer->nFilledLen = %lu\n", nPortIndex, buffer->nFilledLen);
  if (nPortIndex > drv_ctx.ip_buf.actualcount)
  {
    DEBUG_PRINT_ERROR("ERROR:empty_this_buffer_proxy invalid nPortIndex[%u]",
        nPortIndex);
    return OMX_ErrorBadParameter;
  }

  pending_input_buffers++;
  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 3 pending_input_buffers = %d\n", pending_input_buffers);
  /* return zero length and not an EOS buffer */
  if ((buffer->nFilledLen == 0) &&
     ((buffer->nFlags & OMX_BUFFERFLAG_EOS) == 0))
  {
    DEBUG_PRINT_HIGH(" return zero legth buffer");
    post_event ((unsigned int)buffer,VDPP_S_SUCCESS,
                     OMX_COMPONENT_GENERATE_EBD);
    return OMX_ErrorNone;
  }

  // check OMX_BUFFERFLAG_EXTRADATA for interlaced information
  // and set it to drv_ctx.interlace if returned interlace mode
  // doesn't match drv_ctx.interlace.
  DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 3 buffer->nFlags = 0x%x interlace_user_flag = %d ", buffer->nFlags, interlace_user_flag);
  if(((buffer->nFlags & OMX_BUFFERFLAG_EXTRADATA) != 0) && (false == interlace_user_flag))
  {
      OMX_OTHER_EXTRADATATYPE *pExtra;
      v4l2_field field = drv_ctx.interlace;// V4L2_FIELD_NONE;
      OMX_U8 *pTmp = buffer->pBuffer + buffer->nOffset + 3;

      pExtra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U32) pTmp) & ~3);
      DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 3 buffer->nFlags = 0x%x, pExtra->eType = 0x%x\n", buffer->nFlags, pExtra->eType);
      // traverset the list of extra data sections
      while(OMX_ExtraDataNone != pExtra->eType)
      {
          if(OMX_ExtraDataInterlaceFormat == (OMX_QCOM_EXTRADATATYPE)pExtra->eType)
          {
              OMX_STREAMINTERLACEFORMAT *interlace_format;
              interlace_format = (OMX_STREAMINTERLACEFORMAT *)pExtra->data;

              switch (interlace_format->nInterlaceFormats)
              {
                case OMX_InterlaceFrameProgressive:
                    {
                        field = V4L2_FIELD_NONE;
                        DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy  V4L2_FIELD_NONE");
                        break;
                    }
                case OMX_InterlaceInterleaveFrameTopFieldFirst:
                    {
                        field = V4L2_FIELD_INTERLACED_TB;
                        DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy  V4L2_FIELD_INTERLACED_TB");
                        break;
                    }
                case OMX_InterlaceInterleaveFrameBottomFieldFirst:
                    {
                        field = V4L2_FIELD_INTERLACED_BT;
                        DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy  V4L2_FIELD_INTERLACED_BT");
                        break;
                    }
                case OMX_InterlaceFrameTopFieldFirst:
                    {
                        field = V4L2_FIELD_SEQ_TB;
                        break;
                    }
                case OMX_InterlaceFrameBottomFieldFirst:
                    {
                        field = V4L2_FIELD_SEQ_BT;
                        break;
                    }
                default:
                    break;
              }
            break;
          }
          pExtra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) pExtra) + pExtra->nSize);
      }

      if(drv_ctx.interlace != field)
      {
          drv_ctx.interlace = field;

          // set input port format based on the detected interlace mode
          ret = set_buffer_req(&drv_ctx.ip_buf);
          if(OMX_ErrorNone != ret)
          {
             DEBUG_PRINT_ERROR("ERROR:empty_this_buffer_proxy invalid format setting");
             return ret;
          }
      }
  }

  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 4 \n");
  if(input_flush_progress == true)
  {
    DEBUG_PRINT_LOW(" Flush in progress return buffer ");
    post_event ((unsigned int)buffer,VDPP_S_SUCCESS,
                     OMX_COMPONENT_GENERATE_EBD);
    return OMX_ErrorNone;
  }

  temp_buffer = (struct vdpp_bufferpayload *)buffer->pInputPortPrivate;

  if ((temp_buffer -  drv_ctx.ptr_inputbuffer) > drv_ctx.ip_buf.actualcount)
  {
    return OMX_ErrorBadParameter;
  }

  //DEBUG_PRINT_LOW(" ETBProxy: bufhdr = %p, bufhdr->pBuffer = %p", buffer, buffer->pBuffer);
  /*for use_input_heap_buffer memcpy is used*/
  temp_buffer->buffer_len = buffer->nFilledLen;


  if (input_use_buffer)
  {
    //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 5 \n");
    if (buffer->nFilledLen <= temp_buffer->buffer_len)
    {
        DEBUG_PRINT_HIGH("omx_vdpp::empty_this_buffer_proxy 5.1 temp_buffer->bufferaddr = %p, m_inp_heap_ptr[%d].pPlatformPrivate = %p, m_inp_heap_ptr[%d].nOffset = %lu\n",
            temp_buffer->bufferaddr, nPortIndex, m_inp_heap_ptr[nPortIndex].pPlatformPrivate, nPortIndex, m_inp_heap_ptr[nPortIndex].nOffset);
        //memcpy (temp_buffer->bufferaddr, (m_inp_heap_ptr[nPortIndex].pPlatformPrivate + m_inp_heap_ptr[nPortIndex].nOffset),
        //        buffer->nFilledLen);
    }
    else
    {
      return OMX_ErrorBadParameter;
    }
    //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 5.2 \n");
  }

#ifdef INPUT_BUFFER_LOG
    if ((inputBufferFile >= 0) && (input_buffer_write_counter < 10))
    {
        int stride = drv_ctx.video_resolution_input.stride; //w
        int scanlines = drv_ctx.video_resolution_input.scan_lines; //h
        DEBUG_PRINT_HIGH("omx_vdpp::empty_buffer_done 2.5 stride = %d, scanlines = %d , frame_height = %d", stride, scanlines, drv_ctx.video_resolution_input.frame_height);
        char *temp = (char *)temp_buffer->bufferaddr;
	    unsigned i;
	    int bytes_written = 0;
	    for (i = 0; i < drv_ctx.video_resolution_input.frame_height; i++) {
		    bytes_written = write(inputBufferFile, temp, drv_ctx.video_resolution_input.frame_width);
		    temp += stride;
	    }
	    temp = (char *)(char *)temp_buffer->bufferaddr + stride * scanlines;
        int stride_c = stride;
	    for(i = 0; i < drv_ctx.video_resolution_input.frame_height/2; i++) {
		    bytes_written += write(inputBufferFile, temp, drv_ctx.video_resolution_input.frame_width);
		    temp += stride_c;
	    }
        input_buffer_write_counter++;
    }

    if(input_buffer_write_counter >= 10 )
    {
        close(inputBufferFile);
    }
#endif

  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 5.3 \n");
  if(buffer->nFlags & QOMX_VIDEO_BUFFERFLAG_EOSEQ)
  {
     //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 5.4 \n");
    buffer->nFlags &= ~QOMX_VIDEO_BUFFERFLAG_EOSEQ;
  }

    struct v4l2_buffer buf;
    struct v4l2_plane plane[VIDEO_MAX_PLANES];
    int extra_idx = 0;
    int rc;
    unsigned long  print_count;

    memset( (void *)&buf, 0, sizeof(buf));
    memset( (void *)plane, 0, (sizeof(struct v4l2_plane)*VIDEO_MAX_PLANES));

    if (temp_buffer->buffer_len == 0 || (buffer->nFlags & OMX_BUFFERFLAG_EOS))
    {  buf.flags = V4L2_QCOM_BUF_FLAG_EOS;
        DEBUG_PRINT_HIGH("temp_buffer->buffer_len = %d, buffer->nFlags = 0x%lx \n", temp_buffer->buffer_len, buffer->nFlags) ;
        DEBUG_PRINT_HIGH("  INPUT EOS reached \n") ;
    }

	OMX_ERRORTYPE eRet = OMX_ErrorNone;

    // The following fills v4l2_buffer structure
	buf.index = nPortIndex;
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.field = drv_ctx.interlace;
	buf.memory = V4L2_MEMORY_USERPTR;
	buf.length = drv_ctx.input_num_planes;

    // currently V4L2 driver just passes timestamp to maple FW, and maple FW
    // pass the timestamp back to OMX
    *(uint64_t *)(&buf.timestamp) = buffer->nTimeStamp;

    plane[0].bytesused = drv_ctx.video_resolution_input.frame_width *
                         drv_ctx.video_resolution_input.frame_height *
                         drv_ctx.input_bytesperpixel[0];//buffer->nFilledLen = 0 at this stage
    plane[0].length = paddedFrameWidth128(drv_ctx.video_resolution_input.frame_width) *
                      drv_ctx.video_resolution_input.frame_height *
                      drv_ctx.input_bytesperpixel[0];
    plane[0].m.userptr = temp_buffer->pmem_fd;
    plane[0].reserved[0] = 0;
    extra_idx = EXTRADATA_IDX(drv_ctx.input_num_planes);
    if ((extra_idx > 0) && (extra_idx < VIDEO_MAX_PLANES)) {
    plane[extra_idx].bytesused = drv_ctx.video_resolution_input.frame_width *
                                 drv_ctx.video_resolution_input.frame_height *
                                 drv_ctx.input_bytesperpixel[extra_idx];
    plane[extra_idx].length = paddedFrameWidth128(drv_ctx.video_resolution_input.frame_width) *
                              drv_ctx.video_resolution_input.frame_height *
                              drv_ctx.input_bytesperpixel[extra_idx];


    plane[extra_idx].m.userptr = temp_buffer->pmem_fd;
    plane[extra_idx].reserved[0] = plane[0].reserved[0] + drv_ctx.video_resolution_input.stride * drv_ctx.video_resolution_input.scan_lines;
    } else if (extra_idx >= VIDEO_MAX_PLANES) {
    DEBUG_PRINT_ERROR("Extradata index higher than expected: %d\n", extra_idx);
    return OMX_ErrorBadParameter;
    }
    buf.m.planes = plane;
    buf.length = drv_ctx.input_num_planes;
    /*DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy: buffer->nFilledLen = %d, plane[0].bytesused = %d  plane[0].length = %d,\
                    plane[extra_idx].bytesused = %d, plane[extra_idx].length = %d, plane[extra_idx].data_offset = %d plane[0].data_offset = %d\
                    buf.timestamp.tv_sec = 0x%08x, buf.timestamp.tv_usec = 0x%08x\n",
                    buffer->nFilledLen, plane[0].bytesused, plane[0].length, plane[extra_idx].bytesused, plane[extra_idx].length, plane[extra_idx].data_offset, plane[0].data_offset,
                    buf.timestamp.tv_sec, buf.timestamp.tv_usec);
    DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy: buffer->nTimeStamp = 0x%016llx; buf.timestamp.tv_sec = 0x%08lx, buf.timestamp.tv_usec = 0x%08lx\n",
                   buffer->nTimeStamp, buf.timestamp.tv_sec, buf.timestamp.tv_usec);*/

    input_qbuf_count++;

#ifdef STUB_VPU

#else
	rc = ioctl(drv_ctx.video_vpu_fd, VIDIOC_QBUF, &buf);
	if(rc)
	{
		DEBUG_PRINT_ERROR("Failed to qbuf Input buffer to driver\n");
		return OMX_ErrorHardware;
	}
#endif

  //DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 15 \n");
#ifndef STUB_VPU
  if(!streaming[OUTPUT_PORT])
  {
	enum v4l2_buf_type buf_type;
	int ret,r;
    DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 16 \n");
	buf_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Executing\n");
	ret=ioctl(drv_ctx.video_vpu_fd, VIDIOC_STREAMON,&buf_type);
	if(!ret) {
		DEBUG_PRINT_HIGH("V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE STREAMON Successful \n");
		streaming[OUTPUT_PORT] = true;
	} else{
		DEBUG_PRINT_ERROR(" \n Failed to call streamon on V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE \n");
        return OMX_ErrorInsufficientResources;
	}
}
#endif

#ifdef STUB_VPU
  drv_ctx.etb_ftb_info.etb_cnt++;
  DEBUG_PRINT_LOW("omx_vdpp::empty_this_buffer_proxy 15 drv_ctx.etb_ftb_info.etb_cnt = %d\n", drv_ctx.etb_ftb_info.etb_cnt);
  m_index_q_etb.insert_entry(p1,p2,nPortIndex);
  //drv_ctx.etb_ftb_info.etb_index = nPortIndex;
  drv_ctx.etb_ftb_info.etb_len = buf.length;
  sem_post (&(drv_ctx.async_lock));
#endif
  return ret;
}

/* ======================================================================
FUNCTION
  omx_vdpp::FillThisBuffer

DESCRIPTION
  IL client uses this method to release the frame buffer
  after displaying them.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::fill_this_buffer(OMX_IN OMX_HANDLETYPE  hComp,
                                          OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{

  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("FTB in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  if (!m_out_bEnabled)
  {
    DEBUG_PRINT_ERROR("ERROR:FTB incorrect state operation, output port is disabled.");
    return OMX_ErrorIncorrectStateOperation;
  }

  if (buffer == NULL ||
      ((buffer - m_out_mem_ptr) >= drv_ctx.op_buf.actualcount))
  {
    return OMX_ErrorBadParameter;
  }

  if (buffer->nOutputPortIndex != OMX_CORE_OUTPUT_PORT_INDEX)
  {
    DEBUG_PRINT_ERROR("ERROR:FTB invalid port in header %lu", buffer->nOutputPortIndex);
    return OMX_ErrorBadPortIndex;
  }
  //DEBUG_PRINT_LOW("[FTB] bufhdr = %p, bufhdr->pBuffer = %p, buffer->nFilledLen = %lu", buffer, buffer->pBuffer, buffer->nFilledLen);
  post_event((unsigned) hComp, (unsigned)buffer, m_fill_output_msg);

  return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdpp::fill_this_buffer_proxy

DESCRIPTION
  IL client uses this method to release the frame buffer
  after displaying them.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::fill_this_buffer_proxy(
                         OMX_IN OMX_HANDLETYPE        hComp,
                         OMX_IN OMX_BUFFERHEADERTYPE* bufferAdd)
{
  OMX_ERRORTYPE nRet = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE *buffer = bufferAdd;
  unsigned nPortIndex = 0;
  struct vdpp_bufferpayload     *ptr_outputbuffer = NULL;
  struct vdpp_output_frameinfo  *ptr_respbuffer = NULL;
  private_handle_t *handle = NULL;

  unsigned p1 = 0;
  unsigned p2 = 0;

  nPortIndex = buffer-((OMX_BUFFERHEADERTYPE *)m_out_mem_ptr);

  if (bufferAdd == NULL || nPortIndex > drv_ctx.op_buf.actualcount)
    return OMX_ErrorBadParameter;

  DEBUG_PRINT_HIGH(" FTBProxy: nPortIndex = %d, bufhdr = %p, bufhdr->pBuffer = %p, buffer->nFilledLen = %lu",
      nPortIndex, bufferAdd, bufferAdd->pBuffer, buffer->nFilledLen);

      /*Return back the output buffer to client*/
  if(m_out_bEnabled != OMX_TRUE || output_flush_progress == true)
  {
    DEBUG_PRINT_LOW(" Output Buffers return flush/disable condition");
    buffer->nFilledLen = 0;
    m_cb.FillBufferDone (hComp,m_app_data,buffer);
    return OMX_ErrorNone;
  }
  pending_output_buffers++;

  // set from allocate_output_headers
  ptr_respbuffer = (struct vdpp_output_frameinfo*)buffer->pOutputPortPrivate;
  if (ptr_respbuffer)
  {
    ptr_outputbuffer =  (struct vdpp_bufferpayload*)ptr_respbuffer->client_data;
  }

  if (ptr_respbuffer == NULL || ptr_outputbuffer == NULL)
  {
      DEBUG_PRINT_ERROR("resp buffer or outputbuffer is NULL");
      buffer->nFilledLen = 0;
      m_cb.FillBufferDone (hComp,m_app_data,buffer);
      pending_output_buffers--;
      return OMX_ErrorBadParameter;
  }

  int rc = 0;
  struct v4l2_buffer buf;
  struct v4l2_plane plane[VIDEO_MAX_PLANES];
  int extra_idx = 0;
  memset( (void *)&buf, 0, sizeof(buf));
  memset( (void *)plane, 0, (sizeof(struct v4l2_plane)*VIDEO_MAX_PLANES));

  buf.index = nPortIndex;
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  buf.memory = V4L2_MEMORY_USERPTR;
  buf.field = V4L2_FIELD_ANY;

  buf.length = drv_ctx.output_num_planes;
  plane[0].bytesused = drv_ctx.video_resolution_output.frame_width *
                       drv_ctx.video_resolution_output.frame_height *
                       drv_ctx.output_bytesperpixel[0];
  plane[0].length = paddedFrameWidth128(drv_ctx.video_resolution_output.frame_width) *
                       drv_ctx.video_resolution_output.frame_height *
                       drv_ctx.output_bytesperpixel[0];

  plane[0].m.userptr = drv_ctx.ptr_outputbuffer[nPortIndex].pmem_fd;
  plane[0].reserved[0] = 0;
  extra_idx = EXTRADATA_IDX(drv_ctx.output_num_planes);
  if ((extra_idx > 0) && (extra_idx < VIDEO_MAX_PLANES)) {
    plane[extra_idx].bytesused = drv_ctx.video_resolution_output.frame_width *
                                    drv_ctx.video_resolution_output.frame_height *
                                    drv_ctx.output_bytesperpixel[extra_idx];
    plane[extra_idx].length = paddedFrameWidth128(drv_ctx.video_resolution_output.frame_width) *
                                drv_ctx.video_resolution_output.frame_height *
                                drv_ctx.output_bytesperpixel[extra_idx];
    plane[extra_idx].m.userptr = drv_ctx.ptr_outputbuffer[nPortIndex].pmem_fd;
    plane[extra_idx].reserved[0] = plane[0].reserved[0] + drv_ctx.video_resolution_output.stride * drv_ctx.video_resolution_output.scan_lines;// plane[0].length;
    } else if (extra_idx >= VIDEO_MAX_PLANES) {
    DEBUG_PRINT_ERROR("Extradata index higher than expected: %d\n", extra_idx);
    return OMX_ErrorBadParameter;
    }
  buf.m.planes = plane;
  // DEBUG_PRINT_LOW("omx_vdpp::fill_this_buffer_proxy: buffer->nFilledLen = %lu, plane[0].bytesused = %d  plane[0].length = %d, \
                    // plane[extra_idx].bytesused = %d, plane[extra_idx].reserved[0] = 0x%x\n", \
                   // buffer->nFilledLen, plane[0].bytesused, plane[0].length, plane[extra_idx].bytesused, plane[extra_idx].reserved[0]);
  //
  //DEBUG_PRINT_LOW("omx_vdpp::fill_this_buffer_proxy 2 drv_ctx.ptr_outputbuffer[%d].bufferaddr = %p\n", nPortIndex,drv_ctx.ptr_outputbuffer[nPortIndex].bufferaddr);
  //DEBUG_PRINT_LOW("omx_vdpp::fill_this_buffer_proxy 2 drv_ctx.ptr_outputbuffer[%d].offset = %d", nPortIndex,drv_ctx.ptr_outputbuffer[nPortIndex].offset);

#ifdef STUB_VPU

#else
  rc = ioctl(drv_ctx.video_vpu_fd, VIDIOC_QBUF, &buf);
  if (rc) {
    DEBUG_PRINT_ERROR("Failed to qbuf to driver");
    return OMX_ErrorHardware;
  }
#endif

  output_qbuf_count++;
  DEBUG_PRINT_LOW("omx_vdpp::fill_this_buffer_proxy 3\n");
#ifdef STUB_VPU
{
  drv_ctx.etb_ftb_info.ftb_cnt++;
  m_index_q_ftb.insert_entry(p1,p2,nPortIndex);
  drv_ctx.etb_ftb_info.ftb_len = drv_ctx.op_buf.buffer_size;
  sem_post (&(drv_ctx.async_lock));
  DEBUG_PRINT_HIGH("omx_vdpp::fill_this_buffer_proxy 4 nPortIndex = %d, drv_ctx.etb_ftb_info.ftb_cnt = %d, drv_ctx.etb_ftb_info.ftb_len = %d\n",
      nPortIndex, drv_ctx.etb_ftb_info.ftb_cnt, drv_ctx.etb_ftb_info.ftb_len);
}
#endif
  return OMX_ErrorNone;

}

/* ======================================================================
FUNCTION
  omx_vdpp::SetCallbacks

DESCRIPTION
  Set the callbacks.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::set_callbacks(OMX_IN OMX_HANDLETYPE        hComp,
                                           OMX_IN OMX_CALLBACKTYPE* callbacks,
                                           OMX_IN OMX_PTR             appData)
{

  m_cb       = *callbacks;
  DEBUG_PRINT_LOW(" Callbacks Set %p %p %p",m_cb.EmptyBufferDone,\
               m_cb.EventHandler,m_cb.FillBufferDone);
  m_app_data =    appData;
  return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdpp::ComponentDeInit

DESCRIPTION
  Destroys the component and release memory allocated to the heap.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    unsigned i = 0;
    DEBUG_PRINT_HIGH(" omx_vdpp::component_deinit");
    if (OMX_StateLoaded != m_state)
    {
        DEBUG_PRINT_ERROR("WARNING:Rxd DeInit,OMX not in LOADED state %d\n",\
                          m_state);
        DEBUG_PRINT_ERROR("Playback Ended - FAILED");
    }
    else
    {
      DEBUG_PRINT_HIGH(" Playback Ended - PASSED");
    }

    /*Check if the output buffers have to be cleaned up*/
    if(m_out_mem_ptr)
    {
        DEBUG_PRINT_LOW("Freeing the Output Memory\n");
        for (i = 0; i < drv_ctx.op_buf.actualcount; i++ )
        {
          free_output_buffer (&m_out_mem_ptr[i]);
        }
    }

    /*Check if the input buffers have to be cleaned up*/
    if(m_inp_mem_ptr || m_inp_heap_ptr)
    {
        DEBUG_PRINT_LOW("Freeing the Input Memory\n");
        for (i = 0; i<drv_ctx.ip_buf.actualcount; i++ )
        {
          if (m_inp_mem_ptr)
            free_input_buffer (&m_inp_mem_ptr[i]);
        }
    }
    free_input_buffer_header();
    free_output_buffer_header();

    // Reset counters in mesg queues
    m_ftb_q.m_size=0;
    m_cmd_q.m_size=0;
    m_etb_q.m_size=0;
    m_index_q_ftb.m_size=0;
    m_index_q_etb.m_size=0;
    m_ftb_q.m_read = m_ftb_q.m_write =0;
    m_cmd_q.m_read = m_cmd_q.m_write =0;
    m_etb_q.m_read = m_etb_q.m_write =0;
    m_index_q_ftb.m_read = m_index_q_ftb.m_write =0;
    m_index_q_etb.m_read = m_index_q_etb.m_write =0;
#ifdef _ANDROID_
    if (m_debug_timestamp)
    {
      m_timestamp_list.reset_ts_list();
    }
#endif

    DEBUG_PRINT_HIGH(" Close the driver instance");

#ifdef INPUT_BUFFER_LOG
    if (inputBufferFile)
      close (inputBufferFile);
#endif
#ifdef OUTPUT_BUFFER_LOG
   if (outputBufferFile)
     close(outputBufferFile);
#endif
#ifdef OUTPUT_EXTRADATA_LOG
    if (outputExtradataFile)
        fclose (outputExtradataFile);
#endif

  DEBUG_PRINT_HIGH(" omx_vdpp::component_deinit() complete");
  return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdpp::UseEGLImage

DESCRIPTION
  OMX Use EGL Image method implementation <TBD>.

PARAMETERS
  <TBD>.

RETURN VALUE
  Not Implemented error.

========================================================================== */
OMX_ERRORTYPE  omx_vdpp::use_EGL_image(OMX_IN OMX_HANDLETYPE                hComp,
                                          OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                          OMX_IN OMX_U32                        port,
                                          OMX_IN OMX_PTR                     appData,
                                          OMX_IN void*                      eglImage)
{
   return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdpp::ComponentRoleEnum

DESCRIPTION
  OMX Component Role Enum method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdpp::component_role_enum(OMX_IN OMX_HANDLETYPE hComp,
                                                OMX_OUT OMX_U8*        role,
                                                OMX_IN OMX_U32        index)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;

  // no component role (TODO add it later once component role is determined)
/*
  if(!strncmp(drv_ctx.kind, "OMX.qcom.video.vidpp",OMX_MAX_STRINGNAME_SIZE))
  {
    if((0 == index) && role)
    {
      strlcpy((char *)role, "video.vidpp",OMX_MAX_STRINGNAME_SIZE);
      DEBUG_PRINT_LOW("component_role_enum: role %s\n",role);
    }
    else
    {
      eRet = OMX_ErrorNoMore;
    }
  }
*/
  return eRet;
}




/* ======================================================================
FUNCTION
  omx_vdpp::AllocateDone

DESCRIPTION
  Checks if entire buffer pool is allocated by IL Client or not.
  Need this to move to IDLE state.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdpp::allocate_done(void)
{
  bool bRet = false;
  bool bRet_In = false;
  bool bRet_Out = false;

  //DEBUG_PRINT_LOW("omx_vdpp::allocate_done 1\n");
  bRet_In = allocate_input_done();
  bRet_Out = allocate_output_done();

  if(bRet_In && bRet_Out)
  {
      bRet = true;
  //DEBUG_PRINT_LOW("omx_vdpp::allocate_done 2\n");
  }
    //DEBUG_PRINT_LOW("omx_vdpp::allocate_done 3\n");
  return bRet;
}
/* ======================================================================
FUNCTION
  omx_vdpp::AllocateInputDone

DESCRIPTION
  Checks if I/P buffer pool is allocated by IL Client or not.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdpp::allocate_input_done(void)
{
  bool bRet = false;
  unsigned i=0;
  //DEBUG_PRINT_LOW("omx_vdpp::allocate_input_done 1\n");
  if (m_inp_mem_ptr == NULL)
  {
  //DEBUG_PRINT_LOW("omx_vdpp::allocate_input_done 2\n");
      return bRet;
  }
  if(m_inp_mem_ptr )
  {
  //DEBUG_PRINT_LOW("omx_vdpp::allocate_input_done 3\n");
    for(;i<drv_ctx.ip_buf.actualcount;i++)
    {
      if(BITMASK_ABSENT(&m_inp_bm_count,i))
      {
        break;
      }
    }
  }
  //DEBUG_PRINT_LOW("omx_vdpp::allocate_input_done 4 i = %d, drv_ctx.ip_buf.actualcount = %d\n", i, drv_ctx.ip_buf.actualcount);
  if(i == drv_ctx.ip_buf.actualcount)
  {
    //DEBUG_PRINT_LOW("omx_vdpp::allocate_input_done 5\n");
    bRet = true;
    //DEBUG_PRINT_HIGH("Allocate done for all i/p buffers");
  }
  if(i==drv_ctx.ip_buf.actualcount && m_inp_bEnabled)
  {
     m_inp_bPopulated = OMX_TRUE;
  }
  return bRet;
}
/* ======================================================================
FUNCTION
  omx_vdpp::AllocateOutputDone

DESCRIPTION
  Checks if entire O/P buffer pool is allocated by IL Client or not.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdpp::allocate_output_done(void)
{
  bool bRet = false;
  unsigned j=0;

  if (m_out_mem_ptr == NULL)
  {
      return bRet;
  }

  if (m_out_mem_ptr)
  {
    for(;j < drv_ctx.op_buf.actualcount;j++)
    {
      if(BITMASK_ABSENT(&m_out_bm_count,j))
      {
        break;
      }
    }
  }

  if(j == drv_ctx.op_buf.actualcount)
  {
    bRet = true;
    DEBUG_PRINT_HIGH("Allocate done for all o/p buffers");
    if(m_out_bEnabled)
       m_out_bPopulated = OMX_TRUE;
  }

  return bRet;
}

/* ======================================================================
FUNCTION
  omx_vdpp::ReleaseDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdpp::release_done(void)
{
  bool bRet = false;

  if(release_input_done())
  {
    if(release_output_done())
    {
        bRet = true;
    }
  }
  return bRet;
}


/* ======================================================================
FUNCTION
  omx_vdpp::ReleaseOutputDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdpp::release_output_done(void)
{
  bool bRet = false;
  unsigned i=0,j=0;

  DEBUG_PRINT_LOW("omx_vdpp::release_output_done Value of m_out_mem_ptr %p",m_inp_mem_ptr);
  if(m_out_mem_ptr)
  {
      for(;j < drv_ctx.op_buf.actualcount ; j++)
      {
        if(BITMASK_PRESENT(&m_out_bm_count,j))
        {
          break;
        }
      }
    if(j == drv_ctx.op_buf.actualcount)
    {
      m_out_bm_count = 0;
      bRet = true;
    }
  }
  else
  {
    m_out_bm_count = 0;
    bRet = true;
  }
  return bRet;
}
/* ======================================================================
FUNCTION
  omx_vdpp::ReleaseInputDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdpp::release_input_done(void)
{
  bool bRet = false;
  unsigned i=0,j=0;

  DEBUG_PRINT_LOW("omx_vdpp::release_input_done Value of m_inp_mem_ptr %p",m_inp_mem_ptr);
  if(m_inp_mem_ptr)
  {
      for(;j<drv_ctx.ip_buf.actualcount;j++)
      {
        if( BITMASK_PRESENT(&m_inp_bm_count,j))
        {
          break;
        }
      }
    if(j==drv_ctx.ip_buf.actualcount)
    {
      bRet = true;
    }
  }
  else
  {
    bRet = true;
  }
  return bRet;
}

OMX_ERRORTYPE omx_vdpp::fill_buffer_done(OMX_HANDLETYPE hComp,
                               OMX_BUFFERHEADERTYPE * buffer)
{
  OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;
  //DEBUG_PRINT_LOW(" fill_buffer_done 1 : bufhdr = %p, bufhdr->pBuffer = %p, buffer->nFilledLen = %lu",
  //    buffer, buffer->pBuffer, buffer->nFilledLen);

  if (!buffer || (buffer - m_out_mem_ptr) >= drv_ctx.op_buf.actualcount)
  {
    DEBUG_PRINT_ERROR(" [FBD] ERROR in ptr(%p)", buffer);
    return OMX_ErrorBadParameter;
  }
  else if (output_flush_progress)
  {
    DEBUG_PRINT_LOW("FBD: Buffer (%p) flushed", buffer);
    buffer->nFilledLen = 0;
    buffer->nTimeStamp = 0;
    buffer->nFlags &= ~OMX_BUFFERFLAG_EXTRADATA;
    buffer->nFlags &= ~QOMX_VIDEO_BUFFERFLAG_EOSEQ;
    buffer->nFlags &= ~OMX_BUFFERFLAG_DATACORRUPT;
  }

  DEBUG_PRINT_HIGH(" fill_buffer_done: bufhdr = %p, bufhdr->pBuffer = %p, buffer->nFilledLen = %lu, buffer->nFlags = 0x%x",
      buffer, buffer->pBuffer, buffer->nFilledLen, buffer->nFlags);
  pending_output_buffers --;
  output_dqbuf_count++;
  if (buffer->nFlags & OMX_BUFFERFLAG_EOS)
  {
    DEBUG_PRINT_HIGH(" Output EOS has been reached");
    if (!output_flush_progress)
      post_event((unsigned)NULL, (unsigned)NULL,
              OMX_COMPONENT_GENERATE_EOS_DONE);

    if (psource_frame)
    {
      m_cb.EmptyBufferDone(&m_cmp, m_app_data, psource_frame);
      psource_frame = NULL;
    }
    if (pdest_frame)
    {
      pdest_frame->nFilledLen = 0;
      m_input_free_q.insert_entry((unsigned) pdest_frame,(unsigned)NULL,
              (unsigned)NULL);
      pdest_frame = NULL;
    }
  }

  //DEBUG_PRINT_LOW(" In fill Buffer done call address %p , buffer->nFilledLen = %lu",buffer, buffer->nFilledLen);
#ifdef OUTPUT_BUFFER_LOG
  DEBUG_PRINT_LOW("omx_vdpp::fill_buffer_done 1.5, output_buffer_write_counter = %d", output_buffer_write_counter);
    if(outputBufferFile < 0)
    {
	  DEBUG_PRINT_ERROR(" Failed to create outputBufferFile \n");
    }
  if (outputBufferFile && buffer->nFilledLen && (output_buffer_write_counter <= 10))
  {
      DEBUG_PRINT_LOW("omx_vdpp::fill_buffer_done 2");
	  int buf_index = buffer - m_out_mem_ptr;
      int stride = drv_ctx.video_resolution_output.stride; //w
      int scanlines = drv_ctx.video_resolution_output.scan_lines; //h
      char *temp = (char *)drv_ctx.ptr_outputbuffer[buf_index].bufferaddr; // mmap ION buffer addr
	  unsigned i;
	  int bytes_written = 0;
	  for (i = 0; i < drv_ctx.video_resolution_output.frame_height; i++) {
		  bytes_written = write(outputBufferFile, temp, drv_ctx.video_resolution_output.frame_width);
		  temp += stride;
	  }
	  temp = (char *)drv_ctx.ptr_outputbuffer[buf_index].bufferaddr + stride * scanlines;
      int stride_c = stride;
	  for(i = 0; i < drv_ctx.video_resolution_output.frame_height/2; i++) {
		  bytes_written += write(outputBufferFile, temp, drv_ctx.video_resolution_output.frame_width);
		  temp += stride_c;
	  }
      output_buffer_write_counter++;

      if(output_buffer_write_counter > 10)
      {
         close(outputBufferFile);
         DEBUG_PRINT_LOW("omx_vdpp::fill_buffer_done 2.9 close ");
      }
  }
#endif
  //DEBUG_PRINT_LOW("omx_vdpp::fill_buffer_done 3");

  if (m_cb.FillBufferDone)
  {
    //DEBUG_PRINT_LOW("omx_vdpp::fill_buffer_done 4");

    if (buffer->nFlags & OMX_BUFFERFLAG_EOS){
      prev_ts = LLONG_MAX;
      rst_prev_ts = true;
      }

    DEBUG_PRINT_HIGH("omx_vdpp::fill_buffer_done 5 ");
    m_cb.FillBufferDone (hComp,m_app_data, buffer);
    DEBUG_PRINT_HIGH(" After Fill Buffer Done callback");

  }
  else
  {
    return OMX_ErrorBadParameter;
  }

  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdpp::empty_buffer_done(OMX_HANDLETYPE         hComp,
                                          OMX_BUFFERHEADERTYPE* buffer)
{
    if (buffer == NULL || ((buffer - m_inp_mem_ptr) > drv_ctx.ip_buf.actualcount))
    {
        DEBUG_PRINT_ERROR(" empty_buffer_done: ERROR bufhdr = %p", buffer);
       return OMX_ErrorBadParameter;
    }

    DEBUG_PRINT_LOW(" empty_buffer_done: bufhdr = %p, bufhdr->pBuffer = %p",
        buffer, buffer->pBuffer);
    pending_input_buffers--;
    input_dqbuf_count++;

    if(m_cb.EmptyBufferDone)
    {
        buffer->nFilledLen = 0;
        if (input_use_buffer == true){
            buffer = &m_inp_heap_ptr[buffer-m_inp_mem_ptr];
        }
        DEBUG_PRINT_HIGH("!!! empty_buffer_done before callback: buffer = %p\n", buffer);
        m_cb.EmptyBufferDone(hComp ,m_app_data, buffer);
    }
    return OMX_ErrorNone;
}

int omx_vdpp::async_message_process (void *context, void* message)
{
  omx_vdpp* omx = NULL;
  struct vdpp_msginfo *vdpp_msg = NULL;
  OMX_BUFFERHEADERTYPE* omxhdr = NULL;
  struct v4l2_buffer *v4l2_buf_ptr = NULL;
  struct vdpp_output_frameinfo *output_respbuf = NULL;
  int rc=1;
  //DEBUG_PRINT_LOW("async_message_process 0\n");
  if (context == NULL || message == NULL)
  {
    DEBUG_PRINT_ERROR(" FATAL ERROR in omx_vdpp::async_message_process NULL Check");
    return -1;
  }
  //DEBUG_PRINT_LOW("async_message_process 1\n");
  vdpp_msg = (struct vdpp_msginfo *)message;

  omx = reinterpret_cast<omx_vdpp*>(context);

  switch (vdpp_msg->msgcode)
  {

  case VDPP_MSG_EVT_HW_ERROR:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_HARDWARE_ERROR);
  break;

  case VDPP_MSG_RESP_START_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_START_DONE);
  break;

  case VDPP_MSG_RESP_STOP_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_STOP_DONE);
  break;

  case VDPP_MSG_RESP_RESUME_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_RESUME_DONE);
  break;

  case VDPP_MSG_RESP_PAUSE_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_PAUSE_DONE);
  break;

  case VDPP_MSG_RESP_FLUSH_INPUT_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH);
    break;
  case VDPP_MSG_RESP_FLUSH_OUTPUT_DONE:
    omx->post_event ((unsigned)NULL, vdpp_msg->status_code,\
                     OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
    break;
  case VDPP_MSG_RESP_INPUT_FLUSHED:
  case VDPP_MSG_RESP_INPUT_BUFFER_DONE:
    //DEBUG_PRINT_LOW(" VDPP_MSG_RESP_INPUT_BUFFER_DONE 0");
    v4l2_buf_ptr = (v4l2_buffer*)vdpp_msg->msgdata.input_frame_clientdata;
    // Use v4l2_buf_ptr->index returned by VPU V4L2 driver to index into
    // m_inp_mem_ptr. v4l2 driver right now returns the same index used in QBUF
    // In the future the returned ION handle could be used in empty_buffer_done.
    omxhdr=omx->m_inp_mem_ptr+v4l2_buf_ptr->index;
    DEBUG_PRINT_LOW(" VDPP_MSG_RESP_INPUT_BUFFER_DONE 1 v4l2_buf_ptr->index = %d", v4l2_buf_ptr->index);
    if (omxhdr == NULL ||
       ((omxhdr - omx->m_inp_mem_ptr) > omx->drv_ctx.ip_buf.actualcount) )
    {
       //DEBUG_PRINT_ERROR(" VDPP_MSG_RESP_INPUT_BUFFER_DONE 2");
       omxhdr = NULL;
       vdpp_msg->status_code = VDPP_S_EFATAL;
    }
    //DEBUG_PRINT_LOW(" VDPP_MSG_RESP_INPUT_BUFFER_DONE 3");
    // No need to update the omxhdr->nFilledLen using the plane[0].len + plane[1].len here.
    // based on OMX 3.1.2.9.2, nFilledLen = 0 if input buffer is completely consumed in ebd.
    // also refer to ebd code
    omx->post_event ((unsigned int)omxhdr,vdpp_msg->status_code,
                     OMX_COMPONENT_GENERATE_EBD);
    break;
  case VDPP_MSG_RESP_OUTPUT_FLUSHED:
    case VDPP_MSG_RESP_OUTPUT_BUFFER_DONE:

      v4l2_buf_ptr = (v4l2_buffer*)vdpp_msg->msgdata.output_frame.client_data;
      omxhdr=omx->m_out_mem_ptr+v4l2_buf_ptr->index;
      DEBUG_PRINT_LOW("VDPP_MSG_RESP_OUTPUT_BUFFER_DONE 1 v4l2_buf_ptr->index = %d\n", v4l2_buf_ptr->index);

    if (omxhdr && omxhdr->pOutputPortPrivate &&
        ((omxhdr - omx->m_out_mem_ptr) < omx->drv_ctx.op_buf.actualcount) &&
         (((struct vdpp_output_frameinfo *)omxhdr->pOutputPortPrivate
            - omx->drv_ctx.ptr_respbuffer) < omx->drv_ctx.op_buf.actualcount))
    {
      if ( vdpp_msg->msgdata.output_frame.len <=  omxhdr->nAllocLen)
      {
        //DEBUG_PRINT_LOW("VDPP_MSG_RESP_OUTPUT_BUFFER_DONE 2, vdpp_msg->msgdata.output_frame.len = %d, omxhdr->nAllocLen = %ld\n", vdpp_msg->msgdata.output_frame.len, omxhdr->nAllocLen);
	    omxhdr->nFilledLen = vdpp_msg->msgdata.output_frame.len;
	    omxhdr->nOffset = vdpp_msg->msgdata.output_frame.offset;
        omxhdr->nTimeStamp = vdpp_msg->msgdata.output_frame.time_stamp;
        omxhdr->nFlags = omx->m_out_mem_ptr[v4l2_buf_ptr->index].nFlags;

        //DEBUG_PRINT_LOW("VDPP_MSG_RESP_OUTPUT_BUFFER_DONE 2.5 omxhdr->nFilledLen = %ld\n", omxhdr->nFilledLen);
	    if (v4l2_buf_ptr->flags & V4L2_QCOM_BUF_FLAG_EOS)
	    {
	      omxhdr->nFlags |= OMX_BUFFERFLAG_EOS;
	      //rc = -1;
	    }

      // use mmaped ION buffer address
      vdpp_msg->msgdata.output_frame.bufferaddr =
          omx->drv_ctx.ptr_outputbuffer[v4l2_buf_ptr->index].bufferaddr;

        output_respbuf = (struct vdpp_output_frameinfo *)\
                          omxhdr->pOutputPortPrivate;
        output_respbuf->len = vdpp_msg->msgdata.output_frame.len;
        output_respbuf->offset = vdpp_msg->msgdata.output_frame.offset;

        if (omx->output_use_buffer)
          memcpy ( omxhdr->pBuffer, (void *)
                   ((unsigned long)vdpp_msg->msgdata.output_frame.bufferaddr +
                    (unsigned long)vdpp_msg->msgdata.output_frame.offset),
                    vdpp_msg->msgdata.output_frame.len);
      }
      else
        omxhdr->nFilledLen = 0;

      //DEBUG_PRINT_HIGH("VDPP_MSG_RESP_OUTPUT_BUFFER_DONE 4 omxhdr->nFilledLen = %ld, OMX_COMPONENT_GENERATE_FBD = %d\n", omxhdr->nFilledLen, OMX_COMPONENT_GENERATE_FBD);

      omx->post_event ((unsigned int)omxhdr, vdpp_msg->status_code,
                       OMX_COMPONENT_GENERATE_FBD);
    }
    else if (vdpp_msg->msgdata.output_frame.flags & OMX_BUFFERFLAG_EOS)
      omx->post_event ((unsigned int)NULL, vdpp_msg->status_code,
                       OMX_COMPONENT_GENERATE_EOS_DONE);
    else
      omx->post_event ((unsigned int)NULL, vdpp_msg->status_code,
                       OMX_COMPONENT_GENERATE_HARDWARE_ERROR);
    break;
  case VDPP_MSG_EVT_CONFIG_CHANGED:
    DEBUG_PRINT_HIGH(" Port settings changed");
    omx->post_event (OMX_CORE_OUTPUT_PORT_INDEX, OMX_IndexParamPortDefinition,
        OMX_COMPONENT_GENERATE_PORT_RECONFIG);
    break;
  case VDPP_MSG_EVT_ACTIVE_REGION_DETECTION_STATUS:
      {
        struct v4l2_rect * p_ar_result = &(vdpp_msg->msgdata.ar_result);
        DEBUG_PRINT_HIGH(" Active Region Detection Status");
        omx->post_event ((unsigned int)p_ar_result,
                          vdpp_msg->status_code,
                          OMX_COMPONENT_GENERATE_ACTIVE_REGION_DETECTION_STATUS);
        break;
      }
  default:
    break;
  }


  return rc;
}

#ifndef USE_ION
bool omx_vdpp::align_pmem_buffers(int pmem_fd, OMX_U32 buffer_size,
                                  OMX_U32 alignment)
{
  struct pmem_allocation allocation;
  allocation.size = buffer_size;
  allocation.align = clip2(alignment);
  if (allocation.align < 4096)
  {
    allocation.align = 4096;
  }
  if (ioctl(pmem_fd, PMEM_ALLOCATE_ALIGNED, &allocation) < 0)
  {
    DEBUG_PRINT_ERROR(" Aligment(%u) failed with pmem driver Sz(%lu)",
      allocation.align, allocation.size);
    return false;
  }
  return true;
}
#endif
#ifdef USE_ION
int omx_vdpp::alloc_map_ion_memory(OMX_U32 buffer_size,
              OMX_U32 alignment, struct ion_allocation_data *alloc_data,
	      struct ion_fd_data *fd_data, int flag)
{
  int fd = -EINVAL;
  int rc = -EINVAL;
  int ion_dev_flag;
  struct vdpp_ion ion_buf_info;
  if (!alloc_data || buffer_size <= 0 || !fd_data) {
     DEBUG_PRINT_ERROR("Invalid arguments to alloc_map_ion_memory\n");
     return -EINVAL;
  }
  ion_dev_flag = (O_RDONLY | O_DSYNC);
  fd = open (MEM_DEVICE, ion_dev_flag);
  if (fd < 0) {
    DEBUG_PRINT_ERROR("opening ion device failed with fd = %d\n", fd);
    return fd;
  }

  alloc_data->len = buffer_size;

  // the following settings are from vpu_test.c
  alloc_data->align = 16;
  alloc_data->heap_id_mask = ION_HEAP(ION_IOMMU_HEAP_ID);
  alloc_data->flags = 0;

  rc = ioctl(fd,ION_IOC_ALLOC,alloc_data);
  if (rc || !alloc_data->handle) {
    DEBUG_PRINT_ERROR(" ION ALLOC memory failed ");
    alloc_data->handle = NULL;
    close(fd);
    fd = -ENOMEM;
    return fd;
  }
  fd_data->handle = alloc_data->handle;
  rc = ioctl(fd,ION_IOC_MAP,fd_data);
  if (rc) {
    DEBUG_PRINT_ERROR(" ION MAP failed ");
    ion_buf_info.ion_alloc_data = *alloc_data;
    ion_buf_info.ion_device_fd = fd;
    ion_buf_info.fd_ion_data = *fd_data;
    free_ion_memory(&ion_buf_info);
    fd_data->fd =-1;
    close(fd);
    fd = -ENOMEM;
  }

  return fd;
}

void omx_vdpp::free_ion_memory(struct vdpp_ion *buf_ion_info) {

     if(!buf_ion_info) {
       DEBUG_PRINT_ERROR(" ION: free called with invalid fd/allocdata");
       return;
     }
     if(ioctl(buf_ion_info->ion_device_fd,ION_IOC_FREE,
             &buf_ion_info->ion_alloc_data.handle)) {
       DEBUG_PRINT_ERROR(" ION: free failed" );
     }
     close(buf_ion_info->ion_device_fd);
     buf_ion_info->ion_device_fd = -1;
     buf_ion_info->ion_alloc_data.handle = NULL;
     buf_ion_info->fd_ion_data.fd = -1;
}
#endif
void omx_vdpp::free_output_buffer_header()
{
  DEBUG_PRINT_HIGH(" ALL output buffers are freed/released");
  output_use_buffer = false;
  ouput_egl_buffers = false;

  if (m_out_mem_ptr)
  {
    free (m_out_mem_ptr);
    m_out_mem_ptr = NULL;
  }

  if(m_platform_list)
  {
    free(m_platform_list);
    m_platform_list = NULL;
  }

  if (drv_ctx.ptr_respbuffer)
  {
    free (drv_ctx.ptr_respbuffer);
    drv_ctx.ptr_respbuffer = NULL;
  }
  if (drv_ctx.ptr_outputbuffer)
  {
    free (drv_ctx.ptr_outputbuffer);
    drv_ctx.ptr_outputbuffer = NULL;
  }
#ifdef USE_ION
    if (drv_ctx.op_buf_ion_info) {
        DEBUG_PRINT_LOW(" Free o/p ion context");
	free(drv_ctx.op_buf_ion_info);
        drv_ctx.op_buf_ion_info = NULL;
    }
#endif
}

void omx_vdpp::free_input_buffer_header()
{
    input_use_buffer = false;

    if (m_inp_heap_ptr)
    {
      DEBUG_PRINT_LOW(" Free input Heap Pointer");
      free (m_inp_heap_ptr);
      m_inp_heap_ptr = NULL;
    }

    if (m_phdr_pmem_ptr)
    {
      DEBUG_PRINT_LOW(" Free input pmem header Pointer");
      free (m_phdr_pmem_ptr);
      m_phdr_pmem_ptr = NULL;
    }

    if (m_inp_mem_ptr)
    {
      DEBUG_PRINT_LOW(" Free input pmem Pointer area");
      free (m_inp_mem_ptr);
      m_inp_mem_ptr = NULL;
    }

    if (drv_ctx.ptr_inputbuffer)
    {
      DEBUG_PRINT_LOW(" Free Driver Context pointer");
      free (drv_ctx.ptr_inputbuffer);
      drv_ctx.ptr_inputbuffer = NULL;
    }
#ifdef USE_ION
    if (drv_ctx.ip_buf_ion_info) {
        DEBUG_PRINT_LOW(" Free ion context");
	free(drv_ctx.ip_buf_ion_info);
        drv_ctx.ip_buf_ion_info = NULL;
    }
#endif
}

int omx_vdpp::stream_off(OMX_U32 port)
{
	enum v4l2_buf_type btype;
	int rc = 0;
	enum v4l2_ports v4l2_port = OUTPUT_PORT;

	if (port == OMX_CORE_INPUT_PORT_INDEX) {
		btype = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		v4l2_port = OUTPUT_PORT;
	} else if (port == OMX_CORE_OUTPUT_PORT_INDEX) {
		btype = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		v4l2_port = CAPTURE_PORT;
	} else if (port == OMX_ALL) {
		int rc_input = stream_off(OMX_CORE_INPUT_PORT_INDEX);
		int rc_output = stream_off(OMX_CORE_OUTPUT_PORT_INDEX);

		if (!rc_input)
			return rc_input;
		else
			return rc_output;
	}

	if (!streaming[v4l2_port]) {
		// already streamed off, warn and move on
		DEBUG_PRINT_HIGH("Warning: Attempting to stream off on %d port,"
				" which is already streamed off", v4l2_port);
		return 0;
	}

	DEBUG_PRINT_HIGH("Streaming off %d port", v4l2_port);
#ifndef STUB_VPU
	rc = ioctl(drv_ctx.video_vpu_fd, VIDIOC_STREAMOFF, &btype);
	if (rc) {
		     DEBUG_PRINT_ERROR("Failed to call streamoff on %d Port \n", v4l2_port);
	} else {
		streaming[v4l2_port] = false;
	}
#endif

	return rc;
}

// return buffer_prop->actualcount and buffer_prop->buffer_size
// based on ip/op format
#ifdef STUB_VPU
OMX_ERRORTYPE omx_vdpp::get_buffer_req(vdpp_allocatorproperty *buffer_prop)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    struct v4l2_requestbuffers bufreq;
    unsigned int buf_size = 0, extra_data_size = 0, client_extra_data_size = 0;
    struct v4l2_format fmt;

    int ret = 0;

    DEBUG_PRINT_HIGH("omx_vdpp::get_buffer_req GetBufReq IN: ActCnt(%d) Size(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size);

    if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT){

    bufreq.count = VP_INPUT_BUFFER_COUNT_INTERLACE;
    }else if (buffer_prop->buffer_type == VDPP_BUFFER_TYPE_OUTPUT){

    bufreq.count = VP_OUTPUT_BUFFER_COUNT;
    }else
    {
       DEBUG_PRINT_HIGH("omx_vdpp:: wrong buffer type");
    }

    {
        buffer_prop->actualcount = bufreq.count;
        buffer_prop->mincount = bufreq.count;
        DEBUG_PRINT_HIGH("Count = %d \n ",bufreq.count);
    }

    DEBUG_PRINT_LOW("GetBufReq IN: ActCnt(%d) Size(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size);
    {
        buffer_prop->buffer_size = drv_ctx.video_resolution_input.frame_height *
                                   paddedFrameWidth128(drv_ctx.video_resolution_input.frame_width) *
                                   3 / 2; // hardcoded size for stub NV12
    }
    buf_size = buffer_prop->buffer_size;

    buf_size = (buf_size + buffer_prop->alignment - 1)&(~(buffer_prop->alignment - 1));
    DEBUG_PRINT_LOW("GetBufReq UPDATE: ActCnt(%d) Size(%d) BufSize(%d)",
        buffer_prop->actualcount, buffer_prop->buffer_size, buf_size);
    if (in_reconfig) // BufReq will be set to driver when port is disabled
      buffer_prop->buffer_size = buf_size;
    else if (buf_size != buffer_prop->buffer_size)
    {
      buffer_prop->buffer_size = buf_size;
      eRet = set_buffer_req(buffer_prop);
    }
  //}
  DEBUG_PRINT_LOW("GetBufReq OUT: ActCnt(%d) Size(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size);
  return eRet;
}

// set buffer_prop->actualcount through VIDIOC_REQBUFS
OMX_ERRORTYPE omx_vdpp::set_buffer_req(vdpp_allocatorproperty *buffer_prop)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  unsigned buf_size = 0;
  struct v4l2_format fmt;
  struct v4l2_requestbuffers bufreq;
  int ret;
  DEBUG_PRINT_HIGH("omx_vdpp::set_buffer_req SetBufReq IN: ActCnt(%d) Size(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size);
  buf_size = (buffer_prop->buffer_size + buffer_prop->alignment - 1)&(~(buffer_prop->alignment - 1));
  if (buf_size != buffer_prop->buffer_size)
  {
    DEBUG_PRINT_ERROR("Buffer size alignment error: Requested(%d) Required(%d)",
      buffer_prop->buffer_size, buf_size);
    eRet = OMX_ErrorBadParameter;
  }

  return eRet;
}
#else
// call VIDIOC_REQBUFS to set the initial number of buffers that app wants to
// use in streaming
// return buffer_prop->buffer_size and ip/op resolution based on ip/op format
OMX_ERRORTYPE omx_vdpp::get_buffer_req(vdpp_allocatorproperty *buffer_prop)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct v4l2_requestbuffers bufreq;
  unsigned int buf_size = 0, extra_data_size = 0, client_extra_data_size = 0;
  struct v4l2_format fmt;
  int ret = 0;

    memset((void *)&fmt, 0, sizeof(v4l2_format));
    memset((void *)&bufreq, 0, sizeof(v4l2_requestbuffers));
    DEBUG_PRINT_HIGH("GetBufReq IN: ActCnt(%d) Size(%d) buffer_prop->buffer_type (%d), streaming[OUTPUT_PORT] (%d), streaming[CAPTURE_PORT] (%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size, buffer_prop->buffer_type, streaming[OUTPUT_PORT], streaming[CAPTURE_PORT]);
	bufreq.memory = V4L2_MEMORY_USERPTR;
   if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT){
    bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.type =V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.pixelformat = output_capability;
    bufreq.count = VP_INPUT_BUFFER_COUNT_INTERLACE;
  }else if (buffer_prop->buffer_type == VDPP_BUFFER_TYPE_OUTPUT){
    bufreq.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.pixelformat = capture_capability;
    bufreq.count = VP_OUTPUT_BUFFER_COUNT;
  }else {eRet = OMX_ErrorBadParameter;}
  if(eRet==OMX_ErrorNone){
  ret = ioctl(drv_ctx.video_vpu_fd,VIDIOC_REQBUFS, &bufreq);
  }
  if(ret)
  {
    DEBUG_PRINT_ERROR("GetBufReq Requesting buffer requirements failed 1");
    eRet = OMX_ErrorInsufficientResources;
    return eRet;
  }
  else
  {
    buffer_prop->actualcount = bufreq.count;
    buffer_prop->mincount = bufreq.count;
    DEBUG_PRINT_HIGH("Count = %d \n ",bufreq.count);
  }
  DEBUG_PRINT_LOW("GetBufReq IN: ActCnt(%d) Size(%d), buffer_prop->buffer_type(%d) fmt.fmt.pix_mp.pixelformat (0x%08x)",
    buffer_prop->actualcount, buffer_prop->buffer_size, buffer_prop->buffer_type, fmt.fmt.pix_mp.pixelformat);

  if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT)
  {
      fmt.fmt.pix_mp.height = drv_ctx.video_resolution_input.frame_height;
      fmt.fmt.pix_mp.width = drv_ctx.video_resolution_input.frame_width;
      if (V4L2_FIELD_NONE == drv_ctx.interlace)
      {
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
      }
      else
      {
        fmt.fmt.pix_mp.field = V4L2_FIELD_INTERLACED;
      }

  }
  else
  {
      fmt.fmt.pix_mp.height = drv_ctx.video_resolution_output.frame_height;
      fmt.fmt.pix_mp.width = drv_ctx.video_resolution_output.frame_width;
      fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
  }

  //ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_G_FMT, &fmt);
  // S_FMT is always called before get_buffer_req
  // we should be able to use G_FMT to get fmt info.
  ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_TRY_FMT, &fmt);
  //ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_G_FMT, &fmt);

  if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT)
  {
    drv_ctx.input_num_planes = fmt.fmt.pix_mp.num_planes;
    drv_ctx.video_resolution_input.frame_height = fmt.fmt.pix_mp.height;
    drv_ctx.video_resolution_input.frame_width = fmt.fmt.pix_mp.width;
    DEBUG_PRINT_HIGH("GetBufReq drv_ctx.video_resolution_input.frame_height = %d, drv_ctx.video_resolution_input.frame_width = %d ",
        drv_ctx.video_resolution_input.frame_height,  drv_ctx.video_resolution_input.frame_width);
  }
  else
  {
    drv_ctx.output_num_planes = fmt.fmt.pix_mp.num_planes;
    drv_ctx.video_resolution_output.frame_height = fmt.fmt.pix_mp.height;
    drv_ctx.video_resolution_output.frame_width = fmt.fmt.pix_mp.width;
    DEBUG_PRINT_HIGH("GetBufReq drv_ctx.video_resolution_output.frame_height = %d, drv_ctx.video_resolution_output.frame_width = %d ",
        drv_ctx.video_resolution_output.frame_height,  drv_ctx.video_resolution_output.frame_width);
  }

  buffer_prop->frame_size = paddedFrameWidth32(fmt.fmt.pix_mp.height) *
                                   paddedFrameWidth128(fmt.fmt.pix_mp.width) *
                                   3 / 2;
  DEBUG_PRINT_HIGH("GetBufReq fmt.fmt.pix_mp.num_planes = %d, fmt.fmt.pix_mp.height = %d, fmt.fmt.pix_mp.width = %d, buffer_prop->frame_size = %d \n",
      fmt.fmt.pix_mp.num_planes, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.width, buffer_prop->frame_size);


  if(ret)
  {
    DEBUG_PRINT_ERROR("GetBufReq Requesting buffer requirements failed 2");
    eRet = OMX_ErrorInsufficientResources;
  }
  else
  {
    int extra_idx = 0;
    buffer_prop->buffer_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    buf_size = buffer_prop->buffer_size;
    if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT)
    {
        extra_idx = EXTRADATA_IDX(drv_ctx.input_num_planes);
    }
    else
    {
        extra_idx = EXTRADATA_IDX(drv_ctx.output_num_planes);
    }

    if ((extra_idx > 0) && (extra_idx < VIDEO_MAX_PLANES)) {
      extra_data_size =  fmt.fmt.pix_mp.plane_fmt[extra_idx].sizeimage;
      DEBUG_PRINT_HIGH("omx_vdpp::get_buffer_req extra_data_size: %d\n", extra_data_size);
    } else if (extra_idx >= VIDEO_MAX_PLANES) {
      DEBUG_PRINT_ERROR("Extradata index is more than allowed: %d\n", extra_idx);
      return OMX_ErrorBadParameter;
    }
    if (client_extradata & OMX_FRAMEINFO_EXTRADATA)
    {
      DEBUG_PRINT_HIGH("Frame info extra data enabled!");
      client_extra_data_size += OMX_FRAMEINFO_EXTRADATA_SIZE;
    }
    if (client_extradata & OMX_INTERLACE_EXTRADATA)
    {
      DEBUG_PRINT_HIGH("Interlace extra data enabled!");
      client_extra_data_size += OMX_INTERLACE_EXTRADATA_SIZE;
    }
    if (client_extradata & OMX_PORTDEF_EXTRADATA)
    {
      client_extra_data_size += OMX_PORTDEF_EXTRADATA_SIZE;
      DEBUG_PRINT_HIGH("Smooth streaming enabled extra_data_size=%d\n",
         client_extra_data_size);
    }
    if (client_extra_data_size)
    {
      client_extra_data_size += sizeof(OMX_OTHER_EXTRADATATYPE); //Space for terminator
      buf_size = ((buf_size + 3)&(~3)); //Align extradata start address to 64Bit
    }
    // update buffer_prop->buffer_size to include plane 1 buffer size
    // so only 1 ION buffer will be allocated for each input/output buffer
    if (extra_data_size > 0)
    {
        buf_size += extra_data_size;
    }

    drv_ctx.extradata_info.size = buffer_prop->actualcount * extra_data_size;
    drv_ctx.extradata_info.count = buffer_prop->actualcount;
    drv_ctx.extradata_info.buffer_size = extra_data_size;
    buf_size += client_extra_data_size; // client_extra_data_size defaults to 0
    buf_size = (buf_size + buffer_prop->alignment - 1)&(~(buffer_prop->alignment - 1));
    DEBUG_PRINT_LOW("GetBufReq UPDATE: ActCnt(%d) Size(%d) BufSize(%d)",
        buffer_prop->actualcount, buffer_prop->buffer_size, buf_size);
    if (in_reconfig) // BufReq will be set to driver when port is disabled
      buffer_prop->buffer_size = buf_size;
    else if (buf_size != buffer_prop->buffer_size)
    {
      buffer_prop->buffer_size = buf_size;
      eRet = set_buffer_req(buffer_prop);
    }
  }
  DEBUG_PRINT_LOW("GetBufReq OUT: ActCnt(%d) Size(%d) buffer_prop->buffer_type(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size, buffer_prop->buffer_type);
  return eRet;
}

// set buffer_prop->actualcount through VIDIOC_REQBUFS
OMX_ERRORTYPE omx_vdpp::set_buffer_req(vdpp_allocatorproperty *buffer_prop)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  unsigned buf_size = 0;
  unsigned i = 0;
  struct v4l2_format fmt;
  struct v4l2_requestbuffers bufreq;
  int ret;
  DEBUG_PRINT_LOW("SetBufReq IN: ActCnt(%d) Size(%d)",
    buffer_prop->actualcount, buffer_prop->buffer_size);
  memset((void *)&fmt, 0, sizeof(v4l2_format));
  memset((void *)&bufreq, 0, sizeof(v4l2_requestbuffers));
  buf_size = (buffer_prop->buffer_size + buffer_prop->alignment - 1)&(~(buffer_prop->alignment - 1));
  if (buf_size != buffer_prop->buffer_size)
  {
    DEBUG_PRINT_ERROR("Buffer size alignment error: Requested(%d) Required(%d)",
      buffer_prop->buffer_size, buf_size);
    eRet = OMX_ErrorBadParameter;
  }
  else
  {

	if (buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT){
		fmt.type =V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fmt.fmt.pix_mp.pixelformat = output_capability;

        if (V4L2_FIELD_NONE == drv_ctx.interlace)
        {
          fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        }
        else
        {
          fmt.fmt.pix_mp.field = V4L2_FIELD_INTERLACED;
        }
        fmt.fmt.pix_mp.height = drv_ctx.video_resolution_input.frame_height;
	    fmt.fmt.pix_mp.width = drv_ctx.video_resolution_input.frame_width;

        setFormatParams(output_capability, drv_ctx.input_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
        for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
        {
            fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width *
                                                                     drv_ctx.input_bytesperpixel[i] *
                                                                     fmt.fmt.pix_mp.height);
            fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.input_bytesperpixel[0]); // both plane have the same plane stride
        }
	} else if (buffer_prop->buffer_type == VDPP_BUFFER_TYPE_OUTPUT) {
		fmt.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.pixelformat = capture_capability;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.height = drv_ctx.video_resolution_output.frame_height;
	    fmt.fmt.pix_mp.width = drv_ctx.video_resolution_output.frame_width;

        setFormatParams(capture_capability, drv_ctx.output_bytesperpixel, &(fmt.fmt.pix_mp.num_planes));
        for( i=0; i<fmt.fmt.pix_mp.num_planes; i++ )
        {
            fmt.fmt.pix_mp.plane_fmt[i].sizeimage = paddedFrameWidth128(fmt.fmt.pix_mp.width *
                                                                     drv_ctx.output_bytesperpixel[i] *
                                                                     fmt.fmt.pix_mp.height);
            fmt.fmt.pix_mp.plane_fmt[i].bytesperline = paddedFrameWidth128(fmt.fmt.pix_mp.width * drv_ctx.output_bytesperpixel[0]);
            DEBUG_PRINT_HIGH("set_buffer_req fmt.fmt.pix_mp.plane_fmt[%d].sizeimage = %d \n ", i, fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
        }
	} else {eRet = OMX_ErrorBadParameter;}

	ret = ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_FMT, &fmt);
    if (ret)
    {
      DEBUG_PRINT_ERROR("Setting buffer requirements (format) failed %d", ret);
      eRet = OMX_ErrorInsufficientResources;
    }
    else
    {
         DEBUG_PRINT_HIGH("set_buffer_req drv_ctx.interlace = %d", drv_ctx.interlace);
    }

	bufreq.memory = V4L2_MEMORY_USERPTR;
	bufreq.count = buffer_prop->actualcount;
	if(buffer_prop->buffer_type == VDPP_BUFFER_TYPE_INPUT) {
		bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else if (buffer_prop->buffer_type == VDPP_BUFFER_TYPE_OUTPUT) {
		bufreq.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	} else {eRet = OMX_ErrorBadParameter;}

	if (eRet==OMX_ErrorNone) {
		ret = ioctl(drv_ctx.video_vpu_fd,VIDIOC_REQBUFS, &bufreq);
	}

	if (ret)
	{
		DEBUG_PRINT_ERROR("Setting buffer requirements (reqbufs) failed %d", ret);
		eRet = OMX_ErrorInsufficientResources;
	} else if (bufreq.count < buffer_prop->actualcount) {
		DEBUG_PRINT_ERROR("Driver refused to change the number of buffers"
						" on v4l2 port %d to %d (prefers %d)", bufreq.type,
						buffer_prop->actualcount, bufreq.count);
		eRet = OMX_ErrorInsufficientResources;
	}

  }
  return eRet;
}
#endif

OMX_ERRORTYPE omx_vdpp::update_portdef(OMX_PARAM_PORTDEFINITIONTYPE *portDefn)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  if (!portDefn)
  {
    return OMX_ErrorBadParameter;
  }
  DEBUG_PRINT_LOW("omx_vdpp::update_portdef\n");
  portDefn->nVersion.nVersion = OMX_SPEC_VERSION;
  portDefn->nSize = sizeof(portDefn);
  portDefn->eDomain    = OMX_PortDomainVideo;
  if (drv_ctx.frame_rate.fps_denominator > 0)
    portDefn->format.video.xFramerate = drv_ctx.frame_rate.fps_numerator /
                                        drv_ctx.frame_rate.fps_denominator;
  else {
    DEBUG_PRINT_ERROR("Error: Divide by zero \n");
    return OMX_ErrorBadParameter;
  }
  if (OMX_CORE_INPUT_PORT_INDEX == portDefn->nPortIndex)
  {
    portDefn->eDir =  OMX_DirInput;
    portDefn->nBufferCountActual = drv_ctx.ip_buf.actualcount;
    portDefn->nBufferCountMin    = drv_ctx.ip_buf.mincount;
    portDefn->nBufferSize        = drv_ctx.ip_buf.buffer_size;
    portDefn->format.video.eColorFormat = (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;//OMX_COLOR_FormatYUV420Planar;//OMX_COLOR_FormatUnused;
    portDefn->format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    portDefn->bEnabled   = m_inp_bEnabled;
    portDefn->bPopulated = m_inp_bPopulated;
    portDefn->format.video.nFrameHeight =  drv_ctx.video_resolution_input.frame_height;
    portDefn->format.video.nFrameWidth  =  drv_ctx.video_resolution_input.frame_width;
    portDefn->format.video.nStride = drv_ctx.video_resolution_input.stride;
    portDefn->format.video.nSliceHeight = drv_ctx.video_resolution_input.scan_lines;
  }
  else if (OMX_CORE_OUTPUT_PORT_INDEX == portDefn->nPortIndex)
  {
    portDefn->eDir =  OMX_DirOutput;
	portDefn->nBufferCountActual = drv_ctx.op_buf.actualcount;
	portDefn->nBufferCountMin    = drv_ctx.op_buf.mincount;
    portDefn->nBufferSize = drv_ctx.op_buf.buffer_size;
    portDefn->format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    portDefn->bEnabled   = m_out_bEnabled;
    portDefn->bPopulated = m_out_bPopulated;
    portDefn->format.video.eColorFormat = (OMX_COLOR_FORMATTYPE) QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;

    // video_resolution_output.frame_height is retrieved from get_bufreq
    portDefn->format.video.nFrameHeight =  drv_ctx.video_resolution_output.frame_height;
    portDefn->format.video.nFrameWidth  =  drv_ctx.video_resolution_output.frame_width;
    portDefn->format.video.nStride = drv_ctx.video_resolution_output.stride;
    portDefn->format.video.nSliceHeight = drv_ctx.video_resolution_output.scan_lines;
  }
  else
  {
      portDefn->eDir = OMX_DirMax;
    DEBUG_PRINT_LOW(" get_parameter: Bad Port idx %d",
             (int)portDefn->nPortIndex);
    eRet = OMX_ErrorBadPortIndex;
  }

  DEBUG_PRINT_HIGH("update_portdef for %lu Width = %lu Height = %lu Stride = %ld SliceHeight = %lu portDefn->format.video.eColorFormat = %d \n", portDefn->nPortIndex,
    portDefn->format.video.nFrameWidth,
    portDefn->format.video.nFrameHeight,
    portDefn->format.video.nStride,
    portDefn->format.video.nSliceHeight,
    portDefn->format.video.eColorFormat);
  return eRet;

}

OMX_ERRORTYPE omx_vdpp::allocate_output_headers()
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE *bufHdr = NULL;
  unsigned i= 0;

  if(!m_out_mem_ptr) {
    DEBUG_PRINT_HIGH(" Use o/p buffer case - Header List allocation");
    int nBufHdrSize        = 0;
    int nPlatformEntrySize = 0;
    int nPlatformListSize  = 0;
    int nPMEMInfoSize = 0;
    OMX_QCOM_PLATFORM_PRIVATE_LIST      *pPlatformList;
    OMX_QCOM_PLATFORM_PRIVATE_ENTRY     *pPlatformEntry;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

    DEBUG_PRINT_LOW("Setting First Output Buffer(%d)\n",
      drv_ctx.op_buf.actualcount);
    nBufHdrSize        = drv_ctx.op_buf.actualcount *
                         sizeof(OMX_BUFFERHEADERTYPE);

    DEBUG_PRINT_LOW("TotalBufHdr %d BufHdrSize %d \n",nBufHdrSize,
                         sizeof(OMX_BUFFERHEADERTYPE));

    m_out_mem_ptr = (OMX_BUFFERHEADERTYPE  *)calloc(nBufHdrSize,1);

    drv_ctx.ptr_outputbuffer = (struct vdpp_bufferpayload *) \
      calloc (sizeof(struct vdpp_bufferpayload),
      drv_ctx.op_buf.actualcount);
    drv_ctx.ptr_respbuffer = (struct vdpp_output_frameinfo  *)\
      calloc (sizeof (struct vdpp_output_frameinfo),
      drv_ctx.op_buf.actualcount);
#ifdef USE_ION
    drv_ctx.op_buf_ion_info = (struct vdpp_ion * ) \
      calloc (sizeof(struct vdpp_ion),drv_ctx.op_buf.actualcount);

      if (!drv_ctx.op_buf_ion_info) {
          DEBUG_PRINT_ERROR("Failed to alloc drv_ctx.op_buf_ion_info");
          return OMX_ErrorInsufficientResources;
      }
#endif

    if(m_out_mem_ptr && drv_ctx.ptr_outputbuffer
       && drv_ctx.ptr_respbuffer)
    {
      bufHdr          =  m_out_mem_ptr;
      DEBUG_PRINT_LOW("Memory Allocation Succeeded for OUT port%p\n",m_out_mem_ptr);

      for(i=0; i < drv_ctx.op_buf.actualcount ; i++)
      {
        bufHdr->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        bufHdr->nVersion.nVersion  = OMX_SPEC_VERSION;
        // Set the values when we determine the right HxW param
        bufHdr->nAllocLen          = 0;
        bufHdr->nFilledLen         = 0;
        bufHdr->pAppPrivate        = NULL;
        bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
        bufHdr->pBuffer            = NULL; // since no buffer is allocated

        drv_ctx.ptr_outputbuffer[i].pmem_fd = -1;
#ifdef USE_ION
        drv_ctx.op_buf_ion_info[i].ion_device_fd =-1;
#endif
        /*Create a mapping between buffers*/
        bufHdr->pOutputPortPrivate = &drv_ctx.ptr_respbuffer[i];
        drv_ctx.ptr_respbuffer[i].client_data = (void *) \
                                            &drv_ctx.ptr_outputbuffer[i];
        // Move the buffer and buffer header pointers
        bufHdr++;
      }
    }
    else
    {
      DEBUG_PRINT_ERROR("Output buf mem alloc failed[0x%p]\n",\
                                        m_out_mem_ptr);
      if(m_out_mem_ptr)
      {
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
      }

      if(drv_ctx.ptr_outputbuffer)
      {
        free(drv_ctx.ptr_outputbuffer);
        drv_ctx.ptr_outputbuffer = NULL;
      }
      if(drv_ctx.ptr_respbuffer)
      {
        free(drv_ctx.ptr_respbuffer);
        drv_ctx.ptr_respbuffer = NULL;
      }
#ifdef USE_ION
    if (drv_ctx.op_buf_ion_info) {
        DEBUG_PRINT_LOW(" Free o/p ion context");
	free(drv_ctx.op_buf_ion_info);
        drv_ctx.op_buf_ion_info = NULL;
    }
#endif
      eRet =  OMX_ErrorInsufficientResources;
    }
  } else {
    eRet =  OMX_ErrorInsufficientResources;
  }
  return eRet;
}

void omx_vdpp::complete_pending_buffer_done_cbs()
{
  unsigned p1;
  unsigned p2;
  unsigned ident;
  omx_cmd_queue tmp_q, pending_bd_q;
  pthread_mutex_lock(&m_lock);
  // pop all pending GENERATE FDB from ftb queue
  while (m_ftb_q.m_size)
  {
    m_ftb_q.pop_entry(&p1,&p2,&ident);
    if(ident == OMX_COMPONENT_GENERATE_FBD)
    {
      pending_bd_q.insert_entry(p1,p2,ident);
    }
    else
    {
      tmp_q.insert_entry(p1,p2,ident);
    }
  }
  //return all non GENERATE FDB to ftb queue
  while(tmp_q.m_size)
  {
    tmp_q.pop_entry(&p1,&p2,&ident);
    m_ftb_q.insert_entry(p1,p2,ident);
  }
  // pop all pending GENERATE EDB from etb queue
  while (m_etb_q.m_size)
  {
    m_etb_q.pop_entry(&p1,&p2,&ident);
    if(ident == OMX_COMPONENT_GENERATE_EBD)
    {
      pending_bd_q.insert_entry(p1,p2,ident);
    }
    else
    {
      tmp_q.insert_entry(p1,p2,ident);
    }
  }
  //return all non GENERATE FDB to etb queue
  while(tmp_q.m_size)
  {
    tmp_q.pop_entry(&p1,&p2,&ident);
    m_etb_q.insert_entry(p1,p2,ident);
  }
  pthread_mutex_unlock(&m_lock);
  // process all pending buffer dones
  while(pending_bd_q.m_size)
  {
    pending_bd_q.pop_entry(&p1,&p2,&ident);
    switch(ident)
    {
      case OMX_COMPONENT_GENERATE_EBD:
        if(empty_buffer_done(&m_cmp, (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone)
        {
          DEBUG_PRINT_ERROR("ERROR: empty_buffer_done() failed!\n");
          omx_report_error ();
        }
        break;

      case OMX_COMPONENT_GENERATE_FBD:
        if(fill_buffer_done(&m_cmp, (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone )
        {
          DEBUG_PRINT_ERROR("ERROR: fill_buffer_done() failed!\n");
          omx_report_error ();
        }
        break;
    }
  }
}

void omx_vdpp::set_frame_rate(OMX_S64 act_timestamp)
{
// No framerate control on 8084 VPU. This API is for 8092.
#ifdef FRC_ENABLE
  OMX_U32 new_frame_interval = 0;
  if (VALID_TS(act_timestamp) && VALID_TS(prev_ts) && act_timestamp != prev_ts
     && (((act_timestamp > prev_ts )? act_timestamp - prev_ts: prev_ts-act_timestamp)>2000))
  {
    new_frame_interval = (act_timestamp > prev_ts)?
                          act_timestamp - prev_ts :
                          prev_ts - act_timestamp;
    if (new_frame_interval < frm_int || frm_int == 0)
    {
      frm_int = new_frame_interval;
      if(frm_int)
      {
        drv_ctx.frame_rate.fps_numerator = 1e6;
        drv_ctx.frame_rate.fps_denominator = frm_int;
        DEBUG_PRINT_LOW("set_frame_rate: frm_int(%lu) fps(%f)",
                         frm_int, drv_ctx.frame_rate.fps_numerator /
                         (float)drv_ctx.frame_rate.fps_denominator);

        /* We need to report the difference between this FBD and the previous FBD
         * back to the driver for clock scaling purposes. */
        struct v4l2_outputparm oparm;
        /*XXX: we're providing timing info as seconds per frame rather than frames
         * per second.*/
        oparm.timeperframe.numerator = drv_ctx.frame_rate.fps_denominator;
        oparm.timeperframe.denominator = drv_ctx.frame_rate.fps_numerator;

        struct v4l2_streamparm sparm;
        sparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        sparm.parm.output = oparm;
        if (ioctl(drv_ctx.video_vpu_fd, VIDIOC_S_PARM, &sparm))
        {
            DEBUG_PRINT_ERROR("Unable to convey fps info to driver, \
                    performance might be affected");
        }

      }
    }
  }
  prev_ts = act_timestamp;
#endif
}

void omx_vdpp::adjust_timestamp(OMX_S64 &act_timestamp)
{
  if (rst_prev_ts && VALID_TS(act_timestamp))
  {
    prev_ts = act_timestamp;
    rst_prev_ts = false;
  }
  else if (VALID_TS(prev_ts))
  {
    bool codec_cond = (drv_ctx.timestamp_adjust)?
                      (!VALID_TS(act_timestamp) || (((act_timestamp > prev_ts)?
                      (act_timestamp - prev_ts):(prev_ts - act_timestamp)) <= 2000)):
                      (!VALID_TS(act_timestamp) || act_timestamp == prev_ts);
    if(frm_int > 0 && codec_cond)
    {
      DEBUG_PRINT_LOW("adjust_timestamp: original ts[%lld]", act_timestamp);
      act_timestamp = prev_ts + frm_int;
      DEBUG_PRINT_LOW("adjust_timestamp: predicted ts[%lld]", act_timestamp);
      prev_ts = act_timestamp;
    }
    else
      set_frame_rate(act_timestamp);
  }
  else if (frm_int > 0)           // In this case the frame rate was set along
  {                               // with the port definition, start ts with 0
    act_timestamp = prev_ts = 0;  // and correct if a valid ts is received.
    rst_prev_ts = true;
  }
}

OMX_ERRORTYPE omx_vdpp::enable_extradata(OMX_U32 requested_extradata, bool enable)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  if(m_state != OMX_StateLoaded)
  {
     DEBUG_PRINT_ERROR("ERROR: enable extradata allowed in Loaded state only");
     return OMX_ErrorIncorrectStateOperation;
  }
  DEBUG_PRINT_ERROR("enable_extradata: actual[%lx] requested[%lx] enable[%d]",
    client_extradata, requested_extradata, enable);

  if (enable)
    client_extradata |= requested_extradata;
  else
    client_extradata = client_extradata & ~requested_extradata;

  return ret;
}

void omx_vdpp::setFormatParams(int pixelFormat, double bytesperpixel[], unsigned char *planesCount)
{
	/*24 RGB-8-8-8 */
    switch (pixelFormat)
    {
        case V4L2_PIX_FMT_RGB24:
            *planesCount = 0;
		    bytesperpixel[0] = 3;
            break;

            /*32 ARGB-8-8-8-8 */
        case V4L2_PIX_FMT_RGB32:
            *planesCount = 0;
		    bytesperpixel[0] = 4;
            break;

	        /*32 ARGB-2-10-10-10*/
        case V4L2_PIX_FMT_XRGB2:
            *planesCount = 0;
		    bytesperpixel[0] = 4;
            break;

	        /*24  BGR-8-8-8 */
        case V4L2_PIX_FMT_BGR24:
            *planesCount = 0;
		    bytesperpixel[0] = 3;
            break;

	        /*32  BGRX-8-8-8-8 */
        case V4L2_PIX_FMT_BGR32:
            *planesCount = 0;
		    bytesperpixel[0] = 4;
            break;

	    /*32  XBGR-2-10-10-10*/
        case V4L2_PIX_FMT_XBGR2:
            *planesCount = 0;
		    bytesperpixel[0] = 4;
            break;

	    /*12  YUV 4:2:0  semi-planar*/
        case V4L2_PIX_FMT_NV12:
            *planesCount = 2;
		    bytesperpixel[0] = 1;
		    bytesperpixel[1] = 0.5;
            break;

	    /*12  YVU 4:2:0  semi-planar*/
        case V4L2_PIX_FMT_NV21:
            *planesCount = 2;
		    bytesperpixel[0] = 1;
		    bytesperpixel[1] = 0.5;
            break;

	    /* 16 YUYV 4:2:2 interleaved*/
        case V4L2_PIX_FMT_YUYV:
            *planesCount = 2;
		    bytesperpixel[0] = 2;
		    bytesperpixel[1] = 0.5;
            break;

	    /* 16 YVYU 4:2:2 interleaved*/
        case V4L2_PIX_FMT_YVYU:
            *planesCount = 1;
		    bytesperpixel[0] = 2;
            break;

	    /* 16 VYUY 4:2:2 interleaved*/
        case V4L2_PIX_FMT_VYUY:
            *planesCount = 1;
		    bytesperpixel[0] = 2;
            break;

	    /* 16 UYVY 4:2:2 interleaved*/
        case V4L2_PIX_FMT_UYVY:
            *planesCount = 1;
		    bytesperpixel[0] = 2;
            break;

        default:
		    DEBUG_PRINT_ERROR("%s: ERROR: pixel format %d is not supported!\n",
				__func__, pixelFormat);
    }

}

int omx_vdpp::openInput(const char* inputName)
{
    int fd = -1;
    const char *dirname = "/sys/class/video4linux";
    char devname[PATH_MAX];
    char dev[PATH_MAX];
    char *filename;
    DIR *dir;
    struct dirent *de;
    dir = opendir(dirname);
    if(dir == NULL)
        return -1;
    strlcpy(dev, dirname, sizeof(dev));
    filename = dev + strlen(dev);
    *filename++ = '/';
    while((de = readdir(dir)))
    {
        if(de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        strlcpy(filename, de->d_name, PATH_MAX - sizeof(dev) - 1);
        /*DEBUG_PRINT_LOW("Filename found: %s\n", filename);*/
        char name[80];
        int fd_devname;
        int result;
        strlcpy(devname, dev, sizeof(devname));
        strlcat(devname, "/name", sizeof(devname));
        /*DEBUG_PRINT_LOW("opening name file: %s\n", devname);*/
        /* find and read device node names from sys/conf/video4linux dir*/
        fd_devname = open(devname,O_RDONLY);
        if(fd_devname < 0)
            DEBUG_PRINT_ERROR("openInput: could not find device name.\n");
        result = read(fd_devname, name, strlen(inputName));
        if(result < 0)
        {
            DEBUG_PRINT_ERROR("openInput: could not read device name.\n");
        }
        else
        {
            if(!strcmp(name, inputName))
            {
                close(fd_devname);
                /* open the vpu driver node found from /dev dir */
                char temp[80];
                strlcpy(temp, "/dev/", sizeof(temp));
                strlcat(temp, filename, sizeof(temp));
                DEBUG_PRINT_LOW("openInput: opening device: %s\n", temp);
                fd = open(temp, O_RDWR | O_NONBLOCK);
                if(fd < 0)
                    DEBUG_PRINT_ERROR("openInput: Error opening device %s\n", temp);
                else
                    break;
            }
        }
        close(fd_devname);
    }
    closedir(dir);
    ALOGE_IF(fd<0, "couldn't find '%s' input device", inputName);
    return fd;
}
