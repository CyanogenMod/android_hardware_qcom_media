/*--------------------------------------------------------------------------
Copyright (c) 2010, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora nor
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

*//** @file omx_vdec.cpp
  This module contains the implementation of the OpenMAX core & component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "omx_vdec.h"
#include <fcntl.h>

#define BITSTREAM_LOG 0

#if BITSTREAM_LOG
FILE *outputBufferFile1;
char filename [] = "/data/input-bitstream.m4v";
#endif

#define H264_SUPPORTED_WIDTH (480)
#define H264_SUPPORTED_HEIGHT (368)

#define MPEG4_SUPPORTED_WIDTH (480)
#define MPEG4_SUPPORTED_HEIGHT (368)

#define VC1_SP_MP_START_CODE        0xC5000000
#define VC1_SP_MP_START_CODE_MASK   0xFF000000
#define VC1_AP_SEQ_START_CODE       0x0F010000
#define VC1_STRUCT_C_PROFILE_MASK   0xF0
#define VC1_STRUCT_B_LEVEL_MASK     0xE0000000
#define VC1_SIMPLE_PROFILE          0
#define VC1_MAIN_PROFILE            1
#define VC1_ADVANCE_PROFILE         3
#define VC1_SIMPLE_PROFILE_LOW_LEVEL  0
#define VC1_SIMPLE_PROFILE_MED_LEVEL  2
#define VC1_STRUCT_C_LEN            4
#define VC1_STRUCT_C_POS            8
#define VC1_STRUCT_A_POS            12
#define VC1_STRUCT_B_POS            24
#define VC1_SEQ_LAYER_SIZE          36

#ifdef _ANDROID_
    extern "C"{
        #include<utils/Log.h>
    }
#endif//_ANDROID_

#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#define DEBUG_PRINT_ERROR(...) printf(__VA_ARGS__)
#define DEBUG_PRINT_LOW(...) printf(__VA_ARGS__)


void* async_message_thread (void *input)
{
  struct vdec_ioctl_msg ioctl_msg;
  struct vdec_msginfo vdec_msg;
  omx_vdec *omx = reinterpret_cast<omx_vdec*>(input);

  DEBUG_PRINT_HIGH("omx_vdec: Async thread start\n");
  while (1)
  {
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = (void*)&vdec_msg;

    /*Wait for a message from the video decoder driver*/
    if (ioctl ( omx->driver_context.video_driver_fd,VDEC_IOCTL_GET_NEXT_MSG,
                (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Error in ioctl read next msg");
      break;
    }
    else
    {
      /*Call Instance specific process function*/
      if (omx->async_message_process(input,&vdec_msg) < 0)
      {
        DEBUG_PRINT_ERROR("\nERROR:Wrong ioctl message");
      }
    }
  }
  DEBUG_PRINT_HIGH("omx_vdec: Async thread stop\n");
  return NULL;
}

void* message_thread(void *input)
{
  omx_vdec* omx = reinterpret_cast<omx_vdec*>(input);
  unsigned char id;
  int n;

  DEBUG_PRINT_HIGH("omx_vdec: message thread start\n");
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
      DEBUG_PRINT_ERROR("\nERROR: read from pipe failed, ret %d errno %d", n, errno);
      break;
    }
  }
  DEBUG_PRINT_HIGH("omx_vdec: message thread stop\n");
  return 0;
}

void post_message(omx_vdec *omx, unsigned char id)
{
      int ret_value;
      DEBUG_PRINT_LOW("omx_vdec: post_message %d pipe out%d\n", id,omx->m_pipe_out);
      ret_value = write(omx->m_pipe_out, &id, 1);
      DEBUG_PRINT_LOW("post_message to pipe done %d\n",ret_value);
}

// omx_cmd_queue destructor
omx_vdec::omx_cmd_queue::~omx_cmd_queue()
{
  // Nothing to do
}

// omx cmd queue constructor
omx_vdec::omx_cmd_queue::omx_cmd_queue(): m_read(0),m_write(0),m_size(0)
{
    memset(m_q,0,sizeof(omx_event)*OMX_CORE_CONTROL_CMDQ_SIZE);
}

// omx cmd queue insert
bool omx_vdec::omx_cmd_queue::insert_entry(unsigned p1, unsigned p2, unsigned id)
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
bool omx_vdec::omx_cmd_queue::pop_entry(unsigned *p1, unsigned *p2, unsigned *id)
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
unsigned omx_vdec::omx_cmd_queue::get_q_msg_type()
{
    return m_q[m_read].id;
}

// factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
  return (new omx_vdec);
}

#ifdef _ANDROID_
VideoHeap::VideoHeap(int fd, size_t size, void* base)
{
    // dup file descriptor, map once, use pmem
    init(dup(fd), base, size, 0 , "/dev/pmem_adsp");
}
#endif // _ANDROID_

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec

DESCRIPTION
  Constructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_vdec::omx_vdec(): m_state(OMX_StateInvalid),
                      m_app_data(NULL),
                      m_color_format(OMX_COLOR_FormatYUV420Planar),
                      m_inp_mem_ptr(NULL),
                      m_out_mem_ptr(NULL),
                      pending_input_buffers(0),
                      pending_output_buffers(0),
                      m_out_bm_count(0),
                      m_out_buf_count(0),
                      m_inp_buf_count(OMX_VIDEO_DEC_NUM_INPUT_BUFFERS),
                      m_inp_buf_size(OMX_VIDEO_DEC_INPUT_BUFFER_SIZE),
                      m_inp_bm_count(0),
                      m_inp_bPopulated(OMX_FALSE),
                      m_out_bPopulated(OMX_FALSE),
                      m_height(0),
                      m_width(0),
                      m_port_height(0),
                      m_port_width(0),
                      m_crop_x(0),
                      m_crop_y(0),
                      m_crop_dx(0),
                      m_crop_dy(0),
                      m_flags(0),
                      m_inp_bEnabled(OMX_TRUE),
                      m_out_bEnabled(OMX_TRUE),
                      m_event_port_settings_sent(false),
                      input_flush_progress (false),
                      output_flush_progress (false),
                      m_platform_list(NULL),
                      m_platform_entry(NULL),
                      m_pmem_info(NULL),
                      input_use_buffer (false),
                      output_use_buffer (false),
                      m_ineos_reached (0),
                      m_outeos_pending (0),
                      m_outeos_reached (0),
                      arbitrary_bytes (true),
                      psource_frame (NULL),
                      pdest_frame (NULL),
                      m_inp_heap_ptr (NULL),
                      m_heap_inp_bm_count (0),
                      codec_type_parse ((codec_type)0),
                      first_frame_meta (true),
                      frame_count (0),
                      nal_count (0),
                      nal_length(0),
                      look_ahead_nal (false),
                      first_frame(0),
                      first_buffer(NULL),
                      first_frame_size (0),
                      set_seq_header_done(false),
                      gate_output_buffers(true),
                      gate_input_buffers(false),
                      stride(0),
                      sent_first_frame(false),
                      m_error_propogated(false),
                      scan_lines(0),
                      m_device_file_ptr(NULL),
                      m_vc1_profile((vc1_profile_type)0)
{
  /* Assumption is that , to begin with , we have all the frames with decoder */
  DEBUG_PRINT_HIGH("\n In OMX vdec Constuctor");
  memset(&m_cmp,0,sizeof(m_cmp));
  memset(&m_cb,0,sizeof(m_cb));
  memset (&driver_context,0,sizeof(driver_context));
  memset (&h264_scratch,0,sizeof (OMX_BUFFERHEADERTYPE));
  memset (m_hwdevice_name,0,sizeof(m_hwdevice_name));
  driver_context.video_driver_fd = -1;
  m_vendor_config.pData = NULL;
  pthread_mutex_init(&m_lock, NULL);
  sem_init(&m_cmd_lock,0,0);
}


/* ======================================================================
FUNCTION
  omx_vdec::~omx_vdec

DESCRIPTION
  Destructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_vdec::~omx_vdec()
{
  m_pmem_info = NULL;
  m_port_width = m_port_height = 0;
  DEBUG_PRINT_HIGH("\n In OMX vdec Destructor");
  if(m_pipe_in) close(m_pipe_in);
  if(m_pipe_out) close(m_pipe_out);
  m_pipe_in = -1;
  m_pipe_out = -1;
  DEBUG_PRINT_HIGH("\n Waiting on OMX Msg Thread exit");
  pthread_join(msg_thread_id,NULL);
  DEBUG_PRINT_HIGH("\n Waiting on OMX Async Thread exit");
  pthread_join(async_thread_id,NULL);
  pthread_mutex_destroy(&m_lock);
  sem_destroy(&m_cmd_lock);
  DEBUG_PRINT_HIGH("\n Exit OMX vdec Destructor");
}

/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlProcessMsgCb

DESCRIPTION
  IL Client callbacks are generated through this routine. The decoder
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
void omx_vdec::process_event_cb(void *ctxt, unsigned char id)
{
  unsigned p1; // Parameter - 1
  unsigned p2; // Parameter - 2
  unsigned ident;
  unsigned qsize=0; // qsize
  omx_vdec *pThis = (omx_vdec *) ctxt;

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
    qsize = pThis->m_cmd_q.m_size;
    if(qsize)
    {
      pThis->m_cmd_q.pop_entry(&p1,&p2,&ident);
    }

    if (qsize == 0 && !pThis->gate_output_buffers)
    {
      qsize = pThis->m_ftb_q.m_size;
      if (qsize)
      {
        pThis->m_ftb_q.pop_entry(&p1,&p2,&ident);
      }
    }

    if (qsize == 0)
    {
      qsize = pThis->m_etb_q.m_size;
      if (qsize)
      {
        pThis->m_etb_q.pop_entry(&p1,&p2,&ident);
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
                DEBUG_PRINT_HIGH("\n OMX_CommandStateSet complete, m_state = %d",
                    pThis->m_state);
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventCmdComplete, p1, p2, NULL);
                break;

              case OMX_EventError:
                if(p2 == OMX_StateInvalid)
                {
                    DEBUG_PRINT_ERROR("\n OMX_EventError: p2 is OMX_StateInvalid");
                    pThis->m_state = (OMX_STATETYPE) p2;
                    pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                               OMX_EventError, OMX_ErrorInvalidState, p2, NULL);
                }
                else if (p2 == (unsigned)OMX_ErrorHardware)
                {
                   pThis->omx_report_error();
                }
                else
                {
                    pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventError, p2, NULL, NULL );
                }
                break;

              case OMX_CommandPortDisable:
                DEBUG_PRINT_HIGH("\n OMX_CommandPortDisable complete for port [%d]", p2);
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                      OMX_EventCmdComplete, p1, p2, NULL );
                //TODO: Check if output port is one that got disabled
                pThis->gate_output_buffers = false;
                break;
              case OMX_CommandPortEnable:
                DEBUG_PRINT_HIGH("\n OMX_CommandPortEnable complete for port [%d]", p2);
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
        case OMX_COMPONENT_GENERATE_ETB_ARBITRARY:
          if (pThis->empty_this_buffer_proxy_arbitrary((OMX_HANDLETYPE)p1,\
              (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
          {
            DEBUG_PRINT_ERROR("\n empty_this_buffer_proxy_arbitrary failure");
            pThis->omx_report_error ();
          }
      break;
        case OMX_COMPONENT_GENERATE_ETB:
          if (pThis->empty_this_buffer_proxy((OMX_HANDLETYPE)p1,\
              (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
          {
            DEBUG_PRINT_ERROR("\n empty_this_buffer_proxy failure");
            pThis->omx_report_error ();
          }
         break;

        case OMX_COMPONENT_GENERATE_FTB:
          if ( pThis->fill_this_buffer_proxy((OMX_HANDLETYPE)p1,\
               (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
          {
             DEBUG_PRINT_ERROR("\n fill_this_buffer_proxy failure");
             pThis->omx_report_error ();
          }
        break;

        case OMX_COMPONENT_GENERATE_COMMAND:
          pThis->send_command_proxy(&pThis->m_cmp,(OMX_COMMANDTYPE)p1,\
                                    (OMX_U32)p2,(OMX_PTR)NULL);
          break;

        case OMX_COMPONENT_GENERATE_EBD:

          if (p2 != VDEC_S_SUCCESS)
          {
            DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_EBD failure");
            pThis->omx_report_error ();
          }
          else
          {
            if ( pThis->empty_buffer_done(&pThis->m_cmp,
                 (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone)
            {
               DEBUG_PRINT_ERROR("\n empty_buffer_done failure");
               pThis->omx_report_error ();
            }
          }
          break;

        case OMX_COMPONENT_GENERATE_FBD:
          if (p2 != VDEC_S_SUCCESS)
          {
            DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_FBD failure");
            pThis->omx_report_error ();
          }
          else
          {
             if ( pThis->fill_buffer_done(&pThis->m_cmp,
                  (OMX_BUFFERHEADERTYPE *)p1) != OMX_ErrorNone )
             {
               DEBUG_PRINT_ERROR("\n fill_buffer_done failure");
               pThis->omx_report_error ();
             }
          }
          break;

        case OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH:
          DEBUG_PRINT_HIGH("\n Flush i/p Port complete");
          pThis->input_flush_progress = false;
          DEBUG_PRINT_LOW("\n Input flush done pending input %d",
                             pThis->pending_input_buffers);

          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDEC_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("\nOMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH failure");
              pThis->omx_report_error ();
            }
            else
            {
              /*Check if we need generate event for Flush done*/
              if(BITMASK_PRESENT(&pThis->m_flags,
                                 OMX_COMPONENT_INPUT_FLUSH_PENDING))
              {
                BITMASK_CLEAR (&pThis->m_flags,OMX_COMPONENT_INPUT_FLUSH_PENDING);
                DEBUG_PRINT_LOW("\n Input Flush completed - Notify Client");
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                         OMX_EventCmdComplete,OMX_CommandFlush,
                                         OMX_CORE_INPUT_PORT_INDEX,NULL );
              }
              if (BITMASK_PRESENT(&pThis->m_flags,
                                       OMX_COMPONENT_IDLE_PENDING))
              {
                if (!pThis->output_flush_progress)
                {
                   DEBUG_PRINT_LOW("\n Output flush done hence issue stop");
                   if (ioctl (pThis->driver_context.video_driver_fd,
                              VDEC_IOCTL_CMD_STOP,NULL ) < 0)
                   {
                     DEBUG_PRINT_ERROR("\n VDEC_IOCTL_CMD_STOP failed");
                     pThis->omx_report_error ();
                   }
                }
              }
            }
          }

          break;

        case OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH:
        DEBUG_PRINT_HIGH("\n Flush o/p Port complete");
        pThis->output_flush_progress = false;
        DEBUG_PRINT_LOW("\n Output flush done pending buf %d",pThis->pending_output_buffers);

        if (pThis->m_cb.EventHandler)
        {
          if (p2 != VDEC_S_SUCCESS)
          {
            DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH failed");
            pThis->omx_report_error ();
          }
          else
          {
            /*Check if we need generate event for Flush done*/
            if(BITMASK_PRESENT(&pThis->m_flags,
                               OMX_COMPONENT_OUTPUT_FLUSH_PENDING))
            {
              DEBUG_PRINT_LOW("\n Notify Output Flush done");
              BITMASK_CLEAR (&pThis->m_flags,OMX_COMPONENT_OUTPUT_FLUSH_PENDING);

              pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandFlush,
                                       OMX_CORE_OUTPUT_PORT_INDEX,NULL );
            }
            if (BITMASK_PRESENT(&pThis->m_flags ,OMX_COMPONENT_IDLE_PENDING))
            {
              if (!pThis->input_flush_progress)
              {
                DEBUG_PRINT_LOW("\n Input flush done hence issue stop");
                if (ioctl (pThis->driver_context.video_driver_fd,
                           VDEC_IOCTL_CMD_STOP,NULL ) < 0)
                {
                  DEBUG_PRINT_ERROR("\n VDEC_IOCTL_CMD_STOP failed");
                  pThis->omx_report_error ();
                }
              }
            }
          }
        }
        break;

        case OMX_COMPONENT_GENERATE_START_DONE:
          DEBUG_PRINT_HIGH("\n Rxd OMX_COMPONENT_GENERATE_START_DONE");

          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDEC_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_START_DONE Failure");
              pThis->omx_report_error ();
            }
            else
            {
              DEBUG_PRINT_LOW("\n OMX_COMPONENT_GENERATE_START_DONE Success");
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_EXECUTE_PENDING))
              {
                DEBUG_PRINT_LOW("\n Move to executing");
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
                if (ioctl (pThis->driver_context.video_driver_fd,
                           VDEC_IOCTL_CMD_PAUSE,NULL ) < 0)
                {
                  DEBUG_PRINT_ERROR("\n VDEC_IOCTL_CMD_PAUSE failed");
                  pThis->omx_report_error ();
                }
              }
            }
          }
          else
          {
            DEBUG_PRINT_LOW("\n Event Handler callback is NULL");
          }
          break;

        case OMX_COMPONENT_GENERATE_PAUSE_DONE:
          DEBUG_PRINT_HIGH("\n Rxd OMX_COMPONENT_GENERATE_PAUSE_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDEC_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("OMX_COMPONENT_GENERATE_PAUSE_DONE ret failed");
              pThis->omx_report_error ();
            }
            else
            {
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_PAUSE_PENDING))
              {
                DEBUG_PRINT_LOW("\n OMX_COMPONENT_GENERATE_PAUSE_DONE nofity");
                //Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_PAUSE_PENDING);
                pThis->m_state = OMX_StatePause;
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                       OMX_StatePause, NULL);
              }
            }
          }

          break;

        case OMX_COMPONENT_GENERATE_RESUME_DONE:
          DEBUG_PRINT_HIGH("\n Rxd OMX_COMPONENT_GENERATE_RESUME_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDEC_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_RESUME_DONE failed");
              pThis->omx_report_error ();
            }
            else
            {
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_EXECUTE_PENDING))
              {
                DEBUG_PRINT_LOW("\n Moving the decoder to execute state");
                // Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_EXECUTE_PENDING);
                pThis->m_state = OMX_StateExecuting;
                pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                       OMX_StateExecuting,NULL);
              }
            }
          }

          break;

        case OMX_COMPONENT_GENERATE_STOP_DONE:
          DEBUG_PRINT_HIGH("\n Rxd OMX_COMPONENT_GENERATE_STOP_DONE");
          if (pThis->m_cb.EventHandler)
          {
            if (p2 != VDEC_S_SUCCESS)
            {
              DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_STOP_DONE ret failed");
              pThis->omx_report_error ();
            }
            else
            {
              if(BITMASK_PRESENT(&pThis->m_flags,OMX_COMPONENT_IDLE_PENDING))
              {
                DEBUG_PRINT_LOW("\n OMX_COMPONENT_GENERATE_STOP_DONE Success");
                // Send the callback now
                BITMASK_CLEAR((&pThis->m_flags),OMX_COMPONENT_IDLE_PENDING);
                pThis->m_state = OMX_StateIdle;
                DEBUG_PRINT_LOW("\n Move to Idle State");
                pThis->m_cb.EventHandler(&pThis->m_cmp,pThis->m_app_data,
                                       OMX_EventCmdComplete,OMX_CommandStateSet,
                                         OMX_StateIdle,NULL);
              }
            }
          }

          break;

        case OMX_COMPONENT_GENERATE_HARDWARE_ERROR:
          DEBUG_PRINT_ERROR("\n OMX_COMPONENT_GENERATE_HARDWARE_ERROR");
          pThis->omx_report_error ();
          break;

        default:
          break;
        }
      }
    pthread_mutex_lock(&pThis->m_lock);
    if(!pThis->gate_output_buffers)
    {
    qsize = pThis->m_cmd_q.m_size + pThis->m_ftb_q.m_size +\
            pThis->m_etb_q.m_size;
    }
    else
    {
      qsize = pThis->m_cmd_q.m_size + pThis->m_etb_q.m_size;
    }
    pthread_mutex_unlock(&pThis->m_lock);
  }
  while(qsize>0);

}



/* ======================================================================
FUNCTION
  omx_vdec::ComponentInit

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
OMX_ERRORTYPE omx_vdec::component_init(OMX_STRING role)
{

  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  unsigned int   alignment = 0,buffer_size = 0;
  int fds[2];
  int r;
   bool is_fluid = false;

  if (!m_device_file_ptr) {
    int bytes_read = 0,count = 0;
    unsigned min_size;
    m_device_file_ptr = fopen("/sys/devices/system/soc/soc0/hw_platform","rb");
    if (m_device_file_ptr) {
      (void)fgets((char *)m_hwdevice_name,sizeof(m_hwdevice_name),m_device_file_ptr);
      DEBUG_PRINT_HIGH ("\n Name of the device is %s",m_hwdevice_name);
      min_size = strnlen((const char *)m_hwdevice_name,sizeof(m_hwdevice_name));
      if (strlen("Fluid") < min_size) {
          min_size = strnlen("Fluid",sizeof("Fluid"));
      }
      if  (!strncmp("Fluid",(const char *)m_hwdevice_name,min_size)) {
        is_fluid = true;
      }
      fclose (m_device_file_ptr);
    } else {
      DEBUG_PRINT_HIGH("\n Could not open hw_platform file");
    }
  }

  DEBUG_PRINT_HIGH("\n omx_vdec::component_init(): Start of New Playback");
  driver_context.video_driver_fd = open ("/dev/msm_vidc_dec",\
                      O_RDWR|O_NONBLOCK);

  DEBUG_PRINT_HIGH("\n omx_vdec::component_init(): Open returned fd %d",
                   driver_context.video_driver_fd);

  if(driver_context.video_driver_fd == 0){
    driver_context.video_driver_fd = open ("/dev/msm_vidc_dec",\
                      O_RDWR|O_NONBLOCK);
  }

  if(driver_context.video_driver_fd < 0)
  {
    DEBUG_PRINT_ERROR("Omx_vdec::Comp Init Returning failure\n");
    return OMX_ErrorInsufficientResources;
  }

#ifndef MULTI_DEC_INST
  unsigned int instance_count = 0;
  if (!is_fluid) {
    ioctl_msg.outputparam = &instance_count;
    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_NUMBER_INSTANCES,
               (void*)&ioctl_msg) < 0){
        DEBUG_PRINT_ERROR("\n Instance Query Failed");
        return OMX_ErrorInsufficientResources;
    }
    if (instance_count > 1) {
      close(driver_context.video_driver_fd);
      DEBUG_PRINT_ERROR("\n Reject Second instance of Decoder");
      driver_context.video_driver_fd = -1;
      return OMX_ErrorInsufficientResources;
    }
  }
#endif

#if BITSTREAM_LOG
  outputBufferFile1 = fopen (filename, "ab");
#endif

  // Copy the role information which provides the decoder kind
  strncpy(driver_context.kind,role,128);

  if(!strncmp(driver_context.kind,"OMX.qcom.video.decoder.mpeg4",\
      OMX_MAX_STRINGNAME_SIZE))
  {
     strncpy((char *)m_cRole, "video_decoder.mpeg4",\
     OMX_MAX_STRINGNAME_SIZE);
     driver_context.decoder_format = VDEC_CODECTYPE_MPEG4;
     eCompressionFormat = OMX_VIDEO_CodingMPEG4;
     /*Initialize Start Code for MPEG4*/
     codec_type_parse = CODEC_TYPE_MPEG4;
     m_frame_parser.init_start_codes (codec_type_parse);
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.h263",\
         OMX_MAX_STRINGNAME_SIZE))
  {
     strncpy((char *)m_cRole, "video_decoder.h263",OMX_MAX_STRINGNAME_SIZE);
     DEBUG_PRINT_LOW("\n H263 Decoder selected");
     driver_context.decoder_format = VDEC_CODECTYPE_H263;
     eCompressionFormat = OMX_VIDEO_CodingH263;
     codec_type_parse = CODEC_TYPE_H263;
     m_frame_parser.init_start_codes (codec_type_parse);
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.avc",\
         OMX_MAX_STRINGNAME_SIZE))
  {
    strncpy((char *)m_cRole, "video_decoder.avc",OMX_MAX_STRINGNAME_SIZE);
    driver_context.decoder_format = VDEC_CODECTYPE_H264;
    eCompressionFormat = OMX_VIDEO_CodingAVC;
    codec_type_parse = CODEC_TYPE_H264;
    m_frame_parser.init_start_codes (codec_type_parse);
    m_frame_parser.init_nal_length(nal_length);
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.vc1",\
         OMX_MAX_STRINGNAME_SIZE))
  {
    strncpy((char *)m_cRole, "video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE);
    driver_context.decoder_format = VDEC_CODECTYPE_VC1;
    eCompressionFormat = OMX_VIDEO_CodingWMV;
    codec_type_parse = CODEC_TYPE_VC1;
    m_frame_parser.init_start_codes (codec_type_parse);
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR:Unknown Component\n");
    eRet = OMX_ErrorInvalidComponentName;
  }

  if (eRet == OMX_ErrorNone)
  {
    driver_context.output_format = VDEC_YUV_FORMAT_NV12;

    if  (is_fluid) {

         FILE * pFile;
         char disable_overlay = '0';
         pFile = fopen
         ("/data/data/com.arcsoft.videowall/files/disableoverlay.txt", "rb" );
         if (pFile == NULL) {
           DEBUG_PRINT_HIGH(" fopen FAiLED  for disableoverlay.txt\n");
         } else {
            int count  = fread(&disable_overlay, 1, 1, pFile);
            fclose(pFile);
         }

         if(disable_overlay == '1') {
             DEBUG_PRINT_HIGH(" vdec : TILE format \n");
             driver_context.output_format = VDEC_YUV_FORMAT_TILE_4x2;
         } else {
             DEBUG_PRINT_HIGH("  vdec : NV 12 format \n");
             driver_context.output_format = VDEC_YUV_FORMAT_NV12;
         }
      }

    /*Initialize Decoder with codec type and resolution*/
    ioctl_msg.inputparam = &driver_context.decoder_format;
    ioctl_msg.outputparam = NULL;

    if ( (eRet == OMX_ErrorNone) &&
         ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_CODEC,
                (void*)&ioctl_msg) < 0)

    {
      DEBUG_PRINT_ERROR("\n Set codec type failed");
      eRet = OMX_ErrorInsufficientResources;
    }

    /*Set the output format*/
    ioctl_msg.inputparam = &driver_context.output_format;
    ioctl_msg.outputparam = NULL;

    if ( (eRet == OMX_ErrorNone) &&
         ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_OUTPUT_FORMAT,
           (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set output format failed");
      eRet = OMX_ErrorInsufficientResources;
    }

#ifdef MAX_RES_720P
    driver_context.video_resoultion.frame_height = 720;
    driver_context.video_resoultion.frame_width = 1280;
    driver_context.video_resoultion.stride = 1280;
    driver_context.video_resoultion.scan_lines = 720;
#endif
#ifdef MAX_RES_1080P
    driver_context.video_resoultion.frame_height = 1088;
    driver_context.video_resoultion.frame_width = 1920;
    driver_context.video_resoultion.stride = 1920;
    driver_context.video_resoultion.scan_lines = 1088;
#endif

    ioctl_msg.inputparam = &driver_context.video_resoultion;
    ioctl_msg.outputparam = NULL;

    if ( (eRet == OMX_ErrorNone) &&
        ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_PICRES,
           (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set Resolution failed");
      eRet = OMX_ErrorInsufficientResources;
    }

    /*Get the Buffer requirements for input and output ports*/
    driver_context.input_buffer.buffer_type = VDEC_BUFFER_TYPE_INPUT;
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = &driver_context.input_buffer;

    if ( (eRet == OMX_ErrorNone) &&
         ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_BUFFER_REQ,
           (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Requesting for input buffer requirements failed");
      eRet = OMX_ErrorInsufficientResources;
    }

    m_inp_buf_count = driver_context.input_buffer.actualcount;
    buffer_size = driver_context.input_buffer.buffer_size;
    alignment = driver_context.input_buffer.alignment;
    m_inp_buf_size = ((buffer_size + alignment -1 ) & (~(alignment -1)));
    m_inp_buf_count_min = driver_context.input_buffer.mincount;

    /*Get the Buffer requirements for input and output ports*/
    driver_context.input_buffer.buffer_type = VDEC_BUFFER_TYPE_INPUT;
    ioctl_msg.inputparam = &driver_context.input_buffer;
    ioctl_msg.outputparam = NULL;

    m_inp_buf_count_min = m_inp_buf_count = driver_context.input_buffer.actualcount =
     driver_context.input_buffer.mincount + 3;

    if ( (eRet == OMX_ErrorNone) &&
         ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER_REQ,
           (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set input buffer requirements failed");
      eRet = OMX_ErrorInsufficientResources;
    }


    driver_context.output_buffer.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = &driver_context.output_buffer;

    if ((eRet == OMX_ErrorNone) &&
        ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_BUFFER_REQ,
           (void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Requesting for output buffer requirements failed");
      eRet = OMX_ErrorInsufficientResources;
    }

    m_out_buf_count_recon = m_out_buf_count = driver_context.output_buffer.actualcount;
    m_out_buf_count_min_recon = m_out_buf_count_min = driver_context.output_buffer.mincount;

    alignment = driver_context.output_buffer.alignment;
    buffer_size = driver_context.output_buffer.buffer_size;
    m_out_buf_size_recon = m_out_buf_size =
      ((buffer_size + alignment - 1) & (~(alignment -1)));
#ifdef MAX_RES_720P
    scan_lines = m_crop_dy = m_height = 720;
    stride = m_crop_dx = m_width = 1280;
#endif
#ifdef MAX_RES_1080P
    scan_lines = m_crop_dy = m_height = 1088;
    stride = m_crop_dx = m_width = 1920;
#endif
    m_port_height             = m_height;
    m_port_width              = m_width;
    m_state                   = OMX_StateLoaded;

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
        //close (fds[0]);
        //close (fds[1]);
        fds[0] = temp1 [0];
        fds[1] = temp1 [1];
      }
      m_pipe_in = fds[0];
      m_pipe_out = fds[1];
      r = pthread_create(&msg_thread_id,0,message_thread,this);

      if(r < 0)
      {
        DEBUG_PRINT_ERROR("\n component_init(): message_thread creation failed");
        eRet = OMX_ErrorInsufficientResources;
      }
      else
      {
        r = pthread_create(&async_thread_id,0,async_message_thread,this);
        if(r < 0)
        {
          DEBUG_PRINT_ERROR("\n component_init(): async_message_thread creation failed");
          eRet = OMX_ErrorInsufficientResources;
        }
      }
    }
  }

  if (eRet != OMX_ErrorNone)
  {
    DEBUG_PRINT_ERROR("\n Component Init Failed");
    DEBUG_PRINT_HIGH("\n Calling VDEC_IOCTL_STOP_NEXT_MSG");
    (void)ioctl(driver_context.video_driver_fd, VDEC_IOCTL_STOP_NEXT_MSG,
        NULL);
    DEBUG_PRINT_HIGH("\n Calling close() on Video Driver");
    close (driver_context.video_driver_fd);
    driver_context.video_driver_fd = -1;
  }
  else
  {
    DEBUG_PRINT_HIGH("\n omx_vdec::component_init() success");
  }

  return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::GetComponentVersion

DESCRIPTION
  Returns the component version.

PARAMETERS
  TBD.

RETURN VALUE
  OMX_ErrorNone.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::get_component_version
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
  return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdec::SendCommand

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::send_command(OMX_IN OMX_HANDLETYPE hComp,
                                      OMX_IN OMX_COMMANDTYPE cmd,
                                      OMX_IN OMX_U32 param1,
                                      OMX_IN OMX_PTR cmdData
                                      )
{
    DEBUG_PRINT_LOW("\n send_command: Recieved a Command from Client");
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("ERROR: Send Command in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    post_event((unsigned)cmd,(unsigned)param1,OMX_COMPONENT_GENERATE_COMMAND);
    sem_wait(&m_cmd_lock);
    DEBUG_PRINT_LOW("\n send_command: Command Processed\n");
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::SendCommand

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::send_command_proxy(OMX_IN OMX_HANDLETYPE hComp,
                                            OMX_IN OMX_COMMANDTYPE cmd,
                                            OMX_IN OMX_U32 param1,
                                            OMX_IN OMX_PTR cmdData
                                            )
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_STATETYPE eState = (OMX_STATETYPE) param1;
  int bFlag = 1,sem_posted = 0;;

  DEBUG_PRINT_LOW("\n send_command_proxy(): cmd = %d", cmd);
  DEBUG_PRINT_HIGH("\n send_command_proxy(): Current State %d, Expected State %d",
    m_state, eState);

  if(cmd == OMX_CommandStateSet)
  {
    DEBUG_PRINT_HIGH("\n send_command_proxy(): OMX_CommandStateSet issued");
    DEBUG_PRINT_HIGH("\n Current State %d, Expected State %d", m_state, eState);
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
        BITMASK_SET(&m_flags,OMX_COMPONENT_EXECUTE_PENDING);
        DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Executing\n");
        post_event (NULL,VDEC_S_SUCCESS,OMX_COMPONENT_GENERATE_START_DONE);
        bFlag = 0;
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
         if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_CMD_START,
                    NULL) < 0)
         {
           DEBUG_PRINT_ERROR("\n VDEC_IOCTL_CMD_START FAILED");
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
       DEBUG_PRINT_LOW("\n Command Recieved in OMX_StateExecuting");
       /* Requesting transition from Executing to Idle */
       if(eState == OMX_StateIdle)
       {
         /* Since error is None , we will post an event
         at the end of this function definition
         */
         DEBUG_PRINT_LOW("\n send_command_proxy(): Executing --> Idle \n");
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
         DEBUG_PRINT_LOW("\n PAUSE Command Issued");
         if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_CMD_PAUSE,
                    NULL) < 0)
         {
           DEBUG_PRINT_ERROR("\n Error In Pause State");
           post_event(OMX_EventError,OMX_ErrorHardware,\
                      OMX_COMPONENT_GENERATE_EVENT);
           eRet = OMX_ErrorHardware;
         }
         else
         {
           BITMASK_SET(&m_flags,OMX_COMPONENT_PAUSE_PENDING);
           DEBUG_PRINT_LOW("send_command_proxy(): Pause-->Executing\n");
           bFlag = 0;
         }
       }
       /* Requesting transition from Executing to Loaded */
       else if(eState == OMX_StateLoaded)
       {
         DEBUG_PRINT_ERROR("\n send_command_proxy(): Executing --> Loaded \n");
         post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorIncorrectStateTransition;
       }
       /* Requesting transition from Executing to WaitForResources */
       else if(eState == OMX_StateWaitForResources)
       {
         DEBUG_PRINT_ERROR("\n send_command_proxy(): Executing --> WaitForResources \n");
         post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorIncorrectStateTransition;
       }
       /* Requesting transition from Executing to Executing */
       else if(eState == OMX_StateExecuting)
       {
         DEBUG_PRINT_ERROR("\n send_command_proxy(): Executing --> Executing \n");
         post_event(OMX_EventError,OMX_ErrorSameState,\
                    OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorSameState;
       }
       /* Requesting transition from Executing to Invalid */
       else if(eState == OMX_StateInvalid)
       {
         DEBUG_PRINT_ERROR("\n send_command_proxy(): Executing --> Invalid \n");
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
        DEBUG_PRINT_LOW("\n Pause --> Executing \n");
        if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_CMD_RESUME,
                   NULL) < 0)
        {
          DEBUG_PRINT_ERROR("\n VDEC_IOCTL_CMD_RESUME failed");
          post_event(OMX_EventError,OMX_ErrorHardware,\
                     OMX_COMPONENT_GENERATE_EVENT);
          eRet = OMX_ErrorHardware;
        }
        else
        {
          BITMASK_SET(&m_flags,OMX_COMPONENT_EXECUTE_PENDING);
          DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Executing\n");
          post_event (NULL,VDEC_S_SUCCESS,\
                      OMX_COMPONENT_GENERATE_RESUME_DONE);
          bFlag = 0;
        }
      }
      /* Requesting transition from Pause to Idle */
      else if(eState == OMX_StateIdle)
      {
        /* Since error is None , we will post an event
        at the end of this function definition */
        DEBUG_PRINT_LOW("\n Pause --> Idle \n");
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
        DEBUG_PRINT_ERROR("\n Pause --> loaded \n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Pause to WaitForResources */
      else if(eState == OMX_StateWaitForResources)
      {
        DEBUG_PRINT_ERROR("\n Pause --> WaitForResources \n");
        post_event(OMX_EventError,OMX_ErrorIncorrectStateTransition,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorIncorrectStateTransition;
      }
      /* Requesting transition from Pause to Pause */
      else if(eState == OMX_StatePause)
      {
        DEBUG_PRINT_ERROR("\n Pause --> Pause \n");
        post_event(OMX_EventError,OMX_ErrorSameState,\
                   OMX_COMPONENT_GENERATE_EVENT);
        eRet = OMX_ErrorSameState;
      }
       /* Requesting transition from Pause to Invalid */
      else if(eState == OMX_StateInvalid)
      {
        DEBUG_PRINT_ERROR("\n Pause --> Invalid \n");
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
    DEBUG_PRINT_HIGH("\n send_command_proxy(): OMX_CommandFlush issued"
        "with param1: %d", param1);
    if(0 == param1 || OMX_ALL == param1)
    {
      BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_FLUSH_PENDING);
    }
    if(1 == param1 || OMX_ALL == param1)
    {
      //generate output flush event only.
      BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_FLUSH_PENDING);
    }
    if (!sem_posted){
      sem_posted = 1;
      DEBUG_PRINT_LOW("\n Set the Semaphore");
      sem_post (&m_cmd_lock);
      execute_omx_flush(param1);
    }
    bFlag = 0;
  }
  else if ( cmd == OMX_CommandPortEnable)
  {
    DEBUG_PRINT_HIGH("\n send_command_proxy(): OMX_CommandPortEnable issued"
        "with param1: %d", param1);
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
          DEBUG_PRINT_LOW("\n Enable output Port command recieved");
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
      DEBUG_PRINT_HIGH("\n send_command_proxy(): OMX_CommandPortDisable issued"
          "with param1: %d", param1);
      if(param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL)
      {
          m_inp_bEnabled = OMX_FALSE;
          if((m_state == OMX_StateLoaded || m_state == OMX_StateIdle)
              && release_input_done())
          {
             post_event(OMX_CommandPortDisable,OMX_CORE_INPUT_PORT_INDEX,
                        OMX_COMPONENT_GENERATE_EVENT);
          }
          else
          {
             BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_DISABLE_PENDING);
             if(m_state == OMX_StatePause ||m_state == OMX_StateExecuting)
             {
               if(!sem_posted)
               {
                 sem_posted = 1;
                 sem_post (&m_cmd_lock);
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
          DEBUG_PRINT_LOW("\n Disable output Port command recieved");
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
              if(!sem_posted)
              {
                sem_posted = 1;
                sem_post (&m_cmd_lock);
                execute_omx_flush(OMX_CORE_OUTPUT_PORT_INDEX);
              }
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
  omx_vdec::ExecuteOmxFlush

DESCRIPTION
  Executes the OMX flush.

PARAMETERS
  flushtype - input flush(1)/output flush(0)/ both.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec::execute_omx_flush(OMX_U32 flushType)
{
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  enum vdec_bufferflush flush_dir = VDEC_FLUSH_TYPE_ALL;
  bool bRet = false;

  if(flushType == 0 || flushType == OMX_ALL)
  {
    input_flush_progress = true;
    //flush input only
    bRet = execute_input_flush(flushType);
  }
  if(flushType == 1 || flushType == OMX_ALL)
  {
    //flush output only
    output_flush_progress = true;
    bRet = execute_output_flush(flushType);
  }

  if(flushType == OMX_ALL)
  {
    /*Check if there are buffers with the Driver*/
    DEBUG_PRINT_LOW("\n Flush ALL ioctl issued");
    ioctl_msg.inputparam = &flush_dir;
    ioctl_msg.outputparam = NULL;

    if (ioctl(driver_context.video_driver_fd,VDEC_IOCTL_CMD_FLUSH,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Flush ALL Failed ");
      return false;
    }
  }

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
bool omx_vdec::execute_output_flush(OMX_U32 flushType)
{
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  enum vdec_bufferflush flush_dir = VDEC_FLUSH_TYPE_OUTPUT;
  unsigned      p1 = 0; // Parameter - 1
  unsigned      p2 = 0; // Parameter - 2
  unsigned      ident = 0;
  bool bRet = true;

  /*Generate FBD for all Buffers in the FTBq*/
  pthread_mutex_lock(&m_lock);
  DEBUG_PRINT_LOW("\n Initiate Output Flush");
  while (m_ftb_q.m_size)
  {
    DEBUG_PRINT_LOW("\n Buffer queue size %d pending buf cnt %d",
                       m_ftb_q.m_size,pending_output_buffers);
    m_ftb_q.pop_entry(&p1,&p2,&ident);

    if(ident == OMX_COMPONENT_GENERATE_FTB )
    {
      DEBUG_PRINT_LOW("\n Inside Flush Buffer OMX_COMPONENT_GENERATE_FTB");
      pending_output_buffers++;
      fill_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p2);
    }
    else if (ident == OMX_COMPONENT_GENERATE_FBD)
    {
      fill_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p1);
    }
  }
  pthread_mutex_unlock(&m_lock);

  if(gate_output_buffers)
  {
    DEBUG_PRINT_LOW("\n Output Buffers gated Check flush response");
    if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_FLUSH_PENDING))
    {
      DEBUG_PRINT_LOW("\n Notify Output Flush done");
      BITMASK_CLEAR (&m_flags,OMX_COMPONENT_OUTPUT_FLUSH_PENDING);
      m_cb.EventHandler(&m_cmp,m_app_data,OMX_EventCmdComplete,OMX_CommandFlush,
                               OMX_CORE_OUTPUT_PORT_INDEX,NULL );
    }
    output_flush_progress = false;
    return bRet;
  }

  DEBUG_PRINT_LOW("\n output buffers count = %d",pending_output_buffers);

  if(flushType == 1)
  {
    /*Check if there are buffers with the Driver*/
    DEBUG_PRINT_LOW("\n ioctl command flush for output");
    ioctl_msg.inputparam = &flush_dir;
    ioctl_msg.outputparam = NULL;

    if (ioctl(driver_context.video_driver_fd,VDEC_IOCTL_CMD_FLUSH,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n output flush failed");
      return false;
    }
  }

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
bool omx_vdec::execute_input_flush(OMX_U32 flushType)
{
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  enum vdec_bufferflush flush_dir = VDEC_FLUSH_TYPE_INPUT;
  unsigned       i =0;
  unsigned      p1 = 0; // Parameter - 1
  unsigned      p2 = 0; // Parameter - 2
  unsigned      ident = 0;
  bool bRet = true;

  /*Generate EBD for all Buffers in the ETBq*/
  DEBUG_PRINT_LOW("\n Initiate Input Flush \n");

  pthread_mutex_lock(&m_lock);
  DEBUG_PRINT_LOW("\n Check if the Queue is empty \n");
  while (m_etb_q.m_size)
  {
    m_etb_q.pop_entry(&p1,&p2,&ident);

    if (ident == OMX_COMPONENT_GENERATE_ETB_ARBITRARY)
    {
      DEBUG_PRINT_LOW("\n Flush Input Heap Buffer %p",(OMX_BUFFERHEADERTYPE *)p2);
      m_cb.EmptyBufferDone(&m_cmp ,m_app_data, (OMX_BUFFERHEADERTYPE *)p2);
    }
    else if(ident == OMX_COMPONENT_GENERATE_ETB)
    {
      pending_input_buffers++;
      empty_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p2);
    }
    else if (ident == OMX_COMPONENT_GENERATE_EBD)
    {
      empty_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p1);
    }
  }

  /*Check if Heap Buffers are to be flushed*/
  if (arbitrary_bytes)
  {
    DEBUG_PRINT_LOW("\n Reset all the variables before flusing");
    h264_scratch.nFilledLen = 0;
    nal_count = 0;
    look_ahead_nal = false;
    frame_count = 0;
    DEBUG_PRINT_LOW("\n Initialize parser");
    if (m_frame_parser.mutils)
    {
      m_frame_parser.mutils->initialize_frame_checking_environment();
    }

    while (m_input_pending_q.m_size)
    {
      m_input_pending_q.pop_entry(&p1,&p2,&ident);
      m_cb.EmptyBufferDone(&m_cmp ,m_app_data, (OMX_BUFFERHEADERTYPE *)p1);
    }

    if (psource_frame)
    {
      m_cb.EmptyBufferDone(&m_cmp ,m_app_data,psource_frame);
      psource_frame = NULL;
    }

    if (pdest_frame)
    {
      pdest_frame->nFilledLen = 0;
      m_input_free_q.insert_entry((unsigned) pdest_frame,NULL,NULL);
      pdest_frame = NULL;
    }
    m_frame_parser.flush();
  }

  pthread_mutex_unlock(&m_lock);
  DEBUG_PRINT_LOW("\n Value of pending input buffers %d \n",pending_input_buffers);

  if(flushType == 0)
  {
    /*Check if there are buffers with the Driver*/
    DEBUG_PRINT_LOW("\n Input Flush ioctl issued");
    ioctl_msg.inputparam = &flush_dir;
    ioctl_msg.outputparam = NULL;

    if (ioctl(driver_context.video_driver_fd,VDEC_IOCTL_CMD_FLUSH,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Input Flush Failed ");
      return false;
    }
  }

  return bRet;
}


/* ======================================================================
FUNCTION
  omx_vdec::SendCommandEvent

DESCRIPTION
  Send the event to decoder pipe.  This is needed to generate the callbacks
  in decoder thread context.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec::post_event(unsigned int p1,
                          unsigned int p2,
                          unsigned int id)
{
  bool bRet      =                      false;


  pthread_mutex_lock(&m_lock);

  if( id == OMX_COMPONENT_GENERATE_FTB || \
      (id == OMX_COMPONENT_GENERATE_FBD)||
      (id == OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH))
  {
    m_ftb_q.insert_entry(p1,p2,id);
  }
  else if((id == OMX_COMPONENT_GENERATE_ETB) \
          || (id == OMX_COMPONENT_GENERATE_EBD)||
          (id == OMX_COMPONENT_GENERATE_ETB_ARBITRARY)||
          (id == OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH))
  {
    m_etb_q.insert_entry(p1,p2,id);
  }
  else
  {
    m_cmd_q.insert_entry(p1,p2,id);
  }

  bRet = true;
  DEBUG_PRINT_LOW("\n Value of this pointer in post_event %p",this);
  post_message(this, id);

  pthread_mutex_unlock(&m_lock);

  return bRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::GetParameter

DESCRIPTION
  OMX Get Parameter method implementation

PARAMETERS
  <TBD>.

RETURN VALUE
  Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::get_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                           OMX_IN OMX_INDEXTYPE paramIndex,
                                           OMX_INOUT OMX_PTR     paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    unsigned int height=0,width = 0;

    DEBUG_PRINT_LOW("get_parameter: \n");
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(paramData == NULL)
    {
        DEBUG_PRINT_LOW("Get Param in Invalid paramData \n");
        return OMX_ErrorBadParameter;
    }
  switch(paramIndex)
  {
    case OMX_IndexParamPortDefinition:
    {
      OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
      portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

      DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamPortDefinition\n");

        portDefn->nVersion.nVersion = OMX_SPEC_VERSION;
        portDefn->nSize = sizeof(portDefn);
        portDefn->eDomain    = OMX_PortDomainVideo;
        portDefn->format.video.nFrameHeight =  m_crop_dy;
        portDefn->format.video.nFrameWidth  =  m_crop_dx;
        portDefn->format.video.nStride = m_width;
        portDefn->format.video.nSliceHeight = m_height;
        portDefn->format.video.xFramerate= 25;

      if (0 == portDefn->nPortIndex)
      {
        portDefn->eDir =  OMX_DirInput;
        /*Actual count is based on input buffer count*/
        portDefn->nBufferCountActual = m_inp_buf_count;
        /*Set the Min count*/
        portDefn->nBufferCountMin    = m_inp_buf_count_min;
        portDefn->nBufferSize        = m_inp_buf_size;
        portDefn->format.video.eColorFormat = OMX_COLOR_FormatUnused;
        portDefn->format.video.eCompressionFormat = eCompressionFormat;
        portDefn->bEnabled   = m_inp_bEnabled;
        portDefn->bPopulated = m_inp_bPopulated;
      }
      else if (1 == portDefn->nPortIndex)
      {
        m_out_buf_count = m_out_buf_count_recon;
        m_out_buf_count_min = m_out_buf_count_min_recon;
        m_out_buf_size = m_out_buf_size_recon;
        portDefn->eDir =  OMX_DirOutput;
        portDefn->nBufferCountActual = m_out_buf_count;
        portDefn->nBufferCountMin    = m_out_buf_count_min;
        portDefn->nBufferSize = m_out_buf_size;
        portDefn->bEnabled   = m_out_bEnabled;
        portDefn->bPopulated = m_out_bPopulated;
        height = driver_context.video_resoultion.frame_height;
        width = driver_context.video_resoultion.frame_width;

        portDefn->format.video.nFrameHeight =  height;
        portDefn->format.video.nFrameWidth  =  width;
        portDefn->format.video.nStride      = stride;
        portDefn->format.video.nSliceHeight = scan_lines;
        DEBUG_PRINT_LOW("\n Get Param Slice Height %d Slice Width %d",
                           scan_lines,stride);
        //TODO: Need to add color format
        portDefn->format.video.eColorFormat = m_color_format;
        portDefn->format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
        DEBUG_PRINT_LOW("\n Output Actual %d Output Min %d",
                           portDefn->nBufferCountActual,portDefn->nBufferCountMin);
      }
      else
      {
        portDefn->eDir =  OMX_DirMax;
        DEBUG_PRINT_LOW(" get_parameter: Bad Port idx %d",
                 (int)portDefn->nPortIndex);
        eRet = OMX_ErrorBadPortIndex;
      }

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

      if (0 == portFmt->nPortIndex)
      {
        if (0 == portFmt->nIndex)
        {
              portFmt->eColorFormat =  OMX_COLOR_FormatUnused;
              portFmt->eCompressionFormat = eCompressionFormat;
        }
        else
        {
          DEBUG_PRINT_ERROR("get_parameter: OMX_IndexParamVideoPortFormat:"\
              " NoMore compression formats\n");
          eRet =  OMX_ErrorNoMore;
        }
      }
      else if (1 == portFmt->nPortIndex)
      {
        if (0 == portFmt->nIndex)
        {
           if (driver_context.output_format == VDEC_YUV_FORMAT_NV12)
             portFmt->eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
           else
            portFmt->eColorFormat = (OMX_COLOR_FORMATTYPE)0x7F000000;

           portFmt->eCompressionFormat =  OMX_VIDEO_CodingUnused;
        }
        else
        {
           DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoPortFormat:"\
                  " NoMore Color formats\n");
           eRet =  OMX_ErrorNoMore;
        }
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
        strncpy((char*)comp_role->cRole,(const char*)m_cRole,
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
            if(0 == bufferSupplierType->nPortIndex)
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            else if (1 == bufferSupplierType->nPortIndex)
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            else
                eRet = OMX_ErrorBadPortIndex;


            break;
        }
    case OMX_IndexParamVideoAvc:
        {
            DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoAvc %08x\n",
                        paramIndex);
            break;
        }
    case OMX_IndexParamVideoH263:
        {
            DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoH263 %08x\n",
                        paramIndex);
            break;
        }
    case OMX_IndexParamVideoMpeg4:
        {
            DEBUG_PRINT_LOW("get_parameter: OMX_IndexParamVideoMpeg4 %08x\n",
                        paramIndex);
            break;
        }
    default:
    {
      DEBUG_PRINT_ERROR("get_parameter: unknown param %08x\n", paramIndex);
      eRet =OMX_ErrorUnsupportedIndex;
    }

  }

  DEBUG_PRINT_LOW("\n get_parameter returning Height %d , Width %d \n",
              m_height, m_width);
  return eRet;

}

/* ======================================================================
FUNCTION
  omx_vdec::Setparameter

DESCRIPTION
  OMX Set Parameter method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::set_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                           OMX_IN OMX_INDEXTYPE paramIndex,
                                           OMX_IN OMX_PTR        paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
    unsigned int   alignment = 0,buffer_size = 0;
    int           i;

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
  switch(paramIndex)
  {
    case OMX_IndexParamPortDefinition:
    {
      OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
      portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

      /*set_parameter can be called in loaded state
      or disabled port */

      /* When the component is in Loaded state and IDLE Pending*/
      if(((m_state == OMX_StateLoaded)&&
          !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
         /* Or while the I/P or the O/P port or disabled */
         ||((OMX_DirInput == portDefn->eDir && m_inp_bEnabled == OMX_FALSE)||
         (OMX_DirOutput == portDefn->eDir && m_out_bEnabled == OMX_FALSE)))
      {
       DEBUG_PRINT_LOW("Set Parameter called in valid state");
      }
      else
      {
         DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
         return OMX_ErrorIncorrectStateOperation;
      }
      DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d\n",
             (int)portDefn->format.video.nFrameHeight,
             (int)portDefn->format.video.nFrameWidth);

      eRet = omx_vdec_validate_port_param(portDefn->format.video.nFrameHeight,
                                          portDefn->format.video.nFrameWidth);
      if(eRet != OMX_ErrorNone)
      {
         return OMX_ErrorUnsupportedSetting;
      }
      if(OMX_DirOutput == portDefn->eDir)
      {
          if ( portDefn->nBufferCountActual < m_out_buf_count_min ||
               portDefn->nBufferSize !=  m_out_buf_size
              )
          {
              return OMX_ErrorBadParameter;
          }
          driver_context.output_buffer.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
          ioctl_msg.inputparam = NULL;
          ioctl_msg.outputparam = &driver_context.output_buffer;

          if (ioctl (driver_context.video_driver_fd,
                     VDEC_IOCTL_GET_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
              DEBUG_PRINT_ERROR("\n Request output buffer requirements failed");
              return OMX_ErrorUnsupportedSetting;
          }
          driver_context.output_buffer.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
          driver_context.output_buffer.actualcount =
                                                portDefn->nBufferCountActual;
          ioctl_msg.inputparam = &driver_context.output_buffer;
          ioctl_msg.outputparam = NULL;

          if (ioctl (driver_context.video_driver_fd,
                     VDEC_IOCTL_SET_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
              DEBUG_PRINT_ERROR("\n Request output buffer requirements failed");
              return OMX_ErrorUnsupportedSetting;
          }
          m_out_buf_count = portDefn->nBufferCountActual;
          m_out_buf_count_recon = m_out_buf_count;
        DEBUG_PRINT_LOW("set_parameter:OMX_IndexParamPortDefinition output port\n");
      }
      else if(OMX_DirInput == portDefn->eDir)
      {
         if(m_height != portDefn->format.video.nFrameHeight ||
            m_width  != portDefn->format.video.nFrameWidth)
         {
           DEBUG_PRINT_LOW("set_parameter ip port: stride %d\n",
                       (int)portDefn->format.video.nStride);
           // set the HxW only if non-zero
           if((portDefn->format.video.nFrameHeight != 0x0)
              && (portDefn->format.video.nFrameWidth != 0x0))
           {
               m_crop_x = m_crop_y = 0;
               m_crop_dy = m_port_height = m_height =
                 portDefn->format.video.nFrameHeight;
               m_crop_dx = m_port_width = m_width  =
                 portDefn->format.video.nFrameWidth;
               scan_lines = portDefn->format.video.nSliceHeight;
               stride = portDefn->format.video.nStride;
               DEBUG_PRINT_LOW("\n SetParam with new H %d and W %d\n",
                           m_height, m_width );
               driver_context.video_resoultion.frame_height = m_height;
               driver_context.video_resoultion.frame_width = m_width;
               driver_context.video_resoultion.stride = stride;
               driver_context.video_resoultion.scan_lines = scan_lines;
               ioctl_msg.inputparam = &driver_context.video_resoultion;
               ioctl_msg.outputparam = NULL;

               if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_PICRES,
                          (void*)&ioctl_msg) < 0)
               {
                   DEBUG_PRINT_ERROR("\n Set Resolution failed");
                   return OMX_ErrorUnsupportedSetting;
               }
               driver_context.output_buffer.buffer_type =
                                                        VDEC_BUFFER_TYPE_OUTPUT;
               ioctl_msg.inputparam = NULL;
               ioctl_msg.outputparam = &driver_context.output_buffer;

               if (ioctl (driver_context.video_driver_fd,
                          VDEC_IOCTL_GET_BUFFER_REQ,(void*)&ioctl_msg) < 0)
               {
                   DEBUG_PRINT_ERROR("\n Request output buffer requirements failed");
                   return OMX_ErrorUnsupportedSetting;
               }

               m_out_buf_count_recon = m_out_buf_count = driver_context.output_buffer.actualcount;
               m_out_buf_count_min_recon = m_out_buf_count_min = driver_context.output_buffer.mincount;

               alignment = driver_context.output_buffer.alignment;
               buffer_size = driver_context.output_buffer.buffer_size;
               m_out_buf_size_recon = m_out_buf_size =
                 ((buffer_size + alignment - 1) & (~(alignment - 1)));
           }
        }
        else
        {
            /*
              If actual buffer count is greater than the Min buffer
              count,change the actual count.
              m_inp_buf_count is initialized to OMX_CORE_NUM_INPUT_BUFFERS
              in the constructor
            */
            if ( portDefn->nBufferCountActual < m_inp_buf_count_min ||
                 portDefn->nBufferSize !=  m_inp_buf_size
                )
            {
                return OMX_ErrorBadParameter;
            }
               /*Get the Buffer requirements for input and output ports*/
               driver_context.input_buffer.buffer_type = VDEC_BUFFER_TYPE_INPUT;
               ioctl_msg.inputparam = NULL;
               ioctl_msg.outputparam = &driver_context.input_buffer;

               if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_BUFFER_REQ,
                          (void*)&ioctl_msg) < 0)
               {
                   DEBUG_PRINT_ERROR("\n Request input buffer requirements failed");
                   return OMX_ErrorUnsupportedSetting;
               }

               driver_context.input_buffer.buffer_type = VDEC_BUFFER_TYPE_INPUT;
               driver_context.input_buffer.actualcount =
                 portDefn->nBufferCountActual;
               ioctl_msg.inputparam = &driver_context.input_buffer;
               ioctl_msg.outputparam = NULL;

               if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER_REQ,
                          (void*)&ioctl_msg) < 0)
               {
                   DEBUG_PRINT_ERROR("\n Request input buffer requirements failed");
                   return OMX_ErrorUnsupportedSetting;
               }

            m_inp_buf_count = portDefn->nBufferCountActual;
          DEBUG_PRINT_LOW("\n set_parameter: Image Dimensions same  \n");
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
      DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d\n",
              portFmt->eColorFormat);

      DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d\n",
             portFmt->eColorFormat);
      if(1 == portFmt->nPortIndex)
      {

         m_color_format = portFmt->eColorFormat;
      }
    }
    break;

    case OMX_QcomIndexPortDefn:
    {
        OMX_QCOM_PARAM_PORTDEFINITIONTYPE *portFmt =
            (OMX_QCOM_PARAM_PORTDEFINITIONTYPE *) paramData;
        DEBUG_PRINT_LOW("set_parameter: OMX_IndexQcomParamPortDefinitionType %d\n",
            portFmt->nFramePackingFormat);

        /* Input port */
        if (portFmt->nPortIndex == 0)
        {
            if (portFmt->nFramePackingFormat == OMX_QCOM_FramePacking_Arbitrary)
            {
               arbitrary_bytes = true;
            }
            else if (portFmt->nFramePackingFormat ==
                OMX_QCOM_FramePacking_OnlyOneCompleteFrame)
            {
               arbitrary_bytes = false;
            }
            else
            {
                DEBUG_PRINT_ERROR("Setparameter: unknown FramePacking format %d\n",
                    portFmt->nFramePackingFormat);
                eRet = OMX_ErrorUnsupportedSetting;
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

          if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.avc",OMX_MAX_STRINGNAME_SIZE))
          {
              if(!strncmp((char*)comp_role->cRole,"video_decoder.avc",OMX_MAX_STRINGNAME_SIZE))
              {
                  strncpy((char*)m_cRole,"video_decoder.avc",OMX_MAX_STRINGNAME_SIZE);
              }
              else
              {
                  DEBUG_PRINT_ERROR("Setparameter: unknown Index %s\n", comp_role->cRole);
                  eRet =OMX_ErrorUnsupportedSetting;
              }
          }
          else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
          {
              if(!strncmp((const char*)comp_role->cRole,"video_decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
              {
                  strncpy((char*)m_cRole,"video_decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE);
              }
              else
              {
                  DEBUG_PRINT_ERROR("Setparameter: unknown Index %s\n", comp_role->cRole);
                  eRet = OMX_ErrorUnsupportedSetting;
              }
          }
          else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.h263",OMX_MAX_STRINGNAME_SIZE))
          {
              if(!strncmp((const char*)comp_role->cRole,"video_decoder.h263",OMX_MAX_STRINGNAME_SIZE))
              {
                  strncpy((char*)m_cRole,"video_decoder.h263",OMX_MAX_STRINGNAME_SIZE);
              }
              else
              {
                  DEBUG_PRINT_ERROR("Setparameter: unknown Index %s\n", comp_role->cRole);
                  eRet =OMX_ErrorUnsupportedSetting;
              }
          }
          else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.vc1",OMX_MAX_STRINGNAME_SIZE))
          {
              if(!strncmp((const char*)comp_role->cRole,"video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE))
              {
                  strncpy((char*)m_cRole,"video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE);
              }
              else
              {
                  DEBUG_PRINT_ERROR("Setparameter: unknown Index %s\n", comp_role->cRole);
                  eRet =OMX_ErrorUnsupportedSetting;
              }
          }
          else
          {
               DEBUG_PRINT_ERROR("Setparameter: unknown param %s\n", driver_context.kind);
               eRet = OMX_ErrorInvalidComponentName;
          }
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
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt %d\n",
              priorityMgmtype->nGroupID);

            DEBUG_PRINT_LOW("set_parameter: priorityMgmtype %d\n",
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
      case OMX_IndexParamVideoAvc:
          {
              DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoAvc %d\n",
                    paramIndex);
              break;
          }
      case OMX_IndexParamVideoH263:
          {
              DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoH263 %d\n",
                    paramIndex);
              break;
          }
      case OMX_IndexParamVideoMpeg4:
          {
              DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoMpeg4 %d\n",
                    paramIndex);
              break;
          }

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
  omx_vdec::GetConfig

DESCRIPTION
  OMX Get Config Method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::get_config(OMX_IN OMX_HANDLETYPE      hComp,
                                        OMX_IN OMX_INDEXTYPE configIndex,
                                        OMX_INOUT OMX_PTR     configData)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;

  if (m_state == OMX_StateInvalid)
  {
     DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
     return OMX_ErrorInvalidState;
  }

  switch (configIndex)
  {
    case OMX_QcomIndexConfigInterlaced:
    {
      OMX_QCOM_CONFIG_INTERLACETYPE *configFmt =
                                   (OMX_QCOM_CONFIG_INTERLACETYPE *) configData;
      if (configFmt->nPortIndex == 1)
      {
        if (configFmt->nIndex == 0)
        {
          configFmt->eInterlaceType = OMX_QCOM_InterlaceFrameProgressive;
        }
        else if (configFmt->nIndex == 1)
        {
          configFmt->eInterlaceType =
                                  OMX_QCOM_InterlaceInterleaveFrameTopFieldFirst;
        }
        else if (configFmt->nIndex == 2)
        {
          configFmt->eInterlaceType =
          OMX_QCOM_InterlaceInterleaveFrameBottomFieldFirst;
        }
        else
        {
          DEBUG_PRINT_ERROR("get_config: OMX_QcomIndexConfigInterlaced:"
                            " NoMore Interlaced formats\n");
          eRet = OMX_ErrorNoMore;
        }

      }
      else
      {
        DEBUG_PRINT_ERROR("get_config: Bad port index %d queried on only o/p port\n",
        (int)configFmt->nPortIndex);
        eRet = OMX_ErrorBadPortIndex;
      }
    break;
    }
    case OMX_QcomIndexQueryNumberOfVideoDecInstance:
    {
        struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
        QOMX_VIDEO_QUERY_DECODER_INSTANCES *decoderinstances =
          (QOMX_VIDEO_QUERY_DECODER_INSTANCES*)configData;
        ioctl_msg.outputparam = (void*)&decoderinstances->nNumOfInstances;
        (void)(ioctl(driver_context.video_driver_fd,
               VDEC_IOCTL_GET_NUMBER_INSTANCES,&ioctl_msg));
    break;
    }
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
  omx_vdec::SetConfig

DESCRIPTION
  OMX Set Config method implementation

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdec::set_config(OMX_IN OMX_HANDLETYPE      hComp,
                                        OMX_IN OMX_INDEXTYPE configIndex,
                                        OMX_IN OMX_PTR        configData)
{
  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  OMX_ERRORTYPE ret = OMX_ErrorNone;
  OMX_VIDEO_CONFIG_NALSIZE *pNal;

  DEBUG_PRINT_LOW("\n Set Config Called");

  if (m_state == OMX_StateExecuting)
  {
     DEBUG_PRINT_ERROR("set_config:Ignore in Exe state\n");
     return ret;
  }

  if (configIndex == OMX_IndexVendorVideoExtraData)
  {
    OMX_VENDOR_EXTRADATATYPE *config = (OMX_VENDOR_EXTRADATATYPE *) configData;
    DEBUG_PRINT_LOW("\n Index OMX_IndexVendorVideoExtraData called");
    if (!strcmp(driver_context.kind, "OMX.qcom.video.decoder.avc"))
    {
      DEBUG_PRINT_LOW("\n Index OMX_IndexVendorVideoExtraData AVC");
      OMX_U32 extra_size;
      // Parsing done here for the AVC atom is definitely not generic
      // Currently this piece of code is working, but certainly
      // not tested with all .mp4 files.
      // Incase of failure, we might need to revisit this
      // for a generic piece of code.

      // Retrieve size of NAL length field
      // byte #4 contains the size of NAL lenght field
      nal_length = (config->pData[4] & 0x03) + 1;

      extra_size = 0;
      if (nal_length > 2)
      {
        /* Presently we assume that only one SPS and one PPS in AvC1 Atom */
        extra_size = (nal_length - 2) * 2;
      }

      // SPS starts from byte #6
      OMX_U8 *pSrcBuf = (OMX_U8 *) (&config->pData[6]);
      OMX_U8 *pDestBuf;
      m_vendor_config.nPortIndex = config->nPortIndex;

      // minus 6 --> SPS starts from byte #6
      // minus 1 --> picture param set byte to be ignored from avcatom
      m_vendor_config.nDataSize = config->nDataSize - 6 - 1 + extra_size;
      m_vendor_config.pData = (OMX_U8 *) malloc(m_vendor_config.nDataSize);
      OMX_U32 len;
      OMX_U8 index = 0;
      // case where SPS+PPS is sent as part of set_config
      pDestBuf = m_vendor_config.pData;

      DEBUG_PRINT_LOW("Rxd SPS+PPS nPortIndex[%d] len[%d] data[0x%x]\n",
           m_vendor_config.nPortIndex,
           m_vendor_config.nDataSize,
           m_vendor_config.pData);
      while (index < 2)
      {
        uint8 *psize;
        len = *pSrcBuf;
        len = len << 8;
        len |= *(pSrcBuf + 1);
        psize = (uint8 *) & len;
        memcpy(pDestBuf + nal_length, pSrcBuf + 2,len);
        for (int i = 0; i < nal_length; i++)
        {
          pDestBuf[i] = psize[nal_length - 1 - i];
        }
        //memcpy(pDestBuf,pSrcBuf,(len+2));
        pDestBuf += len + nal_length;
        pSrcBuf += len + 2;
        index++;
        pSrcBuf++;   // skip picture param set
        len = 0;
      }
    }
    else if (!strcmp(driver_context.kind, "OMX.qcom.video.decoder.mpeg4"))
    {
      m_vendor_config.nPortIndex = config->nPortIndex;
      m_vendor_config.nDataSize = config->nDataSize;
      m_vendor_config.pData = (OMX_U8 *) malloc((config->nDataSize));
      memcpy(m_vendor_config.pData, config->pData,config->nDataSize);
    }
    else if (!strcmp(driver_context.kind, "OMX.qcom.video.decoder.vc1"))
    {
        if(m_vendor_config.pData)
        {
            free(m_vendor_config.pData);
            m_vendor_config.pData = NULL;
            m_vendor_config.nDataSize = 0;
        }

        if (((*((OMX_U32 *) config->pData)) &
             VC1_SP_MP_START_CODE_MASK) ==
             VC1_SP_MP_START_CODE)
        {
            DEBUG_PRINT_LOW("set_config - VC1 simple/main profile\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize = config->nDataSize;
            m_vendor_config.pData =
                (OMX_U8 *) malloc(config->nDataSize);
            memcpy(m_vendor_config.pData, config->pData,
                   config->nDataSize);
            m_vc1_profile = VC1_SP_MP_RCV;
        }
        else if (*((OMX_U32 *) config->pData) == VC1_AP_SEQ_START_CODE)
        {
            DEBUG_PRINT_LOW("set_config - VC1 Advance profile\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize = config->nDataSize;
            m_vendor_config.pData =
                (OMX_U8 *) malloc((config->nDataSize));
            memcpy(m_vendor_config.pData, config->pData,
                   config->nDataSize);
            m_vc1_profile = VC1_AP;
        }
        else if ((config->nDataSize == VC1_STRUCT_C_LEN))
        {
            DEBUG_PRINT_LOW("set_config - VC1 Simple/Main profile struct C only\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize  = config->nDataSize;
            m_vendor_config.pData = (OMX_U8*)malloc(config->nDataSize);
            memcpy(m_vendor_config.pData,config->pData,config->nDataSize);
            m_vc1_profile = VC1_SP_MP_RCV;
        }
        else
        {
            DEBUG_PRINT_LOW("set_config - Error: Unknown VC1 profile\n");
        }
    }
    return ret;
  }
  else if (configIndex == OMX_IndexConfigVideoNalSize)
  {

    pNal = reinterpret_cast < OMX_VIDEO_CONFIG_NALSIZE * >(configData);
    nal_length = pNal->nNaluBytes;
    m_frame_parser.init_nal_length(nal_length);
    DEBUG_PRINT_LOW("\n Nal Length option called with Size %d",nal_length);
    return ret;
  }

  return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdec::GetExtensionIndex

DESCRIPTION
  OMX GetExtensionIndex method implementaion.  <TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::get_extension_index(OMX_IN OMX_HANDLETYPE      hComp,
                                                OMX_IN OMX_STRING      paramName,
                                                OMX_OUT OMX_INDEXTYPE* indexType)
{
    DEBUG_PRINT_ERROR("get_extension_index: Error, Not implemented\n");
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Extension Index in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdec::GetState

DESCRIPTION
  Returns the state information back to the caller.<TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdec::get_state(OMX_IN OMX_HANDLETYPE  hComp,
                                       OMX_OUT OMX_STATETYPE* state)
{
  *state = m_state;
  DEBUG_PRINT_LOW("get_state: Returning the state %d\n",*state);
  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::ComponentTunnelRequest

DESCRIPTION
  OMX Component Tunnel Request method implementation. <TBD>

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::component_tunnel_request(OMX_IN OMX_HANDLETYPE                hComp,
                                                     OMX_IN OMX_U32                        port,
                                                     OMX_IN OMX_HANDLETYPE        peerComponent,
                                                     OMX_IN OMX_U32                    peerPort,
                                                     OMX_INOUT OMX_TUNNELSETUPTYPE* tunnelSetup)
{
  DEBUG_PRINT_ERROR("Error: component_tunnel_request Not Implemented\n");
  return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdec::UseInputBuffer

DESCRIPTION
  Helper function for Use buffer in the input pin

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::use_input_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct vdec_setbuffer_cmd setbuffers;
  OMX_BUFFERHEADERTYPE *input = NULL;
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  unsigned   i = 0;
  unsigned char *buf_addr = NULL;
  int pmem_fd = -1;

  if(bytes != m_inp_buf_size)
  {
    return OMX_ErrorBadParameter;
  }

  if(!m_inp_mem_ptr)
  {
    DEBUG_PRINT_HIGH("\n Use i/p buffer case - Header List allocation");
    input_use_buffer = true;
    m_inp_mem_ptr = (OMX_BUFFERHEADERTYPE*) \
    calloc( (sizeof(OMX_BUFFERHEADERTYPE)), m_inp_buf_count);

    if (m_inp_mem_ptr == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    driver_context.ptr_inputbuffer = (struct vdec_bufferpayload *) \
    calloc ((sizeof (struct vdec_bufferpayload)),m_inp_buf_count);

    if (driver_context.ptr_inputbuffer == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    for (i=0; i < m_inp_buf_count; i++)
    {
      driver_context.ptr_inputbuffer [i].pmem_fd = -1;
    }
  }

  for(i=0; i< m_inp_buf_count; i++)
  {
    if(BITMASK_ABSENT(&m_inp_bm_count,i))
    {
      break;
    }
  }

  if(i < m_inp_buf_count)
  {
    pmem_fd = open ("/dev/pmem_adsp",O_RDWR | O_SYNC);

    if (pmem_fd < 0)
    {
      return OMX_ErrorInsufficientResources;
    }

    if(pmem_fd == 0)
    {
      pmem_fd = open ("/dev/pmem_adsp",O_RDWR | O_SYNC);
      if (pmem_fd < 0)
      {
        return OMX_ErrorInsufficientResources;
      }
    }

    if(!align_pmem_buffers(pmem_fd, m_inp_buf_size,
      driver_context.input_buffer.alignment))
    {
      DEBUG_PRINT_ERROR("\n align_pmem_buffers() failed");
      close(pmem_fd);
      return OMX_ErrorInsufficientResources;
    }

    buf_addr = (unsigned char *)mmap(NULL,m_inp_buf_size,PROT_READ|PROT_WRITE,
                    MAP_SHARED,pmem_fd,0);

    if (buf_addr == MAP_FAILED)
    {
      return OMX_ErrorInsufficientResources;
    }

    driver_context.ptr_inputbuffer [i].bufferaddr = buf_addr;
    driver_context.ptr_inputbuffer [i].pmem_fd = pmem_fd;
    driver_context.ptr_inputbuffer [i].mmaped_size = m_inp_buf_size;
    driver_context.ptr_inputbuffer [i].offset = 0;

    setbuffers.buffer_type = VDEC_BUFFER_TYPE_INPUT;
    memcpy (&setbuffers.buffer,&driver_context.ptr_inputbuffer [i],
            sizeof (vdec_bufferpayload));
    ioctl_msg.inputparam  = &setbuffers;
    ioctl_msg.outputparam = NULL;

    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER,
         &ioctl_msg) < 0)
    {
      return OMX_ErrorInsufficientResources;
    }

    *bufferHdr = (m_inp_mem_ptr + i);
    input = *bufferHdr;
    BITMASK_SET(&m_inp_bm_count,i);

    input->pBuffer           = (OMX_U8 *)buffer;
    input->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
    input->nVersion.nVersion = OMX_SPEC_VERSION;
    input->nAllocLen         = m_inp_buf_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    input->pInputPortPrivate = (void *)&driver_context.ptr_inputbuffer [i];
  }
  else
  {
    eRet = OMX_ErrorInsufficientResources;
  }
  return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdec::UseOutputBuffer

DESCRIPTION
  Helper function for Use buffer in the input pin

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::use_output_buffer(
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

  if(!m_out_mem_ptr)
  {
    DEBUG_PRINT_HIGH("\n Use o/p buffer case - Header List allocation");
    output_use_buffer = true;
    int nBufHdrSize        = 0;
    int nPlatformEntrySize = 0;
    int nPlatformListSize  = 0;
    int nPMEMInfoSize = 0;
    OMX_QCOM_PLATFORM_PRIVATE_LIST      *pPlatformList;
    OMX_QCOM_PLATFORM_PRIVATE_ENTRY     *pPlatformEntry;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

    DEBUG_PRINT_LOW("Allocating First Output Buffer(%d)\n",m_out_buf_count);
    nBufHdrSize        = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);

    nPMEMInfoSize      = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO);
    nPlatformListSize  = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_LIST);
    nPlatformEntrySize = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_ENTRY);

    DEBUG_PRINT_LOW("TotalBufHdr %d BufHdrSize %d PMEM %d PL %d\n",nBufHdrSize,
                         sizeof(OMX_BUFFERHEADERTYPE),
                         nPMEMInfoSize,
                         nPlatformListSize);
    DEBUG_PRINT_LOW("PE %d bmSize %d \n",nPlatformEntrySize,
                         m_out_bm_count);

    /*
     * Memory for output side involves the following:
     * 1. Array of Buffer Headers
     * 2. Platform specific information List
     * 3. Platform specific Entry List
     * 4. PMem Information entries
     * 5. Bitmask array to hold the buffer allocation details
     * In order to minimize the memory management entire allocation
     * is done in one step.
     */
    m_out_mem_ptr = (OMX_BUFFERHEADERTYPE  *)calloc(nBufHdrSize,1);
    // Alloc mem for platform specific info
    char *pPtr=NULL;
    pPtr = (char*) calloc(nPlatformListSize + nPlatformEntrySize +
                                     nPMEMInfoSize,1);
    driver_context.ptr_outputbuffer = (struct vdec_bufferpayload *) \
      calloc (sizeof(struct vdec_bufferpayload),m_out_buf_count);
    driver_context.ptr_respbuffer = (struct vdec_output_frameinfo  *)\
      calloc (sizeof (struct vdec_output_frameinfo),m_out_buf_count);

    if(m_out_mem_ptr && pPtr && driver_context.ptr_outputbuffer
       && driver_context.ptr_respbuffer)
    {
      bufHdr          =  m_out_mem_ptr;
      m_platform_list = (OMX_QCOM_PLATFORM_PRIVATE_LIST *)(pPtr);
      m_platform_entry= (OMX_QCOM_PLATFORM_PRIVATE_ENTRY *)
                        (((char *) m_platform_list)  + nPlatformListSize);
      m_pmem_info     = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                        (((char *) m_platform_entry) + nPlatformEntrySize);
      pPlatformList   = m_platform_list;
      pPlatformEntry  = m_platform_entry;
      pPMEMInfo       = m_pmem_info;

      DEBUG_PRINT_LOW("Memory Allocation Succeeded for OUT port%p\n",m_out_mem_ptr);

      // Settting the entire storage nicely
      DEBUG_PRINT_LOW("bHdr %p OutMem %p PE %p\n",bufHdr, m_out_mem_ptr,pPlatformEntry);
      DEBUG_PRINT_LOW(" Pmem Info = %p \n",pPMEMInfo);
      for(i=0; i < m_out_buf_count ; i++)
      {
        bufHdr->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        bufHdr->nVersion.nVersion  = OMX_SPEC_VERSION;
        // Set the values when we determine the right HxW param
        bufHdr->nAllocLen          = bytes;
        bufHdr->nFilledLen         = 0;
        bufHdr->pAppPrivate        = appData;
        bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
        // Platform specific PMEM Information
        // Initialize the Platform Entry
        //DEBUG_PRINT_LOW("Initializing the Platform Entry for %d\n",i);
        pPlatformEntry->type       = OMX_QCOM_PLATFORM_PRIVATE_PMEM;
        pPlatformEntry->entry      = pPMEMInfo;
        // Initialize the Platform List
        pPlatformList->nEntries    = 1;
        pPlatformList->entryList   = pPlatformEntry;
        // Keep pBuffer NULL till vdec is opened
        bufHdr->pBuffer            = NULL;

        pPMEMInfo->offset          =  0;
        pPMEMInfo->pmem_fd = 0;
        bufHdr->pPlatformPrivate = pPlatformList;

        driver_context.ptr_outputbuffer[i].pmem_fd = -1;

        /*Create a mapping between buffers*/
        bufHdr->pOutputPortPrivate = &driver_context.ptr_respbuffer[i];
        driver_context.ptr_respbuffer[i].client_data = (void *) \
                                            &driver_context.ptr_outputbuffer[i];
        // Move the buffer and buffer header pointers
        bufHdr++;
        pPMEMInfo++;
        pPlatformEntry++;
        pPlatformList++;
      }
    }
    else
    {
      DEBUG_PRINT_ERROR("Output buf mem alloc failed[0x%x][0x%x]\n",\
                                        m_out_mem_ptr, pPtr);
      if(m_out_mem_ptr)
      {
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
      }
      if(pPtr)
      {
        free(pPtr);
        pPtr = NULL;
      }
      if(driver_context.ptr_outputbuffer)
      {
        free(driver_context.ptr_outputbuffer);
        driver_context.ptr_outputbuffer = NULL;
      }
      if(driver_context.ptr_respbuffer)
      {
        free(driver_context.ptr_respbuffer);
        driver_context.ptr_respbuffer = NULL;
      }
      eRet =  OMX_ErrorInsufficientResources;
    }
  }

  for(i=0; i< m_out_buf_count; i++)
  {
    if(BITMASK_ABSENT(&m_out_bm_count,i))
    {
      break;
    }
  }

  if (eRet == OMX_ErrorNone)
  {
    if(i < m_out_buf_count)
    {
      driver_context.ptr_outputbuffer[i].pmem_fd = \
        open ("/dev/pmem_adsp", O_RDWR | O_SYNC);

      if (driver_context.ptr_outputbuffer[i].pmem_fd < 0)
      {
        return OMX_ErrorInsufficientResources;
      }

      if(driver_context.ptr_outputbuffer[i].pmem_fd == 0)
      {
        driver_context.ptr_outputbuffer[i].pmem_fd = \
          open ("/dev/pmem_adsp", O_RDWR | O_SYNC);

        if (driver_context.ptr_outputbuffer[i].pmem_fd < 0)
        {
          return OMX_ErrorInsufficientResources;
        }
      }

      if(!align_pmem_buffers(driver_context.ptr_outputbuffer[i].pmem_fd,
        m_out_buf_size, driver_context.output_buffer.alignment))
      {
        DEBUG_PRINT_ERROR("\n align_pmem_buffers() failed");
        close(driver_context.ptr_outputbuffer[i].pmem_fd);
        return OMX_ErrorInsufficientResources;
      }

      driver_context.ptr_outputbuffer[i].bufferaddr =
        (unsigned char *)mmap(NULL,m_out_buf_size,PROT_READ|PROT_WRITE,
         MAP_SHARED,driver_context.ptr_outputbuffer[i].pmem_fd,0);

      if (driver_context.ptr_outputbuffer[i].bufferaddr == MAP_FAILED)
      {
        return OMX_ErrorInsufficientResources;
      }
      driver_context.ptr_outputbuffer[i].offset = 0;
      m_pmem_info[i].offset = driver_context.ptr_outputbuffer[i].offset;
      m_pmem_info[i].pmem_fd = driver_context.ptr_outputbuffer[i].pmem_fd;

      // found an empty buffer at i
      *bufferHdr = (m_out_mem_ptr + i );
      (*bufferHdr)->pBuffer = buffer;
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

/* ======================================================================
FUNCTION
  omx_vdec::UseBuffer

DESCRIPTION
  OMX Use Buffer method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None , if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::use_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes,
                         OMX_IN OMX_U8*                   buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Use Buffer in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
      eRet = use_input_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
      eRet = use_output_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
    }
    else
    {
      DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",(int)port);
      eRet = OMX_ErrorBadPortIndex;
    }
    DEBUG_PRINT_LOW("Use Buffer: port %u, buffer %p, eRet %d", port, *bufferHdr, eRet);

    if(eRet == OMX_ErrorNone)
    {
      if(allocate_done())
      {
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
      else if(port == OMX_CORE_OUTPUT_PORT_INDEX && m_out_bPopulated)
      {
        if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
        {
          BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
          post_event(OMX_CommandPortEnable,
                     OMX_CORE_OUTPUT_PORT_INDEX,
                     OMX_COMPONENT_GENERATE_EVENT);
          m_event_port_settings_sent = false;
        }
      }
    }
    return eRet;
}

OMX_ERRORTYPE omx_vdec::free_input_buffer(OMX_BUFFERHEADERTYPE *bufferHdr)
{
  unsigned int index = 0;

  if (bufferHdr == NULL || m_inp_mem_ptr == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  index = bufferHdr - m_inp_mem_ptr;
  DEBUG_PRINT_LOW("\n Free Input Buffer index = %d",index);

  if (index < m_inp_buf_count && driver_context.ptr_inputbuffer)
  {
    DEBUG_PRINT_LOW("\n Free Input Buffer index = %d",index);
    if (driver_context.ptr_inputbuffer[index].pmem_fd > 0)
    {
       struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
       struct vdec_setbuffer_cmd setbuffers;
       setbuffers.buffer_type = VDEC_BUFFER_TYPE_INPUT;
       memcpy (&setbuffers.buffer,&driver_context.ptr_inputbuffer[index],
          sizeof (vdec_bufferpayload));
       ioctl_msg.inputparam  = &setbuffers;
       ioctl_msg.outputparam = NULL;
       int ioctl_r = ioctl (driver_context.video_driver_fd,
                            VDEC_IOCTL_FREE_BUFFER, &ioctl_msg);
       if (ioctl_r < 0)
       {
          DEBUG_PRINT_ERROR("\nVDEC_IOCTL_FREE_BUFFER returned error %d", ioctl_r);
       }

       DEBUG_PRINT_LOW("\n unmap the input buffer fd=%d",
                    driver_context.ptr_inputbuffer[index].pmem_fd);
       DEBUG_PRINT_LOW("\n unmap the input buffer size=%d  address = %d",
                    driver_context.ptr_inputbuffer[index].mmaped_size,
                    driver_context.ptr_inputbuffer[index].bufferaddr);
       munmap (driver_context.ptr_inputbuffer[index].bufferaddr,
               driver_context.ptr_inputbuffer[index].mmaped_size);
       close (driver_context.ptr_inputbuffer[index].pmem_fd);
       driver_context.ptr_inputbuffer[index].pmem_fd = -1;
    }
  }

  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::free_output_buffer(OMX_BUFFERHEADERTYPE *bufferHdr)
{
  unsigned int index = 0;

  if (bufferHdr == NULL || m_out_mem_ptr == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  index = bufferHdr - m_out_mem_ptr;
  DEBUG_PRINT_LOW("\n Free ouput Buffer index = %d",index);

  if (index < m_out_buf_count && driver_context.ptr_outputbuffer)
  {
    DEBUG_PRINT_LOW("\n Free ouput Buffer index = %d addr = %x", index,
                    driver_context.ptr_outputbuffer[index].bufferaddr);

    if (!gate_output_buffers)
    {
      struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
      struct vdec_setbuffer_cmd setbuffers;
      setbuffers.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
      memcpy (&setbuffers.buffer,&driver_context.ptr_outputbuffer[index],
        sizeof (vdec_bufferpayload));
      ioctl_msg.inputparam  = &setbuffers;
      ioctl_msg.outputparam = NULL;
      DEBUG_PRINT_LOW("\nRelease the Output Buffer");
      if (ioctl (driver_context.video_driver_fd, VDEC_IOCTL_FREE_BUFFER,
            &ioctl_msg) < 0)
        DEBUG_PRINT_ERROR("\nRelease output buffer failed in VCD");
    }

    if (driver_context.ptr_outputbuffer[0].pmem_fd > 0)
    {
       DEBUG_PRINT_LOW("\n unmap the output buffer fd = %d",
                    driver_context.ptr_outputbuffer[0].pmem_fd);
       DEBUG_PRINT_LOW("\n unmap the ouput buffer size=%d  address = %d",
                    driver_context.ptr_outputbuffer[0].mmaped_size,
                    driver_context.ptr_outputbuffer[0].bufferaddr);
       munmap (driver_context.ptr_outputbuffer[0].bufferaddr,
               driver_context.ptr_outputbuffer[0].mmaped_size);
       close (driver_context.ptr_outputbuffer[0].pmem_fd);
       driver_context.ptr_outputbuffer[0].pmem_fd = -1;
    }
  }

  return OMX_ErrorNone;

}

OMX_ERRORTYPE omx_vdec::allocate_input_heap_buffer(OMX_HANDLETYPE       hComp,
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
                     calloc( (sizeof(OMX_BUFFERHEADERTYPE)), m_inp_buf_count);
    m_phdr_pmem_ptr = (OMX_BUFFERHEADERTYPE**) \
                     calloc( (sizeof(OMX_BUFFERHEADERTYPE*)), m_inp_buf_count);

    if (m_inp_heap_ptr == NULL)
    {
      DEBUG_PRINT_ERROR("\n m_inp_heap_ptr Allocation failed ");
      return OMX_ErrorInsufficientResources;
    }

    h264_scratch.nAllocLen = m_inp_buf_size;
    h264_scratch.pBuffer = (OMX_U8 *)malloc (m_inp_buf_size);
    h264_scratch.nFilledLen = 0;
    h264_scratch.nOffset = 0;

    if (h264_scratch.pBuffer == NULL)
    {
      DEBUG_PRINT_ERROR("\n h264_scratch.pBuffer Allocation failed ");
      return OMX_ErrorInsufficientResources;
    }

    if (m_frame_parser.mutils == NULL)
    {
       m_frame_parser.mutils = new H264_Utils();

       if (m_frame_parser.mutils == NULL)
       {
         DEBUG_PRINT_ERROR("\n parser utils Allocation failed ");
         return OMX_ErrorInsufficientResources;
       }

       m_frame_parser.mutils->initialize_frame_checking_environment();
       m_frame_parser.mutils->allocate_rbsp_buffer (m_inp_buf_size);
    }
  }

  /*Find a Free index*/
  for(i=0; i< m_inp_buf_count; i++)
  {
    if(BITMASK_ABSENT(&m_heap_inp_bm_count,i))
    {
      DEBUG_PRINT_LOW("\n Free Input Buffer Index %d",i);
      break;
    }
  }

  if (i < m_inp_buf_count)
  {
    buf_addr = (unsigned char *)malloc (m_inp_buf_size);

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
    input->nAllocLen         = m_inp_buf_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    DEBUG_PRINT_LOW("\n Address of Heap Buffer %p",*bufferHdr );
    eRet = allocate_input_buffer(hComp,&m_phdr_pmem_ptr [i],port,appData,bytes);
    DEBUG_PRINT_LOW("\n Address of Pmem Buffer %p",m_phdr_pmem_ptr [i] );
    /*Add the Buffers to freeq*/
    if (!m_input_free_q.insert_entry((unsigned)m_phdr_pmem_ptr [i],NULL,NULL))
    {
      DEBUG_PRINT_ERROR("\nERROR:Free_q is full");
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
  omx_vdec::AllocateInputBuffer

DESCRIPTION
  Helper function for allocate buffer in the input pin

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::allocate_input_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes)
{

  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  struct vdec_setbuffer_cmd setbuffers;
  OMX_BUFFERHEADERTYPE *input = NULL;
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  unsigned   i = 0;
  unsigned char *buf_addr = NULL;
  int pmem_fd = -1;

  if(bytes != m_inp_buf_size)
  {
    DEBUG_PRINT_LOW("\n Requested Size is wrong %d epected is %d",bytes,m_inp_buf_size);
    //return OMX_ErrorBadParameter;
  }

  if(!m_inp_mem_ptr)
  {
    DEBUG_PRINT_HIGH("\n Allocate i/p buffer case - Header List allocation");
    DEBUG_PRINT_LOW("\n Allocating input buffer count %d ",m_inp_buf_count);
    DEBUG_PRINT_LOW("\n Size of input buffer is %d",m_inp_buf_size);

    m_inp_mem_ptr = (OMX_BUFFERHEADERTYPE*) \
    calloc( (sizeof(OMX_BUFFERHEADERTYPE)), m_inp_buf_count);

    if (m_inp_mem_ptr == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    driver_context.ptr_inputbuffer = (struct vdec_bufferpayload *) \
    calloc ((sizeof (struct vdec_bufferpayload)),m_inp_buf_count);

    if (driver_context.ptr_inputbuffer == NULL)
    {
      return OMX_ErrorInsufficientResources;
    }

    for (i=0; i < m_inp_buf_count; i++)
    {
      driver_context.ptr_inputbuffer [i].pmem_fd = -1;
    }
  }

  for(i=0; i< m_inp_buf_count; i++)
  {
    if(BITMASK_ABSENT(&m_inp_bm_count,i))
    {
      DEBUG_PRINT_LOW("\n Free Input Buffer Index %d",i);
      break;
    }
  }

  if(i < m_inp_buf_count)
  {
    DEBUG_PRINT_LOW("\n Allocate input Buffer");
    pmem_fd = open ("/dev/pmem_adsp", O_RDWR, O_SYNC);

    if (pmem_fd < 0)
    {
      DEBUG_PRINT_ERROR("\n open failed for pmem/adsp for input buffer");
      return OMX_ErrorInsufficientResources;
    }

    if (pmem_fd == 0)
    {
      pmem_fd = open ("/dev/pmem_adsp", O_RDWR | O_SYNC);

      if (pmem_fd < 0)
      {
        DEBUG_PRINT_ERROR("\n open failed for pmem/adsp for input buffer");
        return OMX_ErrorInsufficientResources;
      }
    }

    if(!align_pmem_buffers(pmem_fd, m_inp_buf_size,
      driver_context.input_buffer.alignment))
    {
      DEBUG_PRINT_ERROR("\n align_pmem_buffers() failed");
      close(pmem_fd);
      return OMX_ErrorInsufficientResources;
    }

    buf_addr = (unsigned char *)mmap(NULL,m_inp_buf_size,PROT_READ|PROT_WRITE,
                    MAP_SHARED,pmem_fd,0);

    if (buf_addr == MAP_FAILED)
    {
      DEBUG_PRINT_ERROR("\n Map Failed to allocate input buffer");
      return OMX_ErrorInsufficientResources;
    }

    driver_context.ptr_inputbuffer [i].bufferaddr = buf_addr;
    driver_context.ptr_inputbuffer [i].pmem_fd = pmem_fd;
    driver_context.ptr_inputbuffer [i].buffer_len = m_inp_buf_size;
    driver_context.ptr_inputbuffer [i].mmaped_size = m_inp_buf_size;
    driver_context.ptr_inputbuffer [i].offset = 0;

    setbuffers.buffer_type = VDEC_BUFFER_TYPE_INPUT;
    memcpy (&setbuffers.buffer,&driver_context.ptr_inputbuffer [i],
            sizeof (vdec_bufferpayload));
    ioctl_msg.inputparam  = &setbuffers;
    ioctl_msg.outputparam = NULL;

    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER,
         &ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set Buffers Failed");
      return OMX_ErrorInsufficientResources;
    }

    *bufferHdr = (m_inp_mem_ptr + i);
    input = *bufferHdr;
    BITMASK_SET(&m_inp_bm_count,i);
    DEBUG_PRINT_LOW("\n Buffer address %p of pmem",*bufferHdr);

    input->pBuffer           = (OMX_U8 *)buf_addr;
    input->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
    input->nVersion.nVersion = OMX_SPEC_VERSION;
    input->nAllocLen         = m_inp_buf_size;
    input->pAppPrivate       = appData;
    input->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
    input->pInputPortPrivate = (void *)&driver_context.ptr_inputbuffer [i];
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR:Input Buffer Index not found");
    eRet = OMX_ErrorInsufficientResources;
  }
  return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdec::AllocateOutputBuffer

DESCRIPTION
  Helper fn for AllocateBuffer in the output pin

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything went well.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::allocate_output_buffer(
                         OMX_IN OMX_HANDLETYPE            hComp,
                         OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                         OMX_IN OMX_U32                   port,
                         OMX_IN OMX_PTR                   appData,
                         OMX_IN OMX_U32                   bytes)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE       *bufHdr= NULL; // buffer header
  unsigned                         i= 0; // Temporary counter
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  struct vdec_setbuffer_cmd setbuffers;

  if(!m_out_mem_ptr)
  {
    DEBUG_PRINT_HIGH("\n Allocate o/p buffer case - Header List allocation");
    int nBufHdrSize        = 0;
    int nPlatformEntrySize = 0;
    int nPlatformListSize  = 0;
    int nPMEMInfoSize = 0;
    int pmem_fd = -1;
    unsigned char *pmem_baseaddress = NULL;

    OMX_QCOM_PLATFORM_PRIVATE_LIST      *pPlatformList;
    OMX_QCOM_PLATFORM_PRIVATE_ENTRY     *pPlatformEntry;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

    DEBUG_PRINT_LOW("Allocating First Output Buffer(%d)\n",m_out_buf_count);
    nBufHdrSize        = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);

    nPMEMInfoSize      = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO);
    nPlatformListSize  = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_LIST);
    nPlatformEntrySize = m_out_buf_count *
                         sizeof(OMX_QCOM_PLATFORM_PRIVATE_ENTRY);

    DEBUG_PRINT_LOW("TotalBufHdr %d BufHdrSize %d PMEM %d PL %d\n",nBufHdrSize,
                         sizeof(OMX_BUFFERHEADERTYPE),
                         nPMEMInfoSize,
                         nPlatformListSize);
    DEBUG_PRINT_LOW("PE %d OutputBuffer Count %d \n",nPlatformEntrySize,
                         m_out_buf_count);

    pmem_fd = open ("/dev/pmem_adsp", O_RDWR | O_SYNC);

    if (pmem_fd < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR:pmem fd for output buffer %d",m_out_buf_size);
      return OMX_ErrorInsufficientResources;
    }

    if(pmem_fd == 0)
    {
      pmem_fd = open ("/dev/pmem_adsp", O_RDWR | O_SYNC);

      if (pmem_fd < 0)
      {
        DEBUG_PRINT_ERROR("\nERROR:pmem fd for output buffer %d",m_out_buf_size);
        return OMX_ErrorInsufficientResources;
      }
    }

    if(!align_pmem_buffers(pmem_fd, m_out_buf_size * m_out_buf_count,
      driver_context.output_buffer.alignment))
    {
      DEBUG_PRINT_ERROR("\n align_pmem_buffers() failed");
      close(pmem_fd);
      return OMX_ErrorInsufficientResources;
    }

    pmem_baseaddress = (unsigned char *)mmap(NULL,(m_out_buf_size * m_out_buf_count),
                       PROT_READ|PROT_WRITE,MAP_SHARED,pmem_fd,0);
    m_heap_ptr = new VideoHeap (pmem_fd,
                                   m_out_buf_size*m_out_buf_count,
                                   pmem_baseaddress);


    if (pmem_baseaddress == MAP_FAILED)
    {
      DEBUG_PRINT_ERROR("\n MMAP failed for Size %d",m_out_buf_size);
      return OMX_ErrorInsufficientResources;
    }
    m_out_mem_ptr = (OMX_BUFFERHEADERTYPE  *)calloc(nBufHdrSize,1);
    // Alloc mem for platform specific info
    char *pPtr=NULL;
    pPtr = (char*) calloc(nPlatformListSize + nPlatformEntrySize +
                                     nPMEMInfoSize,1);
    driver_context.ptr_outputbuffer = (struct vdec_bufferpayload *) \
      calloc (sizeof(struct vdec_bufferpayload),m_out_buf_count);
    driver_context.ptr_respbuffer = (struct vdec_output_frameinfo  *)\
      calloc (sizeof (struct vdec_output_frameinfo),m_out_buf_count);

    if(m_out_mem_ptr && pPtr && driver_context.ptr_outputbuffer
       && driver_context.ptr_respbuffer)
    {
      driver_context.ptr_outputbuffer[0].mmaped_size =
        (m_out_buf_size * m_out_buf_count);
      bufHdr          =  m_out_mem_ptr;
      m_platform_list = (OMX_QCOM_PLATFORM_PRIVATE_LIST *)(pPtr);
      m_platform_entry= (OMX_QCOM_PLATFORM_PRIVATE_ENTRY *)
                        (((char *) m_platform_list)  + nPlatformListSize);
      m_pmem_info     = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                        (((char *) m_platform_entry) + nPlatformEntrySize);
      pPlatformList   = m_platform_list;
      pPlatformEntry  = m_platform_entry;
      pPMEMInfo       = m_pmem_info;

      DEBUG_PRINT_LOW("Memory Allocation Succeeded for OUT port%p\n",m_out_mem_ptr);

      // Settting the entire storage nicely
      DEBUG_PRINT_LOW("bHdr %p OutMem %p PE %p\n",bufHdr, m_out_mem_ptr,pPlatformEntry);
      DEBUG_PRINT_LOW(" Pmem Info = %p \n",pPMEMInfo);
      for(i=0; i < m_out_buf_count ; i++)
      {
        bufHdr->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        bufHdr->nVersion.nVersion  = OMX_SPEC_VERSION;
        // Set the values when we determine the right HxW param
        bufHdr->nAllocLen          = bytes;
        bufHdr->nFilledLen         = 0;
        bufHdr->pAppPrivate        = appData;
        bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
        // Platform specific PMEM Information
        // Initialize the Platform Entry
        //DEBUG_PRINT_LOW("Initializing the Platform Entry for %d\n",i);
        pPlatformEntry->type       = OMX_QCOM_PLATFORM_PRIVATE_PMEM;
        pPlatformEntry->entry      = pPMEMInfo;
        // Initialize the Platform List
        pPlatformList->nEntries    = 1;
        pPlatformList->entryList   = pPlatformEntry;
        // Keep pBuffer NULL till vdec is opened
        bufHdr->pBuffer            = NULL;
        bufHdr->nOffset            = 0;

        pPMEMInfo->offset          =  m_out_buf_size*i;
        pPMEMInfo->pmem_fd = 0;
        bufHdr->pPlatformPrivate = pPlatformList;

        driver_context.ptr_outputbuffer[i].pmem_fd = pmem_fd;

        /*Create a mapping between buffers*/
        bufHdr->pOutputPortPrivate = &driver_context.ptr_respbuffer[i];
        driver_context.ptr_respbuffer[i].client_data = (void *) \
                                            &driver_context.ptr_outputbuffer[i];
        driver_context.ptr_outputbuffer[i].offset = m_out_buf_size*i;
        driver_context.ptr_outputbuffer[i].bufferaddr = \
          pmem_baseaddress + (m_out_buf_size*i);

        DEBUG_PRINT_LOW("\n pmem_fd = %d offset = %d address = %p",\
                     pmem_fd,driver_context.ptr_outputbuffer[i].offset,driver_context.ptr_outputbuffer[i].bufferaddr);
        // Move the buffer and buffer header pointers
        bufHdr++;
        pPMEMInfo++;
        pPlatformEntry++;
        pPlatformList++;
      }
    }
    else
    {
      DEBUG_PRINT_ERROR("Output buf mem alloc failed[0x%x][0x%x]\n",\
                                        m_out_mem_ptr, pPtr);
      if(m_out_mem_ptr)
      {
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
      }
      if(pPtr)
      {
        free(pPtr);
        pPtr = NULL;
      }
      if(driver_context.ptr_outputbuffer)
      {
        free(driver_context.ptr_outputbuffer);
        driver_context.ptr_outputbuffer = NULL;
      }
      if(driver_context.ptr_respbuffer)
      {
        free(driver_context.ptr_respbuffer);
        driver_context.ptr_respbuffer = NULL;
      }
      eRet =  OMX_ErrorInsufficientResources;
    }
  }

  for(i=0; i< m_out_buf_count; i++)
  {
    if(BITMASK_ABSENT(&m_out_bm_count,i))
    {
      DEBUG_PRINT_LOW("\n Found a Free Output Buffer %d",i);
      break;
    }
  }

  if (eRet == OMX_ErrorNone)
  {
    if(i < m_out_buf_count)
    {
      m_pmem_info[i].offset = driver_context.ptr_outputbuffer[i].offset;
      m_pmem_info[i].pmem_fd = (OMX_U32) m_heap_ptr.get ();
      driver_context.ptr_outputbuffer[i].buffer_len = m_out_buf_size;
      //driver_context.ptr_outputbuffer[i].mmaped_size = m_out_buf_size;
     if(!gate_output_buffers)
     {
     setbuffers.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
     memcpy (&setbuffers.buffer,&driver_context.ptr_outputbuffer [i],
             sizeof (vdec_bufferpayload));
     ioctl_msg.inputparam  = &setbuffers;
     ioctl_msg.outputparam = NULL;

     DEBUG_PRINT_LOW("\n Set the Output Buffer");
     if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER,
          &ioctl_msg) < 0)
     {
       DEBUG_PRINT_ERROR("\n Set output buffer failed");
       return OMX_ErrorInsufficientResources;
     }
     }

      // found an empty buffer at i
      *bufferHdr = (m_out_mem_ptr + i );
      (*bufferHdr)->pBuffer = driver_context.ptr_outputbuffer[i].bufferaddr;
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
  omx_vdec::AllocateBuffer

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::allocate_buffer(OMX_IN OMX_HANDLETYPE                hComp,
                                     OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                     OMX_IN OMX_U32                        port,
                                     OMX_IN OMX_PTR                     appData,
                                     OMX_IN OMX_U32                       bytes)
{
    unsigned i = 0;
    OMX_ERRORTYPE eRet = OMX_ErrorNone; // OMX return type

    DEBUG_PRINT_LOW("\n Allocate buffer on port %d \n", (int)port);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Allocate Buf in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
      if (arbitrary_bytes)
      {
          eRet = allocate_input_heap_buffer (hComp,bufferHdr,port,appData,bytes);
      }
      else
      {
        eRet = allocate_input_buffer(hComp,bufferHdr,port,appData,bytes);
      }
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
                m_event_port_settings_sent = false;
            }
        }
    }
    DEBUG_PRINT_LOW("Allocate Buffer exit with ret Code %d\n",eRet);
    return eRet;
}

// Free Buffer - API call
/* ======================================================================
FUNCTION
  omx_vdec::FreeBuffer

DESCRIPTION

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::free_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                      OMX_IN OMX_U32                 port,
                                      OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    unsigned int nPortIndex;

    DEBUG_PRINT_LOW("In for decoder free_buffer \n");

    if(m_state == OMX_StateIdle &&
       (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        DEBUG_PRINT_LOW(" free buffer while Component in Loading pending\n");
    }
    else if((m_inp_bEnabled == OMX_FALSE && port == OMX_CORE_INPUT_PORT_INDEX)||
            (m_out_bEnabled == OMX_FALSE && port == OMX_CORE_OUTPUT_PORT_INDEX))
    {
        DEBUG_PRINT_LOW("Free Buffer while port %d disabled\n", port);
    }
    else if(m_state == OMX_StateExecuting || m_state == OMX_StatePause)
    {
        DEBUG_PRINT_ERROR("Invalid state to free buffer,ports need to be disabled\n");
        post_event(OMX_EventError,
                   OMX_ErrorPortUnpopulated,
                   OMX_COMPONENT_GENERATE_EVENT);

        return eRet;
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
      if (!arbitrary_bytes)
      {
       // check if the buffer is valid
        nPortIndex = buffer - m_inp_mem_ptr;
      }
      else
      {
         nPortIndex = buffer - m_inp_heap_ptr;
      }

        DEBUG_PRINT_LOW("free_buffer on i/p port - Port idx %d \n", nPortIndex);
        if(nPortIndex < m_inp_buf_count)
        {
          // Clear the bit associated with it.
          BITMASK_CLEAR(&m_inp_bm_count,nPortIndex);

          if (arbitrary_bytes)
          {
            if (m_inp_heap_ptr[nPortIndex].pBuffer)
            {
              BITMASK_CLEAR(&m_heap_inp_bm_count,nPortIndex);
              DEBUG_PRINT_LOW("\n Free Heap Buffer index %d",nPortIndex);
              free (m_inp_heap_ptr[nPortIndex].pBuffer);
              m_inp_heap_ptr[nPortIndex].pBuffer = NULL;
            }
            if (m_phdr_pmem_ptr[nPortIndex])
            {
              DEBUG_PRINT_LOW("\n Free pmem Buffer index %d",nPortIndex);
              free_input_buffer(m_phdr_pmem_ptr[nPortIndex]);
            }
          }
          else
          {
            free_input_buffer(buffer);
          }

          m_inp_bPopulated = OMX_FALSE;

          /*Free the Buffer Header*/
          if (release_input_done())
          {
            DEBUG_PRINT_HIGH("\n ALL input buffers are freed/released");
            input_use_buffer = false;
            if (arbitrary_bytes)
            {
              if (m_frame_parser.mutils)
              {
                DEBUG_PRINT_LOW("\n Free utils parser");
                delete (m_frame_parser.mutils);
                m_frame_parser.mutils = NULL;
              }

              if (m_inp_heap_ptr)
              {
                DEBUG_PRINT_LOW("\n Free input Heap Pointer");
                free (m_inp_heap_ptr);
                m_inp_heap_ptr = NULL;
              }

              if (m_phdr_pmem_ptr)
              {
                DEBUG_PRINT_LOW("\n Free input pmem header Pointer");
                free (m_phdr_pmem_ptr);
                m_phdr_pmem_ptr = NULL;
              }
            }
            if (m_inp_mem_ptr)
            {
              DEBUG_PRINT_LOW("\n Free input pmem Pointer area");
              free (m_inp_mem_ptr);
              m_inp_mem_ptr = NULL;
            }

            if (driver_context.ptr_inputbuffer)
            {
              DEBUG_PRINT_LOW("\n Free Driver Context pointer");
              free (driver_context.ptr_inputbuffer);
              driver_context.ptr_inputbuffer = NULL;
            }
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
        if(nPortIndex < m_out_buf_count)
        {
            DEBUG_PRINT_LOW("free_buffer on o/p port - Port idx %d \n", nPortIndex);
            // Clear the bit associated with it.
            BITMASK_CLEAR(&m_out_bm_count,nPortIndex);
            m_out_bPopulated = OMX_FALSE;
            free_output_buffer (buffer);

            if (release_output_done())
            {
              DEBUG_PRINT_HIGH("\n ALL output buffers are freed/released");
              output_use_buffer = false;
              if (m_out_mem_ptr)
              {
                free (m_out_mem_ptr);
                m_out_mem_ptr = NULL;
              }
              if (driver_context.ptr_respbuffer)
              {
                free (driver_context.ptr_respbuffer);
                driver_context.ptr_respbuffer = NULL;
              }
              if (driver_context.ptr_outputbuffer)
              {
                free (driver_context.ptr_outputbuffer);
                driver_context.ptr_outputbuffer = NULL;
              }
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("Error: free_buffer , Port Index Invalid\n");
            eRet = OMX_ErrorBadPortIndex;
        }
        if(BITMASK_PRESENT((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING)
           && release_output_done() )
        {
            DEBUG_PRINT_LOW("FreeBuffer : If any Disable event pending,post it\n");

                DEBUG_PRINT_LOW("MOVING TO DISABLED STATE \n");
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
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
  omx_vdec::EmptyThisBuffer

DESCRIPTION
  This routine is used to push the encoded video frames to
  the video decoder.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::empty_this_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                           OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
  OMX_ERRORTYPE ret1 = OMX_ErrorNone;
  unsigned int nBufferIndex = m_inp_buf_count;

  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("Empty this buffer in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  if (buffer == NULL)
  {
    DEBUG_PRINT_ERROR("\nERROR:ETB Buffer is NULL");
    return OMX_ErrorBadParameter;
  }

  if (arbitrary_bytes)
  {
    nBufferIndex = buffer - m_inp_heap_ptr;
  }
  else
  {
    nBufferIndex = buffer - m_inp_mem_ptr;
  }

  if (nBufferIndex > m_inp_buf_count )
  {
    DEBUG_PRINT_ERROR("\nERROR:ETB nBufferIndex is invalid");
    return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_LOW("\n ETB: bufhdr = %p, bufhdr->pBuffer = %p", buffer, buffer->pBuffer);
  if (arbitrary_bytes)
  {
    post_event ((unsigned)hComp,(unsigned)buffer,
                OMX_COMPONENT_GENERATE_ETB_ARBITRARY);
  }
  else
  {
    post_event ((unsigned)hComp,(unsigned)buffer,OMX_COMPONENT_GENERATE_ETB);
  }
  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::empty_this_buffer_proxy

DESCRIPTION
  This routine is used to push the encoded video frames to
  the video decoder.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::empty_this_buffer_proxy(OMX_IN OMX_HANDLETYPE         hComp,
                                                 OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
  int push_cnt = 0,i=0;
  unsigned nPortIndex = 0;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  struct vdec_input_frameinfo frameinfo;
  struct vdec_bufferpayload *temp_buffer;
  struct vdec_ioctl_msg ioctl_msg;
  struct vdec_seqheader seq_header;
  bool port_setting_changed = true;

  /*Should we generate a Aync error event*/
  if (buffer == NULL || buffer->pInputPortPrivate == NULL)
  {
    DEBUG_PRINT_ERROR("\nERROR:empty_this_buffer_proxy is invalid");
    return OMX_ErrorBadParameter;
  }

  nPortIndex = buffer-((OMX_BUFFERHEADERTYPE *)m_inp_mem_ptr);

  if (nPortIndex > m_inp_buf_count)
  {
    DEBUG_PRINT_ERROR("\nERROR:empty_this_buffer_proxy invalid nPortIndex[%u]",
        nPortIndex);
    return OMX_ErrorBadParameter;
  }

  pending_input_buffers++;

  if( input_flush_progress == true || m_ineos_reached == 1)
  {
    DEBUG_PRINT_LOW("\n Flush in progress return buffer ");
    post_event ((unsigned int)buffer,VDEC_S_SUCCESS,
                     OMX_COMPONENT_GENERATE_EBD);
    return OMX_ErrorNone;
  }

  if(m_event_port_settings_sent && !arbitrary_bytes)
  {
    post_event((unsigned)hComp,(unsigned)buffer,OMX_COMPONENT_GENERATE_ETB);
    return OMX_ErrorNone;
  }

  temp_buffer = (struct vdec_bufferpayload *)buffer->pInputPortPrivate;

  if ((temp_buffer -  driver_context.ptr_inputbuffer) > m_inp_buf_count)
  {
    return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_LOW("\n ETBProxy: bufhdr = %p, bufhdr->pBuffer = %p", buffer, buffer->pBuffer);
  /*for use buffer we need to memcpy the data*/
  temp_buffer->buffer_len = buffer->nFilledLen;

  if (input_use_buffer)
  {
    if (buffer->nFilledLen <= temp_buffer->buffer_len)
    {
      memcpy (temp_buffer->bufferaddr,(buffer->pBuffer + buffer->nOffset),
              buffer->nFilledLen);
    }
    else
    {
      return OMX_ErrorBadParameter;
    }

  }

  if (!arbitrary_bytes && first_frame < 2  && codec_type_parse == CODEC_TYPE_MPEG4)
  {

    if (first_frame == 0)
    {
       first_buffer = (unsigned char *)malloc (m_inp_buf_size);
       DEBUG_PRINT_LOW("\n Copied the first buffer data size %d ",
                    temp_buffer->buffer_len);
       first_frame = 1;
       memcpy (first_buffer,temp_buffer->bufferaddr,temp_buffer->buffer_len);
       first_frame_size = buffer->nFilledLen;
       buffer->nFilledLen = 0;
       post_event ((unsigned int)buffer,VDEC_S_SUCCESS,
                   OMX_COMPONENT_GENERATE_EBD);
       return OMX_ErrorNone;
    }
    else if (first_frame == 1)
    {
       first_frame = 2;
       DEBUG_PRINT_LOW("\n Second buffer copy the header size %d frame size %d",
                    first_frame_size,temp_buffer->buffer_len);
       memcpy (&first_buffer [first_frame_size],temp_buffer->bufferaddr,
               temp_buffer->buffer_len);
       first_frame_size += temp_buffer->buffer_len;
       memcpy (temp_buffer->bufferaddr,first_buffer,first_frame_size);
       temp_buffer->buffer_len = first_frame_size;
       free (first_buffer);
    }
  }

  frameinfo.bufferaddr = temp_buffer->bufferaddr;
  frameinfo.client_data = (void *) buffer;
  frameinfo.datalen = temp_buffer->buffer_len;
  frameinfo.flags = 0;
  frameinfo.offset = buffer->nOffset;
  frameinfo.pmem_fd = temp_buffer->pmem_fd;
  frameinfo.pmem_offset = temp_buffer->offset;
  frameinfo.timestamp = buffer->nTimeStamp;

#if BITSTREAM_LOG
  int bytes_written;
  bytes_written = fwrite((const char *)temp_buffer->bufferaddr,
                          temp_buffer->buffer_len,1,outputBufferFile1);

#endif

  if(!set_seq_header_done)
  {
    set_seq_header_done = true;
    DEBUG_PRINT_HIGH("\n Set Sequence Header");
    seq_header.ptr_seqheader = frameinfo.bufferaddr;
    seq_header.seq_header_len = frameinfo.datalen;
    seq_header.pmem_fd = frameinfo.pmem_fd;
    seq_header.pmem_offset = frameinfo.pmem_offset;
    ioctl_msg.inputparam = &seq_header;
    ioctl_msg.outputparam = NULL;
    if (ioctl(driver_context.video_driver_fd,VDEC_IOCTL_SET_SEQUENCE_HEADER,
              &ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set Sequence Header Failed");
      /*Generate an async error and move to invalid state*/
      return OMX_ErrorBadParameter;
    }
    if(omx_vdec_check_port_settings (&port_setting_changed) != OMX_ErrorNone)
    {
      DEBUG_PRINT_ERROR("\n Check port setting failed");
      return OMX_ErrorBadParameter;
    }

    if(port_setting_changed)
    {
      DEBUG_PRINT_HIGH("\n Port settings changed");
      m_event_port_settings_sent = true;
      m_cb.EventHandler(&m_cmp, m_app_data,OMX_EventPortSettingsChanged,
                          OMX_CORE_OUTPUT_PORT_INDEX, 0, NULL );
      DEBUG_PRINT_HIGH("\n EventHandler for Port Setting changed done");
      return OMX_ErrorNone;
    }
    else
    {
      if(!register_output_buffers())
      {
        DEBUG_PRINT_ERROR("\n register output failed");
        return OMX_ErrorBadParameter;
      }
      DEBUG_PRINT_HIGH("\n Port settings Not changed");
    }
  }

  if (temp_buffer->buffer_len == 0 || (buffer->nFlags & 0x01))
  {
    DEBUG_PRINT_HIGH("\n Rxd i/p EOS, Notify Driver that EOS has been reached");
    frameinfo.flags |= VDEC_BUFFERFLAG_EOS;
    m_ineos_reached = 1;
  }

  sent_first_frame = true;
  DEBUG_PRINT_LOW("\n Decode Input Frame Size %d",frameinfo.datalen);
  ioctl_msg.inputparam = &frameinfo;
  ioctl_msg.outputparam = NULL;

  if (ioctl(driver_context.video_driver_fd,VDEC_IOCTL_DECODE_FRAME,
            &ioctl_msg) < 0)
  {
    /*Generate an async error and move to invalid state*/
    return OMX_ErrorBadParameter;
  }

  return ret;
}

/* ======================================================================
FUNCTION
  omx_vdec::FillThisBuffer

DESCRIPTION
  IL client uses this method to release the frame buffer
  after displaying them.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::fill_this_buffer(OMX_IN OMX_HANDLETYPE  hComp,
                                          OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{

  if(m_state == OMX_StateInvalid)
  {
      DEBUG_PRINT_ERROR("FTB in Invalid State\n");
      return OMX_ErrorInvalidState;
  }

  if (buffer == NULL || ((buffer - m_out_mem_ptr) > m_out_buf_size))
  {
    return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_LOW("\n FTB: bufhdr = %p, bufhdr->pBuffer = %p", buffer, buffer->pBuffer);
  post_event((unsigned) hComp, (unsigned)buffer,OMX_COMPONENT_GENERATE_FTB);
  return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdec::fill_this_buffer_proxy

DESCRIPTION
  IL client uses this method to release the frame buffer
  after displaying them.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_vdec::fill_this_buffer_proxy(
                         OMX_IN OMX_HANDLETYPE        hComp,
                         OMX_IN OMX_BUFFERHEADERTYPE* bufferAdd)
{
  OMX_ERRORTYPE nRet = OMX_ErrorNone;
  struct vdec_ioctl_msg ioctl_msg = {NULL,NULL};
  OMX_BUFFERHEADERTYPE *buffer = bufferAdd;
  struct vdec_fillbuffer_cmd fillbuffer;
  struct vdec_bufferpayload     *ptr_outputbuffer = NULL;
  struct vdec_output_frameinfo  *ptr_respbuffer = NULL;


  if (bufferAdd == NULL || ((buffer - m_out_mem_ptr) > m_out_buf_count) )
  {
    return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_LOW("\n FTBProxy: bufhdr = %p, bufhdr->pBuffer = %p",
      bufferAdd, bufferAdd->pBuffer);
  /*Return back the output buffer to client*/
  if( (m_event_port_settings_sent == true) || (m_out_bEnabled != OMX_TRUE)
      || output_flush_progress == true || m_outeos_reached == 1)
  {
    DEBUG_PRINT_LOW("\n Output Buffers return in EOS condition");
    buffer->nFlags |= m_outeos_reached;
    m_cb.FillBufferDone (hComp,m_app_data,buffer);
    return OMX_ErrorNone;
  }
  pending_output_buffers++;
  ptr_respbuffer = (struct vdec_output_frameinfo*)buffer->pOutputPortPrivate;
  if (ptr_respbuffer)
  {
    ptr_outputbuffer =  (struct vdec_bufferpayload*)ptr_respbuffer->client_data;
  }

  if (ptr_respbuffer == NULL || ptr_outputbuffer == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  memcpy (&fillbuffer.buffer,ptr_outputbuffer,\
          sizeof(struct vdec_bufferpayload));
  fillbuffer.client_data = bufferAdd;

  ioctl_msg.inputparam = &fillbuffer;
  ioctl_msg.outputparam = NULL;
  if (ioctl (driver_context.video_driver_fd,
         VDEC_IOCTL_FILL_OUTPUT_BUFFER,&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\n Decoder frame failed");
    m_cb.FillBufferDone (hComp,m_app_data,buffer);
    return OMX_ErrorBadParameter;
  }

  if(gate_input_buffers)
  {
    gate_input_buffers = false;
    if(pdest_frame)
    {
      /*Push the frame to the Decoder*/
      if (empty_this_buffer_proxy(hComp,pdest_frame) != OMX_ErrorNone)
      {
        return OMX_ErrorBadParameter;
      }
      frame_count++;
      pdest_frame = NULL;
    }
  }
  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::SetCallbacks

DESCRIPTION
  Set the callbacks.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::set_callbacks(OMX_IN OMX_HANDLETYPE        hComp,
                                           OMX_IN OMX_CALLBACKTYPE* callbacks,
                                           OMX_IN OMX_PTR             appData)
{

  m_cb       = *callbacks;
  DEBUG_PRINT_LOW("\n Callbacks Set %p %p %p",m_cb.EmptyBufferDone,\
               m_cb.EventHandler,m_cb.FillBufferDone);
  m_app_data =    appData;
  return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdec::ComponentDeInit

DESCRIPTION
  Destroys the component and release memory allocated to the heap.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    int i = 0;
    if (OMX_StateLoaded != m_state)
    {
        DEBUG_PRINT_ERROR("WARNING:Rxd DeInit,OMX not in LOADED state %d\n",\
                          m_state);
        DEBUG_PRINT_ERROR("\nPlayback Ended - FAILED");
    }
    else
    {
      DEBUG_PRINT_HIGH("\n Playback Ended - PASSED");
    }

    /*Check if the output buffers have to be cleaned up*/
    if(m_out_mem_ptr)
    {
        DEBUG_PRINT_LOW("Freeing the Output Memory\n");
        for (i=0; i<m_out_buf_count; i++ )
        {
          free_output_buffer (&m_out_mem_ptr[i]);
        }
        if (driver_context.ptr_outputbuffer)
        {
          free (driver_context.ptr_outputbuffer);
          driver_context.ptr_outputbuffer = NULL;
        }

        if (driver_context.ptr_respbuffer)
        {
          free (driver_context.ptr_respbuffer);
          driver_context.ptr_respbuffer = NULL;
        }
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
    }

    /*Check if the input buffers have to be cleaned up*/
    if(m_inp_mem_ptr)
    {
        DEBUG_PRINT_LOW("Freeing the Input Memory\n");
        for (i=0; i<m_inp_buf_count; i++ )
        {
          free_input_buffer (&m_inp_mem_ptr[i]);
        }

        if (driver_context.ptr_inputbuffer)
        {
          free (driver_context.ptr_inputbuffer);
          driver_context.ptr_inputbuffer = NULL;
        }

        free(m_inp_mem_ptr);
        m_inp_mem_ptr = NULL;
    }

    if(h264_scratch.pBuffer)
    {
      free(h264_scratch.pBuffer);
      h264_scratch.pBuffer = NULL;
    }

    if(m_platform_list)
    {
        free(m_platform_list);
        m_platform_list = NULL;
    }
    if(m_vendor_config.pData)
    {
        free(m_vendor_config.pData);
        m_vendor_config.pData = NULL;
    }

    // Reset counters in mesg queues
    m_ftb_q.m_size=0;
    m_cmd_q.m_size=0;
    m_etb_q.m_size=0;
    m_ftb_q.m_read = m_ftb_q.m_write =0;
    m_cmd_q.m_read = m_cmd_q.m_write =0;
    m_etb_q.m_read = m_etb_q.m_write =0;

    DEBUG_PRINT_LOW("\n Calling VDEC_IOCTL_STOP_NEXT_MSG");
    (void)ioctl(driver_context.video_driver_fd, VDEC_IOCTL_STOP_NEXT_MSG,
        NULL);
    DEBUG_PRINT_HIGH("\n Close the driver instance");
    close(driver_context.video_driver_fd);

#if BITSTREAM_LOG
    fclose (outputBufferFile1);
#endif
#ifdef _ANDROID_
    //for (i=0; i<m_out_buf_count; i++ )
    {
       // Clear the strong reference
      m_heap_ptr.clear();
    }
#endif // _ANDROID_
  DEBUG_PRINT_HIGH("\n omx_vdec::component_deinit() complete");
  return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::UseEGLImage

DESCRIPTION
  OMX Use EGL Image method implementation <TBD>.

PARAMETERS
  <TBD>.

RETURN VALUE
  Not Implemented error.

========================================================================== */
OMX_ERRORTYPE  omx_vdec::use_EGL_image(OMX_IN OMX_HANDLETYPE                hComp,
                                          OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                          OMX_IN OMX_U32                        port,
                                          OMX_IN OMX_PTR                     appData,
                                          OMX_IN void*                      eglImage)
{
    DEBUG_PRINT_ERROR("Error : use_EGL_image:  Not Implemented \n");
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_vdec::ComponentRoleEnum

DESCRIPTION
  OMX Component Role Enum method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_vdec::component_role_enum(OMX_IN OMX_HANDLETYPE hComp,
                                                OMX_OUT OMX_U8*        role,
                                                OMX_IN OMX_U32        index)
{
  OMX_ERRORTYPE eRet = OMX_ErrorNone;

  if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
  {
    if((0 == index) && role)
    {
      strncpy((char *)role, "video_decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE);
      DEBUG_PRINT_LOW("component_role_enum: role %s\n",role);
    }
    else
    {
      eRet = OMX_ErrorNoMore;
    }
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.h263",OMX_MAX_STRINGNAME_SIZE))
  {
    if((0 == index) && role)
    {
      strncpy((char *)role, "video_decoder.h263",OMX_MAX_STRINGNAME_SIZE);
      DEBUG_PRINT_LOW("component_role_enum: role %s\n",role);
    }
    else
    {
      DEBUG_PRINT_LOW("\n No more roles \n");
      eRet = OMX_ErrorNoMore;
    }
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.avc",OMX_MAX_STRINGNAME_SIZE))
  {
    if((0 == index) && role)
    {
      strncpy((char *)role, "video_decoder.avc",OMX_MAX_STRINGNAME_SIZE);
      DEBUG_PRINT_LOW("component_role_enum: role %s\n",role);
    }
    else
    {
      DEBUG_PRINT_LOW("\n No more roles \n");
      eRet = OMX_ErrorNoMore;
    }
  }
  else if(!strncmp(driver_context.kind, "OMX.qcom.video.decoder.vc1",OMX_MAX_STRINGNAME_SIZE))
  {
    if((0 == index) && role)
    {
      strncpy((char *)role, "video_decoder.vc1",OMX_MAX_STRINGNAME_SIZE);
      DEBUG_PRINT_LOW("component_role_enum: role %s\n",role);
    }
    else
    {
      DEBUG_PRINT_LOW("\n No more roles \n");
      eRet = OMX_ErrorNoMore;
    }
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR:Querying Role on Unknown Component\n");
    eRet = OMX_ErrorInvalidComponentName;
  }
  return eRet;
}




/* ======================================================================
FUNCTION
  omx_vdec::AllocateDone

DESCRIPTION
  Checks if entire buffer pool is allocated by IL Client or not.
  Need this to move to IDLE state.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdec::allocate_done(void)
{
  bool bRet = false;
  bool bRet_In = false;
  bool bRet_Out = false;

  bRet_In = allocate_input_done();
  bRet_Out = allocate_output_done();

  if(bRet_In && bRet_Out)
  {
      bRet = true;
  }

  return bRet;
}
/* ======================================================================
FUNCTION
  omx_vdec::AllocateInputDone

DESCRIPTION
  Checks if I/P buffer pool is allocated by IL Client or not.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdec::allocate_input_done(void)
{
  bool bRet = false;
  unsigned i=0;

  if (m_inp_mem_ptr == NULL)
  {
      return bRet;
  }
  if(m_inp_mem_ptr )
  {
    for(;i<m_inp_buf_count;i++)
    {
      if(BITMASK_ABSENT(&m_inp_bm_count,i))
      {
        break;
      }
    }
  }
  if(i==m_inp_buf_count)
  {
    bRet = true;
    DEBUG_PRINT_HIGH("\n Allocate done for all i/p buffers");
  }
  if(i==m_inp_buf_count && m_inp_bEnabled)
  {
     m_inp_bPopulated = OMX_TRUE;
  }
  return bRet;
}
/* ======================================================================
FUNCTION
  omx_vdec::AllocateOutputDone

DESCRIPTION
  Checks if entire O/P buffer pool is allocated by IL Client or not.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_vdec::allocate_output_done(void)
{
  bool bRet = false;
  unsigned j=0;

  if (m_out_mem_ptr == NULL)
  {
      return bRet;
  }

  if(m_out_mem_ptr )
  {
    for(;j<m_out_buf_count;j++)
    {
      if(BITMASK_ABSENT(&m_out_bm_count,j))
      {
        break;
      }
    }
  }

  if(j==m_out_buf_count)
  {
    bRet = true;
    DEBUG_PRINT_HIGH("\n Allocate done for all o/p buffers");
  }

  if(j==m_out_buf_count && m_out_bEnabled)
  {
     m_out_bPopulated = OMX_TRUE;
  }
  return bRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::ReleaseDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec::release_done(void)
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
  omx_vdec::ReleaseOutputDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec::release_output_done(void)
{
  bool bRet = false;
  unsigned i=0,j=0;

  DEBUG_PRINT_LOW("\n Value of m_out_mem_ptr %p",m_inp_mem_ptr);
  if(m_out_mem_ptr)
  {
      for(;j<m_out_buf_count;j++)
      {
        if(BITMASK_PRESENT(&m_out_bm_count,j))
        {
          break;
        }
      }
    if(j==m_out_buf_count)
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
  omx_vdec::ReleaseInputDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec::release_input_done(void)
{
  bool bRet = false;
  unsigned i=0,j=0;

  DEBUG_PRINT_LOW("\n Value of m_inp_mem_ptr %p",m_inp_mem_ptr);
  if(m_inp_mem_ptr)
  {
      for(;j<m_inp_buf_count;j++)
      {
        if( BITMASK_PRESENT(&m_inp_bm_count,j))
        {
          break;
        }
      }
    if(j==m_inp_buf_count)
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

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_check_port_settings

DESCRIPTION
  Parse meta data to check the height and width param
  Check the level and profile

PARAMETERS
  None.

RETURN VALUE
  OMX_ErrorNone, if profile and level are supported
  OMX_ErrorUnsupportedSetting, if profile and level are not supported
========================================================================== */
OMX_ERRORTYPE omx_vdec::omx_vdec_check_port_settings(bool *port_setting_changed)
{
    struct vdec_ioctl_msg ioctl_msg;
    unsigned int alignment = 0,buffer_size = 0;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    *port_setting_changed = false;
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = &driver_context.video_resoultion;
    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_PICRES,&ioctl_msg))
    {
      DEBUG_PRINT_ERROR("\n Error in VDEC_IOCTL_GET_PICRES in port_setting");
      return OMX_ErrorHardware;
    }

    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = &driver_context.output_buffer;
    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_GET_BUFFER_REQ,
               &ioctl_msg))
    {
      DEBUG_PRINT_ERROR("\n Error in VDEC_IOCTL_GET_BUFFER_REQ in port_setting");
      return OMX_ErrorHardware;
    }
    DEBUG_PRINT_HIGH("\n Queried Dimensions H=%d W=%d Act H=%d W=%d",
                       driver_context.video_resoultion.frame_height,
                       driver_context.video_resoultion.frame_width,
                       m_height,m_width);
    DEBUG_PRINT_HIGH("\n Queried Buffer ACnt=%d BSiz=%d Act Acnt=%d Bsiz=%d",
                       driver_context.output_buffer.actualcount,
                       driver_context.output_buffer.buffer_size,
                       m_out_buf_count,m_out_buf_size);

    DEBUG_PRINT_HIGH("\n Queried stride cuur Str=%d cur scan=%d Act str=%d act scan =%d",
                       driver_context.video_resoultion.stride,driver_context.video_resoultion.scan_lines,
                       stride,scan_lines);


    if(driver_context.video_resoultion.frame_height != m_height ||
       driver_context.video_resoultion.frame_width != m_width ||
       driver_context.video_resoultion.scan_lines != scan_lines ||
       driver_context.video_resoultion.stride != stride ||
       driver_context.output_buffer.actualcount != m_out_buf_count ||
       driver_context.output_buffer.buffer_size > m_out_buf_size)
    {
      *port_setting_changed = true;
      ioctl_msg.inputparam = &driver_context.output_buffer;
      ioctl_msg.outputparam = NULL;

      if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER_REQ,
                 &ioctl_msg))
      {
         DEBUG_PRINT_ERROR("\n Error in VDEC_IOCTL_GET_BUFFER_REQ in port_setting");
         return OMX_ErrorHardware;
      }
      m_out_buf_size_recon = driver_context.output_buffer.buffer_size;
      m_out_buf_count_recon = driver_context.output_buffer.actualcount;
      m_out_buf_count_min_recon = driver_context.output_buffer.mincount;

      alignment = driver_context.output_buffer.alignment;
      buffer_size = driver_context.output_buffer.buffer_size;
      m_out_buf_size_recon =
        ((buffer_size + alignment - 1) & (~(alignment - 1)));
      m_crop_dy = m_height      = driver_context.video_resoultion.frame_height;
      m_crop_dx = m_width       = driver_context.video_resoultion.frame_width;
      scan_lines = driver_context.video_resoultion.scan_lines;
      stride = driver_context.video_resoultion.stride;
      m_port_height             = m_height;
      m_port_width              = m_width;
    }

    return eRet;
}


/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_validate_port_param

DESCRIPTION
  Get the PMEM area from video decoder

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
OMX_ERRORTYPE omx_vdec::omx_vdec_validate_port_param(int height, int width)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    long hxw = height*width;
    long lmt_hxw = 0;

    return ret;
}

static FILE * outputBufferFile = NULL;

OMX_ERRORTYPE omx_vdec::fill_buffer_done(OMX_HANDLETYPE hComp,
                               OMX_BUFFERHEADERTYPE * buffer)
{
  OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;

  if (buffer == NULL || ((buffer - m_out_mem_ptr) > m_out_buf_count))
  {
     return OMX_ErrorBadParameter;
  }

  DEBUG_PRINT_LOW("\n fill_buffer_done: bufhdr = %p, bufhdr->pBuffer = %p",
      buffer, buffer->pBuffer);
  pending_output_buffers --;

  if (buffer->nFlags & 0x01)
  {
    DEBUG_PRINT_LOW("\n Output EOS has been reached");

    m_outeos_reached = 0;
    m_ineos_reached = 0;
    h264_scratch.nFilledLen = 0;
    nal_count = 0;
    look_ahead_nal = false;
    frame_count = 0;

    if (m_frame_parser.mutils)
    {
      m_frame_parser.mutils->initialize_frame_checking_environment();
    }
    if (psource_frame)
    {
      m_cb.EmptyBufferDone(&m_cmp ,m_app_data,psource_frame);
      psource_frame = NULL;
    }

    if (pdest_frame)
    {
      pdest_frame->nFilledLen = 0;
      m_input_free_q.insert_entry((unsigned) pdest_frame,NULL,NULL);
      pdest_frame = NULL;
    }
    m_frame_parser.flush();
  }

  DEBUG_PRINT_LOW("\n In fill Buffer done call address %p ",buffer);

  if (outputBufferFile == NULL)
  {
    outputBufferFile = fopen ("/data/output.yuv","wb");
  }
  if (outputBufferFile)
  {
    /*fwrite (buffer->pBuffer,1,buffer->nFilledLen,
                  outputBufferFile); */
  }
  /* For use buffer we need to copy the data */
  if (m_cb.FillBufferDone)
  {
    pPMEMInfo  = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                   ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                      buffer->pPlatformPrivate)->entryList->entry;
    DEBUG_PRINT_LOW("\n Before FBD callback Accessed Pmeminfo %d",pPMEMInfo->pmem_fd);
    m_cb.FillBufferDone (hComp,m_app_data,buffer);
    DEBUG_PRINT_LOW("\n After Fill Buffer Done callback %d",pPMEMInfo->pmem_fd);
  }
  else
  {
    return OMX_ErrorBadParameter;
  }

  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::empty_buffer_done(OMX_HANDLETYPE         hComp,
                                          OMX_BUFFERHEADERTYPE* buffer)
{

    if (buffer == NULL || ((buffer - m_inp_mem_ptr) > m_inp_buf_count))
    {
       return OMX_ErrorBadParameter;
    }

    DEBUG_PRINT_LOW("\n empty_buffer_done: bufhdr = %p, bufhdr->pBuffer = %p",
        buffer, buffer->pBuffer);
    pending_input_buffers--;

    if (arbitrary_bytes)
    {
      if (pdest_frame == NULL && input_flush_progress == false)
      {
        DEBUG_PRINT_LOW("\n Push input from buffer done address of Buffer %p",buffer);
        pdest_frame = buffer;
        buffer->nFilledLen = 0;
        push_input_buffer (hComp);
      }
      else
      {
        DEBUG_PRINT_LOW("\n Push buffer into freeq address of Buffer %p",buffer);
        buffer->nFilledLen = 0;
        if (!m_input_free_q.insert_entry((unsigned)buffer,NULL,NULL))
        {
          DEBUG_PRINT_ERROR("\nERROR:i/p free Queue is FULL Error");
        }
      }
    }
    else if(m_cb.EmptyBufferDone)
    {
        buffer->nFilledLen = 0;
        m_cb.EmptyBufferDone(hComp ,m_app_data, buffer);
    }
    return OMX_ErrorNone;
}


int omx_vdec::async_message_process (void *context, void* message)
{
  omx_vdec* omx = NULL;
  struct vdec_msginfo *vdec_msg = NULL;
  OMX_BUFFERHEADERTYPE* omxhdr = NULL;
  struct vdec_output_frameinfo *output_respbuf = NULL;

  if (context == NULL || message == NULL)
  {
    DEBUG_PRINT_ERROR("\n FATAL ERROR in omx_vdec::async_message_process NULL Check");
    return -1;
  }
  vdec_msg = (struct vdec_msginfo *)message;

  omx = reinterpret_cast<omx_vdec*>(context);
  switch (vdec_msg->msgcode)
  {

  case VDEC_MSG_EVT_HW_ERROR:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_HARDWARE_ERROR);
  break;

  case VDEC_MSG_RESP_START_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_START_DONE);
  break;

  case VDEC_MSG_RESP_STOP_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_STOP_DONE);
  break;

  case VDEC_MSG_RESP_RESUME_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_RESUME_DONE);
  break;

  case VDEC_MSG_RESP_PAUSE_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_PAUSE_DONE);
  break;

  case VDEC_MSG_RESP_FLUSH_INPUT_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH);
    break;
  case VDEC_MSG_RESP_FLUSH_OUTPUT_DONE:
    omx->post_event (NULL,vdec_msg->status_code,\
                     OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
    break;
  case VDEC_MSG_RESP_INPUT_FLUSHED:
  case VDEC_MSG_RESP_INPUT_BUFFER_DONE:

    omxhdr = (OMX_BUFFERHEADERTYPE* )\
              vdec_msg->msgdata.input_frame_clientdata;


    if (omxhdr == NULL ||
       ((omxhdr - omx->m_inp_mem_ptr) > omx->m_inp_buf_count) )
    {
       omxhdr = NULL;
       vdec_msg->status_code = VDEC_S_EFATAL;
    }

    omx->post_event ((unsigned int)omxhdr,vdec_msg->status_code,
                     OMX_COMPONENT_GENERATE_EBD);
    break;
  case VDEC_MSG_RESP_OUTPUT_FLUSHED:
  case VDEC_MSG_RESP_OUTPUT_BUFFER_DONE:
    omxhdr = (OMX_BUFFERHEADERTYPE*)vdec_msg->msgdata.output_frame.client_data;
    DEBUG_PRINT_LOW("\n Got Buffer back from Driver %p omxhdr time stamp = %d " ,omxhdr,vdec_msg->msgdata.output_frame.time_stamp);

    if  ( (omxhdr != NULL) &&
         ((omxhdr - omx->m_out_mem_ptr)  < omx->m_out_buf_count) &&
         (omxhdr->pOutputPortPrivate != NULL) &&
         ( ((struct vdec_output_frameinfo *)omxhdr->pOutputPortPrivate
             - omx->driver_context.ptr_respbuffer) < omx->m_out_buf_count)
         )
    {
      if (vdec_msg->msgdata.output_frame.len <=  omxhdr->nAllocLen)
      {
        omxhdr->nFilledLen = vdec_msg->msgdata.output_frame.len;
        omxhdr->nOffset = vdec_msg->msgdata.output_frame.offset;
        omxhdr->nTimeStamp = vdec_msg->msgdata.output_frame.time_stamp;
        omxhdr->nFlags = (vdec_msg->msgdata.output_frame.flags & 0x01);

        output_respbuf = (struct vdec_output_frameinfo *)\
                          omxhdr->pOutputPortPrivate;
        output_respbuf->framesize.bottom = \
          vdec_msg->msgdata.output_frame.framesize.bottom;
        output_respbuf->framesize.left = \
          vdec_msg->msgdata.output_frame.framesize.left;
        output_respbuf->framesize.right = \
          vdec_msg->msgdata.output_frame.framesize.right;
        output_respbuf->framesize.top = \
          vdec_msg->msgdata.output_frame.framesize.top;
        output_respbuf->len = vdec_msg->msgdata.output_frame.len;
        output_respbuf->offset = vdec_msg->msgdata.output_frame.offset;
        output_respbuf->time_stamp = vdec_msg->msgdata.output_frame.time_stamp;
        output_respbuf->flags = vdec_msg->msgdata.output_frame.flags;

        /*Use buffer case*/
        if (omx->output_use_buffer)
        {
          if (vdec_msg->msgdata.output_frame.len <= omxhdr->nAllocLen)
          {
            memcpy ( omxhdr->pBuffer,
                     (vdec_msg->msgdata.output_frame.bufferaddr +
                      vdec_msg->msgdata.output_frame.offset),
                      vdec_msg->msgdata.output_frame.len );
          }
          else
          {
            omxhdr->nFilledLen = 0;
          }
        }
      }
      else
      {
        omxhdr->nFilledLen = 0;
      }

    }
    else
    {
       omxhdr = NULL;
       vdec_msg->status_code = VDEC_S_EFATAL;
    }

    DEBUG_PRINT_LOW("\n Driver returned a output Buffer status %d",
                       vdec_msg->status_code);
    omx->post_event ((unsigned int)omxhdr,vdec_msg->status_code,
                     OMX_COMPONENT_GENERATE_FBD);
    break;
  default:
    break;
  }
  return 1;
}

OMX_ERRORTYPE omx_vdec::empty_this_buffer_proxy_arbitrary (
                                                   OMX_HANDLETYPE hComp,
                                                   OMX_BUFFERHEADERTYPE *buffer
                                                           )
{
  unsigned address,p2,id;
  DEBUG_PRINT_LOW("\n Empty this arbitrary");

  if (buffer == NULL)
  {
    return OMX_ErrorBadParameter;
  }
  DEBUG_PRINT_LOW("\n ETBProxyArb: bufhdr = %p, bufhdr->pBuffer = %p", buffer, buffer->pBuffer);
  DEBUG_PRINT_LOW("\n ETBProxyArb: nFilledLen %u, flags %d, timestamp %u",
        buffer->nFilledLen, buffer->nFlags, (unsigned)buffer->nTimeStamp);

  if( input_flush_progress == true || m_ineos_reached == 1)
  {
    DEBUG_PRINT_LOW("\n Flush in progress return buffer ");
    m_cb.EmptyBufferDone (hComp,m_app_data,buffer);
    return OMX_ErrorNone;
  }

  if (psource_frame == NULL)
  {
    DEBUG_PRINT_LOW("\n Set Buffer as source Buffer %p time stamp %d",buffer,buffer->nTimeStamp);
    psource_frame = buffer;
    DEBUG_PRINT_LOW("\n Try to Push One Input Buffer ");
    push_input_buffer (hComp);
  }
  else
  {
    DEBUG_PRINT_LOW("\n Push the source buffer into pendingq %p",buffer);
    if (!m_input_pending_q.insert_entry((unsigned)buffer,NULL,NULL))
    {
      return OMX_ErrorBadParameter;
    }
  }


  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::push_input_buffer (OMX_HANDLETYPE hComp)
{
  unsigned address,p2,id;
  OMX_ERRORTYPE ret = OMX_ErrorNone;

  if (pdest_frame == NULL || psource_frame == NULL)
  {
    /*Check if we have a destination buffer*/
    if (pdest_frame == NULL)
    {
      DEBUG_PRINT_LOW("\n Get a Destination buffer from the queue");
      if (m_input_free_q.m_size && !gate_input_buffers)
      {
        m_input_free_q.pop_entry(&address,&p2,&id);
        pdest_frame = (OMX_BUFFERHEADERTYPE *)address;
        pdest_frame->nFilledLen = 0;
        DEBUG_PRINT_LOW("\n Address of Pmem Buffer %p",pdest_frame);
      }
    }

    /*Check if we have a destination buffer*/
    if (psource_frame == NULL)
    {
      DEBUG_PRINT_LOW("\n Get a source buffer from the queue");
      if (m_input_pending_q.m_size && !gate_input_buffers)
      {
        m_input_pending_q.pop_entry(&address,&p2,&id);
        psource_frame = (OMX_BUFFERHEADERTYPE *)address;
        DEBUG_PRINT_LOW("\n Next source Buffer %p time stamp %d",psource_frame,
                psource_frame->nTimeStamp);
        DEBUG_PRINT_LOW("\n Next source Buffer flag %d length %d",
        psource_frame->nFlags,psource_frame->nFilledLen);

      }
    }

  }

  while ((pdest_frame != NULL) && (psource_frame != NULL)&& !gate_input_buffers)
  {
    switch (codec_type_parse)
    {
      case CODEC_TYPE_MPEG4:
      case CODEC_TYPE_H263:
        ret =  push_input_sc_codec(hComp);
      break;
      case CODEC_TYPE_H264:
        ret = push_input_h264(hComp);
      break;
      case CODEC_TYPE_VC1:
        ret = push_input_vc1(hComp);
      break;
    }
    if (ret != OMX_ErrorNone)
    {
      DEBUG_PRINT_ERROR("\n Pushing input Buffer Failed");
      omx_report_error ();
      break;
    }
  }

  return ret;
}

OMX_ERRORTYPE omx_vdec::push_input_sc_codec(OMX_HANDLETYPE hComp)
{
  OMX_U32 partial_frame = 1;
  OMX_BOOL genarte_edb = OMX_TRUE,generate_eos = OMX_TRUE;
  unsigned address,p2,id;

  DEBUG_PRINT_LOW("\n Start Parsing the bit stream address %p TimeStamp %d",
        psource_frame,psource_frame->nTimeStamp);
  if (m_frame_parser.parse_mpeg4_frame(psource_frame,
                                       pdest_frame,&partial_frame) == -1)
  {
    DEBUG_PRINT_ERROR("\n Error In Parsing Return Error");
    return OMX_ErrorBadParameter;
  }

  if (partial_frame == 0)
  {
    DEBUG_PRINT_LOW("\n Frame size %d source %p frame count %d",
          pdest_frame->nFilledLen,psource_frame,frame_count);


    DEBUG_PRINT_LOW("\n TimeStamp updated %d",pdest_frame->nTimeStamp);
    /*First Parsed buffer will have only header Hence skip*/
    if (frame_count == 0)
    {
      DEBUG_PRINT_LOW("\n H263/MPEG4 Codec First Frame ");
      mp4h263_flags = psource_frame->nFlags;
      mp4h263_timestamp = psource_frame->nTimeStamp;
      frame_count++;
    }
    else
    {
      pdest_frame->nTimeStamp = mp4h263_timestamp;
      mp4h263_timestamp = psource_frame->nTimeStamp;
      pdest_frame->nFlags = mp4h263_flags;
      mp4h263_flags  = psource_frame->nFlags;

      if(psource_frame->nFilledLen == 0)
      {
        pdest_frame->nFlags = mp4h263_flags;
        generate_eos = OMX_FALSE;
      }


      /*Push the frame to the Decoder*/
      if (empty_this_buffer_proxy(hComp,pdest_frame) != OMX_ErrorNone)
      {
        return OMX_ErrorBadParameter;
      }
      if(m_event_port_settings_sent)
      {
        gate_input_buffers = true;
        return OMX_ErrorNone;
      }
      frame_count++;
      pdest_frame = NULL;

      if (m_input_free_q.m_size && !gate_input_buffers)
      {
        m_input_free_q.pop_entry(&address,&p2,&id);
        pdest_frame = (OMX_BUFFERHEADERTYPE *) address;
        pdest_frame->nFilledLen = 0;
      }
    }
  }
  else
  {
    DEBUG_PRINT_LOW("\n Not a Complete Frame %d",pdest_frame->nFilledLen);
    /*Check if Destination Buffer is full*/
    if (pdest_frame->nAllocLen ==
        pdest_frame->nFilledLen + pdest_frame->nOffset)
    {
      DEBUG_PRINT_ERROR("\nERROR:Frame Not found though Destination Filled");
      return OMX_ErrorStreamCorrupt;
    }
  }

  if (psource_frame->nFilledLen == 0)
  {
    if ((psource_frame->nFlags & 0x01) && generate_eos)
    {
      if (pdest_frame)
      {
        pdest_frame->nTimeStamp = psource_frame->nTimeStamp;
        pdest_frame->nFlags = psource_frame->nFlags;
        DEBUG_PRINT_LOW("\n Frame Found start Decoding Size =%d TimeStamp = %x",
                     pdest_frame->nFilledLen,pdest_frame->nTimeStamp);
        DEBUG_PRINT_LOW("\n Found a frame size = %d number = %d",
                     pdest_frame->nFilledLen,frame_count++);
        /*Push the frame to the Decoder*/
        if (empty_this_buffer_proxy(hComp,pdest_frame) != OMX_ErrorNone)
        {
          return OMX_ErrorBadParameter;
        }
        frame_count++;
        pdest_frame = NULL;
      }
      else
      {
        DEBUG_PRINT_LOW("\n Last frame in else dest addr") ;
        genarte_edb = OMX_FALSE;
      }
   }
    if(genarte_edb)
    {
      DEBUG_PRINT_LOW("\n Buffer Consumed return back to client %p",psource_frame);
      m_cb.EmptyBufferDone (hComp,m_app_data,psource_frame);
      psource_frame = NULL;

      if (m_input_pending_q.m_size)
      {
        DEBUG_PRINT_LOW("\n Pull Next source Buffer %p",psource_frame);
        m_input_pending_q.pop_entry(&address,&p2,&id);
        psource_frame = (OMX_BUFFERHEADERTYPE *) address;
        DEBUG_PRINT_LOW("\n Next source Buffer %p time stamp %d",psource_frame,
                psource_frame->nTimeStamp);
        DEBUG_PRINT_LOW("\n Next source Buffer flag %d length %d",
        psource_frame->nFlags,psource_frame->nFilledLen);
      }
    }
   }
  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::push_input_h264 (OMX_HANDLETYPE hComp)
{
  OMX_U32 partial_frame = 1;
  unsigned address,p2,id;
  OMX_BOOL isNewFrame = OMX_FALSE;
  OMX_BOOL genarte_edb = OMX_TRUE;
  OMX_BOOL skip_parsing = OMX_FALSE;

  if (h264_scratch.pBuffer == NULL)
  {
    DEBUG_PRINT_ERROR("\nERROR:H.264 Scratch Buffer not allocated");
    return OMX_ErrorBadParameter;
  }
  DEBUG_PRINT_LOW("\n Values of h264_scratch.nFilledLen %d look_ahead_nal %d",
                    h264_scratch.nFilledLen,look_ahead_nal);
  DEBUG_PRINT_LOW("\n pdest_frame->nFilledLen %d",pdest_frame->nFilledLen);
  if (h264_scratch.nFilledLen && look_ahead_nal)
  {
    look_ahead_nal = false;
    DEBUG_PRINT_LOW("\n Copy the previous NAL into Buffer %d ",
                       h264_scratch.nFilledLen);
    if ((pdest_frame->nAllocLen - pdest_frame->nFilledLen) >=
         h264_scratch.nFilledLen)
    {
      memcpy ((pdest_frame->pBuffer + pdest_frame->nFilledLen),
              h264_scratch.pBuffer,h264_scratch.nFilledLen);
      pdest_frame->nFilledLen += h264_scratch.nFilledLen;
      DEBUG_PRINT_LOW("\n Total filled length %d",pdest_frame->nFilledLen);
      h264_scratch.nFilledLen = 0;
    }
    else
    {
      DEBUG_PRINT_ERROR("\nERROR:Destination buffer overflow for H264");
      return OMX_ErrorBadParameter;
    }
  }

  if(psource_frame->nFlags & 0x01)
  {
    DEBUG_PRINT_LOW("\n EOS has been reached no parsing required");
    skip_parsing = OMX_TRUE;
    if ((pdest_frame->nAllocLen - pdest_frame->nFilledLen) >=
         psource_frame->nFilledLen)
    {
      memcpy ((pdest_frame->pBuffer + pdest_frame->nFilledLen),
              (psource_frame->pBuffer+psource_frame->nOffset),
              psource_frame->nFilledLen);
      pdest_frame->nFilledLen += psource_frame->nFilledLen;
      DEBUG_PRINT_LOW("\n Total filled length %d",pdest_frame->nFilledLen);
      psource_frame->nFilledLen = 0;
    }
    else
    {
      DEBUG_PRINT_ERROR("\nERROR:Destination buffer overflow for H264");
      return OMX_ErrorBadParameter;
    }
  }

  if(!skip_parsing)
  {
    if (nal_length == 0)
    {
      DEBUG_PRINT_LOW("\n NAL length Zero hence parse using start code");
      if (m_frame_parser.parse_mpeg4_frame(psource_frame,
          &h264_scratch,&partial_frame) == -1)
      {
        DEBUG_PRINT_ERROR("\n Error In Parsing Return Error");
        return OMX_ErrorBadParameter;
      }
    }
    else
    {
      DEBUG_PRINT_LOW("\n NAL length %d hence parse with NAL length %d",nal_length);
      if (m_frame_parser.parse_h264_nallength(psource_frame,
          &h264_scratch,&partial_frame) == -1)
      {
        DEBUG_PRINT_ERROR("\n Error In Parsing NAL Return Error");
        return OMX_ErrorBadParameter;
      }
    }

    if (partial_frame == 0)
    {

      if (nal_count == 0 && h264_scratch.nFilledLen == 0)
      {
        DEBUG_PRINT_LOW("\n First NAL with Zero Length Hence Skip");
        nal_count++;
        h264_scratch.nTimeStamp = psource_frame->nTimeStamp;
        h264_scratch.nFlags = psource_frame->nFlags;
      }
      else
      {
        DEBUG_PRINT_LOW("\n Length of New NAL is %d",h264_scratch.nFilledLen);

        m_frame_parser.mutils->isNewFrame(h264_scratch.pBuffer,
            h264_scratch.nFilledLen,0,isNewFrame);
        nal_count++;

        if (!isNewFrame)
        {
          if ( (pdest_frame->nAllocLen - pdest_frame->nFilledLen) >=
              h264_scratch.nFilledLen)
          {
            DEBUG_PRINT_LOW("\n Not a NewFrame Copy into Dest len %d",
                h264_scratch.nFilledLen);
            memcpy ((pdest_frame->pBuffer + pdest_frame->nFilledLen),
                h264_scratch.pBuffer,h264_scratch.nFilledLen);
            pdest_frame->nFilledLen += h264_scratch.nFilledLen;
            h264_scratch.nFilledLen = 0;
          }
          else
          {
            DEBUG_PRINT_LOW("\n Destination buffer overflow for H264");
            return OMX_ErrorBadParameter;
          }
        }
        else
        {
          look_ahead_nal = true;
          pdest_frame->nTimeStamp = h264_scratch.nTimeStamp;
          pdest_frame->nFlags = h264_scratch.nFlags;
          h264_scratch.nTimeStamp = psource_frame->nTimeStamp;
          h264_scratch.nFlags = psource_frame->nFlags;

          DEBUG_PRINT_LOW("\n Frame Found start Decoding Size =%d TimeStamp = %x",
                       pdest_frame->nFilledLen,pdest_frame->nTimeStamp);
          DEBUG_PRINT_LOW("\n Found a frame size = %d number = %d",
                       pdest_frame->nFilledLen,frame_count++);

          if (pdest_frame->nFilledLen == 0)
          {
            DEBUG_PRINT_LOW("\n Copy the Current Frame since and push it");
            look_ahead_nal = false;
            if ( (pdest_frame->nAllocLen - pdest_frame->nFilledLen) >=
                 h264_scratch.nFilledLen)
            {
              memcpy ((pdest_frame->pBuffer + pdest_frame->nFilledLen),
                      h264_scratch.pBuffer,h264_scratch.nFilledLen);
              pdest_frame->nFilledLen += h264_scratch.nFilledLen;
              h264_scratch.nFilledLen = 0;
            }
            else
            {
              DEBUG_PRINT_ERROR("\nERROR:Destination buffer overflow for H264");
              return OMX_ErrorBadParameter;
            }
          }
          else
          {
            /*Push the frame to the Decoder*/
            if (empty_this_buffer_proxy(hComp,pdest_frame) != OMX_ErrorNone)
            {
              return OMX_ErrorBadParameter;
            }
            if(m_event_port_settings_sent)
            {
              gate_input_buffers = true;
              return OMX_ErrorNone;
            }
            //frame_count++;
            pdest_frame = NULL;
            if (m_input_free_q.m_size && !gate_input_buffers)
            {
              m_input_free_q.pop_entry(&address,&p2,&id);
              pdest_frame = (OMX_BUFFERHEADERTYPE *) address;
              DEBUG_PRINT_LOW("\n Pop the next pdest_buffer %p",pdest_frame);
              pdest_frame->nFilledLen = 0;
            }
          }
        }
      }
    }
    else
    {
      DEBUG_PRINT_LOW("\n Not a Complete Frame %d",pdest_frame->nFilledLen);
      /*Check if Destination Buffer is full*/
      if (h264_scratch.nAllocLen ==
          h264_scratch.nFilledLen + h264_scratch.nOffset)
      {
        DEBUG_PRINT_ERROR("\nERROR: Frame Not found though Destination Filled");
        return OMX_ErrorStreamCorrupt;
      }
    }
  }

  if (psource_frame->nFilledLen == 0)
  {
    DEBUG_PRINT_LOW("\n Buffer Consumed return back to client %p",psource_frame);

    if (psource_frame->nFlags & 0x01)
    {
      if (pdest_frame)
      {
        DEBUG_PRINT_LOW("\n EOS Reached Pass Last Buffer");
        if ( (pdest_frame->nAllocLen - pdest_frame->nFilledLen) >=
             h264_scratch.nFilledLen)
        {
          memcpy ((pdest_frame->pBuffer + pdest_frame->nFilledLen),
                  h264_scratch.pBuffer,h264_scratch.nFilledLen);
          pdest_frame->nFilledLen += h264_scratch.nFilledLen;
          h264_scratch.nFilledLen = 0;
        }
        else
        {
          DEBUG_PRINT_ERROR("\nERROR:Destination buffer overflow for H264");
          return OMX_ErrorBadParameter;
        }
        pdest_frame->nTimeStamp = psource_frame->nTimeStamp;
        pdest_frame->nFlags = psource_frame->nFlags;

        DEBUG_PRINT_LOW("\n Frame Found start Decoding Size =%d TimeStamp = %x",
                     pdest_frame->nFilledLen,pdest_frame->nTimeStamp);
        DEBUG_PRINT_LOW("\n Found a frame size = %d number = %d",
                     pdest_frame->nFilledLen,frame_count++);
        /*Push the frame to the Decoder*/
        if (empty_this_buffer_proxy(hComp,pdest_frame) != OMX_ErrorNone)
        {
          return OMX_ErrorBadParameter;
        }
          if(m_event_port_settings_sent)
          {
            gate_input_buffers = true;
            return OMX_ErrorNone;
          }
        frame_count++;
        pdest_frame = NULL;
      }
      else
      {
        DEBUG_PRINT_LOW("\n Last frame in else dest addr %p size %d",
                     pdest_frame,h264_scratch.nFilledLen);
        genarte_edb = OMX_FALSE;
      }
    }
    if(genarte_edb)
    {
		m_cb.EmptyBufferDone (hComp,m_app_data,psource_frame);
		psource_frame = NULL;
		if (m_input_pending_q.m_size && !gate_input_buffers)
		{
			DEBUG_PRINT_LOW("\n Pull Next source Buffer %p",psource_frame);
		  m_input_pending_q.pop_entry(&address,&p2,&id);
		  psource_frame = (OMX_BUFFERHEADERTYPE *) address;
		  DEBUG_PRINT_LOW("\nNext source Buffer flag %d length %d",
		  psource_frame->nFlags,psource_frame->nFilledLen);
		}
	}
  }
  return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_vdec::push_input_vc1 (OMX_HANDLETYPE hComp)
{
    OMX_U8 *buf, *pdest;
    OMX_U32 partial_frame = 1;
    OMX_U32 buf_len, dest_len;

    if(frame_count == 0)
    {
        DEBUG_PRINT_LOW("\nFirst i/p buffer for VC1 arbitrary bytes\n");
        if(!m_vendor_config.pData)
        {
            DEBUG_PRINT_LOW("\nCheck profile type in 1st source buffer\n");
            buf = psource_frame->pBuffer;
            buf_len = psource_frame->nFilledLen;

            if ((*((OMX_U32 *) buf) & VC1_SP_MP_START_CODE_MASK) ==
                VC1_SP_MP_START_CODE)
            {
                m_vc1_profile = VC1_SP_MP_RCV;
            }
            else if(*((OMX_U32 *) buf) & VC1_AP_SEQ_START_CODE)
            {
                m_vc1_profile = VC1_AP;
            }
            else
            {
                DEBUG_PRINT_ERROR("\nInvalid sequence layer in first buffer\n");
                return OMX_ErrorStreamCorrupt;
            }
        }
        else
        {
            pdest = pdest_frame->pBuffer + pdest_frame->nFilledLen +
                pdest_frame->nOffset;
            dest_len = pdest_frame->nAllocLen - (pdest_frame->nFilledLen +
                pdest_frame->nOffset);

            if(dest_len < m_vendor_config.nDataSize)
            {
                DEBUG_PRINT_ERROR("\nDestination buffer full\n");
                return OMX_ErrorBadParameter;
            }
            else
            {
                memcpy(pdest, m_vendor_config.pData, m_vendor_config.nDataSize);
                pdest_frame->nFilledLen += m_vendor_config.nDataSize;
            }
        }
    }

    switch(m_vc1_profile)
    {
        case VC1_AP:
            DEBUG_PRINT_LOW("\n VC1 AP, hence parse using frame start code");
            if (push_input_sc_codec(hComp) != OMX_ErrorNone)
            {
                DEBUG_PRINT_ERROR("\n Error In Parsing VC1 AP start code");
                return OMX_ErrorBadParameter;
            }
        break;

        case VC1_SP_MP_RCV:
        default:
            DEBUG_PRINT_ERROR("\n Unsupported VC1 profile in ArbitraryBytes Mode\n");
            return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

bool omx_vdec::register_output_buffers()
{
  struct vdec_ioctl_msg ioctl_msg;
  struct vdec_setbuffer_cmd setbuffers;
  int i = 0;
  unsigned p1 =0,p2 = 0,ident = 0;

  for(i=0;i<m_out_buf_count;i++)
  {
    setbuffers.buffer_type = VDEC_BUFFER_TYPE_OUTPUT;
    memcpy (&setbuffers.buffer,&driver_context.ptr_outputbuffer [i],
            sizeof (vdec_bufferpayload));
    ioctl_msg.inputparam  = &setbuffers;
    ioctl_msg.outputparam = NULL;

    DEBUG_PRINT_LOW("\n Set the Output Buffer");
    if (ioctl (driver_context.video_driver_fd,VDEC_IOCTL_SET_BUFFER,
         &ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\n Set output buffer failed");
      return false;
    }
  }
  if(gate_output_buffers)
  {
    /*Generate FBD for all Buffers in the FTBq*/
    pthread_mutex_lock(&m_lock);
    DEBUG_PRINT_LOW("\n Initiate Pushing Output Buffers");
    while (m_ftb_q.m_size)
    {
      m_ftb_q.pop_entry(&p1,&p2,&ident);
      if(ident == OMX_COMPONENT_GENERATE_FTB )
      {
        if (fill_this_buffer_proxy ((OMX_HANDLETYPE)p1,
                                    (OMX_BUFFERHEADERTYPE *)p2) != OMX_ErrorNone)
          {
             DEBUG_PRINT_ERROR("\n fill_this_buffer_proxy failure");
             omx_report_error ();
             return false;
          }
      }
      else if (ident == OMX_COMPONENT_GENERATE_FBD)
      {
        fill_buffer_done(&m_cmp,(OMX_BUFFERHEADERTYPE *)p1);
      }
    }
    gate_output_buffers = false;
    pthread_mutex_unlock(&m_lock);
  }
  return true;
}

bool omx_vdec::align_pmem_buffers(int pmem_fd, OMX_U32 buffer_size,
                                  OMX_U32 alignment)
{
//TODO: figure out if this is really necessary (PMEM_ALLOCATE_ALIGNED is a
// QCOM extension to pmem
  return true;
#if 0
  struct pmem_allocation allocation;
  allocation.size = buffer_size;
  allocation.align = clip2(alignment);
  if (allocation.align < 4096)
  {
    allocation.align = 4096;
  }
  if (ioctl(pmem_fd, PMEM_ALLOCATE_ALIGNED, &allocation) < 0)
  {
    DEBUG_PRINT_ERROR("\n Aligment failed with pmem driver");
    return false;
  }
  return true;
#endif
}

