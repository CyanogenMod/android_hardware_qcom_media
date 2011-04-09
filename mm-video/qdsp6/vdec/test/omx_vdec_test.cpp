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
/*
	An Open max test application ....
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#ifdef _ANDROID_
#include <binder/MemoryHeapBase.h>
#endif
#include "OMX_Core.h"
#include "OMX_Component.h"
#include "OMX_QCOMExtns.h"
#include "qtv_msg.h"
extern "C" {
#include "queue.h"
#include "pmem.h"
}


#include <linux/msm_mdp.h>
#include <linux/fb.h>
#include "qutility.h"


/************************************************************************/
/*				#DEFINES	                        */
/************************************************************************/
#define DELAY 66
#define false 0
#define true 1
#define VOP_START_CODE 0x000001B6
#define SHORT_HEADER_START_CODE 0x00008000
#define SPARK1_START_CODE 0x00008400
#define VC1_START_CODE  0x00000100
#define NUMBER_OF_ARBITRARYBYTES_READ  (4 * 1024)
#define VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC 32
#define VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC 16

#define CONFIG_VERSION_SIZE(param) \
	param.nVersion.nVersion = CURRENT_OMX_SPEC_VERSION;\
	param.nSize = sizeof(param);

#define FAILED(result) (result != OMX_ErrorNone)

#define SUCCEEDED(result) (result == OMX_ErrorNone)
#define SWAPBYTES(ptrA, ptrB) { char t = *ptrA; *ptrA = *ptrB; *ptrB = t;}
#define SIZE_NAL_FIELD_MAX  4
#define MDP_DEINTERLACE 0x80000000
#define MDP_VERSION_3_1 100 /* 3.1--> 51('3') + 49('1') = 100 */

/************************************************************************/
/*				GLOBAL DECLARATIONS                     */
/************************************************************************/
#ifdef _ANDROID_
using namespace android;
#endif

typedef enum {
  CODEC_FORMAT_H264 = 1,
  CODEC_FORMAT_MP4,
  CODEC_FORMAT_H263,
  CODEC_FORMAT_VC1,
  CODEC_FORMAT_DIVX,
  CODEC_FORMAT_VP,
  CODEC_FORMAT_SPARK0,
  CODEC_FORMAT_SPARK1,
  CODEC_FORMAT_MAX = CODEC_FORMAT_SPARK1
} codec_format;

typedef enum {
  FILE_TYPE_DAT_PER_AU = 1,
  FILE_TYPE_ARBITRARY_BYTES,
  FILE_TYPE_COMMON_CODEC_MAX,

  FILE_TYPE_START_OF_H264_SPECIFIC = 10,
  FILE_TYPE_264_NAL_SIZE_LENGTH = FILE_TYPE_START_OF_H264_SPECIFIC,

  FILE_TYPE_START_OF_MP4_SPECIFIC = 20,
  FILE_TYPE_PICTURE_START_CODE = FILE_TYPE_START_OF_MP4_SPECIFIC,

  FILE_TYPE_START_OF_VC1_SPECIFIC = 30,
  FILE_TYPE_RCV = FILE_TYPE_START_OF_VC1_SPECIFIC,
  FILE_TYPE_VC1,

  FILE_TYPE_START_OF_DIVX_SPECIFIC = 40,
  FILE_TYPE_DIVX_4_5_6 = FILE_TYPE_START_OF_DIVX_SPECIFIC,
  FILE_TYPE_DIVX_311,

  FILE_TYPE_START_OF_VP_SPECIFIC = 50,
  FILE_TYPE_VP_6 = FILE_TYPE_START_OF_VP_SPECIFIC,
} file_type;

typedef enum {
  GOOD_STATE = 0,
  PORT_SETTING_CHANGE_STATE,
  ERROR_STATE,
  INVALID_STATE
} test_status;

typedef enum {
  FREE_HANDLE_AT_LOADED = 1,
  FREE_HANDLE_AT_IDLE,
  FREE_HANDLE_AT_EXECUTING,
  FREE_HANDLE_AT_PAUSE
} freeHandle_test;

struct use_egl_id {
    int pmem_fd;
    int offset;
};

static int (*Read_Buffer)(OMX_BUFFERHEADERTYPE  *pBufHdr );

struct use_egl_id *egl_id = NULL;
int is_use_egl_image = 0;

FILE * inputBufferFile;
FILE * outputBufferFile;
int takeYuvLog = 0;
int displayYuv = 0;
int displayWindow = 0;
int is_yamato = 0;

Queue *etb_queue = NULL;
Queue *fbd_queue = NULL;

int realtime_display = 0;

pthread_t ebd_thread_id;
pthread_t fbd_thread_id;
pthread_t fbiopan_thread_id;
void* ebd_thread(void*);
void* fbd_thread(void*);

pthread_mutex_t etb_lock;
pthread_mutex_t fbd_lock;
pthread_mutex_t lock;
pthread_cond_t cond;
pthread_mutex_t elock;
pthread_cond_t econd;
pthread_cond_t fcond;
pthread_mutex_t eos_lock;
pthread_cond_t eos_cond;

sem_t etb_sem;
sem_t fbd_sem;

void* fbiopan_thread(void*);
int fbiopan_pipe[2] = {0};

OMX_PARAM_PORTDEFINITIONTYPE portFmt;
OMX_PORT_PARAM_TYPE portParam;
OMX_ERRORTYPE error;

static int fb_fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr);

/************************************************************************/
/*				GLOBAL INIT			        */
/************************************************************************/
int input_buf_cnt = 0;
int height =0, width =0;
int used_ip_buf_cnt = 0;
volatile int event_is_done = 0;
int ebd_cnt, fbd_cnt;
int bInputEosReached = 0;
int bOutputEosReached = 0;
char in_filename[512];

int timeStampLfile = 0;
int timestampInterval = 33333; /* default 30 fps */
codec_format  codec_format_option;
file_type     file_type_option;
freeHandle_test freeHandle_option;
int nalSize;
int sent_disabled = 0;
int waitForPortSettingsChanged = 1;
test_status currentStatus = GOOD_STATE;

//* OMX Spec Version supported by the wrappers. Version = 1.1 */
const OMX_U32 CURRENT_OMX_SPEC_VERSION = 0x00000101;
OMX_COMPONENTTYPE* dec_handle = 0;

OMX_BUFFERHEADERTYPE  **pInputBufHdrs = NULL;
OMX_BUFFERHEADERTYPE  **pOutYUVBufHdrs= NULL;

int rcv_v1=0;

struct timeval t_avsync={0};

/* Performance related variable*/
QPERF_INIT(render_fb);
QPERF_INIT(client_decode);

/************************************************************************/
/*				GLOBAL FUNC DECL                        */
/************************************************************************/
int Init_Decoder();
int Play_Decoder();
int run_tests();

/**************************************************************************/
/*				STATIC DECLARATIONS                       */
/**************************************************************************/
static int video_playback_count = 1;
static int open_video_file ();
static int Read_Buffer_From_DAT_File(OMX_BUFFERHEADERTYPE  *pBufHdr );
static int Read_Buffer_ArbitraryBytes(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_Vop_Start_Code_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_FrameSize_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBufHdr);

static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize);
static OMX_ERRORTYPE Use_EGL_Buffer ( OMX_COMPONENTTYPE *dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize,
                                      struct use_egl_id **egl);


static OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                                  OMX_IN OMX_PTR pAppData,
                                  OMX_IN OMX_EVENTTYPE eEvent,
                                  OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                                  OMX_IN OMX_PTR pEventData);
static OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_PTR pAppData,
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE FillBufferDone(OMX_OUT OMX_HANDLETYPE hComponent,
                                    OMX_OUT OMX_PTR pAppData,
                                    OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer);

static void do_freeHandle_and_clean_up(bool isDueToError);

/*static usecs_t get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((usecs_t)tv.tv_usec) +
        (((usecs_t)tv.tv_sec) * ((usecs_t)1000000));
}*/


void wait_for_event(void)
{
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Waiting for event\n");
    pthread_mutex_lock(&lock);
    while (event_is_done == 0) {
        pthread_cond_wait(&cond, &lock);
    }
    event_is_done = 0;
    pthread_mutex_unlock(&lock);
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Running .... get the event\n");
}

void event_complete(void )
{
    pthread_mutex_lock(&lock);
    if (event_is_done == 0) {
        event_is_done = 1;
        pthread_cond_broadcast(&cond);
    }
    pthread_mutex_unlock(&lock);
}

void* ebd_thread(void* pArg)
{
  while(currentStatus != INVALID_STATE)
  {
    int readBytes =0;
    OMX_BUFFERHEADERTYPE* pBuffer = NULL;

    sem_wait(&etb_sem);
    pthread_mutex_lock(&etb_lock);
    pBuffer = (OMX_BUFFERHEADERTYPE *) pop(etb_queue);
    pthread_mutex_unlock(&etb_lock);
    if(pBuffer == NULL)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                   "Error - No etb pBuffer to dequeue\n");
      continue;
    }
    pBuffer->nOffset = 0;
    if((readBytes = Read_Buffer(pBuffer)) > 0) {
      pBuffer->nFilledLen = readBytes;
      OMX_EmptyThisBuffer(dec_handle,pBuffer);
    }
    else
    {
      pBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
      bInputEosReached = true;
      pBuffer->nFilledLen = readBytes;
      OMX_EmptyThisBuffer(dec_handle,pBuffer);
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                   "EBD::Either EOS or Some Error while reading file\n");
      break;
    }
  }
  return NULL;
}

void* fbd_thread(void* pArg)
{
  while(currentStatus != INVALID_STATE)
  {
    int bytes_written = 0;
    OMX_BUFFERHEADERTYPE *pBuffer;
    int canDisplay = 1;
    long current_avsync_time = 0, delta_time = 0;
    static int contigous_drop_frame = 0;
    static long base_avsync_time = 0;
    static long base_timestamp = 0;
    long lipsync_time = 250000;
    int ioresult = 0;
    char fbiopan_signal = 1;

    sem_wait(&fbd_sem);
    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s fbd_cnt[%d] \n", __FUNCTION__, fbd_cnt);

    fbd_cnt++;
    pthread_mutex_lock(&fbd_lock);
    pBuffer = (OMX_BUFFERHEADERTYPE *) pop(fbd_queue);
    pthread_mutex_unlock(&fbd_lock);
    if (pBuffer == NULL)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Error - No pBuffer to dequeue\n");
      continue;
    }

    if (realtime_display)
    {
      if(!gettimeofday(&t_avsync,NULL))
      {
         current_avsync_time =(t_avsync.tv_sec*1000000)+t_avsync.tv_usec;
      }

      if (base_avsync_time != 0)
      {
        delta_time = (current_avsync_time - base_avsync_time) - (pBuffer->nTimeStamp - base_timestamp);

        if (delta_time < 0 )
        {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Sleep %d us. AV Sync time is left behind\n",
                 -delta_time);
          usleep(-delta_time);
          canDisplay = 1;
        }
        else if ((delta_time>lipsync_time) && (contigous_drop_frame < 6))
        {
          QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"\n\nError - Drop the frame at the renderer. Video frame with ts %ld behind by %d us\n\n",
                         pBuffer->nTimeStamp,delta_time);
          canDisplay = 0;
          contigous_drop_frame++;
        }
        else
        {
          canDisplay = 1;
        }
      }
      else
      {
        base_avsync_time = current_avsync_time;
        base_timestamp = pBuffer->nTimeStamp;
      }
    }


    /*********************************************
    Write the output of the decoder to the file.
    *********************************************/

    if (sent_disabled)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Ignoring FillBufferDone\n");
       continue;
    }

    if (displayYuv && canDisplay && pBuffer->nFilledLen > 0)
    {
      QPERF_TIME(render_fb, render_fb(pBuffer));
      contigous_drop_frame = 0;
    }

    if (takeYuvLog && outputBufferFile) {

          bytes_written = fwrite((const char *)pBuffer->pBuffer,
                                1,pBuffer->nFilledLen,outputBufferFile);
          if (bytes_written < 0) {
              QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                         "\nFillBufferDone: Failed to write to the file\n");
           }
          else {
              QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                          "\nFillBufferDone: Wrote %d YUV bytes to the file\n",
                          bytes_written);
          }
    }

    /********************************************************************/
    /* De-Initializing the open max and relasing the buffers and */
    /* closing the files.*/
    /********************************************************************/
    if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS ) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "***************************************************\n");
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "FillBufferDone: End Of Stream Reached\n");
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "***************************************************\n");
	  pthread_mutex_lock(&eos_lock);
      bOutputEosReached = true;
      pthread_cond_broadcast(&eos_cond);
      pthread_mutex_unlock(&eos_lock);
      QPERF_END(client_decode);
      QPERF_SET_ITERATION(client_decode, fbd_cnt);
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "***************************************************\n");
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"FBD_THREAD bOutputEosReached %d\n",bOutputEosReached);
      break;
    }
    OMX_FillThisBuffer(dec_handle, pBuffer);

    ioresult =  write(fbiopan_pipe[1], &fbiopan_signal, 1);
    if(ioresult < 0)
    {
       QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"\n Error in writing to fbd PIPE %d \n", ioresult);
       break;
    }

  }
  return NULL;
}

OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData,
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Function %s \n", __FUNCTION__);

    switch(eEvent) {
        case OMX_EventCmdComplete:
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\n OMX_EventCmdComplete \n");
            // check nData1 for DISABLE event
            if(OMX_CommandPortDisable == (OMX_COMMANDTYPE)nData1)
            {
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                             "*********************************************\n");
                QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                              "Recieved DISABLE Event Command Complete[%d]\n",nData2);
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                             "*********************************************\n");
                sent_disabled = 0;
            }
            else if(OMX_CommandPortEnable == (OMX_COMMANDTYPE)nData1)
            {
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                             "*********************************************\n");
                QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                              "Recieved ENABLE Event Command Complete[%d]\n",nData2);
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                             "*********************************************\n");
            }
            currentStatus = GOOD_STATE;
            event_complete();
            break;

        case OMX_EventError:
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                         "OMX_EventError \n");
             currentStatus = INVALID_STATE;
            if (OMX_ErrorInvalidState == (OMX_ERRORTYPE)nData1 ||
                OMX_ErrorStreamCorrupt == (OMX_ERRORTYPE)nData1)
            {
              QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                           "Invalid State \n");
              if(event_is_done == 0)
              {
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                           "Event error in the middle of Decode \n");
                pthread_mutex_lock(&eos_lock);
                bOutputEosReached = true;
                pthread_cond_broadcast(&eos_cond);
                pthread_mutex_unlock(&eos_lock);

              }
            }

            event_complete();
            break;
        case OMX_EventPortSettingsChanged:
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                          "OMX_EventPortSettingsChanged port[%d]\n",nData1);
            waitForPortSettingsChanged = 0;
            currentStatus = PORT_SETTING_CHANGE_STATE;
            // reset the event
            event_complete();
            break;

        default:
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"ERROR - Unknown Event \n");
            break;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent,
                              OMX_IN OMX_PTR pAppData,
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    int readBytes =0; int bufCnt=0;
    OMX_ERRORTYPE result;

    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Function %s cnt[%d]\n", __FUNCTION__, ebd_cnt);
    ebd_cnt++;


    if(bInputEosReached) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                     "*****EBD:Input EoS Reached************\n");
        return OMX_ErrorNone;
    }

    pthread_mutex_lock(&etb_lock);
    if(push(etb_queue, (void *) pBuffer) < 0)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Error in enqueue  ebd data\n");
       return OMX_ErrorUndefined;
    }
    pthread_mutex_unlock(&etb_lock);
    sem_post(&etb_sem);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE FillBufferDone(OMX_OUT OMX_HANDLETYPE hComponent,
                             OMX_OUT OMX_PTR pAppData,
                             OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
{
    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Inside %s callback_count[%d] \n", __FUNCTION__, fbd_cnt);

    /* Test app will assume there is a dynamic port setting
     * In case that there is no dynamic port setting, OMX will not call event cb,
     * instead OMX will send empty this buffer directly and we need to clear an event here
     */
    if(waitForPortSettingsChanged && currentStatus != INVALID_STATE)
    {
      currentStatus = GOOD_STATE;
      waitForPortSettingsChanged = 0;
      event_complete();
    }

    if(!sent_disabled)
    {
      pthread_mutex_lock(&fbd_lock);
      if(push(fbd_queue, (void *)pBuffer) < 0)
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"Error in enqueueing fbd_data\n");
         return OMX_ErrorUndefined;
      }
      pthread_mutex_unlock(&fbd_lock);
      sem_post(&fbd_sem);
    }
    return OMX_ErrorNone;
}

int main(int argc, char **argv)
{
    int i=0;
    int bufCnt=0;
    int num=0;
    int outputOption = 0;
    int test_option = 0;
    OMX_ERRORTYPE result;

    if (argc < 2)
    {
      printf("To use it: ./mm-vdec-omx-test <clip location> \n");
      printf("Command line argument is also available\n");
      return -1;
    }
    strncpy(in_filename, argv[1], strlen(argv[1])+1);

    if(argc > 5)
    {
      codec_format_option = (codec_format)atoi(argv[2]);
      file_type_option = (file_type)atoi(argv[3]);
    }
    else
    {
      printf("Command line argument is available\n");
      printf("To use it: ./mm-vdec-omx-test <clip location> <codec_type> \n");
      printf("           <input_type: 1. per AU(.dat), 2. arbitrary, 3.per NAL/frame>\n");
      printf("           <output_type> <test_case> <size_nal if H264>\n\n\n");
      printf("           <realtime_display: 0 if ASAP, 1 if real time, only for non-arbitrary option>\n");
      printf("           <fps> <size_nal if H264>\n\n\n");

      printf(" *********************************************\n");
      printf(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      printf(" 1--> H264\n");
      printf(" 2--> MP4\n");
      printf(" 3--> H263\n");
      printf(" 4--> VC1\n");
      printf(" 5--> DIVX\n");
      printf(" 6--> VP6\n");
      printf(" 7--> Spark0\n");
      printf(" 8--> Spark1\n");
      fflush(stdin);
      scanf("%d", &codec_format_option);
      fflush(stdin);

      if (codec_format_option > CODEC_FORMAT_MAX)
      {
          printf(" Wrong test case...[%d] \n", codec_format_option);
          return -1;
      }

      printf(" *********************************************\n");
      printf(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      if (codec_format_option != CODEC_FORMAT_DIVX && codec_format_option != CODEC_FORMAT_VP) {
         printf(" 1--> PER ACCESS UNIT CLIP (.dat). Clip only available for H264 and Mpeg4\n");
         printf(" 2--> ARBITRARY BYTES (need .264/.264c/.mv4/.263/.rcv/.vc1)\n");
      }
      if (codec_format_option == CODEC_FORMAT_H264)
      {
        printf(" 3--> NAL LENGTH SIZE CLIP (.264c)\n");
      }
      else if ( (codec_format_option == CODEC_FORMAT_MP4) || (codec_format_option == CODEC_FORMAT_H263) )
      {
        printf(" 3--> MP4 VOP or H263 P0 SHORT HEADER START CODE CLIP or DIVX (.m4v or .263)\n");
      }
      else if (codec_format_option == CODEC_FORMAT_VC1)
      {
        printf(" 3--> VC1 clip Simple/Main Profile (.rcv)\n");
        printf(" 4--> VC1 clip Advance Profile (.vc1)\n");
      }
      else if (codec_format_option == CODEC_FORMAT_DIVX) {
        printf(" 3--> Divx clip 4, 5, 6 format\n");
        printf(" 4--> Divx clip 311 format\n");
      }
      else if (codec_format_option == CODEC_FORMAT_VP)
      {
        printf(" 3--> VP6 raw bit stream (.vp)\n");
      }
      else if (codec_format_option == CODEC_FORMAT_SPARK0 || codec_format_option == CODEC_FORMAT_SPARK1)
      {
        printf(" 3--> SPARK start code based clip\n");
      }
      fflush(stdin);
      scanf("%d", &file_type_option);
      fflush(stdin);
    }

    if (file_type_option >= FILE_TYPE_COMMON_CODEC_MAX)
    {
      switch (codec_format_option)
      {
        case CODEC_FORMAT_H264:
          file_type_option = (file_type)(FILE_TYPE_START_OF_H264_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        case CODEC_FORMAT_MP4:
        case CODEC_FORMAT_H263:
        case CODEC_FORMAT_SPARK0:
        case CODEC_FORMAT_SPARK1:
          file_type_option = (file_type)(FILE_TYPE_START_OF_MP4_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        case CODEC_FORMAT_VC1:
          file_type_option = (file_type)(FILE_TYPE_START_OF_VC1_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        case CODEC_FORMAT_DIVX:
          file_type_option = (file_type)(FILE_TYPE_START_OF_DIVX_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        case CODEC_FORMAT_VP:
          file_type_option = (file_type)(FILE_TYPE_START_OF_VP_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        default:
          printf("Error: Unknown code %d\n", codec_format_option);
      }
    }

    if(argc > 5)
    {
        if(CODEC_FORMAT_H264 != codec_format_option )
        {
           outputOption = atoi(argv[4]);
           test_option = atoi(argv[5]);
           displayWindow = atoi(argv[6]);

           if ((file_type_option != FILE_TYPE_ARBITRARY_BYTES) && (argc > 7))
           {
             realtime_display = atoi(argv[7]);
           }

           if (realtime_display && (argc > 8))
           {
             int fps = atoi(argv[8]);
             timestampInterval = 1000000/fps;
             printf("\n\n ***Real time rending @%d fps**\n\n",fps);
           }

        }
        else
        {
           nalSize = atoi(argv[4]);
           outputOption = atoi(argv[5]);
           test_option = atoi(argv[6]);
           displayWindow = atoi(argv[7]);

           if ((file_type_option != FILE_TYPE_ARBITRARY_BYTES) && (argc > 8))
           {
             realtime_display = atoi(argv[8]);
           }

           if (realtime_display && (argc > 9))
           {
             int fps = atoi(argv[9]);
             timestampInterval = 1000000/fps;
             printf("\n\n ***Real time rending @%d fps**\n\n",fps);
           }

        }
   }
    else
    {
      int fps = 30;
      switch(file_type_option)
      {
          case FILE_TYPE_DAT_PER_AU:
          case FILE_TYPE_ARBITRARY_BYTES:
          case FILE_TYPE_264_NAL_SIZE_LENGTH:
          case FILE_TYPE_PICTURE_START_CODE:
          case FILE_TYPE_RCV:
          case FILE_TYPE_VC1:
          case FILE_TYPE_DIVX_4_5_6:
          case FILE_TYPE_DIVX_311:
          case FILE_TYPE_VP_6:
          {
              nalSize = 0;
              if ((file_type_option == FILE_TYPE_264_NAL_SIZE_LENGTH) ||
                  ((codec_format_option == CODEC_FORMAT_H264) && (file_type_option == FILE_TYPE_ARBITRARY_BYTES)))
              {
                printf(" Enter Nal length size [2 or 4] \n");
                if (file_type_option == FILE_TYPE_ARBITRARY_BYTES)
                {
                  printf(" Enter 0 if it is a start code based clip\n");
                }
                fflush(stdin);
                scanf("%d", &nalSize);
                fflush(stdin);
                if ((file_type_option == FILE_TYPE_264_NAL_SIZE_LENGTH) &&
                    (nalSize == 0))
                {
                  printf("Error - Can't pass NAL length size = 0\n");
                  return -1;
                }
              }

              height=144;width=176; // Assume Default as QCIF
              printf("Executing DynPortReconfig QCIF 144 x 176 \n");
              break;
          }

          default:
          {
              printf(" Wrong test case...[%d] \n",file_type_option);
              return -1;
          }
      }

      printf(" *********************************************\n");
      printf(" Output buffer option:\n");
      printf(" *********************************************\n");
      printf(" 0 --> No display and no YUV log\n");
      printf(" 1 --> Diplay YUV\n");
      printf(" 2 --> Take YUV log\n");
      printf(" 3 --> Display YUV and take YUV log\n");
      fflush(stdin);
      scanf("%d", &outputOption);
      fflush(stdin);

      printf(" *********************************************\n");
      printf(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      printf(" 1 --> Play the clip till the end\n");
      printf(" 2 --> Run compliance test. Do NOT expect any display for most option. \n");
      printf("       Please only see \"TEST SUCCESSFULL\" to indidcate test pass\n");
      fflush(stdin);
      scanf("%d", &test_option);
      fflush(stdin);

      printf(" *********************************************\n");
      printf(" ENTER THE PORTION OF DISPLAY TO USE\n");
      printf(" *********************************************\n");
      printf(" 0 --> Entire Screen\n");
      printf(" 1 --> 1/4 th of the screen starting from top left corner to middle \n");
      printf(" 2 --> 1/4 th of the screen starting from middle to top right corner \n");
      printf(" 3 --> 1/4 th of the screen starting from middle to bottom left \n");
      printf(" 4 --> 1/4 th of the screen starting from middle to bottom right \n");
      printf("       Please only see \"TEST SUCCESSFULL\" to indidcate test pass\n");
      fflush(stdin);
      scanf("%d", &displayWindow);
      fflush(stdin);

      if (file_type_option != FILE_TYPE_ARBITRARY_BYTES) {
        printf(" *********************************************\n");
        printf(" DO YOU WANT TEST APP TO DISPLAY IT BASED ON FPS (Real time) \n");
        printf(" 0 --> NO\n 1 --> YES\n");
        printf(" Warning: For H264, it require one NAL per frame clip.\n");
        printf("          For Arbitrary bytes option, Real time display is not recommend\n");
        printf(" *********************************************\n");
        fflush(stdin);
        scanf("%d", &realtime_display);
        fflush(stdin);
      }


      if (realtime_display)
      {
        printf(" *********************************************\n");
        printf(" ENTER THE CLIP FPS\n");
        printf(" Exception: VC1 Simple and Main Profile will be based on the timestamp at RCV file.\n");
        printf(" *********************************************\n");
        fflush(stdin);
        scanf("%d", &fps);
        fflush(stdin);
        timestampInterval = 1000000/fps;
        printf("\n\n ***Real time rending @%d fps**\n\n",fps);
      }


    }


    if (outputOption == 0)
    {
      displayYuv = 0;
      takeYuvLog = 0;
    }
    else if (outputOption == 1)
    {
      displayYuv = 1;
    }
    else if (outputOption == 2)
    {
      takeYuvLog = 1;
    }
    else if (outputOption == 3)
    {
      displayYuv = 1;
      takeYuvLog = 1;
    }
    else
    {
      printf("Wrong option. Assume you want to see the YUV display\n");
      displayYuv = 1;
    }

    if (test_option == 2)
    {
      printf(" *********************************************\n");
      printf(" ENTER THE COMPLIANCE TEST YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      printf(" 1 --> Call Free Handle at the OMX_StateLoaded\n");
      printf(" 2 --> Call Free Handle at the OMX_StateIdle\n");
      printf(" 3 --> Call Free Handle at the OMX_StateExecuting\n");
      printf(" 4 --> Call Free Handle at the OMX_StatePause\n");
      fflush(stdin);
      scanf("%d", &freeHandle_option);
      fflush(stdin);
    }
    else
    {
      freeHandle_option = (freeHandle_test)0;
    }

    printf("Input values: inputfilename[%s]\n", in_filename);
    printf("*******************************************************\n");
    pthread_cond_init(&cond, 0);
	pthread_cond_init(&eos_cond, 0);
	pthread_mutex_init(&eos_lock, 0);
    pthread_mutex_init(&lock, 0);
    pthread_mutex_init(&etb_lock, 0);
    pthread_mutex_init(&fbd_lock, 0);
    if (-1 == sem_init(&etb_sem, 0, 0))
    {
      printf("Error - sem_init failed %d\n", errno);
    }
    if (-1 == sem_init(&fbd_sem, 0, 0))
    {
      printf("Error - sem_init failed %d\n", errno);
    }

    etb_queue = alloc_queue();
    if (etb_queue == NULL)
    {
      printf("\n Error in Creating etb_queue\n");
      return -1;
    }

    fbd_queue = alloc_queue();
    if (fbd_queue == NULL)
    {
      printf("\n Error in Creating fbd_queue\n");
      free_queue(etb_queue);
      return -1;
    }

    if(0 != pthread_create(&fbd_thread_id, NULL, fbd_thread, NULL))
    {
      printf("\n Error in Creating fbd_thread \n");
      free_queue(etb_queue);
      free_queue(fbd_queue);
      return -1;
    }

    if (displayYuv && (0 != pipe(fbiopan_pipe)))
    {
       printf("\n Error in Creating fbiopan_pipe \n");
       do_freeHandle_and_clean_up(true);
       return -1;
    }

    if(0 != pthread_create(&fbiopan_thread_id, NULL, fbiopan_thread, NULL))
    {
      printf("\n Error in Creating fbiopan_thread \n");
      do_freeHandle_and_clean_up(true);
      return -1;
    }

    if (displayYuv)
    {
      QPERF_RESET(render_fb);
#ifdef _ANDROID_
      fb_fd = open("/dev/graphics/fb0", O_RDWR);
#else
      fb_fd = open("/dev/fb0", O_RDWR);
#endif
      if (fb_fd < 0) {
          printf("[omx_vdec_test] - ERROR - can't open framebuffer!\n");
          return -1;
      }

      if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
          printf("[omx_vdec_test] - ERROR - can't retrieve vscreenInfo!\n");
          close(fb_fd);
          return -1;
      }
      if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
          printf("[omx_vdec_test] - ERROR - can't retrieve vscreenInfo!\n");
          close(fb_fd);
          return -1;
      }
    }

    run_tests();
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&lock);
    pthread_mutex_destroy(&etb_lock);
    pthread_mutex_destroy(&fbd_lock);
    pthread_cond_destroy(&eos_cond);
    pthread_mutex_destroy(&eos_lock);
    if (-1 == sem_destroy(&etb_sem))
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error - sem_destroy failed %d\n", errno);
    }
    if (-1 == sem_destroy(&fbd_sem))
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error - sem_destroy failed %d\n", errno);
    }

    if (displayYuv)
    {
      close(fb_fd);
      fb_fd = -1;
      QPERF_TERMINATE(render_fb);
    }
    QPERF_TERMINATE(client_decode);
    return 0;
}

int run_tests()
{
  QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s\n", __FUNCTION__);
  waitForPortSettingsChanged = 1;
  currentStatus = GOOD_STATE;

  if(file_type_option == FILE_TYPE_DAT_PER_AU) {
    Read_Buffer = Read_Buffer_From_DAT_File;
  }
  else if(file_type_option == FILE_TYPE_ARBITRARY_BYTES) {
    Read_Buffer = Read_Buffer_ArbitraryBytes;
  }
  else if(codec_format_option == CODEC_FORMAT_H264) {
    Read_Buffer = Read_Buffer_From_Size_Nal;
  }
  else if((codec_format_option == CODEC_FORMAT_H263) ||
          (codec_format_option == CODEC_FORMAT_MP4)  ||
          (codec_format_option == CODEC_FORMAT_SPARK0)  ||
          (codec_format_option == CODEC_FORMAT_SPARK1)  ||
          (file_type_option == FILE_TYPE_DIVX_4_5_6)) {
    Read_Buffer = Read_Buffer_From_Vop_Start_Code_File;
  }
  else if(file_type_option == FILE_TYPE_RCV) {
    Read_Buffer = Read_Buffer_From_RCV_File;
  }
  else if(file_type_option == FILE_TYPE_VC1) {
    Read_Buffer = Read_Buffer_From_VC1_File;
  }
  else if (file_type_option == FILE_TYPE_DIVX_311) {
    Read_Buffer = Read_Buffer_From_FrameSize_File;
  }
  else if (file_type_option == FILE_TYPE_VP_6) {
    Read_Buffer = Read_Buffer_From_FrameSize_File;
  }


  QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"file_type_option %d!\n", file_type_option);

  switch(file_type_option)
  {
    case FILE_TYPE_DAT_PER_AU:
    case FILE_TYPE_ARBITRARY_BYTES:
    case FILE_TYPE_264_NAL_SIZE_LENGTH:
    case FILE_TYPE_PICTURE_START_CODE:
    case FILE_TYPE_RCV:
    case FILE_TYPE_VC1:
    case FILE_TYPE_DIVX_4_5_6:
    case FILE_TYPE_DIVX_311:
    case FILE_TYPE_VP_6:
      if(Init_Decoder()!= 0x00)
      {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"Error - Decoder Init failed\n");
        return -1;
      }
      if(Play_Decoder() != 0x00)
      {
        return -1;
      }
      break;
    default:
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error - Invalid Entry...%d\n",file_type_option);
      break;
  }

  pthread_mutex_lock(&eos_lock);
  while (bOutputEosReached == false)
  {
    pthread_cond_wait(&eos_cond, &eos_lock);
  }
  pthread_mutex_unlock(&eos_lock);

  // Wait till EOS is reached...
    if(bOutputEosReached)
    {
      int bufCnt = 0;

      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Moving the decoder to idle state \n");
      OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
      wait_for_event();
      if (currentStatus == INVALID_STATE)
      {
        do_freeHandle_and_clean_up(true);
        return 0;
      }

      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Moving the decoder to loaded state \n");
      OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);

      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "[OMX Vdec Test] - Deallocating i/p and o/p buffers \n");
      for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
      {
        OMX_FreeBuffer(dec_handle, 0, pInputBufHdrs[bufCnt]);
      }

      for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt)
      {
        OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
      }

      fbd_cnt = 0; ebd_cnt=0;
      bInputEosReached = false;
      bOutputEosReached = false;

      wait_for_event();

      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - Free handle decoder\n");
      OMX_ERRORTYPE result = OMX_FreeHandle(dec_handle);
      if (result != OMX_ErrorNone)
      {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                      "[OMX Vdec Test] - Terminate: OMX_FreeHandle error. Error code: %d\n", result);
      }
      dec_handle = NULL;

      /* Deinit OpenMAX */
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - Terminate: De-initializing OMX \n");
      OMX_Deinit();

      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - Terminate: closing all files\n");
      if(inputBufferFile)
      {
      fclose(inputBufferFile);
          inputBufferFile = NULL;
      }


      if (takeYuvLog && outputBufferFile) {
        fclose(outputBufferFile);
        outputBufferFile = NULL;
      }

      if(etb_queue)
      {
        free_queue(etb_queue);
        etb_queue = NULL;
      }
      if(fbd_queue)
      {
        free_queue(fbd_queue);
        fbd_queue = NULL;
      }
      printf("*****************************************\n");
      printf("******...TEST SUCCESSFULL...*******\n");
      printf("*****************************************\n");

  }

  return 0;
}

int Init_Decoder()
{
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE omxresult;
    OMX_U32 total = 0;
    char vdecCompNames[50];
    typedef OMX_U8* OMX_U8_PTR;
    char *role ="video_decoder";

    static OMX_CALLBACKTYPE call_back = {&EventHandler, &EmptyBufferDone, &FillBufferDone};

    int i = 0;
    long bufCnt = 0;

    /* Init. the OpenMAX Core */
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nInitializing OpenMAX Core....\n");
    omxresult = OMX_Init();

    if(OMX_ErrorNone != omxresult) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "\n Failed to Init OpenMAX core");
        return -1;
    }
    else {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "\nOpenMAX Core Init Done\n");
    }

    /* Query for video decoders*/
    OMX_GetComponentsOfRole(role, &total, 0);
    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nTotal components of role=%s :%d", role, total);

    if(total)
    {
        /* Allocate memory for pointers to component name */
        OMX_U8** vidCompNames = (OMX_U8**)malloc((sizeof(OMX_U8*))*total);

        for (i = 0; i < total; ++i) {
            vidCompNames[i] = (OMX_U8*)malloc(sizeof(OMX_U8)*OMX_MAX_STRINGNAME_SIZE);
        }
        OMX_GetComponentsOfRole(role, &total, vidCompNames);
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nComponents of Role:%s\n", role);
        for (i = 0; i < total; ++i) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nComponent Name [%s]\n",vidCompNames[i]);
            free(vidCompNames[i]);
        }
        free(vidCompNames);
    }
    else {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                      "No components found with Role:%s", role);
    }

    if (codec_format_option == CODEC_FORMAT_H264)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.avc", 27);
    }
    else if (codec_format_option == CODEC_FORMAT_MP4)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.mpeg4", 29);
    }
    else if (codec_format_option == CODEC_FORMAT_H263)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.h263", 28);
    }
    else if (codec_format_option == CODEC_FORMAT_SPARK0 || codec_format_option == CODEC_FORMAT_SPARK1)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.spark", 29);
    }
    else if (codec_format_option == CODEC_FORMAT_VC1)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.vc1", 27);
    }
    else if (codec_format_option == CODEC_FORMAT_DIVX)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.divx", 28);
    }
    else if (codec_format_option == CODEC_FORMAT_VP)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.vp", 26);
    }
    else
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error: Unsupported codec %d\n", codec_format_option);
      return -1;
    }

    omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&dec_handle),
                              (OMX_STRING)vdecCompNames, NULL, &call_back);
    if (FAILED(omxresult)) {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                      "\nFailed to Load the component:%s\n", vdecCompNames);
        return -1;
    }
    else
    {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nComponent %s is in LOADED state\n", vdecCompNames);
    }

    /* Get the port information */
    CONFIG_VERSION_SIZE(portParam);
    omxresult = OMX_GetParameter(dec_handle, OMX_IndexParamVideoInit,
                                (OMX_PTR)&portParam);

    if(FAILED(omxresult)) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "ERROR - Failed to get Port Param\n");
        return -1;
    }
    else
    {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "portParam.nPorts:%d\n", portParam.nPorts);
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "portParam.nStartPortNumber:%d\n", portParam.nStartPortNumber);
    }

    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                 "Set parameter immediately followed by getparameter");
     if(is_use_egl_image) {
        portFmt.format.video.pNativeWindow = (void *)0xDEADBEAF;
    }
    omxresult = OMX_SetParameter(dec_handle,
                               OMX_IndexParamPortDefinition,
                               &portFmt);

    if(OMX_ErrorNone != omxresult)
    {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "ERROR - Set parameter failed");
    }

    /* Set the compression format on i/p port */
    if (codec_format_option == CODEC_FORMAT_H264)
    {
      portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    }
    else if (codec_format_option == CODEC_FORMAT_MP4)
    {
      portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
    }
    else if (codec_format_option == CODEC_FORMAT_H263)
    {
      portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingH263;
    }
    else if (codec_format_option == CODEC_FORMAT_SPARK0 || codec_format_option == CODEC_FORMAT_SPARK1)
    {
      //portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingSpark;
      portFmt.format.video.eCompressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingSpark;
    }
    else if (codec_format_option == CODEC_FORMAT_VC1)
    {
      portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingWMV;
    }
    else if (codec_format_option == CODEC_FORMAT_DIVX)
    {
      portFmt.format.video.eCompressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    }
    else if (codec_format_option == CODEC_FORMAT_VP)
    {
      portFmt.format.video.eCompressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingVp;
    }
    else
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error: Unsupported codec %d\n", codec_format_option);
    }


    return 0;
}

int Play_Decoder()
{
    int i, bufCnt;
    int frameSize=0;
 
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE ret;


    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));

    /* open the i/p and o/p files based on the video file format passed */
    if(open_video_file()) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "Error in opening video file\n");
        return -1;
    }

    OMX_QCOM_PARAM_PORTDEFINITIONTYPE inputPortFmt;
    memset(&inputPortFmt, 0, sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE));
    CONFIG_VERSION_SIZE(inputPortFmt);
    inputPortFmt.nPortIndex = 0;  // input port
    switch (file_type_option)
    {
      case FILE_TYPE_DAT_PER_AU:
      case FILE_TYPE_PICTURE_START_CODE:
      case FILE_TYPE_RCV:
      case FILE_TYPE_DIVX_4_5_6:
      case FILE_TYPE_DIVX_311:
      case FILE_TYPE_VP_6:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteFrame;
        break;
      }

      case FILE_TYPE_ARBITRARY_BYTES:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Arbitrary;
        break;
      }

      case FILE_TYPE_264_NAL_SIZE_LENGTH:
      case FILE_TYPE_VC1:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteSubFrame;
        break;
      }

      default:
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Unspecified;
    }
    OMX_SetParameter(dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexPortDefn,
                     (OMX_PTR)&inputPortFmt);

	OMX_VIDEO_PARAM_PORTFORMATTYPE colorFormat;
	memset(&colorFormat,0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	CONFIG_VERSION_SIZE(colorFormat);
	colorFormat.nPortIndex = 1;
	if (!is_yamato) {
	  colorFormat.eColorFormat = (OMX_COLOR_FORMATTYPE) OMX_QCOM_COLOR_FormatYVU420SemiPlanar;
          printf("color format nv21 %d\n", colorFormat.eColorFormat);
        }
	else {
	  colorFormat.eColorFormat = (OMX_COLOR_FORMATTYPE) QOMX_COLOR_FormatYVU420PackedSemiPlanar32m4ka;
          printf("color format nv21 yamato %d\n", colorFormat.eColorFormat);
        }
    OMX_SetParameter(dec_handle, (OMX_INDEXTYPE)OMX_IndexParamVideoPortFormat,
                     (OMX_PTR)&colorFormat);

    /* Query the decoder outport's min buf requirements */
    CONFIG_VERSION_SIZE(portFmt);

    /* Port for which the Client needs to obtain info */
    portFmt.nPortIndex = portParam.nStartPortNumber;

    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nDec: Min Buffer Count %d\n", portFmt.nBufferCountMin);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nDec: Buffer Size %d\n", portFmt.nBufferSize);

    if(OMX_DirInput != portFmt.eDir) {
        printf ("\nDec: Expect Input Port\n");
        return -1;
    }

    if(codec_format_option == CODEC_FORMAT_DIVX) {
        QOMX_VIDEO_PARAM_DIVXTYPE paramDivx;
        CONFIG_VERSION_SIZE(paramDivx);
        paramDivx.nPortIndex = 0;
        if(file_type_option == FILE_TYPE_DIVX_311) {
           int off;
           paramDivx.eFormat = QOMX_VIDEO_DIVXFormat311;
          off =  fread(&width, 1, 4, inputBufferFile);
          if (off == 0)
          {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "Failed to read width for divx\n");
            return  -1;
          }
          off =  fread(&height, 1, 4, inputBufferFile);
          if (off == 0)
          {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "Failed to read wodth for divx\n");
            return  -1;
          }

        }
        else if (file_type_option == FILE_TYPE_DIVX_4_5_6) {
           paramDivx.eFormat = QOMX_VIDEO_DIVXFormat4;
        }
        paramDivx.eProfile = QOMX_VIDEO_DivXProfileqMobile;
        OMX_SetParameter(dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoDivx,
                     (OMX_PTR)&paramDivx);
    }
    else if(codec_format_option == CODEC_FORMAT_VP) {
        QOMX_VIDEO_PARAM_VPTYPE paramVp;
        CONFIG_VERSION_SIZE(paramVp);
        paramVp.nPortIndex = 0;
        if(file_type_option == FILE_TYPE_VP_6) {
           paramVp.eFormat =  QOMX_VIDEO_VPFormat6;
        }
        paramVp.eProfile = QOMX_VIDEO_VPProfileAdvanced;
        OMX_SetParameter(dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoVp,
                     (OMX_PTR)&paramVp);
    } else
        if(codec_format_option == CODEC_FORMAT_SPARK0 || codec_format_option == CODEC_FORMAT_SPARK1) {
        QOMX_VIDEO_PARAM_SPARKTYPE paramSpark;
        CONFIG_VERSION_SIZE(paramSpark);
        paramSpark.nPortIndex = 0;
        if( codec_format_option == CODEC_FORMAT_SPARK1 ) {

            paramSpark.eFormat = QOMX_VIDEO_SparkFormat1;
        }
        else if (codec_format_option == CODEC_FORMAT_SPARK0 ) {

            paramSpark.eFormat = QOMX_VIDEO_SparkFormat0;
        }
        OMX_SetParameter(dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoSpark,
                     (OMX_PTR)&paramSpark);
    }


    bufCnt = 0;
    portFmt.format.video.nFrameHeight = height;
    portFmt.format.video.nFrameWidth  = width;
    if(is_use_egl_image) {
        portFmt.format.video.pNativeWindow = (void *)0xDEADBEAF;
    }
    OMX_SetParameter(dec_handle,OMX_IndexParamPortDefinition,
                                                       (OMX_PTR)&portFmt);
    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,
                                                               &portFmt);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nDec: New Min Buffer Count %d", portFmt.nBufferCountMin);


    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nVideo format, height = %d", portFmt.format.video.nFrameHeight);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nVideo format, height = %d\n", portFmt.format.video.nFrameWidth);
    if(codec_format_option == CODEC_FORMAT_H264)
    {
        OMX_VIDEO_CONFIG_NALSIZE naluSize;
        naluSize.nNaluBytes = nalSize;
        OMX_SetConfig(dec_handle,OMX_IndexConfigVideoNalSize,(OMX_PTR)&naluSize);
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"SETTING THE NAL SIZE to %d\n",naluSize.nNaluBytes);
    }
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nOMX_SendCommand Decoder -> IDLE\n");
    OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);

    input_buf_cnt = portFmt.nBufferCountMin;
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Transition to Idle State succesful...\n");
    /* Allocate buffer on decoder's i/p port */
    error = Allocate_Buffer(dec_handle, &pInputBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountMin, portFmt.nBufferSize);
    if (error != OMX_ErrorNone) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "Error - OMX_AllocateBuffer Input buffer error\n");
        return -1;
    }
    else {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\nOMX_AllocateBuffer Input buffer success\n");
    }

    portFmt.nPortIndex = portParam.nStartPortNumber+1;
    /* Port for which the Client needs to obtain info */

    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"nMin Buffer Count=%d", portFmt.nBufferCountMin);
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"nBuffer Size=%d", portFmt.nBufferSize);
    if(OMX_DirOutput != portFmt.eDir) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "Error - Expect Output Port\n");
        return -1;
    }

    if(!is_use_egl_image) {
    /* Allocate buffer on decoder's o/p port */
    error = Allocate_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountMin, portFmt.nBufferSize);
    }
    else {
        error = Use_EGL_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                                portFmt.nBufferCountMin, portFmt.nBufferSize, &egl_id);
    }

    if (error != OMX_ErrorNone) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "Error - OMX_AllocateBuffer Output buffer error\n");
        return -1;
    }
    else
    {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"OMX_AllocateBuffer Output buffer success\n");
    }

    wait_for_event();
    if (currentStatus == INVALID_STATE)
    {
      do_freeHandle_and_clean_up(true);
      return -1;
    }

    if (freeHandle_option == FREE_HANDLE_AT_IDLE)
    {
      OMX_STATETYPE state = OMX_StateInvalid;
      OMX_GetState(dec_handle, &state);
      if (state == OMX_StateIdle)
      {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "Decoder is in OMX_StateIdle and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
        do_freeHandle_and_clean_up(true);
      }
      return -1;
    }


    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                 "OMX_SendCommand Decoder -> Executing\n");
    OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
    wait_for_event();
    if (currentStatus == INVALID_STATE)
    {
      do_freeHandle_and_clean_up(true);
      return -1;
    }

    for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt) {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "OMX_FillThisBuffer on output buf no.%d\n",bufCnt);
        pOutYUVBufHdrs[bufCnt]->nOutputPortIndex = 1;
        pOutYUVBufHdrs[bufCnt]->nFlags &= ~OMX_BUFFERFLAG_EOS;
        ret = OMX_FillThisBuffer(dec_handle, pOutYUVBufHdrs[bufCnt]);
        if (OMX_ErrorNone != ret) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                          "Error - OMX_FillThisBuffer failed with result %d\n", ret);
        }
        else {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                         "OMX_FillThisBuffer success!\n");
        }
    }

    used_ip_buf_cnt = input_buf_cnt;

    rcv_v1 = 0;

    QPERF_START(client_decode);
    if ((codec_format_option == CODEC_FORMAT_VC1) && (file_type_option == FILE_TYPE_RCV))
    {
      pInputBufHdrs[0]->nOffset = 0;
      frameSize = Read_Buffer_From_RCV_File_Seq_Layer(pInputBufHdrs[0]);
      pInputBufHdrs[0]->nFilledLen = frameSize;
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "After Read_Buffer_From_RCV_File_Seq_Layer frameSize %d\n", frameSize);
      pInputBufHdrs[0]->nInputPortIndex = 0;
      pInputBufHdrs[0]->nOffset = 0;
      pInputBufHdrs[0]->nFlags = 0;
      //pBufHdr[bufCnt]->pAppPrivate = this;
      ret = OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[0]);
      if (OMX_ErrorNone != ret) {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                        "ERROR - OMX_EmptyThisBuffer failed with result %d\n", ret);
          do_freeHandle_and_clean_up(true);
          return -1;
      }
      else {
          QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                       "OMX_EmptyThisBuffer success!\n");
      }
      i = 1;
    }
    else
    {
      i = 0;
    }

    for (i; i < used_ip_buf_cnt;i++) {
      pInputBufHdrs[i]->nInputPortIndex = 0;
      pInputBufHdrs[i]->nOffset = 0;
      if((frameSize = Read_Buffer(pInputBufHdrs[i])) <= 0 ){
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,"NO FRAME READ\n");
        pInputBufHdrs[i]->nFilledLen = frameSize;
        pInputBufHdrs[i]->nInputPortIndex = 0;
        pInputBufHdrs[i]->nFlags |= OMX_BUFFERFLAG_EOS;;
        bInputEosReached = true;

        OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[i]);
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                 "File is small::Either EOS or Some Error while reading file\n");
        break;
      }
      pInputBufHdrs[i]->nFilledLen = frameSize;
      pInputBufHdrs[i]->nInputPortIndex = 0;
      pInputBufHdrs[i]->nFlags = 0;
//pBufHdr[bufCnt]->pAppPrivate = this;
      ret = OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[i]);
      if (OMX_ErrorNone != ret) {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                        "ERROR - OMX_EmptyThisBuffer failed with result %d\n", ret);
          do_freeHandle_and_clean_up(true);
          return -1;
      }
      else {
          QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                       "OMX_EmptyThisBuffer success!\n");
      }
    }

    if(0 != pthread_create(&ebd_thread_id, NULL, ebd_thread, NULL))
    {
      printf("\n Error in Creating fbd_thread \n");
      free_queue(etb_queue);
      free_queue(fbd_queue);
      return -1;
    }

    // wait for event port settings changed event
    wait_for_event();
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "RECIEVED EVENT PORT TO DETERMINE IF DYN PORT RECONFIGURATION NEEDED, currentStatus %d\n",
                  currentStatus);
    if (currentStatus == INVALID_STATE)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                   "Error - INVALID_STATE\n");
      do_freeHandle_and_clean_up(true);
      return -1;
    }
    else if (currentStatus == PORT_SETTING_CHANGE_STATE)
    {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                     "PORT_SETTING_CHANGE_STATE\n");
        // Send DISABLE command
        sent_disabled = 1;
        OMX_SendCommand(dec_handle, OMX_CommandPortDisable, 1, 0);

        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"FREEING BUFFERS\n");
        // Free output Buffer
        for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt) {
            OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
            if(is_use_egl_image && egl_id) {
                struct pmem arena;
                arena.fd = egl_id[bufCnt].pmem_fd;
                arena.size = pOutYUVBufHdrs[bufCnt]->nAllocLen;
                pmem_free(&arena);
            }
        }

        if(egl_id) {
            free(egl_id);
            egl_id = NULL;
        }

        // wait for Disable event to come back
        wait_for_event();
        if (currentStatus == INVALID_STATE)
        {
          do_freeHandle_and_clean_up(true);
          return -1;
        }
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"DISABLE EVENT RECD\n");
        // GetParam and SetParam

        // Send Enable command
        OMX_SendCommand(dec_handle, OMX_CommandPortEnable, 1, 0);
        // AllocateBuffers
        /* Allocate buffer on decoder's o/p port */

        portFmt.nPortIndex = 1;
        /* Port for which the Client needs to obtain info */

        OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Min Buffer Count=%d", portFmt.nBufferCountMin);
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Buffer Size=%d", portFmt.nBufferSize);
        if(OMX_DirOutput != portFmt.eDir) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                         "Error - Expect Output Port\n");
            return -1;
        }

         if(!is_use_egl_image) {
            /* Allocate buffer on decoder's o/p port */
        error = Allocate_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                                portFmt.nBufferCountMin, portFmt.nBufferSize);
         }
         else {
            error = Use_EGL_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                                portFmt.nBufferCountMin, portFmt.nBufferSize, &egl_id);
         }

        if (error != OMX_ErrorNone) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                         "Error - OMX_AllocateBuffer Output buffer error\n");
            return -1;
        }
        else
        {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                         "OMX_AllocateBuffer Output buffer success\n");
        }

        // wait for enable event to come back
        wait_for_event();
        if (currentStatus == INVALID_STATE)
        {
          do_freeHandle_and_clean_up(true);
          return -1;
        }
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"ENABLE EVENT HANDLER RECD\n");

        for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"OMX_FillThisBuffer on output buf no.%d\n",bufCnt);
            pOutYUVBufHdrs[bufCnt]->nOutputPortIndex = 1;
            pOutYUVBufHdrs[bufCnt]->nFlags &= ~OMX_BUFFERFLAG_EOS;
            ret = OMX_FillThisBuffer(dec_handle, pOutYUVBufHdrs[bufCnt]);
            if (OMX_ErrorNone != ret) {
                QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                              "ERROR - OMX_FillThisBuffer failed with result %d\n", ret);
            }
            else {
                QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"OMX_FillThisBuffer success!\n");
            }
        }
    }

    if (freeHandle_option == FREE_HANDLE_AT_EXECUTING)
    {
      OMX_STATETYPE state = OMX_StateInvalid;
      OMX_GetState(dec_handle, &state);
      if (state == OMX_StateExecuting)
      {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "Decoder is in OMX_StateExecuting and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
        do_freeHandle_and_clean_up(true);
      }
      return -1;
    }
    else if (freeHandle_option == FREE_HANDLE_AT_PAUSE)
    {
      OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StatePause,0);
      wait_for_event();

      OMX_STATETYPE state = OMX_StateInvalid;
      OMX_GetState(dec_handle, &state);
      if (state == OMX_StatePause)
      {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                   "Decoder is in OMX_StatePause and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
        do_freeHandle_and_clean_up(true);
      }
      return -1;
    }

    return 0;
}

static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize)
{
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;

    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"pBufHdrs = %x,bufCntMin = %d\n", pBufHdrs, bufCntMin);
    *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
                   malloc(sizeof(OMX_BUFFERHEADERTYPE)*bufCntMin);

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"OMX_AllocateBuffer No %d \n", bufCnt);
        error = OMX_AllocateBuffer(dec_handle, &((*pBufHdrs)[bufCnt]),
                                   nPortIndex, NULL, bufSize);
    }

    return error;
}

static OMX_ERRORTYPE Use_EGL_Buffer ( OMX_COMPONENTTYPE *dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize,
                                      struct use_egl_id **egl)
{
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;
    struct use_egl_id *egl_info;
    struct pmem arena;

    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"pBufHdrs = %x,bufCntMin = %d\n", pBufHdrs, bufCntMin);
    *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
                   malloc(sizeof(OMX_BUFFERHEADERTYPE)*bufCntMin);
    *egl = (struct use_egl_id *)
                   malloc(sizeof(struct use_egl_id) * bufCntMin);

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
        egl_info = *egl+bufCnt;
        if (pmem_alloc (&arena, bufSize))
        {
           QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"OMX_Use_EGL_Buffer No pmem %d \n", bufSize);
           return OMX_ErrorInsufficientResources;
        }
        egl_info->pmem_fd = arena.fd;
        egl_info->offset =  0;

        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"OMX_AllocateBuffer No %d \n", bufCnt);
        error = OMX_UseEGLImage(dec_handle, &((*pBufHdrs)[bufCnt]),
                                   nPortIndex, NULL, (void *) egl_info);
    }

    return error;
}

static void do_freeHandle_and_clean_up(bool isDueToError)
{
    int bufCnt = 0;

    if(isDueToError)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Moving the decoder to idle state \n");
       OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);

       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Moving the decoder to loaded state \n");
       OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);
    }

    for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
    {
        OMX_FreeBuffer(dec_handle, 0, pInputBufHdrs[bufCnt]);
    }

    for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt)
    {
        OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
        if(is_use_egl_image && egl_id) {
                struct pmem arena;
                arena.fd = egl_id[bufCnt].pmem_fd;
                arena.size = pOutYUVBufHdrs[bufCnt]->nAllocLen;
                pmem_free(&arena);
            }
    }
    if(egl_id) {
        free(egl_id);
        egl_id = NULL;
    }

    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - Free handle decoder\n");
    OMX_ERRORTYPE result = OMX_FreeHandle(dec_handle);
    if (result != OMX_ErrorNone)
    {
       QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                     "[OMX Vdec Test] - OMX_FreeHandle error. Error code: %d\n", result);
    }
    dec_handle = NULL;

    /* Deinit OpenMAX */
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - De-initializing OMX \n");
    OMX_Deinit();

    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - closing all files\n");
    if(inputBufferFile)
    {
    fclose(inputBufferFile);
       inputBufferFile = NULL;
    }

    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - after free inputfile\n");

    if (takeYuvLog && outputBufferFile) {
        fclose(outputBufferFile);
        outputBufferFile = NULL;
    }
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - after free outputfile\n");

    if(etb_queue)
    {
      free_queue(etb_queue);
      etb_queue = NULL;
    }
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - after free etb_queue \n");
    if(fbd_queue)
    {
      free_queue(fbd_queue);
      fbd_queue = NULL;
    }
    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"[OMX Vdec Test] - after free iftb_queue\n");

    if(fbiopan_pipe[0])
      close(fbiopan_pipe[0]);
    if(fbiopan_pipe[1])
      close(fbiopan_pipe[1]);

    printf("*****************************************\n");
    if (isDueToError)
    {
      printf("************...TEST FAILED...************\n");
    }
    else
    {
      printf("**********...TEST SUCCESSFULL...*********\n");
    }
    printf("*****************************************\n");
}

static int Read_Buffer_From_DAT_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    long frameSize=0;
    char temp_buffer[10];
    char temp_byte;
    int bytes_read=0;
    int i=0;
    unsigned char *read_buffer=NULL;
    char c = '1'; //initialize to anything except '\0'(0)
    char inputFrameSize[10];
    int count =0; char cnt =0;
    memset(temp_buffer, 0, sizeof(temp_buffer));

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Inside %s \n", __FUNCTION__);

    while (cnt < 10)
    /* Check the input file format, may result in infinite loop */
    {
        QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"loop[%d] count[%d]\n",cnt,count);
        count  = fread(&inputFrameSize[cnt], 1, 1, inputBufferFile);
        if(inputFrameSize[cnt] == '\0' )
          break;
        cnt++;
    }
    inputFrameSize[cnt]='\0';
    frameSize = atoi(inputFrameSize);
    pBufHdr->nFilledLen = 0;

    /* get the frame length */
    fseek(inputBufferFile, -1, SEEK_CUR);
    bytes_read = fread(pBufHdr->pBuffer, 1, frameSize,  inputBufferFile);

    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Actual frame Size [%d] bytes_read using fread[%d]\n",
                  frameSize, bytes_read);

    if(bytes_read == 0 || bytes_read < frameSize ) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                     "Bytes read Zero After Read frame Size \n");
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Checking VideoPlayback Count:video_playback_count is:%d\n",
                       video_playback_count);
        return 0;
    }
    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;
    return bytes_read;
}

static int Read_Buffer_ArbitraryBytes(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    char temp_buffer[10];
    char temp_byte;
    int bytes_read=0;
    int i=0;
    unsigned char *read_buffer=NULL;
    char c = '1'; //initialize to anything except '\0'(0)
    char inputFrameSize[10];
    int count =0; char cnt =0;
    memset(temp_buffer, 0, sizeof(temp_buffer));

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    bytes_read = fread(pBufHdr->pBuffer, 1, NUMBER_OF_ARBITRARYBYTES_READ,  inputBufferFile);

    if(bytes_read == 0) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                     "Bytes read Zero After Read frame Size \n");
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Checking VideoPlayback Count:video_playback_count is:%d\n",
                      video_playback_count);
        return 0;
    }
    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;
    return bytes_read;
}

static int Read_Buffer_From_Vop_Start_Code_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int readOffset = 0;
    unsigned int ret = 0;
    int bytes_read = 0;
    unsigned int code = 0;
    pBufHdr->nFilledLen = 0;
    static unsigned int header_code = 0;
    unsigned char data;

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    do
    {
      //Start codes are always byte aligned.
      if(readOffset <= pBufHdr->nAllocLen) {
      bytes_read = fread(&pBufHdr->pBuffer[readOffset],1, 1,inputBufferFile);
         data = pBufHdr->pBuffer[readOffset];
      }
      else {
         bytes_read = fread(&data,1, 1,inputBufferFile);
      }
      if(!bytes_read)
      {
          QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Bytes read Zero \n");
          break;
      }
      code <<= 8;
      code |= (0x000000FF & data);
      //VOP start code comparision
      if (readOffset>3)
      {
        if(!header_code ){
          if( VOP_START_CODE == code)
          {
            header_code = VOP_START_CODE;
          }
          else if ( (0xFFFFFC00 & code) == SHORT_HEADER_START_CODE )
          {
            header_code = SHORT_HEADER_START_CODE;
          }
          else if ( (0xFFFFFC00 & code) == SPARK1_START_CODE )
          {
            header_code = SPARK1_START_CODE;
          }
        }
        if ((header_code == VOP_START_CODE) && (code == VOP_START_CODE))
        {
          //Seek backwards by 4
          fseek(inputBufferFile, -4, SEEK_CUR);
          readOffset-=3;
          break;

        }
        else if ( (( header_code == SHORT_HEADER_START_CODE ) && ( SHORT_HEADER_START_CODE == (code & 0xFFFFFC00))) ||
                  (( header_code == SPARK1_START_CODE ) && ( SPARK1_START_CODE == (code & 0xFFFFFC00))) )
        {
          //Seek backwards by 4
          fseek(inputBufferFile, -4, SEEK_CUR);
          readOffset-=3;
          break;
        }
      }
      readOffset++;
    }while (1);

    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;

    ret = ((readOffset > pBufHdr->nAllocLen)?pBufHdr->nAllocLen:readOffset);
    return ret;
}

static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    // NAL unit stream processing
    char temp_size[SIZE_NAL_FIELD_MAX];
    int i = 0;
    int j = 0;
    unsigned int size = 0, readSize = 0;   // Need to make sure that uint32 has SIZE_NAL_FIELD_MAX (4) bytes
    int bytes_read = 0;

    // read the "size_nal_field"-byte size field
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset, 1, nalSize, inputBufferFile);
    if (bytes_read == 0)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "Failed to read frame or it might be EOF\n");
      return 0;
    }

    for (i=0; i<SIZE_NAL_FIELD_MAX-nalSize; i++)
    {
      temp_size[SIZE_NAL_FIELD_MAX - 1 - i] = 0;
    }

    /* Due to little endiannes, Reorder the size based on size_nal_field */
    for (j=0; i<SIZE_NAL_FIELD_MAX; i++, j++)
    {
      temp_size[SIZE_NAL_FIELD_MAX - 1 - i] = pBufHdr->pBuffer[pBufHdr->nOffset + j];
    }
    size = (unsigned int)(*((unsigned int *)(temp_size)));

    readSize =( (size > pBufHdr->nAllocLen)?pBufHdr->nAllocLen:size);

    // now read the data
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset + nalSize, 1, readSize, inputBufferFile);
    if (bytes_read != readSize) 
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR, "Failed to read frame\n");
    }
    if(readSize < size)
    {
      /* reseek to beginning of sequence header */
       fseek(inputBufferFile, size-bytes_read, SEEK_CUR);
    }
    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;

    return bytes_read + nalSize;
}

static int Read_Buffer_From_FrameSize_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int  size = 0, readSize =0;
    unsigned int readOffset = 0;
    unsigned char* pBuf = pBufHdr->pBuffer + pBufHdr->nOffset;

    // read the vop size bytes
    readOffset = fread(&size, 1, 4, inputBufferFile);
    if (readOffset != 4)
    {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,"ReadBufferUsingVopSize failed to read vop size  bytes\n");
        return 0;
    }

     QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"vop Size=%d bytes_read=%d \n",size,readOffset);

    readSize =( (size > pBufHdr->nAllocLen)?pBufHdr->nAllocLen:size);
    // read the vop
    readOffset = fread(pBuf, 1, readSize, inputBufferFile);
    if (readOffset != readSize)
    {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,"ReadBufferUsingVopSize failed to read vop %d bytes\n", size);
        return 0;
    }
    if(readSize < size)
    {
      /* reseek to beginning of next frame */
       fseek(inputBufferFile, size-readOffset, SEEK_CUR);
    }

    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;;
    return readOffset;
}
static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int readOffset = 0, size_struct_C = 0;
    unsigned int startcode = 0;
    pBufHdr->nFilledLen = 0;
    pBufHdr->nFlags = 0;

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    fread(&startcode, 4, 1, inputBufferFile);

    /* read size of struct C as it need not be 4 always*/
    fread(&size_struct_C, 1, 4, inputBufferFile);

    /* reseek to beginning of sequence header */
    fseek(inputBufferFile, -8, SEEK_CUR);

    if ((startcode & 0xFF000000) == 0xC5000000)
    {

      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "Read_Buffer_From_RCV_File_Seq_Layer size_struct_C: %d\n", size_struct_C);

      readOffset = fread(pBufHdr->pBuffer, 1, VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC + size_struct_C, inputBufferFile);
    }
    else if((startcode & 0xFF000000) == 0x85000000)
    {
      // .RCV V1 file

      rcv_v1 = 1;

      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "Read_Buffer_From_RCV_File_Seq_Layer size_struct_C: %d\n", size_struct_C);

      readOffset = fread(pBufHdr->pBuffer, 1, VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC + size_struct_C, inputBufferFile);
    }
    else
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                    "Error: Unknown VC1 clip format %x\n", startcode);
    }

#if 0
    {
      int i=0;
      printf("Read_Buffer_From_RCV_File, length %d readOffset %d\n", readOffset, readOffset);
      for (i=0; i<36; i++)
      {
        printf("0x%.2x ", pBufHdr->pBuffer[i]);
        if (i%16 == 15) {
          printf("\n");
        }
      }
      printf("\n");
    }
#endif
    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;
    return readOffset;
}

static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int readOffset = 0;
    unsigned int len = 0;
    unsigned int key = 0;
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Read_Buffer_From_RCV_File - nOffset %d\n", pBufHdr->nOffset);
    if(rcv_v1)
    {
      /* for the case of RCV V1 format, the frame header is only of 4 bytes and has
         only the frame size information */
      readOffset = fread(&len, 1, 4, inputBufferFile);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "Read_Buffer_From_RCV_File - framesize %d %x\n", len, len);

    }
    else
    {
      /* for a regular RCV file, 3 bytes comprise the frame size and 1 byte for key*/
      readOffset = fread(&len, 1, 3, inputBufferFile);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Read_Buffer_From_RCV_File - framesize %d %x\n", len, len);

      readOffset = fread(&key, 1, 1, inputBufferFile);
      if ( (key & 0x80) == false)
      {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "Read_Buffer_From_RCV_File - Non IDR frame key %x\n", key);
       }

    }

    if(!rcv_v1)
    {
      /* There is timestamp field only for regular RCV format and not for RCV V1 format*/
      readOffset = fread(&pBufHdr->nTimeStamp, 1, 4, inputBufferFile);
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Read_Buffer_From_RCV_File - timeStamp %d\n", pBufHdr->nTimeStamp);
    }

    if(len > pBufHdr->nAllocLen)
    {
       QTV_MSG_PRIO3(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"Error in sufficient buffer framesize %d, allocalen %d noffset %d\n",len,pBufHdr->nAllocLen, pBufHdr->nOffset);
       readOffset = fread(pBufHdr->pBuffer+pBufHdr->nOffset, 1, pBufHdr->nAllocLen - pBufHdr->nOffset , inputBufferFile);
       fseek(inputBufferFile, len - readOffset,SEEK_CUR);
       return readOffset;
    }
    else
    readOffset = fread(pBufHdr->pBuffer+pBufHdr->nOffset, 1, len, inputBufferFile);
    if (readOffset != len)
    {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "EOS reach or Reading error %d, %s \n", readOffset, strerror( errno ));
      return 0;
    }

#if 0
    {
      int i=0;
      printf("Read_Buffer_From_RCV_File, length %d readOffset %d\n", len, readOffset);
      for (i=0; i<64; i++)
      {
        printf("0x%.2x ", pBufHdr->pBuffer[i]);
        if (i%16 == 15) {
          printf("\n");
        }
      }
      printf("\n");
    }
#endif

    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;

    return readOffset;
}

static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    static int timeStampLfile = 0;
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    unsigned int readOffset = 0;
    int bytes_read = 0;
    unsigned int code = 0;
    pBufHdr->nFilledLen = 0;

    do
    {
      //Start codes are always byte aligned.
      bytes_read = fread(&pBufHdr->pBuffer[readOffset],1, 1,inputBufferFile);
      if(!bytes_read)
      {
          QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\n Bytes read Zero \n");
          break;
      }
      code <<= 8;
      code |= (0x000000FF & pBufHdr->pBuffer[readOffset]);
      //VOP start code comparision
      if (readOffset>3)
      {
        if (VC1_START_CODE == (code & 0xFFFFFF00))
        {
          //Seek backwards by 4
          fseek(inputBufferFile, -4, SEEK_CUR);
          readOffset-=3;

          while(pBufHdr->pBuffer[readOffset-1] == 0)
            readOffset--;

          break;
        }
      }
      readOffset++;
    }while (1);

    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += 100;

#if 0
    {
      int i=0;
      printf("Read_Buffer_From_VC1_File, readOffset %d\n", readOffset);
      for (i=0; i<64; i++)
      {
        printf("0x%.2x ", pBufHdr->pBuffer[i]);
        if (i%16 == 15) {
          printf("\n");
        }
      }
      printf("\n");
    }
#endif
    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;

    return readOffset;
}

static int open_video_file ()
{
    int error_code = 0;
    char outputfilename[512];
    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Inside %s filename=%s\n", __FUNCTION__, in_filename);

    inputBufferFile = fopen (in_filename, "rb");
    if (inputBufferFile == NULL) {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                      "Error - i/p file %s could NOT be opened\n",
		                  in_filename);
        printf("Error - i/p file %s could NOT be opened\n",in_filename);

        error_code = -1;
    }
    else {
        QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"I/p file %s is opened \n", in_filename);
    }

    if (takeYuvLog) {
        strncpy(outputfilename, "yuvframes.yuv", 14);
        outputBufferFile = fopen (outputfilename, "wb");
        if (outputBufferFile == NULL)
        {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                        "ERROR - o/p file %s could NOT be opened\n", outputfilename);
          error_code = -1;
        }
        else
        {
          QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                        "O/p file %s is opened \n", outputfilename);
          printf ("\n** yuv frames will be logged into %s **\n",outputfilename);
        }
    }
    return error_code;
}

void swap_byte(char *pByte, int nbyte)
{
  int i=0;

  for (i=0; i<nbyte/2; i++)
  {
    pByte[i] ^= pByte[nbyte-i-1];
    pByte[nbyte-i-1] ^= pByte[i];
    pByte[i] ^= pByte[nbyte-i-1];
  }
}

void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr)
{
    unsigned int addr = 0;
    OMX_OTHER_EXTRADATATYPE *pExtraData = 0;
    OMX_QCOM_EXTRADATA_FRAMEINFO *pExtraFrameInfo = 0;
    OMX_QCOM_EXTRADATA_FRAMEDIMENSION *pExtraFrameDimension = 0;
    OMX_QCOM_EXTRADATA_CODEC_DATA *pExtraCodecData = 0;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;
    unsigned int destx, desty,destW, destH,scale,version;
    struct use_egl_id *egl_info = NULL;
#ifdef _ANDROID_
    MemoryHeapBase *vheap = NULL;
#endif

    unsigned int end = (unsigned int)(pBufHdr->pBuffer + pBufHdr->nAllocLen);

    struct mdp_blit_req *e;
    union {
             char dummy[sizeof(struct mdp_blit_req_list) +
			   sizeof(struct mdp_blit_req) * 1];
             struct mdp_blit_req_list list;
    } img;

    if (fb_fd < 0)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                 "Warning: /dev/fb0 is not opened!\n");
       return;
    }

    if(is_use_egl_image) {
        egl_info = (struct use_egl_id *)pBufHdr->pBuffer;
    }

    img.list.count = 1;
    e = &img.list.req[0];


    if(!is_use_egl_image) {
    addr = (unsigned int)(pBufHdr->pBuffer + pBufHdr->nFilledLen);
    // align to a 4 byte boundary
    addr = (addr + 3) & (~3);

    // read to the end of existing extra data sections
    pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;

    while (addr < end && pExtraData->eType != OMX_ExtraDataNone)
    {
        if (pExtraData->eType == OMX_ExtraDataFrameInfo)
        {
           pExtraFrameInfo = (OMX_QCOM_EXTRADATA_FRAMEINFO *)pExtraData->data;
        }
        if (pExtraData->eType == OMX_ExtraDataFrameDimension)
        {
           pExtraFrameDimension = (OMX_QCOM_EXTRADATA_FRAMEDIMENSION *)pExtraData->data;
        }

        if (pExtraData->eType == OMX_ExtraDataH264)
        {
           pExtraCodecData = (OMX_QCOM_EXTRADATA_CODEC_DATA *)pExtraData->data;
        }

        addr += pExtraData->nSize;
        pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;
    }

    if (pExtraData->eType == OMX_ExtraDataNone)
    {
       QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "pExtraData->eType %d pExtraData->nSize %d\n",pExtraData->eType,pExtraData->nSize);
    }

    if (pExtraCodecData)
       QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_LOW,"Extra Data Codec Data TimeStamp %d\n",pExtraCodecData->h264ExtraData.seiTimeStamp);

    if (pExtraFrameDimension)
    {
       QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Extra Data FrameDimension DecWidth %d DecHeight %d\n",pExtraFrameDimension->nDecWidth,pExtraFrameDimension->nDecHeight);
       QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Extra Data FrameDimension CropWidth %d CropHeight %d\n",pExtraFrameDimension->nActualWidth,pExtraFrameDimension->nActualHeight);
    }

    }
    if (pBufHdr->nOffset)
       QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"pBufHdr->nOffset = %d \n",pBufHdr->nOffset);


    pPMEMInfo  = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                    pBufHdr->pPlatformPrivate)->entryList->entry;
#ifdef _ANDROID_
    vheap = (MemoryHeapBase *)pPMEMInfo->pmem_fd;
#endif


    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "DecWidth %d DecHeight %d\n",portFmt.format.video.nStride,portFmt.format.video.nSliceHeight);
    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "DispWidth %d DispHeight %d\n",portFmt.format.video.nFrameWidth,portFmt.format.video.nFrameHeight);


    if(pExtraFrameDimension)
    {
       e->src.width = pExtraFrameDimension->nDecWidth;
       e->src.height = pExtraFrameDimension->nDecHeight;
    }else{
	e->src.width = portFmt.format.video.nStride;
	e->src.height = portFmt.format.video.nSliceHeight;
    }

    if (is_yamato)
    {   int i1,yamato_chroma_offset;
        int nv21_chroma_offset=e->src.width * e->src.height;
        int yamato_chroma_w=2*(((e->src.width >> 1) + 31) & ~31);
        yamato_chroma_offset = ( nv21_chroma_offset + 4095) & ~4095;

        for (i1 = 0; i1 < e->src.height>>1; i1++)
        {
            memcpy(&pBufHdr->pBuffer[nv21_chroma_offset],&pBufHdr->pBuffer[yamato_chroma_offset],e->src.width);
            nv21_chroma_offset+=e->src.width; yamato_chroma_offset+=yamato_chroma_w;
        }
    }

    e->src.format = MDP_Y_CBCR_H2V2;
    if(is_use_egl_image) {
      e->src.offset = egl_info->offset;
      e->src.memory_id = egl_info->pmem_fd;
    }
    else {
    e->src.offset = pPMEMInfo->offset;
#ifdef _ANDROID_
    e->src.memory_id = vheap->getHeapID();
#else
    e->src.memory_id = pPMEMInfo->pmem_fd;
#endif
    }

    QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "pmemOffset %d pmemID %d\n",e->src.offset,e->src.memory_id);

    e->dst.width = (finfo.line_length * 8) / (vinfo.bits_per_pixel);
    e->dst.height = vinfo.yres;

    e->dst.format = MDP_RGBA_8888;
    e->dst.offset = 0;
    e->dst.memory_id = fb_fd;

    e->transp_mask = 0xffffffff;
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                  "Frame interlace type %d!\n", pExtraFrameInfo->interlaceType);
    if(pExtraFrameInfo && pExtraFrameInfo->interlaceType != OMX_QCOM_InterlaceFrameProgressive)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                  "Intrelaced Frame!\n");
       e->flags = MDP_DEINTERLACE;
    }
    else
      e->flags = 0;
	e->alpha = 0xff;

    switch(displayWindow)
    {
    case 1: destx = 0;
            desty = 0;
            destW = vinfo.xres/2;
            destH = vinfo.yres/2;
            break;
    case 2: destx = vinfo.xres/2;
            desty = 0;
            destW = vinfo.xres/2;
            destH = vinfo.yres/2;
            break;

    case 3: destx = 0;
            desty = vinfo.yres/2;
            destW = vinfo.xres/2;
            destH = vinfo.yres/2;
            break;
     case 4: destx = vinfo.xres/2;
            desty = vinfo.yres/2;
            destW = vinfo.xres/2;
            destH = vinfo.yres/2;
            break;
     case 0:
     default:
            destx = 0;
            desty = 0;
            /* MDP driver version 3.1 supports up to 8X scaling */
            version = (finfo.id[5] + finfo.id[6]);
            /* if MDP version is above 3.1 then return unsupported version */
            if(version > MDP_VERSION_3_1)
            {
              QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
              "Unsupported Display driver version%c.%c \n",finfo.id[5] , finfo.id[6]);
              return;
            }/* if MDP version is 3.1 then set 8x as max scaling factor */
            else if(version == MDP_VERSION_3_1)   scale = 3;
            /* if MDP version is less then 3.1 then set 4x as max scaling factor */
            else  scale = 2;

            destW = (e->src.width<<scale);
            destH = (e->src.height<<scale);
            destW = (vinfo.xres > destW) ? destW : vinfo.xres;
            destH = (vinfo.yres > destH) ? destH : vinfo.yres;
    }


    e->dst_rect.x = destx;
    e->dst_rect.y = desty;
    e->dst_rect.w = destW;
    e->dst_rect.h = destH;

    //e->dst_rect.w = 800;
   //e->dst_rect.h = 800;

    e->src_rect.x = 0;
    e->src_rect.y = 0;

    if(pExtraFrameDimension)
    {
      e->src_rect.w = pExtraFrameDimension->nActualWidth;
      e->src_rect.h = pExtraFrameDimension->nActualHeight;
    }else{
      e->src_rect.w = portFmt.format.video.nFrameWidth;
      e->src_rect.h = portFmt.format.video.nFrameHeight;
    }

    //e->src_rect.w = portFmt.format.video.nStride;
    //e->src_rect.h = portFmt.format.video.nSliceHeight;


    if (ioctl(fb_fd, MSMFB_BLIT, &img)) {
	QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"MSMFB_BLIT ioctl failed!\n");
	return;
    }
}


void* fbiopan_thread(void* pArg)
{
  int ioresult = 0;
  char fbiopan_signal = 0;

  while(1)
  {
    ioresult = read(fbiopan_pipe[0], &fbiopan_signal, 1);
    if(ioresult <= 0)
    {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,"\n Error in reading from fbiopan PIPE %d \n", ioresult);
      return NULL;
    }

    vinfo.activate = FB_ACTIVATE_VBL;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;

    if(ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo) < 0)
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_LOW,"FBIOPAN_DISPLAY: Failed\n");
    }

    QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"render_fb complete!\n");
  }
  return NULL;
}

