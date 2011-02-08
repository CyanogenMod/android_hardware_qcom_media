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
/*
    An Open max test application ....
*/

#define LOG_TAG "OMX-VDEC-TEST"

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
#include "OMX_QCOMExtns.h"

#ifdef _ANDROID_
#include <binder/MemoryHeapBase.h>

extern "C"{
#include<utils/Log.h>
}
#define DEBUG_PRINT
#define DEBUG_PRINT_ERROR

#else
#define DEBUG_PRINT printf
#define DEBUG_PRINT_ERROR printf
#endif /* _ANDROID_ */

#include "OMX_Core.h"
#include "OMX_Component.h"
#include "OMX_QCOMExtns.h"
extern "C" {
#include "queue.h"
}

#include <linux/msm_mdp.h>
#include <linux/fb.h>
//#include "qutility.h"

#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#define DEBUG_PRINT_ERROR(...) printf(__VA_ARGS__)
#define DEBUG_PRINT_LOW(...) printf(__VA_ARGS__)

/************************************************************************/
/*              #DEFINES                            */
/************************************************************************/
#define DELAY 66
#define false 0
#define true 1
#define VOP_START_CODE 0x000001B6
#define SHORT_HEADER_START_CODE 0x00008000
#define VC1_START_CODE  0x00000100
#define VC1_FRAME_START_CODE  0x0000010D
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

/************************************************************************/
/*              GLOBAL DECLARATIONS                     */
/************************************************************************/
#ifdef _ANDROID_
using namespace android;
#endif

typedef enum {
  CODEC_FORMAT_H264 = 1,
  CODEC_FORMAT_MP4,
  CODEC_FORMAT_H263,
  CODEC_FORMAT_VC1,
  CODEC_FORMAT_MAX = CODEC_FORMAT_VC1
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
  FILE_TYPE_VC1
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

static int (*Read_Buffer)(OMX_BUFFERHEADERTYPE  *pBufHdr );

FILE * inputBufferFile;
FILE * outputBufferFile;
FILE * seqFile;
int takeYuvLog = 0;
int displayWindow = 0;
int realtime_display = 0;
struct timeval t_avsync={0,0};

Queue *etb_queue = NULL;
Queue *fbd_queue = NULL;

pthread_t ebd_thread_id;
pthread_t fbd_thread_id;
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
sem_t seq_sem;
sem_t in_flush_sem, out_flush_sem;

OMX_PARAM_PORTDEFINITIONTYPE portFmt;
OMX_PORT_PARAM_TYPE portParam;
OMX_ERRORTYPE error;

#define CLR_KEY  0xe8fd
#define COLOR_BLACK_RGBA_8888 0x00000000
#define FRAMEBUFFER_32

static int fb_fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int vid_buf_front_id;
int overlay_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr);
void overlay_set();
void overlay_unset();
void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr);

/************************************************************************/
/*              GLOBAL INIT                 */
/************************************************************************/
unsigned int input_buf_cnt = 0;
int height =0, width =0;
int sliceheight = 0, stride = 0;
int used_ip_buf_cnt = 0;
volatile int event_is_done = 0;
int ebd_cnt, fbd_cnt;
int bInputEosReached = 0;
int bOutputEosReached = 0;
char in_filename[512];
char seq_file_name[512];
unsigned char seq_enabled = 0, flush_in_progress = 0;
unsigned int cmd_data = 0, etb_count = 0;;

char curr_seq_command[100];
OMX_S64 timeStampLfile = 0;
int timestampInterval = 33333;
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

/* Performance related variable*/
//QPERF_INIT(render_fb);
//QPERF_INIT(client_decode);

/************************************************************************/
/*              GLOBAL FUNC DECL                        */
/************************************************************************/
int Init_Decoder();
int Play_Decoder();
int run_tests();

/**************************************************************************/
/*              STATIC DECLARATIONS                       */
/**************************************************************************/
static int video_playback_count = 1;
static int open_video_file ();
static int Read_Buffer_From_DAT_File(OMX_BUFFERHEADERTYPE  *pBufHdr );
static int Read_Buffer_ArbitraryBytes(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_Vop_Start_Code_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBufHdr);

static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *dec_handle,
                                       OMX_BUFFERHEADERTYPE  ***pBufHdrs,
                                       OMX_U32 nPortIndex,
                                       long bufCntMin, long bufSize);


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
    DEBUG_PRINT("Waiting for event\n");
    pthread_mutex_lock(&lock);
    while (event_is_done == 0) {
        pthread_cond_wait(&cond, &lock);
    }
    event_is_done = 0;
    pthread_mutex_unlock(&lock);
    DEBUG_PRINT("Running .... get the event\n");
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
int get_next_command(FILE *seq_file)
{
    int i = -1;
    do{
        i++;
        if(fread(&curr_seq_command[i], 1, 1, seq_file) != 1)
            return -1;
    }while(curr_seq_command[i] != '\n');
    curr_seq_command[i] = 0;
    printf("\n cmd_str = %s", curr_seq_command);
    return 0;
}

void process_current_command(const char *seq_command)
{
    char *data_str = NULL;
    unsigned int data = 0, bufCnt = 0, i = 0;
    int frameSize;
    OMX_ERRORTYPE ret;

    if(strstr(seq_command, "pause") == seq_command)
    {
        printf("\n\n $$$$$   PAUSE    $$$$$");
        data_str = (char*)seq_command + strlen("pause") + 1;
        data = atoi(data_str);
        printf("\n After frame number %u", data);
        cmd_data = data;
        sem_wait(&seq_sem);
        printf("\n Sending PAUSE cmd to OMX compt");
        OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StatePause,0);
        wait_for_event();
        printf("\n EventHandler for PAUSE DONE");
    }
    else if(strstr(seq_command, "sleep") == seq_command)
    {
        printf("\n\n $$$$$   SLEEP    $$$$$");
        data_str = (char*)seq_command + strlen("sleep") + 1;
        data = atoi(data_str);
        printf("\n Sleep Time = %u ms", data);
        usleep(data*1000);
    }
    else if(strstr(seq_command, "resume") == seq_command)
    {
        printf("\n\n $$$$$   RESUME    $$$$$");
        printf("\n Immediate effect");
        printf("\n Sending PAUSE cmd to OMX compt");
        OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
        wait_for_event();
        printf("\n EventHandler for RESUME DONE");
    }
    else if(strstr(seq_command, "flush") == seq_command)
    {
        printf("\n\n $$$$$   FLUSH    $$$$$");
        data_str = (char*)seq_command + strlen("flush") + 1;
        data = atoi(data_str);
        printf("\n After frame number %u", data);
        cmd_data = data;
        sem_wait(&seq_sem);
        printf("\n Sending FLUSH cmd to OMX compt");
        flush_in_progress = 1;
        OMX_SendCommand(dec_handle, OMX_CommandFlush, OMX_ALL, 0);
        wait_for_event();
        flush_in_progress = 0;
        printf("\n EventHandler for FLUSH DONE");
        printf("\n Post EBD_thread flush sem");
        sem_post(&in_flush_sem);
        printf("\n Post FBD_thread flush sem");
        sem_post(&out_flush_sem);
    }
    else
    {
        printf("\n\n $$$$$   INVALID CMD    $$$$$");
        printf("\n seq_command[%s] is invalid", seq_command);
        seq_enabled = 0;
    }
}

void* ebd_thread(void* pArg)
{
  while(currentStatus != INVALID_STATE)
  {
    int readBytes =0;
    OMX_BUFFERHEADERTYPE* pBuffer = NULL;

    if(flush_in_progress)
    {
        printf("\n EBD_thread flush wait start");
        sem_wait(&in_flush_sem);
        printf("\n EBD_thread flush wait complete");
    }

    sem_wait(&etb_sem);
    pthread_mutex_lock(&etb_lock);
    pBuffer = (OMX_BUFFERHEADERTYPE *) pop(etb_queue);
    pthread_mutex_unlock(&etb_lock);
    if(pBuffer == NULL)
    {
      DEBUG_PRINT_ERROR("Error - No etb pBuffer to dequeue\n");
      continue;
    }

    pBuffer->nOffset = 0;
    if((readBytes = Read_Buffer(pBuffer)) > 0) {
        pBuffer->nFilledLen = readBytes;
        OMX_EmptyThisBuffer(dec_handle,pBuffer);
        etb_count++;
        if(cmd_data == etb_count)
        {
            sem_post(&seq_sem);
            printf("\n Posted seq_sem");
        }
    }
    else
    {
        pBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
        bInputEosReached = true;
        pBuffer->nFilledLen = readBytes;
        OMX_EmptyThisBuffer(dec_handle,pBuffer);
        DEBUG_PRINT("EBD::Either EOS or Some Error while reading file\n");
        etb_count++;
        if(cmd_data == etb_count)
        {
            sem_post(&seq_sem);
            printf("\n Posted seq_sem");
        }
        break;
    }
  }
  return NULL;
}

void* fbd_thread(void* pArg)
{
  while(currentStatus != INVALID_STATE)
  {
    long current_avsync_time = 0, delta_time = 0;
    int canDisplay = 1;
    static int contigous_drop_frame = 0;
    static long base_avsync_time = 0;
    static long base_timestamp = 0;
    long lipsync_time = 250000;
    int bytes_written = 0;
    OMX_BUFFERHEADERTYPE *pBuffer;

    if(flush_in_progress)
    {
        printf("\n FBD_thread flush wait start");
        sem_wait(&out_flush_sem);
        printf("\n FBD_thread flush wait complete");
    }

    sem_wait(&fbd_sem);
    DEBUG_PRINT("Inside %s fbd_cnt[%d] \n", __FUNCTION__, fbd_cnt);

    fbd_cnt++;
    pthread_mutex_lock(&fbd_lock);
    pBuffer = (OMX_BUFFERHEADERTYPE *) pop(fbd_queue);
    pthread_mutex_unlock(&fbd_lock);
    if (pBuffer == NULL)
    {
      DEBUG_PRINT("Error - No pBuffer to dequeue\n");
      continue;
    }

    /*********************************************
    Write the output of the decoder to the file.
    *********************************************/

    if (sent_disabled)
    {
       DEBUG_PRINT("Ignoring FillBufferDone\n");
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
        pthread_mutex_lock(&fbd_lock);
        delta_time = (current_avsync_time - base_avsync_time) - ((long)pBuffer->nTimeStamp - base_timestamp);
        if (delta_time < 0 )
        {
          DEBUG_PRINT_ERROR("Sleep %d us. AV Sync time is left behind\n",
                 -delta_time);
          usleep(-delta_time);
          canDisplay = 1;
        }
        else if ((delta_time>lipsync_time) && (contigous_drop_frame < 6))
        {
          DEBUG_PRINT_ERROR("Error - Drop the frame at the renderer. Video frame with ts %lu usec behind by %ld usec"
                         ", pBuffer->nFilledLen %u\n",
                        (unsigned long)pBuffer->nTimeStamp, delta_time, pBuffer->nFilledLen);
          canDisplay = 0;
          contigous_drop_frame++;
        }
        else
    {
          canDisplay = 1;
        }
        pthread_mutex_unlock(&fbd_lock);
      }
      else
      {
        base_avsync_time = current_avsync_time;
        base_timestamp = (long)pBuffer->nTimeStamp;
      }
    }

    if (!flush_in_progress && takeYuvLog) {
        pthread_mutex_lock(&fbd_lock);
        bytes_written = fwrite((const char *)pBuffer->pBuffer,
                                pBuffer->nFilledLen,1,outputBufferFile);
        pthread_mutex_unlock(&fbd_lock);
        if (bytes_written < 0) {
            DEBUG_PRINT("\nFillBufferDone: Failed to write to the file\n");
        }
        else {
            DEBUG_PRINT("\nFillBufferDone: Wrote %d YUV bytes to the file\n",
                          bytes_written);
        }
    }

    /********************************************************************/
    /* De-Initializing the open max and relasing the buffers and */
    /* closing the files.*/
    /********************************************************************/
    if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS ) {
      DEBUG_PRINT("***************************************************\n");
      DEBUG_PRINT("FillBufferDone: End Of Stream Reached\n");
      DEBUG_PRINT("***************************************************\n");
      pthread_mutex_lock(&eos_lock);
      bOutputEosReached = true;
      pthread_cond_broadcast(&eos_cond);
      pthread_mutex_unlock(&eos_lock);
      //QPERF_END(client_decode);
      //QPERF_SET_ITERATION(client_decode, fbd_cnt);
      DEBUG_PRINT("***************************************************\n");
      DEBUG_PRINT("FBD_THREAD bOutputEosReached %d\n",bOutputEosReached);
      break;
    }
    OMX_FillThisBuffer(dec_handle, pBuffer);
  }
  return NULL;
}

OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData,
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{
    DEBUG_PRINT("Function %s \n", __FUNCTION__);

    switch(eEvent) {
        case OMX_EventCmdComplete:
            DEBUG_PRINT("\n OMX_EventCmdComplete \n");
            // check nData1 for DISABLE event
            if(OMX_CommandPortDisable == (OMX_COMMANDTYPE)nData1)
            {
                DEBUG_PRINT("*********************************************\n");
                DEBUG_PRINT("Recieved DISABLE Event Command Complete[%d]\n",nData2);
                DEBUG_PRINT("*********************************************\n");
                sent_disabled = 0;
            }
            else if(OMX_CommandPortEnable == (OMX_COMMANDTYPE)nData1)
            {
                DEBUG_PRINT("*********************************************\n");
                DEBUG_PRINT("Recieved ENABLE Event Command Complete[%d]\n",nData2);
                DEBUG_PRINT("*********************************************\n");
            }
            currentStatus = GOOD_STATE;
            event_complete();
            break;

        case OMX_EventError:
            DEBUG_PRINT("OMX_EventError \n");
            currentStatus = ERROR_STATE;
            if (OMX_ErrorInvalidState == (OMX_ERRORTYPE)nData1 ||
                OMX_ErrorHardware == (OMX_ERRORTYPE)nData1)
            {
              DEBUG_PRINT("Invalid State or hardware error \n");
              currentStatus = INVALID_STATE;
              if(event_is_done == 0)
              {
                DEBUG_PRINT("Event error in the middle of Decode \n");
                pthread_mutex_lock(&eos_lock);
                bOutputEosReached = true;
                pthread_cond_broadcast(&eos_cond);
                pthread_mutex_unlock(&eos_lock);

              }
            }

            event_complete();
            break;
        case OMX_EventPortSettingsChanged:
            DEBUG_PRINT("OMX_EventPortSettingsChanged port[%d]\n",nData1);
            waitForPortSettingsChanged = 0;
            currentStatus = PORT_SETTING_CHANGE_STATE;
            // reset the event
            event_complete();
            break;

        default:
            DEBUG_PRINT_ERROR("ERROR - Unknown Event \n");
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

    DEBUG_PRINT("Function %s cnt[%d]\n", __FUNCTION__, ebd_cnt);
    ebd_cnt++;


    if(bInputEosReached) {
        DEBUG_PRINT("*****EBD:Input EoS Reached************\n");
        return OMX_ErrorNone;
    }

    pthread_mutex_lock(&etb_lock);
    if(push(etb_queue, (void *) pBuffer) < 0)
    {
       DEBUG_PRINT_ERROR("Error in enqueue  ebd data\n");
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
    DEBUG_PRINT("Inside %s callback_count[%d] \n", __FUNCTION__, fbd_cnt);

    /* Test app will assume there is a dynamic port setting
     * In case that there is no dynamic port setting, OMX will not call event cb,
     * instead OMX will send empty this buffer directly and we need to clear an event here
     */
    if(waitForPortSettingsChanged)
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
         DEBUG_PRINT_ERROR("Error in enqueueing fbd_data\n");
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

      printf(" *********************************************\n");
      printf(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      printf(" 1--> H264\n");
      printf(" 2--> MP4\n");
      printf(" 3--> H263\n");
      printf(" 4--> VC1\n");
      fflush(stdin);
      scanf("%d", (int *)&codec_format_option);
      fflush(stdin);

      if (codec_format_option > CODEC_FORMAT_MAX)
      {
          printf(" Wrong test case...[%d] \n", codec_format_option);
          return -1;
      }

      printf(" *********************************************\n");
      printf(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
      printf(" *********************************************\n");
      printf(" 1--> PER ACCESS UNIT CLIP (.dat). Clip only available for H264 and Mpeg4\n");
      printf(" 2--> ARBITRARY BYTES (need .264/.264c/.mv4/.263/.rcv/.vc1)\n");
      if (codec_format_option == CODEC_FORMAT_H264)
      {
        printf(" 3--> NAL LENGTH SIZE CLIP (.264c)\n");
      }
      else if ( (codec_format_option == CODEC_FORMAT_MP4) || (codec_format_option == CODEC_FORMAT_H263) )
      {
        printf(" 3--> MP4 VOP or H263 P0 SHORT HEADER START CODE CLIP (.m4v or .263)\n");
      }
      else if (codec_format_option == CODEC_FORMAT_VC1)
      {
        printf(" 3--> VC1 clip Simple/Main Profile (.rcv)\n");
        printf(" 4--> VC1 clip Advance Profile (.vc1)\n");
      }
      fflush(stdin);
      scanf("%d", (int *)&file_type_option);
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
          file_type_option = (file_type)(FILE_TYPE_START_OF_MP4_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        case CODEC_FORMAT_VC1:
          file_type_option = (file_type)(FILE_TYPE_START_OF_VC1_SPECIFIC + file_type_option - FILE_TYPE_COMMON_CODEC_MAX);
          break;
        default:
          printf("Error: Unknown code %d\n", codec_format_option);
      }
    }

    if(argc > 5)
    {
      outputOption = atoi(argv[4]);
      test_option = atoi(argv[5]);
      if (argc > 6)
      {
        nalSize = atoi(argv[6]);
      }
      else
      {
        nalSize = 0;
      }

      if(argc > 7)
      {
        displayWindow = atoi(argv[7]);
        if(displayWindow > 0)
        {
            printf(" Curently display window 0 only supported; ignoring other values\n");
            displayWindow = 0;
        }
      }
      else
      {
        displayWindow = 0;
      }

      if(file_type_option == FILE_TYPE_PICTURE_START_CODE ||
         file_type_option == FILE_TYPE_RCV ||
         (file_type_option == FILE_TYPE_VC1 && argc > 8))
      {
          realtime_display = atoi(argv[8]);
      }

      if(realtime_display)
      {
          takeYuvLog = 0;
          if(argc > 9)
          {
              int fps = atoi(argv[9]);
              timestampInterval = 1000000/fps;
          }
          else if(argc > 10)
          {
              strncpy(seq_file_name, argv[10], strlen(argv[10])+1);
          }
      }
      else
      {
          if(argc > 9)
          {
              strncpy(seq_file_name, argv[9], strlen(argv[9])+1);
          }
      }
      height=144;width=176; // Assume Default as QCIF
      sliceheight = 144;
      stride = 176;
      printf("Executing DynPortReconfig QCIF 144 x 176 \n");
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
                scanf("%d", &nalSize);
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

      if(displayWindow > 0)
      {
          printf(" Curently display window 0 only supported; ignoring other values\n");
          displayWindow = 0;
      }

      if((file_type_option == FILE_TYPE_PICTURE_START_CODE) ||
         (file_type_option == FILE_TYPE_RCV) ||
         (file_type_option == FILE_TYPE_VC1))
      {
          printf(" *********************************************\n");
          printf(" DO YOU WANT TEST APP TO RENDER in Real time \n");
          printf(" 0 --> NO\n 1 --> YES\n");
          printf(" Warning: For H264, it require one NAL per frame clip.\n");
          printf("          For Arbitrary bytes option, Real time display is not recommended\n");
          printf(" *********************************************\n");
          fflush(stdin);
          scanf("%d", &realtime_display);
          fflush(stdin);
      }


      if (realtime_display)
      {
          printf(" *********************************************\n");
          printf(" ENTER THE CLIP FPS\n");
          printf(" Exception: Timestamp extracted from clips will be used.\n");
          printf(" *********************************************\n");
          fflush(stdin);
          scanf("%d", &fps);
          fflush(stdin);
          timestampInterval = 1000000/fps;
      }
      printf(" *********************************************\n");
      printf(" ENTER THE SEQ FILE NAME\n");
      printf(" *********************************************\n");
      fflush(stdin);
      scanf("%[^\n]", (char *)&seq_file_name);
      fflush(stdin);
    }

    if (outputOption == 0)
    {
      takeYuvLog = 0;
      realtime_display = 0;
    }
    else if (outputOption == 1)
    {
      printf("Sorry, cannot display to screen\n");
      return -1;
    }
    else if (outputOption == 2)
    {
      takeYuvLog = 1;
      realtime_display = 0;
    }
    else if (outputOption == 3)
    {
      printf("Sorry, cannot display to screen\n");
      return -1;
    }
    else
    {
      printf("Wrong option. Assume you want to take YUV log\n");
      takeYuvLog = 1;
      realtime_display = 0;
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
      scanf("%d", (int *)&freeHandle_option);
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
    if (-1 == sem_init(&seq_sem, 0, 0))
    {
      printf("Error - sem_init failed %d\n", errno);
    }
    if (-1 == sem_init(&in_flush_sem, 0, 0))
    {
      printf("Error - sem_init failed %d\n", errno);
    }
    if (-1 == sem_init(&out_flush_sem, 0, 0))
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

    run_tests();
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&lock);
    pthread_mutex_destroy(&etb_lock);
    pthread_mutex_destroy(&fbd_lock);
    pthread_cond_destroy(&eos_cond);
    pthread_mutex_destroy(&eos_lock);
    if (-1 == sem_destroy(&etb_sem))
    {
      DEBUG_PRINT_ERROR("Error - sem_destroy failed %d\n", errno);
    }
    if (-1 == sem_destroy(&fbd_sem))
    {
      DEBUG_PRINT_ERROR("Error - sem_destroy failed %d\n", errno);
    }
    if (-1 == sem_destroy(&seq_sem))
    {
      DEBUG_PRINT_ERROR("Error - sem_destroy failed %d\n", errno);
    }
    if (-1 == sem_destroy(&in_flush_sem))
    {
      DEBUG_PRINT_ERROR("Error - sem_destroy failed %d\n", errno);
    }
    if (-1 == sem_destroy(&out_flush_sem))
    {
      DEBUG_PRINT_ERROR("Error - sem_destroy failed %d\n", errno);
    }
    //QPERF_TERMINATE(client_decode);
    return 0;
}

int run_tests()
{
  DEBUG_PRINT("Inside %s\n", __FUNCTION__);
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
          (codec_format_option == CODEC_FORMAT_MP4)) {
    Read_Buffer = Read_Buffer_From_Vop_Start_Code_File;
  }
  else if(file_type_option == FILE_TYPE_RCV) {
    Read_Buffer = Read_Buffer_From_RCV_File;
  }
  else if(file_type_option == FILE_TYPE_VC1) {
    Read_Buffer = Read_Buffer_From_VC1_File;
  }

  DEBUG_PRINT("file_type_option %d!\n", file_type_option);

  switch(file_type_option)
  {
    case FILE_TYPE_DAT_PER_AU:
    case FILE_TYPE_ARBITRARY_BYTES:
    case FILE_TYPE_264_NAL_SIZE_LENGTH:
    case FILE_TYPE_PICTURE_START_CODE:
    case FILE_TYPE_RCV:
    case FILE_TYPE_VC1:
      if(Init_Decoder()!= 0x00)
      {
        DEBUG_PRINT_ERROR("Error - Decoder Init failed\n");
        return -1;
      }
      if(Play_Decoder() != 0x00)
      {
        return -1;
      }
      break;
    default:
      DEBUG_PRINT_ERROR("Error - Invalid Entry...%d\n",file_type_option);
      break;
  }

  if(strlen(seq_file_name))
  {
        seqFile = fopen (seq_file_name, "rb");
        if (seqFile == NULL)
        {
            DEBUG_PRINT_ERROR("Error - Seq file %s could NOT be opened\n",
                              seq_file_name);
        }
        else
        {
            DEBUG_PRINT("Seq file %s is opened \n", seq_file_name);
            seq_enabled = 1;
        }
  }

  pthread_mutex_lock(&eos_lock);
  while (bOutputEosReached == false)
  {
    if(seq_enabled)
    {
        if(!get_next_command(seqFile))
        {
            process_current_command(curr_seq_command);
        }
        else
        {
            printf("\n Error in get_next_cmd or EOF");
            seq_enabled = 0;
        }
    }
    else
    {
        pthread_cond_wait(&eos_cond, &eos_lock);
    }
  }
  pthread_mutex_unlock(&eos_lock);

  // Wait till EOS is reached...
    if(bOutputEosReached)
    {
      unsigned int bufCnt = 0;

      DEBUG_PRINT("Moving the decoder to idle state \n");
      OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
      wait_for_event();
      if (currentStatus == INVALID_STATE)
      {
        do_freeHandle_and_clean_up(true);
        return 0;
      }

      DEBUG_PRINT("Moving the decoder to loaded state \n");
      OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);

      DEBUG_PRINT("[OMX Vdec Test] - Deallocating i/p and o/p buffers \n");
      for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
      {
        OMX_FreeBuffer(dec_handle, 0, pInputBufHdrs[bufCnt]);
      }

      for(bufCnt=0; bufCnt < portFmt.nBufferCountActual; ++bufCnt)
      {
        OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
      }

      fbd_cnt = 0; ebd_cnt=0;
      bInputEosReached = false;
      bOutputEosReached = false;

      wait_for_event();

      DEBUG_PRINT("[OMX Vdec Test] - Free handle decoder\n");
      OMX_ERRORTYPE result = OMX_FreeHandle(dec_handle);
      if (result != OMX_ErrorNone)
      {
        DEBUG_PRINT_ERROR("[OMX Vdec Test] - Terminate: OMX_FreeHandle error. Error code: %d\n", result);
      }
      dec_handle = NULL;

      /* Deinit OpenMAX */
      DEBUG_PRINT("[OMX Vdec Test] - Terminate: De-initializing OMX \n");
      OMX_Deinit();

      DEBUG_PRINT("[OMX Vdec Test] - Terminate: closing all files\n");
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
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE omxresult;
    OMX_U32 total = 0;
    char vdecCompNames[50];
    typedef OMX_U8* OMX_U8_PTR;
    char role[] ="video_decoder";

    static OMX_CALLBACKTYPE call_back = {&EventHandler, &EmptyBufferDone, &FillBufferDone};

    unsigned int i = 0;
    long bufCnt = 0;

    /* Init. the OpenMAX Core */
    DEBUG_PRINT("\nInitializing OpenMAX Core....\n");
    omxresult = OMX_Init();

    if(OMX_ErrorNone != omxresult) {
        DEBUG_PRINT_ERROR("\n Failed to Init OpenMAX core");
        return -1;
    }
    else {
        DEBUG_PRINT_ERROR("\nOpenMAX Core Init Done\n");
    }

    /* Query for video decoders*/
    OMX_GetComponentsOfRole(role, &total, 0);
    DEBUG_PRINT("\nTotal components of role=%s :%d", role, total);

    if(total)
    {
        /* Allocate memory for pointers to component name */
        OMX_U8** vidCompNames = (OMX_U8**)malloc((sizeof(OMX_U8*))*total);

        for (i = 0; i < total; ++i) {
            vidCompNames[i] = (OMX_U8*)malloc(sizeof(OMX_U8)*OMX_MAX_STRINGNAME_SIZE);
        }
        OMX_GetComponentsOfRole(role, &total, vidCompNames);
        DEBUG_PRINT("\nComponents of Role:%s\n", role);
        for (i = 0; i < total; ++i) {
            DEBUG_PRINT("\nComponent Name [%s]\n",vidCompNames[i]);
            free(vidCompNames[i]);
        }
        free(vidCompNames);
    }
    else {
        DEBUG_PRINT_ERROR("No components found with Role:%s", role);
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
    else if (codec_format_option == CODEC_FORMAT_VC1)
    {
      strncpy(vdecCompNames, "OMX.qcom.video.decoder.vc1", 27);
    }
    else
    {
      DEBUG_PRINT_ERROR("Error: Unsupported codec %d\n", codec_format_option);
      return -1;
    }

    omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&dec_handle),
                              (OMX_STRING)vdecCompNames, NULL, &call_back);
    if (FAILED(omxresult)) {
        DEBUG_PRINT_ERROR("\nFailed to Load the component:%s\n", vdecCompNames);
        return -1;
    }
    else
    {
        DEBUG_PRINT("\nComponent %s is in LOADED state\n", vdecCompNames);
    }

    QOMX_VIDEO_QUERY_DECODER_INSTANCES decoder_instances;
    omxresult = OMX_GetConfig(dec_handle,
                 (OMX_INDEXTYPE)OMX_QcomIndexQueryNumberOfVideoDecInstance,
                              &decoder_instances);
    DEBUG_PRINT("\n Number of decoder instances %d",
                      decoder_instances.nNumOfInstances);

    /* Get the port information */
    CONFIG_VERSION_SIZE(portParam);
    omxresult = OMX_GetParameter(dec_handle, OMX_IndexParamVideoInit,
                                (OMX_PTR)&portParam);

    if(FAILED(omxresult)) {
        DEBUG_PRINT_ERROR("ERROR - Failed to get Port Param\n");
        return -1;
    }
    else
    {
        DEBUG_PRINT("portParam.nPorts:%d\n", portParam.nPorts);
        DEBUG_PRINT("portParam.nStartPortNumber:%d\n", portParam.nStartPortNumber);
    }

    DEBUG_PRINT("Set parameter immediately followed by getparameter");
    omxresult = OMX_SetParameter(dec_handle,
                               OMX_IndexParamPortDefinition,
                               &portFmt);

    if(OMX_ErrorNone != omxresult)
    {
        DEBUG_PRINT_ERROR("ERROR - Set parameter failed");
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
    else if (codec_format_option == CODEC_FORMAT_VC1)
    {
      portFmt.format.video.eCompressionFormat = OMX_VIDEO_CodingWMV;
    }
    else
    {
      DEBUG_PRINT_ERROR("Error: Unsupported codec %d\n", codec_format_option);
    }


    return 0;
}

int Play_Decoder()
{
    int i;
    unsigned int bufCnt;
    int frameSize=0;
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE ret;

    DEBUG_PRINT("sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));

    /* open the i/p and o/p files based on the video file format passed */
    if(open_video_file()) {
        DEBUG_PRINT_ERROR("Error in opening video file\n");
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
      case FILE_TYPE_VC1:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteFrame;
        break;
      }

      case FILE_TYPE_ARBITRARY_BYTES:
      case FILE_TYPE_264_NAL_SIZE_LENGTH:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Arbitrary;
        break;
      }

      default:
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Unspecified;
    }
    OMX_SetParameter(dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexPortDefn,
                     (OMX_PTR)&inputPortFmt);

    /* Query the decoder outport's min buf requirements */
    CONFIG_VERSION_SIZE(portFmt);

    /* Port for which the Client needs to obtain info */
    portFmt.nPortIndex = portParam.nStartPortNumber;

    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    DEBUG_PRINT("\nDec: Min Buffer Count %d\n", portFmt.nBufferCountMin);
    DEBUG_PRINT("\nDec: Buffer Size %d\n", portFmt.nBufferSize);

    if(OMX_DirInput != portFmt.eDir) {
        printf ("\nDec: Expect Input Port\n");
        return -1;
    }

    bufCnt = 0;
    portFmt.format.video.nFrameHeight = height;
    portFmt.format.video.nFrameWidth  = width;
    OMX_SetParameter(dec_handle,OMX_IndexParamPortDefinition,
                                                       (OMX_PTR)&portFmt);
    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,
                                                               &portFmt);
    DEBUG_PRINT("\nDec: New Min Buffer Count %d", portFmt.nBufferCountMin);


    DEBUG_PRINT("\nVideo format, height = %d", portFmt.format.video.nFrameHeight);
    DEBUG_PRINT("\nVideo format, height = %d\n", portFmt.format.video.nFrameWidth);
    if(codec_format_option == CODEC_FORMAT_H264)
    {
        OMX_VIDEO_CONFIG_NALSIZE naluSize;
        naluSize.nNaluBytes = nalSize;
        DEBUG_PRINT("\n Nal length is %d index %d",nalSize,OMX_IndexConfigVideoNalSize);
        OMX_SetConfig(dec_handle,OMX_IndexConfigVideoNalSize,(OMX_PTR)&naluSize);
        DEBUG_PRINT("SETTING THE NAL SIZE to %d\n",naluSize.nNaluBytes);
    }
    DEBUG_PRINT("\nOMX_SendCommand Decoder -> IDLE\n");
    OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);

    input_buf_cnt = portFmt.nBufferCountActual;
    DEBUG_PRINT("Transition to Idle State succesful...\n");
    /* Allocate buffer on decoder's i/p port */
    error = Allocate_Buffer(dec_handle, &pInputBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountActual, portFmt.nBufferSize);
    if (error != OMX_ErrorNone) {
        DEBUG_PRINT_ERROR("Error - OMX_AllocateBuffer Input buffer error\n");
        return -1;
    }
    else {
        DEBUG_PRINT("\nOMX_AllocateBuffer Input buffer success\n");
    }

    portFmt.nPortIndex = portParam.nStartPortNumber+1;
    /* Port for which the Client needs to obtain info */

    OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    DEBUG_PRINT("nMin Buffer Count=%d", portFmt.nBufferCountMin);
    DEBUG_PRINT("nBuffer Size=%d", portFmt.nBufferSize);
    if(OMX_DirOutput != portFmt.eDir) {
        DEBUG_PRINT_ERROR("Error - Expect Output Port\n");
        return -1;
    }

    /* Allocate buffer on decoder's o/p port */
    error = Allocate_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountActual, portFmt.nBufferSize);
    if (error != OMX_ErrorNone) {
        DEBUG_PRINT_ERROR("Error - OMX_AllocateBuffer Output buffer error\n");
        return -1;
    }
    else
    {
        DEBUG_PRINT("OMX_AllocateBuffer Output buffer success\n");
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
        DEBUG_PRINT("Decoder is in OMX_StateIdle and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        DEBUG_PRINT_ERROR("Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
        do_freeHandle_and_clean_up(true);
      }
      return -1;
    }


    DEBUG_PRINT("OMX_SendCommand Decoder -> Executing\n");
    OMX_SendCommand(dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
    wait_for_event();
    if (currentStatus == INVALID_STATE)
    {
      do_freeHandle_and_clean_up(true);
      return -1;
    }

    for(bufCnt=0; bufCnt < portFmt.nBufferCountActual; ++bufCnt) {
        DEBUG_PRINT("OMX_FillThisBuffer on output buf no.%d\n",bufCnt);
        pOutYUVBufHdrs[bufCnt]->nOutputPortIndex = 1;
        pOutYUVBufHdrs[bufCnt]->nFlags &= ~OMX_BUFFERFLAG_EOS;
        ret = OMX_FillThisBuffer(dec_handle, pOutYUVBufHdrs[bufCnt]);
        if (OMX_ErrorNone != ret) {
            DEBUG_PRINT_ERROR("Error - OMX_FillThisBuffer failed with result %d\n", ret);
        }
        else {
            DEBUG_PRINT("OMX_FillThisBuffer success!\n");
        }
    }

    used_ip_buf_cnt = input_buf_cnt;

    rcv_v1 = 0;

    //QPERF_START(client_decode);
    if (codec_format_option == CODEC_FORMAT_VC1)
    {
      pInputBufHdrs[0]->nOffset = 0;
      if(file_type_option == FILE_TYPE_RCV)
      {
      frameSize = Read_Buffer_From_RCV_File_Seq_Layer(pInputBufHdrs[0]);
      pInputBufHdrs[0]->nFilledLen = frameSize;
          DEBUG_PRINT("After Read_Buffer_From_RCV_File_Seq_Layer, "
              "frameSize %d\n", frameSize);
      }
      else if(file_type_option == FILE_TYPE_VC1)
      {
          pInputBufHdrs[0]->nFilledLen = Read_Buffer(pInputBufHdrs[0]);
          DEBUG_PRINT_ERROR("After 1st Read_Buffer for VC1, "
              "pInputBufHdrs[0]->nFilledLen %d\n", pInputBufHdrs[0]->nFilledLen);
      }
      else
      {
          pInputBufHdrs[0]->nFilledLen = Read_Buffer(pInputBufHdrs[0]);
          DEBUG_PRINT("After Read_Buffer pInputBufHdrs[0]->nFilledLen %d\n",
              pInputBufHdrs[0]->nFilledLen);
      }

      pInputBufHdrs[0]->nInputPortIndex = 0;
      pInputBufHdrs[0]->nOffset = 0;
      pInputBufHdrs[0]->nFlags = 0;

      ret = OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[0]);
      if (ret != OMX_ErrorNone)
      {
          DEBUG_PRINT_ERROR("ERROR - OMX_EmptyThisBuffer failed with result %d\n", ret);
          do_freeHandle_and_clean_up(true);
          return -1;
      }
      else
      {
          etb_count++;
          DEBUG_PRINT("OMX_EmptyThisBuffer success!\n");
          if(cmd_data == etb_count)
          {
            sem_post(&seq_sem);
            printf("\n Posted seq_sem");
          }
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
        DEBUG_PRINT("NO FRAME READ\n");
        pInputBufHdrs[i]->nFilledLen = frameSize;
        pInputBufHdrs[i]->nInputPortIndex = 0;
        pInputBufHdrs[i]->nFlags |= OMX_BUFFERFLAG_EOS;;
        bInputEosReached = true;

        OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[i]);
        etb_count++;
        if(cmd_data == etb_count)
        {
            sem_post(&seq_sem);
            printf("\n Posted seq_sem");
        }
        DEBUG_PRINT("File is small::Either EOS or Some Error while reading file\n");
        break;
      }
      pInputBufHdrs[i]->nFilledLen = frameSize;
      pInputBufHdrs[i]->nInputPortIndex = 0;
      pInputBufHdrs[i]->nFlags = 0;
//pBufHdr[bufCnt]->pAppPrivate = this;
      ret = OMX_EmptyThisBuffer(dec_handle, pInputBufHdrs[i]);
      if (OMX_ErrorNone != ret) {
          DEBUG_PRINT_ERROR("ERROR - OMX_EmptyThisBuffer failed with result %d\n", ret);
          do_freeHandle_and_clean_up(true);
          return -1;
      }
      else {
          DEBUG_PRINT("OMX_EmptyThisBuffer success!\n");
          etb_count++;
          if(cmd_data == etb_count)
          {
            sem_post(&seq_sem);
            printf("\n Posted seq_sem");
          }
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
    DEBUG_PRINT("RECIEVED EVENT PORT TO DETERMINE IF DYN PORT RECONFIGURATION NEEDED, currentStatus %d\n",
                  currentStatus);
    if (currentStatus == INVALID_STATE)
    {
      DEBUG_PRINT_ERROR("Error - INVALID_STATE\n");
      do_freeHandle_and_clean_up(true);
      return -1;
    }
    else if (currentStatus == PORT_SETTING_CHANGE_STATE)
    {
        DEBUG_PRINT("PORT_SETTING_CHANGE_STATE\n");
        // Send DISABLE command
        sent_disabled = 1;
        OMX_SendCommand(dec_handle, OMX_CommandPortDisable, 1, 0);

        DEBUG_PRINT("FREEING BUFFERS\n");
        // Free output Buffer
        for(bufCnt=0; bufCnt < portFmt.nBufferCountActual; ++bufCnt) {
            OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
        }

        // wait for Disable event to come back
        wait_for_event();
        if (currentStatus == INVALID_STATE)
        {
          do_freeHandle_and_clean_up(true);
          return -1;
        }
        DEBUG_PRINT("DISABLE EVENT RECD\n");
        // GetParam and SetParam

        // Send Enable command
        OMX_SendCommand(dec_handle, OMX_CommandPortEnable, 1, 0);
        // AllocateBuffers
        /* Allocate buffer on decoder's o/p port */

        portFmt.nPortIndex = 1;
        /* Port for which the Client needs to obtain info */

        OMX_GetParameter(dec_handle,OMX_IndexParamPortDefinition,&portFmt);
        DEBUG_PRINT("Min Buffer Count=%d", portFmt.nBufferCountMin);
        DEBUG_PRINT("Buffer Size=%d", portFmt.nBufferSize);
        if(OMX_DirOutput != portFmt.eDir) {
            DEBUG_PRINT_ERROR("Error - Expect Output Port\n");
            return -1;
        }
        height = portFmt.format.video.nFrameHeight;
        width = portFmt.format.video.nFrameWidth;
        stride = portFmt.format.video.nStride;
        sliceheight = portFmt.format.video.nSliceHeight;

        error = Allocate_Buffer(dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                                portFmt.nBufferCountActual, portFmt.nBufferSize);
        if (error != OMX_ErrorNone) {
            DEBUG_PRINT_ERROR("Error - OMX_AllocateBuffer Output buffer error\n");
            return -1;
        }
        else
        {
            DEBUG_PRINT("OMX_AllocateBuffer Output buffer success\n");
        }

        // wait for enable event to come back
        wait_for_event();
        if (currentStatus == INVALID_STATE)
        {
          do_freeHandle_and_clean_up(true);
          return -1;
        }
        DEBUG_PRINT("ENABLE EVENT HANDLER RECD\n");

        for(bufCnt=0; bufCnt < portFmt.nBufferCountActual; ++bufCnt) {
            DEBUG_PRINT("OMX_FillThisBuffer on output buf no.%d\n",bufCnt);
            pOutYUVBufHdrs[bufCnt]->nOutputPortIndex = 1;
            pOutYUVBufHdrs[bufCnt]->nFlags &= ~OMX_BUFFERFLAG_EOS;
            ret = OMX_FillThisBuffer(dec_handle, pOutYUVBufHdrs[bufCnt]);
            if (OMX_ErrorNone != ret) {
                DEBUG_PRINT_ERROR("ERROR - OMX_FillThisBuffer failed with result %d\n", ret);
            }
            else {
                DEBUG_PRINT("OMX_FillThisBuffer success!\n");
            }
        }
    }

    if (freeHandle_option == FREE_HANDLE_AT_EXECUTING)
    {
      OMX_STATETYPE state = OMX_StateInvalid;
      OMX_GetState(dec_handle, &state);
      if (state == OMX_StateExecuting)
      {
        DEBUG_PRINT("Decoder is in OMX_StateExecuting and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        DEBUG_PRINT_ERROR("Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
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
        DEBUG_PRINT("Decoder is in OMX_StatePause and trying to call OMX_FreeHandle \n");
        do_freeHandle_and_clean_up(false);
      }
      else
      {
        DEBUG_PRINT_ERROR("Error - Decoder is in state %d and trying to call OMX_FreeHandle \n", state);
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
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;

    DEBUG_PRINT("pBufHdrs = %x,bufCntMin = %d\n", pBufHdrs, bufCntMin);
    *pBufHdrs= (OMX_BUFFERHEADERTYPE **)
                   malloc(sizeof(OMX_BUFFERHEADERTYPE)*bufCntMin);

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
        DEBUG_PRINT("OMX_AllocateBuffer No %d \n", bufCnt);
        error = OMX_AllocateBuffer(dec_handle, &((*pBufHdrs)[bufCnt]),
                                   nPortIndex, NULL, bufSize);
    }

    return error;
}

static void do_freeHandle_and_clean_up(bool isDueToError)
{
    unsigned int bufCnt = 0;

    for(bufCnt=0; bufCnt < input_buf_cnt; ++bufCnt)
    {
        OMX_FreeBuffer(dec_handle, 0, pInputBufHdrs[bufCnt]);
    }

    for(bufCnt=0; bufCnt < portFmt.nBufferCountActual; ++bufCnt)
    {
        OMX_FreeBuffer(dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
    }

    DEBUG_PRINT("[OMX Vdec Test] - Free handle decoder\n");
    OMX_ERRORTYPE result = OMX_FreeHandle(dec_handle);
    if (result != OMX_ErrorNone)
    {
       DEBUG_PRINT_ERROR("[OMX Vdec Test] - OMX_FreeHandle error. Error code: %d\n", result);
    }
    dec_handle = NULL;

    /* Deinit OpenMAX */
    DEBUG_PRINT("[OMX Vdec Test] - De-initializing OMX \n");
    OMX_Deinit();

    DEBUG_PRINT("[OMX Vdec Test] - closing all files\n");
    if(inputBufferFile)
    {
    fclose(inputBufferFile);
       inputBufferFile = NULL;
    }

    DEBUG_PRINT("[OMX Vdec Test] - after free inputfile\n");

    if (takeYuvLog && outputBufferFile) {
        fclose(outputBufferFile);
        outputBufferFile = NULL;
    }
    DEBUG_PRINT("[OMX Vdec Test] - after free outputfile\n");

    if(etb_queue)
    {
      free_queue(etb_queue);
      etb_queue = NULL;
    }
    DEBUG_PRINT("[OMX Vdec Test] - after free etb_queue \n");
    if(fbd_queue)
    {
      free_queue(fbd_queue);
      fbd_queue = NULL;
    }
    DEBUG_PRINT("[OMX Vdec Test] - after free iftb_queue\n");


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
    int count =0;
    int cnt = 0;
    memset(temp_buffer, 0, sizeof(temp_buffer));

    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    while (cnt < 10)
    /* Check the input file format, may result in infinite loop */
    {
        DEBUG_PRINT("loop[%d] count[%d]\n",cnt,count);
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

    DEBUG_PRINT("Actual frame Size [%d] bytes_read using fread[%d]\n",
                  frameSize, bytes_read);

    if(bytes_read == 0 || bytes_read < frameSize ) {
        DEBUG_PRINT("Bytes read Zero After Read frame Size \n");
        DEBUG_PRINT("Checking VideoPlayback Count:video_playback_count is:%d\n",
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

    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    bytes_read = fread(pBufHdr->pBuffer, 1, NUMBER_OF_ARBITRARYBYTES_READ,  inputBufferFile);

    if(bytes_read == 0) {
        DEBUG_PRINT("Bytes read Zero After Read frame Size \n");
        DEBUG_PRINT("Checking VideoPlayback Count:video_playback_count is:%d\n",
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
    int bytes_read = 0;
    unsigned int code = 0;
    pBufHdr->nFilledLen = 0;
    static unsigned int header_code = 0;

    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    do
    {
      //Start codes are always byte aligned.
      bytes_read = fread(&pBufHdr->pBuffer[readOffset],1, 1,inputBufferFile);
      if(!bytes_read)
      {
          DEBUG_PRINT("Bytes read Zero \n");
          break;
      }
      code <<= 8;
      code |= (0x000000FF & pBufHdr->pBuffer[readOffset]);
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
        }
        if ((header_code == VOP_START_CODE) && (code == VOP_START_CODE))
        {
          //Seek backwards by 4
          fseek(inputBufferFile, -4, SEEK_CUR);
          readOffset-=3;
          break;

        }
        else if (( header_code == SHORT_HEADER_START_CODE ) && ( SHORT_HEADER_START_CODE == (code & 0xFFFFFC00)))
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
    return readOffset;
}

static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    // NAL unit stream processing
    char temp_size[SIZE_NAL_FIELD_MAX];
    int i = 0;
    int j = 0;
    unsigned int size = 0;   // Need to make sure that uint32 has SIZE_NAL_FIELD_MAX (4) bytes
    unsigned int bytes_read = 0;

    // read the "size_nal_field"-byte size field
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset, 1, nalSize, inputBufferFile);
    if (bytes_read == 0)
    {
      DEBUG_PRINT("Failed to read frame or it might be EOF\n");
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

    // now read the data
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset + nalSize, 1, size, inputBufferFile);
    if (bytes_read != size)
    {
      DEBUG_PRINT_ERROR("Failed to read frame\n");
    }

    return bytes_read + nalSize;
}

static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int readOffset = 0, size_struct_C = 0;
    unsigned int startcode = 0;
    pBufHdr->nFilledLen = 0;
    pBufHdr->nFlags = 0;

    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    fread(&startcode, 4, 1, inputBufferFile);

    /* read size of struct C as it need not be 4 always*/
    fread(&size_struct_C, 1, 4, inputBufferFile);

    /* reseek to beginning of sequence header */
    fseek(inputBufferFile, -8, SEEK_CUR);

    if ((startcode & 0xFF000000) == 0xC5000000)
    {

      DEBUG_PRINT("Read_Buffer_From_RCV_File_Seq_Layer size_struct_C: %d\n", size_struct_C);

      readOffset = fread(pBufHdr->pBuffer, 1, VC1_SEQ_LAYER_SIZE_WITHOUT_STRUCTC + size_struct_C, inputBufferFile);
    }
    else if((startcode & 0xFF000000) == 0x85000000)
    {
      // .RCV V1 file

      rcv_v1 = 1;

      DEBUG_PRINT("Read_Buffer_From_RCV_File_Seq_Layer size_struct_C: %d\n", size_struct_C);

      readOffset = fread(pBufHdr->pBuffer, 1, VC1_SEQ_LAYER_SIZE_V1_WITHOUT_STRUCTC + size_struct_C, inputBufferFile);
    }
    else
    {
      DEBUG_PRINT_ERROR("Error: Unknown VC1 clip format %x\n", startcode);
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
    return readOffset;
}

static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int readOffset = 0;
    unsigned int len = 0;
    unsigned int key = 0;
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    DEBUG_PRINT("Read_Buffer_From_RCV_File - nOffset %d\n", pBufHdr->nOffset);
    if(rcv_v1)
    {
      /* for the case of RCV V1 format, the frame header is only of 4 bytes and has
         only the frame size information */
      readOffset = fread(&len, 1, 4, inputBufferFile);
      DEBUG_PRINT("Read_Buffer_From_RCV_File - framesize %d %x\n", len, len);

    }
    else
    {
      /* for a regular RCV file, 3 bytes comprise the frame size and 1 byte for key*/
      readOffset = fread(&len, 1, 3, inputBufferFile);
      DEBUG_PRINT("Read_Buffer_From_RCV_File - framesize %d %x\n", len, len);

      readOffset = fread(&key, 1, 1, inputBufferFile);
      if ( (key & 0x80) == false)
      {
        DEBUG_PRINT("Read_Buffer_From_RCV_File - Non IDR frame key %x\n", key);
       }

    }

    if(!rcv_v1)
    {
      /* There is timestamp field only for regular RCV format and not for RCV V1 format*/
      readOffset = fread(&pBufHdr->nTimeStamp, 1, 4, inputBufferFile);
      DEBUG_PRINT("Read_Buffer_From_RCV_File - timeStamp %d\n", pBufHdr->nTimeStamp);
      pBufHdr->nTimeStamp *= 1000;
    }
    else
    {
        pBufHdr->nTimeStamp = timeStampLfile;
        timeStampLfile += timestampInterval;
    }

    if(len > pBufHdr->nAllocLen)
    {
       DEBUG_PRINT_ERROR("Error in sufficient buffer framesize %d, allocalen %d noffset %d\n",len,pBufHdr->nAllocLen, pBufHdr->nOffset);
       readOffset = fread(pBufHdr->pBuffer+pBufHdr->nOffset, 1, pBufHdr->nAllocLen - pBufHdr->nOffset , inputBufferFile);
       fseek(inputBufferFile, len - readOffset,SEEK_CUR);
       return readOffset;
    }
    else
    readOffset = fread(pBufHdr->pBuffer+pBufHdr->nOffset, 1, len, inputBufferFile);
    if (readOffset != len)
    {
      DEBUG_PRINT("EOS reach or Reading error %d, %s \n", readOffset, strerror( errno ));
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

    return readOffset;
}

static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    static OMX_S64 timeStampLfile = 0;
    OMX_U8 *pBuffer = pBufHdr->pBuffer + pBufHdr->nOffset;
    DEBUG_PRINT("Inside %s \n", __FUNCTION__);

    unsigned int readOffset = 0;
    int bytes_read = 0;
    unsigned int code = 0;

    do
    {
      //Start codes are always byte aligned.
      bytes_read = fread(&pBuffer[readOffset],1, 1,inputBufferFile);
      if(!bytes_read)
      {
          DEBUG_PRINT("\n Bytes read Zero \n");
          break;
      }
      code <<= 8;
      code |= (0x000000FF & pBufHdr->pBuffer[readOffset]);
      //VOP start code comparision
      if (readOffset>3)
      {
        if (VC1_FRAME_START_CODE == (code & 0xFFFFFFFF))
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
    timeStampLfile += timestampInterval;

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

    return readOffset;
}

static int open_video_file ()
{
    int error_code = 0;
    char outputfilename[512];
    DEBUG_PRINT("Inside %s filename=%s\n", __FUNCTION__, in_filename);

    inputBufferFile = fopen (in_filename, "rb");
    if (inputBufferFile == NULL) {
        DEBUG_PRINT_ERROR("Error - i/p file %s could NOT be opened\n",
                          in_filename);
        error_code = -1;
    }
    else {
        DEBUG_PRINT("I/p file %s is opened \n", in_filename);
    }

    if (takeYuvLog) {
        strcpy(outputfilename, "/data/misc/yuv");
        outputBufferFile = fopen (outputfilename, "ab");
        if (outputBufferFile == NULL)
        {
          DEBUG_PRINT_ERROR("ERROR - o/p file %s could NOT be opened\n", outputfilename);
          error_code = -1;
        }
        else
        {
          DEBUG_PRINT("O/p file %s is opened \n", outputfilename);
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

int drawBG(void)
{
    int result;
    unsigned int i;
#ifdef FRAMEBUFFER_32
    long * p;
#else
    short * p;
#endif
    void *fb_buf = mmap (NULL, finfo.smem_len,PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);

    if (fb_buf == MAP_FAILED)
    {
        printf("ERROR: Framebuffer MMAP failed!\n");
        close(fb_fd);
        return -1;
    }

    vinfo.yoffset = 0;
    p = (long *)fb_buf;

    for (i=0; i < vinfo.xres * vinfo.yres; i++)
    {
    #ifdef FRAMEBUFFER_32
        *p++ = COLOR_BLACK_RGBA_8888;
    #else
        *p++ = CLR_KEY;
    #endif
    }

    if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo) < 0)
    {
        printf("ERROR: FBIOPAN_DISPLAY failed! line=%d\n", __LINE__);
        return -1;
    }

    DEBUG_PRINT("drawBG success!\n");
    return 0;
}

void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr)
{
    unsigned int addr = 0;
    OMX_OTHER_EXTRADATATYPE *pExtraData = 0;
    OMX_QCOM_EXTRADATA_FRAMEINFO *pExtraFrameInfo = 0;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;
    unsigned int destx, desty,destW, destH;
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
        DEBUG_PRINT_ERROR("Warning: /dev/fb0 is not opened!\n");
        return;
    }

    img.list.count = 1;
    e = &img.list.req[0];

    addr = (unsigned int)(pBufHdr->pBuffer + pBufHdr->nFilledLen);
    // align to a 4 byte boundary
    addr = (addr + 3) & (~3);

    // read to the end of existing extra data sections
    pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;

    while (addr < end && pExtraData->eType != (enum OMX_EXTRADATATYPE)OMX_ExtraDataFrameInfo)
    {
            addr += pExtraData->nSize;
            pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;
    }

    if (pExtraData->eType != (enum OMX_EXTRADATATYPE)OMX_ExtraDataFrameInfo)
    {
       DEBUG_PRINT_ERROR("pExtraData->eType %d pExtraData->nSize %d\n",pExtraData->eType,pExtraData->nSize);
    }
    pExtraFrameInfo = (OMX_QCOM_EXTRADATA_FRAMEINFO *)pExtraData->data;

   pPMEMInfo  = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                    pBufHdr->pPlatformPrivate)->entryList->entry;
#ifdef _ANDROID_
    vheap = (MemoryHeapBase *)pPMEMInfo->pmem_fd;
#endif


    DEBUG_PRINT_ERROR("DecWidth %d DecHeight %d\n",portFmt.format.video.nStride,portFmt.format.video.nSliceHeight);
    DEBUG_PRINT_ERROR("DispWidth %d DispHeight %d\n",portFmt.format.video.nFrameWidth,portFmt.format.video.nFrameHeight);



    e->src.width = portFmt.format.video.nStride;
    e->src.height = portFmt.format.video.nSliceHeight;
    e->src.format = MDP_Y_CBCR_H2V2;
        e->src.offset = pPMEMInfo->offset;
#ifdef _ANDROID_
    e->src.memory_id = vheap->getHeapID();
#else
    e->src.memory_id = pPMEMInfo->pmem_fd;
#endif

    DEBUG_PRINT_ERROR("pmemOffset %d pmemID %d\n",e->src.offset,e->src.memory_id);

    e->dst.width = vinfo.xres;
    e->dst.height = vinfo.yres;
    e->dst.format = MDP_RGB_565;
    e->dst.offset = 0;
    e->dst.memory_id = fb_fd;

    e->transp_mask = 0xffffffff;
    DEBUG_PRINT("Frame interlace type %d!\n", pExtraFrameInfo->interlaceType);
    if(pExtraFrameInfo->interlaceType != OMX_QCOM_InterlaceFrameProgressive)
    {
       DEBUG_PRINT("Interlaced Frame!\n");
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
            destW = vinfo.xres;
            destH = vinfo.yres;
    }


        if(portFmt.format.video.nFrameWidth < destW)
          destW = portFmt.format.video.nFrameWidth ;


        if(portFmt.format.video.nFrameHeight < destH)
           destH = portFmt.format.video.nFrameHeight;

    e->dst_rect.x = destx;
    e->dst_rect.y = desty;
    e->dst_rect.w = destW;
    e->dst_rect.h = destH;

    //e->dst_rect.w = 800;
    //e->dst_rect.h = 480;

    e->src_rect.x = 0;
    e->src_rect.y = 0;
    e->src_rect.w = portFmt.format.video.nFrameWidth;
    e->src_rect.h = portFmt.format.video.nFrameHeight;

    //e->src_rect.w = portFmt.format.video.nStride;
    //e->src_rect.h = portFmt.format.video.nSliceHeight;

    if (ioctl(fb_fd, MSMFB_BLIT, &img)) {
        DEBUG_PRINT_ERROR("MSMFB_BLIT ioctl failed!\n");
        return;
    }

    if (ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo) < 0) {
        DEBUG_PRINT_ERROR("FBIOPAN_DISPLAY failed! line=%d\n", __LINE__);
        return;
    }

    DEBUG_PRINT("render_fb complete!\n");
}

