/*--------------------------------------------------------------------------
Copyright (c) 2009, Code Aurora Forum. All rights reserved.

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

*//** @file omx_vdec.c
  This module contains the implementation of the OpenMAX core & component.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

//#define LOG_NDEBUG 0
#define DEBUG_ON 0
#include "qtv_msg.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef USE_EGL_IMAGE_GPU
#include <EGL/egl.h>
#include <EGL/eglQCOM.h>
#endif

#ifdef _ANDROID_
#include "cutils/properties.h"
#endif //_ANDROID_

#include "adsp.h"
#include "omx_vdec.h"
#include "MP4_Utils.h"

#define H264_START_CODE             0x00000001
#define H264_START_CODE_MASK        0x00FFFFFF
#define VC1_SP_MP_START_CODE        0xC5000000
#define VC1_SP_MP_START_CODE_RCV_V1 0x85000000

#define VC1_SP_MP_START_CODE_MASK   0xFF000000
#define VC1_AP_START_CODE           0x00000100
#define VC1_AP_START_CODE_MASK      0xFFFFFF00
#define VC1_STRUCT_C_LEN            5

#define VC1_AP_SLICE_START_CODE       0x0000010B
#define VC1_AP_SLICE_START_CODE_MASK  0xFFFFFFFF
#define  MAKEFOURCC(ch0,ch1,ch2,ch3) ((uint32)(uint8)(ch0) | \
        ((uint32)(uint8)(ch1) << 8) | \
        ((uint32)(uint8)(ch2) << 16) | \
        ((uint32)(uint8)(ch3) << 24 ))
#define EGL_BUFFER_HANDLE_QCOM 0x4F00
#define EGL_BUFFER_OFFSET_QCOM 0x4F01


genericQueue::genericQueue()
{
   head = NULL;
   tail = NULL;
   numElements = 0;
}

int genericQueue::Enqueue(void *data)
{
   if (NULL == data)
      return 1;
   node *new_node = new node;
   new_node->data = data;
   new_node->next = NULL;
   if (0 == numElements) {
      head = new_node;
   } else {
      tail->next = new_node;
   }

   tail = new_node;
   ++numElements;
   return 0;
}

void *genericQueue::Dequeue()
{
   if (!head)
      return NULL;
   void *retVal = head->data;
   node *head_next = head->next;
   delete head;
   head = head_next;
   --numElements;
   if (0 == numElements) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "FA: Setting Tail to NULL\n");
      tail = NULL;
   }
   return retVal;
}

int genericQueue::GetSize()
{
   int ret = numElements;
   return ret;
}

void *genericQueue::checkHead()
{
   void *ret = NULL;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "FA: check Head\n");
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "FA: check Head: after mutex\n");
   if (head)
      ret = head->data;
   else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Queue Head is NULL\n");
      ret = NULL;
   }
   return ret;
}

void *genericQueue::checkTail()
{
   void *ret = NULL;
   if (tail)
      ret = tail->data;
   else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Tail is NULL\n");
      ret = NULL;
   }
   return ret;
}

genericQueue::~genericQueue()
{
   node *tmp = head;
   node *tmp_next;
   while (tmp) {
      tmp_next = tmp->next;
      delete tmp;
      tmp = tmp_next;
   }
   head = NULL;
   tail = NULL;
}

// omx_cmd_queue destructor
omx_vdec::omx_cmd_queue::~omx_cmd_queue()
{
   // Nothing to do
}

// omx cmd queue constructor
omx_vdec::omx_cmd_queue::omx_cmd_queue():m_read(0), m_write(0), m_size(0)
{
   memset(m_q, 0, sizeof(omx_event) * OMX_CORE_CONTROL_CMDQ_SIZE);
}

// omx cmd queue insert
bool omx_vdec::omx_cmd_queue::insert_entry(unsigned p1, unsigned p2,
                  unsigned id)
{
   bool ret = true;
   if (m_size < OMX_CORE_CONTROL_CMDQ_SIZE) {
      m_q[m_write].id = id;
      m_q[m_write].param1 = p1;
      m_q[m_write].param2 = p2;
      m_q[m_write].canceled = false;

      m_write++;
      m_size++;
      if (m_write >= OMX_CORE_CONTROL_CMDQ_SIZE) {
         m_write = 0;
      }
   } else {
      ret = false;
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "ERROR!!! Command Queue Full\n");
   }
   return ret;
}

// omx cmd queue delete
bool omx_vdec::omx_cmd_queue::delete_entry(unsigned *p1, unsigned *p2,
                  unsigned *id, bool * canceled)
{
   bool ret = true;
   if (m_size > 0) {
      *id = m_q[m_read].id;
      *p1 = m_q[m_read].param1;
      *p2 = m_q[m_read].param2;

      if (canceled) {
         *canceled = m_q[m_read].canceled;
      }
      // Move the read pointer ahead
      ++m_read;
      --m_size;
      if (m_read >= OMX_CORE_CONTROL_CMDQ_SIZE) {
         m_read = 0;
      }
   } else {
      ret = false;
   }
   return ret;
}

//#define  OMX_VDEC_NONUI_VERSION
#ifdef _ANDROID_
VideoHeap::VideoHeap(int fd, size_t size, void *base)
{
   // dup file descriptor, map once, use pmem
   init(dup(fd), base, size, 0, "/dev/pmem_adsp");
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
omx_vdec::omx_vdec():m_state(OMX_StateInvalid),
m_app_data(NULL),
m_loc_use_buf_hdr(NULL), m_vdec(NULL), m_inp_mem_ptr(NULL),
    //m_inp_bm_ptr(NULL),
m_out_mem_ptr(NULL),
    //m_out_bm_ptr(NULL),
    m_first_pending_buf_idx(-1),
m_outstanding_frames(-OMX_CORE_NUM_OUTPUT_BUFFERS),
m_eos_timestamp(0), m_out_buf_count(OMX_CORE_NUM_OUTPUT_BUFFERS),
    //m_out_bm_count(0),
    m_inp_buf_count(0),
m_inp_buf_size(OMX_CORE_INPUT_BUFFER_SIZE),
m_inp_bPopulated(OMX_FALSE),
m_out_bPopulated(OMX_FALSE),
m_height(0),
m_width(0),
m_dec_width(0),
m_dec_height(0),
m_crop_x(0),
m_crop_y(0),
m_crop_dx(0),
m_crop_dy(0),
m_bInterlaced(false),
m_port_height(0),
m_port_width(0),
m_nalu_bytes(0),
m_msg_cnt(0),
m_cmd_cnt(0),
m_etb_cnt(0),
m_ebd_cnt(0),
m_ftb_cnt(0),
m_fbd_cnt(0),
m_inp_bEnabled(OMX_TRUE),
m_out_bEnabled(OMX_TRUE),
m_event_port_settings_sent(false),
m_is_use_buffer(false),
m_is_input_use_buffer(false),
m_bEoSNotifyPending(false),
m_platform_list(NULL),
m_platform_entry(NULL),
m_pmem_info(NULL),
m_h264_utils(NULL),
m_pcurrent_frame(NULL),
m_default_arbitrary_bytes(true),
m_default_arbitrary_bytes_vc1(true),
m_default_accumulate_subframe(true),
m_bAccumulate_subframe(false),
m_bArbitraryBytes(true),
m_arbitrary_bytes_input_mem_ptr(NULL),
m_current_arbitrary_bytes_input(NULL),
m_bPartialFrame(false),
m_bStartCode(false), m_header_state(HEADER_STATE_RECEIVED_NONE), m_use_pmem(0),
flush_before_vdec_op_q(NULL),
m_b_divX_parser(false),
m_mp4_utils(NULL),
m_timestamp_interval(0),
m_prev_timestamp(0),
m_b_display_order(false),
m_pPrevFrame(NULL),
m_codec_format(0),
m_codec_profile(0),
m_bInvalidState(false),
m_display_id(NULL),
m_is_use_egl_buffer(false),
m_first_sync_frame_rcvd(false),
m_heap_ptr(NULL)
{
   /* Assumption is that , to begin with , we have all the frames with client */
   memset(m_out_flags, 0x00, (OMX_CORE_NUM_OUTPUT_BUFFERS + 7) / 8);
   memset(m_flags,       0x00,     4);
   memset(&m_cmp, 0, sizeof(m_cmp));
   memset(&m_cb, 0, sizeof(m_cb));
   memset(&m_vdec_cfg, 0, sizeof(m_vdec_cfg));
   m_vdec_cfg.vdec_fd = -1;
   m_vdec_cfg.inputBuffer = NULL;
   m_vdec_cfg.outputBuffer = NULL;
   memset(&m_frame_info, 0, sizeof(m_frame_info));
   memset(m_out_bm_count, 0x0, (OMX_CORE_NUM_OUTPUT_BUFFERS + 7) / 8);

   memset(m_inp_bm_count, 0x0, (MAX_NUM_INPUT_BUFFERS + 7) / 8);
   memset(&m_arbitrary_bytes_info, 0,
          sizeof(union omx_arbitrary_bytes_info));
   for (int i = 0; i < OMX_CORE_NUM_INPUT_BUFFERS; i++) {
      input[i] = NULL;
   }

   for (int i = 0; i < OMX_CORE_NUM_INPUT_BUFFERS; i++) {
      m_arbitrary_bytes_input[i] = NULL;
      memset(&m_extra_buf_info[i], 0,
              sizeof(struct
               omx_extra_arbitrarybytes_buff_info));
      m_input_buff_info[i].pArbitrary_bytes_freed = NULL;
      m_input_buff_info[i].bfree_input = true;
  }

   m_vendor_config.pData = NULL;
   m_bWaitForResource = false;
   m_color_format = (OMX_COLOR_FORMATTYPE)OMX_QCOM_COLOR_FormatYVU420SemiPlanar;
   return;
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
   m_nalu_bytes = 0;
   m_port_width = m_port_height = 0;

   if (flush_before_vdec_op_q) {
      delete flush_before_vdec_op_q;
      flush_before_vdec_op_q = NULL;
   }

   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlFrameDoneCbStub

DESCRIPTION
  Frame done callback from the video decoder

PARAMETERS
  1. ctxt(I)  -- Context information to the self.
  2. frame(I) -- video frame decoded


RETURN VALUE
  None.

========================================================================== */
void omx_vdec::frame_done_cb_stub(struct vdec_context *ctxt,
              struct vdec_frame *frame)
{
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;

   pThis->post_event((unsigned)ctxt, (unsigned)frame,
           OMX_COMPONENT_GENERATE_FRAME_DONE);
   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::frame_done_display_order_cb

DESCRIPTION
  Frame done callback from the video decoder with a display order

PARAMETERS
  1. ctxt(I)  -- Context information to the self.
  2. frame(I) -- video frame decoded

RETURN VALUE
  None.

========================================================================== */
void omx_vdec::frame_done_display_order_cb(struct vdec_context *ctxt, struct vdec_frame *frame)
{
  bool bCurrentBFrame = false;

  omx_vdec *pThis = (omx_vdec *) ctxt->extra;
  if (pThis->m_pPrevFrame)
  {
    if (frame->frameDetails.ePicType[0] == VDEC_PICTURE_TYPE_B)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"frame_done_display_order_cb - b frame");
      bCurrentBFrame = true;
    }

    if (bCurrentBFrame)
    {
      if (pThis->m_pPrevFrame->timestamp < frame->timestamp)
      {
        /* Swap timestamp */
        pThis->m_pPrevFrame->timestamp ^= frame->timestamp;
        frame->timestamp ^= pThis->m_pPrevFrame->timestamp;
        pThis->m_pPrevFrame->timestamp ^= frame->timestamp;
      }
      pThis->frame_done_cb(ctxt, frame);
    }
    else if (frame->flags & FRAME_FLAG_EOS)
    {
      pThis->frame_done_cb(ctxt, pThis->m_pPrevFrame);
      pThis->frame_done_cb(ctxt, frame);
      pThis->m_pPrevFrame = NULL;
    }
    else
    {
      if (pThis->m_pPrevFrame->timestamp > frame->timestamp)
      {
        QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"Warning - previous ts > current ts. And both are non B-frames");
      }
      pThis->frame_done_cb(ctxt, pThis->m_pPrevFrame);
      pThis->m_pPrevFrame = frame;
    }
  }
  else
  {
    if (frame->flags & FRAME_FLAG_EOS)
    {
      pThis->frame_done_cb(ctxt, frame);
    }
    else if (frame->flags & FRAME_FLAG_FLUSHED) {
      pThis->frame_done_cb(ctxt, frame);
    }
    else
    {
      pThis->m_pPrevFrame = frame;
    }
  }
}

/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlFrameDoneCb

DESCRIPTION
  Frame done callback from the video decoder

PARAMETERS
  1. ctxt(I)  -- Context information to the self.
  2. frame(I) -- video frame decoded


RETURN VALUE
  None.

========================================================================== */
void omx_vdec::frame_done_cb(struct vdec_context *ctxt,
              struct vdec_frame *frame)
{
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "omx_vdec::frame_done_cb \n");
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;
   OMX_BUFFERHEADERTYPE *pBufHdr;

   pBufHdr = (OMX_BUFFERHEADERTYPE *) pThis->m_out_mem_ptr;
   if (pThis->m_out_mem_ptr) {
      unsigned int i;
      for (i = 0; i < pThis->m_out_buf_count; i++, pBufHdr++) {
         if (pBufHdr->pOutputPortPrivate == frame) {
            if (BITMASK_ABSENT((pThis->m_out_flags), i)) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "\n Warning: Double framedone - Frame is still with IL client \n");
               return;
            }
            break;
         }
      }
      // Copy from PMEM area to user defined area
      if (pThis->omx_vdec_get_use_buf_flg()) {
         pThis->omx_vdec_cpy_user_buf(pBufHdr);
      }

      if (i < pThis->m_out_buf_count) {
         BITMASK_CLEAR((pThis->m_out_flags), i);
         pThis->m_fbd_cnt++;
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FBD: Count %d %x %lld\n",
                  pThis->m_fbd_cnt, pThis->m_out_flags[0],
                  frame->timestamp);

         if (pThis->m_cb.FillBufferDone) {
            OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo =
                NULL;

            if (pBufHdr->pPlatformPrivate) {
               pPMEMInfo =
                   (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO
                    *)
                   ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                    pBufHdr->pPlatformPrivate)->
                   entryList->entry;
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "\n PMEM fd %u, Offset %x \n",
                        (unsigned)pPMEMInfo->
                        pmem_fd,
                        (unsigned)pPMEMInfo->
                        offset);
            }

            if (frame->flags & FRAME_FLAG_FLUSHED) {
               pBufHdr->nFilledLen = 0;
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "\n **** Flushed Frame-%d **** \n",
                        i);
            } else {
               if(!pThis->omx_vdec_get_use_egl_buf_flg()) {
               pThis->fill_extradata(pBufHdr, frame);
            }
               else {
                 pBufHdr->nFilledLen = pThis->get_output_buffer_size();
                 // Invalidate the cache for the size of the decoded frame
                  #ifdef USE_PMEM_ADSP_CACHED
                  vdec_cachemaint(frame->buffer.pmem_id, pBufHdr->pBuffer, pBufHdr->nFilledLen, PMEM_CACHE_INVALIDATE);
                  #endif
                }
            }

            // If the decoder provides frame done for last frame then set the eos flag.
            if ((frame->flags & FRAME_FLAG_EOS)) {   /* || // @Temporary blocked
                              ((frame->timestamp > 0) && (frame->timestamp == pThis->m_eos_timestamp))) */
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "\n **** Frame-%d EOS  with last timestamp **** \n",
                        i);
               pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
            }

            pBufHdr->nTimeStamp = frame->timestamp;

            PrintFrameHdr(pBufHdr);
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "FBD %d with TS %d \n",
                     pThis->m_fbd_cnt,
                     (unsigned)pBufHdr->nTimeStamp);
            ++pThis->m_outstanding_frames;
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "FBD Outstanding frame cnt %d\n",
                     pThis->m_outstanding_frames);
#ifdef  OMX_VDEC_NONUI_VERSION
            if ((pThis->m_fbd_cnt < 1)
                || (frame->flags & FRAME_FLAG_FLUSHED)) {
               pThis->m_cb.FillBufferDone(&pThis->
                           m_cmp,
                           pThis->
                           m_app_data,
                           pBufHdr);
            } else {
               pThis->post_event((unsigned)&pThis->
                       m_cmp,
                       (unsigned)pBufHdr,
                       OMX_COMPONENT_GENERATE_FTB);
            }
#else
            pThis->m_cb.FillBufferDone(&pThis->m_cmp,
                        pThis->m_app_data,
                        pBufHdr);
#endif
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error: FrameDoneCb Ignored due to NULL callbacks \n");
         }
      } else {
         if(frame->flags & FRAME_FATAL_ERROR) {
             pThis->m_bInvalidState = true;
             pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                    OMX_EventError,
                    OMX_ErrorHardware, 0,
                    NULL);
             return;
         }
         // fake frame provided by the decoder to indicate end of stream
         else if (frame->flags & FRAME_FLAG_EOS) {
            OMX_BUFFERHEADERTYPE *pBufHdr =
                (OMX_BUFFERHEADERTYPE *) pThis->
                m_out_mem_ptr;
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n **** Fake Frame EOS **** \n");
            for (i = 0; i < pThis->m_out_buf_count;
                 i++, pBufHdr++) {
               if (BITMASK_PRESENT
                   ((pThis->m_out_flags), i)) {
                  BITMASK_CLEAR((pThis->
                            m_out_flags), i);
                  break;
               }
            }
            if (i < pThis->m_out_buf_count) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "EOS Indication used buffer numbered %d\n",
                        i);
               pBufHdr->nFlags |= OMX_BUFFERFLAG_EOS;
               pBufHdr->nFilledLen = 0;
               pBufHdr->nTimeStamp = frame->timestamp;
               if (pBufHdr->nTimeStamp == 0) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "eos timestamp used is %lld\n",
                           pThis->
                           m_eos_timestamp);
                  pBufHdr->nTimeStamp =
                      pThis->m_eos_timestamp;
               }
               pThis->m_cb.FillBufferDone(&pThis->
                           m_cmp,
                           pThis->
                           m_app_data,
                           pBufHdr);
               pThis->m_bEoSNotifyPending = false;
               ++pThis->m_outstanding_frames;
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "FBD Outstanding frame cnt %d\n",
                        pThis->
                        m_outstanding_frames);
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "Failed to send EOS to the IL Client\n");
               pThis->m_bEoSNotifyPending = true;
            }

         }
      }
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error: InvalidCb Ignored due to NULL Out storage \n");
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "frame_done_cb  buffer->nTimeStamp %d nFlags %d\n",
            pBufHdr->nTimeStamp, pBufHdr->nFlags);
   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlBufferDoneCbStub

DESCRIPTION
  Buffer done callback from the decoder. This stub posts the command to the
  decoder pipe which will be executed by the decoder later to the client.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
void omx_vdec::buffer_done_cb_stub(struct vdec_context *ctxt, void *cookie)
{
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;

   pThis->post_event((unsigned)ctxt, (unsigned)cookie,
           OMX_COMPONENT_GENERATE_BUFFER_DONE);
   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlBufferDoneCb

DESCRIPTION
  Buffer done callback from the decoder.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
void omx_vdec::buffer_done_cb(struct vdec_context *ctxt, void *cookie)
{
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "buffer Done callback received\n");
   if (pThis->m_cb.EmptyBufferDone) {
      OMX_BUFFERHEADERTYPE *bufHdr = (OMX_BUFFERHEADERTYPE *) cookie;
      PrintFrameHdr(bufHdr);
      unsigned int nPortIndex =
          bufHdr - (OMX_BUFFERHEADERTYPE *) pThis->m_inp_mem_ptr;
      if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
         if (!pThis->is_pending(nPortIndex)) {
            if (pThis->m_bArbitraryBytes) {
               pThis->
                   buffer_done_cb_arbitrarybytes(ctxt,
                             cookie);
            } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "buffer Done %x",bufHdr->pBuffer);
               pThis->m_cb.EmptyBufferDone(&pThis->
                            m_cmp,
                            pThis->
                            m_app_data,
                            bufHdr);
            }
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "buffer Done callback received for pending buffer; Ignoring!!\n");
         }
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "Warning!! Buffer Done Callback Came with Incorrect buffer\n");
      }
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "BufferDoeCb Ignored due to NULL callbacks \n");
   }
   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::buffer_done_cb_arbitrarybytes

DESCRIPTION
  Buffer done callback for arbitrary bytes.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
void omx_vdec::buffer_done_cb_arbitrarybytes(struct vdec_context *ctxt,
                    void *cookie)
{
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;
   OMX_BUFFERHEADERTYPE *bufHdr = (OMX_BUFFERHEADERTYPE *) cookie;
   OMX_S8 extra_buf_index =
       pThis->find_extra_buffer_index(bufHdr->pBuffer);

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "buffer_done_cb_arbitrarybytes %d\n", extra_buf_index);

   if (extra_buf_index != -1) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "buffer Done callback - end of arbitrary bytes buffer!!\n");
      pThis->free_extra_buffer(extra_buf_index);
      if (pThis->m_extra_buf_info[extra_buf_index].arbitrarybytesInput)
      {
         unsigned int nPortIndex =
             pThis->m_extra_buf_info[extra_buf_index].
             arbitrarybytesInput -
             (OMX_BUFFERHEADERTYPE *) pThis->
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                       "buffer Done arb %x",pThis->m_extra_buf_info[extra_buf_index].arbitrarybytesInput->pBuffer);
            pThis->m_cb.EmptyBufferDone(&pThis->m_cmp,
                         pThis->m_app_data,
                         pThis->
                         m_extra_buf_info
                         [extra_buf_index].
                         arbitrarybytesInput);
            pThis->m_extra_buf_info[extra_buf_index].
                arbitrarybytesInput = NULL;
            pThis->push_pending_buffers_proxy();
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                     "Incorrect previous arbitrary bytes buffer %p\n",
                     pThis->m_extra_buf_info[extra_buf_index].
                     arbitrarybytesInput);
         }
     }
    else
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH, "arbitrarybytesInput NULL");
      pThis->push_pending_buffers_proxy();
    }
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "buffer Done callback - m_bPartialFrame %d m_current_frame %p\n",
            pThis->m_bPartialFrame, pThis->m_current_frame);

   pThis->free_input_buffer(bufHdr);
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
void omx_vdec::process_event_cb(struct vdec_context *ctxt, unsigned char id)
{
   unsigned p1;      // Parameter - 1
   unsigned p2;      // Parameter - 2
   unsigned ident = 0;
   unsigned qsize = 0;   // qsize
   omx_vdec *pThis = (omx_vdec *) ctxt->extra;
   bool canceled = false;

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "OMXCntrlProessMsgCb[%x,%d] Enter: \n", (unsigned)ctxt,
            (unsigned)id);
   if (!pThis) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "ERROR : ProcessMsgCb: Context is incorrect; bailing out\n");
      return;
   }
   // Protect the shared queue data structure
   do {
      canceled = false;
      pThis->mutex_lock();
      qsize = pThis->m_ftb_q.m_size;
      if (qsize) {
         pThis->m_ftb_q.delete_entry(&p1, &p2, &ident,
                      &canceled);
         if (canceled) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "canceled ftb %x", p2);
            pThis->mutex_unlock();
            continue;
         }
      } else {
         qsize = pThis->m_cmd_q.m_size;
         if (qsize) {
            pThis->m_cmd_q.delete_entry(&p1, &p2, &ident);
         } else if (pThis->m_bArbitraryBytes) {
            if (pThis->m_current_arbitrary_bytes_input !=
                NULL) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "process_event_cb - continue using m_current_arbitrary_bytes_input %p\n",
                        pThis->
                        m_current_arbitrary_bytes_input);
               ident =
                   OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES;
            } else if (qsize =
                  pThis->m_etb_arbitrarybytes_q.
                  m_size) {
               QTV_MSG_PRIO3(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "ETBQ[%d] FTBQ[%d] CMDQ[%d]\n",
                        pThis->
                        m_etb_arbitrarybytes_q.
                        m_size,
                        pThis->m_ftb_q.m_size,
                        pThis->m_cmd_q.m_size);
               pThis->m_etb_arbitrarybytes_q.
                   delete_entry(&p1, &p2, &ident);
               pThis->m_current_arbitrary_bytes_input =
                   (OMX_BUFFERHEADERTYPE *) p2;
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "set m_current_arbitrary_bytes_input %p %d\n",
                        pThis->
                        m_current_arbitrary_bytes_input);
            }
         }
      }
      if (qsize) {
         pThis->m_msg_cnt++;
      }
      pThis->mutex_unlock();

      if ((qsize > 0)
          || (ident == OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES)) {
         id = ident;
         QTV_MSG_PRIO7(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Process ->%d[%d]ebd %d fbd %d oc %d %x,%x\n",
                  pThis->m_state, ident, pThis->m_etb_cnt,
                  pThis->m_fbd_cnt,
                  pThis->m_outstanding_frames,
                  pThis->m_flags[0] ,
                  pThis->m_out_flags[0]);
         if (id == OMX_COMPONENT_GENERATE_EVENT) {
            if (pThis->m_cb.EventHandler) {
               if (p1 == OMX_CommandStateSet) {
                  pThis->m_state =
                      (OMX_STATETYPE) p2;
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "Process -> state set to %d \n",
                           pThis->m_state);
                  pThis->m_cb.
                      EventHandler(&pThis->m_cmp,
                         pThis->
                         m_app_data,
                         OMX_EventCmdComplete,
                         p1, p2, NULL);
               }
               /* posting error events for illegal state transition */
               else if (p1 == OMX_EventError) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_ERROR,
                           "-------Error %x------",p2);
                  if (p2 == OMX_ErrorInvalidState) {
                     pThis->m_state =
                         OMX_StateInvalid;
                     pThis->m_cb.
                         EventHandler
                         (&pThis->m_cmp,
                          pThis->m_app_data,
                          OMX_EventError,
                          OMX_ErrorInvalidState,
                          0, NULL);
                     pThis->
                         execute_omx_flush
                         (OMX_ALL);
                  } else {
                     pThis->m_cb.
                         EventHandler
                         (&pThis->m_cmp,
                          pThis->m_app_data,
                          OMX_EventError, p2,
                          0, NULL);

                  }
               } else if (p1 == OMX_CommandPortDisable) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "Process -> Port %d set to PORT_STATE_DISABLED state \n",
                           p2);
                  pThis->m_cb.
                      EventHandler(&pThis->m_cmp,
                         pThis->
                         m_app_data,
                         OMX_EventCmdComplete,
                         p1, p2, NULL);
               } else if (p1 == OMX_CommandPortEnable) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "Process -> Port %d set to PORT_STATE_ENABLED state \n",
                           p2);
                  pThis->m_cb.
                      EventHandler(&pThis->m_cmp,
                         pThis->
                         m_app_data,
                         OMX_EventCmdComplete,
                         p1, p2, NULL);
               } else {
                  pThis->m_cb.
                      EventHandler(&pThis->m_cmp,
                         pThis->
                         m_app_data,
                         OMX_EventCmdComplete,
                         p1, p2, NULL);
               }

            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_ERROR,
                       "Error: ProcessMsgCb ignored due to NULL callbacks\n");
            }
         } else if (id == OMX_COMPONENT_GENERATE_EVENT_FLUSH) {
            pThis->m_cb.EventHandler(&pThis->m_cmp,
                      pThis->m_app_data,
                      OMX_EventCmdComplete,
                      p1, p2, NULL);
         } else if (id == OMX_COMPONENT_GENERATE_BUFFER_DONE) {
            buffer_done_cb((struct vdec_context *)p1,
                      (void *)p2);
         } else if (id == OMX_COMPONENT_GENERATE_FRAME_DONE) {
            if (pThis->m_b_display_order)
            {
               frame_done_display_order_cb((struct vdec_context *)p1,(struct vdec_frame *)p2);
            }
            else
            {
            frame_done_cb((struct vdec_context *)p1,
                     (struct vdec_frame *)p2);
            }
         } else if (id == OMX_COMPONENT_GENERATE_ETB) {
            pThis->
                empty_this_buffer_proxy_frame_based((OMX_HANDLETYPE) p1, (OMX_BUFFERHEADERTYPE *) p2);
         } else if (id ==
               OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES) {
            pThis->
                empty_this_buffer_proxy_arbitrary_bytes((OMX_HANDLETYPE) p1, pThis->m_current_arbitrary_bytes_input);
         } else if (id == OMX_COMPONENT_GENERATE_FTB) {
            pThis->
                fill_this_buffer_proxy((OMX_HANDLETYPE) p1,
                        (OMX_BUFFERHEADERTYPE
                         *) p2);
         } else if (id == OMX_COMPONENT_GENERATE_COMMAND) {
            pThis->send_command_proxy(&pThis->m_cmp,
                       (OMX_COMMANDTYPE) p1,
                       (OMX_U32) p2,
                       (OMX_PTR) NULL);
         } else if (id == OMX_COMPONENT_PUSH_PENDING_BUFS) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "process_event_cb :: push_pending_buffer_proxy\n");
            pThis->push_pending_buffers_proxy();
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error: ProcessMsgCb Ignored due to Invalid Identifier\n");
         }
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "OMXCntrlProessMsgCb[%x,%d] Exit: \n",
                  (unsigned)ctxt, (unsigned)id);
      }

      if (pThis->m_bArbitraryBytes) {
         unsigned cmd_ftb_qsize = 0;
         pThis->mutex_lock();
         cmd_ftb_qsize =
             pThis->m_cmd_q.m_size + pThis->m_ftb_q.m_size;
         qsize =
             cmd_ftb_qsize +
             pThis->m_etb_arbitrarybytes_q.m_size;
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "process_event_cb %d %d %d\n",
                  pThis->m_cmd_q.m_size,
                  pThis->m_ftb_q.m_size,
                  pThis->m_etb_arbitrarybytes_q.m_size);
         pThis->mutex_unlock();
         if (cmd_ftb_qsize == 0 && pThis->m_bWaitForResource) {
            // if cmd_ftb_qsize >0, always come back to process those, otherwise if we are out of buffer
            // wait until free_input_buffer is called when q6 sends a buffer done, don't loop forever.
            qsize = 0;
         } else if (qsize == 0
               && pThis->m_current_arbitrary_bytes_input) {
            qsize = 1;
         }
      } else {
         pThis->mutex_lock();
         qsize = pThis->m_cmd_q.m_size + pThis->m_ftb_q.m_size;
         pThis->mutex_unlock();
      }
   } while (qsize > 0);
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "process_event_cb Exit\n");
   return;
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

#ifdef _ANDROID_
   char property_value[PROPERTY_VALUE_MAX] = {0};
#endif
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   int r, fd;

   fd = open("/dev/vdec", O_RDWR);

   if(fd < 0)
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Omx_vdec::Comp Init Returning failure\n");
      // Another decoder instance is running. return from here.
      return OMX_ErrorInsufficientResources;
   }
   //close(fd);
   m_vdec_cfg.vdec_fd =  fd;

#ifdef _ANDROID_
   if(0 != property_get("persist.omxvideo.arb-bytes", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_arbitrary_bytes = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "OMX_VDEC:: Comp Init failed in \
           getting value for the Android property [persist.omxvideo.arb-bytes]");
   }

   if(0 != property_get("persist.omxvideo.arb-bytes-vc1", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_arbitrary_bytes_vc1 = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "OMX_VDEC:: Comp Init failed in \
           getting value for the Android property [persist.omxvideo.arb-bytes-vc1]");
   }

   if(0 != property_get("persist.omxvideo.accsubframe", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_accumulate_subframe = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "OMX_VDEC:: Comp Init failed in \
           getting value for the Android property [persist.omxvideo.accsubframe]");
   }
#endif

   m_vdec_cfg.buffer_done = buffer_done_cb_stub;
   m_vdec_cfg.frame_done = frame_done_cb_stub;
   m_vdec_cfg.process_message = process_event_cb;
   m_vdec_cfg.height = OMX_CORE_QCIF_HEIGHT;
   m_vdec_cfg.width = OMX_CORE_QCIF_WIDTH;
   m_vdec_cfg.extra = this;
   // Copy the role information which provides the decoder kind
   strncpy(m_vdec_cfg.kind, role, 128);

   if (!strncmp
       (m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4",
        OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.mpeg4",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('m', 'p', '4', 'v');
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",
       OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.h263",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('h', '2', '6', '3');
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
       OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.avc",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('h', '2', '6', '4');
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
       OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.vc1",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('w', 'm', 'v', '3');
   } else
       if(!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",
       OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.divx",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('D', 'I', 'V', 'X');
   }else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp",
       OMX_MAX_STRINGNAME_SIZE)) {
      strncpy((char *)m_cRole, "video_decoder.vp",
         OMX_MAX_STRINGNAME_SIZE);
      m_vdec_cfg.fourcc = MAKEFOURCC('V', 'P', '6', '0');
   } else
       if (!strncmp
           (m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",
            OMX_MAX_STRINGNAME_SIZE)) {
           strncpy((char *)m_cRole, "video_decoder.spark",
            OMX_MAX_STRINGNAME_SIZE);
           m_vdec_cfg.fourcc = MAKEFOURCC('F', 'L', 'V', '1');
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "\n Unknown Component\n");
      eRet = OMX_ErrorInvalidComponentName;
   }

   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx", 27) == 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Mp4 output buffer Count updated\n");
      m_out_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS_MP4;
      m_outstanding_frames = -OMX_CORE_NUM_OUTPUT_BUFFERS_MP4;
      m_bArbitraryBytes = false;
      m_b_divX_parser = true;
      memset(&m_divX_buffer_info, 0, sizeof(m_divX_buffer_info));
      m_divX_buffer_info.parsing_required = true;
      m_mp4_utils = new MP4_Utils();
      m_b_display_order = true;
      m_codec_format = QOMX_VIDEO_DIVXFormat4;
   } else if ( (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263", 27) == 0 )
              || (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark", 28) == 0)
              || (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4", 28) == 0)
               ) {
      m_out_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS_MP4;
      m_outstanding_frames = -OMX_CORE_NUM_OUTPUT_BUFFERS_MP4;
      m_mp4_utils = new MP4_Utils();
   } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) ==
         0) {
      m_h264_utils = new H264_Utils();
      m_bAccumulate_subframe = true;   // by default
      m_bAccumulate_subframe = (m_bAccumulate_subframe && m_default_accumulate_subframe);
      m_out_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS_H264;
      m_outstanding_frames = -OMX_CORE_NUM_OUTPUT_BUFFERS_H264;
   } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1", 26) ==
         0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "VC1 component init\n");
      m_out_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS_VC1;
      m_outstanding_frames = -OMX_CORE_NUM_OUTPUT_BUFFERS_VC1;
      m_bAccumulate_subframe = true;
      m_bAccumulate_subframe = (m_bAccumulate_subframe && m_default_accumulate_subframe);
   } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp", 25) ==
         0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "VP6 component init\n");
      m_out_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS_VC1;
      m_outstanding_frames = -OMX_CORE_NUM_OUTPUT_BUFFERS_VC1;
      m_bAccumulate_subframe = false;
      m_bArbitraryBytes =  false;
   }

   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1", 26) == 0)
   {
       m_bArbitraryBytes = (m_bArbitraryBytes && m_default_arbitrary_bytes_vc1);
   }
   else
   {
       m_bArbitraryBytes = (m_bArbitraryBytes && m_default_arbitrary_bytes);
   }

   m_crop_dy = m_height = m_vdec_cfg.height;
   m_crop_dx = m_width = m_vdec_cfg.width;
   m_port_height = m_height;
   m_port_width = m_width;
   m_state = OMX_StateLoaded;
   m_first_pending_buf_idx = -1;
   flush_before_vdec_op_q = new genericQueue();
   if(flush_before_vdec_op_q == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,"flush_before_vdec_op_q creation failed\n");
      eRet = OMX_ErrorInsufficientResources;
   }
   eRet = create_msg_thread();
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
OMX_ERRORTYPE omx_vdec::get_component_version(OMX_IN OMX_HANDLETYPE hComp,
                     OMX_OUT OMX_STRING componentName,
                     OMX_OUT OMX_VERSIONTYPE *
                     componentVersion,
                     OMX_OUT OMX_VERSIONTYPE *
                     specVersion,
                     OMX_OUT OMX_UUIDTYPE *
                     componentUUID)
{
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Get Comp Version in Invalid State\n");
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
OMX_ERRORTYPE omx_vdec::send_command(OMX_IN OMX_HANDLETYPE hComp,
                 OMX_IN OMX_COMMANDTYPE cmd,
                 OMX_IN OMX_U32 param1,
                 OMX_IN OMX_PTR cmdData)
{
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Send Command in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   post_event((unsigned)cmd, (unsigned)param1,
         OMX_COMPONENT_GENERATE_COMMAND);
   semaphore_wait();

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
OMX_ERRORTYPE omx_vdec::send_command_proxy(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_IN OMX_COMMANDTYPE cmd,
                  OMX_IN OMX_U32 param1,
                  OMX_IN OMX_PTR cmdData)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   //   Handle only IDLE and executing
   OMX_STATETYPE eState = (OMX_STATETYPE) param1;
   int bFlag = 1;

   if (cmd == OMX_CommandStateSet) {
    /***************************/
      /* Current State is Loaded */
    /***************************/
      if (m_state == OMX_StateLoaded) {
         if (eState == OMX_StateIdle) {
            if (allocate_done() ||
                (m_inp_bEnabled == OMX_FALSE
                 && m_out_bEnabled == OMX_FALSE)) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "OMXCORE-SM: Loaded-->Idle\n");
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "OMXCORE-SM: Loaded-->Idle-Pending\n");
               BITMASK_SET(m_flags,
                      OMX_COMPONENT_IDLE_PENDING);
               // Skip the event notification
               bFlag = 0;
            }
         }
         /* Requesting transition from Loaded to Loaded */
         else if (eState == OMX_StateLoaded) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Loaded-->Loaded\n");
            post_event(OMX_EventError, OMX_ErrorSameState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorSameState;
         }
         /* Requesting transition from Loaded to WaitForResources */
         else if (eState == OMX_StateWaitForResources) {
            /* Since error is None , we will post an event at the end of this function definition */
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Loaded-->WaitForResources\n");
         }
         /* Requesting transition from Loaded to Executing */
         else if (eState == OMX_StateExecuting) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Loaded-->Executing\n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Loaded to Pause */
         else if (eState == OMX_StatePause) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Loaded-->Pause\n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Loaded to Invalid */
         else if (eState == OMX_StateInvalid) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Loaded-->Invalid\n");
            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorInvalidState;
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "OMXCORE-SM: Loaded-->Invalid(%d Not Handled)\n",
                     eState);
            eRet = OMX_ErrorBadParameter;
         }
      }

    /***************************/
      /* Current State is IDLE */
    /***************************/
      else if (m_state == OMX_StateIdle) {
         if (eState == OMX_StateLoaded) {
            if (release_done()) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "OMXCORE-SM: Idle-->Loaded\n");
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "OMXCORE-SM: Idle-->Loaded-Pending\n");
               BITMASK_SET(m_flags,
                      OMX_COMPONENT_LOADING_PENDING);
               // Skip the event notification
               bFlag = 0;
            }
         } else if (eState == OMX_StateExecuting) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Idle-->Executing\n");

            if (m_bArbitraryBytes) {
               initialize_arbitrary_bytes_environment
                   ();
            }

            if (m_h264_utils) {
               m_h264_utils->
                   initialize_frame_checking_environment
                   ();
            }
         }
         /* Requesting transition from Idle to Idle */
         else if (eState == OMX_StateIdle) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: Idle-->Idle\n");
            post_event(OMX_EventError, OMX_ErrorSameState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorSameState;
         }
         /* Requesting transition from Idle to WaitForResources */
         else if (eState == OMX_StateWaitForResources) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: Idle-->WaitForResources\n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Idle to Pause */
         else if (eState == OMX_StatePause) {
            /* Since error is None , we will post an event at the end of this function definition */
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Idle-->Pause\n");
         }
         /* Requesting transition from Idle to Invalid */
         else if (eState == OMX_StateInvalid) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: Idle-->Invalid\n");
            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorInvalidState;
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "OMXCORE-SM: Idle --> %d Not Handled\n",
                     eState);
            eRet = OMX_ErrorBadParameter;
         }
      }

    /******************************/
      /* Current State is Executing */
    /******************************/
      else if (m_state == OMX_StateExecuting) {
         if (eState == OMX_StateIdle) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n OMXCORE-SM: Executing --> Idle \n");
            execute_omx_flush(OMX_ALL);
         } else if (eState == OMX_StatePause) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n OMXCORE-SM: Executing --> Paused \n");
         }
         /* Requesting transition from Executing to Loaded */
         else if (eState == OMX_StateLoaded) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n OMXCORE-SM: Executing --> Loaded \n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Executing to WaitForResources */
         else if (eState == OMX_StateWaitForResources) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n OMXCORE-SM: Executing --> WaitForResources \n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Executing to Executing */
         else if (eState == OMX_StateExecuting) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n OMXCORE-SM: Executing --> Executing \n");
            post_event(OMX_EventError, OMX_ErrorSameState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorSameState;
         }
         /* Requesting transition from Executing to Invalid */
         else if (eState == OMX_StateInvalid) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n OMXCORE-SM: Executing --> Invalid \n");
            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorInvalidState;
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "OMXCORE-SM: Executing --> %d Not Handled\n",
                     eState);
            eRet = OMX_ErrorBadParameter;
         }
      }
    /***************************/
      /* Current State is Pause  */
    /***************************/
      else if (m_state == OMX_StatePause) {
         if (eState == OMX_StateExecuting) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n Pause --> Executing \n");
         } else if (eState == OMX_StateIdle) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n Pause --> Idle \n");
            execute_omx_flush(OMX_ALL);
         }
         /* Requesting transition from Pause to loaded */
         else if (eState == OMX_StateLoaded) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n Pause --> loaded \n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Pause to WaitForResources */
         else if (eState == OMX_StateWaitForResources) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n Pause --> WaitForResources \n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from Pause to Pause */
         else if (eState == OMX_StatePause) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n Pause --> Pause \n");
            post_event(OMX_EventError, OMX_ErrorSameState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorSameState;
         }
         /* Requesting transition from Pause to Invalid */
         else if (eState == OMX_StateInvalid) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "\n Pause --> Invalid \n");
            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorInvalidState;
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "OMXCORE-SM: Paused --> %d Not Handled\n",
                     eState);
            eRet = OMX_ErrorBadParameter;
         }
      }
     /***************************/
      /* Current State is WaitForResources  */
    /***************************/
      else if (m_state == OMX_StateWaitForResources) {
         /* Requesting transition from WaitForResources to Loaded */
         if (eState == OMX_StateLoaded) {
            /* Since error is None , we will post an event at the end of this function definition */
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: WaitForResources-->Loaded\n");
         }
         /* Requesting transition from WaitForResources to WaitForResources */
         else if (eState == OMX_StateWaitForResources) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: WaitForResources-->WaitForResources\n");
            post_event(OMX_EventError, OMX_ErrorSameState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorSameState;
         }
         /* Requesting transition from WaitForResources to Executing */
         else if (eState == OMX_StateExecuting) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: WaitForResources-->Executing\n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from WaitForResources to Pause */
         else if (eState == OMX_StatePause) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: WaitForResources-->Pause\n");
            post_event(OMX_EventError,
                  OMX_ErrorIncorrectStateTransition,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorIncorrectStateTransition;
         }
         /* Requesting transition from WaitForResources to Invalid */
         else if (eState == OMX_StateInvalid) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "OMXCORE-SM: WaitForResources-->Invalid\n");
            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);
            eRet = OMX_ErrorInvalidState;
         }
         /* Requesting transition from WaitForResources to Loaded - is NOT tested by Khronos TS */
      } else {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "OMXCORE-SM: %d --> %d(Not Handled)\n",
                  m_state, eState);
         eRet = OMX_ErrorBadParameter;
      }
   }
  /********************************/
   /* Current State is Invalid */
    /*******************************/
   else if (m_state == OMX_StateInvalid) {
      /* State Transition from Inavlid to any state */
      if (eState ==
          (OMX_StateLoaded || OMX_StateWaitForResources
           || OMX_StateIdle || OMX_StateExecuting || OMX_StatePause
           || OMX_StateInvalid)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "OMXCORE-SM: Invalid -->Loaded\n");
         post_event(OMX_EventError, OMX_ErrorInvalidState,
               OMX_COMPONENT_GENERATE_EVENT);
         eRet = OMX_ErrorInvalidState;
      }
   } else if (cmd == OMX_CommandFlush) {
      execute_omx_flush(param1);
      if (0 == param1 || OMX_ALL == param1) {
         //generate input flush event only.
         post_event(OMX_CommandFlush, OMX_CORE_INPUT_PORT_INDEX,
               OMX_COMPONENT_GENERATE_EVENT_FLUSH);
      }
      if (1 == param1 || OMX_ALL == param1) {
         //generate output flush event only.
         post_event(OMX_CommandFlush, OMX_CORE_OUTPUT_PORT_INDEX,
               OMX_COMPONENT_GENERATE_EVENT_FLUSH);
      }
      bFlag = 0;
   } else if (cmd == OMX_CommandPortEnable) {
      if (param1 != OMX_CORE_OUTPUT_PORT_INDEX) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "OMXCORE-SM:Enable should be on Ouput Port\n");
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "OMXCORE-SM:Recieved command ENABLE (%d)\n", cmd);
      // call vdec_open
      if (!m_vdec && (m_first_pending_buf_idx >= 0)) {
         if (m_vendor_config.pData) {
            m_vdec_cfg.sequenceHeader =
                m_vendor_config.pData;
            m_vdec_cfg.sequenceHeaderLen =
                m_vendor_config.nDataSize;
            m_vdec_cfg.height = m_port_height;
            m_vdec_cfg.width = m_port_width;
            m_vdec_cfg.size_of_nal_length_field =
                m_nalu_bytes;
            m_vdec_cfg.color_format = m_color_format;
            m_vdec = vdec_open(&m_vdec_cfg);
            eRet =
                (m_vdec ==
                 NULL) ? OMX_ErrorHardware : OMX_ErrorNone;
         } else {
            eRet =
                omx_vdec_create_native_decoder(input
                           [m_first_pending_buf_idx]);
         }

         if (OMX_ErrorNone != eRet) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Native decoder creation failed\n");
            semaphore_post();
            return eRet;
         }
         OMX_BUFFERHEADERTYPE *tmp_buf_hdr =
             (OMX_BUFFERHEADERTYPE *) flush_before_vdec_op_q->
             Dequeue();
         while (tmp_buf_hdr) {
            vdec_release_frame(m_vdec,
                     (vdec_frame *) tmp_buf_hdr->
                     pOutputPortPrivate);
            tmp_buf_hdr =
                (OMX_BUFFERHEADERTYPE *)
                flush_before_vdec_op_q->Dequeue();
         }
#ifdef _ANDROID_
         OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo =
             m_pmem_info;
         // create IMemoryHeap object
         m_heap_ptr =
             new
             VideoHeap(((vdec_frame *) (&m_vdec_cfg.
                         outputBuffer[0]))->
                  buffer.pmem_id, m_vdec->arena[0].size,
                  ((vdec_frame *) (&m_vdec_cfg.
                         outputBuffer[0]))->
                  buffer.base);
         for (unsigned i = 0; i < m_out_buf_count; i++) {
            pPMEMInfo->pmem_fd = (OMX_U32) m_heap_ptr.get();
            pPMEMInfo++;
         }
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "VideoHeap : fd %d data %d size %d\n",
                  ((vdec_frame *) (&m_vdec_cfg.
                         outputBuffer[0]))->
                  buffer.pmem_id,
                  ((vdec_frame *) (&m_vdec_cfg.
                         outputBuffer[0]))->
                  buffer.base, m_vdec->arena[0].size);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "m_heap_ptr =%d",
                  (unsigned)m_heap_ptr.get());
#endif //_ANDROID_
      }

      if (param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL) {
         m_inp_bEnabled = OMX_TRUE;

         if ((m_state == OMX_StateLoaded
              && !BITMASK_PRESENT(m_flags,
                   OMX_COMPONENT_IDLE_PENDING))
             || allocate_input_done()) {
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_INPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Disabled-->Enabled Pending\n");
            BITMASK_SET(m_flags,
                   OMX_COMPONENT_INPUT_ENABLE_PENDING);

         }
         // Skip the event notification
         bFlag = 0;
      }
      if (param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL) {
         m_out_bEnabled = OMX_TRUE;

         if ((m_state == OMX_StateLoaded
              && !BITMASK_PRESENT(m_flags,
                   OMX_COMPONENT_IDLE_PENDING))
             || (allocate_output_done())) {
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_OUTPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);

         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "OMXCORE-SM: Disabled-->Enabled Pending\n");
            BITMASK_SET(m_flags,
                   OMX_COMPONENT_OUTPUT_ENABLE_PENDING);

         }
         // Skip the event notification
         bFlag = 0;
      }
   } else if (cmd == OMX_CommandPortDisable) {
      if (param1 != OMX_CORE_OUTPUT_PORT_INDEX) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "OMXCORE-SM:Disable should be on Ouput Port\n");
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "OMXCORE-SM:Recieved command DISABLE (%d)\n",
               cmd);

      if (param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL) {
         m_inp_bEnabled = OMX_FALSE;
         if ((m_state == OMX_StateLoaded
              || m_state == OMX_StateIdle)
             && release_input_done()) {
            post_event(OMX_CommandPortDisable,
                  OMX_CORE_INPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
         } else {
            BITMASK_SET(m_flags,
                   OMX_COMPONENT_INPUT_DISABLE_PENDING);
            if (m_state == OMX_StatePause
                || m_state == OMX_StateExecuting) {
               execute_omx_flush
                   (OMX_CORE_INPUT_PORT_INDEX);
            }

         }
         // Skip the event notification
         bFlag = 0;
      }
      if (param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL) {
         m_out_bEnabled = OMX_FALSE;

         if ((m_state == OMX_StateLoaded
              || m_state == OMX_StateIdle)
             && release_output_done()) {
            post_event(OMX_CommandPortDisable,
                  OMX_CORE_OUTPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
         } else {
            BITMASK_SET(m_flags,
                   OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
            if (m_state == OMX_StatePause
                || m_state == OMX_StateExecuting) {
               execute_omx_flush
                   (OMX_CORE_OUTPUT_PORT_INDEX);
            }
         }
         // Skip the event notification
         bFlag = 0;
      }

   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error: Invalid Command received other than StateSet (%d)\n",
               cmd);
      eRet = OMX_ErrorNotImplemented;
   }
   if (eRet == OMX_ErrorNone && bFlag) {
      post_event(cmd, eState, OMX_COMPONENT_GENERATE_EVENT);
   }
   semaphore_post();
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
   bool bRet = false;
   if (flushType == 0 || flushType == OMX_ALL) {
      //flush input only
      execute_input_flush();
   }
   if (flushType == 1 || flushType == OMX_ALL) {
      //flush output only
      execute_output_flush();
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
bool omx_vdec::execute_output_flush(void)
{
   unsigned i;
   bool canceled;
   bool bRet = false;
   OMX_BUFFERHEADERTYPE *pOutBufHdr =
       (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
   unsigned int p1 = 0;
   unsigned int p2 = 0;
   unsigned id = 0;
   int nFlushFrameCnt = 0;

   if (pOutBufHdr) {
      unsigned nPortIndex;
      //We will ignore this Queue once m_vdec is created. This will cause no harm
      //now since when output buffers are created in m_vdec, m_vdec assumes that
      //all buffers are with itself.
      OMX_BUFFERHEADERTYPE *tmp_buf_hdr =
          (OMX_BUFFERHEADERTYPE *) flush_before_vdec_op_q->Dequeue();
      while (tmp_buf_hdr) {
         nPortIndex =
             tmp_buf_hdr -
             ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr);
         BITMASK_CLEAR(m_out_flags, nPortIndex);
         m_cb.FillBufferDone(&m_cmp, m_app_data, tmp_buf_hdr);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "Flushing output buffer = %d",
                  m_out_buf_count);
         tmp_buf_hdr =
             (OMX_BUFFERHEADERTYPE *) flush_before_vdec_op_q->
             Dequeue();
      }
   }
   if (m_vdec && m_vdec->is_commit_memory) {
      /* . Execute the decoder flush */
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "\n *** Calling vdec_flush ***  \n");
      if (VDEC_SUCCESS ==
          vdec_flush_port(m_vdec, &nFlushFrameCnt,
                VDEC_FLUSH_ALL_PORT)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n *** Flush Succeeded : Flush Frame Cnt %d *** \n",
                  nFlushFrameCnt);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "\n *** Flush Failed *** \n");
      }
   }
   while (m_ftb_q.m_size > 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "Flushing FTB Q\n");
      m_ftb_q.delete_entry(&p1, &p2, &id, &canceled);
      if (!canceled)
         m_cb.FillBufferDone(&m_cmp, m_app_data,
                   (OMX_BUFFERHEADERTYPE *) p2);
   }
   if (m_b_display_order)
   {
      if(m_pPrevFrame) {
       frame_done_cb((struct vdec_context *)&m_vdec_cfg, m_pPrevFrame);
       m_pPrevFrame = NULL;
      }
      m_divX_buffer_info.parsing_required = true;
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
========================================================================== */
bool omx_vdec::execute_input_flush(void)
{
   bool bRet = false;

   OMX_BUFFERHEADERTYPE *pInpBufHdr =
       (OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr;
   if (!pInpBufHdr) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Omx Flush issued at wrong context\n");
   } else {
      int nFlushFrameCnt = 0;
      int i, index;
      unsigned int p1, p2, ident;
      /* 1. Take care of the pending buffers in the input side */
      /* During flush clear the pending buffer index. Otherwise FTB followed by flush
       ** could push duplicate frames to the decoder
       */
      if (m_bArbitraryBytes) {
         if (m_current_arbitrary_bytes_input) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     " Flush:: current arbitrary bytes not null %x ",
                     m_current_arbitrary_bytes_input);
            m_current_arbitrary_bytes_input->nFilledLen =
                                     m_current_arbitrary_bytes_input->nOffset;
            index = get_free_extra_buffer_index();
            if (index != -1) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        " Flush:flushing current bufferindex  %d ",
                        index);
               m_extra_buf_info[index].
                   arbitrarybytesInput =
                   m_current_arbitrary_bytes_input;
               m_current_arbitrary_bytes_input = NULL;
            }
            else {
               m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                          m_current_arbitrary_bytes_input);
            }
         }
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  " Flush:: qsize %d ",
                  m_etb_arbitrarybytes_q.m_size);
         while (m_etb_arbitrarybytes_q.
                delete_entry(&p1, &p2, &ident)) {
            index = get_free_extra_buffer_index();
            if (index == -1) {
               m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                          (OMX_BUFFERHEADERTYPE
                           *) p2);
               continue;
            }
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "Generating the buffer done in flush, qsize %d\n",
                     m_etb_arbitrarybytes_q.m_size);
            pInpBufHdr = (OMX_BUFFERHEADERTYPE *) p2;
            m_extra_buf_info[index].arbitrarybytesInput =
                pInpBufHdr;
/* since these buffers are not processed by vdec, below statment should make sure that the
   nFilledLen is not zero, so that client knows that vdec didnt consume the buffer*/
            m_extra_buf_info[index].arbitrarybytesInput->nOffset=
               m_extra_buf_info[index].arbitrarybytesInput->nFilledLen;
            //post_event((unsigned)&m_vdec_cfg,(unsigned)pInpBufHdr,OMX_COMPONENT_GENERATE_BUFFER_DONE);
         }
         for (i = 0; i < m_inp_buf_count; i++) {
            if (m_extra_buf_info[i].bExtra_pBuffer_in_use) {
               input[i]->pBuffer =
                   m_extra_buf_info[i].extra_pBuffer;
               input[i]->nOffset = 0;
               input[i]->nFilledLen = 0;
               input[i]->nFlags = 0;
               if (m_extra_buf_info[i].arbitrarybytesInput) {
                 m_extra_buf_info[i].arbitrarybytesInput->nFilledLen =
                         m_extra_buf_info[i].arbitrarybytesInput->nOffset;
                 m_extra_buf_info[i].arbitrarybytesInput->nOffset = 0;
               }
               remove_top_entry();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Generating the buffer done in flush, index %d\n",
                        i);
               buffer_done_cb_stub(&m_vdec_cfg,
                         input[i]);
            }

         }
         if (m_bArbitraryBytes) {
            initialize_arbitrary_bytes_environment();
         }

         if (m_h264_utils) {
            m_h264_utils->
                initialize_frame_checking_environment();
         }

      } else {
         do {
            if (m_bAccumulate_subframe) {
               if (m_pcurrent_frame) {
                  unsigned nBufferIndex =
                      m_pcurrent_frame -
                      ((OMX_BUFFERHEADERTYPE *)
                       m_inp_mem_ptr);
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "Found a valid current Frame in Flush, index %d\n",
                           nBufferIndex);
                  add_entry(nBufferIndex);
               }
               m_pcurrent_frame = NULL;
               m_bPartialFrame = false;
               if (m_h264_utils) {
                  m_h264_utils->
                      initialize_frame_checking_environment
                      ();
               }
            }
            i = remove_top_entry();
            if (i >= 0) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Generating the buffer done in flush, i %d\n",
                        i);
               pInpBufHdr = input[i];
               post_event((unsigned)&m_vdec_cfg,
                     (unsigned)pInpBufHdr,
                     OMX_COMPONENT_GENERATE_BUFFER_DONE);
            }
         } while (i >= 0);
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
bool omx_vdec::post_event(unsigned int p1, unsigned int p2, unsigned int id)
{
   bool bRet = false;

   mutex_lock();
   m_cmd_cnt++;
   if (id == OMX_COMPONENT_GENERATE_FTB) {
      m_ftb_q.insert_entry(p1, p2, id);
   } else if ((id == OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES)) {
      m_etb_arbitrarybytes_q.insert_entry(p1, p2, id);
   } else {
      m_cmd_q.insert_entry(p1, p2, id);
   }

   bRet = true;
   post_message(id);
   mutex_unlock();
   // for all messages that needs a callback before
   // vdec is actually opened, skips the proxy
   QTV_MSG_PRIO8(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "Post -->%d[%x,%d]ebd %d fbd %d oc %d %x,%x \n", m_state,
            (unsigned)m_vdec, id, m_etb_cnt, m_fbd_cnt,
            m_outstanding_frames, m_flags[0] , m_out_flags[0]);
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
OMX_ERRORTYPE omx_vdec::get_parameter(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_IN OMX_INDEXTYPE paramIndex,
                  OMX_INOUT OMX_PTR paramData)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "get_parameter: \n");
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Get Param in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (paramData == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Get Param in Invalid paramData \n");
      return OMX_ErrorBadParameter;
   }

   switch (paramIndex) {
   case OMX_IndexParamPortDefinition:
      {
         OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
         portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamPortDefinition\n");

         portDefn->nVersion.nVersion = OMX_SPEC_VERSION;
         portDefn->nSize = sizeof(portDefn);
         portDefn->bEnabled = OMX_TRUE;
         portDefn->bPopulated = OMX_TRUE;
         portDefn->eDomain = OMX_PortDomainVideo;
         portDefn->format.video.nFrameHeight = m_crop_dy;
         portDefn->format.video.nFrameWidth = m_crop_dx;
         portDefn->format.video.nStride = m_port_width;
         portDefn->format.video.nSliceHeight = m_port_height;
         portDefn->format.video.xFramerate = 25;

         if (0 == portDefn->nPortIndex) {
            VDecoder_buf_info bufferReq;

            if (!m_inp_buf_count) {
               if (VDEC_SUCCESS !=
                   vdec_get_input_buf_requirements
                   (&bufferReq, m_vdec_cfg.postProc)) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "get_parameter: ERROR - Failed in get input buffer requirement\n");
                  m_inp_buf_count =
                      OMX_CORE_NUM_INPUT_BUFFERS;
                  m_inp_buf_size =
                      OMX_CORE_INPUT_BUFFER_SIZE;
               } else {
                  m_inp_buf_count =
                      bufferReq.numbuf;
                  m_inp_buf_size =
                      bufferReq.buffer_size;
               }
            }
            portDefn->nBufferCountActual = m_inp_buf_count;
            portDefn->nBufferCountMin = m_inp_buf_count;                //OMX_CORE_NUM_INPUT_BUFFERS;
            portDefn->nBufferSize = m_inp_buf_size;
            portDefn->eDir = OMX_DirInput;
            portDefn->format.video.eColorFormat =
                OMX_COLOR_FormatUnused;
            portDefn->format.video.eCompressionFormat =
                OMX_VIDEO_CodingAVC;
            portDefn->bEnabled = m_inp_bEnabled;
            portDefn->bPopulated = m_inp_bPopulated;
         } else if (1 == portDefn->nPortIndex) {
	    int extraDataSize,chroma_height, chroma_width;
            portDefn->eDir = OMX_DirOutput;
            if (m_vdec) {
               portDefn->nBufferCountActual =
                   m_vdec_cfg.numOutputBuffers;
               portDefn->nBufferCountMin =
                   m_vdec_cfg.numOutputBuffers;
            } else {
               portDefn->nBufferCountActual =
                   m_out_buf_count;
               portDefn->nBufferCountMin =
                   m_out_buf_count;
            }
            portDefn->nBufferSize = get_output_buffer_size();
            portDefn->format.video.eColorFormat =
                m_color_format;
            portDefn->format.video.eCompressionFormat =
                OMX_VIDEO_CodingUnused;
            portDefn->bEnabled = m_out_bEnabled;
            portDefn->bPopulated = m_out_bPopulated;
         } else {
            portDefn->eDir = OMX_DirMax;
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     " get_parameter: Bad Port idx %d",
                     (int)portDefn->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
         }

         break;
      }
   case OMX_IndexParamVideoInit:
      {
         OMX_PORT_PARAM_TYPE *portParamType =
             (OMX_PORT_PARAM_TYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamVideoInit\n");

         portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
         portParamType->nSize = sizeof(portParamType);
         portParamType->nPorts = 2;
         portParamType->nStartPortNumber = 0;
         break;
      }
   case OMX_IndexParamVideoPortFormat:
      {
         OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
             (OMX_VIDEO_PARAM_PORTFORMATTYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamVideoPortFormat\n");

         portFmt->nVersion.nVersion = OMX_SPEC_VERSION;
         portFmt->nSize = sizeof(portFmt);

         if (0 == portFmt->nPortIndex) {
            if (0 == portFmt->nIndex) {
               if (!strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.avc",
                    OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      OMX_VIDEO_CodingAVC;
               } else
                   if (!strncmp
                  (m_vdec_cfg.kind,
                   "OMX.qcom.video.decoder.h263",
                   OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      OMX_VIDEO_CodingH263;
               } else
                   if (!strncmp
                  (m_vdec_cfg.kind,
                   "OMX.qcom.video.decoder.mpeg4",
                   OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      OMX_VIDEO_CodingMPEG4;
               } else
                   if (!strncmp
                  (m_vdec_cfg.kind,
                   "OMX.qcom.video.decoder.vc1",
                   OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      OMX_VIDEO_CodingWMV;
               }else
                   if (!strncmp
                  (m_vdec_cfg.kind,
                   "OMX.qcom.video.decoder.divx",
                   OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
               } else
                   if (!strncmp
                  (m_vdec_cfg.kind,
                   "OMX.qcom.video.decoder.vp",
                   OMX_MAX_STRINGNAME_SIZE)) {
                  portFmt->eColorFormat =
                      OMX_COLOR_FormatUnused;
                  portFmt->eCompressionFormat =
                      (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingVp;
              } else
                    if (!strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.spark",
                    OMX_MAX_STRINGNAME_SIZE)) {
                   portFmt->eColorFormat =
                       OMX_COLOR_FormatUnused;
                   portFmt->eCompressionFormat =
                       (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingSpark;
                }

            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_ERROR,
                       "get_parameter: OMX_IndexParamVideoPortFormat:"
                       " NoMore compression formats\n");
               eRet = OMX_ErrorNoMore;
            }
         } else if (1 == portFmt->nPortIndex) {
            if (1 == portFmt->nIndex) {
               portFmt->eColorFormat =
                   (OMX_COLOR_FORMATTYPE)
                   QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka;
               portFmt->eCompressionFormat =
                   OMX_VIDEO_CodingUnused;
            } else if (0 == portFmt->nIndex) {
               portFmt->eColorFormat =
                   (OMX_COLOR_FORMATTYPE)
                   OMX_QCOM_COLOR_FormatYVU420SemiPlanar;
               portFmt->eCompressionFormat =
                   OMX_VIDEO_CodingUnused;
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "get_parameter: OMX_IndexParamVideoPortFormat:"
                       " NoMore Color formats\n");
               eRet = OMX_ErrorNoMore;
            }
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "get_parameter: Bad port index %d\n",
                     (int)portFmt->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
         }
         break;
      }
      /*Component should support this port definition */
   case OMX_IndexParamAudioInit:
      {
         OMX_PORT_PARAM_TYPE *audioPortParamType =
             (OMX_PORT_PARAM_TYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamAudioInit\n");
         audioPortParamType->nVersion.nVersion =
             OMX_SPEC_VERSION;
         audioPortParamType->nSize = sizeof(audioPortParamType);
         audioPortParamType->nPorts = 0;
         audioPortParamType->nStartPortNumber = 0;
         break;
      }
      /*Component should support this port definition */
   case OMX_IndexParamImageInit:
      {
         OMX_PORT_PARAM_TYPE *imagePortParamType =
             (OMX_PORT_PARAM_TYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamImageInit\n");
         imagePortParamType->nVersion.nVersion =
             OMX_SPEC_VERSION;
         imagePortParamType->nSize = sizeof(imagePortParamType);
         imagePortParamType->nPorts = 0;
         imagePortParamType->nStartPortNumber = 0;
         break;

      }
      /*Component should support this port definition */
   case OMX_IndexParamOtherInit:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "get_parameter: OMX_IndexParamOtherInit %08x\n",
                  paramIndex);
         eRet = OMX_ErrorUnsupportedIndex;
         break;
      }
   case OMX_IndexParamStandardComponentRole:
      {
         OMX_PARAM_COMPONENTROLETYPE *comp_role;
         comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
         comp_role->nVersion.nVersion = OMX_SPEC_VERSION;
         comp_role->nSize = sizeof(*comp_role);

         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Getparameter: OMX_IndexParamStandardComponentRole %d\n",
                  paramIndex);
         if (NULL != comp_role->cRole) {
            strncpy((char *)comp_role->cRole,
               (const char *)m_cRole,
               OMX_MAX_STRINGNAME_SIZE);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "Getparameter: OMX_IndexParamStandardComponentRole %d is passed with NULL parameter for role\n",
                     paramIndex);
            eRet = OMX_ErrorBadParameter;
         }
         break;
      }
      /* Added for parameter test */
   case OMX_IndexParamPriorityMgmt:
      {
         OMX_PRIORITYMGMTTYPE *priorityMgmType =
             (OMX_PRIORITYMGMTTYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamPriorityMgmt\n");
         priorityMgmType->nVersion.nVersion = OMX_SPEC_VERSION;
         priorityMgmType->nSize = sizeof(priorityMgmType);

         break;
      }
      /* Added for parameter test */
   case OMX_IndexParamCompBufferSupplier:
      {
         OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType =
             (OMX_PARAM_BUFFERSUPPLIERTYPE *) paramData;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_parameter: OMX_IndexParamCompBufferSupplier\n");

         bufferSupplierType->nSize = sizeof(bufferSupplierType);
         bufferSupplierType->nVersion.nVersion =
             OMX_SPEC_VERSION;
         if (0 == bufferSupplierType->nPortIndex)
            bufferSupplierType->nPortIndex =
                OMX_BufferSupplyUnspecified;
         else if (1 == bufferSupplierType->nPortIndex)
            bufferSupplierType->nPortIndex =
                OMX_BufferSupplyUnspecified;
         else
            eRet = OMX_ErrorBadPortIndex;

         break;
      }
   case OMX_IndexParamVideoAvc:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoAvc %08x\n",
                  paramIndex);
         break;
      }

   case OMX_QcomIndexParamInterlaced:
      {
         OMX_QCOM_PARAM_VIDEO_INTERLACETYPE *portInterlace =
             (OMX_QCOM_PARAM_VIDEO_INTERLACETYPE *) paramData;
         if (portInterlace->nPortIndex == 1)
            portInterlace->bInterlace =
                (OMX_BOOL) m_bInterlaced;
         else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "get_parameter: Bad port index %d should be queried on only o/p port\n",
                     (int)portInterlace->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
         }

         break;
      }
   case OMX_IndexParamVideoProfileLevelQuerySupported:
      {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported %08x\n",
                  paramIndex);
          OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevelType =
             (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)paramData;
          if(profileLevelType->nPortIndex == 0) {
             if (!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",OMX_MAX_STRINGNAME_SIZE))
             {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile = OMX_VIDEO_AVCProfileBaseline;
                   profileLevelType->eLevel   = OMX_VIDEO_AVCLevel32;
                }
                else if (profileLevelType->nProfileIndex == 1)
                {
                   profileLevelType->eProfile = OMX_VIDEO_AVCProfileMain;
                   profileLevelType->eLevel   = OMX_VIDEO_AVCLevel32;
                }
                else if (profileLevelType->nProfileIndex == 2)
                {
                   profileLevelType->eProfile = OMX_VIDEO_AVCProfileHigh;
                   profileLevelType->eLevel   = OMX_VIDEO_AVCLevel32;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
             }
             else if((!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",OMX_MAX_STRINGNAME_SIZE)))
             {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile = OMX_VIDEO_H263ProfileBaseline;
                   profileLevelType->eLevel   = OMX_VIDEO_H263Level30;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
             }
             else if (!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
             {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile = OMX_VIDEO_MPEG4ProfileSimple;
                   profileLevelType->eLevel   = OMX_VIDEO_MPEG4Level4a;
                }
                else if (profileLevelType->nProfileIndex == 1)
                {
                   profileLevelType->eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                   profileLevelType->eLevel   = OMX_VIDEO_MPEG4Level5;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
            }
             else if (!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",OMX_MAX_STRINGNAME_SIZE))
             {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                   profileLevelType->eLevel   = OMX_VIDEO_MPEG4Level5;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
            }
            else if (!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp",OMX_MAX_STRINGNAME_SIZE))
            {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile =  QOMX_VIDEO_VPProfileAdvanced;
                   profileLevelType->eLevel   = QOMX_VIDEO_VPFormat6;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
            }
            else if((!strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",OMX_MAX_STRINGNAME_SIZE)))
             {
                if (profileLevelType->nProfileIndex == 0)
                {
                   profileLevelType->eProfile = OMX_VIDEO_H263ProfileBaseline;
                   profileLevelType->eLevel   = OMX_VIDEO_H263Level60;
                }
                else
                {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported nProfileIndex ret NoMore %d\n",
                  profileLevelType->nProfileIndex);
                   eRet = OMX_ErrorNoMore;

                }
             }

          }
          else
          {
             QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
            "get_parameter: OMX_IndexParamVideoProfileLevelQuerySupported should be queries on Input port only %d\n",
            profileLevelType->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
          }
        break;
      }
   default:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "get_parameter: unknown param %08x\n",
                  paramIndex);
         eRet = OMX_ErrorUnsupportedIndex;
      }

   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "\n get_parameter returning Height %d , Width %d \n",
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
OMX_ERRORTYPE omx_vdec::set_parameter(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_IN OMX_INDEXTYPE paramIndex,
                  OMX_IN OMX_PTR paramData)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   int i;

   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Set Param in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (paramData == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Set Param in Invalid paramData \n");
      return OMX_ErrorBadParameter;
   }
   switch (paramIndex) {
   case OMX_IndexParamPortDefinition:
      {
         OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
         portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d\n",
                  (int)portDefn->format.video.nFrameHeight,
                  (int)portDefn->format.video.nFrameWidth);

         if (((m_state == OMX_StateLoaded) &&
              !BITMASK_PRESENT(m_flags,
                     OMX_COMPONENT_IDLE_PENDING))
             /* Or while the I/P or the O/P port or disabled */
             ||
             ((OMX_DirInput == portDefn->eDir
               && m_inp_bEnabled == FALSE)
              || (OMX_DirOutput == portDefn->eDir
             && m_out_bEnabled == FALSE))) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Set Parameter called in valid state");
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Set Parameter called in Invalid State\n");
            return OMX_ErrorIncorrectStateOperation;
         }

         if (OMX_DirOutput == portDefn->eDir) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "set_parameter: OMX_IndexParamPortDefinition on output port\n");
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "set_parameter op port: stride %d\n",
                     (int)portDefn->format.video.
                     nStride);
            /*
               If actual buffer count is greater than the Min buffer
               count,change the actual count.
               m_inp_buf_count is initialized to OMX_CORE_NUM_INPUT_BUFFERS
               in the constructor
             */
            if (portDefn->nBufferCountActual >
                m_out_buf_count) {
               m_out_buf_count =
                   portDefn->nBufferCountActual;
            } else if (portDefn->nBufferCountActual <
                  m_out_buf_count) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        " Set_parameter: Actual buffer count = %d less than Min Buff count",
                        portDefn->
                        nBufferCountActual);
               eRet = OMX_ErrorBadParameter;
            }
            /* Save the DisplayID to be used in useEGLImage api to query the
               pmem fd and offset from GPU client.
            */
            m_display_id = portDefn->format.video.pNativeWindow;

         } else if (OMX_DirInput == portDefn->eDir) {
            if (m_height !=
                portDefn->format.video.nFrameHeight
                || m_width !=
                portDefn->format.video.nFrameWidth) {
               m_crop_x = m_crop_y = 0;
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "set_parameter ip port: stride %d\n",
                        (int)portDefn->format.
                        video.nStride);
               // Re-start case since image dimensions have changed
               QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "\nSetParameter: Dimension mismatch.  Old H: %d New H: %d Old W: %d New W: %d\n",
                        m_height,
                        (int)portDefn->format.
                        video.nFrameHeight,
                        m_width,
                        (int)portDefn->format.
                        video.nFrameWidth);
               m_crop_dy = m_port_height =
                   m_vdec_cfg.height = m_height =
                   portDefn->format.video.nFrameHeight;
               m_crop_dx = m_port_width =
                   m_vdec_cfg.width = m_width =
                   portDefn->format.video.nFrameWidth;

               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "\n VDEC Open with new H %d and W %d\n",
                        m_height, m_width);

               if ((m_height % 16) != 0) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "\n Height %d is not a multiple of 16",
                           m_height);
                  m_vdec_cfg.height =
                      (m_height / 16 + 1) * 16;
                  QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "\n Height %d adjusted to %d \n",
                           m_height,
                           m_vdec_cfg.
                           height);
               }

               if (m_width % 16 != 0) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "\n Width %d is not a multiple of 16",
                           m_width);
                  m_vdec_cfg.width =
                      (m_width / 16 + 1) * 16;
                  QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "\n Width %d adjusted to %d \n",
                           m_width,
                           m_vdec_cfg.width);
               }

            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "\n set_parameter: Image Dimensions same  \n");
            }
            /*
               If actual buffer count is greater than the Min buffer
               count,change the actual count.
               m_inp_buf_count is initialized to OMX_CORE_NUM_INPUT_BUFFERS
               in the constructor
             */
            if (portDefn->nBufferCountActual >
                OMX_CORE_NUM_INPUT_BUFFERS) {
               m_inp_buf_count =
                   portDefn->nBufferCountActual;
            } else if (portDefn->nBufferCountActual < OMX_CORE_MIN_INPUT_BUFFERS) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        " Set_parameter: Actual buffer count = %d less than Min Buff count",
                        portDefn->
                        nBufferCountActual);
               eRet = OMX_ErrorBadParameter;
            }

         } else if (portDefn->eDir == OMX_DirMax) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     " Set_parameter: Bad Port idx %d",
                     (int)portDefn->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
         }
      }
      break;

   case OMX_IndexParamVideoPortFormat:
      {
         OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
             (OMX_VIDEO_PARAM_PORTFORMATTYPE *) paramData;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "set_parameter: OMX_IndexParamVideoPortFormat %d\n",
                  portFmt->eColorFormat);
         if (1 == portFmt->nPortIndex) {
            m_color_format = portFmt->eColorFormat;
         }
      }
      break;

   case OMX_QcomIndexPortDefn:
      {
         OMX_QCOM_PARAM_PORTDEFINITIONTYPE *portFmt =
             (OMX_QCOM_PARAM_PORTDEFINITIONTYPE *) paramData;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexQcomParamPortDefinitionType %d\n",
                  portFmt->nFramePackingFormat);

         if (portFmt->nPortIndex == 0)   // Input port
         {
            if (portFmt->nFramePackingFormat ==
                OMX_QCOM_FramePacking_Arbitrary) {
               m_bArbitraryBytes = true;
               if (strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.avc",
                    26) == 0
                   ||
                   (strncmp
                    (m_vdec_cfg.kind,
                     "OMX.qcom.video.decoder.vc1",
                     26) == 0)) {
                  m_bAccumulate_subframe = true;
               } else if (strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.vp",
                    25) == 0) {
                     QTV_MSG_PRIO(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: OMX_QcomIndexPortDefn - Arbitrary not supported for VP6");
                    m_bArbitraryBytes = false;
                    m_bAccumulate_subframe = false;
                    eRet = OMX_ErrorUnsupportedSetting;
               }
            } else if (portFmt->nFramePackingFormat ==
                  OMX_QCOM_FramePacking_OnlyOneCompleteFrame)
            {
               m_bArbitraryBytes = false;
               m_bAccumulate_subframe = false;
            } else if (portFmt->nFramePackingFormat ==
                  OMX_QCOM_FramePacking_OnlyOneCompleteSubFrame)
            {
               m_bArbitraryBytes = false;
               if (strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.avc",
                    26) == 0
                   ||
                   (strncmp
                    (m_vdec_cfg.kind,
                     "OMX.qcom.video.decoder.vc1",
                     26) == 0)) {
                  m_bAccumulate_subframe = true;
               } else if (strncmp
                   (m_vdec_cfg.kind,
                    "OMX.qcom.video.decoder.vp",
                    25) == 0) {
                     QTV_MSG_PRIO(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: OMX_QcomIndexPortDefn - Subframe not supported for VP6");
                    m_bArbitraryBytes = false;
                    m_bAccumulate_subframe = false;
                    eRet = OMX_ErrorUnsupportedSetting;
               }
            }
         }
      }
      break;
   case OMX_IndexParamStandardComponentRole:
      {
         OMX_PARAM_COMPONENTROLETYPE *comp_role;
         comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexParamStandardComponentRole %s\n",
                  comp_role->cRole);
         if (!strncmp
             (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
              OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((char *)comp_role->cRole,
                 "video_decoder.avc",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.avc",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
            (m_vdec_cfg.kind,
             "OMX.qcom.video.decoder.mpeg4",
             OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((const char *)comp_role->cRole,
                 "video_decoder.mpeg4",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.mpeg4",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",
             OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((const char *)comp_role->cRole,
                 "video_decoder.h263",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.h263",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",
             OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((const char *)comp_role->cRole,
                 "video_decoder.divx",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.divx",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
             OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((const char *)comp_role->cRole,
                 "video_decoder.vc1",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.vc1",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
            (m_vdec_cfg.kind,
             "OMX.qcom.video.decoder.vp",
             OMX_MAX_STRINGNAME_SIZE)) {
            if (!strncmp
                ((const char *)comp_role->cRole,
                 "video_decoder.vp",
                 OMX_MAX_STRINGNAME_SIZE)) {
               strncpy((char *)m_cRole,
                  "video_decoder.vp",
                  OMX_MAX_STRINGNAME_SIZE);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "Setparameter: unknown Index %s\n",
                        comp_role->cRole);
               eRet = OMX_ErrorUnsupportedSetting;
            }
         } else
             if (!strncmp
                (m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",
                 OMX_MAX_STRINGNAME_SIZE)) {
                if (!strncmp
                    ((const char *)comp_role->cRole,
                     "video_decoder.spark",
                     OMX_MAX_STRINGNAME_SIZE)) {
                   strncpy((char *)m_cRole,
                      "video_decoder.spark",
                      OMX_MAX_STRINGNAME_SIZE);
                } else {
                   QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                            QTVDIAG_PRIO_ERROR,
                            "Setparameter: unknown Index %s\n",
                            comp_role->cRole);
                   eRet = OMX_ErrorUnsupportedSetting;
                }
          } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "Setparameter: unknown param %s\n",
                     m_vdec_cfg.kind);
            eRet = OMX_ErrorInvalidComponentName;
         }
         break;
      }

   case OMX_IndexParamPriorityMgmt:
      {
         if (m_state != OMX_StateLoaded) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Set Parameter called in Invalid State\n");
            return OMX_ErrorIncorrectStateOperation;
         }
         OMX_PRIORITYMGMTTYPE *priorityMgmtype =
             (OMX_PRIORITYMGMTTYPE *) paramData;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexParamPriorityMgmt %d\n",
                  priorityMgmtype->nGroupID);

         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: priorityMgmtype %d\n",
                  priorityMgmtype->nGroupPriority);

         m_priority_mgm.nGroupID = priorityMgmtype->nGroupID;
         m_priority_mgm.nGroupPriority =
             priorityMgmtype->nGroupPriority;

         break;
      }

   case OMX_IndexParamCompBufferSupplier:
      {
         OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType =
             (OMX_PARAM_BUFFERSUPPLIERTYPE *) paramData;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexParamCompBufferSupplier %d\n",
                  bufferSupplierType->eBufferSupplier);
         if (bufferSupplierType->nPortIndex == 0
             || bufferSupplierType->nPortIndex == 1)
            m_buffer_supplier.eBufferSupplier =
                bufferSupplierType->eBufferSupplier;

         else

            eRet = OMX_ErrorBadPortIndex;

         break;

      }
   case OMX_IndexParamVideoAvc:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_IndexParamVideoAvc %d\n",
                  paramIndex);
         break;
      }
   case OMX_QcomIndexParamVideoDivx:
      {
          QOMX_VIDEO_PARAM_DIVXTYPE *divxType =
             (QOMX_VIDEO_PARAM_DIVXTYPE *) paramData;
          QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoDivx %d\n",
                  paramIndex);
          if(divxType->nPortIndex == 0) {
             m_codec_format = divxType->eFormat;
             m_codec_profile = divxType->eProfile;
              QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoDivx Format %d, Profile %d\n",
                  m_codec_format,m_codec_profile);
              if(divxType->eFormat == QOMX_VIDEO_DIVXFormat311)
                 m_b_divX_parser = false;
          }
          else {
              QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "set_parameter: OMX_QcomIndexParamVideoDivx BAD PORT INDEX%d \n",
                  divxType->nPortIndex);
              eRet = OMX_ErrorBadPortIndex;
          }
        break;
      }
    case  OMX_QcomIndexParamVideoVp:
      {
          QOMX_VIDEO_PARAM_VPTYPE *vpType =
             (QOMX_VIDEO_PARAM_VPTYPE *) paramData;
          QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoVp %d\n",
                  paramIndex);
          if(vpType->nPortIndex == 0) {
             m_codec_format = vpType->eFormat;
             m_codec_profile = vpType->eProfile;
              QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoVp Format %d, Profile %d\n",
                  m_codec_format,m_codec_profile);
          }
          else {
              QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "set_parameter: OMX_QcomIndexParamVideoVp BAD PORT INDEX%d \n",
                  vpType->nPortIndex);
              eRet = OMX_ErrorBadPortIndex;
          }
        break;
      }
   case OMX_QcomIndexParamVideoSpark:
      {
          QOMX_VIDEO_PARAM_SPARKTYPE *sparkType =
             (QOMX_VIDEO_PARAM_SPARKTYPE *) paramData;

          QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoSpark %d\n",
                  paramIndex);

          if(sparkType->nPortIndex == 0) {

              m_codec_format = sparkType->eFormat;

              QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "set_parameter: OMX_QcomIndexParamVideoSpark Format %d\n",
                  m_codec_format);
          }
          else {

              QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "set_parameter: OMX_QcomIndexParamVideoSpark BAD PORT INDEX%d \n",
                  sparkType->nPortIndex);

              eRet = OMX_ErrorBadPortIndex;
          }
        break;
      }
   case OMX_QcomIndexParamVideoSyncFrameDecodingMode:
      {
         /* client sets vdec into thumbnail mode */
         QOMX_ENABLETYPE *enableType = (QOMX_ENABLETYPE *)paramData;
         if(enableType && TRUE == enableType->bEnable) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Decoder set to Thumbnail Mode");
               m_vdec_cfg.postProc = FLAG_THUMBNAIL_MODE;
            }
         break;
      }

   default:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Setparameter: unknown param %d\n",
                  paramIndex);
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
OMX_ERRORTYPE omx_vdec::get_config(OMX_IN OMX_HANDLETYPE hComp,
               OMX_IN OMX_INDEXTYPE configIndex,
               OMX_INOUT OMX_PTR configData)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;

   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Get Config in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   switch (configIndex) {
   case OMX_QcomIndexConfigInterlaced:
      {
         OMX_QCOM_CONFIG_INTERLACETYPE *configFmt =
             (OMX_QCOM_CONFIG_INTERLACETYPE *) configData;
         if (configFmt->nPortIndex == 1) {
            if (configFmt->nIndex == 0)
               configFmt->eInterlaceType =
                   OMX_QCOM_InterlaceFrameProgressive;
            else if (configFmt->nIndex == 1)
               configFmt->eInterlaceType =
                   OMX_QCOM_InterlaceInterleaveFrameTopFieldFirst;
            else if (configFmt->nIndex == 2)
               configFmt->eInterlaceType =
                   OMX_QCOM_InterlaceInterleaveFrameBottomFieldFirst;
            else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "get_config: OMX_QcomIndexConfigInterlaced:"
                       " NoMore Interlaced formats\n");
               eRet = OMX_ErrorNoMore;
            }

         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "get_config: Bad port index %d queried on only o/p port\n",
                     (int)configFmt->nPortIndex);
            eRet = OMX_ErrorBadPortIndex;
         }
         break;
      }
   default:
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "get_config: unknown param %d\n",
                  configIndex);
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
OMX_ERRORTYPE omx_vdec::set_config(OMX_IN OMX_HANDLETYPE hComp,
               OMX_IN OMX_INDEXTYPE configIndex,
               OMX_IN OMX_PTR configData)
{
   OMX_ERRORTYPE ret = OMX_ErrorNone;
   OMX_VIDEO_CONFIG_NALSIZE *pNal;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "set_config, store the NAL length size\n");
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Set Config in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (m_state == OMX_StateExecuting) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "set_config:Ignore in Exe state\n");
      return ret;
   }
   if (configIndex == OMX_IndexVendorVideoExtraData) {
      OMX_VENDOR_EXTRADATATYPE *config =
          (OMX_VENDOR_EXTRADATATYPE *) configData;
      if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc")) {
         OMX_U32 extra_size;
         // Parsing done here for the AVC atom is definitely not generic
         // Currently this piece of code is working, but certainly
         // not tested with all .mp4 files.
         // Incase of failure, we might need to revisit this
         // for a generic piece of code.

         // Retrieve size of NAL length field
         // byte #4 contains the size of NAL lenght field
         m_nalu_bytes = (config->pData[4] & 0x03) + 1;

         extra_size = 0;
         if (m_nalu_bytes > 2) {
            /* Presently we assume that only one SPS and one PPS in AvC1 Atom */
            extra_size = (m_nalu_bytes - 2) * 2;
         }
         // SPS starts from byte #6
         OMX_U8 *pSrcBuf = (OMX_U8 *) (&config->pData[6]);
         OMX_U8 *pDestBuf;
         m_vendor_config.nPortIndex = config->nPortIndex;

         // minus 6 --> SPS starts from byte #6
         // minus 1 --> picture param set byte to be ignored from avcatom
         m_vendor_config.nDataSize =
             config->nDataSize - 6 - 1 + extra_size;
         m_vendor_config.pData =
             (OMX_U8 *) malloc(m_vendor_config.nDataSize);
         OMX_U32 len;
         OMX_U8 index = 0;
         // case where SPS+PPS is sent as part of set_config
         pDestBuf = m_vendor_config.pData;

         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Rxd SPS+PPS nPortIndex[%d] len[%d] data[0x%x]\n",
                  m_vendor_config.nPortIndex,
                  m_vendor_config.nDataSize,
                  m_vendor_config.pData);
         while (index < 2) {
            uint8 *psize;
            len = *pSrcBuf;
            len = len << 8;
            len |= *(pSrcBuf + 1);
            psize = (uint8 *) & len;
            memcpy(pDestBuf + m_nalu_bytes, pSrcBuf + 2,
                   len);
            for (int i = 0; i < m_nalu_bytes; i++) {
               pDestBuf[i] =
                   psize[m_nalu_bytes - 1 - i];
            }
            //memcpy(pDestBuf,pSrcBuf,(len+2));
            pDestBuf += len + m_nalu_bytes;
            pSrcBuf += len + 2;
            index++;
            pSrcBuf++;   // skip picture param set
            len = 0;
         }
         m_header_state = HEADER_STATE_RECEIVED_COMPLETE;
      } else
          if (!strcmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4")) {
         m_vendor_config.nPortIndex = config->nPortIndex;
         m_vendor_config.nDataSize = config->nDataSize;
         m_vendor_config.pData =
             (OMX_U8 *) malloc((config->nDataSize));
         memcpy(m_vendor_config.pData, config->pData,
                config->nDataSize);
      } else
          if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1"))
      {
         if ((((*((OMX_U32 *) config->pData)) &
               VC1_SP_MP_START_CODE_MASK) ==
              VC1_SP_MP_START_CODE)
             ||
             (((*((OMX_U32 *) config->pData)) &
               VC1_SP_MP_START_CODE_MASK) ==
              VC1_SP_MP_START_CODE_RCV_V1)
             ) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                    "set_config - VC1 simple/main profile\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize = config->nDataSize;
            m_vendor_config.pData =
                (OMX_U8 *) malloc(config->nDataSize);
            memcpy(m_vendor_config.pData, config->pData,
                   config->nDataSize);
         } else if (*((OMX_U32 *) config->pData) == 0x0F010000) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                    "set_config - VC1 Advance profile\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize = config->nDataSize;
            m_vendor_config.pData =
                (OMX_U8 *) malloc((config->nDataSize));
            memcpy(m_vendor_config.pData, config->pData,
                   config->nDataSize);
         } else if ((config->nDataSize == VC1_STRUCT_C_LEN)) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_LOW,
                         "set_config - VC1 Simple/Main profile struct C only\n");
            m_vendor_config.nPortIndex = config->nPortIndex;
            m_vendor_config.nDataSize  = config->nDataSize;
            m_vendor_config.pData = (OMX_U8*)malloc(config->nDataSize);
            memcpy(m_vendor_config.pData,config->pData,config->nDataSize);
          }
         else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "set_config - Error: Unknown VC1 profile\n");
         }
      }
   } else if (configIndex == OMX_IndexConfigVideoNalSize) {
      pNal =
          reinterpret_cast < OMX_VIDEO_CONFIG_NALSIZE * >(configData);
      m_nalu_bytes = pNal->nNaluBytes;
      if (m_nalu_bytes == 0) {
         m_bStartCode = true;
      } else if (m_nalu_bytes < 0 || m_nalu_bytes > 4) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "set_config, invalid NAL length size [%d]\n",
                  m_nalu_bytes);
         m_nalu_bytes = 4;
         m_bStartCode = false;
         ret = OMX_ErrorBadParameter;
      } else {
         m_bStartCode = false;
      }
   }
   return ret;
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
OMX_ERRORTYPE omx_vdec::get_extension_index(OMX_IN OMX_HANDLETYPE hComp,
                   OMX_IN OMX_STRING paramName,
                   OMX_OUT OMX_INDEXTYPE * indexType)
{
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Get Extension Index in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   else if (!strncmp(paramName, "OMX.QCOM.index.param.video.SyncFrameDecodingMode",sizeof("OMX.QCOM.index.param.video.SyncFrameDecodingMode") - 1))
   {
      *indexType = (OMX_INDEXTYPE)OMX_QcomIndexParamVideoSyncFrameDecodingMode;
   }
   return OMX_ErrorNone;
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
OMX_ERRORTYPE omx_vdec::get_state(OMX_IN OMX_HANDLETYPE hComp,
              OMX_OUT OMX_STATETYPE * state)
{
   *state = m_state;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_state: Returning the state %d\n", *state);
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
OMX_ERRORTYPE omx_vdec::component_tunnel_request(OMX_IN OMX_HANDLETYPE hComp,
                   OMX_IN OMX_U32 port,
                   OMX_IN OMX_HANDLETYPE
                   peerComponent,
                   OMX_IN OMX_U32 peerPort,
                   OMX_INOUT OMX_TUNNELSETUPTYPE *
                   tunnelSetup)
{
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
           "Error: component_tunnel_request Not Implemented\n");
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
OMX_ERRORTYPE omx_vdec::use_input_buffer(OMX_IN OMX_HANDLETYPE hComp,
                OMX_INOUT OMX_BUFFERHEADERTYPE **
                bufferHdr, OMX_IN OMX_U32 port,
                OMX_IN OMX_PTR appData,
                OMX_IN OMX_U32 bytes,
                OMX_IN OMX_U8 * buffer)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   unsigned i,temp;      // Temporary counter
   temp = OMX_CORE_NUM_INPUT_BUFFERS;
   if (bytes <= OMX_CORE_INPUT_BUFFER_SIZE) {
      if (!m_inp_mem_ptr) {
         int nBufHdrSize =
             m_inp_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
         temp = BITMASK_SIZE(m_inp_buf_count);
         m_inp_mem_ptr =
             (char *)calloc((nBufHdrSize + temp), 1);
         m_use_pmem = 0;

         if (m_inp_mem_ptr) {
            // We have valid Input memory block here
            QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "Ist User Input Buffer(%d,%d,%d)\n",
                     m_inp_buf_count, nBufHdrSize,
                     m_inp_bm_count);
            if (m_bArbitraryBytes) {
               if (!m_arbitrary_bytes_input_mem_ptr) {
                  m_arbitrary_bytes_input_mem_ptr
                      =
                      (char *)
                      calloc((nBufHdrSize), 1);
               }
               *bufferHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_arbitrary_bytes_input_mem_ptr;
               bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_arbitrary_bytes_input_mem_ptr;
               //m_inp_bm_ptr  = ((char*)bufHdr) + nBufHdrSize ;
               m_arbitrary_bytes_input[0] =
                   *bufferHdr;;
            } else {
               *bufferHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_inp_mem_ptr;
               bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_inp_mem_ptr;
               //m_inp_bm_ptr  = ((char*)bufHdr) + nBufHdrSize ;
               input[0] = *bufferHdr;
            }
            BITMASK_SET(m_inp_bm_count, 0);
            // Settting the entire storage nicely
            for (i = 0; i < m_inp_buf_count; i++, bufHdr++) {
               memset(bufHdr, 0,
                      sizeof(OMX_BUFFERHEADERTYPE));
               bufHdr->nSize =
                   sizeof(OMX_BUFFERHEADERTYPE);
               bufHdr->nVersion.nVersion =
                   OMX_SPEC_VERSION;
               bufHdr->nAllocLen =
                   OMX_CORE_INPUT_BUFFER_SIZE;
               bufHdr->pAppPrivate = appData;
               bufHdr->nInputPortIndex =
                   OMX_CORE_INPUT_PORT_INDEX;
               //QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Input BufferHdr[%p] index[%d]\n",\
               bufHdr, i);
            }

            if (m_bArbitraryBytes) {
               input[0] = bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_inp_mem_ptr;
               for (i = 0; i < m_inp_buf_count;
                    i++, bufHdr++) {
                  memset(bufHdr, 0,
                         sizeof
                         (OMX_BUFFERHEADERTYPE));
                  bufHdr->pBuffer = NULL;
                  bufHdr->nSize =
                      sizeof
                      (OMX_BUFFERHEADERTYPE);
                  bufHdr->nVersion.nVersion =
                      OMX_SPEC_VERSION;
                  bufHdr->nAllocLen =
                      m_inp_buf_size;
                  bufHdr->pAppPrivate = appData;
                  bufHdr->nInputPortIndex =
                      OMX_CORE_INPUT_PORT_INDEX;
                  QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "arbitrarybytesInput Buffer %p arbitrarybytesInput[%p]\n",
                           bufHdr->pBuffer,
                           bufHdr);

                  if (m_extra_buf_info[i].
                      extra_pBuffer == NULL) {
                     m_extra_buf_info[i].
                         extra_pBuffer =
                         (OMX_U8 *)
                         malloc
                         (m_inp_buf_size);
                     if (m_extra_buf_info[i].
                         extra_pBuffer ==
                         NULL) {
                        QTV_MSG_PRIO
                            (QTVDIAG_GENERAL,
                             QTVDIAG_PRIO_ERROR,
                             "ERROR - Failed to get extra_pBuffer\n");
                        eRet =
                            OMX_ErrorInsufficientResources;
                        break;
                     }
                  }

                  memset(m_extra_buf_info[i].
                         extra_pBuffer, 0,
                         m_inp_buf_size);
                  m_extra_buf_info[i].
                      bExtra_pBuffer_in_use =
                      false;
                  m_extra_buf_info[i].
                      arbitrarybytesInput = NULL;
               }
            }
            omx_vdec_set_input_use_buf_flg();
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Input buffer memory allocation failed\n");
            eRet = OMX_ErrorInsufficientResources;
         }
      } else {
         for (i = 0; i < m_inp_buf_count; i++) {
            if (BITMASK_ABSENT(m_inp_bm_count, i)) {
               // bit space available
               break;
            }
         }
         if (i < m_inp_buf_count) {
            if (m_bArbitraryBytes) {
               *bufferHdr =
                   ((OMX_BUFFERHEADERTYPE *)
                    m_arbitrary_bytes_input_mem_ptr) +
                   i;
               m_arbitrary_bytes_input[i] = *bufferHdr;
               input[i] =
                   ((OMX_BUFFERHEADERTYPE *)
                    m_inp_mem_ptr) + i;
            } else {
               *bufferHdr =
                   ((OMX_BUFFERHEADERTYPE *)
                    m_inp_mem_ptr) + i;
               input[i] = *bufferHdr;
            }
            (*bufferHdr)->pAppPrivate = appData;
            BITMASK_SET(m_inp_bm_count, i);
         } else {
            eRet = OMX_ErrorInsufficientResources;
         }
      }
      (*bufferHdr)->pBuffer = (OMX_U8 *) buffer;
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "BUffer Header[%p] buffer=%p\n", *bufferHdr,
               (*bufferHdr)->pBuffer);
   } else {
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
OMX_ERRORTYPE omx_vdec::use_output_buffer(OMX_IN OMX_HANDLETYPE hComp,
                 OMX_INOUT OMX_BUFFERHEADERTYPE **
                 bufferHdr, OMX_IN OMX_U32 port,
                 OMX_IN OMX_PTR appData,
                 OMX_IN OMX_U32 bytes,
                 OMX_IN OMX_U8 * buffer) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   unsigned i;      // Temporary counter
   if (!m_out_mem_ptr) {
      int nBufHdrSize = 0;
      int nPlatformEntrySize = 0;
      int nPlatformListSize = 0;
      int nPMEMInfoSize = 0;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *pPlatformList;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *pPlatformEntry;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Ist Use Output Buffer(%d)\n", m_out_buf_count);
      nBufHdrSize = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
      nPMEMInfoSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO);
      nPlatformListSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_LIST);
      nPlatformEntrySize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_ENTRY);
      //m_out_bm_count     = BITMASK_SIZE(m_out_buf_count);
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "UOB::TotalBufHdr %d BufHdrSize %d PMEM %d PL %d\n",
               nBufHdrSize, sizeof(OMX_BUFFERHEADERTYPE),
               nPMEMInfoSize, nPlatformListSize);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "UOB::PE %d bmSize %d \n", nPlatformEntrySize,
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
      /*m_out_mem_ptr = (char *)calloc(nBufHdrSize +
         nPlatformListSize +
         nPlatformEntrySize +
         nPMEMInfoSize +m_out_bm_count, 1); */
      // Alloc mem for out buffer headers
      m_out_mem_ptr = (char *)calloc(nBufHdrSize, 1);
      // Alloc mem for platform specific info
      char *pPtr = NULL;
      pPtr = (char *)calloc(nPlatformListSize + nPlatformEntrySize +
                  nPMEMInfoSize, 1);
      // Alloc mem for maintaining a copy of use buf headers
      char *omxHdr = (char *)calloc(nBufHdrSize, 1);
      m_loc_use_buf_hdr = (OMX_BUFFERHEADERTYPE *) omxHdr;

      if (m_out_mem_ptr && m_loc_use_buf_hdr && pPtr) {
         bufHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         m_platform_list =
             (OMX_QCOM_PLATFORM_PRIVATE_LIST *) pPtr;
         m_platform_entry = (OMX_QCOM_PLATFORM_PRIVATE_ENTRY *)
             (((char *)m_platform_list) + nPlatformListSize);
         m_pmem_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
             (((char *)m_platform_entry) + nPlatformEntrySize);
         pPlatformList = m_platform_list;
         pPlatformEntry = m_platform_entry;
         pPMEMInfo = m_pmem_info;
         //m_out_bm_ptr   = (((char *) pPMEMInfo)     + nPMEMInfoSize);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "UOB::Memory Allocation Succeeded for OUT port%p\n",
                  m_out_mem_ptr);

         // Settting the entire storage nicely
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "UOB::bHdr %p OutMem %p PE %p pmem[%p]\n",
                  bufHdr, m_out_mem_ptr, pPlatformEntry,
                  pPMEMInfo);
         for (i = 0; i < m_out_buf_count; i++) {
            memset(bufHdr, 0, sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->nSize = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            // Set the values when we determine the right HxW param
            bufHdr->nAllocLen = bytes;   // get_output_buffer_size();
            bufHdr->nFilledLen = 0;
            bufHdr->pAppPrivate = appData;
            bufHdr->nOutputPortIndex =
                OMX_CORE_OUTPUT_PORT_INDEX;
            // Platform specific PMEM Information
            // Initialize the Platform Entry
            pPlatformEntry->type =
                OMX_QCOM_PLATFORM_PRIVATE_PMEM;
            pPlatformEntry->entry = pPMEMInfo;
            // Initialize the Platform List
            pPlatformList->nEntries = 1;
            pPlatformList->entryList = pPlatformEntry;

            // Assign the buffer space to the bufHdr
            bufHdr->pBuffer = buffer;
            // Keep this NULL till vdec_open is done
            bufHdr->pOutputPortPrivate = NULL;
            pPMEMInfo->offset = 0;
            bufHdr->pPlatformPrivate = pPlatformList;
            // Move the buffer and buffer header pointers
            bufHdr++;
            pPMEMInfo++;
            pPlatformEntry++;
            pPlatformList++;
         }
         *bufferHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         BITMASK_SET(m_out_bm_count, 0x0);
      } else {
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Output buf mem alloc failed[0x%x][0x%x][0x%x]\n",
                  m_out_mem_ptr, m_loc_use_buf_hdr, pPtr);
         eRet = OMX_ErrorInsufficientResources;
         return eRet;
      }
   } else {
      for (i = 0; i < m_out_buf_count; i++) {
         if (BITMASK_ABSENT(m_out_bm_count, i)) {
            break;
         }
      }
      if (i < m_out_buf_count) {
         // found an empty buffer at i
         *bufferHdr =
             ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr) + i;
         (*bufferHdr)->pAppPrivate = appData;
         (*bufferHdr)->pBuffer = buffer;
         BITMASK_SET(m_out_bm_count, i);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "All Output Buf Allocated:\n");
         eRet = OMX_ErrorInsufficientResources;
         return eRet;
      }
   }
   if (allocate_done()) {
      omx_vdec_display_in_buf_hdrs();
      omx_vdec_display_out_buf_hdrs();
      //omx_vdec_dup_use_buf_hdrs();
      //omx_vdec_display_out_use_buf_hdrs();

      // If use buffer and pmem alloc buffers
      // then dont make any local copies of use buf headers
      omx_vdec_set_use_buf_flg();
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
OMX_ERRORTYPE omx_vdec::use_buffer(OMX_IN OMX_HANDLETYPE hComp,
               OMX_INOUT OMX_BUFFERHEADERTYPE ** bufferHdr,
               OMX_IN OMX_U32 port,
               OMX_IN OMX_PTR appData,
               OMX_IN OMX_U32 bytes,
               OMX_IN OMX_U8 * buffer) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;

   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Use Buffer in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (port == OMX_CORE_INPUT_PORT_INDEX) {
      eRet =
          use_input_buffer(hComp, bufferHdr, port, appData, bytes,
                 buffer);
   } else if (port == OMX_CORE_OUTPUT_PORT_INDEX) {
      eRet =
          use_output_buffer(hComp, bufferHdr, port, appData, bytes,
                  buffer);
   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error: Invalid Port Index received %d\n",
               (int)port);
      eRet = OMX_ErrorBadPortIndex;
   }

   if (eRet == OMX_ErrorNone) {
      if (allocate_done()) {
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_IDLE_PENDING)) {
            // Send the callback now
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_IDLE_PENDING);
            post_event(OMX_CommandStateSet, OMX_StateIdle,
                  OMX_COMPONENT_GENERATE_EVENT);
         }
      }
      if (port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated) {
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING)) {
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_INPUT_ENABLE_PENDING);
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_INPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
         }

      } else if (port == OMX_CORE_OUTPUT_PORT_INDEX
            && m_out_bPopulated) {
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING)) {
             if (m_event_port_settings_sent) {
                  if (VDEC_SUCCESS != vdec_commit_memory(m_vdec)) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                                "ERROR!!! vdec_commit_memory failed\n");
                   m_bInvalidState = true;
                   m_cb.EventHandler(&m_cmp, m_app_data,
                          OMX_EventError,
                          OMX_ErrorInsufficientResources, 0,
                          NULL);
                   eRet = OMX_ErrorInsufficientResources;
                 }
             }
            // Populate the Buffer Headers
            omx_vdec_get_out_buf_hdrs();
            // Populate Use Buffer Headers
            if (omx_vdec_get_use_buf_flg()) {
               omx_vdec_dup_use_buf_hdrs();
               omx_vdec_get_out_use_buf_hdrs();
               omx_vdec_add_entries();
               omx_vdec_display_out_buf_hdrs();
            }

            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_OUTPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
            m_event_port_settings_sent = false;
         }
      }
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
OMX_ERRORTYPE omx_vdec::allocate_input_buffer(OMX_IN OMX_HANDLETYPE hComp,
                     OMX_INOUT OMX_BUFFERHEADERTYPE **
                     bufferHdr, OMX_IN OMX_U32 port,
                     OMX_IN OMX_PTR appData,
                     OMX_IN OMX_U32 bytes) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   unsigned i;      // Temporary counter

//  m_inp_buf_count = OMX_CORE_NUM_INPUT_BUFFERS;

   if (bytes <= OMX_CORE_INPUT_BUFFER_SIZE) {
      if (!m_inp_mem_ptr) {
         Vdec_BufferInfo vdec_buf;

         int nBufHdrSize =
             m_inp_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
         //m_inp_bm_count    = BITMASK_SIZE(m_inp_buf_count);
         m_inp_mem_ptr = (char *)calloc((nBufHdrSize), 1);

         if (m_inp_mem_ptr) {
            // We have valid Input memory block here
            QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "Allocating First Input Buffer(%d,%d,%d)\n",
                     m_inp_buf_count, nBufHdrSize,
                     m_inp_bm_count);
            if (m_bArbitraryBytes) {
               input[0] = bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_inp_mem_ptr;
            } else {
               input[0] = *bufferHdr = bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_inp_mem_ptr;
//        m_inp_bm_ptr  = ((char *)buf)    + nBufSize;
            }

            BITMASK_SET(m_inp_bm_count, 0);

            // Settting the entire storage nicely
            for (i = 0; i < m_inp_buf_count; i++, bufHdr++) {
               memset(bufHdr, 0,
                      sizeof(OMX_BUFFERHEADERTYPE));
               if (m_bArbitraryBytes) {
                  bufHdr->pBuffer = NULL;
                  bufHdr->nAllocLen =
                      m_inp_buf_size;
               } else {
                  if (VDEC_EFAILED ==
                      vdec_allocate_input_buffer
                      (m_inp_buf_size, &vdec_buf,
                       m_use_pmem)) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to allocate memory from vdec\n");
                     bufHdr->pBuffer = NULL;
                  } else {
                     if (m_use_pmem) {
                        if (i == 0) {
                           m_vdec_cfg.
                               inputBuffer
                               =
                               (struct
                                Vdec_BufferInfo
                                *)
                               malloc
                               (sizeof
                                (struct
                                 Vdec_BufferInfo)
                                *
                                m_inp_buf_count);
                           if (m_vdec_cfg.inputBuffer == NULL) {
                              QTV_MSG_PRIO
                                  (QTVDIAG_GENERAL,
                                   QTVDIAG_PRIO_ERROR,
                                   "ERROR - Failed to get input structures\n");
                              eRet = OMX_ErrorInsufficientResources;
                              break;
                           }
                           m_vdec_cfg.
                               numInputBuffers
                               = 0;
                        }

                        m_vdec_cfg.
                            inputBuffer
                            [i].base =
                            vdec_buf.
                            base;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            pmem_id =
                            vdec_buf.
                            pmem_id;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            bufferSize =
                            vdec_buf.
                            bufferSize;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            pmem_offset
                            =
                            vdec_buf.
                            pmem_offset;
                        m_vdec_cfg.
                            inputBuffer
                            [i].state =
                            VDEC_BUFFER_WITH_APP;
                        m_vdec_cfg.
                            numInputBuffers++;

                     }
                     bufHdr->pBuffer =
                         (OMX_U8 *) vdec_buf.
                         base;
                     bufHdr->nAllocLen =
                         vdec_buf.bufferSize;
                  }
               }

               bufHdr->nSize =
                   sizeof(OMX_BUFFERHEADERTYPE);
               bufHdr->nVersion.nVersion =
                   OMX_SPEC_VERSION;
               bufHdr->pAppPrivate = appData;
               bufHdr->nInputPortIndex =
                   OMX_CORE_INPUT_PORT_INDEX;
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Input Buffer %x bufHdr[%p]\n",
                        bufHdr->pBuffer, bufHdr);
               if (!m_bArbitraryBytes
                   && bufHdr->pBuffer == NULL) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to get input buffer\n");
                  eRet =
                      OMX_ErrorInsufficientResources;
                  break;
               }
            }
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Input buffer memory allocation failed\n");
            eRet = OMX_ErrorInsufficientResources;
         }

         if (m_bArbitraryBytes) {

            if (!m_arbitrary_bytes_input_mem_ptr) {
               m_arbitrary_bytes_input_mem_ptr =
                   (char *)calloc((nBufHdrSize), 1);
            }

            if (m_arbitrary_bytes_input_mem_ptr) {
               m_arbitrary_bytes_input[0] =
                   *bufferHdr = bufHdr =
                   (OMX_BUFFERHEADERTYPE *)
                   m_arbitrary_bytes_input_mem_ptr;
               for (i = 0; i < m_inp_buf_count;
                    i++, bufHdr++) {
                  memset(bufHdr, 0,
                         sizeof
                         (OMX_BUFFERHEADERTYPE));
                  if (VDEC_EFAILED ==
                      vdec_allocate_input_buffer
                      (m_inp_buf_size, &vdec_buf,
                       m_use_pmem)) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to allocate memory from vdec\n");
                     bufHdr->pBuffer = NULL;
                  } else {
                     if (m_use_pmem) {
                        if (i == 0) {
                           m_vdec_cfg.
                               inputBuffer
                               =
                               (struct
                                Vdec_BufferInfo
                                *)
                               malloc
                               (sizeof
                                (struct
                                 Vdec_BufferInfo)
                                *
                                (m_inp_buf_count
                                 +
                                 OMX_CORE_NUM_INPUT_BUFFERS));
                           if (m_vdec_cfg.inputBuffer == NULL) {
                              QTV_MSG_PRIO
                                  (QTVDIAG_GENERAL,
                                   QTVDIAG_PRIO_ERROR,
                                   "ERROR - Failed to get input structures\n");
                              eRet = OMX_ErrorInsufficientResources;
                              break;
                           }

                           m_vdec_cfg.
                               numInputBuffers
                               = 0;
                        }

                        m_vdec_cfg.
                            inputBuffer
                            [i].base =
                            vdec_buf.
                            base;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            pmem_id =
                            vdec_buf.
                            pmem_id;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            bufferSize =
                            vdec_buf.
                            bufferSize;
                        m_vdec_cfg.
                            inputBuffer
                            [i].
                            pmem_offset
                            =
                            vdec_buf.
                            pmem_offset;
                        m_vdec_cfg.
                            inputBuffer
                            [i].state =
                            VDEC_BUFFER_WITH_APP;
                        m_vdec_cfg.
                            numInputBuffers++;

                     }
                     bufHdr->pBuffer =
                         (OMX_U8 *) vdec_buf.
                         base;
                     bufHdr->nAllocLen =
                         vdec_buf.bufferSize;
                  }
                  bufHdr->nSize =
                      sizeof
                      (OMX_BUFFERHEADERTYPE);
                  bufHdr->nVersion.nVersion =
                      OMX_SPEC_VERSION;
                  bufHdr->pAppPrivate = appData;
                  bufHdr->nInputPortIndex =
                      OMX_CORE_INPUT_PORT_INDEX;
                  QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "arbitrarybytesInput Buffer %p arbitrarybytesInput[%p]\n",
                           bufHdr->pBuffer,
                           bufHdr);
                  if (bufHdr->pBuffer == NULL) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to get arbitrarybytesInput buffer\n");
                     eRet =
                         OMX_ErrorInsufficientResources;
                     break;
                  }
               }
            }
            for (int i = 0; i < OMX_CORE_NUM_INPUT_BUFFERS;
                 i++) {
               if (m_extra_buf_info[i].extra_pBuffer ==
                   NULL) {
                   QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                          "allocating extra buffers\n");
                  //m_extra_buf_info[i].extra_pBuffer = (OMX_U8*) malloc(m_inp_buf_size);
                  if (VDEC_EFAILED ==
                      vdec_allocate_input_buffer
                      (m_inp_buf_size, &vdec_buf,
                       m_use_pmem)) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to allocate memory from vdec\n");
                     bufHdr->pBuffer = NULL;
                  } else {
                     if (m_use_pmem) {
                        m_vdec_cfg.
                            inputBuffer
                            [m_inp_buf_count
                             + i].base =
                            vdec_buf.
                            base;
                        m_vdec_cfg.
                            inputBuffer
                            [m_inp_buf_count
                             +
                             i].
                            pmem_id =
                            vdec_buf.
                            pmem_id;
                        m_vdec_cfg.
                            inputBuffer
                            [m_inp_buf_count
                             +
                             i].
                            bufferSize =
                            vdec_buf.
                            bufferSize;
                        m_vdec_cfg.
                            inputBuffer
                            [m_inp_buf_count
                             +
                             i].
                            pmem_offset
                            =
                            vdec_buf.
                            pmem_offset;
                        m_vdec_cfg.
                            inputBuffer
                            [m_inp_buf_count
                             +
                             i].state =
                            VDEC_BUFFER_WITH_APP;
                        m_vdec_cfg.
                            numInputBuffers++;

                     }
                     m_extra_buf_info[i].
                         extra_pBuffer =
                         (OMX_U8 *) vdec_buf.
                         base;

                  }
                  if (m_extra_buf_info[i].
                      extra_pBuffer == NULL) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "ERROR - Failed to get extra_pBuffer\n");
                     eRet =
                         OMX_ErrorInsufficientResources;
                     break;
                  }
               }
               memset(m_extra_buf_info[i].
                      extra_pBuffer, 0,
                      vdec_buf.bufferSize);
               m_extra_buf_info[i].
                   bExtra_pBuffer_in_use = false;
               m_extra_buf_info[i].
                   arbitrarybytesInput = NULL;
            }
         }
      } else {
         for (i = 0; i < m_inp_buf_count; i++) {
            if (BITMASK_ABSENT(m_inp_bm_count, i)) {
               // bit space available
               break;
            }
         }
         if (i < m_inp_buf_count) {
            // found an empty buffer at i
            if (m_bArbitraryBytes) {
               m_arbitrary_bytes_input[i] =
                   *bufferHdr =
                   ((OMX_BUFFERHEADERTYPE *)
                    m_arbitrary_bytes_input_mem_ptr) +
                   i;
            } else {
               *bufferHdr =
                   ((OMX_BUFFERHEADERTYPE *)
                    m_inp_mem_ptr) + i;
            }
            input[i] =
                ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr) +
                i;
            (*bufferHdr)->pAppPrivate = appData;
            BITMASK_SET(m_inp_bm_count, i);
         } else {
            eRet = OMX_ErrorInsufficientResources;
         }

      }
   } else {
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
OMX_ERRORTYPE omx_vdec::allocate_output_buffer(OMX_IN OMX_HANDLETYPE hComp,
                      OMX_INOUT OMX_BUFFERHEADERTYPE **
                      bufferHdr, OMX_IN OMX_U32 port,
                      OMX_IN OMX_PTR appData,
                      OMX_IN OMX_U32 bytes) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   unsigned i;      // Temporary counter
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "Allocating Output Buffer(%d)\n", m_out_mem_ptr);
   if (!m_out_mem_ptr) {
      int nBufHdrSize = 0;
      int nPlatformEntrySize = 0;
      int nPlatformListSize = 0;
      int nPMEMInfoSize = 0;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *pPlatformList;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *pPlatformEntry;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Allocating First Output Buffer(%d)\n",
               m_out_buf_count);
      nBufHdrSize = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);

      nPMEMInfoSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO);
      nPlatformListSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_LIST);
      nPlatformEntrySize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_ENTRY);

      //m_out_bm_count     = BITMASK_SIZE(m_out_buf_count);

      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "TotalBufHdr %d BufHdrSize %d PMEM %d PL %d\n",
               nBufHdrSize, sizeof(OMX_BUFFERHEADERTYPE),
               nPMEMInfoSize, nPlatformListSize);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "PE %d bmSize %d \n", nPlatformEntrySize,
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
      //m_out_mem_ptr    = (char *)calloc(nBufHdrSize + nPlatformListSize + nPlatformEntrySize +
      //                         nPMEMInfoSize +m_out_bm_count, 1);
      // Alloc mem for output buf headers
      m_out_mem_ptr = (char *)calloc(nBufHdrSize, 1);
      // Alloc mem for platform specific info
      char *pPtr = NULL;
      pPtr = (char *)calloc(nPlatformListSize + nPlatformEntrySize +
                  nPMEMInfoSize, 1);
      if (m_out_mem_ptr && pPtr) {
         bufHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         m_platform_list =
             (OMX_QCOM_PLATFORM_PRIVATE_LIST *) (pPtr);
         m_platform_entry = (OMX_QCOM_PLATFORM_PRIVATE_ENTRY *)
             (((char *)m_platform_list) + nPlatformListSize);
         m_pmem_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
             (((char *)m_platform_entry) + nPlatformEntrySize);
         pPlatformList = m_platform_list;
         pPlatformEntry = m_platform_entry;
         pPMEMInfo = m_pmem_info;

         //m_out_bm_ptr   = (((char *) pPMEMInfo)     + nPMEMInfoSize);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Memory Allocation Succeeded for OUT port%p\n",
                  m_out_mem_ptr);

         // Settting the entire storage nicely
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "bHdr %p OutMem %p PE %p\n", bufHdr,
                  m_out_mem_ptr, pPlatformEntry);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  " Pmem Info = %p \n", pPMEMInfo);
         for (i = 0; i < m_out_buf_count; i++) {
            memset(bufHdr, 0, sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->nSize = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            // Set the values when we determine the right HxW param
            bufHdr->nAllocLen = 0;
            bufHdr->nFilledLen = 0;
            bufHdr->pAppPrivate = appData;
            bufHdr->nOutputPortIndex =
                OMX_CORE_OUTPUT_PORT_INDEX;
            // Platform specific PMEM Information
            // Initialize the Platform Entry
            //QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Initializing the Platform Entry for %d\n",i);
            pPlatformEntry->type =
                OMX_QCOM_PLATFORM_PRIVATE_PMEM;
            pPlatformEntry->entry = pPMEMInfo;
            // Initialize the Platform List
            pPlatformList->nEntries = 1;
            pPlatformList->entryList = pPlatformEntry;
            // Keep pBuffer NULL till vdec is opened
            bufHdr->pBuffer = (OMX_U8 *) 0xDEADBEEF;
            bufHdr->pOutputPortPrivate = NULL;
            pPMEMInfo->offset = 0;
            bufHdr->pPlatformPrivate = pPlatformList;
            // Move the buffer and buffer header pointers
            bufHdr++;
            pPMEMInfo++;
            pPlatformEntry++;
            pPlatformList++;
         }
         *bufferHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         //m_out_bm_ptr[0]=0x1;
         BITMASK_SET(m_out_bm_count, 0);
      } else {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Output buf mem alloc failed[0x%x][0x%x]\n",
                  m_out_mem_ptr, pPtr);
         eRet = OMX_ErrorInsufficientResources;
      }
   } else {
      for (i = 0; i < m_out_buf_count; i++) {
         if (BITMASK_ABSENT(m_out_bm_count, i)) {
            break;
         }
      }
      if (i < m_out_buf_count) {
         // found an empty buffer at i
         *bufferHdr =
             ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr) + i;
         (*bufferHdr)->pAppPrivate = appData;
         BITMASK_SET(m_out_bm_count, i);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "All the Output Buffers have been Allocated ; Returning Insufficient \n");
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
OMX_ERRORTYPE omx_vdec::allocate_buffer(OMX_IN OMX_HANDLETYPE hComp,
               OMX_INOUT OMX_BUFFERHEADERTYPE **
               bufferHdr, OMX_IN OMX_U32 port,
               OMX_IN OMX_PTR appData,
               OMX_IN OMX_U32 bytes) {

   OMX_ERRORTYPE eRet = OMX_ErrorNone;   // OMX return type

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "\n Allocate buffer on port %d \n", (int)port);
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Allocate Buf in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   // What if the client calls again.
   if (port == OMX_CORE_INPUT_PORT_INDEX) {
      eRet =
          allocate_input_buffer(hComp, bufferHdr, port, appData,
                 bytes);
   } else if (port == OMX_CORE_OUTPUT_PORT_INDEX) {
      eRet =
          allocate_output_buffer(hComp, bufferHdr, port, appData,
                  bytes);
   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error: Invalid Port Index received %d\n",
               (int)port);
      eRet = OMX_ErrorBadPortIndex;
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "Checking for Output Allocate buffer Done");
   if (eRet == OMX_ErrorNone) {
      if (allocate_done()) {

         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_IDLE_PENDING)) {

            // Send the callback now
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_IDLE_PENDING);
            post_event(OMX_CommandStateSet, OMX_StateIdle,
                  OMX_COMPONENT_GENERATE_EVENT);
         }
      }
      if (port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Input allocate done");
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING)) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Input enable pending");
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_INPUT_ENABLE_PENDING);
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_INPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
         }
      } else if (port == OMX_CORE_OUTPUT_PORT_INDEX
            && m_out_bPopulated) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "output allocate done");
          if (m_event_port_settings_sent) {
                  if (VDEC_SUCCESS != vdec_commit_memory(m_vdec)) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                                "ERROR!!! vdec_commit_memory failed\n");
                   m_bInvalidState = true;
                   m_cb.EventHandler(&m_cmp, m_app_data,
                          OMX_EventError,
                          OMX_ErrorInsufficientResources, 0,
                          NULL);
                   eRet = OMX_ErrorInsufficientResources;
                 }
            }
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING)) {

            // Populate the Buffer Headers
            if (m_vdec)
               omx_vdec_get_out_buf_hdrs();
            // Populate Use Buffer Headers
            if (omx_vdec_get_use_buf_flg()) {
               omx_vdec_dup_use_buf_hdrs();
               omx_vdec_get_out_use_buf_hdrs();
               omx_vdec_add_entries();
               omx_vdec_display_out_buf_hdrs();
            }
#ifdef _ANDROID_
            OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo =
                m_pmem_info;
            // create IMemoryHeap object
            m_heap_ptr =
                new
                VideoHeap(((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     pmem_id, m_vdec->arena[0].size,
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     base);
            for (unsigned i = 0; i < m_out_buf_count; i++) {
               pPMEMInfo->pmem_fd =
                   (OMX_U32) m_heap_ptr.get();
               pPMEMInfo++;
            }
            QTV_MSG_PRIO3(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_MED,
                     "VideoHeap : fd %d data %d size %d\n",
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     pmem_id,
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     base, m_vdec->arena[0].size);
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_MED,
                     "m_heap_ptr =%d",
                     (unsigned)m_heap_ptr.get());
#endif //_ANDROID_

            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "output enable pending");
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_OUTPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
            m_event_port_settings_sent = false;
         }
      }
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "Allocate Buffer exit with ret Code %d\n", eRet);
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
void omx_vdec::cancel_ftb_entry(OMX_BUFFERHEADERTYPE * buffer) {
   int i = 0;
   mutex_lock();
   for (i = 0; i < m_ftb_q.m_size; i++) {
      if (m_ftb_q.m_q[i].param2 == (unsigned)buffer) {
         m_ftb_q.m_q[i].canceled = true;
         break;
      }
   }
   mutex_unlock();
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
OMX_ERRORTYPE omx_vdec::free_buffer(OMX_IN OMX_HANDLETYPE hComp,
                OMX_IN OMX_U32 port,
                OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   unsigned int nPortIndex;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "free_buffer \n");

   if (m_state == OMX_StateIdle
       && (BITMASK_PRESENT(m_flags, OMX_COMPONENT_LOADING_PENDING))) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              " free buffer while Component in Loading pending\n");
   } else
       if ((m_inp_bEnabled == OMX_FALSE
       && port == OMX_CORE_INPUT_PORT_INDEX)
      || (m_out_bEnabled == OMX_FALSE
          && port == OMX_CORE_OUTPUT_PORT_INDEX)) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Free Buffer while port %d disabled\n", port);
   } else if (m_state == OMX_StateExecuting || m_state == OMX_StatePause) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Invalid state to free buffer,ports need to be disabled\n");
      post_event(OMX_EventError, OMX_ErrorPortUnpopulated,
            OMX_COMPONENT_GENERATE_EVENT);

      return eRet;
   } else if (m_state != OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Invalid state to free buffer,port lost Buffers\n");
      post_event(OMX_EventError, OMX_ErrorPortUnpopulated,
            OMX_COMPONENT_GENERATE_EVENT);
   }

   if (port == OMX_CORE_INPUT_PORT_INDEX) {
      // check if the buffer is valid
      if (m_bArbitraryBytes) {
         nPortIndex =
             buffer -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (m_extra_buf_info[nPortIndex].extra_pBuffer != NULL) {
            if (omx_vdec_get_input_use_buf_flg()) {
              free(m_extra_buf_info[nPortIndex].extra_pBuffer);
            }
            else {
            Vdec_BufferInfo buf_info;
            buf_info.base =
                m_extra_buf_info[nPortIndex].extra_pBuffer;
            if (m_use_pmem) {
               buf_info.bufferSize =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     nPortIndex].bufferSize;
               buf_info.pmem_id =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     nPortIndex].pmem_id;
               buf_info.pmem_offset =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     nPortIndex].pmem_offset;
            }
            //free(m_extra_buf_info[nPortIndex].extra_pBuffer);
            vdec_free_input_buffer(&buf_info, m_use_pmem);
            }
            m_extra_buf_info[nPortIndex].extra_pBuffer =
                NULL;
         }
      } else {
         nPortIndex =
             buffer - (OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr;
      }

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "free_buffer on i/p port - Port idx %d \n",
               nPortIndex);
      if (nPortIndex < m_inp_buf_count) {
         if(omx_vdec_get_input_use_buf_flg()) {
             QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "free_buffer on i/p port - use buffer so do not free pBuffer %x \n",
                  buffer->pBuffer);
         }
         else{
         Vdec_BufferInfo buf_info;
         buf_info.base = buffer->pBuffer;
         if (m_use_pmem) {
            buf_info.bufferSize =
                m_vdec_cfg.inputBuffer[m_inp_buf_count +
                        nPortIndex].
                bufferSize;
            buf_info.pmem_id =
                m_vdec_cfg.inputBuffer[m_inp_buf_count +
                        nPortIndex].pmem_id;
            buf_info.pmem_offset =
                m_vdec_cfg.inputBuffer[m_inp_buf_count +
                        nPortIndex].
                pmem_offset;
         }

         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "free_buffer on i/p port - pBuffer %x \n",
                  buffer->pBuffer);
         vdec_free_input_buffer(&buf_info, m_use_pmem);
         buffer->pBuffer = NULL;
         }

         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "free_buffer on i/p port - before Clear bitmask %x \n",
                  m_inp_bm_count);
         // Clear the bit associated with it.
         BITMASK_CLEAR(m_inp_bm_count, nPortIndex);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "free_buffer on i/p port - after Clear bitmask %x \n",
                  m_inp_bm_count);
         m_inp_bPopulated = OMX_FALSE;
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Error: free_buffer , \
                               Port Index calculation came out Invalid\n");
         eRet = OMX_ErrorBadPortIndex;
      }
      if (BITMASK_PRESENT
          ((m_flags), OMX_COMPONENT_INPUT_DISABLE_PENDING)
          && release_input_done()) {
         if (m_use_pmem) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Release input done freeing input buffer \n");
            if (m_vdec_cfg.inputBuffer) {
               free(m_vdec_cfg.inputBuffer);
               m_vdec_cfg.inputBuffer = NULL;
            }
         }
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "MOVING TO DISABLED STATE \n");
         BITMASK_CLEAR((m_flags),
                  OMX_COMPONENT_INPUT_DISABLE_PENDING);
         post_event(OMX_CommandPortDisable,
               OMX_CORE_INPUT_PORT_INDEX,
               OMX_COMPONENT_GENERATE_EVENT);
      }
      if (omx_vdec_get_input_use_buf_flg() && release_input_done()) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Resetting use_buf flag\n");
         omx_vdec_reset_input_use_buf_flg();
      }
   } else if (port == OMX_CORE_OUTPUT_PORT_INDEX) {
      // check if the buffer is valid
      nPortIndex = buffer - (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
      if (nPortIndex < m_out_buf_count) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "free_buffer on o/p port - Port idx %d \n",
                  nPortIndex);
         // Clear the bit associated with it.
         BITMASK_CLEAR(m_out_bm_count, nPortIndex);
         cancel_ftb_entry(buffer);
         m_out_bPopulated = OMX_FALSE;
         if (omx_vdec_get_use_buf_flg()) {
            OMX_BUFFERHEADERTYPE *temp;
            // Remove both the mappings.
            temp = m_use_buf_hdrs.find(buffer);
            if (buffer && temp) {
               m_use_buf_hdrs.erase(buffer);
               m_use_buf_hdrs.erase(temp);
            }
         }
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Error: free_buffer , \
                              Port Index calculation came out Invalid\n");
         eRet = OMX_ErrorBadPortIndex;
      }
      if (BITMASK_PRESENT
          ((m_flags), OMX_COMPONENT_OUTPUT_DISABLE_PENDING)
          && release_output_done()) {

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "FreeBuffer : If any Disable event pending,post it\n");

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "MOVING TO DISABLED STATE \n");
         BITMASK_CLEAR((m_flags),
                  OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
         if (m_vdec) {
            m_out_buf_count = m_vdec_cfg.numOutputBuffers;
            omx_vdec_free_output_port_memory();
         }
         if(omx_vdec_get_use_egl_buf_flg()) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Resetting use_egl_buf flag\n");
          omx_vdec_reset_use_elg_buf_flg();
         }
         post_event(OMX_CommandPortDisable,
               OMX_CORE_OUTPUT_PORT_INDEX,
               OMX_COMPONENT_GENERATE_EVENT);

         return eRet;
      }
      if (omx_vdec_get_use_buf_flg() && release_done()) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Resetting use_buf flag\n");
         omx_vdec_reset_use_buf_flg();
      }
   } else {
      eRet = OMX_ErrorBadPortIndex;
   }
   if ((eRet == OMX_ErrorNone) &&
       (BITMASK_PRESENT(m_flags, OMX_COMPONENT_LOADING_PENDING))) {
      if (release_done()) {
         // Send the callback now
         BITMASK_CLEAR((m_flags),
                  OMX_COMPONENT_LOADING_PENDING);
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
OMX_ERRORTYPE omx_vdec::empty_this_buffer(OMX_IN OMX_HANDLETYPE hComp,
                 OMX_IN OMX_BUFFERHEADERTYPE * buffer)
{
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
            "empty_this_buffer buffer %p, len %d, offset %d timestamp %ld\n",
            buffer->pBuffer, buffer->nFilledLen, buffer->nOffset,
            buffer->nTimeStamp);
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "ETB in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (!buffer) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Buffer Header NULL\n");
      return OMX_ErrorBadParameter;
   }
   if (m_bArbitraryBytes) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "empty_this_buffer - post event OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES\n");
      post_event((unsigned)hComp, (unsigned)buffer,
            OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES);
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "empty_this_buffer - post event OMX_COMPONENT_GENERATE_ETB\n");
      post_event((unsigned)hComp, (unsigned)buffer,
            OMX_COMPONENT_GENERATE_ETB);
      // eRet = empty_this_buffer_frame_based(hComp, buffer);
   }
   return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::empty_this_buffer_frame_based

DESCRIPTION
  This routine is used to push the encoded video frames to
  the video decoder.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE omx_vdec::
    empty_this_buffer_proxy_frame_based(OMX_IN OMX_HANDLETYPE hComp,
               OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "empty_this_buffer_frame_based - enter\n");
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "empty_this_buffer_frame_based buffer->nTimeStamp %d nFlags %d\n",
            buffer->nTimeStamp, buffer->nFlags);
   unsigned height = 0,val_height = 0;
   unsigned width = 0, val_width = 0;
   bool bInterlace = false;
   unsigned cropx = 0, cropy = 0, cropdx = 0, cropdy = 0;

   /*if(m_state == OMX_StateInvalid)
      {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,_ERROR("ETB in Invalid State\n");
      return OMX_ErrorInvalidState;
      } */

    if(m_bInvalidState == true)
    {
       buffer_done_cb_stub(&m_vdec_cfg, buffer);
       return OMX_ErrorNone;
    }

   OMX_ERRORTYPE ret = OMX_ErrorNone;
   OMX_ERRORTYPE ret1 = OMX_ErrorNone;
   OMX_U32 ret2;
   bool has_frame = true;
   unsigned nBufferIndex =
       buffer - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
   if (m_event_port_settings_sent == true) {
      if (m_bAccumulate_subframe == false) {
         add_entry(nBufferIndex);
         return OMX_ErrorNone;
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "empty_this_buffer - m_event_port_settings_sent == true\n");
         return add_entry_subframe_stitching(buffer);
      }
   }
   if (!m_vdec) {
      if (nBufferIndex > m_inp_buf_count) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "omx_vdec::etb--> Buffer Index Invalid\n");
         return OMX_ErrorBadPortIndex;
      }

      if ((strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26)
           == 0)
          && m_header_state != HEADER_STATE_RECEIVED_COMPLETE) {
         bool is_partial;

         has_frame = false;
         ret2 =
             m_h264_utils->check_header(buffer, m_nalu_bytes,
                         is_partial,
                         (OMX_U32)
                         m_header_state);

         if (ret2 == -1) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "omx_vdec::etb--> Wrong Data before recieving the header\n");
            buffer_done_cb_stub(&m_vdec_cfg, buffer);
            return OMX_ErrorFormatNotDetected;
         } else {
            if (is_partial) {
               if (m_header_state ==
                   HEADER_STATE_RECEIVED_NONE) {
                  m_vendor_config.pData =
                      (OMX_U8 *) malloc(buffer->
                              nFilledLen);
                  memcpy(m_vendor_config.pData,
                         buffer->pBuffer,
                         buffer->nFilledLen);
                  m_vendor_config.nDataSize =
                      buffer->nFilledLen;
                  m_header_state =
                      HEADER_STATE_RECEIVED_PARTIAL;
               } else {
                  if (m_vendor_config.pData) {
                     OMX_U8 *tempData;
                     tempData =
                         m_vendor_config.
                         pData;
                     m_vendor_config.pData =
                         (OMX_U8 *)
                         malloc(buffer->
                           nFilledLen +
                           m_vendor_config.
                           nDataSize);
                     memcpy(m_vendor_config.
                            pData, tempData,
                            m_vendor_config.
                            nDataSize);
                     memcpy(m_vendor_config.
                            pData +
                            m_vendor_config.
                            nDataSize,
                            buffer->pBuffer,
                            buffer->
                            nFilledLen);
                     m_vendor_config.
                         nDataSize +=
                         buffer->nFilledLen;
                     free(tempData);
                     m_header_state =
                         HEADER_STATE_RECEIVED_COMPLETE;
                  } else {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "omx_vdec::etb--> header_state partial but pData NULL\n");
                     return
                         OMX_ErrorFormatNotDetected;
                  }
               }
            } else {
               if (m_vendor_config.pData) {
                  free(m_vendor_config.pData);
               }
               m_vendor_config.pData =
                   (OMX_U8 *) malloc(buffer->
                           nFilledLen);
               memcpy(m_vendor_config.pData,
                      buffer->pBuffer,
                      buffer->nFilledLen);
               m_vendor_config.nDataSize =
                   buffer->nFilledLen;
               m_header_state =
                   HEADER_STATE_RECEIVED_COMPLETE;
            }
            if (m_header_state !=
                HEADER_STATE_RECEIVED_COMPLETE) {
               buffer_done_cb_stub(&m_vdec_cfg,
                         buffer);
               return OMX_ErrorNone;
            }
         }
      } else
          if ((strncmp
          (m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4",
           28) == 0))
      {
         has_frame = MP4_Utils::HasFrame(buffer);
      } else
          if ((strncmp
          (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
           26) == 0))
      {
         if(!m_bArbitraryBytes)
          has_frame = false;
      } else
           if ((strncmp
          (m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",
           27) == 0))
       {
          if (m_codec_format != QOMX_VIDEO_DIVXFormat311)
             has_frame = MP4_Utils::HasFrame(buffer);

       }

      ret =
          omx_vdec_check_port_settings(buffer, height, width,
                   bInterlace, cropx, cropy,
                   cropdx, cropdy);
      if (ret != OMX_ErrorNone) {
         buffer_done_cb_stub(&m_vdec_cfg, buffer);
         m_bInvalidState = true;
         m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
                 OMX_ErrorFormatNotDetected , 0, NULL);
         return ret;
      }
      m_bInterlaced = bInterlace;
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "ETB::after parsing-->Ht[%d] Wd[%d] m_ht[%d] m_wdth[%d]\n",
               height, width, m_height, m_width);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "ETB::after parsing-->cropdy[%d] Cropdx[%d] \n",
               cropdy, cropdx);
      m_crop_x = cropx;
      m_crop_y = cropy;
      m_crop_dx = cropdx;
      m_crop_dy = cropdy;
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "ETB::after parsing-->Height[%d] Width[%d] m_ht[%d] m_wdth[%d]\n",
               height, width, m_height, m_width);
      if (m_color_format == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka)
      {
        val_height =  (m_crop_dy + 15) & ~15;
        val_width = (m_crop_dx + 15) & ~15;
      }
      else
      {
       val_height = height;
       val_width = width;
      }

      if ((ret1 =
           omx_vdec_validate_port_param(val_height,
                    val_width)) == OMX_ErrorNone) {
         m_port_height = height;
         m_port_width = width;
         // Create native decoder to figure out the output buffer count and size.
         ret = omx_vdec_create_native_decoder(buffer);
         if (OMX_ErrorNone != ret) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Native decoder creation failed\n");
            m_bInvalidState = true;
            m_cb.EventHandler(&m_cmp, m_app_data,
                    OMX_EventError,
                    OMX_ErrorInsufficientResources, 0,
                    NULL);
            return ret;
         }

         if ((m_crop_dy == height == m_height)
             && (m_crop_dx == width == m_width)
             && (m_out_buf_count == m_vdec_cfg.numOutputBuffers)
             && (!m_bInterlaced)) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                    "Port setting Changed is not needed\n");
             if (VDEC_SUCCESS != vdec_commit_memory(m_vdec)) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                                "ERROR!!! vdec_commit_memory failed\n");
                   m_bInvalidState = true;
                   m_cb.EventHandler(&m_cmp, m_app_data,
                          OMX_EventError,
                          OMX_ErrorInsufficientResources, 0,
                          NULL);
                  return OMX_ErrorInsufficientResources;
             }

            // Populate Output Buffer Headers
            omx_vdec_get_out_buf_hdrs();
            // Populate Use Buffer Headers
            if (omx_vdec_get_use_buf_flg()) {
               omx_vdec_dup_use_buf_hdrs();
               omx_vdec_get_out_use_buf_hdrs();
               omx_vdec_display_out_buf_hdrs();
               omx_vdec_add_entries();
               omx_vdec_display_out_use_buf_hdrs();
            }
            OMX_BUFFERHEADERTYPE *tmp_buf_hdr =
                (OMX_BUFFERHEADERTYPE *)
                flush_before_vdec_op_q->Dequeue();
            while (tmp_buf_hdr) {
               vdec_release_frame(m_vdec,
                        (vdec_frame *)
                        tmp_buf_hdr->
                        pOutputPortPrivate);
               tmp_buf_hdr =
                   (OMX_BUFFERHEADERTYPE *)
                   flush_before_vdec_op_q->Dequeue();
            }
#ifdef _ANDROID_
            OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo =
                m_pmem_info;
            // create IMemoryHeap object
            m_heap_ptr =
                new
                VideoHeap(((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     pmem_id, m_vdec->arena[0].size,
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     base);
            for (unsigned i = 0;
                 i < m_vdec_cfg.numOutputBuffers; i++) {
               pPMEMInfo->pmem_fd =
                   (OMX_U32) m_heap_ptr.get();
               pPMEMInfo++;
            }
            QTV_MSG_PRIO3(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_MED,
                     "VideoHeap : fd %d data %d size %d\n",
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     pmem_id,
                     ((vdec_frame *) (&m_vdec_cfg.
                            outputBuffer
                            [0]))->buffer.
                     base, m_vdec->arena[0].size);
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_MED,
                     "m_heap_ptr =%d",
                     (unsigned)m_heap_ptr.get());
#endif //_ANDROID_
         } else {
            // Store the Ht and Width param so that the client can do a GetParam
            m_event_port_settings_sent = true;
            // Notify Apps about the Event [ PortSettingsChangedEvent ]
            if (m_cb.EventHandler) {
               m_cb.EventHandler(&m_cmp, m_app_data,
                       OMX_EventPortSettingsChanged,
                       OMX_CORE_OUTPUT_PORT_INDEX,
                       0, NULL);
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "Sending OMX EVENT PORT_SETTINGS_CHANGED EVENT \n");
            }
            if (has_frame) {
               if (m_bAccumulate_subframe == false
                   || (buffer->
                  nFlags & OMX_BUFFERFLAG_EOS)) {
                  add_entry(nBufferIndex);
                  return OMX_ErrorNone;
               } else {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_HIGH,
                          "After sending Port Setting Change Event\n");
                  return
                      add_entry_subframe_stitching
                      (buffer);
               }
            }
         }
         if (!has_frame) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                    "There is no frame to send - do buffer done and return\n");
            buffer_done_cb_stub(&m_vdec_cfg, buffer);
            return OMX_ErrorNone;
         }
      } else {
         m_bInvalidState = true;
         m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
                 ret1, 0, NULL);
         return ret1;
      }
   }

   return empty_this_buffer_proxy(hComp, buffer);
}

/* ======================================================================
FUNCTION
  omx_vdec::empty_this_buffer_proxy_arbitrary_bytes

DESCRIPTION
  This routine is used to push the arbritary bit stream to
  the video decoder.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE omx_vdec::
    empty_this_buffer_proxy_arbitrary_bytes(OMX_HANDLETYPE hComp,
                   OMX_BUFFERHEADERTYPE * buffer) {
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "omx_vdec::empty_this_buffer_proxy_arbitrary_bytes - m_current_arbitrary_bytes_input %p %p\n",
            m_current_arbitrary_bytes_input, m_vdec);
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "empty_this_buffer_proxy_arbitrary_bytes in Invalid State\n");
      m_current_arbitrary_bytes_input = NULL;
      return OMX_ErrorInvalidState;
   }
   if (m_bInvalidState) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "empty_this_buffer_proxy_arbitrary_bytes in Invalid State Flag true\n");
       buffer_done_cb_stub(&m_vdec_cfg, buffer);
       return OMX_ErrorNone;
   }

   if (!m_bPartialFrame) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Start to get complete new frame %x\n",
               m_current_frame);
      m_current_frame = get_free_input_buffer();
      if (m_current_frame == NULL) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "omx_vdec::empty_this_buffer_proxy_arbitrary_bytes, waiting for resource");
         m_bWaitForResource = true;
         return OMX_ErrorNone;
      }
      m_current_frame->pBuffer = buffer->pBuffer + buffer->nOffset;
      m_current_frame->nOffset = 0;
      m_current_frame->nFilledLen = 0;
      m_current_frame->nTimeStamp = -1;

   }

   if (!m_vdec) {
      if (m_vendor_config.pData) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Sending SPS+PPS as part of vdec_open\n");
      } else {
         if (strncmp
             (m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4",
              28) == 0) {
            m_arbitrary_bytes_info.start_code.m_start_code =
                VOP_START_CODE;
            m_arbitrary_bytes_info.start_code.
                m_start_code_mask = VOP_START_CODE_MASK;
            m_bStartCode = true;
         } else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",
             27) == 0) {
            m_arbitrary_bytes_info.start_code.m_start_code =
                VOP_START_CODE;
            m_arbitrary_bytes_info.start_code.
                m_start_code_mask = VOP_START_CODE_MASK;
            m_bStartCode = true;
         }else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",
             28) == 0) {
                 if (m_codec_format == QOMX_VIDEO_SparkFormat1) {
                     m_arbitrary_bytes_info.start_code.m_start_code =
                         SPARK1_START_CODE;
                 }
                 else {
                    m_arbitrary_bytes_info.start_code.m_start_code =
                        SHORT_HEADER_START_CODE;
                 }
                 m_arbitrary_bytes_info.start_code.
                     m_start_code_mask = SHORT_HEADER_MASK;
                 m_bStartCode = true;
         }else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",
             27) == 0) {
            m_arbitrary_bytes_info.start_code.m_start_code =
                SHORT_HEADER_START_CODE;
            m_arbitrary_bytes_info.start_code.
                m_start_code_mask = SHORT_HEADER_MASK;
            m_bStartCode = true;
         } else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
             26) == 0) {
            if (m_nalu_bytes == 0) {
               m_arbitrary_bytes_info.start_code.
                   m_start_code = H264_START_CODE;
               m_arbitrary_bytes_info.start_code.
                   m_start_code_mask =
                   H264_START_CODE_MASK;
               m_bStartCode = true;
            } else {
               m_bStartCode = false;
            }
         } else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
             26) == 0) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "VC1 clip \n");
            if ((((*((OMX_U32 *) m_current_frame->pBuffer))
                  & VC1_SP_MP_START_CODE_MASK) ==
                 VC1_SP_MP_START_CODE)
                ||
                (((*((OMX_U32 *) m_current_frame->pBuffer))
                  & VC1_SP_MP_START_CODE_MASK) ==
                 VC1_SP_MP_START_CODE_RCV_V1)
                ) {

               OMX_U32 *pBuf32;
               OMX_U8 *pBuf8;

               pBuf32 =
                   (OMX_U32 *) m_current_frame->
                   pBuffer;

               /* size of struct C or sequence header appears right after the number of frames information in the sequence header */
               m_vdec_cfg.sequenceHeaderLen =
                   *(++pBuf32);

               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "empty_this_buffer_proxy_arbitrary_bytes: sequence header len: %d \n",
                        m_vdec_cfg.
                        sequenceHeaderLen);

               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "VC1 clip - Simple Main Profile\n");

               if (m_vendor_config.pData) {
                  free(m_vendor_config.pData);
               }
               m_vendor_config.nPortIndex = 0;
               if (((*
                     ((OMX_U32 *) m_current_frame->
                      pBuffer)) &
                    VC1_SP_MP_START_CODE_MASK) ==
                   VC1_SP_MP_START_CODE) {
                  m_vendor_config.nDataSize =
                      OMX_VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC
                      +
                      m_vdec_cfg.
                      sequenceHeaderLen;
                  m_arbitrary_bytes_info.
                      frame_size.
                      m_timestamp_field_present =
                      1;
               } else
                   if (((*
                    ((OMX_U32 *) m_current_frame->
                     pBuffer)) &
                   VC1_SP_MP_START_CODE_MASK) ==
                  VC1_SP_MP_START_CODE_RCV_V1) {
                  m_vendor_config.nDataSize =
                      OMX_VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC
                      +
                      m_vdec_cfg.
                      sequenceHeaderLen;
                  /* set the time stamp field to be 0 so that the timestamp field is not parsed for RCV V1 format */
                  m_arbitrary_bytes_info.
                      frame_size.
                      m_timestamp_field_present =
                      0;
               }

               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "empty_this_buffer_proxy_arbitrary_bytes: m_vendor_config.nDataSize: %d \n",
                        m_vendor_config.
                        nDataSize);

               m_vendor_config.pData =
                   (OMX_U8 *) malloc(m_vendor_config.
                           nDataSize);
               memcpy(m_vendor_config.pData,
                      m_current_arbitrary_bytes_input->
                      pBuffer +
                      m_current_arbitrary_bytes_input->
                      nOffset,
                      m_vendor_config.nDataSize);

               m_current_arbitrary_bytes_input->
                   nFilledLen -=
                   m_vendor_config.nDataSize;
               memmove
                   (m_current_arbitrary_bytes_input->
                    pBuffer,
                    m_current_arbitrary_bytes_input->
                    pBuffer +
                    m_vendor_config.nDataSize,
                    m_current_arbitrary_bytes_input->
                    nFilledLen);
               m_bStartCode = false;
               m_bAccumulate_subframe = false;

            } else
                if (*((OMX_U32 *) m_current_frame->pBuffer)
               == 0x0F010000) {
               m_arbitrary_bytes_info.start_code.
                   m_start_code = VC1_AP_START_CODE;
               m_arbitrary_bytes_info.start_code.
                   m_start_code_mask =
                   VC1_AP_START_CODE_MASK;
               m_bStartCode = true;
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "empty_this_buffer_proxy_arbitrary_bytes - Warning: Possibility of Simple Main VC1 profile without sequence layer\n");
            }
         }
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Start code 0x%.8x mask 0x%.8x\n",
                  m_arbitrary_bytes_info.start_code.
                  m_start_code,
                  m_arbitrary_bytes_info.start_code.
                  m_start_code_mask);
      }
   }

   if (get_one_complete_frame(m_current_frame)) {
      empty_this_buffer_proxy_frame_based(hComp, m_current_frame);
   } else {
      int pend_idx = get_first_pending_index();
      if (pend_idx >= 0) {
         push_pending_buffers_proxy();
      }
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
OMX_ERRORTYPE omx_vdec::empty_this_buffer_proxy(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_IN OMX_BUFFERHEADERTYPE *
                  buffer) {
   int push_cnt = 0;
   OMX_ERRORTYPE ret = OMX_ErrorNone;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "EMPTY THIS BUFFER...%p\n", buffer);

   ++m_etb_cnt;
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "ETB: Count %u TS %lld\n", m_etb_cnt, buffer->nTimeStamp);

   if (m_bAccumulate_subframe) {
      ret = empty_this_buffer_proxy_subframe_stitching(buffer);
   } else {
      unsigned nPortIndex =
          buffer - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
      if (nPortIndex < m_inp_buf_count) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Original Empty this buffer\n");
         int pend_idx = get_first_pending_index();
         if (pend_idx >= 0 && (pend_idx != (int)nPortIndex)) {
            // Buffer-2 will hold the existing buffer in hand
            // We are trying to append the data to buffer2
            OMX_BUFFERHEADERTYPE *buffer2 = input[pend_idx];
            signed long long T1 = buffer->nTimeStamp;
            signed long long T2 = buffer2->nTimeStamp;
            {
               add_entry(nPortIndex);
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Setting the pending flag for buffer-%d (%x) \n",
                        nPortIndex + 1,
                        m_flags[0] );
               push_cnt = push_pending_buffers_proxy();
            }
         } else {
            push_cnt += push_one_input_buffer(buffer);
         }
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "empty_this_buffer_proxy pushed %d frames to the decoder\n",
                  push_cnt);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "FATAL ERROR: Why client is pushing the invalid buffer\n");
         ret = OMX_ErrorFormatNotDetected;
      }
   }
   return ret;
}

/* ======================================================================
FUNCTION
  omx_vdec::empty_this_buffer_subframe_stitching

DESCRIPTION
  This routine is used to push the encoded video frames to
  the video decoder considering the subframe stitching

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE omx_vdec::
    empty_this_buffer_proxy_subframe_stitching(OMX_BUFFERHEADERTYPE * buffer) {
#if DEBUG_ON
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "empty_this_buffer_proxy_subframe_stitching, length %d offset %d\n",
            buffer->nFilledLen, buffer->nOffset);
   for (OMX_U32 i = 0; i < 32; i++) {
      printf("0x%.2x ", buffer->pBuffer[buffer->nOffset + i]);
      if (i % 16 == 15) {
         printf("\n");
      }
   }
   printf("\n");

   if (m_pcurrent_frame) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "empty_this_buffer_proxy_subframe_stitching, current length %d offset %d\n",
               m_pcurrent_frame->nFilledLen,
               m_pcurrent_frame->nOffset);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", m_pcurrent_frame->pBuffer[i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      printf("\n");
   }
#endif
   int pend_idx = -1;
   int push_cnt = 0;
   unsigned nPortIndex = buffer - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
   OMX_ERRORTYPE ret = OMX_ErrorNone;
   bool isUpdatetimestamp = false;
   bool is_frame_no_error = true;

   if (nPortIndex < m_inp_buf_count) {
      OMX_BOOL isNewFrame = OMX_TRUE;
      if (buffer->nFilledLen > 0) {
         if (strncmp
             (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
              26) == 0) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_MED,
                    "empty_this_buffer_proxy_subframe_stitching- H264\n");
            is_frame_no_error =
                m_h264_utils->isNewFrame(buffer->pBuffer +
                          buffer->nOffset,
                          buffer->nFilledLen,
                          m_vdec_cfg.
                          size_of_nal_length_field,
                          isNewFrame,
                          isUpdatetimestamp);
            if(isUpdatetimestamp && (m_pcurrent_frame != NULL)) {
                m_pcurrent_frame->nTimeStamp = buffer->nTimeStamp;
            }
         } else
             if (strncmp
            (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
             26) == 0) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_MED,
                    "empty_this_buffer_proxy_subframe_stitching- VC1\n");
            is_frame_no_error =
                find_new_frame_ap_vc1(buffer->pBuffer +
                       buffer->nOffset,
                       buffer->nFilledLen,
                       isNewFrame);
         }
         if (false == is_frame_no_error) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "empty_this_buffer_proxy_subframe_stitching- Bit stream Error send Eventerro\n");

            /* bitsteam is corrupted beyond improvisation
             * so moving to invalid state.
             */

            m_state = OMX_StateInvalid;

            post_event(OMX_EventError,
                  OMX_ErrorStreamCorrupt,
                  OMX_COMPONENT_GENERATE_EVENT);

            post_event(OMX_EventError,
                  OMX_ErrorInvalidState,
                  OMX_COMPONENT_GENERATE_EVENT);

            return OMX_ErrorInvalidState;

         }
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "After Subframe stitching process %d\n",
               isNewFrame);

      if (OMX_TRUE == isNewFrame) {
         pend_idx = get_first_pending_index();

         nPortIndex =
             m_pcurrent_frame -
             ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Subframe stitching - NEW Frame %d %d\n",
                  pend_idx, nPortIndex);
         if (pend_idx >= 0 && (pend_idx != (int)nPortIndex)) {
            add_entry(nPortIndex);
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "Setting the pending flag for buffer-%d (%x) \n",
                     nPortIndex + 1, m_flags[0] );
            push_cnt = push_pending_buffers_proxy();
         } else if (m_pcurrent_frame) {
            push_cnt +=
                push_one_input_buffer(m_pcurrent_frame);
         }

         if (buffer->nFlags & OMX_BUFFERFLAG_EOS) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Subframe stitching - EOS\n");

            pend_idx = get_first_pending_index();
            nPortIndex =
                buffer -
                ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
            if (pend_idx >= 0
                && (pend_idx != (int)nPortIndex)) {
               add_entry(nPortIndex);
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Setting the pending flag for buffer-%d (%x) \n",
                        nPortIndex + 1,
                        m_flags[0] );
               push_cnt = push_pending_buffers_proxy();
            } else {
               push_cnt +=
                   push_one_input_buffer(buffer);
            }
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "empty_this_buffer_proxy_subframe_stitching pushed %d frames to the decoder\n",
                     push_cnt);
            m_pcurrent_frame = buffer;
            if (m_bArbitraryBytes) {
               m_pcurrent_frame->pBuffer +=
                   m_pcurrent_frame->nOffset;
               m_pcurrent_frame->nOffset = 0;
            }
         }
      } else {
         if (m_bArbitraryBytes) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                    "empty_this_buffer_proxy_subframe_stitching arbitrary bytes -  SUBFRAME_TYPE_PREVIOUS_FRAME\n");
            if (m_pcurrent_frame == NULL) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "DUDE - It's not a new frame but m_pcurrent_frame is NULL - It's a good case for first Subframe\n");
               m_pcurrent_frame = buffer;
            } else
                if (find_extra_buffer_index
               (m_pcurrent_frame->pBuffer) != -1) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_HIGH,
                        "Copy the new buffer to the current frame allocLen %d\n",
                        m_pcurrent_frame->
                        nAllocLen);
               if (m_pcurrent_frame->nAllocLen >=
                  (m_pcurrent_frame->nFilledLen +buffer->nFilledLen)){
                  memcpy(m_pcurrent_frame->pBuffer +
                         m_pcurrent_frame->nFilledLen,
                         buffer->pBuffer +
                         buffer->nOffset,
                         buffer->nFilledLen);
                  m_pcurrent_frame->nFilledLen +=
                      buffer->nFilledLen;
               }
               else{
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                          "Frame size too high [%d], Aborting session\n",(m_pcurrent_frame->nFilledLen +buffer->nFilledLen));
                  /* bitsteam is corrupted beyond improvisation
                   * so moving to invalid state.
                   */

                  m_state = OMX_StateInvalid;

                  post_event(OMX_EventError,
                        OMX_ErrorStreamCorrupt,
                        OMX_COMPONENT_GENERATE_EVENT);

                  post_event(OMX_EventError,
                        OMX_ErrorInvalidState,
                        OMX_COMPONENT_GENERATE_EVENT);

                  return OMX_ErrorInvalidState;

               }

            } else
                if (find_extra_buffer_index(buffer->pBuffer)
               != -1) {
               QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_HIGH,
                        "Buffer %p must be an extra buffer size of current len %d extra len %d extra offset %d\n",
                        buffer->pBuffer,
                        m_pcurrent_frame->
                        nFilledLen,
                        buffer->nFilledLen,
                        buffer->nOffset);
               memmove(buffer->pBuffer +
                  m_pcurrent_frame->nFilledLen,
                  buffer->pBuffer +
                  buffer->nOffset,
                  buffer->nFilledLen);
               memcpy(buffer->pBuffer,
                      m_pcurrent_frame->pBuffer,
                      m_pcurrent_frame->nFilledLen);

               /* We need to swap pBuffer pointer so buffer done cb will free the unused pbuffer */
               OMX_U8 *temp =
                   m_pcurrent_frame->pBuffer;
               m_pcurrent_frame->pBuffer =
                   buffer->pBuffer;
               buffer->pBuffer = temp;
               m_pcurrent_frame->nFilledLen +=
                   buffer->nFilledLen;
            } else
                if ((m_pcurrent_frame->pBuffer +
                m_pcurrent_frame->nFilledLen ==
                buffer->pBuffer + buffer->nOffset)
               && (m_pcurrent_frame->nFilledLen +
                   buffer->nFilledLen <=
                   m_pcurrent_frame->nAllocLen)) {
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_HIGH,
                        "No need memcpy, current length %d added by %d\n",
                        m_pcurrent_frame->
                        nFilledLen,
                        buffer->nFilledLen);
               m_pcurrent_frame->nFilledLen +=
                   buffer->nFilledLen;
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "Buffer is not enough. Need to get extra buffer \n");
               OMX_S8 index =
                   get_free_extra_buffer_index();
               if (index != -1) {
                  memcpy(m_extra_buf_info[index].
                         extra_pBuffer,
                         m_pcurrent_frame->
                         pBuffer,
                         m_pcurrent_frame->
                         nFilledLen);
                  memcpy(m_extra_buf_info[index].
                         extra_pBuffer +
                         m_pcurrent_frame->
                         nFilledLen,
                         buffer->pBuffer +
                         buffer->nOffset,
                         buffer->nFilledLen);
                  m_pcurrent_frame->pBuffer =
                      m_extra_buf_info[index].
                      extra_pBuffer;
                  m_pcurrent_frame->nFilledLen +=
                      buffer->nFilledLen;
               } else {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "Couldn't find extra buffer\n");
                  return OMX_ErrorHardware;
               }
            }
         } else   // non arbitrary bytes - previous frame
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Subframe stitching - Previous Frame\n");
            if (m_pcurrent_frame == NULL) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "It's not a new frame but m_pcurrent_frame is NULL - it's a good case for the first Subframe\n");
               m_pcurrent_frame = buffer;
            } else if (m_pcurrent_frame->nFilledLen +
                  buffer->nFilledLen <=
                  m_pcurrent_frame->nAllocLen) {
               // Stitching the current buffer into previous one
               memcpy(&m_pcurrent_frame->
                      pBuffer[m_pcurrent_frame->
                         nFilledLen],
                      buffer->pBuffer,
                      buffer->nFilledLen);
               m_pcurrent_frame->nFilledLen +=
                   buffer->nFilledLen;
            } else {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "Not enough memory - Stitching failed \n");
               ret = OMX_ErrorFormatNotDetected;
            }
         }

         m_pcurrent_frame->nFlags |= buffer->nFlags;

         if (m_pcurrent_frame->nFlags & OMX_BUFFERFLAG_EOS) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Subframe stitching partial frame - EOS\n");

            pend_idx = get_first_pending_index();
            nPortIndex =
                m_pcurrent_frame -
                ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
            if (pend_idx >= 0
                && (pend_idx != (int)nPortIndex)) {
               add_entry(nPortIndex);
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "Setting the pending flag for buffer-%d (%x) \n",
                        nPortIndex + 1,
                        m_flags[0] );
               push_cnt = push_pending_buffers_proxy();
            } else {
               push_cnt +=
                   push_one_input_buffer
                   (m_pcurrent_frame);
            }
         }
         buffer_done_cb_stub(&m_vdec_cfg, buffer);
      }
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "FATAL ERROR: Why client is pushing the invalid buffer\n");
      ret = OMX_ErrorFormatNotDetected;
   }
   return ret;
}

/* ======================================================================
FUNCTION
  omx_vdec::add_entry_subframe_stitching

DESCRIPTION
  add_entry scenarion when SUBFRAME stitching required

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything went successful.

========================================================================== */
OMX_ERRORTYPE omx_vdec::
    add_entry_subframe_stitching(OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
#if DEBUG_ON
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "add_entry_subframe_stitching, length %d\n",
            buffer->nFilledLen);
   for (OMX_U32 i = 0; i < 32; i++) {
      printf("0x%.2x ", buffer->pBuffer[buffer->nOffset + i]);
      if (i % 16 == 15) {
         printf("\n");
      }
   }
   printf("\n");

   if (m_pcurrent_frame) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "add_entry_subframe_stitching, current length %d offset %d\n",
               m_pcurrent_frame->nFilledLen,
               m_pcurrent_frame->nOffset);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", m_pcurrent_frame->pBuffer[i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      printf("\n");
   }
#endif

   OMX_ERRORTYPE ret = OMX_ErrorNone;
   unsigned nBufferIndex =
       m_pcurrent_frame - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
   OMX_BOOL isNewFrame = OMX_FALSE;
   bool isUpdatetimestamp = false;
   bool is_frame_no_error = true;

   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "add_entry_subframe_stitching- H264\n");
      is_frame_no_error =
          m_h264_utils->isNewFrame(buffer->pBuffer + buffer->nOffset,
                    buffer->nFilledLen,
                    m_vdec_cfg.
                    size_of_nal_length_field,
                    isNewFrame,
                    isUpdatetimestamp);
      if(isUpdatetimestamp && (m_pcurrent_frame != NULL)) {
           m_pcurrent_frame->nTimeStamp = buffer->nTimeStamp;
      }
   } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1", 26) ==
         0) {
      is_frame_no_error =
          find_new_frame_ap_vc1(buffer->pBuffer + buffer->nOffset,
                 buffer->nFilledLen, isNewFrame);
   }
   if (false == is_frame_no_error) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Subframe stitching - Bit stream Error send Eventerro\n");
      m_bInvalidState = true;
      m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
              OMX_ErrorStreamCorrupt, 0, NULL);
      return OMX_ErrorStreamCorrupt;
   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "add_entry_subframe_stitching\n");

   if (OMX_TRUE == isNewFrame) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "add_entry_subframe_stitching - NEW Frame\n");
      if (m_pcurrent_frame != NULL || buffer->nFlags & OMX_BUFFERFLAG_EOS)  {
         if(m_pcurrent_frame == NULL)
          {
            m_pcurrent_frame = buffer;
            nBufferIndex = m_pcurrent_frame - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
          }
          m_pcurrent_frame->nFlags |= buffer->nFlags;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "add_entry_subframe_stitching - add entry previous buffer\n");
         add_entry(nBufferIndex);
      }
      m_pcurrent_frame = buffer;
      if (m_bArbitraryBytes) {
         m_pcurrent_frame->pBuffer += m_pcurrent_frame->nOffset;
         m_pcurrent_frame->nOffset = 0;
      }
   } else if (m_bArbitraryBytes) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "add_entry_subframe_stitching arbitrary bytes -  SUBFRAME_TYPE_PREVIOUS_FRAME\n");
      if (m_pcurrent_frame == NULL) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "It's not a new frame but m_pcurrent_frame is NULL - It's a good case for first Subframe\n");
         m_pcurrent_frame = buffer;
      } else if (find_extra_buffer_index(m_pcurrent_frame->pBuffer) != -1) {   /*/
                                    (m_pcurrent_frame->nFilledLen + buffer->nFilledLen <= m_pcurrent_frame->nAllocLen)) */
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "Copy the new buffer to the current frame allocLen %d\n",
                  m_pcurrent_frame->nAllocLen);
         memcpy(m_pcurrent_frame->pBuffer +
                m_pcurrent_frame->nFilledLen,
                buffer->pBuffer + buffer->nOffset,
                buffer->nFilledLen);
         m_pcurrent_frame->nFilledLen += buffer->nFilledLen;
         m_pcurrent_frame->nTimeStamp = buffer->nTimeStamp;

         if(buffer->nFlags & OMX_BUFFERFLAG_EOS ) {
             m_pcurrent_frame->nFlags |= buffer->nFlags;
             add_entry(nBufferIndex);
         }

      } else if (find_extra_buffer_index(buffer->pBuffer) != -1) {
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "Buffer %p must be an extra buffer size of current len %d extra len %d extra offset %d\n",
                  buffer->pBuffer,
                  m_pcurrent_frame->nFilledLen,
                  buffer->nFilledLen, buffer->nOffset);
         memmove(buffer->pBuffer + m_pcurrent_frame->nFilledLen,
            buffer->pBuffer + buffer->nOffset,
            buffer->nFilledLen);
         memcpy(buffer->pBuffer, m_pcurrent_frame->pBuffer,
                m_pcurrent_frame->nFilledLen);

         /* We need to swap pBuffer pointer so buffer done cb will free the unused pbuffer */
         OMX_U8 *temp = m_pcurrent_frame->pBuffer;
         m_pcurrent_frame->pBuffer = buffer->pBuffer;
         buffer->pBuffer = temp;
         m_pcurrent_frame->nFilledLen += buffer->nFilledLen;
      } else
          if ((m_pcurrent_frame->pBuffer +
          m_pcurrent_frame->nFilledLen ==
          buffer->pBuffer + buffer->nOffset)
         && (m_pcurrent_frame->nFilledLen + buffer->nFilledLen <=
             m_pcurrent_frame->nAllocLen)) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "No need memcpy, current length %d added by %d\n",
                  m_pcurrent_frame->nFilledLen,
                  buffer->nFilledLen);
         m_pcurrent_frame->nFilledLen += buffer->nFilledLen;
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "Buffer is not enough. Need to get extra buffer \n");
         OMX_S8 index = get_free_extra_buffer_index();
         if (index != -1) {
            memcpy(m_extra_buf_info[index].extra_pBuffer,
                   m_pcurrent_frame->pBuffer,
                   m_pcurrent_frame->nFilledLen);
            memcpy(m_extra_buf_info[index].extra_pBuffer +
                   m_pcurrent_frame->nFilledLen,
                   buffer->pBuffer + buffer->nOffset,
                   buffer->nFilledLen);
            m_pcurrent_frame->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
            m_pcurrent_frame->nFilledLen +=
                buffer->nFilledLen;
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Couldn't find extra buffer\n");
            return OMX_ErrorHardware;
         }
      }

      buffer_done_cb_stub(&m_vdec_cfg, buffer);
   } else         // non arbitrary bytes previous frame
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "add_entry_subframe_stitching -  SUBFRAME_TYPE_PREVIOUS_FRAME\n");
      if (m_pcurrent_frame == NULL) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "It's not a new frame but m_pcurrent_frame is NULL - It's a good case for first Subframe\n");
         m_pcurrent_frame = buffer;
      } else if (m_pcurrent_frame->nFilledLen + buffer->nFilledLen <=
            m_pcurrent_frame->nAllocLen) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "Concat the current Subframe into previous one, size of prev %d current %d\n",
                  m_pcurrent_frame->nFilledLen,
                  buffer->nFilledLen);
         // Stitching the current buffer into previous one
         memcpy(&m_pcurrent_frame->
                pBuffer[m_pcurrent_frame->nFilledLen],
                buffer->pBuffer, buffer->nFilledLen);
         m_pcurrent_frame->nFilledLen += buffer->nFilledLen;
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "ERROR - Not enough memory - Stitching failed \n");
         ret = OMX_ErrorFormatNotDetected;
      }
      buffer_done_cb_stub(&m_vdec_cfg, buffer);
   }

   return ret;
}

/* ======================================================================
FUNCTION
  omx_vdec::PushPendingBuffers

DESCRIPTION
  Internal method used to push the pending buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
unsigned omx_vdec::push_pending_buffers(void) {
   post_event(0, 0, OMX_COMPONENT_PUSH_PENDING_BUFS);
   return 0;
}
/* ======================================================================
FUNCTION
  omx_vdec::PushPendingBufers

DESCRIPTION
  This routine is used to push the  pending buffers if decoder
  has space.

PARAMETERS
  None.

RETURN VALUE
  Returns the push count. either 0, 1 or 2.

========================================================================== */
    unsigned omx_vdec::push_pending_buffers_proxy(void) {
   unsigned push_cnt = 0;
   unsigned int ret = 0;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "omx_vdec::push_pending_buffer_proxy\n");

   if (m_event_port_settings_sent == true) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "push_pending_buffers_proxy - m_event_port_settings_sent == true\n");
         return 0;
      }

   while (is_pending()) {
      // If both buffers are pending try to push the first one
      int pend_idx = get_first_pending_index();
      ret = push_one_input_buffer(input[pend_idx]);
      if (ret == 0) {
         // If we are not able to push then skip the next step
         return push_cnt;
      }
      push_cnt += ret;
   }
   if (get_first_pending_index() >= (int)m_inp_buf_count) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "FATAL Error: pending index invalid\n");
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "push_pending_buffers pushed %d frames to the decoder\n",
            push_cnt);
   return push_cnt;
}

/* ======================================================================
FUNCTION
  omx_vdec::PushOneInputBuffer

DESCRIPTION
  This routine is used to push the encoded video frames to
  the video decoder.

PARAMETERS
  None.

RETURN VALUE
  True if it is able to the push the buffer to the decoders.

========================================================================== */
unsigned omx_vdec::push_one_input_buffer(OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
   unsigned push_cnt = 0;
   QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "push_one_input_buffer pBuffer %p, nOffset %d, nFilledLen %d\n",
            buffer->pBuffer, buffer->nOffset, buffer->nFilledLen);

   unsigned nPortIndex = buffer - ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
   if (nPortIndex < m_inp_buf_count) {
    if (m_b_divX_parser)
    {
      if (m_prev_timestamp < buffer->nTimeStamp)
      {
        if ((m_timestamp_interval > (buffer->nTimeStamp - m_prev_timestamp)) ||
            (m_timestamp_interval == 0))
        {
          m_timestamp_interval = buffer->nTimeStamp - m_prev_timestamp;
        }
        m_prev_timestamp = buffer->nTimeStamp;
      }

      if (m_divX_buffer_info.parsing_required)
      {
        m_divX_buffer_info.nFrames = m_mp4_utils->parse_frames_in_chunk(
                                                          buffer->pBuffer + buffer->nOffset,
                                                          buffer->nFilledLen,
                                                          m_timestamp_interval,
                                                          m_divX_buffer_info.frame_info);
        m_divX_buffer_info.parsing_required = false;
      }

      if ((buffer->nFlags & OMX_BUFFERFLAG_EOS) &&  (m_divX_buffer_info.nFrames == 0))
      {
        // Zero length EOS
        m_divX_buffer_info.nFrames = 1 ;
      }

      while (m_divX_buffer_info.last_decoded_index < m_divX_buffer_info.nFrames)
      {
        memset(&m_frame_info,0,sizeof(m_frame_info));
        m_frame_info.data      = buffer->pBuffer + buffer->nOffset +
                                 m_divX_buffer_info.frame_info[m_divX_buffer_info.last_decoded_index].offset;
        m_frame_info.len       = m_divX_buffer_info.frame_info[m_divX_buffer_info.last_decoded_index].size;
        m_frame_info.timestamp = buffer->nTimeStamp +
                                 m_divX_buffer_info.frame_info[m_divX_buffer_info.last_decoded_index].timestamp_increment;

        bool all_done = (m_divX_buffer_info.last_decoded_index == m_divX_buffer_info.nFrames - 1)?true:false;
        remove_top_entry();
        if ((buffer->nFlags & OMX_BUFFERFLAG_EOS) && all_done)
        {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"empty_this_buffer: EOS received with TS %d\n",(int)buffer->nTimeStamp);
          m_eos_timestamp = buffer->nTimeStamp;
          m_frame_info.flags = FRAME_FLAG_EOS;
        }
        void *cookie = ((all_done)?buffer:NULL);

         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"Post input buffer %d ts %d", m_ebd_cnt, m_frame_info.timestamp);
        int nRet = vdec_post_input_buffer(m_vdec, &(m_frame_info), cookie, m_use_pmem);
       QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"vdec_post_input_buffer returned %d\n",nRet);
        if(VDEC_EOUTOFBUFFERS == nRet)
        {
          push_back_entry(nPortIndex);
          break;
        }
        else if (all_done)
        {
           ++m_ebd_cnt;
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "\n ETB Count %u \n", m_ebd_cnt);
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "\n First pending buffer index is set to %d (%x)\n",
                     m_first_pending_buf_idx, m_flags[0] );
            push_cnt++;

          memset(&m_divX_buffer_info, 0, sizeof(m_divX_buffer_info));
          m_divX_buffer_info.parsing_required = true;
          break;
        }
        else
        {
          m_divX_buffer_info.last_decoded_index++;
        }
      }
    }
      else {
      memset(&m_frame_info, 0, sizeof(m_frame_info));
      if (buffer->nFlags & OMX_BUFFERFLAG_EOS) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "empty_this_buffer: EOS received with TS %d\n",
                  (int)buffer->nTimeStamp);
         m_eos_timestamp = buffer->nTimeStamp;
         m_frame_info.flags = FRAME_FLAG_EOS;
      }
      PrintFrameHdr(buffer);
      m_frame_info.data = buffer->pBuffer + buffer->nOffset;
      m_frame_info.len = buffer->nFilledLen;
      m_frame_info.timestamp = buffer->nTimeStamp;
      m_frame_info.flags = buffer->nFlags;
      remove_top_entry();
      int nRet =
          vdec_post_input_buffer(m_vdec, &(m_frame_info), buffer,
                  m_use_pmem);
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "vdec_post_input_buffer returned %d\n", nRet);
      if (VDEC_EOUTOFBUFFERS == nRet) {
         push_back_entry(nPortIndex);
      } else {
         ++m_ebd_cnt;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n ETB Count %u \n", m_ebd_cnt);
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n First pending buffer index is set to %d (%x)\n",
                  m_first_pending_buf_idx, m_flags[0] );
         push_cnt++;

      }
   }
   }
   return push_cnt;
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
OMX_ERRORTYPE omx_vdec::fill_this_buffer(OMX_IN OMX_HANDLETYPE hComp,
                OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
   if (m_state == OMX_StateInvalid) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "FTB in Invalid State\n");
      return OMX_ErrorInvalidState;
   }

   if (m_out_bEnabled == OMX_FALSE) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "FTB when port disabled \n");
      return OMX_ErrorIncorrectStateOperation;
   }
   post_event((unsigned)hComp, (unsigned)buffer,
         OMX_COMPONENT_GENERATE_FTB);
   return OMX_ErrorNone;
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
OMX_ERRORTYPE omx_vdec::fill_this_buffer_proxy(OMX_IN OMX_HANDLETYPE hComp,
                      OMX_IN OMX_BUFFERHEADERTYPE *
                      bufferAdd) {
   OMX_BUFFERHEADERTYPE *buffer = bufferAdd;
   // pOutMem points to the start of the array
   unsigned nPortIndex = buffer - ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr);

   if ((m_event_port_settings_sent == true) || !m_vdec
       || (m_out_bEnabled != OMX_TRUE)) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "OMX_VDEC::FTB --> Decoder Not initialised\n");
      flush_before_vdec_op_q->Enqueue((void *)buffer);
      BITMASK_SET((m_out_flags), nPortIndex);
      return OMX_ErrorNone;
   }

   m_ftb_cnt++;
   PrintFrameHdr(buffer);
   unsigned push_cnt = 0;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "FTB Outstanding frame cnt %d\n", m_outstanding_frames);

    if(m_bInvalidState == true)
    {
       m_cb.FillBufferDone(&m_cmp, m_app_data, buffer);
       return OMX_ErrorNone;
    }

   if (omx_vdec_get_use_buf_flg()) {
      // Get the PMEM buf
      OMX_BUFFERHEADERTYPE *tempHdr;
      tempHdr = m_use_buf_hdrs.find(buffer);
      if (tempHdr) {
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FTB::Found bufHdr[0x%x]0x%x-->0x%x\n",
                  buffer, buffer->pBuffer, tempHdr,
                  tempHdr->pBuffer);
         // Map the pBuf add to pMEM address.
         buffer = tempHdr;

      } else {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FTB::No match found  bufHdr[0x%x] \n",
                  buffer);
      }
   }
   if (nPortIndex < m_out_buf_count) {
      if (BITMASK_PRESENT((m_out_flags), nPortIndex)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FTB[%d] Ignored \n", nPortIndex);
         return OMX_ErrorNone;
      }

      if (true == m_bEoSNotifyPending) {
         unsigned int i = 0;
         OMX_BUFFERHEADERTYPE *pBufHdr =
             &(((OMX_BUFFERHEADERTYPE *)
                m_out_mem_ptr)[nPortIndex]);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FTB: EOS notify pending - Generate EOS using buffer[%d]\n",
                  nPortIndex);
         pBufHdr->nFlags = OMX_BUFFERFLAG_EOS;
         pBufHdr->nFilledLen = 0;
         pBufHdr->nTimeStamp = m_eos_timestamp;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "FBD Outstanding frame cnt %d\n",
                  m_outstanding_frames);
         m_bEoSNotifyPending = false;
         if (omx_vdec_get_use_buf_flg()) {
            // get the pMEM corresponding to the pBUF
            // copy the pMEM contents to pBUF
         }
         m_cb.FillBufferDone(&m_cmp, m_app_data, pBufHdr);
      } else {
         vdec_frame *frame =
             (vdec_frame *) buffer->pOutputPortPrivate;

         if (frame->flags) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "FTB Reset frame flags\n");
            frame->flags = 0;
         }

         if (buffer->nFlags) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "FTB Reset buffer hdr flags\n");
            buffer->nFlags = 0;
         }

         BITMASK_SET((m_out_flags), nPortIndex);
         // Release frame should be called only while executing
         // We stash the h64 frame inside the OutputPortPrivate field
         --m_outstanding_frames;
         vdec_release_frame(m_vdec,
                  (vdec_frame *) buffer->
                  pOutputPortPrivate);

        if (m_state == OMX_StateExecuting)
        {
           QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                   "FTB:: push_pending_buffer_proxy\n");
           push_cnt = push_pending_buffers_proxy();
        }
        QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "FTB Pushed %d input frames at the same time\n",
              push_cnt);

      }
   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "FATAL ERROR:Invalid Port Index[%d]\n",
               nPortIndex);
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
OMX_ERRORTYPE omx_vdec::set_callbacks(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_IN OMX_CALLBACKTYPE * callbacks,
                  OMX_IN OMX_PTR appData) {
   m_cb = *callbacks;
   m_app_data = appData;
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
OMX_ERRORTYPE omx_vdec::component_deinit(OMX_IN OMX_HANDLETYPE hComp) {

   OMX_BUFFERHEADERTYPE *bufferHdr = NULL;
   int i;
   if (OMX_StateLoaded != m_state) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "WARNING:Rxd DeInit,OMX not in LOADED state %d\n",
               m_state);
      for(i =0; i <m_inp_buf_count; i++ ) {
         if(m_bArbitraryBytes && m_arbitrary_bytes_input_mem_ptr) {
               bufferHdr =
                 ((OMX_BUFFERHEADERTYPE *)m_arbitrary_bytes_input_mem_ptr) + i;
          }
          else if (m_inp_mem_ptr) {
               bufferHdr =
                 ((OMX_BUFFERHEADERTYPE *)m_inp_mem_ptr) + i;
          }
          if(bufferHdr && bufferHdr->pBuffer && !omx_vdec_get_input_use_buf_flg()) {
             Vdec_BufferInfo buf_info;
             buf_info.base = bufferHdr->pBuffer;
             if (m_use_pmem) {
               buf_info.bufferSize =
                     m_vdec_cfg.inputBuffer[i].bufferSize;
               buf_info.pmem_id =
                     m_vdec_cfg.inputBuffer[i].pmem_id;
               buf_info.pmem_offset =
                      m_vdec_cfg.inputBuffer[i].pmem_offset;
             }

             QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "free_buffer on i/p port - pBuffer %x \n",
                 bufferHdr->pBuffer);
             vdec_free_input_buffer(&buf_info, m_use_pmem);
         }

      }
   }
   if (m_bArbitraryBytes) {
      for (i = 0; i < OMX_CORE_NUM_INPUT_BUFFERS; i++) {
         if (m_extra_buf_info[i].extra_pBuffer) {
            if(omx_vdec_get_input_use_buf_flg()) {
                free(m_extra_buf_info[i].extra_pBuffer);
            }
            else {
             Vdec_BufferInfo buf_info;
             buf_info.base =
                m_extra_buf_info[i].extra_pBuffer;
            if (m_use_pmem) {
               buf_info.bufferSize =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     i].bufferSize;
               buf_info.pmem_id =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     i].pmem_id;
               buf_info.pmem_offset =
                   m_vdec_cfg.
                   inputBuffer[m_inp_buf_count +
                     i].pmem_offset;
            }
            //free(m_extra_buf_info[nPortIndex].extra_pBuffer);
            vdec_free_input_buffer(&buf_info, m_use_pmem);
            }
            m_extra_buf_info[i].extra_pBuffer = NULL;
         }
       }
   }

   if (m_vdec) {
      vdec_close(m_vdec);
      m_vdec_cfg.vdec_fd = -1;
      m_vdec = NULL;
   }

   if (m_vdec_cfg.vdec_fd >= 0) {
      close(m_vdec_cfg.vdec_fd);
      m_vdec_cfg.vdec_fd = -1;
   }

   if (m_inp_mem_ptr) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the Input Memory\n");
      free(m_inp_mem_ptr);
      m_inp_mem_ptr = NULL;
   }
   if (m_arbitrary_bytes_input_mem_ptr) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the Input Memory\n");
      free(m_arbitrary_bytes_input_mem_ptr);
      m_arbitrary_bytes_input_mem_ptr = NULL;
   }
   if (m_loc_use_buf_hdr) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the UseBuffer Header Memory\n");
   }
   if (m_use_buf_hdrs.size()) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "WARNING::Cleanup Not correct\n");
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "WARNING::Num of ele still in the container=%d\n",
               m_use_buf_hdrs.size());
      m_use_buf_hdrs.show();
      m_use_buf_hdrs.eraseall();

   }
   if (m_h264_utils) {
      delete m_h264_utils;
      m_h264_utils = NULL;
   }
   if(m_mp4_utils)
   {
     delete m_mp4_utils;
     m_mp4_utils = NULL;
   }

   QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
            "Unread mesg FTB-Q[%d] CMD-Q[%d] ETB-Q[%d]\n",
            m_ftb_q.m_size, m_cmd_q.m_size,
            m_etb_arbitrarybytes_q.m_size);
   // Reset counters in mesg queues
   m_ftb_q.m_size = 0;
   m_cmd_q.m_size = 0;
   m_etb_arbitrarybytes_q.m_size = 0;
   m_ftb_q.m_read = m_ftb_q.m_write = 0;
   m_cmd_q.m_read = m_cmd_q.m_write = 0;
   m_etb_arbitrarybytes_q.m_read = m_etb_arbitrarybytes_q.m_write = 0;

   if (m_vendor_config.pData) {
      free(m_vendor_config.pData);
      m_vendor_config.pData = NULL;
   }
#ifdef _ANDROID_
   /* get strong count gets the refernce count of the pmem, the count will
    * be incremented by our kernal driver and surface flinger, by the time
    * we close the pmem, this cound needs to be zero, but there is no way
    * for us to know when surface flinger reduces its cound, so we wait
    * here in a infinite loop till the count is zero
    */
   if(m_heap_ptr != NULL) {
     while(1)
     {
       if ( ((m_heap_ptr.get() != NULL) && (m_heap_ptr.get())->getStrongCount()) == 1)
         break;
       else
         usleep(10);
     }
     // Clear the strong reference
     m_heap_ptr.clear();
   }
#endif // _ANDROID_
omx_vdec_free_output_port_memory();

   return OMX_ErrorNone;
}
/* ======================================================================
FUNCTION
  omx_vdec::use_egl_output_buffer

DESCRIPTION
  OMX Use EGL Image method implementation <TBD>.

PARAMETERS
  <TBD>.

RETURN VALUE
  Not Implemented error.

========================================================================== */

OMX_ERRORTYPE omx_vdec::use_egl_output_buffer(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_INOUT OMX_BUFFERHEADERTYPE **
                  bufferHdr, OMX_IN OMX_U32 port,
                  OMX_IN OMX_PTR appData,
                  OMX_IN void *eglImage)
{
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   unsigned i;      // Temporary counter
#ifdef USE_EGL_IMAGE_GPU
   PFNEGLQUERYIMAGEQUALCOMMPROC egl_queryfunc;
   EGLint fd = -1, offset = 0;
#else
   int fd = -1, offset = 0;
#endif
   vdec_frame *output_buf;
#ifdef USE_EGL_IMAGE_GPU
   if(m_display_id == NULL) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Display ID is not set by IL client and EGL image can't be used with out this \n");
        return OMX_ErrorInsufficientResources;

   }

   egl_queryfunc = (PFNEGLQUERYIMAGEQUALCOMMPROC)
     eglGetProcAddress("eglQueryImageKHR");

   egl_queryfunc(m_display_id, eglImage, EGL_BUFFER_HANDLE_QCOM,
            &fd);

    egl_queryfunc(m_display_id, eglImage, EGL_BUFFER_OFFSET_QCOM,
          &offset);
#else //with OMX test app
    struct temp_egl {
        int pmem_fd;
        int offset;
    };
    struct temp_egl *temp_egl_id;
    temp_egl_id = (struct temp_egl *)eglImage;
    fd = temp_egl_id->pmem_fd;
    offset = temp_egl_id->offset;
#endif

   if (fd < 0) {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Improper pmem fd by EGL clinet %d  \n",fd);
        return OMX_ErrorInsufficientResources;
   }
   if (!m_out_mem_ptr) {
      int nBufHdrSize = 0;
      int nPlatformEntrySize = 0;
      int nPlatformListSize = 0;
      int nPMEMInfoSize = 0;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *pPlatformList;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *pPlatformEntry;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo;

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Ist Use Output Buffer(%d)\n", m_out_buf_count);
      nBufHdrSize = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
      nPMEMInfoSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO);
      nPlatformListSize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_LIST);
      nPlatformEntrySize = m_out_buf_count *
          sizeof(OMX_QCOM_PLATFORM_PRIVATE_ENTRY);
      //m_out_bm_count     = BITMASK_SIZE(m_out_buf_count);
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "UOB::TotalBufHdr %d BufHdrSize %d PMEM %d PL %d\n",
               nBufHdrSize, sizeof(OMX_BUFFERHEADERTYPE),
               nPMEMInfoSize, nPlatformListSize);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "UOB::PE %d bmSize %d \n", nPlatformEntrySize,
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
      /*m_out_mem_ptr = (char *)calloc(nBufHdrSize +
         nPlatformListSize +
         nPlatformEntrySize +
         nPMEMInfoSize +m_out_bm_count, 1); */
      // Alloc mem for out buffer headers
      m_out_mem_ptr = (char *)calloc(nBufHdrSize, 1);
      // Alloc mem for platform specific info
      char *pPtr = NULL;
      pPtr = (char *)calloc(nPlatformListSize + nPlatformEntrySize +
                  nPMEMInfoSize, 1);
       m_vdec_cfg.outputBuffer =
          (struct vdec_frame *)malloc(sizeof(struct vdec_frame) *
                  m_out_buf_count);
      if (m_out_mem_ptr && pPtr &&  m_vdec_cfg.outputBuffer) {
         bufHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         m_platform_list =
             (OMX_QCOM_PLATFORM_PRIVATE_LIST *) pPtr;
         m_platform_entry = (OMX_QCOM_PLATFORM_PRIVATE_ENTRY *)
             (((char *)m_platform_list) + nPlatformListSize);
         m_pmem_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
             (((char *)m_platform_entry) + nPlatformEntrySize);
         pPlatformList = m_platform_list;
         pPlatformEntry = m_platform_entry;
         pPMEMInfo = m_pmem_info;
         //m_out_bm_ptr   = (((char *) pPMEMInfo)     + nPMEMInfoSize);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "UOB::Memory Allocation Succeeded for OUT port%p\n",
                  m_out_mem_ptr);

         // Settting the entire storage nicely
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "UOB::bHdr %p OutMem %p PE %p pmem[%p]\n",
                  bufHdr, m_out_mem_ptr, pPlatformEntry,
                  pPMEMInfo);
         for (i = 0; i < m_out_buf_count; i++) {
            memset(bufHdr, 0, sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->nSize = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            // Set the values when we determine the right HxW param
            bufHdr->nAllocLen = get_output_buffer_size();
            bufHdr->nFilledLen = 0;
            bufHdr->pAppPrivate = appData;
            bufHdr->nOutputPortIndex =
                OMX_CORE_OUTPUT_PORT_INDEX;
            // Platform specific PMEM Information
            // Initialize the Platform Entry
            pPlatformEntry->type =
                OMX_QCOM_PLATFORM_PRIVATE_PMEM;
            pPlatformEntry->entry = pPMEMInfo;
            // Initialize the Platform List
            pPlatformList->nEntries = 1;
            pPlatformList->entryList = pPlatformEntry;

            // Assign the buffer space to the bufHdr
            bufHdr->pBuffer = (OMX_U8*)eglImage;
            // Keep this NULL till vdec_open is done
            bufHdr->pOutputPortPrivate = NULL;
            pPMEMInfo->offset = 0;
            bufHdr->pPlatformPrivate = pPlatformList;
            // Move the buffer and buffer header pointers
            bufHdr++;
            pPMEMInfo++;
            pPlatformEntry++;
            pPlatformList++;
         }
         *bufferHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
         output_buf = (vdec_frame *) &m_vdec_cfg.outputBuffer[0];
         output_buf[0].buffer.pmem_id = fd;
         output_buf[0].buffer.pmem_offset = offset;
         output_buf[0].buffer.bufferSize = get_output_buffer_size();
         output_buf[0].buffer.base = (OMX_U8*)eglImage;
         output_buf[0].buffer.state = VDEC_BUFFER_WITH_APP_FLUSHED;
         BITMASK_SET(m_out_bm_count, 0x0);
      } else {
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Output buf mem alloc failed[0x%x][0x%x][0x%x]\n",
                  m_out_mem_ptr, m_loc_use_buf_hdr, pPtr);
         eRet = OMX_ErrorInsufficientResources;
         return eRet;
      }
   } else {
      for (i = 0; i < m_out_buf_count; i++) {
         if (BITMASK_ABSENT(m_out_bm_count, i)) {
            break;
         }
      }
      if (i < m_out_buf_count) {
         // found an empty buffer at i
         *bufferHdr =
             ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr) + i;
         (*bufferHdr)->pAppPrivate = appData;
         (*bufferHdr)->pBuffer = (OMX_U8*)eglImage;
         output_buf = (vdec_frame *) &m_vdec_cfg.outputBuffer[0] ;
         output_buf[i].buffer.pmem_id = fd;
         output_buf[i].buffer.pmem_offset = offset;
         output_buf[i].buffer.bufferSize = get_output_buffer_size();
         output_buf[i].buffer.base = (OMX_U8*)eglImage;
         output_buf[i].buffer.state = VDEC_BUFFER_WITH_APP_FLUSHED;
         BITMASK_SET(m_out_bm_count, i);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "All Output Buf Allocated:\n");
         eRet = OMX_ErrorInsufficientResources;
         return eRet;
      }
   }
   if (allocate_done()) {
      omx_vdec_display_in_buf_hdrs();
      omx_vdec_display_out_buf_hdrs();
      //omx_vdec_dup_use_buf_hdrs();
      //omx_vdec_display_out_use_buf_hdrs();

      // If use buffer and pmem alloc buffers
      // then dont make any local copies of use buf headers
      omx_vdec_set_use_egl_buf_flg();
   }
   return eRet;
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
OMX_ERRORTYPE omx_vdec::use_EGL_image(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_INOUT OMX_BUFFERHEADERTYPE **
                  bufferHdr, OMX_IN OMX_U32 port,
                  OMX_IN OMX_PTR appData,
                  OMX_IN void *eglImage) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "use_EGL_image: Begin  \n");

   if (m_state == OMX_StateInvalid) {
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Use Buffer in Invalid State\n");
      return OMX_ErrorInvalidState;
   }
   if (port == OMX_CORE_OUTPUT_PORT_INDEX) {
      eRet =
          use_egl_output_buffer(hComp, bufferHdr, port, appData, eglImage);
   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error: Invalid Port Index received %d\n",
               (int)port);
      eRet = OMX_ErrorBadPortIndex;
   }

   if (eRet == OMX_ErrorNone) {
      if (allocate_done()) {
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_IDLE_PENDING)) {
            // Send the callback now
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_IDLE_PENDING);
            post_event(OMX_CommandStateSet, OMX_StateIdle,
                  OMX_COMPONENT_GENERATE_EVENT);
         }
      }
      if (port == OMX_CORE_OUTPUT_PORT_INDEX
            && m_out_bPopulated) {
         if (BITMASK_PRESENT
             (m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING)) {
            if (m_event_port_settings_sent) {
                  if (VDEC_SUCCESS != vdec_commit_memory(m_vdec)) {
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                                "ERROR!!! vdec_commit_memory failed\n");
                   m_bInvalidState = true;
                   m_cb.EventHandler(&m_cmp, m_app_data,
                          OMX_EventError,
                          OMX_ErrorInsufficientResources, 0,
                          NULL);
                   eRet = OMX_ErrorInsufficientResources;
                 }
            }
            // Populate the Buffer Headers
            omx_vdec_get_out_buf_hdrs();
            BITMASK_CLEAR((m_flags),
                     OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
            post_event(OMX_CommandPortEnable,
                  OMX_CORE_OUTPUT_PORT_INDEX,
                  OMX_COMPONENT_GENERATE_EVENT);
            m_event_port_settings_sent = false;
         }
      }
   }
   return eRet;
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
OMX_ERRORTYPE omx_vdec::component_role_enum(OMX_IN OMX_HANDLETYPE hComp,
                   OMX_OUT OMX_U8 * role,
                   OMX_IN OMX_U32 index) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;

   if (!strncmp
       (m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4",
        OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.mpeg4",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.h263",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.divx",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.avc",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.vc1",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.vp",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
   } else
       if (!strncmp
      (m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",
       OMX_MAX_STRINGNAME_SIZE)) {
      if ((0 == index) && role) {
         strncpy((char *)role, "video_decoder.spark",
            OMX_MAX_STRINGNAME_SIZE);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "component_role_enum: role %s\n", role);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "\n No more roles \n");
         eRet = OMX_ErrorNoMore;
      }
    } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "\n Querying Role on Unknown Component\n");
      eRet = OMX_ErrorInvalidComponentName;
   }
   return eRet;
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
bool omx_vdec::allocate_input_done(void) {
   bool bRet = false;
   unsigned i = 0;

   if (m_inp_mem_ptr == NULL) {
      return bRet;
   }
   if (m_inp_mem_ptr) {
      for (; i < m_inp_buf_count; i++) {
         if (BITMASK_ABSENT(m_inp_bm_count, i)) {
            break;
         }
      }
   }
   if (i == m_inp_buf_count) {
      bRet = true;
   }
   if (i == m_inp_buf_count && m_inp_bEnabled) {
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
bool omx_vdec::allocate_output_done(void) {
   bool bRet = false;
   unsigned j = 0;

   if (m_out_mem_ptr == NULL) {
      return bRet;
   }

   if (m_out_mem_ptr) {
      for (; j < m_out_buf_count; j++) {
         if (BITMASK_ABSENT(m_out_bm_count, j)) {
            break;
         }
      }
   }

   if (j == m_out_buf_count) {
      bRet = true;
   }

   if (j == m_out_buf_count && m_out_bEnabled) {
      m_out_bPopulated = OMX_TRUE;
   }
   return bRet;
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
bool omx_vdec::allocate_done(void) {
   bool bRet = false;
   bool bRet_In = false;
   bool bRet_Out = false;

   bRet_In = allocate_input_done();
   bRet_Out = allocate_output_done();

   if (bRet_In && bRet_Out) {
      bRet = true;
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
bool omx_vdec::release_done(void) {
   bool bRet = false;

   if (release_input_done()) {
      if (release_output_done()) {
         bRet = true;
      }
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
bool omx_vdec::release_input_done(void) {
   bool bRet = false;
   unsigned i = 0, j = 0;

   if (m_inp_mem_ptr) {
      for (; j < m_inp_buf_count; j++) {
         if (BITMASK_PRESENT(m_inp_bm_count, j)) {
            break;
         }
      }
      if (j == m_inp_buf_count) {
         bRet = true;
      }
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error: Invalid Inp/OutMem pointers \n");
      bRet = true;
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
bool omx_vdec::release_output_done(void) {
   bool bRet = false;
   unsigned i = 0, j = 0;
   if (m_out_mem_ptr) {
      for (; j < m_out_buf_count; j++) {
         if (BITMASK_PRESENT(m_out_bm_count, j)) {
            break;
         }
      }
      if (j == m_out_buf_count) {
         bRet = true;
      }
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error: Invalid Inp/OutMem pointers \n");
      bRet = true;
   }
   return bRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_get_out_buf_hdrs

DESCRIPTION
  Get the PMEM area from video decoder

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
void omx_vdec::omx_vdec_get_out_buf_hdrs() {
   OMX_BUFFERHEADERTYPE *bufHdr;
   OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = m_pmem_info;

   bufHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
   vdec_frame *base_frame = (vdec_frame *) & m_vdec_cfg.outputBuffer[0];
   m_out_buf_count = m_vdec_cfg.numOutputBuffers;

   if (base_frame) {
      for (unsigned int i = 0; i < m_out_buf_count; i++) {
         bufHdr->nAllocLen = m_vdec_cfg.nOutBufAllocLen;   //get_output_buffer_size();
         //bufHdr->nFilledLen= get_output_buffer_size();
         bufHdr->nFilledLen = 0;
         bufHdr->pBuffer =
             (OMX_U8 *) (base_frame[i].buffer.base);
         bufHdr->pOutputPortPrivate = (void *)&base_frame[i];

         pPMEMInfo->offset = base_frame[i].buffer.pmem_offset;
         pPMEMInfo->pmem_fd = base_frame[i].buffer.pmem_id;
         QTV_MSG_PRIO5(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "Output Buffer param: Index [%d]: \
          fd 0x%x output 0x%x base 0x%x off 0x%x\n", i,
                  (unsigned)pPMEMInfo->pmem_fd, &base_frame[i], (unsigned)base_frame[i].buffer.base, (unsigned)pPMEMInfo->offset);
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "Output [%d]: buf %x \n", i,
                  (unsigned)bufHdr->pBuffer);
         bufHdr++;
         pPMEMInfo++;
      }
   }
   return;
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_get_out_use_buf_hdrs

DESCRIPTION
  Maintain a local copy of the output use buffers

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
void omx_vdec::omx_vdec_get_out_use_buf_hdrs() {
   OMX_BUFFERHEADERTYPE *bufHdr;
   int nBufHdrSize = 0;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "GET OUTPUT USE BUF\n");

   bufHdr = (OMX_BUFFERHEADERTYPE *) m_loc_use_buf_hdr;

   vdec_frame *base_frame = (vdec_frame *) & m_vdec_cfg.outputBuffer[0];
   nBufHdrSize = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
   if (base_frame) {
      for (unsigned int i = 0; i < m_out_buf_count; i++) {
         bufHdr->nAllocLen = get_output_buffer_size();
         //bufHdr->nFilledLen= get_output_buffer_size();
         bufHdr->nFilledLen = 0;

         bufHdr->pBuffer =
             (OMX_U8 *) (base_frame[i].buffer.base);
         bufHdr->pOutputPortPrivate = (void *)&base_frame[i];
         // just the offset instead of physical address
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "OutputBuffer[%d]: buf[0x%x]: pmem[0x%x] \n",
                  i, (unsigned)bufHdr->pBuffer,
                  (OMX_U8 *) (base_frame[i].buffer.base));
         bufHdr++;
      }
   }
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_check_port_settings

DESCRIPTION
  Parser to check the HxW param

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
OMX_ERRORTYPE omx_vdec::omx_vdec_check_port_settings(OMX_BUFFERHEADERTYPE *
                       buffer, unsigned &height,
                       unsigned &width,
                       bool & bInterlace,
                       unsigned &cropx,
                       unsigned &cropy,
                       unsigned &cropdx,
                       unsigned &cropdy) {
   OMX_ERRORTYPE ret = OMX_ErrorNone;
   OMX_U8 *buf;
   OMX_U32 buf_len;
   OMX_U32 mult_fact = 16;

   if (m_vendor_config.pData) {
      buf = m_vendor_config.pData;
      buf_len = m_vendor_config.nDataSize;
   } else {
      buf = buffer->pBuffer;
      buf_len = buffer->nFilledLen;
   }

   if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc")) {
      if (false ==
          m_h264_utils->parseHeader(buf, buf_len, m_nalu_bytes,
                     height, width, bInterlace, cropx,
                     cropy, cropdx, cropdy)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Parsing Error unsupported profile or level\n");
         return OMX_ErrorStreamCorrupt;
      }
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "Parsing Done height[%d] width[%d]\n", height,
               width);
   } else if ((!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4"))
         || (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263"))
         ||  (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx") &&
               m_codec_format != QOMX_VIDEO_DIVXFormat311) ) {
      mp4StreamType dataStream;
      dataStream.data = (unsigned char *)buf;
      dataStream.numBytes = (unsigned long int)buf_len;
      if (false == m_mp4_utils->parseHeader(&dataStream)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "VOL header parsing failure, aborting playback\n");
         return OMX_ErrorStreamCorrupt;
      }
      cropx = cropy = 0;
      cropdy = height = m_mp4_utils->SrcHeight();
      cropdx = width = m_mp4_utils->SrcWidth();
      bInterlace = false;
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "MPEG4/H263 ht[%d] wdth[%d]\n", height, width);
   } else if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark")) {
       mp4StreamType dataStream;
       dataStream.data = (unsigned char *)buf;
       dataStream.numBytes = (unsigned long int)buf_len;
        QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Parsing Spark bit stream\n");
       if (false == m_mp4_utils->parseSparkHeader(&dataStream)) {
          QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Parsing Error unsupported profile or level\n");
          return OMX_ErrorStreamCorrupt;
       }
       cropx = cropy = 0;
       cropdy = height = m_mp4_utils->SrcHeight();
       cropdx = width = m_mp4_utils->SrcWidth();
       bInterlace = false;
       QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                "SPARK ht[%d] wdth[%d]\n", height, width);
   } else if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx") &&
              m_codec_format == QOMX_VIDEO_DIVXFormat311) {
      bInterlace = false;
      cropx = cropy = 0;
      cropdy = height = m_crop_dy;
      cropdx = width = m_crop_dx;

   } else if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp")) {
     // TODO FOR VP6
      OMX_U8 *pBuf;
     if (buf_len <= 8)
        return OMX_ErrorStreamCorrupt;

     pBuf = buf;
     /* Skip the first 4 bytes to start reading the Height and Width*/
     pBuf += 4;

     cropx = cropy = 0;
     cropdy = height = ( ((OMX_U32) (*pBuf++)) * 16);
     cropdx = width = ( ((OMX_U32) (*pBuf)) * 16);
     bInterlace = false;
   } else if (!strcmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1")) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "omx_vdec_check_port_settings - start code in seq header %x\n", ((*((OMX_U32 *) buf))));   // & VC1_SP_MP_START_CODE_MASK));
      if ((((*((OMX_U32 *) buf)) & VC1_SP_MP_START_CODE_MASK) ==
           VC1_SP_MP_START_CODE)
          || (((*((OMX_U32 *) buf)) & VC1_SP_MP_START_CODE_MASK) ==
         VC1_SP_MP_START_CODE_RCV_V1)
          ) {
         OMX_U32 *pBuf32, num_frames = 0;
         OMX_U8 *pBuf8;

         pBuf32 = (OMX_U32 *) buf;

         /* get the number of frames from the sequence header */
         num_frames = *pBuf32 & 0x00FFFFFF;

         /* size of struct C appears right after the number of frames information in the sequence header */
         m_vdec_cfg.sequenceHeaderLen = *(++pBuf32);

         /* advance the pointer by the size of struct C in order to get the height and width */
         pBuf8 = (OMX_U8 *) (++pBuf32);
         pBuf8 += m_vdec_cfg.sequenceHeaderLen;

         /* read the height and width information which are each 4 bytes */
         pBuf32 = (OMX_U32 *) pBuf8;
         height = *pBuf32;
         width = *(++pBuf32);
         bInterlace = false;
         m_bAccumulate_subframe = false;

         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "omx_vdec_check_port_settings - VC1 Simple/Main profile, %d x %d %x x %x\n",
                  width, height, width, height);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "omx_vdec_check_port_settings - VC1 sequence header length %d\n",
                  m_vdec_cfg.sequenceHeaderLen);

         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "omx_vdec_check_port_settings - num_frames %d\n",
                  num_frames);
      } else if (*((OMX_U32 *) buf) == 0x0F010000) {
         OMX_U16 temp_dimension =
             ((OMX_U16) (buf[6]) << 4) | ((OMX_U16) (buf[7]) >>
                      4);
         width = 2 * (temp_dimension + 1);

         temp_dimension =
             ((OMX_U16) (buf[7] & 0x0F) << 8) |
             (OMX_U16) (buf[8]);
         height = 2 * (temp_dimension + 1);

         bInterlace = ((buf[9] & 0x40) ? true : false);
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "omx_vdec_check_port_settings - VC1 Advance profile Width:%d x Height:%d\n",
                  width, height);
      } else if(m_vendor_config.nDataSize == VC1_STRUCT_C_LEN) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_LOW,
                      "QC_DEBUG :: omx_vdec_check_port_settings - VC1 height and width, %d x %d\n",
                      width, height);
          height = m_height;
          width = m_width;
      }
      else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "omx_vdec_check_port_settings - ERROR: Unknown VC1 profile. Couldn't find height and width\n");
         return OMX_ErrorStreamCorrupt;
      }
      cropdy = height;
      cropdx = width;
      cropx = cropy = 0;
    }
    if( m_color_format == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka)
       mult_fact = 32;

    if ((height % mult_fact) != 0) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Height %d is not a multiple of %d",
                  height, mult_fact);
         height = (height / mult_fact + 1) * mult_fact;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Height adjusted to %d \n", height);
      }
      if ((width % mult_fact) != 0) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Width %d is not a multiple of %d",
                  width, mult_fact);
         width = (width / mult_fact + 1) * mult_fact;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Width adjusted to %d \n", width);
      }

   return OMX_ErrorNone;
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
OMX_ERRORTYPE omx_vdec::omx_vdec_validate_port_param(int height, int width) {
   OMX_ERRORTYPE ret = OMX_ErrorNone;
   long hxw = height * width;

   if (hxw > (OMX_CORE_720P_HEIGHT * OMX_CORE_720P_WIDTH)) {
      ret = OMX_ErrorNotImplemented;
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "-----Invalid Ht[%d] wdth[%d]----", height, width);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "--Max supported is 720 x 1280---", height, width);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "-------Aborting session---------", height, width);
   }
   return ret;
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_add_entries

DESCRIPTION
  Add the buf header entries to the container

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
void omx_vdec::omx_vdec_add_entries() {
   OMX_BUFFERHEADERTYPE *pOmxHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
   OMX_BUFFERHEADERTYPE *pOmxOutHdr = m_loc_use_buf_hdr;
   for (unsigned int i = 0; i < 8; i++, pOmxHdr++, pOmxOutHdr++) {
      m_use_buf_hdrs.insert(pOmxOutHdr, pOmxHdr);
      m_use_buf_hdrs.insert(pOmxHdr, pOmxOutHdr);
   }

}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_dup_use_buf_hdrs

DESCRIPTION
  Populate the copy buffer [ use buffer ]

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
OMX_ERRORTYPE omx_vdec::omx_vdec_dup_use_buf_hdrs() {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_BUFFERHEADERTYPE *bufHdr;   // buffer header
   OMX_BUFFERHEADERTYPE *pHdr = (OMX_BUFFERHEADERTYPE *) m_out_mem_ptr;
   int nBufHdrSize = 0;

   nBufHdrSize = m_out_buf_count * sizeof(OMX_BUFFERHEADERTYPE);
   memcpy(m_loc_use_buf_hdr, pHdr, nBufHdrSize);
   omx_vdec_display_out_use_buf_hdrs();

   return OMX_ErrorNone;
}

void omx_vdec::omx_vdec_cpy_user_buf(OMX_BUFFERHEADERTYPE * pBufHdr) {
   OMX_BUFFERHEADERTYPE *bufHdr;
   bufHdr = m_use_buf_hdrs.find(pBufHdr);
   if (bufHdr) {
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "CPY::Found bufHdr[0x%x]0x%x-->[0x%x]0x%x\n",
               pBufHdr->pBuffer, pBufHdr, bufHdr,
               bufHdr->pBuffer);
      // Map the local buff address to the user space address.
      // Basically pMEM -->pBuf translation

      // DO A MEMCPY from pMEM are to the user defined add
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "CPY::[bufferHdr]pBuffer maps[0x%x]0x%x-->[0x%x]0x%x\n",
               pBufHdr->pBuffer, pBufHdr, bufHdr,
               bufHdr->pBuffer);
      // first buffer points to user defined add, sec one to PMEM area
      memcpy(pBufHdr->pBuffer, bufHdr->pBuffer,
             get_output_buffer_size());
   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "CPY::No match found  bufHdr[0x%x] \n", pBufHdr);
      omx_vdec_display_out_use_buf_hdrs();
   }
}

void omx_vdec::omx_vdec_display_in_buf_hdrs() {
   OMX_BUFFERHEADERTYPE *omxhdr = ((OMX_BUFFERHEADERTYPE *) m_inp_mem_ptr);
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^INPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
   for (unsigned int i = 0; i < 2; i++) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "hdr[0x%x] buffer[0x%x]\n", omxhdr + i,
               (omxhdr + i)->pBuffer);
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^INPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
}

void omx_vdec::omx_vdec_display_out_buf_hdrs() {
   OMX_BUFFERHEADERTYPE *omxhdr = ((OMX_BUFFERHEADERTYPE *) m_out_mem_ptr);
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^OUTPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
   for (unsigned int i = 0; i < 8; i++) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "hdr[0x%x] buffer[0x%x]\n", omxhdr + i,
               (omxhdr + i)->pBuffer);
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^^OUTPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");

}

void omx_vdec::omx_vdec_display_out_use_buf_hdrs() {
   OMX_BUFFERHEADERTYPE *omxhdr = m_loc_use_buf_hdr;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^^USE OUTPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
   for (unsigned int i = 0; i < 8; i++) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "hdr[0x%x] buffer[0x%x]\n", omxhdr + i,
               (omxhdr + i)->pBuffer);
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^^^^USE OUTPUT BUF HDRS^^^^^^^^^^^^^^^^^^^^^^^^^\n");
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^^^USE BUF HDRS MAPPING^^^^^^^^^^^^^^^^^^^^^^^^^\n");
   m_use_buf_hdrs.show();
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_create_native_decoder

DESCRIPTION
  Native decoder creation

PARAMETERS
  None.

RETURN VALUE
  OMX_ErrorNone if successful
========================================================================== */
OMX_ERRORTYPE omx_vdec::
    omx_vdec_create_native_decoder(OMX_IN OMX_BUFFERHEADERTYPE * buffer) {
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   OMX_U32 codec_type;
   // if NALU is zero assume START CODE
   m_vdec_cfg.height = m_port_height;
   m_vdec_cfg.width = m_port_width;
   m_vdec_cfg.size_of_nal_length_field = m_nalu_bytes;
   m_vdec_cfg.vc1Rowbase = 0;
   m_vdec_cfg.color_format = m_color_format;

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "m_vdec_cfg.kind %s\n",
            m_vdec_cfg.kind);

   if (!m_vendor_config.pData) {
      if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4", 28)
          == 0 ) {
         m_vdec_cfg.sequenceHeader =
             (byte *) malloc(buffer->nFilledLen);
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Mpeg4 clip \n");
         m_vdec_cfg.sequenceHeaderLen = buffer->nFilledLen;
         memcpy(m_vdec_cfg.sequenceHeader, buffer->pBuffer,
                m_vdec_cfg.sequenceHeaderLen);
      } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.divx", 27)
          == 0 ) {
         if(m_codec_format == QOMX_VIDEO_DIVXFormat311) {
            m_vdec_cfg.sequenceHeader = NULL;
            m_vdec_cfg.sequenceHeaderLen = 0;
            m_vdec_cfg.fourcc = MAKEFOURCC('D', 'I', 'V', '3');
         }else {
            m_vdec_cfg.sequenceHeader =
                (byte *) malloc(buffer->nFilledLen);
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "Divx clip \n");
            m_vdec_cfg.sequenceHeaderLen = buffer->nFilledLen;
            memcpy(m_vdec_cfg.sequenceHeader, buffer->pBuffer,
                   m_vdec_cfg.sequenceHeaderLen);
             m_vdec_cfg.fourcc = MAKEFOURCC('D', 'I', 'V', 'X');
         }
      }else
          if (strncmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc",
          26) == 0) {
         m_vdec_cfg.sequenceHeader =
             (byte *) malloc(buffer->nFilledLen);
         m_vdec_cfg.sequenceHeaderLen =
             m_h264_utils->parse_first_h264_input_buffer(buffer,
                           m_vdec_cfg.
                           size_of_nal_length_field);
         memcpy(m_vdec_cfg.sequenceHeader, buffer->pBuffer,
                m_vdec_cfg.sequenceHeaderLen);
         buffer->nFilledLen -= m_vdec_cfg.sequenceHeaderLen;
         memmove(buffer->pBuffer,
            &buffer->pBuffer[m_vdec_cfg.sequenceHeaderLen],
            buffer->nFilledLen);
      } else
          if (strncmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.spark",
          28) == 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "SPARK clip \n");
         m_vdec_cfg.sequenceHeaderLen = 0;
         m_vdec_cfg.sequenceHeader = NULL;
      } else
          if (strncmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.h263",
          27) == 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "H263 clip \n");
         m_vdec_cfg.sequenceHeaderLen = 0;
         m_vdec_cfg.sequenceHeader = NULL;
      } else
          if (strncmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vp",
          25) == 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "VP6 clip \n");
         m_vdec_cfg.sequenceHeaderLen = 0;
         m_vdec_cfg.sequenceHeader = NULL;
      } else
          if (strncmp
         (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
          26) == 0) {
         m_vdec_cfg.sequenceHeader =
             (byte *) malloc(buffer->nFilledLen);
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "VC1 clip \n");
         if ((((*((OMX_U32 *) buffer->pBuffer)) &
               VC1_SP_MP_START_CODE_MASK) ==
              VC1_SP_MP_START_CODE)
             ||
             (((*((OMX_U32 *) buffer->pBuffer)) &
               VC1_SP_MP_START_CODE_MASK) ==
              VC1_SP_MP_START_CODE_RCV_V1)
             )
         {

            OMX_U32 pos_sequence_header =
                0, sequence_layer_len = 0;

            OMX_U32 *pBuf32, num_frames;
            OMX_U8 *pBuf8;

            pBuf32 = (OMX_U32 *) buffer->pBuffer;

            /* get the number of frames from the sequence header */
            num_frames = *pBuf32 & 0x00FFFFFF;

            /* size of struct C appears right after the number of frames information in the sequence header */
            m_vdec_cfg.sequenceHeaderLen = *(++pBuf32);

            /* advance the pointer by the size of struct C in order to get the height and width */
            pBuf8 = (OMX_U8 *) (++pBuf32);
            pBuf8 += m_vdec_cfg.sequenceHeaderLen;

            /* read the height and width information which are each 4 bytes */
            pBuf32 = (OMX_U32 *) pBuf8;
            m_vdec_cfg.height = *pBuf32;
            m_vdec_cfg.width = *(++pBuf32);

            /* get the position of struct C or sequence header */
            pos_sequence_header = OMX_VC1_POS_STRUCT_C;

            if (((*((OMX_U32 *) buffer->pBuffer)) &
                 VC1_SP_MP_START_CODE_MASK) ==
                VC1_SP_MP_START_CODE) {
               sequence_layer_len =
                   OMX_VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC
                   + m_vdec_cfg.sequenceHeaderLen;
            } else
                if (((*((OMX_U32 *) buffer->pBuffer)) &
                VC1_SP_MP_START_CODE_MASK) ==
               VC1_SP_MP_START_CODE_RCV_V1) {
               sequence_layer_len =
                   OMX_VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC
                   + m_vdec_cfg.sequenceHeaderLen;
            }

            /* copy the sequence header information to be sent to vdec core */
            memcpy(m_vdec_cfg.sequenceHeader,
                   &buffer->pBuffer[pos_sequence_header],
                   m_vdec_cfg.sequenceHeaderLen);

            for (int i = 0;
                 i < m_vdec_cfg.sequenceHeaderLen; i++) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "seq header: %x",
                        m_vdec_cfg.
                        sequenceHeader[i]);
            }

            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "\n");

            /* update the nFilledLen based on the number of bytes of the sequence layer */
            buffer->nFilledLen -= sequence_layer_len;
            memmove(buffer->pBuffer,
               &buffer->pBuffer[sequence_layer_len],
               buffer->nFilledLen);
         }

         else if (*((OMX_U32 *) buffer->pBuffer) == 0x0F010000) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                    "VC1 Advance profile\n");
            // Skip the start code. With the start code (first byte is 0x00),
            // then Q6 will think it is a simple profile
            m_vdec_cfg.sequenceHeaderLen =
                buffer->nFilledLen - 4;
            memcpy(m_vdec_cfg.sequenceHeader,
                   buffer->pBuffer + 4,
                   m_vdec_cfg.sequenceHeaderLen);
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error: Unknown VC1 profile\n");
         }
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "ERROR: Not supported codec. But let's try it anyway\n");
         m_vdec_cfg.sequenceHeaderLen = buffer->nFilledLen;
         memcpy(m_vdec_cfg.sequenceHeader, buffer->pBuffer,
                m_vdec_cfg.sequenceHeaderLen);
      }
   } else {
      m_vdec_cfg.sequenceHeader =
          (byte *) malloc(m_vendor_config.nDataSize);

      if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.mpeg4", 28)
          == 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Mpeg4 clip \n");
         memcpy(m_vdec_cfg.sequenceHeader, m_vendor_config.pData,
                m_vendor_config.nDataSize);
         m_vdec_cfg.sequenceHeaderLen =
             m_vendor_config.nDataSize;
#if 1            // temporary until q6 can decode without VOL header
         memmove(buffer->pBuffer + buffer->nOffset +
            m_vendor_config.nDataSize,
            buffer->pBuffer + buffer->nOffset,
            buffer->nFilledLen);
         memcpy(buffer->pBuffer + buffer->nOffset,
                m_vendor_config.pData,
                m_vendor_config.nDataSize);
         buffer->nFilledLen += m_vendor_config.nDataSize;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "size %d\n", buffer->nFilledLen);
#endif
      } else
          if ((strncmp
          (m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1",
           26) == 0)
         &&
         ((((*((OMX_U32 *) m_vendor_config.pData)) &
            VC1_SP_MP_START_CODE_MASK) == VC1_SP_MP_START_CODE)
          ||
          (((*((OMX_U32 *) m_vendor_config.pData)) &
            VC1_SP_MP_START_CODE_MASK) ==
           VC1_SP_MP_START_CODE_RCV_V1)

         )) {

         OMX_U32 pos_sequence_header = 0, sequence_layer_len = 0;

         OMX_U32 *pBuf32, num_frames = 0;
         OMX_U8 *pBuf8;

         pBuf32 = (OMX_U32 *) m_vendor_config.pData;

         /* get the number of frames from the sequence header */
         num_frames = *pBuf32 & 0x00FFFFFF;

         /* size of struct C appears right after the number of frames information in the sequence header */
         m_vdec_cfg.sequenceHeaderLen = *(++pBuf32);

         /* advance the pointer by the size of struct C in order to get the height and width */
         pBuf8 = (OMX_U8 *) (++pBuf32);
         pBuf8 += m_vdec_cfg.sequenceHeaderLen;

         /* read the height and width information which are each 4 bytes */
         pBuf32 = (OMX_U32 *) pBuf8;
         m_vdec_cfg.height = *pBuf32;
         m_vdec_cfg.width = *(++pBuf32);

         /* the struct C / sequence header starts at position 8 in the sequence header */
         pos_sequence_header = OMX_VC1_POS_STRUCT_C;

         if (((*((OMX_U32 *) m_current_frame->pBuffer)) &
              VC1_SP_MP_START_CODE_MASK) ==
             VC1_SP_MP_START_CODE) {
            m_vendor_config.nDataSize =
                OMX_VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC +
                m_vdec_cfg.sequenceHeaderLen;
         } else
             if (((*((OMX_U32 *) m_current_frame->pBuffer)) &
             VC1_SP_MP_START_CODE_MASK) ==
            VC1_SP_MP_START_CODE_RCV_V1) {
            m_vendor_config.nDataSize =
                OMX_VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC +
                m_vdec_cfg.sequenceHeaderLen;
         }

         /* copy the sequence header information to be sent to vdec core */
         memcpy(m_vdec_cfg.sequenceHeader,
                &m_vendor_config.pData[pos_sequence_header],
                m_vdec_cfg.sequenceHeaderLen);
      } else {
         memcpy(m_vdec_cfg.sequenceHeader, m_vendor_config.pData,
                m_vendor_config.nDataSize);
         m_vdec_cfg.sequenceHeaderLen =
             m_vendor_config.nDataSize;
      }
   }

#if 1
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "OMX - header, length %d\n",
            m_vdec_cfg.sequenceHeaderLen);
   for (OMX_U32 i = 0; i < m_vdec_cfg.sequenceHeaderLen; i++) {
      printf("0x%.2x ", m_vdec_cfg.sequenceHeader[i]);
      if (i % 16 == 15) {
         printf("\n");
      }
   }
   printf("\n");
#endif

   m_vdec = vdec_open(&m_vdec_cfg);
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "vdec_open[%p]\n",
            m_vdec);
   if (!m_vdec) {
      m_bInvalidState = true;
      m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
              OMX_ErrorInsufficientResources, 0, NULL);
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "ERROR!!! vdec_open failed\n");
      if (m_vdec_cfg.sequenceHeader) {
         free(m_vdec_cfg.sequenceHeader);
      }
      return OMX_ErrorHardware;
   }
   if(m_bInterlaced) {
     vdec_performance_change_request(m_vdec, VDEC_PERF_SET_MAX);
   }

   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0) {
      if (m_h264_utils != NULL) {
         m_h264_utils->allocate_rbsp_buffer(OMX_CORE_INPUT_BUFFER_SIZE);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "ERROR!!! m_h264_utils exist\n");
      }
   }

   if (m_vdec_cfg.sequenceHeader) {
      free(m_vdec_cfg.sequenceHeader);
   }
   return eRet;
}

/* ======================================================================
FUNCTION
  omx_vdec::omx_vdec_free_output_port_memory

DESCRIPTION
  Free output port memory

PARAMETERS
  None.

RETURN VALUE
  None
========================================================================== */
void omx_vdec::omx_vdec_free_output_port_memory(void) {
   if (m_out_mem_ptr) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the Output Memory\n");
      free(m_out_mem_ptr);
      m_out_mem_ptr = NULL;
   }
   if (m_platform_list) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the platform list\n");
      free(m_platform_list);
      m_platform_list = NULL;
   }
   if (m_platform_entry) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the platform entry\n");
      m_platform_entry = NULL;
   }
   if (m_pmem_info) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the pmem info\n");
      m_pmem_info = NULL;
   }
   if(omx_vdec_get_use_egl_buf_flg() && m_vdec_cfg.outputBuffer) {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Freeing the output buf info egl \n");
      if (m_vdec_cfg.outputBuffer) {
         free(m_vdec_cfg.outputBuffer);
         m_vdec_cfg.outputBuffer = NULL;
      }
   }
}

/* ======================================================================
FUNCTION
  omx_vdec::get_free_input_buffer

DESCRIPTION
  get a free input buffer

PARAMETERS
  None

RETURN VALUE
  pointer to input buffer header
  NULL if couldn't find
========================================================================== */
OMX_BUFFERHEADERTYPE *omx_vdec::get_free_input_buffer() {
   int i;
   for (i = 0; i < m_inp_buf_count; i++) {
      if (m_input_buff_info[i].bfree_input) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "get_free_input_buffer_index - Find free input buffer %d\n",
                  i);
         m_input_buff_info[i].bfree_input = false;
         return input[i];
      }
   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "get_free_input_buffer_index - Couldn't find free extra buffer\n");
   return NULL;
}

/* ======================================================================
FUNCTION
  omx_vdec::find_input_buffer_index

DESCRIPTION
  find input buffer index

PARAMETERS
  OMX_U8* buffer

RETURN VALUE
  index of extra buffer
  -1 if couldn't find
========================================================================== */
OMX_S8 omx_vdec::find_input_buffer_index(OMX_BUFFERHEADERTYPE * pBuffer) {
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "find_input_buffer_index %p\n", pBuffer);
   OMX_S8 i;
   for (i = 0; i < m_inp_buf_count; i++) {
      if (pBuffer == input[i]) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "find_input_buffer_index %d\n", i);
         return i;
      }
   }
   return -1;
}

/* ======================================================================
FUNCTION
  omx_vdec::free_input_buffer

DESCRIPTION
  free input buffer that is passed

PARAMETERS
  OMX_BUFFERHEADERTYPE  *pBuffer

RETURN VALUE
  true if successfull
  false if buffer is not part extra buffer
========================================================================== */
bool omx_vdec::free_input_buffer(OMX_BUFFERHEADERTYPE * pBuffer) {
   int i;
   for (i = 0; i < m_inp_buf_count; i++) {
      if (pBuffer == input[i]) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "free_input_buffer input %p\n", input);
         if (m_input_buff_info[i].pArbitrary_bytes_freed) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "free_input_buffer - EmptyBufferDone!!\n");
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       m_input_buff_info[i].
                       pArbitrary_bytes_freed);
            m_input_buff_info[i].pArbitrary_bytes_freed =
                NULL;
         }
         m_input_buff_info[i].bfree_input = true;
         m_bWaitForResource = false;
         return true;
      }
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
            "free_input_buffer - Error: Couldn't find input %p\n",
            input);
   return false;
}

/* ======================================================================
FUNCTION
  omx_vdec::get_free_extra_buffer_index

DESCRIPTION
  get an extra buffer that's been already allocated

PARAMETERS
  None

RETURN VALUE
  index of extra buffer
  -1 if couldn't find
========================================================================== */
OMX_S8 omx_vdec::get_free_extra_buffer_index() {
   int i;
   for (i = 0; i < m_inp_buf_count; i++) {
      if (!m_extra_buf_info[i].bExtra_pBuffer_in_use) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "get_free_extra_buffer - Find free extra buffer %d\n",
                  i);
         m_extra_buf_info[i].bExtra_pBuffer_in_use = true;
         return i;
      }
   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
           "ERROR: get_free_extra_buffer - Couldn't find free extra buffer\n");
   return -1;
}

/* ======================================================================
FUNCTION
  omx_vdec::find_extra_buffer_index

DESCRIPTION
  determine if buffer is belong to an extra buffer

PARAMETERS
  OMX_U8* buffer

RETURN VALUE
  index of extra buffer
  -1 if couldn't find
========================================================================== */
OMX_S8 omx_vdec::find_extra_buffer_index(OMX_U8 * buffer) {
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "find_extra_buffer_index %p\n", buffer);
   OMX_S8 i;
   for (i = 0; i < m_inp_buf_count; i++) {
      if (m_extra_buf_info[i].bExtra_pBuffer_in_use
          && (buffer == m_extra_buf_info[i].extra_pBuffer)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "find_extra_buffer_index - Find used extra buffer %d\n",
                  i);
         return i;
      }
   }
   return -1;
}

/* ======================================================================
FUNCTION
  omx_vdec::free_extra_buffer

DESCRIPTION
  free extra buffer that is passed

PARAMETERS
  OMX_S8 index

RETURN VALUE
  true if successfull
  false if buffer is not part extra buffer
========================================================================== */
bool omx_vdec::free_extra_buffer(OMX_S8 index) {
   if (index < 0 || index > OMX_CORE_NUM_INPUT_BUFFERS) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "free_extra_buffer - Error: Invalid index \n");
      return false;
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "free_extra_buffer index %d\n", index);
   m_extra_buf_info[index].bExtra_pBuffer_in_use = false;

   return true;
}

/* ======================================================================
FUNCTION
  initialize_arbitrary_bytes_environment

DESCRIPTION
  Initialize some member variables used for arbitrary bytes input packing format
  This function is used before parsing arbitrary input buffer or
  after doing flush to parse the next I frame (seek, fast forward, fast rewind, etc).

PARAMETERS
  none

RETURN VALUE
  true if success
  false otherwise
========================================================================== */
void omx_vdec::initialize_arbitrary_bytes_environment() {

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "initialize_arbitrary_bytes_environment \n");
   if (m_bStartCode) {
      m_arbitrary_bytes_info.start_code.m_last_4byte = 0xFFFFFFFF;
      m_arbitrary_bytes_info.start_code.m_last_start_code = 0;
   } else {
      m_arbitrary_bytes_info.frame_size.m_size_byte_left = 0;
      m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      m_arbitrary_bytes_info.frame_size.m_timestamp_byte_left = 0;
   }

   m_is_copy_truncated = false;

   m_current_frame = NULL;
   m_current_arbitrary_bytes_input = NULL;
   m_pcurrent_frame = NULL;
   m_bPartialFrame = false;
}

/* ======================================================================
FUNCTION
  omx_vdec::get_one_complete_frame

DESCRIPTION
  Try to get one complete frame

PARAMETERS
  OMX_OUT   OMX_BUFFERHEADERTYPE* dest

RETURN VALUE
  true if get one complete frame
  false if get partial frame or failed
========================================================================== */
bool omx_vdec::get_one_complete_frame(OMX_OUT OMX_BUFFERHEADERTYPE * dest) {
   if (m_current_arbitrary_bytes_input == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error - m_current_arbitrary_bytes_input is NULL \n");
      return false;
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "Start of get_one_complete_frame - flags %d\n",
            m_current_arbitrary_bytes_input->nFlags);

   get_one_frame(dest, m_current_arbitrary_bytes_input, &m_bPartialFrame);
   while (m_bPartialFrame) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_complete_frame got partial frame dest->pBuffer %p\n",
               dest->pBuffer);
      unsigned p1;   // Parameter - 1
      unsigned p2;   // Parameter - 2
      unsigned ident = 0;

      mutex_lock();
      if (m_etb_arbitrarybytes_q.m_size) {
         m_etb_arbitrarybytes_q.delete_entry(&p1, &p2, &ident);
      }
      mutex_unlock();
      if (ident == OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES) {
         m_current_arbitrary_bytes_input =
             (OMX_BUFFERHEADERTYPE *) p2;

         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_complete_frame - partial frame continue to get one frame %p %p offset %d flags %d\n",
                  m_current_arbitrary_bytes_input,
                  m_current_arbitrary_bytes_input->pBuffer,
                  m_current_arbitrary_bytes_input->nOffset,
                  m_current_arbitrary_bytes_input->nFlags);
         get_one_frame(dest, m_current_arbitrary_bytes_input,
                  &m_bPartialFrame);
      } else {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_complete_frame - No buffer available. Try again later\n",
                  p2, ident);
         m_current_arbitrary_bytes_input = NULL;
         break;
      }
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "After get_one_complete_frame - m_current_frame %p %p\n",
            dest, dest->pBuffer);
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "After get_one_complete_frame - nFilledLen %d nOffset %d nFlags %d partial %d\n",
            dest->nFilledLen, dest->nOffset, dest->nFlags,
            m_bPartialFrame);

   if (!m_bPartialFrame) {
      dest->nOffset = 0;
#if DEBUG_ON
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_complete_frame buffer, length %d %x\n",
               dest->nFilledLen, dest->nFilledLen);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", dest->pBuffer[i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      if (dest->nFilledLen > 64) {
         printf(".....\n");
         for (OMX_U32 i = dest->nFilledLen - 32;
              i < dest->nFilledLen; i++) {
            printf("0x%.2x ", dest->pBuffer[i]);
            if (i % 16 == 15) {
               printf("\n");
            }
         }
      }
      printf("\n");
#endif

      return true;
   } else {
      //m_current_frame = dest;
      return false;
   }
}

/* ======================================================================
FUNCTION
  omx_vdec::get_one_frame

DESCRIPTION
  - strore one frame from source to dest
  - set the dest->offset to indicate the beginning of the new frame
  - set the source->offset for the next frame
  - update the remaining source->nFilledLen

PARAMETERS
  OMX_OUT   OMX_BUFFERHEADERTYPE* dest,
  OMX_IN    OMX_BUFFERHEADERTYPE* source,
  OMX_INOUT bool *isPartialFrame

RETURN VALUE
  OMX_ERRORTYPE
========================================================================== */
OMX_ERRORTYPE omx_vdec::get_one_frame(OMX_OUT OMX_BUFFERHEADERTYPE * dest,
                  OMX_IN OMX_BUFFERHEADERTYPE * source,
                  OMX_INOUT bool * isPartialFrame) {
   QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_one_frame dest %p source %p isPartialFrame %d\n",
            dest, source, *isPartialFrame);

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "Before get_one_frame\n");
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "dest pBuf %p len %d offset %d flags %d\n", dest->pBuffer,
            dest->nFilledLen, dest->nOffset, dest->nFlags);
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "source pBuf %p len %d offset %d flags %d\n",
            source->pBuffer, source->nFilledLen, source->nOffset,
            source->nFlags);

#if DEBUG_ON
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_one_frame source, length %d\n", source->nFilledLen);
   for (OMX_U32 i = 0; (i < 32) && (i < source->nFilledLen); i++) {
      printf("0x%.2x ", source->pBuffer[source->nOffset + i]);
      if (i % 16 == 15) {
         printf("\n");
      }
   }
   printf("\n");
#endif

   if (m_bStartCode) {
      get_one_frame_using_start_code(dest, source, isPartialFrame);
   } else {
      if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26)
          == 0) {
         get_one_frame_h264_size_nal(dest, source,
                      isPartialFrame);
      } else {
         get_one_frame_sp_mp_vc1(dest, source, isPartialFrame);
      }
   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "After get_one_frame\n");
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "dest pBuf %p len %d offset %d flags %d\n", dest->pBuffer,
            dest->nFilledLen, dest->nOffset, dest->nFlags);
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "source pBuf %p len %d offset %d flags %d\n",
            source->pBuffer, source->nFilledLen, source->nOffset,
            source->nFlags);

   return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_vdec::get_one_frame_using_start_code

DESCRIPTION

PARAMETERS
  OMX_OUT   OMX_BUFFERHEADERTYPE* dest,
  OMX_IN    OMX_BUFFERHEADERTYPE* source,
  OMX_IN    OMX_U32 start_code,
  OMX_INOUT OMX_U32 *current_position,
  OMX_INOUT bool *isPartialFrame

RETURN VALUE
  OMX_ERRORTYPE
========================================================================== */
OMX_ERRORTYPE omx_vdec::
    get_one_frame_using_start_code(OMX_OUT OMX_BUFFERHEADERTYPE * dest,
               OMX_IN OMX_BUFFERHEADERTYPE * source,
               OMX_INOUT bool * isPartialFrame) {
   OMX_U32 code = m_arbitrary_bytes_info.start_code.m_last_4byte;
   OMX_U32 readSize = 0;
   OMX_U32 copy_size = 0;
   OMX_U32 pos = 0;
   OMX_U32 in_len = source->nFilledLen;
   OMX_U8 *inputBitStream = source->pBuffer + source->nOffset;
   OMX_U8 scl = 4; // by default start code length is 4
   bool bH264 = false;

   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0)
   {
     scl = 3; // h264 uses 3 bytes start code
     bH264 = true;
   }

   // To concatenate with previous frame if there is
   if (*isPartialFrame == false) {
      dest->nOffset = dest->nFilledLen;
   }

   while (pos < in_len) {
      code <<= 8;
      code |= (0x000000FF & *inputBitStream);
      if ((code & m_arbitrary_bytes_info.start_code.
           m_start_code_mask) ==
          m_arbitrary_bytes_info.start_code.m_start_code) {
         scl = 4;
         if (bH264 && (code>>24))
            scl = 3;
         if (readSize > scl -1 ) {
             // in this case, this start code is the beyond
             // the buffer boundary, fully inside inputBitStream buffer
            readSize = readSize - scl + 1;
            copy_size = readSize;
            if ((m_arbitrary_bytes_info.start_code.
                 m_last_start_code & m_arbitrary_bytes_info.
                 start_code.m_start_code_mask)
                == m_arbitrary_bytes_info.start_code.
                m_start_code) {
                 // we have a start code left over from the previous frame
                 scl = 4;
                 if (bH264 &&
                     (m_arbitrary_bytes_info.start_code.m_last_start_code >> 24))
                       scl = 3; // only 2 zeros are in the start code
               QTV_MSG_PRIO3(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "source->nOffset = %d start_code %x %x\n",
                        source->nOffset, code,
                        m_arbitrary_bytes_info.
                        start_code.
                        m_last_start_code);
               if (find_extra_buffer_index
                   (dest->pBuffer) == -1) {
                  OMX_S8 index =
                      get_free_extra_buffer_index
                      ();
                  if (index != -1) {
                     OMX_U8 *temp_buffer =
                         dest->pBuffer;
                     OMX_U32 temp_size =
                         dest->nFilledLen;
                     dest->pBuffer =
                         m_extra_buf_info
                         [index].
                         extra_pBuffer;
                     if(dest->nAllocLen < dest->nFilledLen) {
                         QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                                  QTVDIAG_PRIO_ERROR,
                                  "Not enough memory %d \n",
                                  __LINE__);
                         temp_size = dest->nAllocLen;
                         m_is_copy_truncated = true;
                     }
                     memcpy(dest->pBuffer,
                            temp_buffer,
                            temp_size);
                  } else {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_ERROR,
                          "Couldn't find extra buffer\n");
                     return
                         OMX_ErrorHardware;
                  }

                  QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "Copy the start_code %d %x\n",
                           dest->nFilledLen,
                           m_arbitrary_bytes_info.
                           start_code.
                           m_last_start_code);
                  for (OMX_S8 i = 0; i < scl; i++) {
                     *(dest->pBuffer +
                       dest->nFilledLen) =
                   (OMX_U8) ((m_arbitrary_bytes_info.
                         start_code.
                         m_last_start_code & 0xFF
                         << (8 * (scl - 1 - i))) >> (8 *
                                (scl - 1 - i)));
                     dest->nFilledLen++;
                  }
               }
               if(!m_is_copy_truncated) {
                  copy_size = readSize;
                  if((dest->nAllocLen - dest->nFilledLen)
                      < readSize) {
                     QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                                  QTVDIAG_PRIO_ERROR,
                                  "ERROR -- memcpy failed at line %d \n",
                                  __LINE__);
                     copy_size = dest->nAllocLen - dest->nFilledLen;
                     m_is_copy_truncated = true;
                  }
                  memcpy(dest->pBuffer + dest->nFilledLen,
                         source->pBuffer +
                         source->nOffset, copy_size);
              }
            } else
                if (find_extra_buffer_index(dest->pBuffer)
               != -1) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_MED,
                       "Concatenate to extra buffer\n");
               if(!m_is_copy_truncated) {
                  copy_size = readSize;
                  if((dest->nAllocLen - dest->nFilledLen)
                      < readSize) {
                     QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                                  QTVDIAG_PRIO_ERROR,
                                  "ERROR -- memcpy failed at line %d \n",
                                  __LINE__);
                     copy_size = dest->nAllocLen - dest->nFilledLen;
                     m_is_copy_truncated = true;
                  }
                  memcpy(dest->pBuffer + dest->nFilledLen,
                         source->pBuffer +
                         source->nOffset, copy_size);
              }
            }
            dest->nFilledLen += copy_size;
            dest->nFlags = source->nFlags;
            if (-1 == dest->nTimeStamp)
            {
                dest->nTimeStamp = source->nTimeStamp;
            }
            *isPartialFrame = false;
            m_is_copy_truncated = false;
            m_arbitrary_bytes_info.start_code.
                m_last_start_code = 0x00;
            break;
         } else if (*isPartialFrame) {
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "Start code boundary = %x %d\n",
                     code, readSize);
            m_arbitrary_bytes_info.start_code.
                m_last_start_code = code;
            dest->nFilledLen -= (scl - 1 - readSize);
            dest->nFlags = source->nFlags;
            *isPartialFrame = false;
            m_is_copy_truncated = false;
            readSize++;
            break;
         } else {
            m_arbitrary_bytes_info.start_code.
                m_last_start_code = 0x00;
         }
      }
      inputBitStream++;
      pos++;
      readSize++;
   }
   m_arbitrary_bytes_info.start_code.m_last_4byte = code;

   if (pos == source->nFilledLen) {
      OMX_S8 index;
      if (*isPartialFrame == false) {
         index = get_free_extra_buffer_index();
         if (index != -1) {
            QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "With free extra buffer used %p %p %d %d\n",
                     m_extra_buf_info[index].
                     extra_pBuffer, dest->pBuffer,
                     dest->nOffset, dest->nFilledLen);
            OMX_U8 *temp_buffer = dest->pBuffer;
            OMX_U32 temp_size = dest->nOffset;
            dest->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
            if(dest->nAllocLen < dest->nOffset) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                       "Not enough memory %d \n",
                        __LINE__);
               temp_size = dest->nAllocLen;
               m_is_copy_truncated = true;
            }
            memcpy(dest->pBuffer, temp_buffer,
                   temp_size);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "Error - couldn't get free extra buffer %p",
                     dest->pBuffer);
            return OMX_ErrorHardware;
         }

         if (m_extra_buf_info[index].arbitrarybytesInput) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR - Previous arbitrary bytes input hasn't been freed");
         }
         m_extra_buf_info[index].arbitrarybytesInput = source;
      }

      if ((m_arbitrary_bytes_info.start_code.
           m_last_start_code & m_arbitrary_bytes_info.start_code.
           m_start_code_mask)
          == m_arbitrary_bytes_info.start_code.m_start_code) {
          scl = 4;
          if (bH264 &&
              (m_arbitrary_bytes_info.start_code.m_last_start_code>>24))
             scl = 3;
         for (OMX_S8 i = 0; i < scl; i++) {
            *(dest->pBuffer + dest->nFilledLen) =
                (OMX_U8) ((m_arbitrary_bytes_info.
                      start_code.
                      m_last_start_code & 0xFF << (8 *
                               (scl - 1 - i)))
                     >> (8 * (scl - 1 - i)));
            dest->nFilledLen++;
         }
         m_arbitrary_bytes_info.start_code.m_last_start_code =
             0x00;
      }
      if(!m_is_copy_truncated) {
         copy_size = readSize;
         if((dest->nAllocLen - dest->nFilledLen)
            < readSize) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_ERROR,
                       "ERROR -- memcpy failed at line %d \n",
                        __LINE__);
               copy_size = dest->nAllocLen - dest->nFilledLen;
               m_is_copy_truncated = true;
         }
         memcpy(dest->pBuffer + dest->nFilledLen,
                source->pBuffer + source->nOffset, copy_size);
      }
      dest->nFilledLen += copy_size;
      dest->nFlags = source->nFlags;
      dest->nTimeStamp = source->nTimeStamp;

      if (*isPartialFrame == true) {
         unsigned int nPortIndex =
             source -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame - EmptyBufferDone %p\n",
                     source);
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       source);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                     source);
         }
      } else {
         *isPartialFrame = true;
         source->nFilledLen -= readSize;
         source->nOffset += readSize;
         if(FLAG_THUMBNAIL_MODE == m_vdec_cfg.postProc)
         {
            unsigned int nPortIndex =
                source -
                (OMX_BUFFERHEADERTYPE *)
                m_arbitrary_bytes_input_mem_ptr;
            if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame - EmptyBufferDone %p\n",
                     source);
               m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                          source);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                        source);
            }
            m_extra_buf_info[index].arbitrarybytesInput = NULL;
         }
      }
      m_current_arbitrary_bytes_input = NULL;

      if (dest->nFlags & OMX_BUFFERFLAG_EOS) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "EOS observed\n");
         *isPartialFrame = false;
      }
#if DEBUG_ON
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame partial buffer, length %d\n",
               dest->nFilledLen);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", dest->pBuffer[dest->nOffset + i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      printf("\n");
#endif
   } else {
      source->nFilledLen -= readSize;
      source->nOffset += readSize;
   }

   return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  get_one_frame_h264_size_nal

DESCRIPTION
  get one nal from size nal length clip

PARAMETERS
  OMX_IN OMX_BUFFERHEADERTYPE* buffer.

RETURN VALUE
  true if success
  false otherwise
========================================================================== */
OMX_ERRORTYPE omx_vdec::
    get_one_frame_h264_size_nal(OMX_OUT OMX_BUFFERHEADERTYPE * dest,
            OMX_IN OMX_BUFFERHEADERTYPE * source,
            OMX_INOUT bool * isPartialFrame) {
   int i = 0;
   int j = 0;
   OMX_U8 temp_size[4];
   OMX_U32 sizeofNAL = 0;

   if ((source->nFilledLen == 0) && (source->nFlags & OMX_BUFFERFLAG_EOS)) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "EOS observed\n");
      dest->nFlags = source->nFlags;
      m_current_arbitrary_bytes_input = NULL;
      if (*isPartialFrame == true) {
         unsigned int nPortIndex =
             source -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame_h264_size_nal - EmptyBufferDone %p\n",
                     source);
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       source);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                     source);
         }
      }
      *isPartialFrame = false;
      return OMX_ErrorNone;
   }

   if (*isPartialFrame == false) {
      dest->nOffset = dest->nFilledLen;

      if (source->nFilledLen < m_nalu_bytes) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_one_frame_h264_size_nal - get_free_extra_buffer_index\n");
         OMX_S8 index = get_free_extra_buffer_index();
         if (index != -1) {
            OMX_U8 *temp_buffer = dest->pBuffer;
            dest->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
            memcpy(dest->pBuffer, temp_buffer,
                   dest->nFilledLen);

            memcpy(dest->pBuffer + dest->nFilledLen,
                   source->pBuffer + source->nOffset,
                   source->nFilledLen);
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error - Couldn't find extra buffer\n");
            return OMX_ErrorHardware;
         }

         if (m_extra_buf_info[index].arbitrarybytesInput) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR - Previous arbitrary bytes input hasn't been freed");
         }
         m_extra_buf_info[index].arbitrarybytesInput = source;
      }

      sizeofNAL = m_nalu_bytes;
      m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      while (source->nFilledLen && sizeofNAL) {
         sizeofNAL--;
         m_arbitrary_bytes_info.frame_size.m_size_remaining |=
             source->pBuffer[source->
                   nOffset] << (sizeofNAL << 3);
         source->nOffset++;
         dest->nFilledLen++;
         source->nFilledLen--;
      }
      m_arbitrary_bytes_info.frame_size.m_size_byte_left = sizeofNAL;
      QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_h264_size_nal - m_size_remaining %d %x m_size_byte_left %d\n",
               m_arbitrary_bytes_info.frame_size.
               m_size_remaining,
               m_arbitrary_bytes_info.frame_size.
               m_size_remaining,
               m_arbitrary_bytes_info.frame_size.
               m_size_byte_left);
      if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_frame_h264_size_nal - partialFrame at size nal length %d\n",
                  m_arbitrary_bytes_info.frame_size.
                  m_size_byte_left);
         *isPartialFrame = true;
         m_current_arbitrary_bytes_input = NULL;
         return OMX_ErrorNone;
      }
      dest->nTimeStamp = source->nTimeStamp;
   } else if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_h264_size_nal - find the frame size %d\n",
               m_arbitrary_bytes_info.frame_size.
               m_size_byte_left);

      sizeofNAL = m_arbitrary_bytes_info.frame_size.m_size_byte_left;

      while (source->nFilledLen && sizeofNAL) {
         sizeofNAL--;
         m_arbitrary_bytes_info.frame_size.m_size_remaining |=
             source->pBuffer[source->
                   nOffset] << (sizeofNAL << 3);
         dest->pBuffer[dest->nFilledLen] =
             source->pBuffer[source->nOffset];
         source->nOffset++;
         dest->nFilledLen++;
         source->nFilledLen--;
      }

      m_arbitrary_bytes_info.frame_size.m_size_byte_left = sizeofNAL;
      if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Error - Never go here, unless input buffer is extremely small\n");
      }
      dest->nTimeStamp = source->nTimeStamp;
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_one_frame_h264_size_nal - get the frame remaining size %d, dest size %d\n",
            m_arbitrary_bytes_info.frame_size.m_size_remaining,
            dest->nFilledLen);

   OMX_S8 extra_buffer_index = find_extra_buffer_index(dest->pBuffer);
   if (m_arbitrary_bytes_info.frame_size.m_size_remaining >=
       source->nFilledLen) {
      bool complete_nal = false;
      if (m_arbitrary_bytes_info.frame_size.m_size_remaining ==
          source->nFilledLen) {
         complete_nal = true;
      }
      if (extra_buffer_index == -1 && (!m_is_copy_truncated)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_one_frame_h264_size_nal - get_free_extra_buffer_index\n");
         OMX_S8 index = get_free_extra_buffer_index();
         if (index != -1) {
            OMX_U8 *temp_buffer = dest->pBuffer;
            unsigned int tmp_size = dest->nFilledLen;
            dest->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
            if (dest->nAllocLen < dest->nFilledLen) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "Not enough memory -  \n");
               tmp_size = dest->nAllocLen;
               m_is_copy_truncated = true;
            }
            memcpy(dest->pBuffer, temp_buffer, tmp_size);

         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_FATAL,
                    "Error - Couldn't find extra buffer\n");
            return OMX_ErrorHardware;
         }

         if (m_extra_buf_info[index].arbitrarybytesInput) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR - Previous arbitrary bytes input hasn't been freed");
         }
         m_extra_buf_info[index].arbitrarybytesInput = source;
      }
      if (!m_is_copy_truncated) {
         unsigned int copy_size = source->nFilledLen;
         if ((dest->nAllocLen - dest->nFilledLen) <
             source->nFilledLen) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR - we should never reach here memcpy failed at line %d",
                     __LINE__);
            copy_size = dest->nAllocLen - dest->nFilledLen;
            m_is_copy_truncated = true;
         }
         memcpy(dest->pBuffer + dest->nFilledLen,
                source->pBuffer + source->nOffset, copy_size);
         dest->nFilledLen += copy_size;
      }

      m_arbitrary_bytes_info.frame_size.m_size_remaining -=
          source->nFilledLen;
      source->nOffset += source->nFilledLen;
      source->nFilledLen = 0;

      if (*isPartialFrame == true) {
         unsigned int nPortIndex =
             source -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame_h264_size_nal - EmptyBufferDone %p\n",
                     source);
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       source);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                     source);
         }
      }

      if (complete_nal) {
         *isPartialFrame = false;
          m_is_copy_truncated = false;
          m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      } else {
         *isPartialFrame = true;
      }
      m_current_arbitrary_bytes_input = NULL;
   } else {
      unsigned int copy_size =
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      if (extra_buffer_index != -1 && (false == m_is_copy_truncated)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Concatenate to extra buffer\n");
         if ((dest->nAllocLen - dest->nFilledLen) <
             m_arbitrary_bytes_info.frame_size.
             m_size_remaining) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "Not enough memory - %d \n",
                     __LINE__);
            copy_size =
                (dest->nAllocLen - dest->nFilledLen);
            m_is_copy_truncated = true;
            dest->nFilledLen += copy_size;

         }

         memcpy(dest->pBuffer + dest->nFilledLen,
                source->pBuffer + source->nOffset, copy_size);

      }
      if (false == m_is_copy_truncated)
         dest->nFilledLen += copy_size;

      source->nOffset +=
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      source->nFilledLen -=
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      m_is_copy_truncated = false;
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_h264_size_nal - dest size %d\n",
               dest->nFilledLen);
      dest->nFlags = source->nFlags;

      if (source->nFilledLen == 0) {
         m_current_arbitrary_bytes_input = NULL;
         if (*isPartialFrame == false) {
            OMX_S8 input_index =
                find_input_buffer_index(dest);
            if ((input_index == -1)
                || m_input_buff_info[input_index].
                pArbitrary_bytes_freed) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "ERROR - Couldn't find input index %d or Previous arbitrary bytes input hasn't been freed",
                        input_index);
            }
            m_input_buff_info[input_index].
                pArbitrary_bytes_freed = source;
         }
      }
      *isPartialFrame = false;

#if DEBUG_ON
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_h264_size_nal partial buffer, length %d\n",
               dest->nFilledLen);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", dest->pBuffer[dest->nOffset + i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      printf("\n");
#endif
   }

   return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  get_one_frame_sp_mp_vc1

DESCRIPTION
  get one nal from size nal length clip

PARAMETERS
  OMX_IN OMX_BUFFERHEADERTYPE* buffer.

RETURN VALUE
  true if success
  false otherwise
========================================================================== */
OMX_ERRORTYPE omx_vdec::get_one_frame_sp_mp_vc1(OMX_OUT OMX_BUFFERHEADERTYPE *
                  dest,
                  OMX_IN OMX_BUFFERHEADERTYPE *
                  source,
                  OMX_INOUT bool *
                  isPartialFrame) {
   int i = 0;
   int j = 0;
   OMX_U8 temp_size[4];
   OMX_U8 temp_timestamp[4];
   OMX_U32 timestamp = 0;
   OMX_U32 frameSizeBytes = 0;

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
            "get_one_frame_sp_mp_vc1 - timestamp field present: %d\n",
            m_arbitrary_bytes_info.frame_size.
            m_timestamp_field_present);

   if ((source->nFilledLen == 0) && (source->nFlags & OMX_BUFFERFLAG_EOS)) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "EOS observed\n");
      dest->nFlags = source->nFlags;
      m_current_arbitrary_bytes_input = NULL;
      if (*isPartialFrame == true) {
         unsigned int nPortIndex =
             source -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame_sp_mp_vc1 - EmptyBufferDone %p\n",
                     source);
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       source);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                     source);
         }
      }
      *isPartialFrame = false;
      return OMX_ErrorNone;
   }
   // Frame size
   if (*isPartialFrame == false) {
      dest->nOffset = dest->nFilledLen;
      /* for RCV V1 format, there is no timestamp field, so this should not be parsed */
      if (m_arbitrary_bytes_info.frame_size.
          m_timestamp_field_present == 1) {
         m_arbitrary_bytes_info.frame_size.
             m_timestamp_byte_left = 5;
      }

      if (source->nFilledLen < 8) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_one_frame_sp_mp_vc1 - get_free_extra_buffer_index\n");
         OMX_S8 index = get_free_extra_buffer_index();
         if (index != -1) {
            dest->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error - Couldn't find extra buffer\n");
            return OMX_ErrorHardware;
         }

         if (m_extra_buf_info[index].arbitrarybytesInput) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR - Previous arbitrary bytes input hasn't been freed");
         }
         m_extra_buf_info[index].arbitrarybytesInput = source;
      }

      frameSizeBytes = 3;
      m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      while (source->nFilledLen && frameSizeBytes) {
         frameSizeBytes--;
         m_arbitrary_bytes_info.frame_size.m_size_remaining |=
             source->pBuffer[source->
                   nOffset] << ((2 -
                       frameSizeBytes) << 3);
         source->nOffset++;
         source->nFilledLen--;
      }

      m_arbitrary_bytes_info.frame_size.m_size_byte_left =
          frameSizeBytes;

      if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_frame_sp_mp_vc1 - partialFrame at size nal length %d\n",
                  m_arbitrary_bytes_info.frame_size.
                  m_size_byte_left);
         *isPartialFrame = true;
         m_current_arbitrary_bytes_input = NULL;
         return OMX_ErrorNone;
      } else {
         /* in the case of RCV V1 format, there is no timestamp field left, but increment byte for key field */
         if (m_arbitrary_bytes_info.frame_size.
             m_timestamp_field_present == 0
             && source->nFilledLen > 0) {
            source->nFilledLen--;
            source->nOffset++;
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "RCV V1- source filled len: %d, source offset: %d \n",
                     source->nFilledLen,
                     source->nOffset);
         }
      }
   } else if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_sp_mp_vc1 - find the frame size %d\n",
               m_arbitrary_bytes_info.frame_size.
               m_size_byte_left);

      frameSizeBytes =
          m_arbitrary_bytes_info.frame_size.m_size_byte_left;

      while (source->nFilledLen && frameSizeBytes) {
         frameSizeBytes--;
         m_arbitrary_bytes_info.frame_size.m_size_remaining |=
             source->pBuffer[source->
                   nOffset] << ((2 -
                       frameSizeBytes) << 3);
         source->nOffset++;
         source->nFilledLen--;
      }

      m_arbitrary_bytes_info.frame_size.m_size_byte_left =
          frameSizeBytes;

      if (m_arbitrary_bytes_info.frame_size.m_size_byte_left > 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Error - Never go here, unless input buffer is extremely small\n");
      } else {
         /* in the case of RCV V1, no timestamp field, but increment the byte for key field */
         if (m_arbitrary_bytes_info.frame_size.
             m_timestamp_field_present == 0
             && source->nFilledLen > 0) {
            source->nFilledLen--;
            source->nOffset++;
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "RCV V1- source filled len: %d, source offset: %d \n",
                     source->nFilledLen,
                     source->nOffset);
         }

      }
   }

   OMX_S8 extra_buffer_index = find_extra_buffer_index(dest->pBuffer);
   // Time stamp
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_one_frame_sp_mp_vc1 - partialFrame at time stamp byte left %d, nFilledLen %d\n",
            m_arbitrary_bytes_info.frame_size.m_timestamp_byte_left,
            source->nFilledLen);
   if (m_arbitrary_bytes_info.frame_size.m_timestamp_field_present == 1) {

      if ((m_arbitrary_bytes_info.frame_size.m_timestamp_byte_left >
           0) && (source->nFilledLen < 5)) {
         if (source->nFilledLen != 0) {
            if (m_arbitrary_bytes_info.frame_size.
                m_timestamp_byte_left == 5) {
               source->nFilledLen--;   // for KEY and RES
               source->nOffset++;
               m_arbitrary_bytes_info.frame_size.
                   m_timestamp_byte_left--;
            }
            frameSizeBytes =
                m_arbitrary_bytes_info.frame_size.
                m_timestamp_byte_left;
            timestamp = 0;

            while (source->nFilledLen && frameSizeBytes) {
               frameSizeBytes--;
               timestamp |=
                   source->pBuffer[source->
                         nOffset] << ((3 -
                             frameSizeBytes)
                            << 3);
               source->nOffset++;
               source->nFilledLen--;
            }
            dest->nTimeStamp = (OMX_TICKS) timestamp;
            m_arbitrary_bytes_info.frame_size.
                m_timestamp_byte_left = frameSizeBytes;
         }
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_frame_sp_mp_vc1 - partialFrame at time stamp %d %d %x %x\n",
                  m_arbitrary_bytes_info.frame_size.
                  m_timestamp_byte_left, dest->nTimeStamp,
                  dest->nTimeStamp, timestamp);
         *isPartialFrame = true;
         m_current_arbitrary_bytes_input = NULL;
         return OMX_ErrorNone;
      } else if (m_arbitrary_bytes_info.frame_size.
            m_timestamp_byte_left > 0) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "get_one_frame_sp_mp_vc1 - find the time stamp %d\n",
                  m_arbitrary_bytes_info.frame_size.
                  m_timestamp_byte_left);

         if (m_arbitrary_bytes_info.frame_size.
             m_timestamp_byte_left == 5) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "m_arbitrary_bytes_info.frame_size.m_timestamp_byte_left == 5\n");
            source->nFilledLen--;   // for KEY and RES
            source->nOffset++;
            m_arbitrary_bytes_info.frame_size.
                m_timestamp_byte_left--;
            dest->nTimeStamp = 0;
         }

         frameSizeBytes =
             m_arbitrary_bytes_info.frame_size.
             m_timestamp_byte_left;
         timestamp = dest->nTimeStamp;

         while (source->nFilledLen && frameSizeBytes) {
            frameSizeBytes--;
            timestamp |=
                source->pBuffer[source->
                      nOffset] << ((3 -
                          frameSizeBytes)
                         << 3);
            source->nOffset++;
            source->nFilledLen--;
         }

         dest->nTimeStamp = (OMX_TICKS) timestamp;
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "timeStamp %x dest timestamp %x, temp_timestamp %x\n",
                  timestamp, dest->nTimeStamp,
                  temp_timestamp);
         m_arbitrary_bytes_info.frame_size.
             m_timestamp_byte_left = frameSizeBytes;

         if (extra_buffer_index == -1) {
            dest->pBuffer += 8;
         }

         if (m_arbitrary_bytes_info.frame_size.
             m_timestamp_byte_left > 0) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "Error - Never go here, unless input buffer is extremely small\n");
         }
      }
   }
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_one_frame_sp_mp_vc1 - get the frame remaining size %d, dest size %d, dest ts %x %d\n",
            m_arbitrary_bytes_info.frame_size.m_size_remaining,
            dest->nFilledLen, dest->nTimeStamp, dest->nTimeStamp);

   if (m_arbitrary_bytes_info.frame_size.m_size_remaining >=
       source->nFilledLen) {
      bool complete_frame = false;
      if (m_arbitrary_bytes_info.frame_size.m_size_remaining ==
          source->nFilledLen) {
         complete_frame = true;
      }
      if (extra_buffer_index == -1 && (!m_is_copy_truncated)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "get_one_frame_sp_mp_vc1 - get_free_extra_buffer_index\n");
         OMX_S8 index = get_free_extra_buffer_index();
         if (index != -1) {
            OMX_U8 *temp_buffer = dest->pBuffer;
            unsigned int tmp_size = dest->nFilledLen;
            dest->pBuffer =
                m_extra_buf_info[index].extra_pBuffer;
            if (dest->nAllocLen < dest->nFilledLen) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_HIGH,
                       "Not enough memory -  \n");
               tmp_size = dest->nAllocLen;
               m_is_copy_truncated = true;
            }

            memcpy(dest->pBuffer, temp_buffer, tmp_size);
         } else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_FATAL,
                    "Error - Couldn't find extra buffer\n");
            return OMX_ErrorHardware;
         }

         if (m_extra_buf_info[index].arbitrarybytesInput) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR - Previous arbitrary bytes input hasn't been freed");
         }
         m_extra_buf_info[index].arbitrarybytesInput = source;
      }
      if (!m_is_copy_truncated) {
         unsigned int copy_size = source->nFilledLen;
         if ((dest->nAllocLen - dest->nFilledLen) <
             source->nFilledLen) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR - we should never reach here memcpy failed at line %d",
                     __LINE__);
            copy_size = dest->nAllocLen - dest->nFilledLen;
            m_is_copy_truncated = true;
         }

         memcpy(dest->pBuffer + dest->nFilledLen,
                source->pBuffer + source->nOffset, copy_size);
         dest->nFilledLen += copy_size;
      }

      m_arbitrary_bytes_info.frame_size.m_size_remaining -=
          source->nFilledLen;
      source->nOffset += source->nFilledLen;
      source->nFilledLen = 0;

      if (*isPartialFrame == true) {
         unsigned int nPortIndex =
             source -
             (OMX_BUFFERHEADERTYPE *)
             m_arbitrary_bytes_input_mem_ptr;
         if (nPortIndex < MAX_NUM_INPUT_BUFFERS) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "get_one_frame_sp_mp_vc1 - EmptyBufferDone %p\n",
                     source);
            m_cb.EmptyBufferDone(&m_cmp, m_app_data,
                       source);
         } else {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "ERROR!! Incorrect arbitrary bytes buffer %p\n",
                     source);
         }
      }
      if (complete_frame) {
         *isPartialFrame = false;
      } else {
         *isPartialFrame = true;
      }
      m_current_arbitrary_bytes_input = NULL;
   } else {
      unsigned int copy_size =
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      if (extra_buffer_index != -1 && (false == m_is_copy_truncated)) {

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Concatenate to extra buffer\n");
         if ((dest->nAllocLen - dest->nFilledLen) <
             m_arbitrary_bytes_info.frame_size.
             m_size_remaining) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "Not enough memory - %d \n",
                     __LINE__);
            copy_size =
                (dest->nAllocLen - dest->nFilledLen);
            m_is_copy_truncated = true;
            dest->nFilledLen += copy_size;
         }
         memcpy(dest->pBuffer + dest->nFilledLen,
                source->pBuffer + source->nOffset, copy_size);
      }
      if (false == m_is_copy_truncated)
         dest->nFilledLen += copy_size;
      source->nOffset +=
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      source->nFilledLen -=
          m_arbitrary_bytes_info.frame_size.m_size_remaining;
      m_arbitrary_bytes_info.frame_size.m_size_remaining = 0;
      m_is_copy_truncated = false;
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_sp_mp_vc1 - dest size %d\n",
               dest->nFilledLen);
      dest->nFlags = source->nFlags;

      if (source->nFilledLen == 0) {
         m_current_arbitrary_bytes_input = NULL;
         if (*isPartialFrame == false) {
            OMX_S8 input_index =
                find_input_buffer_index(dest);
            if ((input_index == -1)
                || m_input_buff_info[input_index].
                pArbitrary_bytes_freed) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_ERROR,
                        "ERROR - Couldn't find input index %d or Previous arbitrary bytes input hasn't been freed",
                        input_index);
            }
            m_input_buff_info[input_index].
                pArbitrary_bytes_freed = source;
         }
      }
      *isPartialFrame = false;

#if DEBUG_ON
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "get_one_frame_sp_mp_vc1 partial buffer, length %d\n",
               dest->nFilledLen);
      for (OMX_U32 i = 0; i < 32; i++) {
         printf("0x%.2x ", dest->pBuffer[dest->nOffset + i]);
         if (i % 16 == 15) {
            printf("\n");
         }
      }
      printf("\n");
#endif
   }

   return OMX_ErrorNone;
}

void omx_vdec::fill_extradata(OMX_INOUT OMX_BUFFERHEADERTYPE * pBufHdr,
               OMX_IN vdec_frame * frame) {
   int n = 0;
   Vdec_FrameDetailsType *frameDetails = &frame->frameDetails;
   uint32 addr = 0;
   uint32 end = (uint32) (pBufHdr->pBuffer + pBufHdr->nAllocLen);
   OMX_OTHER_EXTRADATATYPE *pExtraData = 0;
   OMX_QCOM_EXTRADATA_FRAMEINFO *pExtraFrameInfo = 0;
   OMX_QCOM_EXTRADATA_FRAMEDIMENSION *pExtraFrameDimension = 0;
   OMX_QCOM_EXTRADATA_CODEC_DATA *pExtraCodecData = 0;
   uint32 size = 0;
   pBufHdr->nFlags |= OMX_BUFFERFLAG_EXTRADATA;

   m_dec_width = frameDetails->nDecPicWidth;
   m_dec_height = frameDetails->nDecPicHeight;

   pBufHdr->nFilledLen = get_output_buffer_size() - getExtraDataSize();
   addr = (uint32) (pBufHdr->pBuffer + pBufHdr->nFilledLen);
   // align to a 4 byte boundary
   addr = (addr + 3) & (~3);

   // read to the end of existing extra data sections
   pExtraData = (OMX_OTHER_EXTRADATATYPE *) addr;

   // append the common frame info extra data
   size =
       (OMX_EXTRADATA_HEADER_SIZE + sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO) +
        3) & (~3);
   pExtraData->nSize = size;
   pExtraData->nVersion.nVersion = OMX_SPEC_VERSION;
   pExtraData->nPortIndex = 1;
   pExtraData->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataFrameInfo;   /* Extra Data type */
   pExtraData->nDataSize = sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO);   /* Size of the supporting data to follow */
   pExtraFrameInfo = (OMX_QCOM_EXTRADATA_FRAMEINFO *) pExtraData->data;
   pBufHdr->nFlags &= (~OMX_BUFFERFLAG_SYNCFRAME);
   if (frameDetails->ePicType[0] == VDEC_PICTURE_TYPE_I) {
   /* assume that there is stream which starts with a non I frame, in such
    * cases it makes sense to drop the decoded frame tills a I frame shows up
    */

   /* Q6 for H264 even for a P frame(if they come before the first I frame in the stream)
    * sends it as a I frame with 100% concealment, then we should not set the sync frame
    * flag.
    */
      if((false == m_first_sync_frame_rcvd)
      && (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0)
      && (100 == frameDetails->nPercentConcealedMacroblocks))
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,"First I frame with 100% concealment.");
      }
      else
      {
         pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,"First I frame received.");
         m_first_sync_frame_rcvd = true;
      }
   }


   if (frameDetails->ePicFormat == VDEC_PROGRESSIVE_FRAME)
      pExtraFrameInfo->interlaceType =
          OMX_QCOM_InterlaceFrameProgressive;
   else if (frameDetails->ePicFormat = VDEC_INTERLACED_FRAME) {
      if (frameDetails->bTopFieldFirst)
         pExtraFrameInfo->interlaceType =
             OMX_QCOM_InterlaceInterleaveFrameTopFieldFirst;
      else
         pExtraFrameInfo->interlaceType =
             OMX_QCOM_InterlaceInterleaveFrameBottomFieldFirst;
   }
   pExtraFrameInfo->panScan.numWindows = frameDetails->panScan.numWindows;
   for (n = 0; n < frameDetails->panScan.numWindows; n++) {
      pExtraFrameInfo->panScan.window[n].x =
          frameDetails->panScan.winHorOffset[n];
      pExtraFrameInfo->panScan.window[n].y =
          frameDetails->panScan.winVerOffset[n];
      pExtraFrameInfo->panScan.window[n].dx =
          frameDetails->panScan.winWidth[n];
      pExtraFrameInfo->panScan.window[n].dy =
          frameDetails->panScan.winHeight[n];
   }
   pExtraFrameInfo->nConcealedMacroblocks =
       frameDetails->nPercentConcealedMacroblocks;

   // append the derived timestamp or YUV range mapping
   addr += size;
   pExtraData = (OMX_OTHER_EXTRADATATYPE *) addr;
   size =
       (OMX_EXTRADATA_HEADER_SIZE + sizeof(OMX_QCOM_EXTRADATA_CODEC_DATA) +
        3) & (~3);
   pExtraData->nSize = size;
   pExtraData->nVersion.nVersion = OMX_SPEC_VERSION;
   pExtraData->nPortIndex = 1;
   pExtraCodecData = (OMX_QCOM_EXTRADATA_CODEC_DATA *) pExtraData->data;
   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0) {
      pExtraData->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataH264;   /* Extra Data type */
      pExtraData->nDataSize = sizeof(OMX_QCOM_H264EXTRADATA);   /* Size of the supporting data to follow */

      /* (frame->timestamp & SEI_TRIGGER_BIT_VDEC) -> tells if we asked for sei math
       * frame->flags & SEI_TRIGGER_BIT_QDSP) -> dsp is succesfull,if this is zero
       */
       pExtraCodecData->h264ExtraData.seiTimeStamp = 0;

      if(!(frame->timestamp & SEI_TRIGGER_BIT_VDEC) &&
         !(frame->flags & SEI_TRIGGER_BIT_QDSP))
      {
         /* default sei time stamp value */
         pExtraCodecData->h264ExtraData.seiTimeStamp |=  SEI_TRIGGER_BIT_VDEC;
      }
      else if((frame->timestamp & SEI_TRIGGER_BIT_VDEC) &&
         !(frame->flags & SEI_TRIGGER_BIT_QDSP))
      {
         /* DSP successfully calculated the time stamp*/
         pExtraCodecData->h264ExtraData.seiTimeStamp =
             frameDetails->calculatedTimeStamp;

         /*this is a internally set by vdec so no need to propagate
          *it to client
          */
         frame->flags &= (~SEI_TRIGGER_BIT_QDSP);
      }
      else if((frame->timestamp & SEI_TRIGGER_BIT_VDEC) &&
         (frame->flags & SEI_TRIGGER_BIT_QDSP))
      {
         /* sei math failure */
         pExtraCodecData->h264ExtraData.seiTimeStamp |=  SEI_TRIGGER_BIT_VDEC;

         /*this is a internally set by vdec so no need to propagate
          *it to client
          */
         frame->flags &= (~SEI_TRIGGER_BIT_QDSP);
      }

   } else if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.vc1", 26) ==
         0) {
      pExtraData->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataVC1;   /* Extra Data type */
      pExtraData->nDataSize = sizeof(OMX_QCOM_VC1EXTRADATA);   /* Size of the supporting data to follow */
      pExtraCodecData->vc1ExtraData.nVC1RangeY =
          frameDetails->nVC1RangeY;
      pExtraCodecData->vc1ExtraData.nVC1RangeUV =
          frameDetails->nVC1RangeUV;
      pExtraCodecData->vc1ExtraData.eVC1PicResolution =
          (OMX_QCOM_VC1RESOLUTIONTYPE) frameDetails->ePicResolution;
   }

   // append the Height and Width adjusted to multiple of 16  and Actual Height and Width
   addr += size;
   pExtraData = (OMX_OTHER_EXTRADATATYPE *)addr;
   size = (OMX_EXTRADATA_HEADER_SIZE + sizeof(OMX_QCOM_EXTRADATA_FRAMEDIMENSION) + 3 ) & (~3);
   pExtraData->nSize = size;
   pExtraData->nVersion.nVersion = OMX_SPEC_VERSION;
   pExtraData->nPortIndex = 1;
   if (strncmp(m_vdec_cfg.kind, "OMX.qcom.video.decoder.avc", 26) == 0)
   {
      pExtraData->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataFrameDimension;
      pExtraData->nDataSize = sizeof(OMX_QCOM_EXTRADATA_FRAMEDIMENSION);    /* Size of the supporting data to follow */
      pExtraFrameDimension = (OMX_QCOM_EXTRADATA_FRAMEDIMENSION *)pExtraData->data;
      pExtraFrameDimension->nDecWidth  = frameDetails->nDecPicWidth;
      pExtraFrameDimension->nDecHeight = frameDetails->nDecPicHeight;
      pExtraFrameDimension->nActualWidth = frameDetails->cwin.x2 - frameDetails->cwin.x1;
      pExtraFrameDimension->nActualHeight= frameDetails->cwin.y2 - frameDetails->cwin.y1;
   }

   /* khronos standardized way of converying interlaced info */
   OMX_STREAMINTERLACEFORMATTYPE *pInterlaceInfo=NULL;

   size =
       (OMX_EXTRADATA_HEADER_SIZE + sizeof(OMX_STREAMINTERLACEFORMATTYPE) +
        3) & (~3);
   pExtraData->nSize = size;
   pExtraData->nVersion.nVersion = OMX_SPEC_VERSION;
   pExtraData->nPortIndex = 1;
   pExtraData->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataInterlaceFormat;   /* Extra Data type */
   pExtraData->nDataSize = sizeof(OMX_STREAMINTERLACEFORMATTYPE);   /* Size of the supporting data to follow */
   pInterlaceInfo = (OMX_STREAMINTERLACEFORMATTYPE *) pExtraData->data;
   pInterlaceInfo ->nSize = sizeof(OMX_STREAMINTERLACEFORMATTYPE);
   pInterlaceInfo ->nVersion.nVersion = OMX_SPEC_VERSION;
   pInterlaceInfo ->nPortIndex = 1;

   if (frameDetails->ePicFormat == VDEC_PROGRESSIVE_FRAME)
   {
       pInterlaceInfo ->bInterlaceFormat = OMX_FALSE;
       pInterlaceInfo ->nInterlaceFormats = OMX_InterlaceFrameProgressive;
   }
   else if (frameDetails->ePicFormat = VDEC_INTERLACED_FRAME)
   {
      pInterlaceInfo ->bInterlaceFormat = OMX_TRUE;
      if (frameDetails->bTopFieldFirst)
       pInterlaceInfo ->nInterlaceFormats =
             OMX_InterlaceInterleaveFrameTopFieldFirst;
      else
       pInterlaceInfo ->nInterlaceFormats =
             OMX_InterlaceInterleaveFrameBottomFieldFirst;
   }

   // append extradata terminator
   addr += size;
   pExtraData = (OMX_OTHER_EXTRADATATYPE *) addr;
   pExtraData->nSize = OMX_EXTRADATA_HEADER_SIZE;
   pExtraData->nVersion.nVersion = OMX_SPEC_VERSION;
   pExtraData->nPortIndex = 1;
   pExtraData->eType = OMX_ExtraDataNone;
   pExtraData->nDataSize = 0;

   pBufHdr->nOffset = 0;
   if (frameDetails->cwin.x1 || frameDetails->cwin.y1)
   {
      pBufHdr->nOffset = frameDetails->cwin.y1 * frameDetails->nDecPicWidth + frameDetails->cwin.x1;
   }
}

/* ======================================================================
FUNCTION
  find_new_frame_ap_vc1

DESCRIPTION
  finds whether the BDU belongs to previous frame

PARAMETERS
  OMX_IN OMX_U8* buffer.
  OMX_IN OMX_U32 buffer_length
  OMX_OUT OMX_BOOL isNewFrame

RETURN VALUE
  true if success
  false otherwise
========================================================================== */
bool omx_vdec::find_new_frame_ap_vc1(OMX_IN OMX_U8 * buffer,
                 OMX_IN OMX_U32 buffer_length,
                 OMX_OUT OMX_BOOL & isNewFrame) {
   uint32 code = 0xFFFFFFFF;
   isNewFrame = OMX_TRUE;
   for (uint32 i = 0; i < buffer_length; i++) {
      code <<= 8;
      code |= *buffer++;

      if ((code & VC1_AP_SLICE_START_CODE_MASK) ==
          VC1_AP_SLICE_START_CODE) {
         isNewFrame = OMX_FALSE;
         break;
      }
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "find_new_frame_ap_vc1  %d\n", isNewFrame);
   return true;
}

