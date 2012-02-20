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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "semaphore.h"
#include "OMX_QCOMExtns.h"

extern "C" {
#include "pmem.h"
#include <linux/msm_q6vdec.h>
#include "adsp.h"
}
#include "qtv_msg.h"
#include "qutility.h"
#include "vdec.h"
#ifdef T_WINNT
#define LOG_YUV_FRAMES 1
#define LOG_INPUT_BUFFERS 1
#else
#define LOG_YUV_FRAMES 0
#define LOG_INPUT_BUFFERS 0
#endif

#define DEBUG_ON 0
#define Q6_VDEC_PAGE_SIZE  0x1000
#define Q6_VDEC_PAGE_MASK  (~(Q6_VDEC_PAGE_SIZE-1))
#define Q6_VDEC_PAGE_ALIGN(addr)  (((addr) + Q6_VDEC_PAGE_SIZE - 1) & Q6_VDEC_PAGE_MASK)
#define  MAKEFOURCC(ch0,ch1,ch2,ch3) ((uint32)(uint8)(ch0) | ((uint32)(uint8)(ch1) << 8) |   ((uint32)(uint8)(ch2) << 16) | ((uint32)(uint8)(ch3) << 24 ))
static void vdec_frame_cb_handler(void *vdec_context,
              struct Vdec_FrameDetailsType *pFrame);
static void vdec_reuse_input_cb_handler(void *vdec_context, void *buffer_id);

#define VDEC_INPUT_BUFFER_SIZE  450 * 1024
#define VDEC_NUM_INPUT_BUFFERS  8
#define VDEC_NUM_INPUT_BUFFERS_THUMBNAIL_MODE  2
#define VDEC_MAX_SEQ_HEADER_SIZE 300

struct Vdec_pthread_info {
   pthread_mutex_t in_buf_lock;
   pthread_mutex_t out_buf_lock;
   sem_t flush_sem;
} Vdec_pthread_info;

QPERF_INIT(arm_decode);

#if LOG_YUV_FRAMES
FILE *pYUVFile=NULL;
#endif /* LOG_YUV_FRAMES */

#if LOG_INPUT_BUFFERS
FILE *pInputFile=NULL;
static int counter = 0;
#endif /* LOG_INPUT_BUFFERS */

int timestamp = 0;

int getExtraDataSize()
{
   int extraSize =
      (((OMX_EXTRADATA_HEADER_SIZE + sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO)+3) & (~3)) +
       ((OMX_EXTRADATA_HEADER_SIZE+sizeof(OMX_QCOM_EXTRADATA_CODEC_DATA)+3) & (~3)) +
       ((OMX_EXTRADATA_HEADER_SIZE+sizeof(OMX_QCOM_EXTRADATA_FRAMEDIMENSION)+3) & (~3))+
       ((OMX_EXTRADATA_HEADER_SIZE+sizeof(OMX_STREAMINTERLACEFORMAT)+3) & (~3))+
       (OMX_EXTRADATA_HEADER_SIZE + 4));
   return extraSize;
}

void vdec_frame_cb_handler(void *vdec_context,
            struct Vdec_FrameDetailsType *pFrame,
            unsigned int fd, unsigned int offset)
{
   int index;
   struct VDecoder *dec;
   static unsigned int nFrameDoneCnt = 0;
   static unsigned int nGoodFrameCnt = 0;
   struct Vdec_pthread_info *pthread_info;

   dec = (struct VDecoder *)vdec_context;

   if (NULL == pFrame || dec == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: pFrame or dec parameter is NULL, dropping frame\n");
      return;
   }

   if (NULL == dec->ctxt || NULL == dec->ctxt->outputBuffer
       || NULL == dec->ctxt->frame_done) {
      QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "vdec: dec_output or context corrupted dec_output %x, ctxt %x, "
               "frame_done %x, dropping frame\n", dec->ctxt,
               dec->ctxt->outputBuffer, dec->ctxt->frame_done);
      return;
   }

   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;
   ++nFrameDoneCnt;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec: frame done cb cnt: %d", nFrameDoneCnt);
   switch (pFrame->status) {
   case VDEC_FRAME_DECODE_SUCCESS:
      {

         //Iterate through the output frame array to locate corresponding index
         for (index = 0; index < dec->ctxt->numOutputBuffers;
              index++) {
            if (fd ==
                dec->ctxt->outputBuffer[index].buffer.
                pmem_id
                && offset ==
                dec->ctxt->outputBuffer[index].buffer.
                pmem_offset) {
               break;
            }
         }
         if (dec->ctxt->numOutputBuffers == index) {
            QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "vdec: error: unable to map offset to address %x, and fd %d dropping frame\n",
                     offset, fd);
            return;
         } else {
            ++nGoodFrameCnt;
            /* we dont need any other flags at this momment for a successfully decoded
             * frame
             */
            dec->ctxt->outputBuffer[index].flags = (SEI_TRIGGER_BIT_QDSP & (pFrame -> flags));
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                     "vdec: callback status good frame, cnt: %d\n",
                     nGoodFrameCnt);

            if(pFrame ->nPercentConcealedMacroblocks > 0)
               QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "***nPercentConcealedMacroblocks %d",pFrame ->nPercentConcealedMacroblocks);

#if LOG_YUV_FRAMES
            if (pYUVFile) {
                  int size=dec->ctxt->width *  dec->ctxt->height;
    
                  if (dec->ctxt->color_format == QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka) 
                  {  
                     size = ( size + 4095) & ~4095;
                     size += 2* (((dec->ctxt->width >> 1) + 31) & ~31)*(((dec->ctxt->height >> 1) + 31) & ~31);
                  }
                  else
                     size = size * 1.5;

                  fwritex(dec->ctxt->outputBuffer[index].buffer.base, size, pYUVFile);
             }
#endif
            memcpy(&dec->ctxt->outputBuffer[index].
                   frameDetails, pFrame,
                   sizeof(struct Vdec_FrameDetailsType));
            dec->ctxt->outputBuffer[index].timestamp =
                pFrame->timestamp;
            pthread_mutex_lock(&pthread_info->out_buf_lock);
            dec->ctxt->outputBuffer[index].buffer.state =
                VDEC_BUFFER_WITH_APP;
            pthread_mutex_unlock(&pthread_info->
                       out_buf_lock);

            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "****** vdec: got the frame_done, call the frame done callback %x\n",
                     pFrame->userData1);
            dec->ctxt->frame_done(dec->ctxt,
                        &dec->ctxt->
                        outputBuffer[index]);
         }
         break;
      }

   case VDEC_FLUSH_DONE:
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "vdec: callback status flush Done\n");
         if (-1 == sem_post(&pthread_info->flush_sem)) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "[readframe] - sem_post failed %d\n",
                     errno);;

      /* There is to know the last frame sent by
       * DSP for every flush, we need this for
       * the divx time stamp swap logic, also DSP send EOS
       * status but it does it in a erratic fashion.
       */
           static struct vdec_frame vdecFrame;
           memset(&vdecFrame, 0, sizeof(vdecFrame));
           vdecFrame.flags |= FRAME_FLAG_EOS;
           dec->ctxt->frame_done(dec->ctxt, &vdecFrame);

           QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                   "vdec: Fake frame EOS for flush done.\n");

         }
         break;
      }

   case VDEC_FRAME_DECODE_ERROR:
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: callback status decode error\n");
         break;
      }

   case VDEC_FATAL_ERROR:
      {
         static struct vdec_frame vdecFrame;
         memset(&vdecFrame, 0, sizeof(vdecFrame));
         vdecFrame.flags |= FRAME_FATAL_ERROR;
         dec->ctxt->frame_done(dec->ctxt, &vdecFrame);

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_FATAL,
                 "vdec: callback status error fatal\n");
         break;
      }

   case VDEC_END_OF_STREAM:
      {
         static struct vdec_frame vdecFrame;
         memset(&vdecFrame, 0, sizeof(vdecFrame));
         vdecFrame.flags |= FRAME_FLAG_EOS;
         dec->ctxt->frame_done(dec->ctxt, &vdecFrame);

         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                 "vdec: callback status EOS\n");
         break;
      }

   default:
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: callback status unknown status\n");
      break;
   }

   return;

}
void vdec_reuse_input_cb_handler(void *vdec_context, void *buffer_id)
{
   struct VDecoder *dec;
   struct Vdec_pthread_info *pthread_info;
   dec = (struct VDecoder *)vdec_context;
   if (NULL == dec || NULL == dec->ctxt || NULL == dec->ctxt->buffer_done) {
      QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "vdec: dec_output or context corrupted dec_output %x, ctxt %x, "
               "buffer_done %x, dropping reuse input buffer\n",
               dec, dec->ctxt, dec->ctxt->buffer_done);
      return;
   }
   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;
   for (int i = 0; i < dec->ctxt->numInputBuffers; i++) {
      if (dec->ctxt->inputBuffer[i].omx_cookie == buffer_id) {
         pthread_mutex_lock(&pthread_info->in_buf_lock);
         dec->ctxt->inputBuffer[i].state =
             VDEC_BUFFER_WITH_VDEC_CORE;
         pthread_mutex_unlock(&pthread_info->in_buf_lock);
         dec->ctxt->buffer_done(dec->ctxt,
                      dec->ctxt->inputBuffer[i].
                      omx_cookie);
         break;
      }

   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "vdec: got reuse input buffer\n");
}

Vdec_ReturnType vdec_close(struct VDecoder *dec)
{
   unsigned int i = 0;
   struct Vdec_pthread_info *pthread_info;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "vdec: vdec_close()\n");

   if (dec == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: dec parameter is NULL, bailing out\n");
      return VDEC_EFAILED;
   }

   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;

#if LOG_YUV_FRAMES
   if (pYUVFile) {
      fclose(pYUVFile);
      pYUVFile = NULL;
   }
#endif
#if LOG_INPUT_BUFFERS
   if (pInputFile) {
      fclose(pInputFile);
      pInputFile=NULL;
   }
#endif
   dec->is_commit_memory = 0;
   adsp_close((struct adsp_module *)dec->adsp_module);
   free(dec->ctxt->inputBuffer);
   free(dec->ctxt->outputBuffer);
   dec->ctxt->outputBuffer =NULL;
   dec->ctxt->inputBuffer =NULL;
   if (-1 == sem_destroy(&pthread_info->flush_sem)) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "[vdec_close] - sem_destroy failed %d\n", errno);
   }
   pthread_mutex_destroy(&pthread_info->in_buf_lock);
   pthread_mutex_destroy(&pthread_info->out_buf_lock);
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "vdec: Free pmem\n");
   for (i = 0; i < dec->pmem_buffers; i++) {
      pmem_free(&dec->arena[i]);
   }
   free(dec->arena);
   free(dec->thread_specific_info);
   free(dec);

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "vdec: closed\n");
   return VDEC_SUCCESS;
}

Vdec_ReturnType vdec_get_input_buf_requirements(struct VDecoder_buf_info *
                  buf_req, int mode)
{
   if (NULL == buf_req) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "vdec_get_input_buf_requirements: Null parameter\n");
      return VDEC_EFAILED;
   }

   buf_req->buffer_size = VDEC_INPUT_BUFFER_SIZE;
   if(mode == FLAG_THUMBNAIL_MODE) {
      buf_req->numbuf = VDEC_NUM_INPUT_BUFFERS_THUMBNAIL_MODE;
   } else {
      buf_req->numbuf = VDEC_NUM_INPUT_BUFFERS;
   }
   return VDEC_SUCCESS;
}

Vdec_ReturnType vdec_allocate_input_buffer(unsigned int size,
                  Vdec_BufferInfo * buf, int is_pmem)
{
   int bufin_size, bufout_size, in_offset, out_offset, total_size;
   unsigned off;
   byte *ipBuffer;
   int n;
   int page_size = sysconf(_SC_PAGESIZE);
   if (buf == NULL || size <= 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Invalid argument allocate buffer\n");
      return VDEC_EFAILED;
   }
   if (is_pmem) {
      struct pmem pmem_data;

      pmem_data.fd = -1;
      pmem_data.size = (size + page_size - 1) & (~(page_size - 1));;

      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "Allocating input buffer from pmem\n");
      if (-1 == pmem_alloc(&pmem_data, pmem_data.size)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "pmem allocation failed\n");
         return VDEC_EFAILED;
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "Allocated input buffer from pmem size %d\n",
               pmem_data.size);

      buf->base = (byte *) pmem_data.data;
      buf->pmem_id = pmem_data.fd;
      buf->bufferSize = pmem_data.size;
      buf->pmem_offset = 0;
      buf->state = VDEC_BUFFER_WITH_APP;
   } else {
      byte *data = NULL;
      data = (byte *) malloc(size * sizeof(byte));
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "Allocating input buffer from heap %x\n",data );
      if (data == NULL) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "heap allocation failed\n");
         return VDEC_EFAILED;
      }
      buf->base = data;
      buf->bufferSize = size;
      buf->state = VDEC_BUFFER_WITH_APP;

   }
   return VDEC_SUCCESS;
}

Vdec_ReturnType vdec_free_input_buffer(Vdec_BufferInfo * buf_info, int is_pmem)
{
   int i;
   if (NULL == buf_info || buf_info->base == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Invalid buffer to free\n");
      return VDEC_EFAILED;
   }
   if (is_pmem) {
      struct pmem pmem_data;
      pmem_data.data = buf_info->base;
      pmem_data.fd = buf_info->pmem_id;
      pmem_data.size = buf_info->bufferSize;
      pmem_free(&pmem_data);
   } else {
      free(buf_info->base);
   }
   return VDEC_SUCCESS;

}

Vdec_ReturnType vdec_commit_memory(struct VDecoder * dec)
{
   unsigned off;
   int n;
   int r;
   int pmemid, pmembuf_cnt;
   void *pmem_buf;
   int bufnum, bufsize, total_out_size = 0, extraSize = 0, total_set_buffers =
       0;
   int bufin_size, bufout_size = 0, bufdec1_size, bufdec2_size, in_offset,
       out_offset, dec1_offset, total_size, dec2_offset;
   struct adsp_buffer_info adsp_buf_info;
   struct pmem arena;

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec commit memory 0x%x\n", dec);

   int page_size = sysconf(_SC_PAGESIZE);

   if (dec->ctxt->inputBuffer) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "vdec commit memory OMC allocated input buffer\n");
      dec->ctxt->outputBuffer =
          (struct vdec_frame *)malloc(sizeof(struct vdec_frame) *
                  dec->ctxt->outputReq.
                  numMinBuffers);

      if (NULL == dec->ctxt->outputBuffer) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to allocate output buffers\n");
         return VDEC_EFAILED;
      }

      dec->pmem_buffers = 1;

      dec->arena =
          (struct pmem *)malloc(sizeof(struct pmem) *
                 dec->pmem_buffers);
      if (NULL == dec->arena) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to allocate output buffers\n");
         free(dec->ctxt->outputBuffer);
         return VDEC_EFAILED;
      }

      pmembuf_cnt = 0;

      extraSize = getExtraDataSize();

      dec->ctxt->nOutBufAllocLen =
          dec->ctxt->outputReq.bufferSize + extraSize;
      //dec->ctxt->nOutBufAllocLen = dec->ctxt->outputReq.bufferSize ;
      bufout_size =
          Q6_VDEC_PAGE_ALIGN(dec->ctxt->outputReq.bufferSize +
                   extraSize);
      total_out_size =
          Q6_VDEC_PAGE_ALIGN(bufout_size *
                   dec->ctxt->outputReq.numMinBuffers);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "vdec: Commit: out buf size %d and tot out size %d\n",  bufout_size, total_out_size);
      bufdec1_size = 0;
      if (dec->decReq1.numMinBuffers) {
         bufdec1_size =
             dec->decReq1.numMinBuffers *
             dec->decReq1.bufferSize;
      }

      bufdec2_size = 0;
      if (dec->decReq2.numMinBuffers) {
         bufdec2_size =
             dec->decReq2.numMinBuffers *
             dec->decReq2.bufferSize;
      }

      out_offset = 0;
      dec1_offset = Q6_VDEC_PAGE_ALIGN(out_offset + total_out_size);
      dec2_offset = Q6_VDEC_PAGE_ALIGN(dec1_offset + bufdec1_size);
      total_size = dec2_offset + bufdec2_size;
      total_size = (total_size + page_size - 1) & (~(page_size - 1));

      if (pmem_alloc(&arena, total_size)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "vdec: failed to allocate input pmem arena (%d bytes)\n",
                  total_size);
         return VDEC_EFAILED;
      }

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "vdec: allocated %d bytes of input pmem\n",
               total_size);
      dec->arena[pmembuf_cnt].data = arena.data;
      dec->arena[pmembuf_cnt].fd = arena.fd;
      dec->arena[pmembuf_cnt].phys = arena.phys;
      dec->arena[pmembuf_cnt].size = arena.size;

      for (n = 0; n < dec->ctxt->numInputBuffers; n++) {
         struct Vdec_BufferInfo *fr = dec->ctxt->inputBuffer + n;
         adsp_buf_info.buf.pmem_id = fr->pmem_id;
         adsp_buf_info.buf.offset = fr->pmem_offset;
         adsp_buf_info.buf.size = fr->bufferSize;
         adsp_buf_info.buf_type = ADSP_BUFFER_TYPE_INPUT;
         adsp_buf_info.numbuf = 1;
         adsp_buf_info.is_last =
             (n == (dec->ctxt->numInputBuffers - 1));
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "      input[%d]  base=%p off=0x%08x id=%d\n",
                  n, fr->base, fr->pmem_offset,
                  fr->pmem_id);
         if (adsp_set_buffers
             ((struct adsp_module *)dec->adsp_module,
              adsp_buf_info)) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "vdec: failed to set adsp buffers");
            return VDEC_EFAILED;
         }
      }

   } else {
      dec->ctxt->inputBuffer =
          (struct Vdec_BufferInfo *)
          malloc(sizeof(struct Vdec_BufferInfo) *
            dec->ctxt->inputReq.numMinBuffers);
      if (NULL == dec->ctxt->inputBuffer) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to allocate Input buffers\n");
         return VDEC_EFAILED;
      }

      dec->pmem_buffers = 1;

      dec->arena =
          (struct pmem *)malloc(sizeof(struct pmem) *
                 dec->pmem_buffers);
      if (NULL == dec->arena) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to allocate output buffers\n");
         free(dec->ctxt->inputBuffer);
         return VDEC_EFAILED;
      }
       extraSize = getExtraDataSize();

      if(NULL == dec->ctxt->outputBuffer ) {
          dec->ctxt->outputBuffer =
             (struct vdec_frame *)malloc(sizeof(struct vdec_frame) *
                     dec->ctxt->outputReq.
                     numMinBuffers);

         if (NULL == dec->ctxt->outputBuffer) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to allocate output buffers\n");
         free(dec->ctxt->inputBuffer);
            free(dec->arena);
         return VDEC_EFAILED;
      }
          bufout_size =
             Q6_VDEC_PAGE_ALIGN(dec->ctxt->outputReq.bufferSize +
                   extraSize);
          total_out_size =
             Q6_VDEC_PAGE_ALIGN(bufout_size *
                   dec->ctxt->outputReq.numMinBuffers);
      }

      bufin_size =
          dec->ctxt->inputReq.numMinBuffers *
          dec->ctxt->inputReq.bufferSize;
      pmembuf_cnt = 0;
      dec->ctxt->numInputBuffers = dec->ctxt->inputReq.numMinBuffers;


      dec->ctxt->nOutBufAllocLen =
          dec->ctxt->outputReq.bufferSize + extraSize;
      //dec->ctxt->nOutBufAllocLen = dec->ctxt->outputReq.bufferSize ;
    
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "vdec: Commit: out buf size %d and tot out size %d\n",  bufout_size, total_out_size);

      bufdec1_size = 0;
      if (dec->decReq1.numMinBuffers) {
         bufdec1_size =
             dec->decReq1.numMinBuffers *
             dec->decReq1.bufferSize;
      }

      bufdec2_size = 0;
      if (dec->decReq2.numMinBuffers) {
         bufdec2_size =
             dec->decReq2.numMinBuffers *
             dec->decReq2.bufferSize;
      }

      out_offset = bufin_size;
      out_offset = Q6_VDEC_PAGE_ALIGN(out_offset);
      dec1_offset = Q6_VDEC_PAGE_ALIGN(out_offset + total_out_size);
      dec2_offset = Q6_VDEC_PAGE_ALIGN(dec1_offset + bufdec1_size);
      total_size = dec2_offset + bufdec2_size;
      total_size = (total_size + page_size - 1) & (~(page_size - 1));
      if (pmem_alloc(&arena, total_size)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "vdec: failed to allocate input pmem arena (%d bytes)\n",
                  total_size);
         return VDEC_EFAILED;
      }

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "vdec: allocated %d bytes of input pmem\n",
               total_size);
      dec->arena[pmembuf_cnt].data = arena.data;
      dec->arena[pmembuf_cnt].fd = arena.fd;
      dec->arena[pmembuf_cnt].phys = arena.phys;
      dec->arena[pmembuf_cnt].size = arena.size;

      off = 0;
      for (n = 0; n < dec->ctxt->inputReq.numMinBuffers; n++) {
         struct Vdec_BufferInfo *fr = dec->ctxt->inputBuffer + n;
         fr->pmem_id = dec->arena[pmembuf_cnt].fd;
         fr->pmem_offset = off;
         fr->base =
             ((byte *) dec->arena[pmembuf_cnt].data) + off;
         fr->bufferSize = dec->ctxt->inputReq.bufferSize;
         off += dec->ctxt->inputReq.bufferSize;
         fr->state = VDEC_BUFFER_WITH_VDEC_CORE;
         QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "      input[%d]  base=%p off=0x%08x id=%d\n",
                  n, fr->base, fr->pmem_offset,
                  fr->pmem_id);
      }
      adsp_buf_info.buf.pmem_id = dec->arena[pmembuf_cnt].fd;
      adsp_buf_info.buf.offset = 0;
      adsp_buf_info.buf.size = bufin_size;
      adsp_buf_info.buf_type = ADSP_BUFFER_TYPE_INPUT;
      adsp_buf_info.numbuf = dec->ctxt->inputReq.numMinBuffers;
      adsp_buf_info.is_last = 1;

      if (adsp_set_buffers
          ((struct adsp_module *)dec->adsp_module, adsp_buf_info)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to set adsp buffers");
         return VDEC_EFAILED;
      }
   }

   off = out_offset;
   for (n = 0; n < dec->ctxt->outputReq.numMinBuffers; n++) {
      struct vdec_frame *fr = NULL;
      fr = dec->ctxt->outputBuffer + n;
      if(bufout_size > 0) {
      fr->buffer.pmem_id = dec->arena[pmembuf_cnt].fd;
      fr->buffer.pmem_offset = off;
      fr->buffer.base = ((byte *) dec->arena[pmembuf_cnt].data) + off;
      fr->buffer.state = VDEC_BUFFER_WITH_APP_FLUSHED;
         off += bufout_size;
      }
      QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "      output[%d] base=%p off=0x%08x id=%d\n", n,
               fr->buffer.base, fr->buffer.pmem_offset,
               fr->buffer.pmem_id);

      adsp_buf_info.buf.pmem_id = fr->buffer.pmem_id;
      adsp_buf_info.buf.offset = fr->buffer.pmem_offset;
      adsp_buf_info.buf.size = dec->ctxt->outputReq.bufferSize;
      adsp_buf_info.buf_type = ADSP_BUFFER_TYPE_OUTPUT;
      adsp_buf_info.numbuf = 1;
      adsp_buf_info.is_last =
          ((n == (dec->ctxt->outputReq.numMinBuffers - 1)) ? 1 : 0);
      if (adsp_set_buffers
          ((struct adsp_module *)dec->adsp_module, adsp_buf_info)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to set adsp buffers");
         return VDEC_EFAILED;
      }
   }

   if (dec->decReq1.numMinBuffers) {
      adsp_buf_info.buf.pmem_id = dec->arena[pmembuf_cnt].fd;
      adsp_buf_info.buf.offset = dec1_offset;
      adsp_buf_info.buf.size = bufdec1_size;
      adsp_buf_info.buf_type = ADSP_BUFFER_TYPE_INTERNAL1;
      adsp_buf_info.numbuf = dec->decReq1.numMinBuffers;
      adsp_buf_info.is_last = 1;

      if (adsp_set_buffers
          ((struct adsp_module *)dec->adsp_module, adsp_buf_info)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to set adsp buffers");
         return VDEC_EFAILED;
      }
   }

   if (dec->decReq2.numMinBuffers) {
      adsp_buf_info.buf.pmem_id = dec->arena[pmembuf_cnt].fd;
      adsp_buf_info.buf.offset = dec2_offset;
      adsp_buf_info.buf.size = bufdec2_size;
      adsp_buf_info.buf_type = ADSP_BUFFER_TYPE_INTERNAL2;
      adsp_buf_info.numbuf = dec->decReq2.numMinBuffers;
      adsp_buf_info.is_last = 1;
      if (adsp_set_buffers
          ((struct adsp_module *)dec->adsp_module, adsp_buf_info)) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "vdec: failed to set adsp buffers");
         return VDEC_EFAILED;
      }
   }
   dec->is_commit_memory = 1;
   return VDEC_SUCCESS;
}
struct VDecoder *vdec_open(struct vdec_context *ctxt)
{

   struct VDecoder *dec;
   int r;
   int fd;
   struct adsp_open_info openinfo;
   struct adsp_init init;
   struct adsp_buf_req buf;
   struct Vdec_pthread_info *pthread_info;
   dec = (VDecoder *) calloc(1, sizeof(struct VDecoder));

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "vdec_open\n");
   if (!dec)
      return 0;

   pthread_info =
       (struct Vdec_pthread_info *)
       malloc(sizeof(struct Vdec_pthread_info));

   if (!pthread_info) {
      free(dec);
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "vdec_open failed while allocating memory for pthread_info\n");
      return 0;
   }
   sem_init(&pthread_info->flush_sem, 0, 0);
   pthread_mutex_init(&pthread_info->in_buf_lock, 0);
   pthread_mutex_init(&pthread_info->out_buf_lock, 0);

   dec->thread_specific_info = (void *)pthread_info;
   dec->ctxt = ctxt;
   dec->is_commit_memory = 0;

   openinfo.frame_done = vdec_frame_cb_handler;
   openinfo.buffer_done = vdec_reuse_input_cb_handler;

   dec->adsp_module =
       (void *)adsp_open("/dev/vdec", openinfo, (void *)dec, dec->ctxt->vdec_fd );

   if (NULL == dec->adsp_module) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Adsp Open Failed\n");
      goto fail_open;
   }

#if LOG_YUV_FRAMES
#ifdef T_WINNT
   pYUVFile = fopen("../debug/yuvframes.yuv", "wb");
#elif _ANDROID_
   pYUVFile = fopen("/data/yuvframes.yuv", "wb");
#else
   pYUVFile = fopen("yuvframes.yuv", "wb");
#endif
   if(!pYUVFile) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
          "vdec: error: Unable to open file to log YUV frames.");
   }
#endif /* LOG_YUV_FRAMES */

#if LOG_INPUT_BUFFERS
#ifdef T_WINNT
   pInputFile = fopen("../debug/inputbuffers.264", "wb");
#elif _ANDROID_
   pInputFile = fopen("/data/inputbuffers.264", "wb");
#else
   pInputFile = fopen("inputbuffers.264", "wb");
#endif
   if(!pInputFile) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
          "vdec: error: Unable to open file to log Input buffers.");
   }

#endif /* LOG_INPUT_BUFFERS */


   init.seq_header = dec->ctxt->sequenceHeader;
   init.seq_len = dec->ctxt->sequenceHeaderLen;
   if(init.seq_len > VDEC_MAX_SEQ_HEADER_SIZE)
     init.seq_len = VDEC_MAX_SEQ_HEADER_SIZE;

#if LOG_INPUT_BUFFERS
   if(pInputFile) {
      fwritex((uint8 *) init.seq_header, init.seq_len, pInputFile);
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                           "seq head length %d\n", init.seq_len);
   }
#endif

   init.width = dec->ctxt->width;

   init.height = dec->ctxt->height;
   init.order = 1;
   init.fourcc = dec->ctxt->fourcc;
   init.notify_enable = 1;
   init.h264_nal_len_size = dec->ctxt->size_of_nal_length_field;
   init.h264_startcode_detect = 0;
   if (!dec->ctxt->size_of_nal_length_field) {
      init.h264_startcode_detect = 1;
   }
   init.postproc_flag = dec->ctxt->postProc;
   init.vc1_rowbase = dec->ctxt->vc1Rowbase;
   init.fruc_enable = 0;
   init.color_format = 0;
   if (dec->ctxt->color_format ==
              QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Open setting color format to yamato \n");
      init.color_format = ADSP_COLOR_FORMAT_NV21_YAMATO;
   }
   init.buf_req = &buf;

   if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.avc")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening H264 Decoder \n");
      init.order = 0;
   } else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.mpeg4")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening MPEG4 Decoder \n");
      init.order = 0;
   } else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.divx")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening Divx Decoder \n");
      init.order = 1;
   }else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.h263")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening H263 Decoder \n");
     init.order = 0;
   } else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.vc1")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening VC1 Decoder \n");
     init.order = 0;
   }else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.vp")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening VP6 Decoder \n");
     init.order = 0;
   } else if (!strcmp(dec->ctxt->kind, "OMX.qcom.video.decoder.spark")) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "vdec: Opening Spark Decoder \n");
     init.order = 0;
   } else {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Incorrect codec kind\n");
      goto fail_initialize;
   }

   if (adsp_init((struct adsp_module *)dec->adsp_module, &init) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Adsp Open Failed\n");
      goto fail_initialize;
   }
   if(FLAG_THUMBNAIL_MODE == init.postproc_flag) {
      struct vdec_property_info property;
      property.id = VDEC_PRIORITY;
      property.property.priority = 0;
      adsp_setproperty((struct adsp_module *)dec->adsp_module, &property);
   }

   QPERF_RESET(arm_decode);

   timestamp = 0;

   dec->ctxt->inputReq.numMinBuffers = init.buf_req->input.bufnum_min;
   dec->ctxt->inputReq.numMaxBuffers = init.buf_req->input.bufnum_max;
   dec->ctxt->inputReq.bufferSize = init.buf_req->input.bufsize;

   if(init.buf_req->output.bufnum_min < 2) {
      /* actually according to dsp for thumbnails only 1 o/p buff is enough, but
         because of some issue for some clips they needed atleast two o/p buffs */
      dec->ctxt->outputReq.numMinBuffers = 2;
      dec->ctxt->outputReq.numMaxBuffers = 2;
   } else {
      dec->ctxt->outputReq.numMinBuffers = init.buf_req->output.bufnum_min;
      dec->ctxt->outputReq.numMaxBuffers = init.buf_req->output.bufnum_max;
   }
   /*for VC1 QDSP6 requires 4 output buffers, but Android surface flinger
     holds 2 buffers due to this it is causing some issues, to resolve it
     arm vdec allocates two more o/p buffers.*/
   if (!strncmp(dec->ctxt->kind, "OMX.qcom.video.decoder.vc1",OMX_MAX_STRINGNAME_SIZE))
            dec->ctxt->outputReq.numMinBuffers = init.buf_req->output.bufnum_min+2;
   else
            dec->ctxt->outputReq.numMinBuffers = init.buf_req->output.bufnum_min;

   dec->ctxt->outputReq.bufferSize = init.buf_req->output.bufsize;
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec_open input numbuf= %d and bufsize= %d\n",
            dec->ctxt->inputReq.numMinBuffers,
            dec->ctxt->inputReq.bufferSize);
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec_open output numbuf= %d and bufsize= %d\n",
            dec->ctxt->outputReq.numMinBuffers,
            dec->ctxt->outputReq.bufferSize);
   dec->decReq1.numMinBuffers = init.buf_req->dec_req1.bufnum_min;
   dec->decReq1.numMaxBuffers = init.buf_req->dec_req1.bufnum_max;
   dec->decReq1.bufferSize = init.buf_req->dec_req1.bufsize;
   dec->decReq2.numMinBuffers = init.buf_req->dec_req2.bufnum_min;
   dec->decReq2.numMaxBuffers = init.buf_req->dec_req2.bufnum_max;
   dec->decReq2.bufferSize = init.buf_req->dec_req2.bufsize;
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec_open decoder1 numbuf= %d and bufsize= %d\n",
            dec->decReq1.numMinBuffers, dec->decReq1.bufferSize);
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec_open ointernal 2 numbuf= %d and bufsize= %d\n",
            dec->decReq2.numMinBuffers,
            dec->decReq2.bufferSize);
   dec->ctxt->numOutputBuffers =
          dec->ctxt->outputReq.numMinBuffers;

   return dec;

fail_initialize:
   adsp_close((struct adsp_module *)dec->adsp_module);
fail_open:
   free(dec);
   free(pthread_info);
   return 0;
}

Vdec_ReturnType vdec_post_input_buffer(struct VDecoder * dec,
                   video_input_frame_info * frame,
                   void *cookie, int is_pmem)
{
   QTV_MSG_PRIO4(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "vdec: post_input data=%p len=%d %x cookie=%p\n",
            frame->data, frame->len, frame->len, cookie);

#if DEBUG_ON
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec_post_input_buffer, length %d\n", frame->len);
   for (uint32 i = 0; i < 32; i++) {
      printf("0x%.2x ", ((uint8 *) (frame->data))[i]);
      if (i % 16 == 15) {
         printf("\n");
      }
   }
   printf("\n");
#endif

   adsp_input_buf input;
   int buf_index, i;
   struct Vdec_pthread_info *pthread_info;
   unsigned int copy_size;

   buf_index = -1;

   if (NULL == dec || NULL == frame || NULL == frame->data) {
      QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "vdec: error: encountered NULL parameter dec: 0x%x frame: 0x%x data: 0x%x\n",
               (unsigned int)dec, (unsigned int)frame,
               (unsigned int)frame->data);
      return VDEC_EFAILED;
   }

   if (dec->ctxt->outputBuffer) {
      for (i = 0; i < dec->ctxt->numOutputBuffers; i++) {
         if (dec->ctxt->outputBuffer[i].buffer.state ==
             VDEC_BUFFER_WITH_HW) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "vdec_post available free output buffer %d\n",
                     i);
            break;
         }
      }
      if (i >= dec->ctxt->numOutputBuffers) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "vdec_post output buffer not avilable for decode \n");
         return VDEC_EOUTOFBUFFERS;
      }
   }

   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;

   if (is_pmem == 0) {
      if (dec->ctxt->inputBuffer) {
      for (i = 0; i < dec->ctxt->inputReq.numMinBuffers; i++) {
         if (dec->ctxt->inputBuffer[i].state ==
             VDEC_BUFFER_WITH_VDEC_CORE) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                     "vdec: got input buffer index %d\n",
                     i);
            pthread_mutex_lock(&pthread_info->in_buf_lock);
            dec->ctxt->inputBuffer[i].state =
                VDEC_BUFFER_WITH_HW;
            pthread_mutex_unlock(&pthread_info->
                       in_buf_lock);
            buf_index = i;
            break;
         }
      }
      }
   } else {
      for (i = 0; i < dec->ctxt->numInputBuffers; i++) {
         if ((frame->data >= dec->ctxt->inputBuffer[i].base) &&
             (frame->data <
              (dec->ctxt->inputBuffer[i].base +
               dec->ctxt->inputBuffer[i].bufferSize))) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                     "vdec: got input buffer index %d\n",
                     i);
            pthread_mutex_lock(&pthread_info->in_buf_lock);
            dec->ctxt->inputBuffer[i].state =
                VDEC_BUFFER_WITH_HW;
            pthread_mutex_unlock(&pthread_info->
                       in_buf_lock);
            buf_index = i;
            break;
         }
      }

   }

   if (buf_index < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
              "Wrong Input buffer and not able to get the buffer Index\n");
      return VDEC_EOUTOFBUFFERS;
   }
#if LOG_INPUT_BUFFERS
   if(pInputFile) {
      fwritex((uint8 *) frame->data, frame->len, pInputFile);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "vdec: input buffer %d len %d\n", counter++, frame->len);
   }

#endif

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
            "vdec: inputBuffer[].base %x, buffer size %d\n",
            dec->ctxt->inputBuffer[buf_index].base,
            dec->ctxt->inputBuffer[buf_index].bufferSize);
   copy_size = frame->len;
   if (!is_pmem) {
      copy_size =
          ((dec->ctxt->inputBuffer[buf_index].bufferSize >=
            frame->len) ? frame->len : dec->ctxt->
           inputBuffer[buf_index].bufferSize);
      memcpy(dec->ctxt->inputBuffer[buf_index].base,
             (uint8 *) frame->data, (uint32) copy_size);
      input.offset = dec->ctxt->inputBuffer[buf_index].pmem_offset;
   } else {
      input.offset =
          (byte *) frame->data -
          dec->ctxt->inputBuffer[buf_index].base;
   }

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "cookie %d\tbuf_index %d\n", (int)cookie, buf_index);

   input.flags = 0;
   if((frame->timestamp & SEI_TRIGGER_BIT_VDEC))
   {
      /* client want the sei calculation so trigger dsp to do sei math*/
      input.flags |= SEI_TRIGGER_BIT_QDSP;
   }

   dec->ctxt->inputBuffer[buf_index].omx_cookie = cookie;
   input.pmem_id = dec->ctxt->inputBuffer[buf_index].pmem_id;
   input.timestamp_lo = (int32) (frame->timestamp & 0x00000000FFFFFFFFLL);
   input.timestamp_hi =
       (int32) ((frame->timestamp & 0xFFFFFFFF00000000LL) >> 32);
   input.size = (uint32) copy_size;
   input.data = (uint32) (dec->ctxt->inputBuffer[buf_index].omx_cookie);

   //RAJESH: TBD below
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec: input->size %d, input->offset %x\n", input.size,
            input.offset);
   //input.avsync_state

#ifdef USE_PMEM_ADSP_CACHED
   //Flush/clean the cache (bit-stream data sent to driver)
   vdec_cachemaint(input.pmem_id, dec->ctxt->inputBuffer[buf_index].base, copy_size, PMEM_CACHE_FLUSH);
#endif

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec: received ts: %lld",
            frame->timestamp);
   if (frame->timestamp < timestamp) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "vdec: error: out of order stamp! %d < %d\n",
               (int)(frame->timestamp & 0xFFFFFFFF), timestamp);
   }
   timestamp = (int)frame->timestamp;

   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "vdec: vdec_core_post_input. buffer_size[0]: %ld frame->flags: 0x%x\n",
            input.size, frame->flags);

   if (input.size == 0 && frame->flags & FRAME_FLAG_EOS) {

      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
              "vdec: Zero-length buffer with EOS bit set\n");

      pthread_mutex_lock(&pthread_info->in_buf_lock);
      dec->ctxt->inputBuffer[i].state = VDEC_BUFFER_WITH_VDEC_CORE;
      pthread_mutex_unlock(&pthread_info->in_buf_lock);

      if (adsp_post_input_buffer
          ((struct adsp_module *)dec->adsp_module, input, 1) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                 "vdec: Post Input Buffer Failed\n");
         return VDEC_EFAILED;
      }
      dec->ctxt->buffer_done(dec->ctxt, cookie);
      return VDEC_SUCCESS;

   }

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "vdec: vdec_core_post_input\n");

   QPERF_START(arm_decode);

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
           "vdec: queue frame (ioctl) \n");

   if (adsp_post_input_buffer
       ((struct adsp_module *)dec->adsp_module, input, 0) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
              "vdec: Post Input Buffer Failed\n");
      pthread_mutex_lock(&pthread_info->in_buf_lock);
      dec->ctxt->inputBuffer[i].state = VDEC_BUFFER_WITH_VDEC_CORE;
      pthread_mutex_unlock(&pthread_info->in_buf_lock);
      dec->ctxt->buffer_done(dec->ctxt,cookie);
      return VDEC_EFAILED;
   }

   if (frame->flags & FRAME_FLAG_EOS) {
      if (adsp_post_input_buffer
          ((struct adsp_module *)dec->adsp_module, input, 1) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                 "vdec: Post Input Buffer EOS Failed\n");
         return VDEC_EFAILED;
      }

   }

   return VDEC_SUCCESS;
}

Vdec_ReturnType vdec_release_frame(struct VDecoder * dec,
               struct vdec_frame * frame)
{
   unsigned int buf, i;
   struct Vdec_pthread_info *pthread_info;
   int buf_index = -1;

   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "vdec: release_frame %p\n", frame);
   if (NULL == dec || NULL == frame) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "vdec: ERROR: encountered NULL parameter vdec: 0x%x frame: 0x%x",
               (unsigned int)dec, (unsigned int)frame);
      return VDEC_EFAILED;
   }
   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;
   for (i = 0; i < dec->ctxt->numOutputBuffers; i++) {
      if (dec->ctxt->outputBuffer[i].buffer.base ==
          frame->buffer.base) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
                  "vdec: got output buffer index %d\n", i);
         buf_index = i;
         break;
      }
   }
   if (buf_index < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "vdec_release_frame: Wrong Output buffer and not able to get the buffer Index\n");
      return VDEC_EFAILED;
   }

   if (dec->ctxt->outputBuffer[buf_index].buffer.state ==
       VDEC_BUFFER_WITH_APP) {
      pthread_mutex_lock(&pthread_info->out_buf_lock);
      dec->ctxt->outputBuffer[buf_index].buffer.state =
          VDEC_BUFFER_WITH_HW;
      pthread_mutex_unlock(&pthread_info->out_buf_lock);
      buf = frame->frameDetails.userData1;
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "vdec: release_frame userData1 %d\n", buf);

      if (adsp_release_frame
          ((struct adsp_module *)dec->adsp_module, &buf) < 0) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Adsp release frame failed\n");
         return VDEC_EFAILED;
      }
   } else {
      pthread_mutex_lock(&pthread_info->out_buf_lock);
      dec->ctxt->outputBuffer[buf_index].buffer.state =
          VDEC_BUFFER_WITH_HW;
      pthread_mutex_unlock(&pthread_info->out_buf_lock);
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
               "vdec: release_frame called for Buffer not with OMX: %d",
               dec->ctxt->outputBuffer[buf_index].buffer.state);
   }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
            "vdec: released_frame with ptr: %d", buf);
   return VDEC_SUCCESS;
}

#ifdef USE_PMEM_ADSP_CACHED
void vdec_cachemaint(int pmem_id, void *addr, unsigned size, PMEM_CACHE_OP op)
{
   pmem_cachemaint(pmem_id,addr,size, op);
}
#endif

Vdec_ReturnType vdec_flush_port(struct VDecoder * dec, int *nFlushedFrames,
            Vdec_PortType port)
{
   struct Vdec_pthread_info *pthread_info;
   int i = 0;
   Vdec_ReturnType retVal=VDEC_SUCCESS;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW, "vdec: flush \n");
   if (NULL == dec || NULL == dec->ctxt || !dec->is_commit_memory) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "vdec: error: encountered NULL parameter vdec: 0x%x or commit memroy not called %d \n",
               (unsigned int)dec, dec->is_commit_memory);
      return VDEC_EFAILED;
   }
   pthread_info = (struct Vdec_pthread_info *)dec->thread_specific_info;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "[vdec_flush] - calling IoCTL \n");;

   if (adsp_flush
       ((struct adsp_module *)dec->adsp_module, (unsigned int)port) < 0) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Adsp Flush failed\n");
      retVal = VDEC_EFAILED;
   }else{
      /* this is to make flush a sync call*/
      if (-1 == sem_wait(&pthread_info->flush_sem)) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "[vdec_flush] - sem_wait failed %d\n", errno);;
      }
   }
   /*Now release all the Input as well as Frame buffers */
   if (dec->ctxt->inputBuffer) {
      for (i = 0; i < dec->ctxt->inputReq.numMinBuffers; i++) {
         if (dec->ctxt->inputBuffer[i].state ==
             VDEC_BUFFER_WITH_HW) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "vdec_flush Flushing input buffer %d\n",
                     i);
            pthread_mutex_lock(&pthread_info->in_buf_lock);
            dec->ctxt->inputBuffer[i].state =
                VDEC_BUFFER_WITH_VDEC_CORE;
            pthread_mutex_unlock(&pthread_info->
                       in_buf_lock);
            dec->ctxt->buffer_done(dec->ctxt,
                         dec->ctxt->
                         inputBuffer[i].
                         omx_cookie);

         }
      }
   }
   if (dec->ctxt->outputBuffer) {
      for (i = 0; i < dec->ctxt->numOutputBuffers; i++) {
         if (dec->ctxt->outputBuffer[i].buffer.state ==
             VDEC_BUFFER_WITH_HW) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "vdec_flush Flushing output buffer %d\n",
                     i);
            pthread_mutex_lock(&pthread_info->out_buf_lock);
            dec->ctxt->outputBuffer[i].buffer.state =
                VDEC_BUFFER_WITH_APP_FLUSHED;
            pthread_mutex_unlock(&pthread_info->
                       out_buf_lock);
            dec->ctxt->outputBuffer[i].flags =
                FRAME_FLAG_FLUSHED;
            dec->ctxt->frame_done(dec->ctxt,
                        &dec->ctxt->
                        outputBuffer[i]);

         }
      }
   }
   return retVal;
}
Vdec_ReturnType vdec_performance_change_request(struct VDecoder* dec, unsigned int request_type) {
  if (NULL == dec || NULL == dec->adsp_module) {
    QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
      "vdec: error: encountered NULL parameter vdec: 0x%x or adsp_module: 0x%x \n",
      (unsigned int)dec, dec->adsp_module);
    return VDEC_EFAILED;
  }
  if(adsp_performance_change_request((struct adsp_module *)dec->adsp_module,request_type))
    return VDEC_EFAILED;
  return VDEC_SUCCESS;
}
