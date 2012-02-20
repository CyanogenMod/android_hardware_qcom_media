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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef T_WINNT
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#else
#include "qdspext.h"
#include "qdsprtos.h"
#define QDSP_mpuVDecCmdQueue              4
#define QDSP_mpuVDecPktQueue              5
#endif //T_WINNT
#include <errno.h>
#include <pthread.h>

#include "adsp.h"
#include "qtv_msg.h"
#include "qutility.h"

#if PROFILE_DECODER
QPERF_INIT(dsp_decode);
#endif

#define DEBUG 0         // TEST

#if DEBUG         // TEST
static pthread_mutex_t logMTX;
static FILE *logFD = NULL;
static const char *logFN = "/data/adsp_log.txt";
#endif

struct adsp_module {
   int fd;
   volatile int dead;
   volatile int init_done;

   void *ctxt;
   adsp_msg_frame_done_func frame_done;
   adsp_msg_buffer_done_func buffer_done;

   pthread_t thr;
};

#ifndef T_WINNT
void convertFrameInfoToFrameDetails(struct Vdec_FrameDetailsType *Frame_details,
                struct vdec_frame_info *pFrame)
{
   int i;
   if (NULL == Frame_details || NULL == pFrame) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "convertFrameInfoToFrameDetails() Frame Details or vdec-frame NULL "
               "Frame Details: 0x%x, pFrame: 0x%x\n",
               Frame_details, pFrame);
   }
   Frame_details->status = (Vdec_StatusType) pFrame->status;   //TBD
   Frame_details->userData1 = pFrame->data1;
   Frame_details->userData2 = pFrame->data2;
   Frame_details->timestamp =
       (unsigned long long)((unsigned long long)pFrame->
             timestamp_hi << 32 | (unsigned long long)
             pFrame->timestamp_lo & 0x0FFFFFFFFLL);
   Frame_details->calculatedTimeStamp =
       (unsigned long long)((unsigned long long)pFrame->
             cal_timestamp_hi << 32 | (unsigned long long)
             pFrame->cal_timestamp_lo & 0x0FFFFFFFFLL);
   Frame_details->nDecPicWidth = pFrame->dec_width;
   Frame_details->nDecPicHeight = pFrame->dec_height;
   Frame_details->cwin.x1 = pFrame->cwin.x1;
   Frame_details->cwin.y1 = pFrame->cwin.y1;
   Frame_details->cwin.x2 = pFrame->cwin.x2;
   Frame_details->cwin.y2 = pFrame->cwin.y2;
   for (i = 0; i < MAX_FIELDS; i++) {
      Frame_details->ePicType[i] =
          (Vdec_PictureType) pFrame->picture_type[i];
   }
   Frame_details->ePicFormat = (Vdec_PictureFormat) pFrame->picture_format;
   Frame_details->nVC1RangeY = pFrame->vc1_rangeY;
   Frame_details->nVC1RangeUV = pFrame->vc1_rangeUV;
   Frame_details->ePicResolution =
       (Vdec_PictureRes) pFrame->picture_resolution;
   Frame_details->nRepeatProgFrames = pFrame->frame_disp_repeat;
   Frame_details->bRepeatFirstField = pFrame->repeat_first_field;
   Frame_details->bTopFieldFirst = pFrame->top_field_first;
   Frame_details->bFrameInterpFlag = pFrame->interframe_interp;
   Frame_details->panScan.numWindows = pFrame->panscan.num;
   for (i = 0; i < MAX_VC1_PAN_SCAN_WINDOWS; i++) {
      Frame_details->panScan.winHeight[i] = pFrame->panscan.width[i];
      Frame_details->panScan.winHeight[i] = pFrame->panscan.height[i];
      Frame_details->panScan.winHorOffset[i] =
          pFrame->panscan.xoffset[i];
      Frame_details->panScan.winVerOffset[i] =
          pFrame->panscan.yoffset[i];
   }

   Frame_details->nPercentConcealedMacroblocks =
       pFrame->concealed_macblk_num;
   Frame_details->flags = pFrame->flags;
   Frame_details->performanceStats = pFrame->performance_stats;
}
void *adsp_thread(void *_mod)
{
   struct adsp_module *mod = _mod;
   int n;
   struct vdec_msg vdecMsg;
   struct Vdec_FrameDetailsType vdec_frame;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "adsp_thread: event thread start\n");
   while (!mod->dead) {
      if (!mod->init_done)
         continue;

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "adsp_thread: Calling IOCTL_GETMSG fd %d\n",
               mod->fd);
      if (ioctl(mod->fd, VDEC_IOCTL_GETMSG, &vdecMsg) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                 "adsp_thread:VDEC_IOCTL_GETMSG failed\n");
         continue;
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "adsp_thread: %d\n", vdecMsg.id);
      if (vdecMsg.id == VDEC_MSG_REUSEINPUTBUFFER) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "adsp_thread: Reuse input buffer %d\n",
                  vdecMsg.buf_id);
         mod->buffer_done(mod->ctxt, (void *)vdecMsg.buf_id);
      } else if (vdecMsg.id == VDEC_MSG_FRAMEDONE) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "adsp_thread: Frame done %x\n",
                  vdecMsg.vfr_info);
         convertFrameInfoToFrameDetails(&vdec_frame,
                         &vdecMsg.vfr_info);
#if PROFILE_DECODER
         if (vdec_frame.status != VDEC_FLUSH_DONE) {
            QPERF_END_AND_START(dsp_decode);
         }
#endif
         mod->frame_done(mod->ctxt, &vdec_frame,
               vdecMsg.vfr_info.data2,
               vdecMsg.vfr_info.offset);
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "adsp_thread:VDEC_IOCTL_GETMSG Unknown Msg ID\n");
      }
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "h264: event thread stop\n");
   return 0;
}
#endif

void adsp_close(struct adsp_module *mod)
{
#if PROFILE_DECODER
    QPERF_TERMINATE(dsp_decode);
    QPERF_RESET(dsp_decode);
#endif
#ifndef T_WINNT
   int ret;
   int thread_ret = 0;
   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "adsp_close() mod NULL: 0x%x\n", mod);
      return ;
   }

   mod->dead = 1;

   if (ioctl(mod->fd, VDEC_IOCTL_CLOSE, NULL) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_CLOSE failed\n");
      return;
   }

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "adsp_close: 0x%x",
            (unsigned)mod);

   /*Wait on the adsp event thread completion */

   ret = pthread_join(mod->thr, (void **)&thread_ret);

   if (ret != 0) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "*************adsp_close: Could not join on the adsp event thread err=%d!!\n",
               ret);
   }

   /*Wait on the adsp event thread completion */

   ret = close(mod->fd);
   if (ret < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "*************adsp_close ERROR!");
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "adsp_close returned %d, fd: %d", ret, mod->fd);

#if DEBUG
   if (logFD) {
      pthread_mutex_lock(&logMTX);
      fclose(logFD);
      logFD = NULL;
      pthread_mutex_destroy(&logMTX);
   }
#endif

   //sleep(1); /* XXX need better way to stop thread XXX */
   free(mod);
   mod = NULL;
#endif //T_WINNT
}

struct adsp_module *adsp_open(const char *name, struct adsp_open_info info,
               void *context, int32 vdec_fd)
{

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "adsp_open: %s", name);
   int fds[2], r;
   struct adsp_module *mod;

   mod = calloc(1, sizeof(*mod));
   if (!mod)
      return 0;

   mod->ctxt = context;
   mod->frame_done = info.frame_done;
   mod->buffer_done = info.buffer_done;

#ifndef T_WINNT
   mod->dead = 0;
   mod->init_done = 0;
   r = pthread_create(&mod->thr, 0, adsp_thread, mod);
   if (r < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error - unable to create adsp_thread\n");
      goto fail_thread;
   }

   mod->fd = vdec_fd;
   if(mod->fd < 0) {
      mod->fd = open("/dev/vdec", O_RDWR);
      if (mod->fd < 0) {
         QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_FATAL,
                  "adsp: cannot open '%s', fd: %d (%s)\n", name,
                  mod->fd, strerror(errno));
         goto fail_open;
      }
   }
#if DEBUG
   if (pthread_mutex_init(&logMTX, NULL) == 0) {
      if (pthread_mutex_lock(&logMTX) == 0) {
         if (!logFD) {
            logFD = fopen(logFN, "a");
         }
         if (logFD) {
            fprintf(logFD, "\n");
            pthread_mutex_unlock(&logMTX);
         }
      }
      if (!logFD) {
         pthread_mutex_destroy(&logMTX);
      }
   }
   if (!logFD) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "adsp: unable to log adsp writes\n");
   }
#endif

#endif

   return mod;
      fail_open:
   mod->dead = 1;
      fail_thread:
   free(mod);
   return 0;
}

int adsp_set_buffers(struct adsp_module *mod, struct adsp_buffer_info bufinfo)
{
   struct vdec_buffer mem;
   int r;

   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_set_buffers() mod NULL: 0x%x\n", mod);
      return -1;
   }

   mem.pmem_id = bufinfo.buf.pmem_id;
   mem.buf.buf_type = bufinfo.buf_type;
   mem.buf.num_buf = bufinfo.numbuf;
   mem.buf.islast = bufinfo.is_last;

   mem.buf.region.src_id = 0x0106e429;
   mem.buf.region.offset = bufinfo.buf.offset;
   mem.buf.region.size = bufinfo.buf.size;

   if (ioctl(mod->fd, VDEC_IOCTL_SETBUFFERS, &mem) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_SETBUFFERS failed\n");
      mod->dead = 1;
      return -1;
   }

   mod->init_done = 1;

   return 0;

}

int adsp_free_buffers(struct adsp_module *mod, struct adsp_buffer_info bufinfo)
{
   struct vdec_buffer mem;
   int r;

   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_set_buffers() mod NULL: 0x%x\n", mod);
      return -1;
   }

   mem.pmem_id = bufinfo.buf.pmem_id;
   mem.buf.buf_type = bufinfo.buf_type;
   mem.buf.num_buf = bufinfo.numbuf;
   mem.buf.islast = bufinfo.is_last;

   mem.buf.region.src_id = 0x0106e429;
   mem.buf.region.offset = bufinfo.buf.offset;
   mem.buf.region.size = bufinfo.buf.size;

   if (ioctl(mod->fd, VDEC_IOCTL_FREEBUFFERS, &mem) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_SETBUFFERS failed\n");
      return -1;
   }

   return 0;

}

int adsp_setproperty(struct adsp_module *mod, struct vdec_property_info *property)
{
//   LOGE("Setting priority");
   int ret = ioctl(mod->fd, VDEC_IOCTL_SETPROPERTY, property);
   if(ret) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_FATAL, "Setting priority failed");
   }
   return ret;
}
int adsp_init(struct adsp_module *mod, struct adsp_init *init)
{
   struct vdec_init vi;
   struct vdec_buf_req buf;
   struct vdec_version ver;
   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_init() mod NULL: 0x%x\n", mod);
      return -1;
   }

   /* Get the driver version */
   if (ioctl(mod->fd, VDEC_IOCTL_GETVERSION, &ver) < 0) {
     QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_GETVERSION failed setting to default version\n");
     ver.major = 1;
     ver.minor = 0;
   }

   vi.sps_cfg.cfg.width = init->width;
   vi.sps_cfg.cfg.height = init->height;
   vi.sps_cfg.cfg.order = init->order;
   vi.sps_cfg.cfg.notify_enable = init->notify_enable;
   vi.sps_cfg.cfg.fourcc = init->fourcc;
   vi.sps_cfg.cfg.vc1_rowbase = init->vc1_rowbase;
   vi.sps_cfg.cfg.h264_startcode_detect = init->h264_startcode_detect;
   vi.sps_cfg.cfg.h264_nal_len_size = init->h264_nal_len_size;
   vi.sps_cfg.cfg.postproc_flag = init->postproc_flag;
   vi.sps_cfg.cfg.fruc_enable = init->fruc_enable;
   vi.sps_cfg.cfg.color_format = init->color_format;
   vi.sps_cfg.seq.header = init->seq_header;
   vi.sps_cfg.seq.len = init->seq_len;
   vi.buf_req = &buf;

   /* set the color format based on version */
   if (ver.major < 2 && init->color_format != 0) {
     QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_INITIALIZE wrong value for reserved field\n");
     vi.sps_cfg.cfg.color_format = 0;
   }

   if (ioctl(mod->fd, VDEC_IOCTL_INITIALIZE, &vi) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "VDEC_IOCTL_INITIALIZE failed\n");
      mod->dead = 1;
      return -1;
   }
   init->buf_req->input.bufnum_min = vi.buf_req->input.num_min_buffers;
   init->buf_req->input.bufnum_max = vi.buf_req->input.num_max_buffers;
   init->buf_req->input.bufsize = vi.buf_req->input.bufsize;
   init->buf_req->output.bufnum_min = vi.buf_req->output.num_min_buffers;
   init->buf_req->output.bufnum_max = vi.buf_req->output.num_max_buffers;
   init->buf_req->output.bufsize = vi.buf_req->output.bufsize;
   init->buf_req->dec_req1.bufnum_min =
       vi.buf_req->dec_req1.num_min_buffers;
   init->buf_req->dec_req1.bufnum_max =
       vi.buf_req->dec_req1.num_max_buffers;
   init->buf_req->dec_req1.bufsize = vi.buf_req->dec_req1.bufsize;
   init->buf_req->dec_req2.bufnum_min =
       vi.buf_req->dec_req2.num_min_buffers;
   init->buf_req->dec_req2.bufnum_max =
       vi.buf_req->dec_req2.num_max_buffers;
   init->buf_req->dec_req2.bufsize = vi.buf_req->dec_req2.bufsize;

   return 0;

}
int adsp_post_input_buffer(struct adsp_module *mod, struct adsp_input_buf input,
            unsigned int eos)
{
   struct vdec_input_buf ip;
   struct vdec_queue_status stat;

   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_post_input_buffer() mod NULL: 0x%x\n", mod);
      return -1;
   }
   ip.pmem_id = input.pmem_id;
   ip.buffer.avsync_state = input.avsync_state;
   ip.buffer.data = input.data;
   ip.buffer.offset = input.offset;
   ip.buffer.size = input.size;
   ip.buffer.timestamp_lo = input.timestamp_lo;
   ip.buffer.timestamp_hi = input.timestamp_hi;
   ip.buffer.flags = input.flags;
   ip.queue_status = &stat;

#if PROFILE_DECODER
   QPERF_START(dsp_decode);
#endif
   if (eos) {
      if (ioctl(mod->fd, VDEC_IOCTL_EOS, NULL) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "adsp:VDEC_IOCTL_EOS failed\n");
         return -1;
      }
   } else {
      //usleep(1000000);
      if (ioctl(mod->fd, VDEC_IOCTL_QUEUE, &ip) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "adsp:VDEC_IOCTL_QUEUE failed\n");
         return -1;
      }
   }

   return 0;
}
int adsp_release_frame(struct adsp_module *mod, unsigned int *buf)
{
   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_release_frame() mod NULL: 0x%x\n", mod);
      return -1;
   }
   return ioctl(mod->fd, VDEC_IOCTL_REUSEFRAMEBUFFER, buf);

}
int adsp_flush(struct adsp_module *mod, unsigned int port)
{
   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_flush() mod NULL: 0x%x\n", mod);
      return -1;
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "adsp_flush() Before Flush \n");
   return ioctl(mod->fd, VDEC_IOCTL_FLUSH, &port);
}
int adsp_performance_change_request(struct adsp_module *mod, unsigned int request_type)
{
   if (NULL == mod) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "adsp_performance_change_request() mod NULL: 0x%x\n", mod);
      return -1;
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "adsp_performance_change_request()\n");
   return ioctl(mod->fd, VDEC_IOCTL_PERFORMANCE_CHANGE_REQ, &request_type);
}
