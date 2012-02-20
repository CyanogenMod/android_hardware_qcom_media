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
#ifndef _ADSP_SIMPLE_H_
#define _ADSP_SIMPLE_H_

#include <linux/msm_q6vdec.h>

#ifdef __cplusplus
extern "C" {
#endif            /* __cplusplus */
#include "vdec.h"
   enum {
      ADSP_BUFFER_TYPE_INPUT,
      ADSP_BUFFER_TYPE_OUTPUT,
      ADSP_BUFFER_TYPE_INTERNAL1,
      ADSP_BUFFER_TYPE_INTERNAL2,
   };

   enum {
      ADSP_COLOR_FORMAT_NV21 = 0x01,
      ADSP_COLOR_FORMAT_NV21_YAMATO = 0x02,
   };

   struct adsp_module;
   struct adsp_pmem_info;
   struct adsp_buffers {
      unsigned int pmem_id;
      unsigned int offset;
      unsigned int size;
   };

   struct adsp_buffer_info {
      unsigned int buf_type;
      struct adsp_buffers buf;
      unsigned int numbuf;
      unsigned int is_last;
   };
   struct adsp_buf_data {
      unsigned int bufnum_min;
      unsigned int bufnum_max;
      unsigned int bufsize;
   };
   struct adsp_buf_req {
      unsigned int maxnum_input_buf;
      struct adsp_buf_data input;
      struct adsp_buf_data output;
      struct adsp_buf_data dec_req1;
      struct adsp_buf_data dec_req2;
   };
   struct adsp_init {
      unsigned int seq_len;
      unsigned int width;
      unsigned int height;
      unsigned int order;
      unsigned int notify_enable;
      unsigned int fourcc;
      unsigned int vc1_rowbase;
      unsigned int h264_startcode_detect;
      unsigned int h264_nal_len_size;
      unsigned int postproc_flag;
      unsigned int fruc_enable;
      unsigned int color_format;
      unsigned char *seq_header;
      struct adsp_buf_req *buf_req;
   };
   struct adsp_input_buf {
      unsigned int pmem_id;
      unsigned int offset;
      unsigned int data;
      unsigned int size;
      int timestamp_lo;
      int timestamp_hi;
      int avsync_state;
      unsigned int flags;
   };

   typedef void (*adsp_msg_frame_done_func) (void *context,
                    Vdec_FrameDetailsType *
                    pframe,
                    unsigned int timestamp,
                    unsigned int offset);

   typedef void (*adsp_msg_buffer_done_func) (void *context,
                     void *buffer_id);

   struct adsp_open_info {
      adsp_msg_frame_done_func frame_done;
      adsp_msg_buffer_done_func buffer_done;
   };

   struct adsp_dec_attr {
      unsigned int fourcc;
      unsigned int profile;
      unsigned int level;
      unsigned int dec_pic_width;
      unsigned int dec_pic_height;
      struct adsp_buf_data input;
      struct adsp_buf_data output;
      struct adsp_buf_data dec_req1;
      struct adsp_buf_data dec_req2;
   };

   struct adsp_module *adsp_open(const char *name,
                  struct adsp_open_info info,
                  void *context, int32 vdec_fd);

   void adsp_close(struct adsp_module *mod);
   int adsp_set_buffers(struct adsp_module *mod,
              struct adsp_buffer_info bufinfo);
   int adsp_setproperty(struct adsp_module *mod, struct vdec_property_info *property);
   int adsp_init(struct adsp_module *mod, struct adsp_init *init);
   int adsp_post_input_buffer(struct adsp_module *mod,
               struct adsp_input_buf input,
               unsigned int eos);
   int adsp_release_frame(struct adsp_module *mod, unsigned int *buf);
   int adsp_flush(struct adsp_module *mod, unsigned int port);
   int adsp_free_buffers(struct adsp_module *mod,
               struct adsp_buffer_info bufinfo);
   int adsp_get_dec_attr(struct adsp_module *mod,
               struct adsp_dec_attr *attr);
   int adsp_performance_change_request(struct adsp_module *mod, unsigned int);

#ifdef __cplusplus
}
#endif            /* __cplusplus */
#endif
