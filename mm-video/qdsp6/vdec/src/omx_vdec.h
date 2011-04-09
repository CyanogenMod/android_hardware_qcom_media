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

#ifndef __OMX_VDEC_H__
#define __OMX_VDEC_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Decoder

*//** @file comx_vdec.h
  This module contains the class definition for openMAX decoder component.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

/* Uncomment out below line
#define LOG_NDEBUG 0 if we want to see all LOGV messaging */

#include<stdlib.h>
#define LOG_TAG "QCvdec"

#include <stdio.h>
#ifdef _ANDROID_
#include "cutils/log.h"
#include <binder/MemoryHeapBase.h>
#define LOG_NDEBUG 0
#endif // _ANDROID_

#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "vdec.h"
#include "qc_omx_component.h"
#include "Map.h"
#include <omx_vdec_inpbuf.h>
#include "qtv_msg.h"
#include "OMX_QCOMExtns.h"
#include "H264_Utils.h"
#include "MP4_Utils.h"

extern "C" {
   void *get_omx_component_factory_fn(void);
}
#ifdef _ANDROID_
using namespace android;
    // local pmem heap object
class VideoHeap:public MemoryHeapBase {
      public:
   VideoHeap(int fd, size_t size, void *base);
    virtual ~ VideoHeap() {
}};
#endif // _ANDROID_
//////////////////////////////////////////////////////////////////////////////
//                       Module specific globals
//////////////////////////////////////////////////////////////////////////////
#define OMX_SPEC_VERSION  0x00000101

//////////////////////////////////////////////////////////////////////////////
//               Macros
//////////////////////////////////////////////////////////////////////////////
#define PrintFrameHdr(bufHdr) QTV_MSG_PRIO4(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,\
                        "bufHdr %x buf %x size %d TS %d\n",\
                       (unsigned) bufHdr,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->pBuffer,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nFilledLen,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nTimeStamp)

// BitMask Management logic
#define BITS_PER_BYTE        0x08
#define BITMASK_SIZE(mIndex) \
            (((mIndex) + BITS_PER_BYTE - 1)/BITS_PER_BYTE)
#define BITMASK_OFFSET(mIndex) \
            ((mIndex)/BITS_PER_BYTE)
#define BITMASK_FLAG(mIndex)  \
            (1 << ((mIndex) % BITS_PER_BYTE))
#define BITMASK_CLEAR(mArray,mIndex) \
            (mArray)[BITMASK_OFFSET(mIndex)] &=  ~(BITMASK_FLAG(mIndex))
#define BITMASK_SET(mArray,mIndex) \
            (mArray)[BITMASK_OFFSET(mIndex)] |=  BITMASK_FLAG(mIndex)
#define BITMASK_PRESENT(mArray,mIndex) \
            ((mArray)[BITMASK_OFFSET(mIndex)] & BITMASK_FLAG(mIndex))
#define BITMASK_ABSENT(mArray,mIndex) \
            (((mArray)[BITMASK_OFFSET(mIndex)] & BITMASK_FLAG(mIndex)) == 0x0)

#define OMX_CORE_MIN_INPUT_BUFFERS   1
#define OMX_CORE_NUM_INPUT_BUFFERS   MAX_NUM_INPUT_BUFFERS
#define OMX_CORE_NUM_OUTPUT_BUFFERS  32   //changed from 32 - 8
#define OMX_CORE_NUM_OUTPUT_BUFFERS_H264  8
#define OMX_CORE_NUM_OUTPUT_BUFFERS_MP4 6
#define OMX_CORE_NUM_OUTPUT_BUFFERS_VC1 6
#define OMX_CORE_INPUT_BUFFER_SIZE   (450 * 1024)
#define OMX_CORE_CONTROL_CMDQ_SIZE   100
#define OMX_CORE_QCIF_HEIGHT         144
#define OMX_CORE_QCIF_WIDTH          176
#define OMX_CORE_VGA_HEIGHT          480
#define OMX_CORE_VGA_WIDTH           640
#define OMX_CORE_WVGA_HEIGHT         480
#define OMX_CORE_WVGA_WIDTH          800
#define OMX_CORE_720P_HEIGHT         720
#define OMX_CORE_720P_WIDTH          1280
#define OMX_VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC 32
#define OMX_VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC 16
#define OMX_VC1_POS_STRUCT_C 8

class genericQueue {
      private:
   struct node {
      void *data;
      node *next;
   };
   node *head;
   node *tail;
   int numElements;
   //pthread_mutex_t queue_protector;
      public:

    genericQueue();

   int Enqueue(void *data);
   void *Dequeue();
   int GetSize();
   void *checkHead();
   void *checkTail();

   ~genericQueue();
};

// OMX video decoder class
class omx_vdec:public qc_omx_component, public omx_vdec_inpbuf {
      public:
   // video decoder input structure
   struct vdec_context m_vdec_cfg;   // video decoder input structure

    omx_vdec();      // constructor
    virtual ~ omx_vdec();   // destructor

   virtual OMX_ERRORTYPE create_msg_thread() = 0;
   virtual void post_message(unsigned char id) = 0;
   virtual void mutex_lock() = 0;
   virtual void mutex_unlock() = 0;
   virtual void semaphore_wait() = 0;
   virtual void semaphore_post() = 0;

   virtual OMX_ERRORTYPE allocate_buffer(OMX_HANDLETYPE hComp,
                     OMX_BUFFERHEADERTYPE ** bufferHdr,
                     OMX_U32 port,
                     OMX_PTR appData, OMX_U32 bytes);

   virtual OMX_ERRORTYPE component_deinit(OMX_HANDLETYPE hComp);

   virtual OMX_ERRORTYPE component_init(OMX_STRING role);

   virtual OMX_ERRORTYPE component_role_enum(OMX_HANDLETYPE hComp,
                    OMX_U8 * role, OMX_U32 index);

   virtual OMX_ERRORTYPE component_tunnel_request(OMX_HANDLETYPE hComp,
                         OMX_U32 port,
                         OMX_HANDLETYPE
                         peerComponent,
                         OMX_U32 peerPort,
                         OMX_TUNNELSETUPTYPE *
                         tunnelSetup);

   virtual OMX_ERRORTYPE empty_this_buffer(OMX_HANDLETYPE hComp,
                  OMX_BUFFERHEADERTYPE * buffer);

   virtual OMX_ERRORTYPE fill_this_buffer(OMX_HANDLETYPE hComp,
                      OMX_BUFFERHEADERTYPE * buffer);

   void cancel_ftb_entry(OMX_BUFFERHEADERTYPE * buffer);
   virtual OMX_ERRORTYPE free_buffer(OMX_HANDLETYPE hComp,
                 OMX_U32 port,
                 OMX_BUFFERHEADERTYPE * buffer);

   virtual OMX_ERRORTYPE get_component_version(OMX_HANDLETYPE hComp,
                      OMX_STRING componentName,
                      OMX_VERSIONTYPE *
                      componentVersion,
                      OMX_VERSIONTYPE *
                      specVersion,
                      OMX_UUIDTYPE *
                      componentUUID);

   virtual OMX_ERRORTYPE get_config(OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE configIndex,
                OMX_PTR configData);

   virtual OMX_ERRORTYPE get_extension_index(OMX_HANDLETYPE hComp,
                    OMX_STRING paramName,
                    OMX_INDEXTYPE * indexType);

   virtual OMX_ERRORTYPE get_parameter(OMX_HANDLETYPE hComp,
                   OMX_INDEXTYPE paramIndex,
                   OMX_PTR paramData);

   virtual OMX_ERRORTYPE get_state(OMX_HANDLETYPE hComp,
               OMX_STATETYPE * state);

   virtual OMX_ERRORTYPE send_command(OMX_HANDLETYPE hComp,
                  OMX_COMMANDTYPE cmd,
                  OMX_U32 param1, OMX_PTR cmdData);

   virtual OMX_ERRORTYPE set_callbacks(OMX_HANDLETYPE hComp,
                   OMX_CALLBACKTYPE * callbacks,
                   OMX_PTR appData);

   virtual OMX_ERRORTYPE set_config(OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE configIndex,
                OMX_PTR configData);

   virtual OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE hComp,
                   OMX_INDEXTYPE paramIndex,
                   OMX_PTR paramData);

   virtual OMX_ERRORTYPE use_buffer(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE ** bufferHdr,
                OMX_U32 port,
                OMX_PTR appData,
                OMX_U32 bytes, OMX_U8 * buffer);

   virtual OMX_ERRORTYPE use_EGL_image(OMX_HANDLETYPE hComp,
                   OMX_BUFFERHEADERTYPE ** bufferHdr,
                   OMX_U32 port,
                   OMX_PTR appData, void *eglImage);

   static void buffer_done_cb(struct vdec_context *ctxt, void *cookie);

   static void buffer_done_cb_stub(struct vdec_context *ctxt,
               void *cookie);

   static void frame_done_cb_stub(struct vdec_context *ctxt,
                   struct vdec_frame *frame);

   static void frame_done_cb(struct vdec_context *ctxt,
              struct vdec_frame *frame);
   static void   frame_done_display_order_cb(struct vdec_context *ctxt,
                                              struct vdec_frame *frame);

   static void process_event_cb(struct vdec_context *ctxt,
                 unsigned char id);
      protected:
   // Bit Positions
   enum flags_bit_positions {
      // Defer transition to IDLE
      OMX_COMPONENT_IDLE_PENDING = 0x1,
      // Defer transition to LOADING
      OMX_COMPONENT_LOADING_PENDING = 0x2,
      // First  Buffer Pending
      OMX_COMPONENT_FIRST_BUFFER_PENDING = 0x3,
      // Second Buffer Pending
      OMX_COMPONENT_SECOND_BUFFER_PENDING = 0x4,
      // Defer transition to Enable
      OMX_COMPONENT_INPUT_ENABLE_PENDING = 0x5,
      // Defer transition to Enable
      OMX_COMPONENT_OUTPUT_ENABLE_PENDING = 0x6,
      // Defer transition to Disable
      OMX_COMPONENT_INPUT_DISABLE_PENDING = 0x7,
      // Defer transition to Disable
      OMX_COMPONENT_OUTPUT_DISABLE_PENDING = 0x8,
   };

   // Deferred callback identifiers
   enum {
      //Event Callbacks from the vdec component thread context
      OMX_COMPONENT_GENERATE_EVENT = 0x1,
      //Buffer Done callbacks from the vdec component thread context
      OMX_COMPONENT_GENERATE_BUFFER_DONE = 0x2,
      //Frame Done callbacks from the vdec component thread context
      OMX_COMPONENT_GENERATE_FRAME_DONE = 0x3,
      //Buffer Done callbacks from the vdec component thread context
      OMX_COMPONENT_GENERATE_FTB = 0x4,
      //Empty This Buffer event
      OMX_COMPONENT_GENERATE_ETB = 0x5,
      //Command
      OMX_COMPONENT_GENERATE_COMMAND = 0x6,
      //Push-Pending Buffers
      OMX_COMPONENT_PUSH_PENDING_BUFS = 0x7,
      //Empty This Buffer event for arbitrary bytes
      OMX_COMPONENT_GENERATE_ETB_ARBITRARYBYTES = 0x8,
      OMX_COMPONENT_GENERATE_EVENT_FLUSH = 0x9
   };

   enum header_state {
      HEADER_STATE_RECEIVED_NONE = 0x00,
      HEADER_STATE_RECEIVED_PARTIAL = 0x01,
      HEADER_STATE_RECEIVED_COMPLETE = 0x02
   };

   enum port_state {
      PORT_STATE_DISABLED = 0x00,
      PORT_STATE_ENABLED = 0x01,
      PORT_STATE_INVALID = 0x02,
   };         // PORT_SETTING

   enum port_indexes {
      OMX_CORE_INPUT_PORT_INDEX = 0,
      OMX_CORE_OUTPUT_PORT_INDEX = 1
   };

   struct omx_event {
      unsigned param1;
      unsigned param2;
      unsigned id;
      bool canceled;
   };

   struct omx_cmd_queue {
      omx_event m_q[OMX_CORE_CONTROL_CMDQ_SIZE];
      unsigned m_read;
      unsigned m_write;
      unsigned m_size;

       omx_cmd_queue();
      ~omx_cmd_queue();
      bool insert_entry(unsigned p1, unsigned p2, unsigned id);
      bool delete_entry(unsigned *p1, unsigned *p2, unsigned *id,
              bool * canceled = 0);

   };
   // Store buf Header mapping between OMX client and
   // PMEM allocated.
   typedef Map < OMX_BUFFERHEADERTYPE *, OMX_BUFFERHEADERTYPE * >
       use_buffer_map;
   // Get the pMem area from Video decoder
   void omx_vdec_get_out_buf_hdrs();
   // Get the pMem area from video decoder and copy to local use buf hdrs
   void omx_vdec_get_out_use_buf_hdrs();
   // Copy the decoded frame to the user defined buffer area
   void omx_vdec_cpy_user_buf(OMX_BUFFERHEADERTYPE * pBufHdr);

   void omx_vdec_add_entries();

   // Make a copy of the buf headers --> use buf only
   OMX_ERRORTYPE omx_vdec_dup_use_buf_hdrs();

   OMX_ERRORTYPE omx_vdec_check_port_settings(OMX_BUFFERHEADERTYPE *
                     buffer, unsigned &height,
                     unsigned &width,
                     bool & bInterlace,
                     unsigned &cropx,
                     unsigned &cropy,
                     unsigned &cropdx,
                     unsigned &cropdy);
   OMX_ERRORTYPE omx_vdec_validate_port_param(int height, int width);

   void omx_vdec_display_in_buf_hdrs();
   void omx_vdec_display_out_buf_hdrs();
   void omx_vdec_display_out_use_buf_hdrs();

   bool allocate_done(void);
   bool allocate_input_done(void);
   bool allocate_output_done(void);

   OMX_ERRORTYPE allocate_input_buffer(OMX_HANDLETYPE hComp,
                   OMX_BUFFERHEADERTYPE ** bufferHdr,
                   OMX_U32 port,
                   OMX_PTR appData, OMX_U32 bytes);

   OMX_ERRORTYPE allocate_output_buffer(OMX_HANDLETYPE hComp,
                    OMX_BUFFERHEADERTYPE ** bufferHdr,
                    OMX_U32 port, OMX_PTR appData,
                    OMX_U32 bytes);

   OMX_ERRORTYPE use_input_buffer(OMX_HANDLETYPE hComp,
                   OMX_BUFFERHEADERTYPE ** bufferHdr,
                   OMX_U32 port,
                   OMX_PTR appData,
                   OMX_U32 bytes, OMX_U8 * buffer);

   OMX_ERRORTYPE use_output_buffer(OMX_HANDLETYPE hComp,
               OMX_BUFFERHEADERTYPE ** bufferHdr,
               OMX_U32 port,
               OMX_PTR appData,
               OMX_U32 bytes, OMX_U8 * buffer);
   bool execute_omx_flush(OMX_U32);
   bool execute_output_flush(void);
   bool execute_input_flush(void);

   unsigned push_one_input_buffer(OMX_BUFFERHEADERTYPE * buffer);
   unsigned push_pending_buffers(void);
   unsigned push_pending_buffers_proxy(void);

   OMX_ERRORTYPE add_entry_subframe_stitching(OMX_IN OMX_BUFFERHEADERTYPE *
                     buffer);

   OMX_ERRORTYPE empty_this_buffer_proxy_arbitrary_bytes(OMX_HANDLETYPE
                           hComp,
                           OMX_BUFFERHEADERTYPE
                           * buffer);

   OMX_ERRORTYPE empty_this_buffer_proxy_frame_based(OMX_HANDLETYPE hComp,
                       OMX_BUFFERHEADERTYPE *
                       buffer);

   OMX_ERRORTYPE empty_this_buffer_proxy(OMX_HANDLETYPE hComp,
                     OMX_BUFFERHEADERTYPE * buffer);

   OMX_ERRORTYPE
       empty_this_buffer_proxy_subframe_stitching(OMX_BUFFERHEADERTYPE *
                         buffer);
   bool find_new_frame_ap_vc1(OMX_IN OMX_U8 * buffer,
               OMX_IN OMX_U32 buffer_length,
               OMX_OUT OMX_BOOL & isNewFrame);

   OMX_ERRORTYPE fill_this_buffer_proxy(OMX_HANDLETYPE hComp,
                    OMX_BUFFERHEADERTYPE * buffer);
   bool release_done();

   bool release_output_done();

   bool release_input_done();

   OMX_ERRORTYPE send_command_proxy(OMX_HANDLETYPE hComp,
                OMX_COMMANDTYPE cmd,
                OMX_U32 param1, OMX_PTR cmdData);

   inline unsigned int get_output_buffer_size() {
       OMX_U32 buffer_size, chroma_height, chroma_width;
       if (m_color_format == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka) {
         buffer_size = (m_port_height * m_port_width + 4095) & ~4095;
         chroma_height = ((m_port_height >> 1) + 31) & ~31;
         chroma_width = 2 * (((m_port_width >> 1) + 31) & ~31);
         buffer_size += (chroma_height * chroma_width) + getExtraDataSize();
       } 
       else {
          buffer_size = m_port_height * m_port_width * 3/2  + getExtraDataSize();
       }
       return buffer_size;
   } inline void omx_vdec_set_use_buf_flg() {
      m_is_use_buffer = true;
   }
   inline void omx_vdec_reset_use_buf_flg() {
      m_is_use_buffer = false;
   }
   inline bool omx_vdec_get_use_buf_flg() {
      return m_is_use_buffer;
   }
   inline void omx_vdec_set_use_egl_buf_flg() {
      m_is_use_egl_buffer = true;
   }
   inline void omx_vdec_reset_use_elg_buf_flg() {
      m_is_use_egl_buffer = false;
   }
   inline bool omx_vdec_get_use_egl_buf_flg() {
      return m_is_use_egl_buffer;
   }
   inline void omx_vdec_set_input_use_buf_flg() {
      m_is_input_use_buffer = true;
   }
   inline void omx_vdec_reset_input_use_buf_flg() {
      m_is_input_use_buffer = false;
   }
   inline bool omx_vdec_get_input_use_buf_flg() {
      return m_is_input_use_buffer;
   }
   bool post_event(unsigned int p1, unsigned int p2, unsigned int id);

   static void buffer_done_cb_arbitrarybytes(struct vdec_context *ctxt,
                    void *cookie);

   // Native decoder creation
   OMX_ERRORTYPE omx_vdec_create_native_decoder(OMX_IN OMX_BUFFERHEADERTYPE
                       * buffer);
   // Free output port memory
   void omx_vdec_free_output_port_memory(void);
   OMX_BUFFERHEADERTYPE *get_free_input_buffer();
   OMX_S8 find_input_buffer_index(OMX_BUFFERHEADERTYPE * pBuffer);
   bool free_input_buffer(OMX_BUFFERHEADERTYPE * pBuffer);
   OMX_S8 get_free_extra_buffer_index();
   OMX_S8 find_extra_buffer_index(OMX_U8 * buffer);
   bool free_extra_buffer(OMX_S8 index);
   void initialize_arbitrary_bytes_environment();
   bool get_one_complete_frame(OMX_OUT OMX_BUFFERHEADERTYPE * dest);
   OMX_ERRORTYPE get_one_frame(OMX_OUT OMX_BUFFERHEADERTYPE * dest,
                OMX_IN OMX_BUFFERHEADERTYPE * source,
                OMX_OUT bool * isPartialNal);
   OMX_ERRORTYPE get_one_frame_using_start_code(OMX_OUT
                       OMX_BUFFERHEADERTYPE *
                       dest,
                       OMX_IN OMX_BUFFERHEADERTYPE
                       * source,
                       OMX_INOUT bool *
                       isPartialFrame);
   OMX_ERRORTYPE get_one_frame_h264_size_nal(OMX_OUT OMX_BUFFERHEADERTYPE *
                    dest,
                    OMX_IN OMX_BUFFERHEADERTYPE *
                    source,
                    OMX_INOUT bool *
                    isPartialFrame);
   OMX_ERRORTYPE get_one_frame_sp_mp_vc1(OMX_OUT OMX_BUFFERHEADERTYPE *
                     dest,
                     OMX_IN OMX_BUFFERHEADERTYPE *
                     source,
                     OMX_INOUT bool * isPartialFrame);
   OMX_ERRORTYPE use_egl_output_buffer(OMX_IN OMX_HANDLETYPE hComp,
                  OMX_INOUT OMX_BUFFERHEADERTYPE **
                  bufferHdr, OMX_IN OMX_U32 port,
                  OMX_IN OMX_PTR appData,
                  OMX_IN void *eglImage);

   void fill_extradata(OMX_INOUT OMX_BUFFERHEADERTYPE * pBufHdr,
             OMX_IN vdec_frame * frame);

   //*************************************************************
   //*******************MEMBER VARIABLES *************************
   //*************************************************************
   // OMX State
   OMX_STATETYPE m_state;
   // Application data
   OMX_PTR m_app_data;
   // Application callbacks
   OMX_CALLBACKTYPE m_cb;
   OMX_COLOR_FORMATTYPE m_color_format;
   OMX_PRIORITYMGMTTYPE m_priority_mgm;
   OMX_PARAM_BUFFERSUPPLIERTYPE m_buffer_supplier;
   OMX_BUFFERHEADERTYPE *m_pcurrent_frame;
   OMX_BUFFERHEADERTYPE *input[OMX_CORE_NUM_INPUT_BUFFERS];
   OMX_BUFFERHEADERTYPE *m_loc_use_buf_hdr;
    /*************************************************/
   // used by arbitrary bytes
   struct omx_start_code_based_info {
      OMX_U32 m_start_code;
      OMX_U32 m_start_code_mask;
      OMX_U32 m_last_4byte;
      OMX_U32 m_last_start_code;
   };

   struct omx_frame_size_based_info {
      OMX_U32 m_size_remaining;
      OMX_U32 m_size_byte_left;
      OMX_U32 m_timestamp_byte_left;
      OMX_U32 m_timestamp_field_present;
   };

   union omx_arbitrary_bytes_info {
      struct omx_start_code_based_info start_code;
      struct omx_frame_size_based_info frame_size;
   } m_arbitrary_bytes_info;

   OMX_BUFFERHEADERTYPE *m_current_frame;
   OMX_BUFFERHEADERTYPE *m_current_arbitrary_bytes_input;
   OMX_BUFFERHEADERTYPE
       *m_arbitrary_bytes_input[OMX_CORE_NUM_INPUT_BUFFERS];
   bool m_is_copy_truncated;

   struct omx_extra_input_buff_info {
      OMX_BUFFERHEADERTYPE *pArbitrary_bytes_freed;   // pointer to arbitrary byte input need to be freed
      bool bfree_input;
   };
   struct omx_extra_input_buff_info
       m_input_buff_info[OMX_CORE_NUM_INPUT_BUFFERS];
   struct omx_extra_arbitrarybytes_buff_info {
      OMX_U8 *extra_pBuffer;
      bool bExtra_pBuffer_in_use;
      OMX_BUFFERHEADERTYPE *arbitrarybytesInput;
   };
   struct omx_extra_arbitrarybytes_buff_info
       m_extra_buf_info[OMX_CORE_NUM_INPUT_BUFFERS];

   // flag for reaching partialFrame for arbitrary bytes
   bool m_bPartialFrame;

    /*************************************************/

   // video decoder context
   struct VDecoder *m_vdec;
   // fill this buffer queue
   omx_cmd_queue m_ftb_q;
   // Command Q for rest of the events
   omx_cmd_queue m_cmd_q;
   // empty this buffer queue for arbitrary bytes
   omx_cmd_queue m_etb_arbitrarybytes_q;
   // Input memory pointer
   char *m_inp_mem_ptr;
   // arbitrary bytes input memory pointer
   char *m_arbitrary_bytes_input_mem_ptr;
   // Output memory pointer
   char *m_out_mem_ptr;

   int m_first_pending_buf_idx;
   int m_outstanding_frames;
   // EOS Timestamp
   signed long long m_eos_timestamp;
   // bitmask array size for output side
   unsigned char m_out_bm_count[(OMX_CORE_NUM_OUTPUT_BUFFERS + 7) / 8];
   // Number of Output Buffers
   unsigned int m_out_buf_count;
   // Number of Input Buffers
   unsigned int m_inp_buf_count;
   // Size of Input Buffers
   unsigned int m_inp_buf_size;
   // bitmask array size for input side
   unsigned char m_inp_bm_count[(MAX_NUM_INPUT_BUFFERS + 7) / 8];
   //Input port Populated
   OMX_BOOL m_inp_bPopulated;
   //Output port Populated
   OMX_BOOL m_out_bPopulated;
   //Height
   unsigned int m_height;
   // Width
   unsigned int m_width;

   unsigned int m_dec_width;   // the decoder width with padding
   unsigned int m_dec_height;   // the decoder height with padding
   bool m_bInterlaced;   // Indicate whether the content has interlace

   // Storage of HxW during dynamic port reconfig
   unsigned int m_port_height;
   unsigned int m_port_width;
   unsigned int m_crop_x;
   unsigned int m_crop_y;
   unsigned int m_crop_dx;
   unsigned int m_crop_dy;
   // encapsulate the waiting states.
   unsigned char m_flags[4];
   // size of NAL length
   unsigned int m_nalu_bytes;
   genericQueue *flush_before_vdec_op_q;
   unsigned int m_use_pmem;

#ifdef _ANDROID_
   // Heap pointer to frame buffers
   sp < MemoryHeapBase > m_heap_ptr;
#endif //_ANDROID_
   unsigned char m_out_flags[(OMX_CORE_NUM_OUTPUT_BUFFERS + 7) / 8];
   // message count
   unsigned m_msg_cnt;
   // command count
   unsigned m_cmd_cnt;
   // Empty This Buffer count
   unsigned m_etb_cnt;
   // Empty Buffer Done Count
   unsigned m_ebd_cnt;
   // Fill This Buffer count
   unsigned m_ftb_cnt;
   // Fill Buffer done count
   unsigned m_fbd_cnt;
   // store I/P PORT state
   OMX_BOOL m_inp_bEnabled;
   // store O/P PORT state
   OMX_BOOL m_out_bEnabled;
   OMX_U8 m_cRole[OMX_MAX_STRINGNAME_SIZE];

   // default members for pre-defined system properties
   bool m_default_arbitrary_bytes;
   bool m_default_arbitrary_bytes_vc1;
   bool m_default_accumulate_subframe;

   // to know whether Event Port Settings change has been triggered or not.
   bool m_event_port_settings_sent;
   // is USE Buffer in use
   bool m_is_use_buffer;
   bool m_is_input_use_buffer;
   bool m_is_use_egl_buffer;
   bool m_first_sync_frame_rcvd;
   //bool                  m_is_use_pmem_buffer;
   // EOS notify pending to the IL client
   bool m_bEoSNotifyPending;
   // NAL stitching required
   bool m_bAccumulate_subframe;
   // flag if input is arbitrary bytes
   bool m_bArbitraryBytes;
   // flag to indicate if the buffer use start code
   bool m_bStartCode;

   // wait for resource, (buffer done from Q6 to free buffers)
   bool m_bWaitForResource;

   use_buffer_map m_use_buf_hdrs;

   H264_Utils *m_h264_utils;

   // Input frame details
   struct video_input_frame_info m_frame_info;
   // Platform specific details
   OMX_QCOM_PLATFORM_PRIVATE_LIST *m_platform_list;
   OMX_QCOM_PLATFORM_PRIVATE_ENTRY *m_platform_entry;
   OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *m_pmem_info;
   // SPS+PPS sent as part of set_config
   OMX_VENDOR_EXTRADATATYPE m_vendor_config;
   header_state m_header_state;
   bool m_bInvalidState;
    bool                                m_b_divX_parser;
    MP4_Utils                           *m_mp4_utils;
    uint64                              m_timestamp_interval;
    uint64                              m_prev_timestamp;
    bool                                m_b_display_order;
    struct vdec_frame                   *m_pPrevFrame;

    typedef struct
    {
      mp4_frame_info_type frame_info[MAX_FRAMES_IN_CHUNK];
      uint32              nFrames;
      uint32              last_decoded_index;
      bool                parsing_required;
    } omx_mp4_divX_buffer_info;

    omx_mp4_divX_buffer_info            m_divX_buffer_info;
    uint32 m_codec_format;
    uint32 m_codec_profile;
    OMX_NATIVE_WINDOWTYPE m_display_id;
};

#endif // __OMX_VDEC_H__
