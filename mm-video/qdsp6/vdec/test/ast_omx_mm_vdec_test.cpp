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

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

GENERAL DESCRIPTION
AST OpenMax Test App - Video Decoders

REFERENCES

EXTERNALIZED FUNCTIONS


INITIALIZATION AND SEQUENCING REQUIREMENTS

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

/* <EJECT> */
/*===========================================================================

EDIT HISTORY FOR MODULE

This section contains comments describing changes made to the module.
Notice that changes are listed in reverse chronological order.

$Header:

when       who      what, where, why
--------   ---      ---------------------------------------------------------- 
===========================================================================*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef TARGET_ARCH_7225
    #warning "=============||||||| TARGET_ARCH_7225 ENABLED |||||||=============="
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#ifdef _ANDROID_
    #warning "=============||||||| PLATFORM IS ANDROID |||||||=============="
   // #include <utils/MemoryHeapBase.h>
    #include <binder/MemoryHeapBase.h>
#endif
#include "OMX_Core.h"
#include "OMX_Component.h"
#include "OMX_QCOMExtns.h"
#include "lasic_control.h"
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#ifdef TARGET_ARCH_8K
    #warning "=============||||||| TARGET_ARCH_8K ENABLED |||||||=============="
    #include "vdec.h"   // to display frame 
    #define USE_INPUT_FILE_MUTEX
    #define USE_PRINT_MUTEX
    #include<stdint.h>
#else
    #define USE_INPUT_FILE_MUTEX
    #include<stdint.h>
#endif

#include <linux/msm_mdp.h>
#include <linux/fb.h>
#ifdef TARGET_ARCH_KARURA
    #warning "=============||||||| TARGET_ARCH_KARURA ENABLED |||||||=============="
    #include "comdef.h" 
#endif
#include "qutility.h"
#include "qtv_msg.h"

/************************************************************************/
/*				#DEFINES	                        */
/************************************************************************/
#ifndef QTV_MSG_PRIO 
    #define QTV_MSG_PRIO(type, prio, str) fprintf(stdout, str)
#endif
#ifndef QTV_MSG_PRIO1
    #define QTV_MSG_PRIO1(type, prio, str, _x) fprintf(stdout, str, _x)
#endif
#ifndef QTV_MSG_PRIO2
    #define QTV_MSG_PRIO2(type, prio, str, _x, _y) fprintf(stdout, str, _x, _y)
#endif

#define PRINTF RegularPrintf
/* #define printf print_timestamp(stdout) , fprintf(stdout, "%s :: Line %d :: %s() :: ", \
              (my_rindex(__FILE__, '/')==NULL ? __FILE__ : my_rindex(__FILE__, '/')+1), __LINE__, __FUNCTION__) , myPrintf */

#define DELAY 66 
#define false 0
#define true 1
#define INPUT_BUFFER_SIZE 128*1024*2
#define OUTPUT_BUFFER_SIZE 38016

#define DISPLAY_YUV 1   
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
#define SIZE_NAL_FIELD_MAX 4

/* For Profile mode, this shud hold atleast (Max output buffs) + (Max input buffs) + approx 10 */
#define Q_SIZE_App 90
#define CIRQ_SIZE_WaitOmxEvent 5
#define MAXSIZE_FName 500
#define APP_OMX_STATENAME_SIZE  30
#define BUFFER_DUMP_BYTES 300
#define ERR_OK 0
#define INBUF  0
#define OUTBUF 1
#define USEDINBUF 1
#define USEDOUTBUF 11
#define FREE 0
#define USED 1
#define MIN(_x,_y) ((_x) < (_y) ? (_x) : (_y))
#define MAX(_x,_y) ((_x) > (_y) ? (_x) : (_y))
#define NUMELEM_ARRAY(_a) sizeof(_a)/sizeof(_a[0])

#define _HERE   __FILE__, __LINE__, __FUNCTION__
#define _NOHERE __FILE__, 0, NULL
#define _TIMEONLY __FILE__, __LINE__, NULL
#define DPRINT debug_printf
#define PRINT_TIME print_timestamp(0, 0, gtMyStdout)
#define LOCK_MUTEX(_m) { if(gnLogLevel >=11) {                              \
               print_timestamp(0, 0, (gtMyStdout ? gtMyStdout : stdout));   \
               fprintf((gtMyStdout ? gtMyStdout : stdout), "Line %05d :: %s() ==== LOCKED_MUTEX[%s] ====", __LINE__, __FUNCTION__, #_m); }   \
            pthread_mutex_lock(_m); }
#define UNLOCK_MUTEX(_m) { if(gnLogLevel >=11) {                            \
               print_timestamp(0, 0, (gtMyStdout ? gtMyStdout : stdout));   \
               fprintf((gtMyStdout ? gtMyStdout : stdout), "Line %05d :: %s() ==== UN-LOCKED_MUTEX[%s] ====", __LINE__, __FUNCTION__, #_m); }\
            pthread_mutex_unlock(_m); }
#define LOCK_MUTEX_HIFREQ(_m) { if(gnLogLevel >=12) {                       \
               print_timestamp(0, 0, (gtMyStdout ? gtMyStdout : stdout));   \
               fprintf((gtMyStdout ? gtMyStdout : stdout), "Line %05d :: %s() ==== LOCKED_MUTEX[%s] ====", __LINE__, __FUNCTION__, #_m); }   \
            pthread_mutex_lock(_m); }
#define UNLOCK_MUTEX_HIFREQ(_m) { if(gnLogLevel >=12) {                     \
               print_timestamp(0, 0, (gtMyStdout ? gtMyStdout : stdout));   \
               fprintf((gtMyStdout ? gtMyStdout : stdout), "Line %05d :: %s() ==== UN-LOCKED_MUTEX[%s] ====", __LINE__, __FUNCTION__, #_m); }\
            pthread_mutex_unlock(_m); }

#define ERROR_ACTION(level, errcode) {                                                                                              \
    if(gnAbortLevel >= level) {                                                                                                     \
      int i;                                                                                                                        \
      struct timeval end;  /* gettimeofday for gtDecEndTimeVal needs to be as thread-safe as possible without mutex use */          \
      (void)gettimeofday(&end, NULL); gtDecEndTimeVal = end; gtDeinitStartTimeVal = gtDeinitEndTimeVal;                             \
      dump_global_data(); print_stats_summary(); dump_omx_buffers(1, 199); dump_omx_buffers(1, 99);                                 \
      DPRINT(_TIMEONLY, 0, "\n**********************");                                                                             \
      DPRINT(_TIMEONLY, 0, "\nERROR_ABORT[%d] : %s", errcode, abort_err_str(errcode));                                              \
      DPRINT(_TIMEONLY, 0, "\n...TEST FAILED...");                                                                                  \
      DPRINT(_TIMEONLY, 0, "\n**********************\n");                                                                           \
      terminate(true); my_fcloseall();                                                                                              \
      for(i=0; i<5; i++) DPRINT(_TIMEONLY, 0, "\nDummy line %d", i);                                                                \
      exit(errcode);                                                                                                                \
    }                                                                                                                               \
 }


#define CHECK_OMX_STATE(_s) { if(0 == check_omx_state(_s) && false != gbEnableOmxStateCheck) ERROR_ACTION(0, 113); }
#define CHANGE_OMX_STATE(_s) change_omx_state(_s);
#define CHECK_BUFFERS_RETURN(_usec, _which, _abort, _severity, _mutex) {                                                \
    int rtv;                                                                                                            \
    if((rtv = wait_for_all_buffers(_usec, _which, _mutex)) != 0) {                                                      \
        DPRINT(_HERE, 6, "\nAPP_EVENT_OMX_STATE_SET: ERROR : DID NOT GET BACK ALL BUFFERS (retval=%d)", rtv);           \
        dump_omx_buffers(_abort, 199);                                                                                  \
        if(_abort != false) {                                                                                           \
            ERROR_ACTION(_severity, 55);                                                                                \
      }}}

#define EMPTY_BUFFER(_x, _pB) ( (!gbSetSyncFlag && _pB->nFlags & ~OMX_BUFFERFLAG_SYNCFRAME),                                            \
            (gbDumpInputBuffer && inBufferDumpFile && fwrite((void *)_pB->pBuffer, 1, _pB->nFilledLen, inBufferDumpFile)),              \
            DPRINT(_HERE, 8, "EmptyThisBuffer[%d]: pBuffHdr=0x%x, pBuffer=0x%x, nFilledLen=%lu, nOffset=%lu, nFlags=0x%x ",             \
            ++gnEtbCnt, _pB, _pB->pBuffer, (unsigned long)_pB->nFilledLen, _pB->nOffset, _pB->nFlags), OMX_EmptyThisBuffer(_x, _pB))

#define APP_CHANGE_STATE(_s) {                                              \
          pthread_mutex_lock(&gtGlobalVarMutex);                            \
            DPRINT(_HERE, 6, "\nCHANGING APP STATE TO %s[%d]... ", #_s, _s);\
            geAppState = (app_state_t)_s;                                   \
          pthread_mutex_unlock(&gtGlobalVarMutex);                          \
        }

/* ATOMIC_DO_GLOBAL(_x) : Do a write operation to a global atomically (using gtGlobalVarMutex)
DO NOT USE THIS WHEN any of the SAFE_COPY_GLOBALS* / LOCVAL* / GLOBVAL* / COMMIT_LOCVAL etc macros below are being used in a function */
//#define ATOMIC_DO_GLOBAL(_x) pthread_mutex_lock(&gtGlobalVarMutex); (_x); pthread_mutex_unlock(&gtGlobalVarMutex);
#define ATOMIC_DO_GLOBAL(_x) { LOCK_MUTEX(&gtGlobalVarMutex); (_x); UNLOCK_MUTEX(&gtGlobalVarMutex); }
/* ATOMIC_DO(_m, _x) : Do a generic write operation atomically (using specified mutex '_m') */
#define ATOMIC_DO(_m, _x) { LOCK_MUTEX(&_m); (_x); UNLOCK_MUTEX(&_m); }

/* These macros are used for calculating average function latencies for recurring functions like EBD/FBD handlers */
#define LATENCY_FUNC_ENTRY  struct timeval _l_starttime, _l_currtime;   \
                            /*SAFE_COPY_GLOBALS_FUNC_ENTRY; */          \
                            (void)gettimeofday(& _l_starttime, NULL);   
                            /*SAFE_COPY_GLOBALS;*/    

#define LATENCY_FUNC_EXIT(_update, _retval) /*LATE_COMMIT_LOCVALS*/;                                    \
                                            (void)gettimeofday(& _l_currtime, NULL);                    \
                                            _update += diff_timeofday(& _l_currtime, & _l_starttime);   \
                                            return _retval;

#define LATENCY_FUNC_CONT(_update)          /*LATE_COMMIT_LOCVALS*/;                                    \
                                            (void)gettimeofday(& _l_currtime, NULL);                    \
                                            _update += diff_timeofday(& _l_currtime, & _l_starttime);   

/************************************************************************/
/*				GLOBAL DECLARATIONS                     */
/************************************************************************/
#if defined(_ANDROID_)
using namespace android;
#endif

typedef struct {
    int errmin;
    int errmax;
    const char *err_string;
} abort_err_str_t;

/* Defines ranges of abort codes followed by a descriptive string for each range.
   NOTE : In any given range, put the most specific codes/sub-ranges first & then the more generic ones */
static const abort_err_str_t gtAbortErrs[] = {
    /* Abort codes convention...(refer table below for LATEST data)
    50-99 : Codec related
    100-199 : OMX related (problem in EmptyThisBuffer / GetParam etc) 
    152-199 : Display related 
    200-249 : Test app specific...
       200-204 : File I/O related
       205-219 : App Errors (eg. appQ full etc)
       220-224 : Unexpected conditions in app code
       225-244 : <UNUSED>
       245-249 : Memory alloc related errors 
       250-255 : Misc <UNUSED> */
    { 52, 52, "CODEC : EOS Flag not propagated to out buffer" },
    { 54, 54, "CODEC : OMX_EventBufferFlag (EOS on last out buffer) not recvd" },
    { 55, 55, "CODEC : Buffers not returned" },   
    { 56, 56, "CODEC : Buffer returned during OMX_Paused" },
    { 57, 57, "CODEC : Buffer returned when not expected" },
    { 58, 58, "CODEC : Port Reconfig failed" },
    { 60, 60, "CODEC : Watchdog thread timed out" },
    { 61, 61, "CODEC : State transition timed out" },
    { 62, 62, "CODEC : Event wait timed out" },
    { 65, 65, "CODEC : (SEEK) No free input OMX buffer after flush" }, 
    { 66, 66, "CODEC : OMX State Transition Failed" },
    { 50, 99, "CODEC related" },
    { 102, 102, "OMX : EmptyBuffer Failed" },
    { 105, 105, "OMX : FillBuffer Failed" },
    { 107, 107, "OMX : OMX_FreeHandle Failed" },
    { 110, 110, "OMX : GetParam Failed" },
    { 111, 111, "OMX : Got OMX_Event_Error" },
    { 112, 112, "OMX : GetState Failed" },
    { 113, 113, "OMX : Not in expected OMX state" },
    { 100, 149, "OMX related" },
    { 152, 152, "DISPLAY : Couldn't open FrameBuffer" }, 
    { 155, 155, "DISPLAY : Couldn't get VScreenInfo" }, 
    { 157, 157, "DISPLAY : MSMFB_BLIT failed" },
    { 159, 159, "DISPLAY : FBIOPAN_DISPLAY failed" },
    { 150, 199, "DISPLAY related" }, 
    { 201, 201, "APP I/O : File read/open failed" },
    { 202, 202, "APP I/O : File create failed" },
    { 203, 203, "APP I/O : Invalid NAL unit in inputfile" },
    { 200, 204, "APP : File I/O related" },
    { 205, 205, "APP : Thread create failed" },
    { 210, 210, "APP : APP_Q Full. INCREASE App QUE SIZE !" }, 
    { 211, 211, "APP : No free input OMX buffers" }, 
    { 212, 212, "APP : Invalid OMX buffer header pointer" }, 
    { 213, 213, "APP : (SEEK) Extra frames decoded during seek" }, 
    { 214, 214, "APP : (SEEK) Cannot detect frame type / unsupported for now" }, 
    { 215, 215, "APP : Invalid SET_PARAM sent to pipe" }, 
    { 216, 216, "APP : Invalid param passed by app" }, 
    { 210, 219, "APP : General Failure in Test App" }, 
    { 222, 222, "APP : Unexpected condition in test app" },
    { 220, 224, "APP : Unexpected condition" },
    { 245, 245, "APP : Malloc failed" },
    { 247, 247, "APP : Re-alloc failed" },
    { 245, 249, "APP : Memory related" },
    { 200, 249, "Test App related" },
    { 250, 255, "Miscellaneous" },
};

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

static int (*Read_Buffer_Func)(OMX_BUFFERHEADERTYPE  *pBuff ) = NULL;
static int (*get_frame_type)(int how, const void *packet, unsigned int size) = NULL;

typedef enum {
  VIDEO_DISPLAY_FORMAT_QCIF,
  VIDEO_DISPLAY_FORMAT_QVGA,
  VIDEO_DISPLAY_FORMAT_HVGA,
  VIDEO_DISPLAY_FORMAT_VGA,
  VIDEO_DISPLAY_FORMAT_MAX
}video_display_format_type;

typedef struct vidFrame {
	long fSize;
	OMX_U8 *fData;  
}vidFrame;

FILE * gtMyStdout = NULL;
FILE * inputBufferFile = NULL;    
FILE * outputBufferFile = NULL; 
static FILE * outputIFramesFile = NULL;
static FILE *outputEncodedIFramesFile = NULL;
static FILE *inBufferDumpFile = NULL;

pthread_mutex_t lock;
pthread_cond_t cond;

OMX_PARAM_PORTDEFINITIONTYPE portFmt;
OMX_PORT_PARAM_TYPE portParam;
OMX_ERRORTYPE error;

/* Display related variables */
union 
{
  char dummy[sizeof(struct mdp_blit_req_list) + 
	         sizeof(struct mdp_blit_req)*1];
  struct mdp_blit_req_list list;
} yuv;

static struct sigaction sigact = {0};
static int fb_fd = -1;
static struct fb_var_screeninfo vinfo = {0};
static struct fb_fix_screeninfo finfo = {0};
static int wait_for_all_buffers(unsigned long howlong, int which, pthread_mutex_t *mutex);
#if defined(TARGET_ARCH_8K) || ( defined(_ANDROID_) && defined(TARGET_ARCH_KARURA) )
// 06/01/09 void render_fb(struct vdec_frame *pFrame);    
void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr);
#else
static void display_frame_7k(void* pBuffer);
#endif

// App state machine and command/event que was needed
/* Keeps track of the OMX event for which an app thread is waiting (refer expect_some_event()) */
typedef struct {
    int eEvent;
    int nData1;
    int nData2;
    void *pEventData;
} wait_omx_event_info_t;

typedef enum {
	APP_STATE_Foetus=0,       
    APP_STATE_WaitFirstFrame,       // Wait for 1st frame decode (needed on 8K)
    APP_STATE_OutPortReconfig,      // If OMX Port Reconfig event was sent after first frame decode then app state moves to this
	APP_STATE_Meditating,           // Pause
	APP_STATE_Walking,              // Play
	APP_STATE_Sprinting,	        // FFwd
	APP_STATE_SprintingBackwards,	// Rewind (not implemented)
    APP_STATE_WaitOmxFlush,         // Wait for Omx Flush Complete events (used for ffwd)
    APP_STATE_CollectOmxBuffers,    // Wait for all OMX buffers to be returned through EBD / FBD callbacks
    APP_STATE_CollectOmxInBuffers,  // Wait for OMX input buffers to be returned through EBD callbacks
    APP_STATE_CollectOmxOutBuffers, // Wait for OMX output buffers to be returned through FBD callbacks
    APP_STATE_DeIniting,             // Finish up de-init activities at end of decode
    APP_STATE_DEBUG_BLOCKED,        // App suspends itself for debugging purposes (eg. after I-frame is sent for decode, etc)
} app_state_t;

typedef enum {
    APP_EVENT_CMD_CHANGE_STATE=0,   // User commands : Play/pause/stop etc    
    APP_EVENT_CMD_MISC,         // Other user commands which cannot be directly processed in handle_lasic_cmd() due to thread context
    APP_EVENT_EOS_INPUT,        // End of input stream
    APP_EVENT_EOS_OUTPUT,       // End of output stream
    APP_EVENT_PORT_RECONFIG,    // Port Settings changed so handle that
    APP_EVENT_SYNC_UNBLOCK,     // Internal event to wake up main app thread (to prevent starvation) : NOT NEEDED
    APP_EVENT_END,              // Tell main app thread to cleanup & abort
    APP_EVENT_STOP,             // Tell main app thread to stop all activity 
    APP_EVENT_OMX_STATE_SET,    // For ad-hoc OMX State Transition tests (try setting OMX state to desired value)
    APP_EVENT_EMPTY_BUFFER_DONE,// EBD callback handler can post app event & return so that EBD processing is queued (only used in Profile mode for now)
    APP_EVENT_FILL_BUFFER_DONE, // FBD callback handler can post app event & return so that FBD processing is queued (only used in Profile mode for now)
    APP_EVENT_CONTINUE_LOOP,    // Posted by EBD/FBD to cause main thread to continue looped play (by first collecting buffers before restting input file)
} app_event_t; 

typedef struct app_evt_ebd_type {
    OMX_IN OMX_HANDLETYPE hComponent;
    OMX_IN OMX_PTR pAppData;
    OMX_IN OMX_BUFFERHEADERTYPE* pBuffer;
} appQ_evtdata_OMX_EBD_t;

typedef struct {
    app_event_t event;
    int data;
    void *pMoreData;
    appQ_evtdata_OMX_EBD_t omx_ebd_params;
} appQ_event_data_t;

/************************************************************************/
/*				GLOBAL INIT			        */
/************************************************************************/
/* Value of WAIT_OMX_EVT_ANY for eEvent or nData1 indicates wait for ANY event (and/or ANY Cmd complete)
   Value of WAIT_OMX_EVT_ANYERR for nData2 indicates the waiting thread will be woken on any ERROR event, APART from the event
   it is actually waiting for */
#define WAIT_OMX_EVT_ANY  -99
#define WAIT_OMX_EVT_ANYERR -88
#define WAIT_OMX_EVT_WAITING -11
static const wait_omx_event_info_t WAIT_OMX_EVENT_DATA_INIT = { WAIT_OMX_EVT_ANY, WAIT_OMX_EVT_ANY, 0, NULL };
static int gbDumpInputBuffer = false;       // Debug feature: Dump input buffers just before emptying
static int gbDontFreeHandle = false;        // Debug feature : Workarounds & tests for issues with OMX_FreeHandle
static int gbNoWaitEosOnLastBuff = false;   // Debug feature : OMX buffer flag propagation wasn't working so use workarounds to test both conditions
static int gbNoBufferFlagEosWait = true;    // Debug feature : Don't wait for OMX BufferFlag event (when component gets EOS on output port) to test both conditions
static int gbDontWaitForBuffs = true;       // Debug feature : OMX output buffers not returned sometimes so use workarounds to test both conditions
static int gbSetSyncFlag = false;           // Debug feature : to check for buffer flag propagation, set this to 1 & also set gbDumpDecodedIframes=1                                        
static int gbNoEosInLoopedPlay = false;     // Debug feature : Don't add EOS flag to last input buffer during looped play. Doesn't affect FFWD
static int gbSendEosAtStart = false;        // Debug feature : Verify tat decode will continue if more i/p buffs are passed after sending EOS (spec says it shud)
static int gbCollectBuffsInPaused = true;   // Debug feature : Collect all input buffers during OMX_Executing -> OMX_Pause transition
static int gbMarkBuffsFreeAfterPause = false;   // Debug feature: mark all buffers as free after Pause is complete
static int gbForceFeedBuffsOnResume = false;    // Debug feature : Feed input buffers After OMX_Pause -> OMX_Executing
static int gbForceFeedBuffsOnLooping = false;   // Debug feature : For Looped play, Collect all buffers at input file reset by doing Exec->Idle->Exec transition
static int gbEnableOmxStateCheck = false;       // Debug feature : Enable current OMX state checking after state transitions
static int gbFramePackArbitrary = true;     // Debug feature : Set arbitrary frame packing in codec irrespective of frame read method
static int gbWaitFirstFrameDec = false;      /* Debug feature : If true then sends only first few input buffers (sps/pps/iframe) & waits for 
                                               first frame EBD OR port reconfig event (as per spec). If false then uses old way of starting 
                                               normal decode & EventHandler sends port_reconfig_event to app if decoder sent OMX_EventPortSettingsChanged */
static int gbFfwd_ErrorSeekMode = false;    // Debug feature : Will not look for I-frames & just seek to any frame (simulation of streaming errors)

static int gbProfileMode = 0;     /* In profiling mode, the rate of input feed may need to be reduced to match input video FPS. Otherwise
                                     the decoder may decode too fast (eg 60 fps for a 25 fps input vide) & hence use up higher cpu */
static int gnProfileFPS = 32;                   // In profiling mode, if this value is set then the input feed will be slowed to get this approx FPS value for decode 
static unsigned int gnProfileSleepUsecs = 7000; // In profiling mode, if FPS not set then sleep for this fixed amount for each input buffer feed 
static int gbWatchDogEnabled = true;    // Enable/Disable Watchdog thread (WatchDogOn==0 implies the thread is temporarily off for cases like PAUSE)
static int gbWatchDogOn = true;         // Watchdog thread (times out if no Empty/Fill BufferDone callbacks in a certain time period)
static int gnWatchDogInterval = 5;      // Nr. of secs of after which watchdog thread checks app status                                        
static int gnDisplayWindow = 0;         // Display window # (assuming 0 = full screen & 1-4 are the 4 quadrants)
static int gnLastEbdCnt = -1;           // Watchdog : If LastEbdCnt == current ebd_cnt then timeout
static int gnLastFbdCnt = -1;           // Watchdog : If LastFbdCnt == current fbd_cnt then timeout
static int gnAbortLevel = 2;            // Error level at which program will abort (0 = most_critical,  -1 = Never abort)
static unsigned int gnTimeoutSecs = 2;  // Timeout for any wait-for-event
static unsigned int gnTimeoutUSecs = 0; // Timeout(usecs) for any wait-for-event
static int gnLogLevel = 9;
static int gbRepeatMenu = false;        // For menu driven mode, keep reprinting menu after completing a test case
static int gbLoopedPlay = false;            
static int gnLoopCount = 1;             // Nr. of iterations the whole clip is being decoded (will be >1 if LoopedPlay is ON)
static int gbDumpDecodedIframes = 0;    // If set, then all raw YUV I-frames decoded for start-decode-seek-points during FFWD will be dumped to a file
static int gnDumpedDecIframeCnt = 0;    /* Nr. of start-decode-seek-point Iframes dumped. A start-decode-seek-point Iframe is 
                                           the 1st frame to be decoded during a single seek step. FFWD is implemented by repeatedly doing the seek step */
static int gbFfwd_PauseOnIframeDec = false; // If set, then during fast forward, app will while(1) after it sends a seek-point I-frame for decode
static int gbFfwd_FlushBeforeISeek = true;  // Whether to flush before seeking I-frame during random seek
static int gnFfwd_IFrameSkipCnt = 0;    // For fast fwd, skip to every n'th I-frame
static int gnFfwd_NrUnitsToDecode = 8;  // In FFWD: nr. input units to decode including the buffer containing I-frame
static int gnFfwd_UnitsDecoded = 0;     // In FFWD: Counts units after Iframe sent for decode. In PLAY: counts units sent for decode
static int gnInNewIFrame = 0;           // In FFWD: Keeps track of whether the current set of NAL units belong to an I-frame or not
static int gnIFrameCount = 0;           // Total Iframes found in stream
static int gnIFrameType = 5;            // Frame type value that denotes an I-frame (depends on codec)                                        
static int gbDuringOutPortReconfig= false;    // This remains true while port reconfig is being processed
static int gnInputUnitCnt = 0;          // Total nr. of input frames sent for decode
static int gnOutputUnitCnt = 0;         // Total nr. of output frames given by decoder                                        
static int gnNALUnitCnt = 0;            // Total valid NAL units found in stream
static int gnFlushCount = 0;            // Nr. of times flush was called
static int gnEtbCnt = 0;                // Nr. of OMX_EmptyThisBuffer() calls                                        
static unsigned long long gnUsedInputBuffsMap = 0;      // Tracks currently used input buffers 
static unsigned long long gnUsedOutputBuffsMap = 0;     // Tracks currently used output buffers
static int gbGotBufferFlagEvent = false;    // Indicates if BufferFlag event for EOS on output port was received
static volatile app_state_t geAppState = APP_STATE_Foetus;	    // Is volatile required ? Check code
static unsigned int gnEBDLatency = 0;
static unsigned int gnFBDLatency = 0;
static unsigned int gnFlushLatency = 0;

static char gsInputFilename[MAXSIZE_FName+1]="";
static char gsOutputFilename[MAXSIZE_FName+1]="";
static char gsPerfLogFilename[MAXSIZE_FName+1]="ast_vdec_test_PerfMode.log";
static int gnAppQIndex = -1;
static appQ_event_data_t gtAppQ[Q_SIZE_App];
static video_display_format_type geFormat = VIDEO_DISPLAY_FORMAT_MAX;
static pthread_mutex_t gtPrintMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gtWatchdogMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gtAppQMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gtAppQCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t gtWaitOmxEventMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gtWaitOmxEventCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t gtGlobalVarMutex = PTHREAD_MUTEX_INITIALIZER;    // For thread-safe updating of global vars
// Mutex synchronisation in EBD/FBD causes latency of around 0.02 millisec on 8K
static pthread_mutex_t gtInputFileMutex = PTHREAD_MUTEX_INITIALIZER;    // For thread-safe access to the input file
/* Specifies what OMX event is being waited for (refer WAIT_OMX_EVENT_DATA_INIT & expect_some_event() etc */
static wait_omx_event_info_t gtWaitOmxEventData;
/* Stores data for event that occured before waking up thread that is waiting for that event (ref. expect_some_event()) */
static int gnGotOmxEvtQ_Head = 0;
static int gnGotOmxEvtQ_Tail = -1;
static wait_omx_event_info_t gtGotOmxEventDataQ[CIRQ_SIZE_WaitOmxEvent];    
static struct timeval gtInitStartTimeVal = {0, 0};
static struct timeval gtInitEndTimeVal = {0, 0};
static struct timeval gtDecStartTimeVal = {0, 0};
static struct timeval gtDecEndTimeVal = {0, 0};
static struct timeval gtDeinitStartTimeVal = {0, 0};
static struct timeval gtDeinitEndTimeVal = {0, 0};
static struct timeval gtFirstFrameDecTimeVal = {0, 0};

static int file_write = 0;
static int fb_display = 1;
static int gnTotalOutputBufs = 0;
static int gnTotalInputBufs = 0;
static int height =0, width =0;
static codec_format  codec_format_option = CODEC_FORMAT_H264;
static file_type     file_type_option = FILE_TYPE_DAT_PER_AU;
//const int video_playback_count_init = 1;
static volatile int event_is_done = 0;
static int ebd_cnt, fbd_cnt;
static int bInputEosReached = 0;
static int bOutputEosReached = 0;

static int timeStampLfile = 100;
static int timestampInterval = 100;
static int curr_test_num;
static int nalSize = 0;
static int rcv_v1 = 0;

OMX_U8* input_use_buffer;
OMX_U8* output_use_buffer;
OMX_U8 output_use_buffer_qcif[304128];
OMX_U8 output_use_buffer_qvga[921600];
OMX_U8 output_use_buffer_hvga[2119680];
OMX_U8 output_use_buffer_vga[3686400];

//* OMX Spec Version supported by the wrappers. Version = 1.1 */
const OMX_U32 CURRENT_OMX_SPEC_VERSION = 0x00000101;
OMX_COMPONENTTYPE* avc_dec_handle = 0;

OMX_BUFFERHEADERTYPE  **pInputBufHdrs = NULL;	
OMX_BUFFERHEADERTYPE  **pOutYUVBufHdrs= NULL;

const char *video_input_file_names[] = {
    /* H264 input filenames */
    "QCIFinputvideo.dat",
    "QVGAinputvideo.dat"
    "HVGAinputvideo.dat",
    "VGAinputvideo.dat"
};

const char *video_output_file_names[] = {
    /* H264 output filenames */
    "QCIFoutputvideo.yuv",
    "QVGAoutputvideo.yuv",
    "HVGAoutputvideo.yuv",
    "VGAoutputvideo.yuv",
};

#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA) 
/* Performance related variable*/
QPERF_INIT(render_fb);
QPERF_INIT(client_decode);
#endif


struct gv_table_s {
    char varname[40];
    void *ptr;
};
// For now only for Int vars
const struct gv_table_s gtGlobalIntVarTable[] = {
    { "file_write", (void*)& file_write },
    { "fb_display", (void*)& fb_display },
    { "fb_fd", (void*)& fb_fd },
    { "bInputEosReached", (void*)& bInputEosReached },
    { "bOutputEosReached", (void*)& bOutputEosReached },
    { "geAppState", (void*)& geAppState },
    { "gbDumpDecodedIframes", (void*)& gbDumpDecodedIframes },
    { "gnDumpedDecIframeCnt", (void*)& gnDumpedDecIframeCnt },
    { "gbFfwd_FlushBeforeISeek", (void*)& gbFfwd_FlushBeforeISeek },
    { "gbFfwd_PauseOnIframeDec", (void*)& gbFfwd_PauseOnIframeDec },
    { "gnFfwd_IFrameSkipCnt", (void*)& gnFfwd_IFrameSkipCnt },
    { "gnFfwd_NrUnitsToDecode", (void*)& gnFfwd_NrUnitsToDecode },
    { "gnInputUnitCnt", (void*)& gnInputUnitCnt },
    { "gnOutputUnitCnt", (void*)& gnOutputUnitCnt },
    { "curr_test_num", (void*)& curr_test_num },
    { "nalSize", (void*)& nalSize },
    { "gbLoopedPlay", (void*)& gbLoopedPlay },
    { "gbNoEosInLoopedPlay", (void*)& gbNoEosInLoopedPlay },
    { "gnTotalOutputBufs", (void*)& gnTotalOutputBufs },
    { "gnTotalInputBufs", (void*)& gnTotalInputBufs },
    { "gnAbortLevel", (void*)& gnAbortLevel },        
    { "gbDontFreeHandle", (void*)& gbDontFreeHandle },
    { "gbNoWaitEosOnLastBuff", (void*)& gbNoWaitEosOnLastBuff },
    { "gbNoBufferFlagEosWait", (void*)& gbNoBufferFlagEosWait },
    { "gbDontWaitForBuffs", (void*)& gbDontWaitForBuffs },
    { "gbSetSyncFlag", (void*)& gbSetSyncFlag },
    { "gnTimeoutSecs", (void*)& gnTimeoutSecs },
    { "gnTimeoutUSecs", (void*)& gnTimeoutUSecs },
    { "gnWatchDogInterval", (void*)& gnWatchDogInterval },
    { "gbWatchDogOn", (void*)& gbWatchDogOn },
    { "gbWatchDogEnabled", (void*)& gbWatchDogEnabled },
    { "gbProfileMode", (void*)& gbProfileMode },
    { "gnProfileFPS", (void*)& gnProfileFPS },
    { "gnProfileSleepUsecs", (void*)& gnProfileSleepUsecs },
    { "gbSendEosAtStart", (void*)& gbSendEosAtStart },
    { "gbCollectBuffsInPaused", (void*)& gbCollectBuffsInPaused },
    { "gbMarkBuffsFreeAfterPause", (void*)& gbMarkBuffsFreeAfterPause },
    { "gbForceFeedBuffsOnResume", (void*)& gbForceFeedBuffsOnResume },
    { "gbForceFeedBuffsOnLooping", (void*)& gbForceFeedBuffsOnLooping },
    { "gbEnableOmxStateCheck", (void*)& gbEnableOmxStateCheck },
    { "gnLogLevel", (void*)& gnLogLevel },
    { "gbFramePackArbitrary", (void*)& gbFramePackArbitrary },
    { "gbWaitFirstFrameDec", (void*)& gbWaitFirstFrameDec },
    { "gbFfwd_ErrorSeekMode", (void*)& gbFfwd_ErrorSeekMode },
    { "gbDumpInputBuffer", (void*)& gbDumpInputBuffer },
    { "gnDisplayWindow", (void*)& gnDisplayWindow },
};


/************************************************************************/
/*				GLOBAL FUNC DECL                        */
/************************************************************************/

extern void TEST_UTILS_H264_Init(void);
extern void TEST_UTILS_H264_DeInit(void);
extern int TEST_UTILS_H264_IsNewFrame(OMX_IN OMX_U8 *buffer, OMX_IN OMX_U32 buffer_length, OMX_IN OMX_U32 size_of_nal_length_field);
extern int TEST_UTILS_H264_AllocateRBSPBuffer(unsigned int inbuf_size);
/**************************************************************************/
/*				STATIC DECLARATIONS                       */
/**************************************************************************/
static vidFrame    frame;
//static int video_playback_count = 1; 

char *my_rindex (char *s, int c);
void my_fcloseall(void);
static const char *abort_err_str(int errcode);
static int Init_Decoder();
static int Play_Decoder(video_display_format_type format);
static int run_tests(video_display_format_type format);
static int open_video_file (video_display_format_type format, char *infile, char *outfile);
static int Read_Buffer_From_DAT_File(OMX_BUFFERHEADERTYPE  *pBuff );
static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBuff );
static int Read_Buffer_ArbitraryBytes(OMX_BUFFERHEADERTYPE  *pBuff);
static int Read_Buffer_From_Vop_Start_Code_File(OMX_BUFFERHEADERTYPE  *pBuff);
static int Read_Buffer_From_FrameSize_File(OMX_BUFFERHEADERTYPE  *pBufHdr);
static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBuff);
static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBuff);
static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBuff);

static OMX_ERRORTYPE Allocate_Buffer ( OMX_COMPONENTTYPE *avc_dec_handle, 
                                       OMX_BUFFERHEADERTYPE  ***pBuffs, 
                                       OMX_U32 nPortIndex, 
                                       long bufCntMin, 
                                       long bufSize); 


static OMX_ERRORTYPE EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                                  OMX_IN OMX_PTR pAppData, 
                                  OMX_IN OMX_EVENTTYPE eEvent,
                                  OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2, 
                                  OMX_IN OMX_PTR pEventData);
static OMX_ERRORTYPE EBD_Handler(OMX_IN OMX_HANDLETYPE hComponent, 
                                     OMX_IN OMX_PTR pAppData, 
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE FBD_Handler(OMX_IN OMX_HANDLETYPE hComponent, 
                                     OMX_IN OMX_PTR pAppData, 
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent, 
                                     OMX_IN OMX_PTR pAppData, 
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);
static OMX_ERRORTYPE FillBufferDone(OMX_OUT OMX_HANDLETYPE hComponent, 
                                    OMX_OUT OMX_PTR pAppData, 
                                    OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer);

static int user_input_from_menu(void);
static int token_2_enum(int how, char *token, int index);
static int set_height_width(video_display_format_type format, int *h, int *w);
static int handle_port_settings_changed(int port);
static int print_timestamp(int how, char *outbuf, FILE *outstream);
static int myPrintf(char *format, ...);
static int RegularPrintf(char *format, ...);
static void setup_logging(int how, int loglevel);
static int dump_omx_buffers(int how, int which);
static int wait_for_event(unsigned int waitsecs, unsigned int waitusecs, int err_continue);
static void event_complete(OMX_EVENTTYPE eEvent, int nData1, int nData2, void *pEventData);
static void clear_event(void);
static int map_omx_state_name(int how, OMX_STATETYPE *state, char *statename);
static int appQ_post_event(app_event_t event, int data, const void *pMoreData, int blocking);
static void terminate(int justfreehandle);
static int debug_printf(const char *file, int line, const char *function, int importance, char *format, ...);
static void watchdog_switch(int turn_on);

/* Keep a table of global vars & pointers to them so that their values can be dynamically set through LASIC thread from linux shell 
(used more for debugging)
varname : Name of global whose pointer & value will be returned
len     : Length of varname */
static void *
get_ptr_to_global(char *varname, int len)
{
    int i;

    for(i=0; i<NUMELEM_ARRAY(gtGlobalIntVarTable); i++)
        if(! strncmp(varname, gtGlobalIntVarTable[i].varname, len))
            return gtGlobalIntVarTable[i].ptr;
    return NULL;
}

/* Atomic read of global varname
NOTE : NO DPRINT type macros to be used here since this function can be called within DPRINT statements. 
ONLY use regular printf statements */
static unsigned int
get_curr_val_global(char *varname)
{
    int i;
    unsigned int val;

    for(i=0; i<NUMELEM_ARRAY(gtGlobalIntVarTable); i++)
        if(! strcmp(varname, gtGlobalIntVarTable[i].varname)) {
            LOCK_MUTEX(&gtGlobalVarMutex);
            val = *(unsigned int *)gtGlobalIntVarTable[i].ptr;
            UNLOCK_MUTEX(&gtGlobalVarMutex);
            return val;
        }
    (gtMyStdout != NULL && gtMyStdout != stdout) && fprintf(gtMyStdout , "\n%s(): ERROR: Global Varname '%s' not found in global var table", __FUNCTION__, varname);
    fprintf(stdout, "\n%s(): ERROR: Global Varname '%s' not found in global var table", __FUNCTION__, varname);
    exit(222);
}


char *
my_rindex (char *s, int c)
{
  return strrchr (s, c);
}

static long 
diff_timeofday(struct timeval *tv1, struct timeval *tv2)
{
    return ((tv1->tv_sec - tv2->tv_sec)*1000000 + (tv1->tv_usec - tv2->tv_usec));
}

/* Prints the current timestamp 
   IF how == 0 : Print timestamp to outstream 
          == 1 : Write timestamp to outbuf */
static int 
print_timestamp(int how, char *outbuf, FILE *outstream) {
    struct tm gmt; 
    struct timeval tv;
    int msec, umsec;

    (void)gettimeofday(&tv, NULL);
    (void)localtime_r(&tv.tv_sec, &gmt);
    msec = tv.tv_usec/1000;
    umsec = tv.tv_usec - msec*1000;
    if(0 == how && outstream)
        return fprintf(outstream, "\n#### [%02d:%02d:%02d] [%03d:%03d] #### ", gmt.tm_hour, gmt.tm_min, gmt.tm_sec, msec, umsec); 
    else if(1 == how && outbuf)
        return sprintf(outbuf, "\n#### [%02d:%02d:%02d] [%03d:%03d] #### ", gmt.tm_hour, gmt.tm_min, gmt.tm_sec, msec, umsec);
    else
        return 0;
}

static int 
myPrintf(char *format, ...) {
    va_list ap;
    int cnt;

    while(format && *format && isspace(*format))
        ++format;

    va_start(ap, format);
    cnt = vprintf((const char*)format, ap);
    va_end(ap);
    return cnt;
}

static int 
RegularPrintf(char *format, ...) {
    va_list ap;
    int cnt;
    // Any processing ?
    va_start(ap, format);
    cnt=vprintf((const char*)format, ap);
    va_end(ap);
    return cnt;
}

/* Prints based on current logging level. Log level=0 is highest. 
   importance of 0-2 : Errors 
                 3-5 : Warnings
                 6-8 : Info (only upto importance==6 will be printed in performance mode). 7-8 are HIGH FREQUENCY messages (affects performance)
                 9-10 : Debug messages (HIGH FREQUENCY : affects performance)
                 11 & higher are for VERY HIGH FREQUENCY debug messages (printing these may considerably degrade performance)
   THREAD SAFE : Yes
   IF function == NULL : Don't print Filename / Line# / Function name
   IF line == 0        : Don't print timestamp  
*/
static int 
debug_printf(const char *file, int line, const char *function, int importance, char *format, ...) 
{
    va_list ap;
    int cnt;
    static char *fname = NULL;
    char *lvlstr = "INF";
    FILE *mystdout;
    char timestamp[80];
    static unsigned int nMesgNum = 0;

    /* Lower values of logLevel filter higher priority messages (hence importance==0 is highest priority) */
    if(gnLogLevel < importance)
        return 0;
    else if(importance <= 2)
        lvlstr = "ERR";
    else if(importance >= 3 && importance <= 5)
        lvlstr = "WRN";
    else if(importance >= 6 && importance <= 8)
        lvlstr = "INF";
    else 
        lvlstr = "DBG";

    mystdout = gtMyStdout;
    /*if(NULL == fname && file && *file)
        fname = (my_rindex(file, '/')==NULL ? file : my_rindex(file, '/') + 1); */

    // Skip any spaces/newlines since print_timestamp will already print on a new line
    while(format && *format && isspace(*format))
        ++format;

#ifdef USE_PRINT_MUTEX
    LOCK_MUTEX_HIFREQ(&gtPrintMutex);  // To serialise prints
    nMesgNum++;
#endif

    if(line != 0) {
        char temp[sizeof(timestamp)];
        int len;
        len = print_timestamp(1, timestamp, NULL); 
        snprintf(timestamp + len, sizeof(timestamp)-len, "[Msg %06d] ", nMesgNum);
    }
    else { 
        snprintf(timestamp, sizeof(timestamp), "\n[Msg %06d] ", nMesgNum);
    }

    if(function != NULL) {
        fprintf(mystdout, "%s<APP/%s> :: Line %05d :: %s() :: ", timestamp, lvlstr, line, function);
        (mystdout != stdout) && fprintf(stdout, "%s<APP/%s> :: Line %05d :: %s() :: ", timestamp, lvlstr, line, function);
    }
    else {
        fprintf(mystdout, "%s", timestamp);
        (mystdout != stdout) && fprintf(stdout, "%s", timestamp);
    }

    va_start(ap, format);
    cnt=vfprintf(mystdout, (const char*)format, ap);
    va_end(ap);

    va_start(ap, format);
    (mystdout != stdout) && vfprintf(stdout, (const char*)format, ap);
    va_end(ap);

    fflush(mystdout);
    fflush(stdout);

#ifdef USE_PRINT_MUTEX
    UNLOCK_MUTEX_HIFREQ(&gtPrintMutex);
#endif
    return cnt;
}


static const char * 
abort_err_str(int errcode) {
    int i;
    static char unknown[] = "APP_ERR_UNKNOWN";

    for(i=0; i<NUMELEM_ARRAY(gtAbortErrs); i++) {
        if(errcode >= gtAbortErrs[i].errmin && errcode <= gtAbortErrs[i].errmax)
            return(gtAbortErrs[i].err_string);
    }
    return unknown;
}

void 
my_fcloseall(void)
{
/* #ifdef _GNU_SOURCE
    fcloseall();
#else */
    DPRINT(_HERE, 6, "\nClosing all files");
    inputBufferFile && (fclose(inputBufferFile), DPRINT(_HERE, 6, "\nInputFile closed"));
    outputBufferFile && (fclose(outputBufferFile), DPRINT(_HERE, 6, "\nOutputFile closed"));
    outputIFramesFile && (fclose(outputIFramesFile), DPRINT(_HERE, 6, "\nOutIFramesFile closed"));
    outputEncodedIFramesFile && (fclose(outputEncodedIFramesFile), DPRINT(_HERE, 6, "\nOutEncIFramesFile closed"));
    inBufferDumpFile && (fclose(inBufferDumpFile), DPRINT(_HERE, 6, "inBufferDumpFile closed"));
    inputBufferFile = outputBufferFile = outputIFramesFile = outputEncodedIFramesFile = inBufferDumpFile = NULL;
//#endif
    (fb_fd >= 0) && (close(fb_fd), fb_fd = -1, DPRINT(_HERE, 6, "\nFB dev file closed"));
    DPRINT(_HERE, 6, "\nClosed all files");
    gtMyStdout && (gtMyStdout != stdout) && fclose(gtMyStdout);
    gtMyStdout = stdout;
}

static void
print_stats_summary(void)
{
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : INIT TIME=%.2f secs", diff_timeofday(&gtInitEndTimeVal, &gtInitStartTimeVal)/1000000.0);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : DE-INIT TIME=%.2f secs", diff_timeofday(&gtDeinitEndTimeVal, &gtDeinitStartTimeVal)/1000000.0);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : FIRST FRAME DECODE TIME=%.2f secs", diff_timeofday(&gtFirstFrameDecTimeVal, &gtDecStartTimeVal)/1000000.0);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : TOTAL UNITS SENT FOR DECODE=%d", gnInputUnitCnt);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : TOTAL DECODED OUTPUT UNITS=%d", gnOutputUnitCnt);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : TOTAL I-FRAMES FOUND=%d", gnIFrameCount);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : TOTAL FRAME DROPS=%d", gnInputUnitCnt-gnOutputUnitCnt);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : TOTAL DECODE TIME=%.2f secs", diff_timeofday(&gtDecEndTimeVal, &gtDecStartTimeVal)/1000000.0);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : AVG OUTPUT FRAME RATE=%.2f fps",  gnOutputUnitCnt / (diff_timeofday(&gtDecEndTimeVal, &gtDecStartTimeVal)/(1000000.0)));
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : AVG FLUSH Latency=%.3f millisec [FlushCnt=%d]",  gnFlushCount ? ((gnFlushLatency/1000.0) / (gnFlushCount*1.0)) : 0, gnFlushCount);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : AVG EBD Callback Latency=%.2f millisec",  ebd_cnt ? ((gnEBDLatency/1000.0) / (ebd_cnt*1.0)) : 0);
    DPRINT(_NOHERE, 0, "\nAPP : DECODER SUMMARY : AVG FBD Callback Latency=%.2f millisec\n\n",  fbd_cnt ? ((gnFBDLatency/1000.0) / (fbd_cnt*1.0)) : 0);
}

static void 
dump_global_data(void)
{
    DPRINT(_HERE, 8, "\nEntered...");
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: geAppState=%d", geAppState);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnLoopCount=%d", gnLoopCount);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnNALUnitCnt=%d", gnNALUnitCnt);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnIFrameCount=%d", gnIFrameCount);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnEtbCnt=%d / ebd_cnt=%d / fbd_cnt=%d", gnEtbCnt, ebd_cnt, fbd_cnt);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnInputUnitCnt=%d / gnOutputUnitCnt=%d", gnInputUnitCnt, gnOutputUnitCnt);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnTotalInputBufs=%d / gnTotalOutputBufs=%d", gnTotalInputBufs, gnTotalOutputBufs);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: bInputEosReached=%d / bOutputEosReached=%d", bInputEosReached, bOutputEosReached);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnUsedInputBuffsMap=%llu / gnUsedOutputBuffsMap=%llu", gnUsedInputBuffsMap, gnUsedOutputBuffsMap);
    gbDumpDecodedIframes && DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnDumpedDecIframeCnt=%d", gnDumpedDecIframeCnt);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnAppQIndex=%d", gnAppQIndex);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: h264_dec_handle=0x%x", avc_dec_handle);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: State Info :: gnEBDLatency=%u / gnFBDLatency=%u / gnFlushLatency=%u", gnEBDLatency, gnFBDLatency, gnFlushLatency);
    DPRINT(_NOHERE, 0, "\n");
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbLoopedPlay=%d / gbNoEosInLoopedPlay=%d / gbForceFeedBuffsOnLooping=%d", gbLoopedPlay, gbNoEosInLoopedPlay, gbForceFeedBuffsOnLooping);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbCollectBuffsInPaused=%d / gbMarkBuffsFreeAfterPause=%d / gbForceFeedBuffsOnResume=%d", gbCollectBuffsInPaused, gbMarkBuffsFreeAfterPause, gbForceFeedBuffsOnResume);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbFfwd_ErrorSeekMode=%d / gnFfwd_IFrameSkipCnt=%d / gnFfwd_NrUnitsToDecode=%d", gbFfwd_ErrorSeekMode, gnFfwd_IFrameSkipCnt, gnFfwd_NrUnitsToDecode);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbFfwd_FlushBeforeISeek=%d / gnFlushCount=%d", gbFfwd_FlushBeforeISeek, gnFlushCount);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: codec_format_option=%d / file_type_option=%d", codec_format_option, file_type_option);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: nalSize=%d", nalSize);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gnDisplayWindow=%d / fb_display=%d / file_write=%d", gnDisplayWindow, fb_display, file_write);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: height=%d / width=%d", height, width);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: curr_test_num=%d", curr_test_num);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbDumpDecodedIframes=%d / gbFfwd_PauseOnIframeDec=%d", gbDumpDecodedIframes, gbFfwd_PauseOnIframeDec);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbDontFreeHandle=%d", gbDontFreeHandle);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gnAbortLevel=%d / gnLogLevel=%d", gnAbortLevel, gnLogLevel);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gnTimeoutSecs=%d / gnTimeoutUSecs=%d", gnTimeoutSecs, gnTimeoutUSecs);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbWatchDogEnabled=%d / gnWatchDogInterval=%d", gbWatchDogEnabled, gnWatchDogInterval);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbSetSyncFlag=%d", gbSetSyncFlag);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbSendEosAtStart=%d", gbSendEosAtStart);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbNoWaitEosOnLastBuff=%d / gbNoBufferFlagEosWait=%d", gbNoWaitEosOnLastBuff, gbNoBufferFlagEosWait);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbDumpInputBuffer=%d", gbDumpInputBuffer);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbDontWaitForBuffs=%d", gbDontWaitForBuffs);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbProfileMode=%d [%s] / gnProfileFPS=%d", gbProfileMode, gbProfileMode ? (2 == gbProfileMode ? "Delay in FILLBufferDone" : "Delay in EMPTYBufferDone") : "NO delay", gnProfileFPS);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbEnableOmxStateCheck=%d", gbEnableOmxStateCheck);
    DPRINT(_NOHERE, 0, "\nGLOBAL VAR DUMP: SETTINGS :: gbFramePackArbitrary=%d / gbWaitFirstFrameDec=%d", gbFramePackArbitrary, gbWaitFirstFrameDec);
    DPRINT(_HERE, 8, "\nLeaving...");
}

/* Do a hex dump
   if(method == 0) then 'output' should be a FILE* & dump is written to a file
   if(method == 1) then 'output' is a char buffer & dump is written to buffer
   nr_columns : The nr. of bytes of packet dumped to a single line
   NOTE : If char buffer is used then its size should be atleast 3*packetlen+1
   THREAD SAFE : Yes
*/
void hex_dump_packet(void *output, int method, const void *packet, int packetlen, int nr_columns)
{
  int i, bytes;

  DPRINT(_HERE, 6, "Entered...");
  if((output && packet) == 0)
        return; 

  for(i=0; i<packetlen; i++) {
      if(1 == method) {
          bytes = sprintf((char*)output, "%c%.2x", ((0 == i% nr_columns) ? '\n' : ' '), ((char*)packet)[i]);
          output = (char*)output + bytes;
      }
      else
          bytes = fprintf((FILE*)output, "%c%.2x", ((0 == i% nr_columns) ? '\n' : ' '), ((char*)packet)[i]);
  }
  DPRINT(_HERE, 6, "Leaving...");
}

/* Searches OMX buffer header pointer list to find a match with the buffer header passed as parameter 
IF dir == 0 : Search input headers 
       == 1 : Search output headers
RETURNS: index (buffer number starting from 0) to buffer header list if pBuffHdr is found in the list
         -1 if pBuffHdr is not found in the list 
*/
static int
get_buffer_num(int dir, OMX_OUT OMX_BUFFERHEADERTYPE* pBuffHdr)
{
    int i;

    // Input buffers
    if(0 == dir) {
        for (i=0; i<gnTotalInputBufs; i++) 
            if(pInputBufHdrs[i] == pBuffHdr) 
                return i;

        DPRINT(_HERE, 0, "ERROR: Got invalid INPUT buffer header pointer=0x%x", pBuffHdr);
        ERROR_ACTION(0, 212);
        return -1;
    }
    else {
        for (i=0; i<gnTotalOutputBufs; i++) 
            if(pOutYUVBufHdrs[i] == pBuffHdr) 
                return i;

        DPRINT(_HERE, 0, "ERROR: Got invalid OUTPUT buffer header pointer=0x%x", pBuffHdr);
        ERROR_ACTION(0, 212);
        return -1;
    }
}

/* Returns info about buffer (whether it is currently used or unused & empty or non-empty
IF dir == 0 : Assume input buffer
       == 1 : Assume output buffer

RETURNS : +ve if buffer currently used & -ve if unused & 1 for empty & 2 for non-empty ie...
          -2 : unused & NON-empty   buffer
          -1 : unused & empty       buffer
           0 : ERROR (invalid bufnum)
           1 : used & empty         buffer
           2 : used & NON-empty     buffer */
static int 
get_buffer_status(int dir, int bufnum)
{
    int ret = 1;

    //DPRINT(_HERE, 8, "\nEntered with dir=%d, bufnum=%d", dir, bufnum);
    // Input buffers
    if(0 == dir) {
        if(bufnum < 0 || bufnum >= gnTotalInputBufs)
            return 0;
        if(pInputBufHdrs[bufnum]->nFilledLen != 0)
            ++ret;
        (gnUsedInputBuffsMap & (1ul << bufnum)) ? 0 : (ret = -ret);
    }
    // Output buffers
    else {
        if(bufnum < 0 || bufnum >= gnTotalOutputBufs)
            return 0;
        if(pOutYUVBufHdrs[bufnum]->nFilledLen != 0)
            ++ret;
        (gnUsedOutputBuffsMap & (1ul << bufnum)) ? 0 : (ret = -ret);
    }
    return ret;
}

/* Updates the list of used buffers
If dir == 0 : Assumes buffer is for input 
       == 1 : Assumes buffer is for output
IF used == 0 : Marks buffer as free
        == 1 : Marks buffer as used
RETURNS: 1 on success
         0 in case of errors */
static int 
update_used_buffer_list(int dir, int used, OMX_OUT OMX_BUFFERHEADERTYPE* pBuffHdr)
{
    int bufnum;

    // Input buffer
    if(0 == dir && (bufnum = get_buffer_num(0, pBuffHdr)) < sizeof(unsigned long long)*8 && bufnum > -1) {
        used ? (gnUsedInputBuffsMap |= 1ul << bufnum) : (gnUsedInputBuffsMap &= ~(1ul << bufnum));
    }
    // Output buffer
    else if(0 != dir && (bufnum = get_buffer_num(1, pBuffHdr)) < sizeof(unsigned long long)*8 && bufnum > -1) {
        used ? (gnUsedOutputBuffsMap |= 1ul << bufnum) : (gnUsedOutputBuffsMap &= ~(1ul << bufnum));
    }
    else if(bufnum >= sizeof(unsigned long long)*8) {
        DPRINT(_HERE, 3, "WARNING: Nr. of buffers > sizeof(unsigned long long)... dir=%d, bufnum=%d, used=%d", dir, bufnum, used);
        ERROR_ACTION(4, 222);
        return 0;
    }
    else {
        DPRINT(_HERE, 0, "ERROR: dir=%d, bufnum=%d, used=%d, pBuffHdr=0x%x", dir, bufnum, used, pBuffHdr);
        ERROR_ACTION(0, 222);
        return 0;
    }
    return 1;
}

/* Prints the list of used buffers
If dir == 1 : Print list for input buffers
       == 11 : Print list for output buffers
       == 199 : Print list for both input & output */
static void print_used_buffer_list(int dir)
{
    int i, len;
    char str[sizeof(unsigned long long)*3 + 30] = "";

    DPRINT(_HERE, 10, "Entered with dir=%d", dir);
    // Input buffers
    if(1 == dir || 199 == dir) {
        for (i=0; i<gnTotalInputBufs; i++) 
            if(gnUsedInputBuffsMap & (1ul << i)) {
                len = strlen(str);
                sprintf(str + len, "#%02d# ", i);
            }
            DPRINT(_NOHERE, 6, "#### USED INPUT BUFFER LIST: '%s' ####", str);
    }
    strcpy(str, "");
    if(11 == dir || 199 == dir) {
        for (i=0; i<gnTotalOutputBufs; i++) 
            if(gnUsedOutputBuffsMap & (1ul << i)) {
                len = strlen(str);
                sprintf(str + len, "#%02d# ", i);
            }
            DPRINT(_NOHERE, 6, "#### USED OUTPUT BUFFER LIST: '%s' ####", str);
    }
    DPRINT(_HERE, 10, "Leaving...");
}

/* Pretty much the same as stop_and_cleanup() but this tries whatever cleanup can be done & doesn't check for failure 
IF justfreehandle == 1 then only free handle & deinit */
static void 
terminate(int justfreehandle)
{
    int bufCnt = 0, result;
    OMX_STATETYPE omx_currstate;
    char statename[APP_OMX_STATENAME_SIZE+1];

    TEST_UTILS_H264_DeInit();
    DPRINT(_HERE, 6, "TEST_UTILS_H264_DeInit() Done... ");
    APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
    watchdog_switch(false); 
    if(0 == justfreehandle && (result = OMX_GetState(avc_dec_handle, &omx_currstate)) != OMX_ErrorNone) {
        map_omx_state_name(0, &omx_currstate, statename);
        if(omx_currstate != OMX_StateIdle && omx_currstate != OMX_StateInvalid && omx_currstate != OMX_StateLoaded) {
            DPRINT(_HERE, 6, "\nTRYING OMX_SendCommand Decoder [%s->Idle]", statename);
            clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
            wait_for_event(0, 500000, true);
            DPRINT(_HERE, 6, "DONE trying [XXX -> Idle]... ");
        }
        if(OMX_StateIdle == omx_currstate || OMX_StateInvalid == omx_currstate) {
            DPRINT(_HERE, 6, "\nTRYING OMX_SendCommand Decoder [Idle->Loaded]");
            clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);
            DPRINT(_HERE, 6, "\nDeallocating i/p and o/p buffers \n");
            for(bufCnt=0; bufCnt < gnTotalInputBufs; ++bufCnt) {
                OMX_FreeBuffer(avc_dec_handle, 0, pInputBufHdrs[bufCnt]);
            }

            for(bufCnt=0; bufCnt < gnTotalOutputBufs; ++bufCnt) {
                OMX_FreeBuffer(avc_dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
            }
            wait_for_event(0, 500000, true);   
            DPRINT(_HERE, 6, "DONE trying [Idle -> Loaded]... ");
        }
    }
    DPRINT(_HERE, 6, "\nCalling FreeHandle...");
    result = OMX_FreeHandle(avc_dec_handle);
    DPRINT(_HERE, 6, "\nFreeHandle done with result=%s", result == OMX_ErrorNone ? "SUCCESS" : "FAIL");

    /* Deinit OpenMAX */
    DPRINT(_HERE, 6, "\nDe-initializing OMX ");
    OMX_Deinit();
    DPRINT(_HERE, 6, "\nDONE: De-init OMX ");
    DPRINT(_HERE, 6, "\nLeaving... ");
}


/* Waits on pthread_cond for specified timeout period 
IF err_continue == 1 then don't abort in case of failure / timeout
RETURNS : 
    0 = success
   -1 = timeout */
static int
wait_for_event(unsigned int waitsecs, unsigned int waitusecs, int err_continue)
{
    struct timeval currtime;
    struct timespec timeout;
    int retcode;

    LOCK_MUTEX(&lock);
    DPRINT(_HERE, 8, "\nwait_for_event() Entered [event_is_done=%d] with wait_secs=%d, wait_usecs=%u...", event_is_done, waitsecs, waitusecs);
    gettimeofday(&currtime, 0);
    timeout.tv_sec = currtime.tv_sec + waitsecs;
    timeout.tv_nsec = (currtime.tv_usec + waitusecs)*1000u;
    retcode = 0;
    while (event_is_done == 0 && retcode != ETIMEDOUT) {
        retcode = pthread_cond_timedwait(&cond, &lock, &timeout);
    }
    event_is_done = 0;
    if (retcode == ETIMEDOUT) {
        DPRINT(_HERE, 0, "ERROR : Mutex/Cond wait timeout");
        UNLOCK_MUTEX(&lock);
        if(false == err_continue)
             ERROR_ACTION(0, 62);
        return -1;
    }
    DPRINT(_HERE, 8, "\nwait_for_event() Leaving [event_is_done=%d]...", event_is_done);
    UNLOCK_MUTEX(&lock);
    return 0;
}

static void 
event_complete(OMX_EVENTTYPE eEvent, int nData1, int nData2, void *pEventData)
{
    LOCK_MUTEX(&lock);
    DPRINT(_HERE, 8, "\nevent_complete() Entered [event_is_done=%d] with eEvent=%u, nData1=%u, nData2=%u, pEventData=0x%x...", event_is_done, eEvent, nData1, nData2, pEventData);
    if (event_is_done == 0) {
        event_is_done = 1;
        pthread_cond_broadcast(&cond);
    }
    DPRINT(_HERE, 8, "\nevent_complete() Leaving [event_is_done=%d, eEvent=%u, nData1=%u, nData2=%u, pEventData=0x%x]...", event_is_done, eEvent, nData1, nData2, pEventData);
    UNLOCK_MUTEX(&lock);
}

static void 
clear_event(void) 
{
    LOCK_MUTEX(&lock);
    event_is_done = 0;
    DPRINT(_HERE, 6, "\nCleared data [event_is_done=%d] ", event_is_done);
    UNLOCK_MUTEX(&lock);
}

static void *
watchdog_thread(void *arg)
{
    sigset_t threadsigset;

    /* 05/11/09 - commented out 
    sigfillset(&threadsigset);
    if(pthread_sigmask(SIG_BLOCK, &threadsigset, NULL)) {
        DPRINT(_HERE, 2, "ERROR: pthread_sigmask() failed. Couldn't block signals !");
    } */
    DPRINT(_HERE, 6, "\nWatchdog thread started...");

    while(1) {
        LOCK_MUTEX(&gtWatchdogMutex);
        gnLastEbdCnt = ebd_cnt;
        gnLastFbdCnt = fbd_cnt;
        UNLOCK_MUTEX(&gtWatchdogMutex);
        sleep(gnWatchDogInterval);
    
        LOCK_MUTEX(&gtWatchdogMutex);
        if(gbWatchDogOn && gnLastEbdCnt == ebd_cnt && gnLastFbdCnt == fbd_cnt) {
            UNLOCK_MUTEX(&gtWatchdogMutex);
            DPRINT(_HERE, 0, "\nWATCHDOG TIMEOUT (ebd_cnt=%d / gnLastEbdCnt=%d ; fbd_cnt=%d / gnLastFbdCnt=%d ; Timeoutsecs=%d)", ebd_cnt, gnLastEbdCnt, fbd_cnt, gnLastFbdCnt, gnWatchDogInterval);
            ERROR_ACTION(0, 60);
            exit(60);
        }
        else if(gbWatchDogOn) {
            UNLOCK_MUTEX(&gtWatchdogMutex);
            DPRINT(_HERE, 6, "Watchdog check OK [ebd_cnt=%d / LastEbdCnt=%d ; fdb_cnt=%d / LastFbdCnt=%d]... ", ebd_cnt, gnLastEbdCnt, fbd_cnt, gnLastFbdCnt);
        }
        else
            UNLOCK_MUTEX(&gtWatchdogMutex);
    }
}

static void 
watchdog_switch(int turn_on)
{
    if(! gbWatchDogEnabled)
        return;
    LOCK_MUTEX(&gtWatchdogMutex);
    /* This avoids race conditions/false alarms like: 
       Watchdog is Off -> watchdog thread wakes up after timeout sleep -> Watchdog is turned ON by another thread -> ebd/fbd etc are same
       & hence watchdog errors out (although it is expected that after it is turned ON, it should wait for the full timeout period), etc */
    gnLastEbdCnt = -1;
    gnLastFbdCnt = -1;
    if(false == turn_on) {
        gbWatchDogOn = false;
    }
    else {
        gbWatchDogOn = true;
    }
    UNLOCK_MUTEX(&gtWatchdogMutex);
    DPRINT(_HERE, 6, "WATCHDOG NOW: %s", gbWatchDogOn ? "ON" : "OFF");
}

unsigned int atoi_big_endian(void *buff, int numbytes)
{
  unsigned int val=0;
  int i;

  if(numbytes > sizeof(unsigned int))
      numbytes=sizeof(unsigned int);
  for(i=0; i<numbytes; i++) {
        val = (val*10) + (unsigned int)*((unsigned char*)buff + i);
        //DPRINT(_HERE, 8, "\n%s(): i=%d, byte=%d, val=%u", __FUNCTION__, i, (unsigned int)*((unsigned char*)buff+i), val);
  }

  return val;
}

unsigned int atoi_little_endian(void *buff, int numbytes)
{
  unsigned int val=0;
  int i;

  if(numbytes > sizeof(unsigned int))
      numbytes=sizeof(unsigned int);
  for(i=numbytes-1; i>=0; i--) {
        val = (val * 10) + (unsigned int)*((unsigned char*)buff + i);
        //DPRINT(_HERE, 8, "\n%s(): i=%d, byte=%d, val=%u", __FUNCTION__, i, (unsigned int)*((unsigned char*)buff+i), val);
  }

  return val;
}


/* Function to read input frame(s) into input buffers according to the current codec & frame packing format */
static int 
Read_Buffer(OMX_BUFFERHEADERTYPE  *pBuff)
{
    int retval;

    if(Read_Buffer_Func == NULL) {
        DPRINT(_HERE, 0, "ERROR: Read_Buffer_Func is NULL");
        ERROR_ACTION(0, 222);
    }
#ifdef USE_INPUT_FILE_MUTEX
    LOCK_MUTEX(&gtInputFileMutex);
#endif
    retval = Read_Buffer_Func(pBuff);
    pBuff->nInputPortIndex = 0;     // 03/27/09
#ifdef USE_INPUT_FILE_MUTEX
    UNLOCK_MUTEX(&gtInputFileMutex);
#endif

    return retval;
}

/* Maps between an OMX_STATETYPE value and it's corresponding string equivalent
IF how == 0 then map (*state) value into an OMX statename string & WRITE the string into (statename)
   how == 1 then map (statename) string into an OMX state value & WRITE the value into (state)
    
RETURNS : 1 on success
          0 otherwise
*/
static int
map_omx_state_name(int how, OMX_STATETYPE *state, char *statename)
{
    static const OMX_STATETYPE omx_states[] = { 
        OMX_StateLoaded, OMX_StateIdle, OMX_StateExecuting, OMX_StatePause, 
        OMX_StateInvalid, OMX_StateWaitForResources, 
    };
    static const char omx_state_names[][APP_OMX_STATENAME_SIZE] = {                
        "OMX_StateLoaded", "OMX_StateIdle", "OMX_StateExecuting", "OMX_StatePause", 
        "OMX_StateInvalid", "OMX_StateWaitForResources", 
    };
    int size = NUMELEM_ARRAY(omx_states);
    int i;

    for(i=0; i<size; i++) {
        if(0 == how && omx_states[i] == *state) {
            strcpy(statename, omx_state_names[i]);
            return 1;
        }
        else if(1 == how && !strcmp(statename, omx_state_names[i])) {
            *state = omx_states[i];
            return 1;
        }
    }
    return 0;
}

/* 
Gets the current OMX state
Writes the current OMX state into 'curr_state'
RETURNS: 1 on successful GetState
         0 otherwise
*/
static int
get_omx_state(OMX_STATETYPE *curr_state)
{
    OMX_ERRORTYPE ret;
    if((ret = OMX_GetState(avc_dec_handle, curr_state)) != OMX_ErrorNone) {
        DPRINT(_HERE, 0, "\nERROR: OMX_GetState failed with retval=%d", ret);
        ERROR_ACTION(0, 112);
        return 0;
    }
    return 1;
}

/* Validate the current OMX state 
RETURNS : 1 if current state is as expected
        : 0 otherwise */
static int 
check_omx_state(OMX_STATETYPE expected_state) 
{ 
    OMX_STATETYPE curr_state; 
    char name[30], name2[30];

    if(get_omx_state(&curr_state) && curr_state != expected_state) {
        if(! map_omx_state_name(0, &curr_state, name))
            strcpy(name, "-ERROR-");
        if(! map_omx_state_name(0, &expected_state, name2))
            strcpy(name, "-ERROR-");
        DPRINT(_HERE, 0, "\nERROR: REACHED UNEXPECTED OMX_STATE[%d]='%s', Expected=[%d]'%s'", curr_state, name, expected_state, name2);
        return 0;
    }
    return 1;
}


/* Changes OMX state synchronously 
RETURNS: 0 on failure
       : 1 on success */
static int 
change_omx_state(OMX_STATETYPE newstate)
{
    char namecurr[30];
    char namenew[30];
    OMX_STATETYPE currstate;
    int waitForBuffs;
    app_state_t savedAppState;

    waitForBuffs = 0;
    ATOMIC_DO_GLOBAL(savedAppState = geAppState);

    if(! map_omx_state_name(0, &newstate, namenew)) {
        DPRINT(_HERE, 0, "ERROR: APP_EVENT_OMX_STATE_SET: Got Invalid OMX_State Val=%d", (int)newstate);
        return 0;
    }
    if(get_omx_state(&currstate) && map_omx_state_name(0, &currstate, namecurr)) {
        if((currstate == OMX_StateExecuting || currstate == OMX_StatePause) && 
           (newstate == OMX_StateIdle || newstate == OMX_StateInvalid)) {
            waitForBuffs = 1;
        }
        if(waitForBuffs)
            APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);

        DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder [%s->%s] / AppState=%d\n", namecurr, namenew, geAppState);
        clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, newstate,0);
        if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true)) {
            ERROR_ACTION(0, 61);
            return 0;
        }
        APP_CHANGE_STATE(savedAppState);
        CHECK_OMX_STATE(newstate); 
        DPRINT(_HERE, 6, "\nDONE: %s->%s / AppState=%d", namecurr, namenew, geAppState);
        if(waitForBuffs) {
            DPRINT(_HERE, 6, "\nAfter [%s->%s] : Checking if all buffers were returned...", namecurr, namenew);
            CHECK_BUFFERS_RETURN(200000ul, 99, !gbDontWaitForBuffs, 1, NULL);
        }
    } /* if(get_omx_state( ... */
    else {
        DPRINT(_HERE, 0, "\nERROR: Couldn't get current OMX state !");
        ERROR_ACTION(0, 112);
        return 0;
    }
    return 1;
}


static void 
stop_and_cleanup(int testpassed) 
{
    int bufCnt, i;
    int result = 0;

    DPRINT(_HERE, 8, "Entered with testpassed=%d", testpassed);
    //(void)gettimeofday(&gtDecEndTimeVal, NULL);
    LOCK_MUTEX(&gtGlobalVarMutex);
        (void)gettimeofday(&gtDeinitStartTimeVal, NULL);
    UNLOCK_MUTEX(&gtGlobalVarMutex);

    DPRINT(_HERE, 6, "\nmain(): END DECODE...EmptyBufferDoneCnt=%d, FillBufferDoneCnt=%d, inputEOS=%d, outputEOS=%d", ebd_cnt, fbd_cnt, bInputEosReached, bOutputEosReached);
    DPRINT(_HERE, 6, "\nmain() EOS/END: BEFORE any action...GLOBAL VAR DUMP...");
    dump_global_data();     
    DPRINT(_HERE, 6, "\nMoving the decoder to idle state\n");
    DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder [Exec->Idle]");
    APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);
    CHECK_OMX_STATE(OMX_StateIdle); DPRINT(_HERE, 6, "\nDONE: EXEC->IDLE!");
    DPRINT(_HERE, 6, "\nAfter [Exec->Idle] : Checking all buffers returned...");
    // Check if all port buffers have been returned via OMX_EmptyBufferDone
    CHECK_BUFFERS_RETURN(200000ul, 99, !gbDontWaitForBuffs, 1, NULL);

    DPRINT(_HERE, 6, "\nMoving the decoder to loaded state\n");
    DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder [Idle->Loaded]");
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);
    DPRINT(_HERE, 6, "\nDeallocating i/p and o/p buffers \n");
    for(bufCnt=0; bufCnt < gnTotalInputBufs; ++bufCnt) {
        OMX_FreeBuffer(avc_dec_handle, 0, pInputBufHdrs[bufCnt]);
    }

    for(bufCnt=0; bufCnt < gnTotalOutputBufs; ++bufCnt) {
        OMX_FreeBuffer(avc_dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
    }
    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);
    CHECK_OMX_STATE(OMX_StateLoaded); DPRINT(_HERE, 6, "\nDONE: IDLE->LOADED");

    if(gbDontFreeHandle == 2) {
        DPRINT(_HERE, 4, "\nWARNING: CALLING OMX_FreeHandle() with NULL - gbDontFreeHandle WAS SET to 2 !"); 
        result = OMX_FreeHandle(NULL);
    }
    else if(gbDontFreeHandle == 1) {
        DPRINT(_HERE, 4, "\nWARNING: NOT CALLING OMX_FreeHandle() - gbDontFreeHandle WAS SET !"); 
        result = OMX_ErrorNone;
    }
    else {
        DPRINT(_HERE, 6, "\nCalling FreeHandle...");
        result = OMX_FreeHandle(avc_dec_handle);
        fprintf(gtMyStdout,"\nFreeHandle done");
    }

    if (result != OMX_ErrorNone) {
        DPRINT(_HERE, 6, "\nOMX_FreeHandle error. Error code: %d\n", result);
        ERROR_ACTION(0, 107);
    }

    /* Deinit OpenMAX */
    DPRINT(_HERE, 6, "\nDe-initializing OMX \n");
    OMX_Deinit();
    DPRINT(_HERE, 6, "\nDONE: De-init OMX\n");

    (void)gettimeofday(&gtDeinitEndTimeVal, NULL);
    print_stats_summary();

    DPRINT(_HERE, 6, "\nmain() EOS/END: AFTER all actions...GLOBAL VAR DUMP...");
    dump_global_data();     

    DPRINT(_HERE, 0, "*************************************");
    DPRINT(_HERE, 0, "**********...%s...***********", testpassed == true ? "TEST PASSED" : "TEST FAILED");
    DPRINT(_HERE, 0, "*************************************");

    my_fcloseall();
    if(CODEC_FORMAT_H264 == codec_format_option) { 
        TEST_UTILS_H264_DeInit();
        DPRINT(_HERE, 6, "TEST_UTILS_H264_DeInit() Done... ");
    }
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&lock);
    DPRINT(_HERE, 8, "\nLeaving...");
}


/* Signal Handler function to catch SIGINT signal and perform cleanup actions */
static void 
sig_handler(int signum, siginfo_t *siginfo, void *ucontext)
{   
  /* TEMP (workaround to allow gdb debugging to work) TODO: Keep this commented
  DPRINT(_HERE, 0, "==== CAUGHT SIGNAL: %s[%d] (continuing) ====", (signum == SIGINT ? "SIGINT" : (signum == SIGSEGV ? "SIGSEGV" : "UNEXPECTED")), signum);
  return; */

  if(signum == SIGINT) 
  {
      struct timeval end;  /* gettimeofday for gtDecEndTimeVal needs to be thread-safe without mutex use */                         

      (void)gettimeofday(&end, NULL); 
      gtDecEndTimeVal = end; 
      DPRINT(_HERE, 0, "==== Caught SIGINT signal from user ====\n");
      DPRINT(_HERE, 0, "\n\nAppState=%d, Frame BUF Count- TO DSP:%d FROm DSP:%d\n\n", geAppState, ebd_cnt, fbd_cnt); 

      if(geAppState >= APP_STATE_Meditating && geAppState <= APP_STATE_CollectOmxOutBuffers) {
          DPRINT(_HERE, 6, "Posting EVENT_END to app...");
          appQ_post_event(APP_EVENT_END, 0, 0, false);    // Experimental 
          return;                                         // Experimental
      }
      DPRINT(_HERE, 6, "Terminating without cleanup...");
      dump_global_data();
      //terminate(true);    //03/23/09 : Experimental 
      print_stats_summary();
      my_fcloseall();
#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
      QPERF_END(client_decode);
      QPERF_SET_ITERATION(client_decode, fbd_cnt);
    #ifdef TARGET_ARCH_8K
      QPERF_TERMINATE(client_decode);
      if(fb_display) {
          QPERF_TERMINATE(render_fb);
      }
    #else
      QPERF_SHOW_STATISTIC(client_decode);
      if(fb_display) {
        QPERF_SHOW_STATISTIC(render_fb);
      }
    #endif
#endif
      exit(1);
  }
  else if(signum == SIGSEGV) {
          DPRINT(_HERE, 0, "==== ERROR FATAL: Caught SIGSEGV signal ===\n");
          DPRINT(_HERE, 0, "Terminating without cleanup...");
          dump_global_data();
          terminate(true);
          my_fcloseall();
          DPRINT(_NOHERE, 0, "Segmentation Fault [ SIGSEGV: Addr=0x%x, si_signo=%d, si_errno=%d, si_code=%d ]\n", siginfo->si_addr, siginfo->si_signo, siginfo->si_errno, siginfo->si_code);
          exit(1);
      }
}


/* Function to install signal handler for SIGINT */
static void 
install_sighandler(void)
{
  //sigact.sa_handler = (__sighandler_t)sig_handler;    // 05/11/09
  sigact.sa_sigaction = sig_handler;    
  memset(&(sigact.sa_mask), 0, sizeof(sigact.sa_mask));
  sigact.sa_flags = SA_SIGINFO;
  sigact.sa_restorer = NULL;
  sigaction(SIGINT, &sigact,NULL);
  sigaction(SIGSEGV, &sigact, NULL);                // 05/11/09
}

/* Wait for any OMX event OR wait for a particular OMX event and/or CmdComplete notification to occur (can also use for other kinda internal 
events by typecasting to OMX event type & using negative numbers for event type etc since OMX event types shud all be +ve)
   If whichEvent== WAIT_OMX_EVT_ANY then wait for any event else its value specifies an OMX event type to wait for
   If whichCmdComplete== WAIT_OMX_EVT_ANY then wait for any CmdComplete event else its value is an OMX CmdComplete to wait for
   whichCmdComplete is ignored for all values of whichEvent other than whichEvent==OMX_EventCmdComplete
   IF wakeOnError==1 then return as soon as ANY error event is received 
   IF pGotEvent is not NULL then it will be filled with the event data
   USAGE: 
   Call flow usually is either of the two: 
   1> reset_all_event_data() --> [action that raises event] --> expect_some_event()
   2> reset_all_event_data() --> [action that raises event(s)] --> expect_some_event() --> expect_some_event() --> ....
   If the action is expected to raise multiple events and more than one of those events needs to be handled then flow <2> is used
        The action that can raise an event and the call to wait for that event are not done together atomically. Hence 
        the action might raise an OMX event even before the wait for that event is called & hence the event will be missed / 
        infinite wait can happen etc. Also, an action may raise multiple events & we may want to wait for one or more of those. 
        Hence by default the data for the latest few events will be stored & retrieved when the wait function is called. 
        And hence when we want to avoid old event data from being returned, we first clear all stored event data & then call wait 
*/
static void 
expect_some_event(int whichEvent, int whichCmdComplete, int wakeOnError, wait_omx_event_info_t *pGotEvent)
{
    DPRINT(_HERE, 8, "\nENTERED expect_some_event(): whichEvt=%d, whichCmd=%d, wakeErr=%d, Q_Head=%d, Q_Tail=%d", whichEvent, whichCmdComplete, wakeOnError, gnGotOmxEvtQ_Head, gnGotOmxEvtQ_Tail);
    LOCK_MUTEX(&gtWaitOmxEventMutex);
    gtWaitOmxEventData.eEvent = whichEvent;
    gtWaitOmxEventData.nData1 = whichCmdComplete;
    gtWaitOmxEventData.nData2 = (wakeOnError) ? WAIT_OMX_EVT_ANYERR : 0;
    OMX_EVENTTYPE eEvent; int nData1; int nData2; void *pEventData;
    wait_omx_event_info_t gotEvent;

    /* Wait while event que is empty or the event got isn't the expected one */
    while (1) {                   
        if(gnGotOmxEvtQ_Head == gnGotOmxEvtQ_Tail)      // Que empty
            pthread_cond_wait(&gtWaitOmxEventCond, &gtWaitOmxEventMutex);

        /* If control reaches here then some event present in que so get it & examine it */
        gotEvent = gtGotOmxEventDataQ[gnGotOmxEvtQ_Tail];
        eEvent = (OMX_EVENTTYPE)gotEvent.eEvent;
        nData1 = gotEvent.nData1;
        nData2 = gotEvent.nData2;
        pEventData = gotEvent.pEventData;
        gnGotOmxEvtQ_Tail = (gnGotOmxEvtQ_Tail + 1) % CIRQ_SIZE_WaitOmxEvent;    /* Delete event from que */

        if ((WAIT_OMX_EVT_ANYERR == gtWaitOmxEventData.nData2 && OMX_EventError == eEvent)     /* Wake on any error event also if that was specified */
            || (WAIT_OMX_EVT_ANY == gtWaitOmxEventData.eEvent)                                 /* Wake on any event if that's what was asked for */
            || gtWaitOmxEventData.eEvent == eEvent && (WAIT_OMX_EVT_ANY == gtWaitOmxEventData.nData1 || nData1 == gtWaitOmxEventData.nData1)) 
        {
            break;  /* Found expected event */
        }
        DPRINT(_HERE, 8, "\nINFO: UNEXPECTED gotEvent=%d, gotCmd=%d, nData2=%d, Q_Head=%d, Q_Tail=%d...condWait'ing again...\n", (int)eEvent, nData1, nData2, gnGotOmxEvtQ_Head, gnGotOmxEvtQ_Tail);
    }   /* while(1.... */ 

    /* If control reaches here then found expected event */
    if(pGotEvent)
        *pGotEvent = gotEvent;   

    gtWaitOmxEventData = WAIT_OMX_EVENT_DATA_INIT;              /* Found expected event so reset this */
    UNLOCK_MUTEX(&gtWaitOmxEventMutex);
    DPRINT(_HERE, 8, "\nLEAVING expect_some_event(): gotEvt=%d, gotCmd=%d, nData2=%d, Q_Head=%d, Q_Tail=%d", (int)eEvent, nData1, nData2, gnGotOmxEvtQ_Head, gnGotOmxEvtQ_Tail);
}

static void 
received_some_event(OMX_EVENTTYPE eEvent, int nData1, int nData2, void *pEventData)
{
    DPRINT(_HERE, 8, "\nENTERED received_some_event(): Event=%d, nData1=%d, nData2=%d, Q_Head=%d, Q_Tail=%d", (int)eEvent, nData1, nData2, gnGotOmxEvtQ_Head, gnGotOmxEvtQ_Tail);

    LOCK_MUTEX(&gtWaitOmxEventMutex);

    if((gnGotOmxEvtQ_Head + 1) % CIRQ_SIZE_WaitOmxEvent == gnGotOmxEvtQ_Tail) { // Que Full
        DPRINT(_HERE, 6, "\nINFO: gtGotOmxEventDataQ QUE Full, Wrapping around...");
        gnGotOmxEvtQ_Tail = (gnGotOmxEvtQ_Tail + 1) % CIRQ_SIZE_WaitOmxEvent;   // Delete oldest element to make space
    }

    gtGotOmxEventDataQ[gnGotOmxEvtQ_Head].eEvent = (int)eEvent;
    gtGotOmxEventDataQ[gnGotOmxEvtQ_Head].nData1 = nData1;
    gtGotOmxEventDataQ[gnGotOmxEvtQ_Head].nData2 = nData2;
    gtGotOmxEventDataQ[gnGotOmxEvtQ_Head].pEventData = pEventData;
    gnGotOmxEvtQ_Head = (gnGotOmxEvtQ_Head + 1) % CIRQ_SIZE_WaitOmxEvent;    

    pthread_cond_broadcast(&gtWaitOmxEventCond);
    UNLOCK_MUTEX(&gtWaitOmxEventMutex);
    DPRINT(_HERE, 8, "\nLEAVING received_some_event(): Event=%d, nData1=%d, nData2=%d, Q_Head=%d, Q_Tail=%d\n", (int)eEvent, nData1, nData2, gnGotOmxEvtQ_Head, gnGotOmxEvtQ_Tail);
}

/* Mostly for debugging, shudnt need it (uncomment the pthread_cond_broadcast() in received_some_event() 
   Was needed only to change thread scheduling order slightly: ie. it was observed at runtime that the moment a cond_broadcast is given,
   the woken thread begins running. Hence In the middle of received_some_event() the expect_an_event() call finishes and that thread's code
   starts running, thus starving the received_some_event() thread for a while & hence received_some_event() completes much later */
static int
broadcast_pending_events(void)
{
    int retval = false;
    LOCK_MUTEX(&gtWaitOmxEventMutex);
    if(gnGotOmxEvtQ_Head != gnGotOmxEvtQ_Tail) {
        pthread_cond_broadcast(&gtWaitOmxEventCond);
        retval = true;
    }
    UNLOCK_MUTEX(&gtWaitOmxEventMutex);
    return retval;
}

// Check if there's any event in the 'expect_some_event' que
static int 
is_event_pending(void)
{
    int retval;
    LOCK_MUTEX(&gtWaitOmxEventMutex);
    if(gnGotOmxEvtQ_Head == gnGotOmxEvtQ_Tail)
        retval = false;
    else
        retval = true;
    UNLOCK_MUTEX(&gtWaitOmxEventMutex);
    return retval;
}

static void
reset_all_event_data(void) 
{
    LOCK_MUTEX(&gtWaitOmxEventMutex);
    gnGotOmxEvtQ_Head = gnGotOmxEvtQ_Tail = 0;        // Mark que as empty
    gtWaitOmxEventData = WAIT_OMX_EVENT_DATA_INIT;    // Reset waiting-for-event data
    UNLOCK_MUTEX(&gtWaitOmxEventMutex);
}

/* Get app event.
RETURNS ptr to app event data. 
        If app que empty and blocking==1 then BLOCK until event arives else return NULL 
*/
static appQ_event_data_t *
appQ_get_event(int blocking)
{
    appQ_event_data_t *retval;

    DPRINT(_HERE, 8, "Entered [appQIndex=%d] with blocking=%d", gnAppQIndex, blocking);
    LOCK_MUTEX(&gtAppQMutex);
    while(blocking && gnAppQIndex == -1)    // Que empty
        pthread_cond_wait(&gtAppQCond, &gtAppQMutex);

    if(gnAppQIndex >= 0) 
        retval = &gtAppQ[gnAppQIndex--];
    else
        retval = NULL;
    pthread_cond_broadcast(&gtAppQCond);    // TEMP2 CHECK
    UNLOCK_MUTEX(&gtAppQMutex);
    DPRINT(_HERE, 8, "Returning with [appQIndex=%d] [retval->event=%d]", gnAppQIndex, retval ? retval->event : -1);
    return retval;
}

/* Post app event. 
blocking == 0 : (non-blocking) If que full then return immediately with error (-1)
blocking == 1 : (blocking) Block till que has space
RETURNS: 0 on success & -1 on error (or on que full in non-block mode)
*/
static int 
appQ_post_event(app_event_t event, int data, const void *pMoreData, int blocking)
{
    int retval = -1;

    DPRINT(_HERE, 8, "Entered [appQIndex=%d] with event=%d, data=%d, pMoreData=0x%x, blocking=%d", gnAppQIndex, event, data, pMoreData, blocking);
    LOCK_MUTEX(&gtAppQMutex);
    if(gnAppQIndex < Q_SIZE_App-1) {
        gtAppQ[++gnAppQIndex].event = event;
        gtAppQ[gnAppQIndex].data = data;
        //gtAppQ[gnAppQIndex].pMoreData = (void*)pMoreData;     03/25/09
        if(pMoreData)
            gtAppQ[gnAppQIndex].omx_ebd_params = *(appQ_evtdata_OMX_EBD_t *)pMoreData;
        pthread_cond_broadcast(&gtAppQCond);
        retval = 0;
    }
    else if(blocking) {         // Que full  
        DPRINT(_HERE, 3, "\nWARNING: AppQ FULL ! (Q_SIZE=%d, appQIndex=%d) ... Blocking...", Q_SIZE_App, gnAppQIndex);
        while(blocking && gnAppQIndex >= Q_SIZE_App-1)    // Que full so block until there's space
            pthread_cond_wait(&gtAppQCond, &gtAppQMutex);
        DPRINT(_HERE, 6, "\nDone blocking(AppQ_Full)...continuing");
        retval = 0;
    }
    else {
        retval = -1;    
        DPRINT(_HERE, 0, "\nappQ_post_event(): ERROR: AppQ FULL (Q_SIZE=%d, appQIndex=%d) ! Returning...", Q_SIZE_App, gnAppQIndex); 
        ERROR_ACTION(0, 210);
    }

    UNLOCK_MUTEX(&gtAppQMutex);
    DPRINT(_HERE, 8, "Returning with [appQIndex=%d] [retval=%d]", gnAppQIndex, retval);
    return retval;
}

/* Return first free buffer or NULL if nothing available */
static OMX_BUFFERHEADERTYPE *
get_free_omx_inbuffer(OMX_BUFFERHEADERTYPE **bufArray, int numelem)
{
    int i;
    int bufstatus;

    for(i=0; i<numelem; i++) {
        //if(bufArray[i]->pBuffer && bufArray[i]->nAllocLen && 0 == bufArray[i]->nFilledLen) {
        if(bufArray[i]->pBuffer && bufArray[i]->nAllocLen && (bufstatus = get_buffer_status(INBUF, i)) < 0) {
            return bufArray[i];
        }
        else if(bufstatus == 0) {
            DPRINT(_HERE, 0, "ERROR: Buffer Status returned as 0 (i=%d)", i);
            ERROR_ACTION(0, 222);
        }
        else {
            //DPRINT(_HERE, 6, "Buffer status for Buffer#%d = %d", i, bufstatus);
        }
    }
    return NULL;
}

/* Mark 'numelem' buffers as empty starting from bufArray */
static void mark_omx_buffers_empty(OMX_BUFFERHEADERTYPE **bufArray, int numelem)
{
    int i;

    if(! bufArray)
        return;
    for(i=0; i<numelem; i++)
        if(bufArray[i] && bufArray[i]->pBuffer && bufArray[i]->nAllocLen) {
            bufArray[i]->nFilledLen = bufArray[i]->nFlags = bufArray[i]->nOffset = 0;
        }
}

/* Get the NAL Unit Type (ie. video frame type) from the NAL bytestream/packet (need I-frames for random seek hence this function) 
   how == 1 : Don't look for startcode (assume 1st byte of packet to have NAL unit type)
   how == 0 : Look for startcode & take the next byte after startcode to have NAL unit type (to read .dat format)
   Note: Won't work if start code is in last 3 bytes of packet but we dont expect that to occur
         Also, using strings instead of bit-manipulation seems better 'cos bit-manipulation will have to make assumptions about integer size etc
RETURNS : Nal unit type (I-frame is type 5) 
          -1 if no startcode prefix found (ie. no NAL unit found)
          -2 if forbidden_zero_bit is wrong (ie. bad NAL unit) */
static int 
h264_get_NAL_unit_type(int file_type, const void *packet, unsigned int size)
{
    unsigned char isStartPrefix[4+1], *ptemp;
    unsigned int i, how;
    int type = -1;

    isStartPrefix[4] = '\0';
    // First 'nalSize' bytes contain the packet size so skip those
    ptemp = (unsigned char*)packet + nalSize;
    size -= nalSize;        

    if (FILE_TYPE_ARBITRARY_BYTES == file_type || FILE_TYPE_DAT_PER_AU == file_type)
        how = 0;
    else 
        how = 1;

    if(1 == how && (*ptemp & 0x80) != 0) {      // forbidden_zero_bit is not zero (indicates error in NAL unit)
        DPRINT(_HERE, 4, "\nWARNING: INVALID NAL UNIT : [Byte1]=0x%x doesn't have Forbidden_zero_bit as 0...", (unsigned int)*ptemp);
        ERROR_ACTION(4, 203);
        return -2;
    }
    else if(1 == how) {
        type = *ptemp & 0x1f;
        DPRINT(_HERE, 8, "\nFound NAL unit#%d, Type=%d, IFrameCnt=%d, Size=%d, Start byte=0x%x ...", gnNALUnitCnt, type, gnIFrameCount, size, (unsigned int)*ptemp);
        return type;    // Lower unsigned 5 bits contain the nal_unit_type
    }

    while(ptemp+4 < (unsigned char*)packet+size) {     // Dont like seg faults
        for(i=0; i<4; i++)
            isStartPrefix[i] = ptemp[i] + '0';      // Convert each byte value into an ASCII value so that comparison is easier
        // Spec says start prefix is a 3-byte value of 0x1 possibly preceeded by a zero byte (ie. start prefix = 001 or 0001)
        if(!memcmp(isStartPrefix, "001", 3) || !memcmp(isStartPrefix, "0001", 4)) {        
            // Found start prefix so point ptemp to 1st byte of NAL unit
            ptemp = ptemp + 4 - (isStartPrefix[2]=='1' ? 1 : 0);  // If start prefix was 001 then increment by 3 instead of 4

            type = *ptemp & 0x1f;
            DPRINT(_HERE, 8, "\nFound NAL unit#%d, Type=%d, IFrameCnt=%d, Size=%d, Start byte=0x%x ...", gnNALUnitCnt, type, gnIFrameCount, size, (unsigned int)*ptemp);
            if((*ptemp & 0x80) != 0) {      // forbidden_zero_bit is not zero (indicates error in NAL unit)
                DPRINT(_HERE, 4, "\nWARNING: INVALID NAL UNIT : [Byte1]=0x%x doesn't have Forbidden_zero_bit as 0...", (unsigned int)*ptemp);
                ERROR_ACTION(4, 203);
                return -2;
            }
            else 
                return type;    // Lower unsigned 5 bits contain the nal_unit_type
        }
        ptemp++;
    }
    return -1;  
}

/* Get the VOP Type & determine if it's Iframe from the bytestream/packet 
   how == 1 : Assume 1st 4 bytes of packet to have startcode & the NEXT byte to have VOP type
   how == 0 : Search for startcode & the next byte will have VOP type (NOT SUPPORTED yet)
RETURNS : VOP type (I-frame = 0x0) 
          -1 on error
*/
static int 
mpeg4_get_VOP_coding_type(int file_type, const void *packet, unsigned int size)
{
    unsigned char isStartPrefix[4+1], *ptemp;
    unsigned int i, how;

    isStartPrefix[4] = '\0';
    ptemp = (unsigned char*)packet;

    if (FILE_TYPE_ARBITRARY_BYTES == file_type || FILE_TYPE_DAT_PER_AU == file_type)
        how = 0;
    else 
        how = 1; 

    if(how == 0) {
        DPRINT(_HERE, 0, "\nAPP ERROR: VOP STARTCODE SEARCH (arbitrary bytes) not supported !");
        ERROR_ACTION(0, 214);       
        return -1;
    }

    if(size > 4) {
        DPRINT(_HERE, 8, "\nFound Mpeg4 unit, Type=%d, IFrameCnt=%d, Size=%u...", (*(ptemp+4) & 0xC0), gnIFrameCount, size);
        return (*(ptemp+4) & 0xC0);    // Upper 2 bits of the 1st byte after the startcode contain the vop_coding_type
    }
    else
        return -1;  
}


/* Get frame type for VC1 (currently not implemented)
RETURNS: 1 : for IDR
         0 : other frame
        -1 : Error 
*/
static int
vc1_get_frame_type(int file_type, const void *packet, unsigned int size)
{
    if(FILE_TYPE_VC1 == file_type && APP_STATE_Sprinting == geAppState) {
        DPRINT(_HERE, 5, "\nWARNING: Unsupported IDR search in VC1 seek [IFrameCnt=%d, Size=%u]... ", gnIFrameCount, size);
        return 1;
    }
    return -1;
}

/* Read into 'bBuffHeader' until Next I-frame is found & return that pointer OR NULL (if EndOfStream encountered before any IFrame) */
static OMX_BUFFERHEADERTYPE *
get_next_iframe(OMX_BUFFERHEADERTYPE *pBuffHeader)
{
    while(pBuffHeader) {
        pBuffHeader->nFilledLen = pBuffHeader->nFlags = pBuffHeader->nOffset = 0;
        if(Read_Buffer(pBuffHeader) <= 0) {
            return NULL;
        }

        /* If gbFfwd_ErrorSeekMode=true then try to simulate streaming error condition (ie. return some regular frame instead of Iframe) */
        if(gbFfwd_ErrorSeekMode) {
            DPRINT(_HERE, 9, "[Seek with ErrorSeekMode=ON]... returning this buffer regardless of frame type ");
            return pBuffHeader;
        }
        else if(FILE_TYPE_ARBITRARY_BYTES == file_type_option) {
            DPRINT(_HERE, 9, "[Seek in Arbitrary_Bytes_Read]... returning this buffer ");
            return pBuffHeader;
        }

        /* For H264 or RCV input frames, Read_Buffer() will set the flag for I-frames */

        switch(codec_format_option) {
        case CODEC_FORMAT_VC1:
            if(pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
                DPRINT(_HERE, 8, "(VC1)sync frame: [IFrameCnt=%d / NAL UnitCnt=%d] Hdr->nFlags=%d [IsSync=%d] [codec_format_option=%d, file_type_option=%d]", gnIFrameCount, 
                   gnNALUnitCnt, pBuffHeader->nFlags, (pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME), codec_format_option, file_type_option);
                return pBuffHeader;
            }
            else if ((FILE_TYPE_RCV == file_type_option ) && rcv_v1)
            {
               DPRINT(_HERE, 8, "\n\nseek not supported for rcv v1 format files\n\n");
            }
        break;

        case CODEC_FORMAT_H264:
            if(pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
                DPRINT(_HERE, 8, "(H264)sync frame: [IFrameCnt=%d / NAL UnitCnt=%d] Hdr->nFlags=%d [IsSync=%d] [codec_format_option=%d, file_type_option=%d]", gnIFrameCount, 
                   gnNALUnitCnt, pBuffHeader->nFlags, (pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME), codec_format_option, file_type_option);
                return pBuffHeader;
            }
        break;

        case CODEC_FORMAT_VP:
            if(pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
                DPRINT(_HERE, 8, "(VP)sync frame: [IFrameCnt=%d / NAL UnitCnt=%d] Hdr->nFlags=%d [IsSync=%d] [codec_format_option=%d, file_type_option=%d]", gnIFrameCount, 
                   gnNALUnitCnt, pBuffHeader->nFlags, (pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME), codec_format_option, file_type_option);
                return pBuffHeader;
            }
        break;


        default:
            if(get_frame_type(file_type_option, pBuffHeader->pBuffer + pBuffHeader->nOffset, pBuffHeader->nFilledLen - pBuffHeader->nOffset) == gnIFrameType) {
                DPRINT(_HERE, 8, "sync frame: [IFrameCnt=%d / NAL UnitCnt=%d] Hdr->nFlags=%d [IsSync=%d] [codec_format_option=%d, file_type_option=%d]", gnIFrameCount, 
                   gnNALUnitCnt, pBuffHeader->nFlags, (pBuffHeader->nFlags & OMX_BUFFERFLAG_SYNCFRAME), codec_format_option, file_type_option);
                return pBuffHeader;
            }
            break;
        }   /* switch(codec_format... */
    }   /* while(pBuffer... */
    //DPRINT(_HERE, 8, "Returning NULL");
    return NULL;
}

/* Fill a bunch of OMX output buffers 
   howMany : Nr. of output buffers 
   outputPortNum : Output port number 
   how : If how == 1 then only fill buffers that are currently unused. For any othe value of how, fill all buffers regardless
   RETURNS: count of buffers successfully submitted to be filled */
static int 
fill_buffers(int howMany, int outputPortNum, int how)
{
    int bufCnt, count = 0, ret;

    DPRINT(_HERE, 8, "\nEntered with howMany=%d, outputPortNum=%d, how=%d", howMany, outputPortNum, how);
    for(bufCnt=0; bufCnt < howMany; ++bufCnt) {
        DPRINT(_HERE, 8, "\nOMX_FillThisBuffer on output buf no.%d\n",bufCnt);
        LOCK_MUTEX(&gtGlobalVarMutex);
        // 04/06/09  if(how == 1 && pOutYUVBufHdrs[bufCnt]->nFilledLen > 0) {
        if(how == 1 && get_buffer_status(OUTBUF, bufCnt) > 0) {
            UNLOCK_MUTEX(&gtGlobalVarMutex);
            DPRINT(_HERE, 6, "INFO: Non-empty/USED buffer: NOT FILLING Buffer#%d, BuffHdr=0x%x, BuffPtr=0x%x\n",
                   bufCnt, pOutYUVBufHdrs[bufCnt], pOutYUVBufHdrs[bufCnt]->pBuffer);
            continue;
        }
        pOutYUVBufHdrs[bufCnt]->nOutputPortIndex = outputPortNum;
        pOutYUVBufHdrs[bufCnt]->nOffset = 0;
        pOutYUVBufHdrs[bufCnt]->nFlags = 0; /* &= ~OMX_BUFFERFLAG_EOS; */ //03/18/09
        update_used_buffer_list(OUTBUF, USED, pOutYUVBufHdrs[bufCnt]);     // Mark buffer as used
        UNLOCK_MUTEX(&gtGlobalVarMutex);
        ret = OMX_FillThisBuffer(avc_dec_handle, pOutYUVBufHdrs[bufCnt]);
        if (OMX_ErrorNone != ret) {
            ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pOutYUVBufHdrs[bufCnt]));
            DPRINT(_HERE, 0, "\nERROR: FILL BUFFER#%d FAILED with retval=%d, BuffPtr=0x%x, nFilledLen=%d, nFlags=0x%x, nTickCount=%d, nTimeStamp=%lu\n", 
                   ret, pOutYUVBufHdrs[bufCnt]->pBuffer, pOutYUVBufHdrs[bufCnt]->nFilledLen, pOutYUVBufHdrs[bufCnt]->nFlags,
                   (unsigned long)pOutYUVBufHdrs[bufCnt]->nTickCount, (unsigned long)pOutYUVBufHdrs[bufCnt]->nTimeStamp);
            ERROR_ACTION(1, 105);
        } 
        else {
            // After successful FTB, buffer is with decoder hence do not access any buffer data except header & buffptr
            DPRINT(_HERE, 8, "\nINFO: OMX_FillThisBuffer success : DONE FILL BUFFER#%d, BuffHdr=0x%x, BuffPtr=0x%x\n", 
                   count, pOutYUVBufHdrs[bufCnt], pOutYUVBufHdrs[bufCnt]->pBuffer);
            count++;
        }
    }
    DPRINT(_HERE, 8, "\nReturning with FilledCount=%d\n", count);
    return count;
}


/* Take appropriate action on input EOS
if inSyncSearch == 1 : Take actions for EOS while seeking I-frame
   inSyncSearch == 0 : Take actions for regular EOS case 
*/
static int 
process_inputread_eos(OMX_BUFFERHEADERTYPE *pLastInputBuff, int inSyncSearch)
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;
    int readBytes;

    DPRINT(_HERE, 8, "Entered with pLastInputBuff=0x%x, inSyncSearch=%d", pLastInputBuff, inSyncSearch);
    if(ferror(inputBufferFile)) {
        DPRINT(_HERE, 0, "\n[ebd_cnt=%d]: ERROR: Input file read possibly had issues...\n", ebd_cnt);
        ERROR_ACTION(4, 201); //03/20/09
    }

    /* 04/20/09 : Fix the case when there's input EOS & the output EOS cannot be raised because there are currently no output buffers with 
    the decoder */
    if(0 == dump_omx_buffers(1, USEDOUTBUF)) {
        DPRINT(_HERE, 5, "\n[ebd_cnt=%d] WARNING: EOS_INPUT: No output buffer to handle fake-frame-done hence FILLING one out-buffer...", ebd_cnt); 
        fill_buffers(1, 1, 1);      
    }
    DPRINT(_HERE, 8, "DEBUG: Step 1");
    LOCK_MUTEX(&gtGlobalVarMutex);
        /* Just in case EOS_Input is found concurrently in app main thread as well as in EBD, return the buffer */
        if(true == bInputEosReached) {
            pLastInputBuff->nFilledLen = pLastInputBuff->nOffset = pLastInputBuff->nFlags = 0;
            update_used_buffer_list(INBUF, FREE, pLastInputBuff);
            DPRINT(_HERE, 6, "[ebd_cnt=%d] : Already processed EOS_INPUT elsewhere...RETURNING BUFFER [BuffHdr=0x%x, BuffPtr=0x%x]...\n", ebd_cnt, 
                   pLastInputBuff, pLastInputBuff->pBuffer);
            UNLOCK_MUTEX(&gtGlobalVarMutex);
            return OMX_ErrorNone;
        } 

        DPRINT(_HERE, 8, "DEBUG: Step 2");

        DPRINT(_HERE, 6, "\n**********************************************");
        DPRINT(_HERE, 6, "[ebd_cnt=%d]: End of stream found on input port\n", ebd_cnt);
        DPRINT(_HERE, 6, "\n**********************************************");

        if(true == gbLoopedPlay && true == gbNoEosInLoopedPlay /* 04/23/09 && false == inSyncSearch*/) {  
#ifdef USE_INPUT_FILE_MUTEX
            LOCK_MUTEX(&gtInputFileMutex);
#endif
                fseek(inputBufferFile, 0L, SEEK_SET);
                bInputEosReached = bOutputEosReached = gbGotBufferFlagEvent = false;    
                gnLoopCount++;
#ifdef USE_INPUT_FILE_MUTEX
            UNLOCK_MUTEX(&gtInputFileMutex);
#endif
            /* If input EOS found during I-frame seek when looping-without-EOS-flag is on, then nothing more to be done, just trigger FFWD 
            sequence again & return buffer */
            if(true == inSyncSearch) {
                pLastInputBuff->nFilledLen = pLastInputBuff->nOffset = pLastInputBuff->nFlags = 0;
                update_used_buffer_list(INBUF, FREE, pLastInputBuff);
                UNLOCK_MUTEX(&gtGlobalVarMutex);
                DPRINT(_HERE, 6, "[ebd_cnt=%d / FlushCnt=%d / LoopCnt=%d]: LOOPING ON (EOS during I-frame search): Throttling main thread for FFWD & RETURNING BUFFER [BuffHdr=0x%x, BuffPtr=0x%x]...\n", 
                       ebd_cnt, gnFlushCount, gnLoopCount, pLastInputBuff, pLastInputBuff->pBuffer);
                appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, LASIC_CMD_FFWD, 0, 1);
                return OMX_ErrorNone;
            }
            /* Looped play without EOS so just reset to start of file & send first frame for decode */
            DPRINT(_HERE, 6, "[ebd_cnt=%d] : LOOPING ON (WITHOUT EOS wait)...File reset & read 1st frame [BuffHdr=0x%x, BuffPtr=0x%x]...\n", ebd_cnt, 
                   pLastInputBuff, pLastInputBuff->pBuffer);


            if(Read_Buffer(pLastInputBuff) <= 0) {
                DPRINT(_HERE, 0, "[ebd_cnt=%d]: ERROR: (LOOPING without EOS): 1st input frame read FAILED after file reset....", ebd_cnt);
                pLastInputBuff->nFilledLen = pLastInputBuff->nOffset = pLastInputBuff->nFlags = 0;
                update_used_buffer_list(INBUF, FREE, pLastInputBuff);
                appQ_post_event(APP_EVENT_END, 0, 0, true);     // Wake up main thread. This call blocks to avoid main thread starvation
                UNLOCK_MUTEX(&gtGlobalVarMutex);
                return OMX_ErrorNone;
            }
            else
                DPRINT(_HERE, 6, "[ebd_cnt=%d]: LOOPING without EOS [LoopCnt=%d]: Emptying 1st frame...", ebd_cnt, gnLoopCount);
            /* 04/22/09 
            DPRINT(_HERE, 6, "[ebd_cnt=%d] : LOOPING ON (WITHOUT EOS wait)...RETURNING BUFFER [BuffHdr=0x%x, BuffPtr=0x%x]...\n", ebd_cnt, 
                   pLastInputBuff, pLastInputBuff->pBuffer);
            pLastInputBuff->nFilledLen = pLastInputBuff->nOffset = pLastInputBuff->nFlags = 0;
            bInputEosReached = true; update_used_buffer_list(INBUF, FREE, pLastInputBuff);
            UNLOCK_MUTEX(&gtGlobalVarMutex);
            appQ_post_event(APP_EVENT_CONTINUE_LOOP, 0, 0, true);     // Wake up main thread. This call blocks to avoid main thread starvation
            return OMX_ErrorNone; */
        } /* if(true == gbLoopedPlay... */
        else if(false == gbNoEosInLoopedPlay /* 04/23/09 || true == inSyncSearch */) {
            pLastInputBuff->nFlags |= OMX_BUFFERFLAG_EOS;    // Set EOS flag
            pLastInputBuff->nInputPortIndex = 0;   
            bInputEosReached = true;
            (void)gettimeofday(&gtDecEndTimeVal, NULL);

            DPRINT(_HERE, 8, "DEBUG: Step 3");
        } 
    UNLOCK_MUTEX(&gtGlobalVarMutex);

    if(OMX_ErrorNone != (ret = EMPTY_BUFFER(avc_dec_handle, pLastInputBuff))) {
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pLastInputBuff));     // Mark buffer as free
        DPRINT(_HERE, 0, "[ebd_cnt=%d]: EOS_INPUT: ERROR: Call to EMPTY_BUFFER() for EOS FAILED !", ebd_cnt);
        ERROR_ACTION(1, 102);
    }
    /* If this isn't a fake EOS frame (zero len input buffer with EOS flag set) then increment input unit cnt */
    else if(pLastInputBuff->nFilledLen > 0) {
        ATOMIC_DO_GLOBAL(++gnInputUnitCnt);
    }

    DPRINT(_HERE, 8, "DEBUG: Step 4");

    if(false == gbNoEosInLoopedPlay || true == inSyncSearch) 
        appQ_post_event(APP_EVENT_EOS_INPUT, 0, 0, true);     // Wake up main thread. This call blocks to avoid main thread starvation
    return ret;
}


/* Find empty OMX buffers, fill them with data read from input file & call OMX empty-buffer
    howMany : nr. of buffers to read & empty
    inputPortNum : Input port number
    nrAttempts : if set to more than 1 and NO unused buffers found then usleep for time specified by 'sleep_usecs' & retry 'nrRetries' times to
                         get atleast 1 buffer 
    RETURNS : Count of buffers successfully read & emptied OR 
              -(Count) in case END_OF_INPUT_STREAM found. 
              If Count==0 is returned then caller should explicitly check for end of stream / feof(inputfile) / ferror(inputfile) */
static int 
read_and_empty_buffers(int howMany, int inputPortNum, int nrAttempts, unsigned long sleep_usecs)
{
    int i, j, cnt, frameSize, ret, locked;
    OMX_BUFFERHEADERTYPE *pBuff = NULL;

    DPRINT(_HERE, 6, "Entered with howMany=%d, inputPort=%d, nrAttempts=%d, usleep_secs=%lu", howMany, inputPortNum, nrAttempts, sleep_usecs);
    cnt = 0; locked = false;
    // Attempt to read and empty the specified nr. of buffers
    for (i = 0; i < howMany; i++) {
        DPRINT(_HERE, 8, "\nTrying GetFreeBuffer=%d, Total needed=%d, nrAttempts=%d...", i+1, howMany, nrAttempts);
        // Try to find free buffer atleast once
        for(j = 0; j < nrAttempts; j++) {    
            if(false == locked) { 
                locked = true; LOCK_MUTEX(&gtGlobalVarMutex);
            }
            if(pBuff = get_free_omx_inbuffer(pInputBufHdrs, gnTotalInputBufs)) {
                update_used_buffer_list(INBUF, USED, pBuff);     // Mark buffer as used
                break;
            }
            else if(0 == i && nrAttempts > 1) {
                    locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);
                    // Didn't find even one free buffer so sleep since sleeping is enabled (nrAttempts is > 1)
                    DPRINT(_HERE, 8, "\nBuffer:%d , Attempt:%d ... NO FREE BUFFER...Sleeping for: %d (usecs)...", i+1, j+1, sleep_usecs);
                    usleep(sleep_usecs);
            }

        }   /* for(j=0... */
        /* Make sure mutex is unlocked (in case the above loop exited with the mutex being locked */
        if(true == locked) {
            locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);
        }
        if(nrAttempts == j && 0 == i) {       // This means no free buffer was found at all, hence error out
            DPRINT(_HERE, 0, "\nWARNING : COULD NOT FIND ANY FREE INPUT BUFFER...");
            ERROR_ACTION(4, 211);
            return 0;
        }
        else if(nrAttempts == j) {     // This means no more free buffers found this time but atleast one was found earlier and used
            DPRINT(_HERE, 6, "\nReturning with Nr.EmptiedOmxBuffers=%d", cnt);
            return cnt;
        }

        // 01/30/09
        pBuff->nFilledLen = pBuff->nFlags = pBuff->nOffset = 0;
        frameSize = Read_Buffer(pBuff);
        pBuff->nInputPortIndex = inputPortNum;

        if(frameSize <=0 )  {         // This means there's no more data to read (EndOfStream) or some error in read
            DPRINT(_HERE, 6, "NO FRAME READ for Buffer#%d, BuffPtr=0x%x ... Assuming END_OF_INPUT_STREAM \n", i+1, pBuff);
            process_inputread_eos(pBuff, false);    // 03/17/09
            return -cnt;                            // 03/17/09
        }
        ret = EMPTY_BUFFER(avc_dec_handle, pBuff);
        if (OMX_ErrorNone != ret) {
            ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuff));     // Mark buffer as free
            DPRINT(_HERE, 0, "\nEMPTY_BUFFER for Buffer#%d FAILED with result %d\n", i+1, ret);
            ERROR_ACTION(1, 102);
        } 
        else {
            ATOMIC_DO_GLOBAL((++cnt, ++gnInputUnitCnt));
            // After successful ETB, buffer is with decoder hence do not access any buffer data except header & buffptr
            DPRINT(_HERE, 8, "\nINFO: EMPTY_BUFFER success: DONE EMPTY BUFFER#%d (NrEmptiedThisRun=%d), BuffHdr=0x%x, BuffPtr=0x%x\n", 
                   i+1, cnt, pBuff, pBuff->pBuffer);
        }
    } /* for(i=0... */
    DPRINT(_HERE, 6, "\nReturning from read_and_empty_buffers() with Nr.EmptiedBuffers=%d, Requested Nr=%d\n", cnt, howMany);
    return cnt;
}


// Start FB if needed
static int 
display_start_check(void)
{
    DPRINT(_HERE, 8, "\ndisplay_start_check() Entered...");
    if(fb_display && fb_fd < 0)
    {
        DPRINT(_HERE, 8, "\nTrying to open FB device...");
#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
        QPERF_RESET(render_fb);
#endif

       /* Open frame buffer device */
#ifdef _ANDROID_
      fb_fd = open("/dev/graphics/fb0", O_RDWR);
#else
       fb_fd = open("/dev/fb0", O_RDWR );
#endif

       if (fb_fd < 0) 
       {
         DPRINT(_HERE, 0, "ERROR: cannot open framebuffer device file node\n");
         ERROR_ACTION(1, 152);
         return -1;
       }
       else 
           DPRINT(_HERE, 8, "\nFB Device now open");

   	/* Get the Variable screen Information for the FrameBuffer device */
       if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) 
       {
         close(fb_fd);
         DPRINT(_HERE, 0, "ERROR: cannot retrieve vscreenInfo!\n");
         ERROR_ACTION(1, 155);
         return -1;
       }

      if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0)
      {
         DPRINT(_HERE, 0, "ERROR: cannot retrieve fscreenInfo!\n");
         close(fb_fd);
         return -1;
      }
    }
    DPRINT(_HERE, 8, "\ndisplay_start_check() Leaving...");
    return 0;
}

/* If how == 99 : Turn ON performance mode (minimal logging to a logfile) 
          == 88 : Turn ON ALL logging to the performance logfile (needed in builds that disable printf driver)
          == 77 : Turn ON DEBUG Messages (gnLogLevel is set to value of loglevel)
          ==  0 : Turn OFF performance mode & ALL logging back on (logging back to stdout) 
          == 100 : Just set gnLogLevel to value of loglevel
*/
static void setup_logging(int how, int loglevel)
{
    DPRINT(_HERE, 8, "\nEntered");
    if(99 == how && (NULL == gtMyStdout || stdout == gtMyStdout)) {
        DPRINT(_HERE, 3, "\nAPP SETTINGS CHANGED: PERFORMANCE MODE ON (Minimum Logging, Logfile='%s')", gsPerfLogFilename);
        if(! (gtMyStdout = fopen(gsPerfLogFilename, "wt"))) {
            gtMyStdout = stdout;
            DPRINT(_HERE, 3, "\nERROR: Couldn't open performance logfile '%s' for write", gsPerfLogFilename);
            ERROR_ACTION(3, 202);
        }
        else 
            loglevel = 6;
    }
    else if(88 == how) {
        if(gtMyStdout && gtMyStdout != stdout) {
            DPRINT(_HERE, 3, "\nClosing current logfile...");
            fclose(gtMyStdout);
            gtMyStdout = stdout;
        }
        DPRINT(_HERE, 3, "\nAPP SETTINGS CHANGED: ALL LOGGING TO LOGFILE (Logfilename='%s')", gsPerfLogFilename);
        if(! (gtMyStdout = fopen(gsPerfLogFilename, "wt"))) {
            gtMyStdout = stdout;
            DPRINT(_HERE, 0, "\nERROR: Couldn't open logfile '%s' for write", gsPerfLogFilename);
            ERROR_ACTION(3, 202);
        }
        else 
            loglevel = MAX(9, loglevel);
    }
    else if(0 == how) {
        DPRINT(_HERE, 3, "\nAPP SETTINGS CHANGED: PERFORMANCE MODE OFF (Logfile=stdout, LogLevel=ALL)");
        if(gtMyStdout != NULL && gtMyStdout != stdout)
            fclose(gtMyStdout);

        gtMyStdout = stdout;
        loglevel = MAX(9, loglevel);
    }
    else if(77 == how) {
        DPRINT(_HERE, 3, "\nAPP SETTINGS CHANGED: DEBUG LOGGING (level=%d)", loglevel);
    }

    gnLogLevel = loglevel;
    DPRINT(_HERE, 3, "\nLeaving with LOGLEVEL SET TO %d", gnLogLevel);
}

// Callback handler for Commands from Automation (LASIC handler)
// DO NOT MODIFY cmd_string. Make a copy if you need to modify (max len = LASIC_MAX_MESG_LEN)
static void 
handle_lasic_cmd(const char *cmd_string)
{
    int cmdgot;
    char cmdline[LASIC_MAX_MESG_LEN+50];
    char *w[LASIC_PARSERULE_MAXARGS];
    int l[LASIC_PARSERULE_MAXARGS];
    int nr, i;

    DPRINT(_HERE, 6, "\nEntered...");
    for(i=0; i <LASIC_PARSERULE_MAXARGS; i++) w[i]=NULL, l[i]=0;

    if((cmdgot = LASIC_ParseCommand(cmd_string, &nr, w, l)) <= 0) {
        DPRINT(_HERE, 6, "After LASIC_ParseCommand()...");
        DPRINT(_HERE, 6, "\nPARSE RESULT(ERROR): CmdGot=%d ... %s", cmdgot, ((cmdgot==-1) ? "TOKEN NOT FOUND" : "INVALID ARGS"));
        return;
    }
    else {
        DPRINT(_HERE, 6, "After LASIC_ParseCommand()...");
        snprintf(cmdline, sizeof(cmdline), "\n%s(): PARSE RESULT: CMD=%d, Nr.Args=%d", __FUNCTION__, cmdgot, nr);
        DPRINT(_HERE, 9, "%s", cmdline);
#ifndef _ANDROID_
        for(i=0; i<nr; i++) {
            char temp[50];
            int len;

            snprintf(temp, 50, " / Word[%d](Len=%d):'", i, l[i]); 
            //DPRINT(_HERE, 10, "After Step 1...");
            len = strlen(cmdline);
            snprintf(cmdline+len, sizeof(cmdline)-len, "%s", temp);
            //DPRINT(_HERE, 10, "After Step 2...");
            len = strlen(cmdline);
            snprintf(cmdline+len, sizeof(cmdline)-len, "%.*s'", l[i], w[i]);
            //DPRINT(_HERE, 10, "After Step 3...");
        }
        PRINTF("%s\n", cmdline); 
#endif
    } 
    DPRINT(_HERE, 8, "\nENTERING SWITCH");    
    switch(cmdgot) {
    case LASIC_CMD_PLAY:
    case LASIC_CMD_PAUSE:
    case LASIC_CMD_FFWD:
    case LASIC_CMD_RWND:
        appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, cmdgot, 0, true);
        break;
    case LASIC_CMD_STOP:
        appQ_post_event(APP_EVENT_STOP, 0, 0, true);
        break;
    case LASIC_CMD_PLAY_FROM_FILE:
        if(! strncasecmp("NONE", w[1], l[1]))
           strcpy(gsInputFilename, "");   // Reset input file name 
        else
            snprintf(gsInputFilename, MIN(sizeof(gsInputFilename), l[1]+1), "%s", w[1]);
        DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : INPUT FILENAME='%s'\nSTARTING PLAYBACK FROM FILE...\n", (*gsInputFilename ? gsInputFilename : "<Hardcoded filename>"));
        appQ_post_event(APP_EVENT_EOS_OUTPUT, 0, 0, true);
        appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, LASIC_CMD_PLAY, 0, true);
        break;
    case LASIC_CMD_TOGGLE_LOOPING:
        gbLoopedPlay = (true == gbLoopedPlay ? false : true);
        gbLoopedPlay ? (gnLoopCount = 1) : 0;
        DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : LOOPED PLAYBACK = %s", gbLoopedPlay == true ? "ON" : "OFF");
        break;
    case LASIC_CMD_SET_RESOLUTION:
        snprintf(cmdline, MIN(sizeof(cmdline), l[0]+1), "%s", w[0]);
        DPRINT(_HERE, 8, "\nLASIC_CMD_SET_RESOLUTION: cmdline=%s", cmdline);
        i = token_2_enum(0, cmdline, 0);
        if(i >= VIDEO_DISPLAY_FORMAT_QCIF && i < VIDEO_DISPLAY_FORMAT_MAX) {
            geFormat = (video_display_format_type)i;
            DPRINT(_HERE, 8, "\nSETTINGS CHANGED : RESOLUTION=%s / geFormat=%d", cmdline, geFormat);
        }
        break;
    case LASIC_CMD_GET_RESOLUTION:
        DPRINT(_HERE, 8, "\nLASIC_CMD_GET_RESOLUTION : Pre-set RESOLUTION=%d, Current Clip Width=%d / Height=%d", geFormat, width, height);
        break;
    case LASIC_CMD_SET_FRAMERATE:
        break;
    case LASIC_CMD_GET_FRAMERATE:
        break;
    case LASIC_CMD_SETPARAM:
        if(!strncmp(w[1], "OUTPUT_TO_FILENAME", l[1])) {
            if(! strncasecmp("NONE", w[2], l[2]))
               strcpy(gsOutputFilename, "");   // Reset output file name 
            else
               snprintf(gsOutputFilename, l[2], "%s", w[2]);
            DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : OUTPUT FILENAME='%s'", (*gsOutputFilename ? gsOutputFilename : "<Hardcoded filename>"));
        }
        /* INTERNAL (for debugging only) - Set/Get App State */
        else if(!strncmp(w[1], "APP_STATE", l[1])) {
            int state = w[2][0]-'0';
            if(state >= APP_STATE_Foetus && state <= APP_STATE_WaitOmxFlush) {
                APP_CHANGE_STATE(state); 
                DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : NEW APP_STATE=%d", geAppState);
            }
            else { DPRINT(_HERE, 0, "\nERROR: SETPARAM APP_STATE: Invalid value"); ERROR_ACTION(4, 215); }
        }
        else if(!strncmp(w[1], "FFWD_SKIPFRAMES", l[1])) {
            int nr = w[2][0]-'0';
            if(nr > 0) {
                gnFfwd_IFrameSkipCnt = nr;
                DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : NEW FFWD_SKIPFRAMES=%d", gnFfwd_IFrameSkipCnt);
            }
            else DPRINT(_HERE, 8, "\nSETPARAM FFWD_SKIPFRAMES: Invalid value");
        }
        else if(!strncmp(w[1], "OMX_STATE", l[1])) {
            OMX_STATETYPE stateval;
            char statename[30];
            snprintf(statename, sizeof(statename), "%.*s", l[2], w[2]);
            if(! map_omx_state_name(1, &stateval, statename)) {
                DPRINT(_HERE, 8, "\nAPP_EVENT_OMX_STATE_SET: OMX State Transition request for INVALID StateName='%s'", statename);
                break;
            }
            DPRINT(_HERE, 8, "\nSETPARAM : Got OMX State Transition Request[NewState='%s' Val=%d]", statename, (int)stateval);
            appQ_post_event(APP_EVENT_OMX_STATE_SET, (int)stateval, 0, true);
        }
        else {
            int i;
            for(i=1; i<nr; i+=2) {
                if(get_ptr_to_global(w[i], l[i])) {     // Set value of a global int var
                    char str[8];
                    int *p = (int*)get_ptr_to_global(w[i], l[i]);
                    snprintf(str, 8, "%s", w[i+1]);
                    *p = atoi(str);
                    DPRINT(_HERE, 8, "\nSET_PARAM : GLOBAL VAR CHANGED : %.*s=%d", l[i], w[i], atoi(str));
                }
                else
                    DPRINT(_HERE, 8, "\nSETPARAM(%.*s):%.*s NOT RECOGNISED/SUPPORTED PARAM", l[1], w[1], l[i], w[i]);
            }
        }
        break;
    case LASIC_CMD_GETPARAM:
        if(!strncmp(w[1], "APP_STATE", l[1])) {
            DPRINT(_HERE, 8, "\nLASIC_CMD_GETPARAM: CURRENT APP_STATE=%d", geAppState);
        }
        else if(!strncmp(w[1], "gnTotalOutputBufs", l[1])) {
            DPRINT(_HERE, 8, "\nLASIC_CMD_GETPARAM: gnTotalOutputBufs=%d", gnTotalOutputBufs);
        }
        else if(!strncmp(w[1], "FFWD_SKIPFRAMES", l[1])) {
            DPRINT(_HERE, 8, "\nLASIC_CMD_GETPARAM: CURRENT FFWD_SKIPFRAMES=%d", gnFfwd_IFrameSkipCnt);
        }
        else if(!strncmp(w[1], "DUMP_VARS", l[1])) {
            dump_global_data();
        }
        else if(!strncmp(w[1], "DUMP_BUFFS_IN", l[1])) {
            DPRINT(_HERE, 6, "DUMPING ALL NON-EMPTY INPUT BUFFERS");
            ATOMIC_DO_GLOBAL(dump_omx_buffers(0, 0));
            DPRINT(_HERE, 6, "DUMPING ALL USED INPUT BUFFERS");
            ATOMIC_DO_GLOBAL(dump_omx_buffers(0, USEDINBUF));
        }
        else if(!strncmp(w[1], "DUMP_BUFFS_OUT", l[1])) {
            DPRINT(_HERE, 6, "DUMPING ALL NON-EMPTY OUTPUT BUFFERS\n\n");
            ATOMIC_DO_GLOBAL(dump_omx_buffers(0, 10));
            DPRINT(_HERE, 6, "DUMPING ALL USED OUTPUT BUFFERS");
            ATOMIC_DO_GLOBAL(dump_omx_buffers(0, USEDOUTBUF));
        }
        else
            DPRINT(_HERE, 8, "\nGETPARAM: NOT RECOGNISED/SUPPORTED PARAM (%.*s):%.*s ", w[0], l[0], w[1], l[1]);

        break;
    case LASIC_CMD_EXIT:
        appQ_post_event(APP_EVENT_END, 0, 0, true);
        DPRINT(_HERE, 8, "\nAPP CLEANING UP & SHUTTING DOWN...");
        break;
    case LASIC_CMD_ABORT:
        DPRINT(_HERE, 8, "\nAPP RECEIVED ABORT...No cleanup done...");
        dump_global_data();
        exit(1);
        break;
    case LASIC_CMD_TOGGLE_FB_DISPLAY:
        fb_display = ! fb_display;
        avc_dec_handle ? display_start_check() : 0; 
        DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : FB_DISPLAY='%s' (effective next playback)", fb_display ? "ON" : "OFF");
        break;
    case LASIC_CMD_TOGGLE_FILE_WRITE:
        file_write = ! file_write;
        DPRINT(_HERE, 8, "\nAPP SETTINGS CHANGED : FILE_WRITE='%s' (effective next playback)", file_write ? "ON" : "OFF");
        break;
    case LASIC_CMD_SET_LOGLEVEL:
        {
            char temp[4];
            snprintf(temp, 4, "%.*s", l[1], w[1]);
            setup_logging(atoi(temp), atoi(temp));
        }
        break;

    default:
        DPRINT(_HERE, 8, "\nCommand='%.*s' NOT SUPPORTED", l[0], w[0]);
        break;
    }   /* switch(cmdgot.... */
    DPRINT(_HERE, 6, "\nLeaving... ");
}


static OMX_ERRORTYPE 
EventHandler(OMX_IN OMX_HANDLETYPE hComponent,
                           OMX_IN OMX_PTR pAppData, 
                           OMX_IN OMX_EVENTTYPE eEvent,
                           OMX_IN OMX_U32 nData1, OMX_IN OMX_U32 nData2,
                           OMX_IN OMX_PTR pEventData)
{	
    DPRINT(_HERE, 6, "Entered... ");

    switch(eEvent) {
        case OMX_EventCmdComplete:
            DPRINT(_HERE, 6, "\n OMX_EventCmdComplete \n");
            // check nData1 for DISABLE event
            if(OMX_CommandPortDisable == (OMX_COMMANDTYPE)nData1)
            {
                DPRINT(_HERE, 6, "*********************************************\n");
                DPRINT(_HERE, 6, "Recieved OMX_Event: Port DISABLE Complete [Port %d]\n",nData2);
                DPRINT(_HERE, 6, "*********************************************\n");
            }
            else if(OMX_CommandPortEnable == (OMX_COMMANDTYPE)nData1)
            {
                DPRINT(_HERE, 6, "*********************************************\n");
                DPRINT(_HERE, 6, "Recieved OMX_Event: Port ENABLE Complete [Port %d]\n",nData2);
                DPRINT(_HERE, 6, "*********************************************\n");
            }
            else if(OMX_CommandFlush == (OMX_COMMANDTYPE)nData1)
            {
                DPRINT(_HERE, 6, "*********************************************\n");
                DPRINT(_HERE, 6, "Recieved OMX_Event: FLUSH Complete [Port %d]\n",nData2);
                DPRINT(_HERE, 6, "*********************************************\n");
            }
            else if(OMX_CommandStateSet == (OMX_COMMANDTYPE)nData1)
            {
                DPRINT(_HERE, 6, "*********************************************\n");
                DPRINT(_HERE, 6, "Recieved OMX_Event: CommandStateSet Complete [State %d]\n",nData2);
                DPRINT(_HERE, 6, "*********************************************\n");
            }
            event_complete(eEvent, nData1, nData2, pEventData);     
	    break;
        case OMX_EventError:
                DPRINT(_HERE, 0, "*********************************************\n");
                DPRINT(_HERE, 0, "Error %x:%x",nData1,nData2);
                DPRINT(_HERE, 0, "\nReceived OMX_EventError \n");
                DPRINT(_HERE, 0, "*********************************************\n");
                ERROR_ACTION(2, 111);
	    break;

        case OMX_EventPortSettingsChanged:
                DPRINT(_HERE, 6, "*********************************************\n");
                DPRINT(_HERE, 6, "\nReceived OMX_Event: PortSettingsChanged [Port %d]\n",nData1);
                DPRINT(_HERE, 6, "*********************************************\n");
                // reset the event 
                event_complete(eEvent, nData1, nData2, pEventData);     
                /* 05/21/09 */
                if(gbWaitFirstFrameDec) {
                    APP_CHANGE_STATE(APP_STATE_OutPortReconfig);   
                }
                else {
                    ATOMIC_DO_GLOBAL(gbDuringOutPortReconfig = true);
                    appQ_post_event(APP_EVENT_PORT_RECONFIG, nData1, 0, false);     // Send app event to handle port reconfig     
                }
                break;

        case OMX_EventBufferFlag:
            DPRINT(_HERE, 6, "*********************************************\n");
            DPRINT(_HERE, 6, "\n OMX_EventBufferFlag [outPortIndex %d] [outBuf nFlags 0x%x] \n",nData1, nData2);
            DPRINT(_HERE, 6, "*********************************************\n");
            gbGotBufferFlagEvent = true;
            break;
    default:
        DPRINT(_HERE, 6, "*********************************************\n");
	    DPRINT(_HERE, 6, "\nReceived OMX_Event: Unknown Event \n");
        DPRINT(_HERE, 6, "*********************************************\n");
	    break;
    }
    DPRINT(_HERE, 6, "\nWaking up any threads that called expect_some_event()...");
    received_some_event(eEvent, nData1, nData2, pEventData);     
    DPRINT(_HERE, 6, "Leaving...\n");
    return OMX_ErrorNone;
}


/* OMX Callback handler for EmptyBufferDone */
static OMX_ERRORTYPE EBD_Handler(OMX_IN OMX_HANDLETYPE hComponent, 
                                     OMX_IN OMX_PTR pAppData, 
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    appQ_evtdata_OMX_EBD_t ebd_data;
    int usedin, usedout;
    LATENCY_FUNC_ENTRY;

    LOCK_MUTEX(&gtGlobalVarMutex);
        ++ebd_cnt;
        usedin = dump_omx_buffers(2, USEDINBUF);
        usedout = dump_omx_buffers(2, USEDOUTBUF);
    UNLOCK_MUTEX(&gtGlobalVarMutex);

    DPRINT(_HERE, 8, "Entered %s[%d] [fbd_cnt=%d, UsedIN=%d, UsedOUT=%d, IFrame_Cnt=%d, FFd_UnitsDecoded=%d]: BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nOffset=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x ", __FUNCTION__, 
            ebd_cnt, fbd_cnt, usedin, usedout, gnIFrameCount, gnFfwd_UnitsDecoded, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nOffset,
            pBuffer->nFlags, pBuffer->nTimeStamp, pBuffer->nTickCount);

    if(APP_STATE_CollectOmxBuffers == geAppState || APP_STATE_CollectOmxInBuffers == geAppState || APP_STATE_OutPortReconfig == geAppState
       || APP_STATE_WaitFirstFrame == geAppState || true == gbDuringOutPortReconfig) {
        DPRINT(_HERE, 6, "\n%s[%d]: (CollectOmxBuffers: geAppState=%d / DuringOutReconfig=%s): RETURNING BUFFER [BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x] ", __FUNCTION__, 
               ebd_cnt, geAppState, gbDuringOutPortReconfig ? "YES" : "NO", pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags,
               pBuffer->nTimeStamp, pBuffer->nTickCount);
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
        LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
    }
    else if(APP_STATE_CollectOmxOutBuffers == geAppState) {
        DPRINT(_HERE, 1, "\n%s[%d]: ERROR : Input buffer callback when OUTPUT callback expected [AppState=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]...continuing...\n", __FUNCTION__, 
               ebd_cnt, geAppState, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags,
               pBuffer->nTimeStamp, pBuffer->nTickCount);
        // 04/23/09 pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        // 04/23/09 ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
        ERROR_ACTION(4, 57);
        //04/23/09 LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
    }

    if(bInputEosReached) {
        DPRINT(_HERE, 6, "\n*********************************************\n");
        DPRINT(_HERE, 6, "   EBD:: was already EOS on input port\n ");
        DPRINT(_HERE, 6, "*********************************************\n");
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
        LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
    }

    if(APP_STATE_WaitOmxFlush == geAppState) {
        DPRINT(_HERE, 8, "\n%s[%d]: INFO: During FLUSH[%d] RETURNING BUFFER [BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]", __FUNCTION__, 
               ebd_cnt, gnFlushCount, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags,
               pBuffer->nTimeStamp, pBuffer->nTickCount);
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
        LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
    }
    else if(APP_STATE_Meditating == geAppState) {
        if(true == gbCollectBuffsInPaused) { 
            DPRINT(_HERE, 1, "\n%s[%d]: WARNING : RETURNING INPUT BUFFER during OMX PAUSE [gbCollectBuffsInPaused=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]\n", __FUNCTION__, 
                   ebd_cnt, gbCollectBuffsInPaused, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
                   pBuffer->nTimeStamp, pBuffer->nTickCount);
            pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
            ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
            LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
        }
        DPRINT(_HERE, 1, "\n%s[%d]: WARNING : REFEEDING Input Buffer returned during OMX PAUSE [gbCollectBuffsInPaused=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]\n", __FUNCTION__, 
                   ebd_cnt, gbCollectBuffsInPaused, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
                   pBuffer->nTimeStamp, pBuffer->nTickCount);
        /* Ref. Sec 3.1.1.2.4 Pg 51 of IL 1.2 spec */
        /* ERROR_ACTION(3, 56);
        LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone); */
    } 
    LATENCY_FUNC_CONT(gnEBDLatency);

    if(1 != gbProfileMode && 3 != gbProfileMode) {
        return EmptyBufferDone(hComponent, pAppData, pBuffer);
    }

    /* Only if Profiling mode is on for EBD delay, que the EBD info to app main thread & let that process it. This is done in order to control the input
    data feed rate to simulate an actual use case of 'X' FPS video (ie. input feed rate should be around 30-35 input buffers/sec for 
    a video of 30 FPS. Allowing decode to happen uncontrolled causes very high cpu usage by decoder bcos it does the decode at max speed. With 
    the help of ADSP, this speed comes to around 60 fps or higher on an 8k board */
    ebd_data.hComponent = hComponent;
    ebd_data.pAppData = pAppData;
    ebd_data.pBuffer = pBuffer;

    appQ_post_event(APP_EVENT_EMPTY_BUFFER_DONE, 0, (void*)&ebd_data, false);
    return OMX_ErrorNone;
}


static OMX_ERRORTYPE FBD_Handler(OMX_IN OMX_HANDLETYPE hComponent, 
                                     OMX_IN OMX_PTR pAppData, 
                                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
    appQ_evtdata_OMX_EBD_t fbd_data;    // FBD callback has same prototype as EBD
    int usedin, usedout;
    LATENCY_FUNC_ENTRY;

    LOCK_MUTEX(&gtGlobalVarMutex);
        ++fbd_cnt;
        usedin = dump_omx_buffers(2, USEDINBUF);
        usedout = dump_omx_buffers(2, USEDOUTBUF);
    UNLOCK_MUTEX(&gtGlobalVarMutex);

    DPRINT(_HERE, 8, "Entered %s[%d] [ebd_cnt=%d, UsedIN=%d, UsedOUT=%d, IFrame_Cnt=%d, FFd_UnitsDecoded=%d]: pBuffHdr=0x%x, pBuffer=0x%x, nFilledLen=%d, nOffset=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x ", __FUNCTION__, 
            fbd_cnt, ebd_cnt, usedin, usedout, gnIFrameCount, gnFfwd_UnitsDecoded, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nOffset,
            pBuffer->nFlags, pBuffer->nTimeStamp, pBuffer->nTickCount);

    if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
        DPRINT(_HERE, 0, "\n[fbd_cnt=%d / ebd_cnt=%d] DEBUG : FOUND EOS ON OUTPUT BUFFER. AppState=%d", fbd_cnt, ebd_cnt, geAppState);
    }

    if(APP_STATE_CollectOmxBuffers == geAppState || APP_STATE_CollectOmxOutBuffers == geAppState || APP_STATE_OutPortReconfig == geAppState) {
        DPRINT(_HERE, 8, "\n%s[%d]: (CollectOmxBuffers): RETURNING BUFFER [BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x] ", __FUNCTION__, 
               fbd_cnt, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, pBuffer->nTimeStamp, pBuffer->nTickCount);
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone);
    }
    else if(APP_STATE_CollectOmxInBuffers == geAppState) {
        DPRINT(_HERE, 1, "\n%s[%d]: ERROR : Output buffer returned when INPUT return expected [AppState=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]...RETURNING BUFFER...", __FUNCTION__, 
               fbd_cnt, geAppState, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, pBuffer->nTimeStamp, pBuffer->nTickCount);
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
        ERROR_ACTION(4, 57);
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone);
    }

    if(bOutputEosReached /* 04/29/09 && gbSendEosAtStart == 0 */){
        DPRINT(_HERE, 8, "\n*********************************************\n");
        DPRINT(_HERE, 8, "   FBD[%d]:: was already EOS on output port\n ", fbd_cnt);
        DPRINT(_HERE, 8, "*********************************************\n");
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone);
    }

    if(APP_STATE_WaitOmxFlush == geAppState) {
        DPRINT(_HERE, 8, "\n%s[%d]: INFO: During FLUSH[%d] RETURNING BUFFER [BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]", __FUNCTION__, 
               fbd_cnt, gnFlushCount, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags,
               pBuffer->nTimeStamp, pBuffer->nTickCount);

        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone); 
    }
    else if(APP_STATE_Meditating == geAppState) {
        if(true == gbCollectBuffsInPaused) { 
            DPRINT(_HERE, 1, "\n%s[%d]: WARNING : RETURNING OUTPUT Buffer during OMX PAUSE [gbCollectBuffsInPaused=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]\n", __FUNCTION__, 
                   fbd_cnt, gbCollectBuffsInPaused, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
                   pBuffer->nTimeStamp, pBuffer->nTickCount);
            pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
            ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
            LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone); 
        }
        DPRINT(_HERE, 1, "\n%s[%d]: WARNING : REFEEDING OUTPUT Buffer returned during OMX PAUSE [gbCollectBuffsInPaused=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]\n", __FUNCTION__, 
                   fbd_cnt, gbCollectBuffsInPaused, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
                   pBuffer->nTimeStamp, pBuffer->nTickCount);
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        /* Ref. Sec 3.1.1.2.4 Pg 51 of IL 1.2 spec */
        /* ERROR_ACTION(3, 56);
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone); */
    }
    LATENCY_FUNC_CONT(gnFBDLatency);

    if(2 != gbProfileMode && 3 != gbProfileMode) {
        return FillBufferDone(hComponent, pAppData, pBuffer);
    }

    /* Only if Profiling mode is on for FBD delay, que the FBD info to app main thread & let that process it. This is done in order to control the input
    data feed rate to simulate an actual use case of 'X' FPS video (ie. input feed rate should be around 30-35 input buffers/sec for 
    a video of 30 FPS. Allowing decode to happen uncontrolled causes very high cpu usage by decoder bcos it does the decode at max speed. With 
    the help of ADSP, this speed comes to around 60 fps or higher on an 8k board */
    fbd_data.hComponent = hComponent;
    fbd_data.pAppData = pAppData;
    fbd_data.pBuffer = pBuffer;

    appQ_post_event(APP_EVENT_FILL_BUFFER_DONE, 0, (void*)&fbd_data, false);
    return OMX_ErrorNone;
}


static void 
delay_in_profile_mode(int mode)
{
    const int framesBeforeAdjustment = 3;
    static long int usleeptime = 0;
    static float currFps;
    long int diff = 0;
    struct timeval currtime;

    if(0 == (gnOutputUnitCnt % framesBeforeAdjustment)) {
        (void)gettimeofday(&currtime, NULL);
        diff = diff_timeofday(&currtime, &gtDecStartTimeVal);
        currFps = (gnOutputUnitCnt*1.0) / ((diff == 0 ? 1 : diff)/(1000000.0));
        if(gnProfileFPS > 0) {
            // TargetFPS=gnProfileFPS ; CurrFPS = gnOutputUnitCnt / (diff / 10^6) ; Do delay if (CurrFPS - TargetFPS) > 0
            usleeptime = (long int)((gnOutputUnitCnt*1000000.0)/(gnProfileFPS*1.0)) - diff;
            // Spread the required sleep time evenly over the next few frames
            usleeptime = usleeptime / framesBeforeAdjustment;
        }
        else 
            usleeptime = gnProfileSleepUsecs;
    }

    if(usleeptime > 0) {
        DPRINT(_HERE, 8, "PROFILE MODE [%s Delay / InUnits=%d / OutUnits=%d]: Desired FPS=%d, CurrFPS=%.1f, USleep=%u... ", mode ? (mode==1 ? "EBD" : "FBD") : "NO", gnInputUnitCnt, gnOutputUnitCnt, gnProfileFPS, currFps, (unsigned int)usleeptime/5);
        usleep((unsigned int)usleeptime/5);
    }
    else 
        DPRINT(_HERE, 8, "PROFILE MODE [%s Delay / InUnits=%d / OutUnits=%d]: Desired FPS=%d, CurrFPS=%.1f, SKIPPED UsleepValue=%ld... ", mode ? (mode==1 ? "EBD" : "FBD") : "NO", gnInputUnitCnt, gnOutputUnitCnt, gnProfileFPS, currFps, usleeptime);
}


/* THREAD-SAFETY: 
    On SMP, all global reads/writes should be mutex protected.
    On Uniprocessor, all global writes & global reads larger than the machine wordsize(Int) need to be mutex protected 
   GLOBALS READ BY THIS FUNCTION:
    [gnIFrameCount] , [gnFfwd_UnitsDecoded] , [geAppState] 
   GLOBALS MODIFIED BY THIS FUNCTION:  
    [gtDecEndTimeVal] , [ebd_cnt], [geAppState]
   GLOBALS THAT NEED TO BE THREAD-SAFE [ie. globals that EITHER are updated in another thread OR are larger than the machine wordsize(Int)]: 
    [gtDecEndTimeVal] , [gnFfwd_UnitsDecoded] , [ebd_cnt], [geAppState] [geAppState]
    */
static OMX_ERRORTYPE 
EmptyBufferDone(OMX_IN OMX_HANDLETYPE hComponent, 
                              OMX_IN OMX_PTR pAppData, 
                              OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{

    int readBytes =0; int bufCnt=0;
    OMX_ERRORTYPE result;
    LATENCY_FUNC_ENTRY;


    // Mark buffer as empty
    pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;

    /* Even if app in FFWD mode, keep reading & emptying buffers just like in PLAY mode. When reqd. buffers are decoded for FFWD, FillBufferDone
    will handle it & throttle main thread again */
    if(APP_STATE_Walking == geAppState || APP_STATE_Sprinting == geAppState || APP_STATE_Meditating == geAppState) { 
        /* For profiling mode-1 (delay ETBs) : to match framerate so that excess decode doesn't eat up CPU */
        if(1 == gbProfileMode || 3 == gbProfileMode)
            delay_in_profile_mode(gbProfileMode);

        if((readBytes = Read_Buffer(pBuffer)) > 0) {
            if(OMX_ErrorNone == (result = EMPTY_BUFFER(hComponent, pBuffer))) {
                ATOMIC_DO_GLOBAL(++gnInputUnitCnt);
                // After successful ETB, buffer is with decoder hence do not access any buffer data except header & buffptr
                DPRINT(_HERE, 8, "\n%s[%d]: INFO: DONE EMPTY BUFFER [BuffHdr=0x%x, Buffer=0x%x]", __FUNCTION__, 
                       ebd_cnt, pBuffer, pBuffer->pBuffer);
                LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
            }
            else {
                ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
                DPRINT(_HERE, 0, "\n%s[%d]: ERROR: Call to EMPTY_BUFFER() FAILED with result=%d\n", __FUNCTION__, ebd_cnt, result);
                ERROR_ACTION(1, 102);
            }
        }
        // End of input stream 
        else {
            process_inputread_eos(pBuffer, false);     // 03/17/09
        } /* else... (for end of input stream) */
        LATENCY_FUNC_EXIT(gnEBDLatency, result);
    } /* else if(APP_STATE_Walking ==... */

    DPRINT(_HERE, 5, "\n%s[%d]: WARNING: UNEXPECTED APP_STATE=%d, IFrame_Cnt=%d, FFd_UnitsDecoded=%d [BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]...RETURNING BUFFER...\n", __FUNCTION__, 
           ebd_cnt, geAppState, gnIFrameCount, gnFfwd_UnitsDecoded, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
                   pBuffer->nTimeStamp, pBuffer->nTickCount);
    ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuffer));    // Mark buffer as free
    LATENCY_FUNC_EXIT(gnEBDLatency, OMX_ErrorNone);
}


/* THREAD-SAFETY: 
    On SMP, all global reads/writes should be mutex protected.
    On Uniprocessor, all global writes & global reads larger than the machine wordsize(Int) need to be mutex protected 
   GLOBALS READ BY THIS FUNCTION:
    [gnIFrameCount], 
   GLOBALS MODIFIED BY THIS FUNCTION:  
    [fbd_cnt], [gnOutputUnitCnt], [gtDecEndTimeVal], [bOutputEosReached], [gnFfwd_UnitsDecoded]
   GLOBALS THAT NEED TO BE THREAD-SAFE [ie. globals that EITHER are updated in another thread OR are larger than the machine wordsize(Int)]: 
    [fbd_cnt], [gnOutputUnitCnt], [gtDecEndTimeVal], [bOutputEosReached], [gnFfwd_UnitsDecoded] */
static OMX_ERRORTYPE 
FillBufferDone(OMX_OUT OMX_HANDLETYPE hComponent,
                             OMX_OUT OMX_PTR pAppData, 
                             OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer) 
{
    OMX_ERRORTYPE result;
    int bufCnt=0;
    int bytes_written = 0;
    LATENCY_FUNC_ENTRY;

    /* (05/21/09) Use 'wait for first frame EBD or port reconfig' method (as per spec) if the flag is enabled */
    if(gbWaitFirstFrameDec && APP_STATE_WaitFirstFrame == geAppState) {
        DPRINT(_HERE, 6, "\nFBD: THIS IS THE FIRST FRAME DECODE (No PortReconfig happened)...");
        APP_CHANGE_STATE(APP_STATE_Walking);
    }

    if(pBuffer->nFilledLen > 0) {
        ATOMIC_DO_GLOBAL(++gnOutputUnitCnt); 
    }
    /* For profiling mode-1 (delay ETBs) : to match framerate so that excess decode doesn't eat up CPU */
    if(2 == gbProfileMode || 3 == gbProfileMode)
        delay_in_profile_mode(gbProfileMode);

    /*********************************************
    Write the output of the decoder to the file.
    *********************************************/
    if(file_write && outputBufferFile && pBuffer->nFilledLen > 0) {
        bytes_written = fwrite((const char *)pBuffer->pBuffer,
                            1, pBuffer->nFilledLen,outputBufferFile);
        if (bytes_written < 0) {
            DPRINT(_HERE, 0, "\n%s[%d]: ERROR: Failed to write to output file\n", __FUNCTION__, fbd_cnt);
        } 
        else {
            DPRINT(_HERE, 8, "\n%s[%d]: Wrote %d of %d YUV bytes to the file\n", __FUNCTION__, fbd_cnt,
                                                      bytes_written, pBuffer->nFilledLen);
        }
    }

    if(fb_display && pBuffer->nFilledLen > 0) {
        DPRINT(_HERE, 8, "\nDisplaying Frame#%d", gnOutputUnitCnt);
#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
        //06/01/09 QPERF_TIME(render_fb, render_fb(pBuffer->pOutputPortPrivate));
        QPERF_TIME(render_fb, render_fb(pBuffer));
#else
        display_frame_7k((void*)pBuffer);
#endif
        DPRINT(_HERE, 8, "\nDONE: Display Frame#%d", gnOutputUnitCnt);
    }

    if(gnOutputUnitCnt == 1) {
        DPRINT(_HERE, 6, "\n%s[%d]: Displayed First Decoded Frame [AppState=%d, IFrame_Cnt=%d, FFd_UnitsDecoded=%d, BuffHdr=0x%x, Buffer=0x%x, nFilledLen=%d, nFlags=0x%x, nTimeStamp=0x%x, nTickCount=0x%x]\n", __FUNCTION__, 
               fbd_cnt, geAppState, gnIFrameCount, gnFfwd_UnitsDecoded, pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags, 
               pBuffer->nTimeStamp, pBuffer->nTickCount);

       (void)gettimeofday(&gtFirstFrameDecTimeVal, NULL);
    }

    if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
        DPRINT(_HERE, 6, "\n***************************************************\n");
        DPRINT(_HERE, 6, "%s[%d]: End Of Stream Flag was found set on output buffer\n", __FUNCTION__, fbd_cnt);
        DPRINT(_HERE, 6, "\n***************************************************\n");
        LOCK_MUTEX(&gtGlobalVarMutex);
            bOutputEosReached = true;
            (void)gettimeofday(&gtDecEndTimeVal, NULL);
            pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
            update_used_buffer_list(OUTBUF, FREE, pBuffer);
        UNLOCK_MUTEX(&gtGlobalVarMutex);
        if(! gbNoEosInLoopedPlay && ! gbNoWaitEosOnLastBuff)
            appQ_post_event(APP_EVENT_EOS_OUTPUT, 0, 0, false);  // Dont block since it's in decoder context
        LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone);
    }

        /* This flag is set by testapp only at seek points(I-frames) that are decoded during FFWD. Hence this outbuf buffer must be a decoded
       I-frame. Dump this to a file as needed */
    if(gbDumpDecodedIframes && (pBuffer->nFlags & OMX_BUFFERFLAG_STARTTIME || pBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) && outputIFramesFile) {
        DPRINT(_HERE, 6, "\nDUMPING I-frame OUTPUT BUFFER (IFrame#%d, Header=0x%x, Buffer=0x%x, Size=%d)...", 
                            gnIFrameCount,pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen);
        //hex_dump_packet(gtMyStdout, 0, pBuff->pBuffer, pBuff->nFilledLen, 30);
        fwrite((const char *)pBuffer->pBuffer,
                            1, pBuffer->nFilledLen, outputIFramesFile);
        gnDumpedDecIframeCnt++;
    }
    else if(gbDumpDecodedIframes) {
        DPRINT(_HERE, 8, "\n(DumpDecodedIFrames ==TRUE): Frame NOT dumped (IFrame#%d, Header=0x%x, Buffer=0x%x, Size=%d, nFlags=0x%x)", 
               gnIFrameCount,pBuffer, pBuffer->pBuffer, pBuffer->nFilledLen, pBuffer->nFlags);
    }

    // 01/27/09 
    /* If app is in Ffwd mode then the main thread is waiting for all needed buffers to be decoded, hence throttle main thread with the FFWD event 
       when all buffers are done, so that main thread will again flush, forward seek to an I-frame & start decode */
    LOCK_MUTEX(&gtGlobalVarMutex);
    ++gnFfwd_UnitsDecoded;
    if(APP_STATE_Sprinting == geAppState && gnFfwd_UnitsDecoded > gnFfwd_NrUnitsToDecode) {
        pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
        /* 08/26/09 TODO CHECK IF NEEDED to be commented update_used_buffer_list(OUTBUF, FREE, pBuffer);
        UNLOCK_MUTEX(&gtGlobalVarMutex); */
        DPRINT(_HERE, 4, "\nWARNING: FFWD : Extra FillBufferDone() after main thread was throttled [gnFfwd_NrUnitsToDecode=%d]...RETURNING BUFFER...", gnFfwd_NrUnitsToDecode);
        /* LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone); */
    }
    if(APP_STATE_Sprinting == geAppState && gnFfwd_UnitsDecoded == gnFfwd_NrUnitsToDecode) {  
        UNLOCK_MUTEX(&gtGlobalVarMutex);
        DPRINT(_HERE, 8, "\n%s[%d]: FFWD: I-FrameCnt=%d, NrDecodedUnits=%d...Collected all output buffers...THROTTLING MAIN THREAD...\n", __FUNCTION__, fbd_cnt, gnIFrameCount, gnFfwd_UnitsDecoded);
        appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, LASIC_CMD_FFWD, 0, true);  // This call blocks to avoid main() thread starvation
        //LATENCY_FUNC_EXIT(gnFBDLatency, OMX_ErrorNone);
        LOCK_MUTEX(&gtGlobalVarMutex);  // Mutex should remain locked
    }
    DPRINT(_HERE, 8, "%s[%d], IFrame_Cnt=%d...Calling next FillBuffer...\n", __FUNCTION__, fbd_cnt, gnIFrameCount);
    pBuffer->nFilledLen = pBuffer->nFlags = pBuffer->nOffset = 0;
    pBuffer->nOutputPortIndex = 1;
    UNLOCK_MUTEX(&gtGlobalVarMutex);

    if((result = OMX_FillThisBuffer(hComponent, pBuffer)) == OMX_ErrorNone) {
        // After successful FTB, buffer is with decoder hence do not access any buffer data except header & buffptr
        DPRINT(_HERE, 8, "\n%s[%d]: OMX_FillThisBuffer success: DONE FILL BUFFER, BuffHdr=0x%x, BuffPtr=0x%x\n", __FUNCTION__, fbd_cnt,
               pBuffer, pBuffer->pBuffer);
    }
    else {
        ATOMIC_DO_GLOBAL(update_used_buffer_list(OUTBUF, FREE, pBuffer));
        DPRINT(_HERE, 6, "\n%s[%d]: ERROR: FILL BUFFER FAILED with retcode=%d, BuffPtr=0x%x, nFilledLen=%d, nFlags=0x%x, nTickCount=%d, nTimeStamp=%lu\n", 
               __FUNCTION__, fbd_cnt, result, pOutYUVBufHdrs[bufCnt]->pBuffer, pOutYUVBufHdrs[bufCnt]->nFilledLen, pOutYUVBufHdrs[bufCnt]->nFlags,
               (unsigned long)pOutYUVBufHdrs[bufCnt]->nTickCount, (unsigned long)pOutYUVBufHdrs[bufCnt]->nTimeStamp);
        ERROR_ACTION(0, 105);
    }
    LATENCY_FUNC_EXIT(gnFBDLatency, result);
}

/* Convert strings & integers to appropriate enums (eg. for resolution n maybe other things later on) 
   how == 0 : Convert 'token' into enum 
   how == 1 : Simply return enum_value[index] 
   RETURNS: Appropriate enum value or error (-1) */
static int 
token_2_enum(int how, char *token, int index)
{
    struct t2v_t {
        int enum_val;
        char *token;
    };
    const struct t2v_t enum_list[] = {
        { VIDEO_DISPLAY_FORMAT_QCIF, "QCIF" },
        { VIDEO_DISPLAY_FORMAT_QVGA, "QVGA" },
        { VIDEO_DISPLAY_FORMAT_HVGA, "HVGA" },
        { VIDEO_DISPLAY_FORMAT_VGA, "VGA" },
        { FILE_TYPE_ARBITRARY_BYTES, "arbitrary" },
        { FILE_TYPE_PICTURE_START_CODE, "startcode" },
        { FILE_TYPE_DAT_PER_AU, "dat" },
        { FILE_TYPE_RCV, "rcv" },
        { FILE_TYPE_VC1, "vc1" },
    };
    int i;
    int size = NUMELEM_ARRAY(enum_list);
    DPRINT(_HERE, 8, "\nArray size=%d, how=%s, index=%d, token='%s'", size, (how==0?"Convert":"Subscript"), index, token);

    if(1 == how) 
        return ((index >= 0 && index < size) ? enum_list[index].enum_val : -1);

    for(i=0; i<size; i++)
        if(! strcasecmp(token, enum_list[i].token))
            return enum_list[i].enum_val;
    return -1;
}


static int 
user_input_from_menu(void) {
    int num;

    PRINTF(" *********************************************\n");
    PRINTF(" ENTER THE TEST CASE YOU WOULD LIKE TO EXECUTE\n");
    PRINTF(" *********************************************\n");
    PRINTF(" 1--> H264 144 176 QCIF\n");
    PRINTF(" 2--> H264 240 320 QVGA\n");
    PRINTF(" 3--> H264 368 480 HVGA\n");
    PRINTF(" 4--> H264 480 640 VGA\n");
    PRINTF(" 5--> H264 DYNAMIC PORT RECONFIG\n");
    PRINTF(" 6--> NAL LENGTH SIZE[ NO START CODES] \n");
    PRINTF(" 9--> EXIT\n");
    fflush(stdin);
    scanf("%d", &num);
    fflush(stdin);
    PRINTF("\n Selected: %d\n", num);
    return num;
}

/* Set global height/width values. RETURNS: 0 on success / -1 on error */
static int 
set_height_width(video_display_format_type format, int *h, int *w)
{
    int height, width;
    DPRINT(_HERE, 8, "\nFormat=%d", format);
    if(format == VIDEO_DISPLAY_FORMAT_QCIF) 
        { height = 144 ; width = 176; }
    else if(format == VIDEO_DISPLAY_FORMAT_QVGA) 
        { height = 240 ; width = 320; }
    else if(format == VIDEO_DISPLAY_FORMAT_HVGA) 
        { height = 368 ; width = 480; }
    else if(format == VIDEO_DISPLAY_FORMAT_VGA) 
        { height = 480 ; width = 640; }
    else
    {
        DPRINT(_HERE, 8, "\nInvalid Resolution \n");
        return -1;
    }
    *h = height;
    *w = width;
    return 0;
}


/* Dump all non-empty or currently used OMX buffers. 
NOTE : CURRENTLY USED OMX BUFFERS implies they're currently with codec & hence should not ideally be accessed 
how == 0 : Dump buffers according to value of 'which'
    == 1 : Only output the nr. of non-empty or used buffers (depending on value of 'which')
    == 2 : Dont print anything, just return the nr. of non-empty or used buffers (depending on value of 'which')    
which == 0 : dump all non-empty INPUT buffers 
      == 1: dump all USED INPUT buffers
      == 10 : dump all non-empty OUTPUT buffers
      == 11: dump all USED OUTPUT buffers
      == 99: dump ALL non-empty buffers (input & output)
      == 199: dump ALL USED buffers (input & output)

RETURNS: Total number of non-empty / used buffers
*/
static int
dump_omx_buffers(int how, int which)
{
    int i, in, out, inbuf = 0, outbuf = 0, used;

    in = out = 0;
    how <= 1 && DPRINT(_HERE, 8, "Entered with which=%d, how=%d", which, how);
    // Input buffers
    if(which < 10  || which >= 99) {
        how <= 1 && DPRINT(_HERE, 6, "\nChecking INPUT BUFFERS...");
        for (i=0, in=0, inbuf=1; i<gnTotalInputBufs; i++) 
            if(((0 == which || 99 == which) && pInputBufHdrs[i]->nFilledLen != 0) ||        /* Non-empty input buffers */
               ((1 == which || 199 == which) && (used=get_buffer_status(0, i)) > 0)) {      /* Used input buffers */
                ++in;
                if(how == 0) {
                    DPRINT(_NOHERE, 6, "\n###################### P A C K E T   H E X   D U M P ####################");
                    DPRINT(_NOHERE, 6, "\n## Buffer #%d, USED=%s, Len=%d, Offset=%d, ptrHdr=0x%x, ptrBuff=0x%x ##", i, used ? "YES" : "NO", 
                            pInputBufHdrs[i]->nFilledLen, pInputBufHdrs[i]->nOffset, pInputBufHdrs[i], pInputBufHdrs[i]->pBuffer);
                    DPRINT(_NOHERE, 6, "\n######################## (FIRST %06d bytes) ###########################", BUFFER_DUMP_BYTES);
                    hex_dump_packet(gtMyStdout, 0, pInputBufHdrs[i]->pBuffer, BUFFER_DUMP_BYTES, 30);
                }
            }
    }   /* if(0 == which... */

    // Output buffers
    if(which >= 10) {
        how <= 1 && DPRINT(_HERE, 6, "\nChecking OUTPUT BUFFERS");
        for (i=0, out=0, outbuf=1; i<gnTotalOutputBufs; i++) 
            if(((10 == which || 99 == which) && pOutYUVBufHdrs[i]->nFilledLen != 0) ||      /* Non-empty output buffers */
               ((11 == which || 199 == which) && (used=get_buffer_status(1, i)) > 0)) {     /* Used output buffers */
                ++out;
                if(how == 0) {
                    DPRINT(_NOHERE, 6, "\n###################### P A C K E T   H E X   D U M P ####################");
                    DPRINT(_NOHERE, 6, "\n## Buffer #%d, USED=%s, Len=%d, Offset=%d, ptrHdr=0x%x, ptrBuff=0x%x ##", i, used ? "YES" : "NO", 
                            pOutYUVBufHdrs[i]->nFilledLen, pOutYUVBufHdrs[i]->nOffset, pOutYUVBufHdrs[i], pOutYUVBufHdrs[i]->pBuffer);
                    DPRINT(_NOHERE, 6, "\n######################## (FIRST %06d bytes) ###########################", BUFFER_DUMP_BYTES);
                    hex_dump_packet(gtMyStdout, 0, pOutYUVBufHdrs[i]->pBuffer, BUFFER_DUMP_BYTES, 30);
                }
            }
    }   /* if(0 == which... */

/* 08/11/09 - TODO: TEMP QUICK FIX (this section causes abort on Android) */
    if(how <= 1) {
        DPRINT(_NOHERE, 6, "\n#########################################################################");
        if(1 == which || 11 == which || 199 == which) 
            DPRINT(_NOHERE, 6, "\n########## USED INPUT buffers=%d, USED OUTPUT buffers=%d ##########", (inbuf ? in : -1), (outbuf ? out : -1));
        else 
            DPRINT(_NOHERE, 6, "\n########## NON-EMPTY INPUT buffers=%d, NON-EMPTY OUTPUT buffers=%d ##########", (inbuf ? in : -1), (outbuf ? out : -1));

#ifndef _ANDROID_
        print_used_buffer_list(which);
#endif
        DPRINT(_NOHERE, 6, "\n#########################################################################");
        DPRINT(_HERE, 8, "\nLeaving with in=%d, out=%d", in, out);
    }
    DPRINT(_HERE, 10, "Leaving...");
    return in+out;
}


/* Wait until all buffers are returned (ie. all buffers are marked as unused)
howlong: Max time to wait (given in microseconds) for each set of buffers (input / output)
mutex : IF non-NULL then get_buffer_status() calls are mutex protected
which == 0 : wait for all INPUT buffers 
      == 1 : wait for all OUTPUT buffers
      == 99: wait for ALL buffers
RETURNS: 0 if all buffers were returned
        -1 if all input buffers weren't returned
        -2 if all output buffers weren't returned
        -3 if both input & output failed
*/
static int 
wait_for_all_buffers(unsigned long howlong, int which, pthread_mutex_t *mutex)
{
    int i, ret = 0;
    unsigned long slept;

    DPRINT(_HERE, 6, "\nEntered with howlong=%lu, which=%d", howlong, which);
    slept = 0;
    if(0 == which  || 99 == which) {
        DPRINT(_HERE, 8, "\nWAITING for INPUT buffers");
        do {
            if(mutex) LOCK_MUTEX(mutex);
            for (i=0; i<gnTotalInputBufs; i++) 
                if(get_buffer_status(INBUF, i) >= 0) 
                    break; 
            if(mutex) UNLOCK_MUTEX(mutex);

            slept += 100000;
            if(slept > howlong) 
                break;
            usleep(100000);
        } while(i != gnTotalInputBufs);
        (i != gnTotalInputBufs) ? (ret = ret - 1) : 0;
        DPRINT(_HERE, 6, "\nDONE waiting for INPUT buffers...Result=%s",(ret==0) ? "SUCCESS" : "FAILED");
    }   /* if(0 == which... */

    slept = 0;
    if(1 == which  || 99 == which) {
        DPRINT(_HERE, 8, "\nWAITING for OUTPUT buffers");
        do {
            if(mutex) LOCK_MUTEX(mutex);
            for (i=0; i<gnTotalOutputBufs; i++) 
                if(get_buffer_status(OUTBUF, i) >= 0)
                    break; 
            if(mutex) UNLOCK_MUTEX(mutex);

            slept += 100000;
            if(slept > howlong)
                break;
            usleep(100000);
        } while(i != gnTotalOutputBufs);
        (i != gnTotalOutputBufs) ? (ret = ret - 2) : 0;

        DPRINT(_HERE, 6, "\nDONE waiting for OUTPUT buffers...Result=%s",(i == gnTotalOutputBufs) ? "SUCCESS" : "FAILED");

    } /* if(1 == which... */
    DPRINT(_HERE, 6, "\nLeaving with ret=%d", ret);
    return ret;
}

static void 
set_read_buffer_func(void) 
{
    if(file_type_option == FILE_TYPE_DAT_PER_AU) {
        Read_Buffer_Func = Read_Buffer_From_DAT_File;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_DAT_File()");
    }
    else if(file_type_option == FILE_TYPE_ARBITRARY_BYTES) {
        Read_Buffer_Func = Read_Buffer_ArbitraryBytes;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_ArbitraryBytes()");
    }
    else if(codec_format_option == CODEC_FORMAT_H264) {
        Read_Buffer_Func = Read_Buffer_From_Size_Nal;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_Size_Nal()");
    }
    else if((codec_format_option == CODEC_FORMAT_H263) ||
            (codec_format_option == CODEC_FORMAT_MP4) ||
            (codec_format_option == CODEC_FORMAT_SPARK0) ||
            (codec_format_option == CODEC_FORMAT_SPARK1) ||
	    (file_type_option == FILE_TYPE_DIVX_4_5_6)) {
        Read_Buffer_Func = Read_Buffer_From_Vop_Start_Code_File;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_Vop_Start_Code_File()");
    }
    else if(file_type_option == FILE_TYPE_RCV) {
        Read_Buffer_Func = Read_Buffer_From_RCV_File;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_RCV_File()");
    }
    else if(file_type_option == FILE_TYPE_VC1) {
        Read_Buffer_Func = Read_Buffer_From_VC1_File;
        DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_VC1_File()");
    }
    else if (file_type_option == FILE_TYPE_DIVX_311) {
        Read_Buffer_Func = Read_Buffer_From_FrameSize_File;
	DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_FrameSize_File()");
    }
    else if (file_type_option == FILE_TYPE_VP_6 ) {
        Read_Buffer_Func = Read_Buffer_From_FrameSize_File;
	DPRINT(_HERE, 6, "\nRead_Buffer SET to Read_Buffer_From_FrameSize_File()");
    }	

    else {
        DPRINT(_HERE, 0, "\nERROR: Read_Buffer_Func COULD NOT BE SET...Aborting");
        ERROR_ACTION(0, 222);
    }
}

static void set_frame_type_func(void) {
    DPRINT(_HERE, 6, "\nEntered");
    if(CODEC_FORMAT_H264 == codec_format_option) {
        get_frame_type = h264_get_NAL_unit_type;
        gnIFrameType = 5;
        DPRINT(_HERE, 6, "\nAPP : get_frame_type SET to h264_get_NAL_unit_type(), gnIFrameType=%d !", gnIFrameType);
    }
    else if(CODEC_FORMAT_VC1 == codec_format_option) {
        get_frame_type = vc1_get_frame_type;
        gnIFrameType = 1;
        DPRINT(_HERE, 6, "\nAPP : get_frame_type SET to vc1_get_frame_type(), gnIFrameType=%d !", gnIFrameType);
    }
    else {
        get_frame_type = mpeg4_get_VOP_coding_type;
        gnIFrameType = 0;
        DPRINT(_HERE, 6, "\nAPP : get_frame_type SET to mpeg4_get_VOP_coding_type(), gnIFrameType=%d !", gnIFrameType);
    }
    DPRINT(_HERE, 8, "\nLeaving");
}

/* Check if specified option 'opt' was passed on a cmdline arg of this app ('str') 
   The first word after app name is a list of options to the app starting with a hyphen 
   Eg. ./ast-mm-vdec-test -ASP qcif input output 4 
RETURNS: 1 if option found in string 
         0 otherwise */
static int
cmp_option(const char *str, char opt)
{
    char *s;

    if(!str || ! *str) 
        return 0;

    s = (char *)strchr(str, (int)'-');
    if(s)   
        return (strchr(s+1, (int)opt) ? 1 : 0); 
    else    
        return 0;
}

/* Waits until *varptr equals the expectedval (integers only) 
IF 'mutex' is non-NULL then the varptr read is protected by this mutex
RETURNS: 1 on success OR
         0 on failure */
static int
timed_wait_for_val(const volatile unsigned long *varptr, unsigned long expectedval, unsigned int waitsecs, pthread_mutex_t *mutex) 
{
    unsigned int i;
    unsigned int sleepusecs = 100000;
    unsigned int num = waitsecs*1000000/sleepusecs;
    unsigned long currval;

    (0 == num) ? ++num : 0;
    for(i=0; i < num ; i++) {
        if(mutex) LOCK_MUTEX(mutex);
            currval = *varptr;
        if(mutex) UNLOCK_MUTEX(mutex);
        if(currval == expectedval)
            return 1;
        else
            usleep(sleepusecs);
    }
    return 0;
}

static void
print_usage(char *appname)
{
    PRINTF("\n###################################################################################################################");
    PRINTF("\n### AST VIDEO DECODER TEST ###");
    PRINTF("\n--- Usage I --- (Menu based): \n%s <input file>", appname);
    PRINTF("\n--- Usage II --- (Automation): \n%s -A<options> <resolution> <infile> <outfile> <nal_len / 'mpeg4' / 'h263' / 'vc1' / 'rcv' 'divx' > <extended options>", appname);
    PRINTF("\nEg. %s -AS qcif 50_dates_input.264 out.yuv 0 arbitrary    ==> H264 StartCode based file (arbitrary bytes read)", appname);
    PRINTF("\nEg. %s -AS qcif 50_dates_input.264 out.yuv 4 arbitrary	==> H264 NAL length file (arbitrary bytes read)", appname);
    PRINTF("\nEg. %s -AS qcif VC1_Files/butte3-wvn128q15.rcv out.yuv rcv ==> RCV file", appname);
    PRINTF("\n--- OPTIONS ---");
    PRINTF("\nA: Automation mode");
    PRINTF("\nS: Single session (exit after single decode session)");
    PRINTF("\nW: Start in suspended mode (Can be used to change settings before start of decode). Send PLAY cmd to begin dec");
    PRINTF("\nR: Profile mode (match framerate 'gnProfileFPS' or use a constant delay 'gnProfileSleepUsecs' for each frame");
    PRINTF("\nP: Performance mode on (minimal logging to a logfile=%s", gsPerfLogFilename);
    PRINTF("\np: Log everything to logfile=%s (used if OMX libs have disabled print messages)", gsPerfLogFilename);
    PRINTF("\nD: Debug Mode (track low frequency mutex locks/unlocks also)");
    PRINTF("\nd: Debug Mode hi freq (track ALL mutex locks/unlocks)");
    PRINTF("\nB: Dump Input Buffers during decode");
    PRINTF("\nb: Toggle FramePackingArbitrary (default=%s) EXCEPT for RCV(deault=ALWAYS-OFF for FrameByFrame)", gbFramePackArbitrary ? "ON" : "OFF");
    PRINTF("\nF: FILE WRITE=ON / DISPLAY=OFF (default: file write=off / display=on)");
    PRINTF("\nf: FILE WRITE=ON / DISPLAY=ON  (default: file write=off / display=on)");
    PRINTF("\nM: MIN LOGGING  (LOGLEVEL = 6)");
    PRINTF("\nm: NO LOGGING  (LOGLEVEL = 0)");
    PRINTF("\n[1-4]: Display Window Selection (default = 0 = FullScreen)");
    PRINTF("\n--- EXTENDED OPTIONS ---");
    PRINTF("\n    'arbitrary' : Read arbitrary amounts of input into buffers during decode");
    PRINTF("\n###################################################################################################################");
}

int 
main(int argc, char **argv) 
{
    int i; int bufCnt; int num; int exitAfterTest = false; int tempWatchdogInterval = 1;
    struct timeval flushStartTime, flushEndTime;
    unsigned long keepAliveTick = 0;
    pthread_t listener_tid;
    int result;
    int unitType;
    char *pipename = "pVdec";
    sigset_t threadsigset;

    /* 05/11/09 
    sigfillset(&threadsigset);
    if(pthread_sigmask(SIG_BLOCK, &threadsigset, NULL)) {
        DPRINT(_HERE, 2, "ERROR: pthread_sigmask() failed. Couldn't block signals !");
    } */

    gtMyStdout = stdout;
    pthread_cond_init(&cond, 0);
    pthread_mutex_init(&lock, 0);


    /* Install the signal handler for SIGINT signal */ 
    install_sighandler();

    /*  
      Usage 1 : <app_name> <input file>     ==> Menu based interactive mode
      Usage 2 : <app_name> -A<s> <resolution eg. "QCIF">  <optional input file>  <optional output file>  <optnal NAL_LEN_SIZE> ==> Automation mode 
                -AS will run just a single test in automation mode */
    i = bufCnt = num = 0;
    if(2 >= argc && strstr(argv[1], "--help")) {
        print_usage(argv[0]);
        exit(0);
    }
    if( 2 == argc && 1 != cmp_option(argv[1], 'A')) {      // Menu based (Usage I) - TODO expand menu later on (Kirk's request)
        snprintf(gsInputFilename, MAXSIZE_FName+1, "%s", argv[1]);
        LABEL_MENU:
        do {
            num = user_input_from_menu();
            if(9 == num) 
                exit(0);
        } while(num < 1 || num > 6);

        curr_test_num = num;
        if(num >= 5) {      // Call menu-based ASW code (TODO: sync to latest)
            if(num == 6) {
                file_type_option = FILE_TYPE_264_NAL_SIZE_LENGTH;
                PRINTF("\nEnter Nal length size[ 2 or 4] \n");
                scanf("%d", &nalSize);
            }
            geFormat = VIDEO_DISPLAY_FORMAT_QCIF;
            height=144;width=176; // Assume Default as QCIF
            PRINTF("\nExecuting DynPortReconfig QCIF 144 x 176 \n");
            PRINTF("\nInput values: curr_test_num=%d , inputfilename[%s]\n", curr_test_num, gsInputFilename);
            PRINTF("\n*******************************************************\n");	

            run_tests(geFormat);
            return 0;
        } /* if(num >= 5...*/

        /* Regular formats - play accordingly */
        geFormat = (video_display_format_type)token_2_enum(1, NULL, num-1);
        PRINTF("\nnum=%d, Set geFormat=%d", num, geFormat);
        if(set_height_width(geFormat, &height, &width) < 0)
            return -1;
        gbRepeatMenu = true;
    }

    if(argc >=2 && 1 == cmp_option(argv[1], 'S')) {
        exitAfterTest = true;
        PRINTF("\nAPP SETTING : SINGLE TEST MODE...");
    }
    /* Automation mode (Usage II) TODO: Cleanup/change all this to support the same things thru both menu and LASIC (including port reconfig etc) */
    if(argc >= 4 && set_height_width((geFormat = (video_display_format_type)token_2_enum(0, argv[3], 0)), &height, &width) == 0) {
        pipename = argv[2];
        (argc >= 5) && snprintf(gsInputFilename, MAXSIZE_FName+1, "%s", argv[4]);
        (argc >= 6) && snprintf(gsOutputFilename, MAXSIZE_FName+1, "%s", argv[5]);
        (argc >= 7) && (strlen(argv[6]) == 1) 
            && (nalSize = atoi(argv[6]), file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_264_NAL_SIZE_LENGTH), codec_format_option = CODEC_FORMAT_H264);
        (argc >= 7) && !strcasecmp(argv[6], "mpeg4") 
            && (file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_PICTURE_START_CODE), codec_format_option = CODEC_FORMAT_MP4);
        (argc >= 7) && !strcasecmp(argv[6], "h263") 
            &&  (file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_PICTURE_START_CODE), codec_format_option = CODEC_FORMAT_H263);
        (argc >= 7) && !strcasecmp(argv[6], "vc1") && (codec_format_option = CODEC_FORMAT_VC1, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_VC1));
        (argc >= 7) && !strcasecmp(argv[6], "rcv") && (codec_format_option = CODEC_FORMAT_VC1, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_RCV));
        (argc >= 7) && !strcasecmp(argv[6], "divx") && (codec_format_option = CODEC_FORMAT_DIVX, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_DIVX_4_5_6));
        (argc >= 7) && !strcasecmp(argv[6], "vp") && (codec_format_option = CODEC_FORMAT_VP, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_VP_6));
        (argc >= 7) && !strcasecmp(argv[6], "spark0") && (codec_format_option = CODEC_FORMAT_SPARK0, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_PICTURE_START_CODE));
        (argc >= 7) && !strcasecmp(argv[6], "spark1") && (codec_format_option = CODEC_FORMAT_SPARK1, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_PICTURE_START_CODE));
        (argc >= 7) && !strcasecmp(argv[6], "div3") && (codec_format_option = CODEC_FORMAT_DIVX, file_type_option = (file_type)(argc >= 8 ? token_2_enum(0, argv[7], 0) : FILE_TYPE_DIVX_311));
        PRINTF("**************************************************\n");	
        PRINTF("USING VALUES: height[%d] width[%d] inputfilename['%s'] outputfilename['%s'] codec_format[%d] file_type[%d] NAL_len[%d]\n",
                              height, width, gsInputFilename, gsOutputFilename, codec_format_option, file_type_option, nalSize);
        PRINTF("**************************************************\n");	
        if(cmp_option(argv[1], 'P')) {
            PRINTF("\nAPP SETTING : PERFORMANCE MODE ON");
            num = 99;
        }
        else if(cmp_option(argv[1], 'p')) {
            PRINTF("\nAPP SETTING : LOGFILE ON");
            num = 88;
        }

        if(cmp_option(argv[1], 'D'))
               num = 77, i = 11;
        else if(cmp_option(argv[1], 'd'))
               num = 77, i = 12;

        if(cmp_option(argv[1], 'M'))
               num = 100, i = 6;
        else if(cmp_option(argv[1], 'm'))
               num = 100, i = 0;

        if(cmp_option(argv[1], 'R')) {
            PRINTF("\nAPP SETTING : PROFILING MODE ON");
            gbProfileMode = 1;      
        }

        if(cmp_option(argv[1], 'b')) {
            gbFramePackArbitrary = ! gbFramePackArbitrary;      
        }

        if(cmp_option(argv[1], 'B')) {
            gbDumpInputBuffer = true;
            PRINTF("\nAPP SETTING : INPUT BUFFER DUMP: ON");
        }

        if(cmp_option(argv[1], 'w')) {
            gbWaitFirstFrameDec = true;
            PRINTF("\nAPP SETTING : WAIT FIRST FRAME DECODE: ON");
        }

        if(cmp_option(argv[1], 'F')) {
            file_write = true; fb_display = false; 
            PRINTF("\nAPP SETTING : FILE WRITE=ON / DISPLAY=OFF");
        }

        if(cmp_option(argv[1], 'f')) {
            file_write = true; fb_display = true; 
            PRINTF("\nAPP SETTING : FILE WRITE=ON / DISPLAY=ON");
        }

        if(cmp_option(argv[1], '1')) {
            gnDisplayWindow = 1;
            PRINTF("\nAPP SETTING : DISPLAY WINDOW : 1");
        }
        else if(cmp_option(argv[1], '2')) {
            gnDisplayWindow = 2;
            PRINTF("\nAPP SETTING : DISPLAY WINDOW : 2");
        }
        else if(cmp_option(argv[1], '3')) {
            gnDisplayWindow = 3;
            PRINTF("\nAPP SETTING : DISPLAY WINDOW : 3");
        }
        else if(cmp_option(argv[1], '4')) {
            gnDisplayWindow = 4;
            PRINTF("\nAPP SETTING : DISPLAY WINDOW : 4");
        }

        /* RCV files cannot be read frame by frame when using arbitrary frame packing (since arbitrary requires ALL frame/seq layer data) */
        if(FILE_TYPE_RCV == file_type_option && gbFramePackArbitrary)
            gbFramePackArbitrary = false;
        if(gbFramePackArbitrary) 
            PRINTF("\nAPP SETTING : Forced Arbitrary Frame packing : ON (all input read methods, even NAL)");
        else 
            PRINTF("\nAPP SETTING : Forced Arbitrary Frame packing : OFF");
    }
    else {
        print_usage(argv[0]);
        PRINTF("\n\nAPP : NOTHING TO DO....\n");
    }

    reset_all_event_data();
    setup_logging(num, i);
    set_read_buffer_func();
    set_frame_type_func();
    if(codec_format_option == CODEC_FORMAT_H264) {
        TEST_UTILS_H264_Init();      // Interface with C++ utils code
        DPRINT(_HERE, 6, "TEST_UTILS_H264_Init() Done... ");
    }

	DPRINT(_HERE, 8, "\****************Named pipe is %s**************************\n", pipename);
    /* Listen for automation commands */
    if(LASIC_Start_Listener(pipename, handle_lasic_cmd, &listener_tid) != 0) {
        DPRINT(_HERE, 8, "\nCouldn't start listener thread ! Aborting...");
         exit(1);
    }

    // This implies the user passed cmdline args
    if(geFormat != VIDEO_DISPLAY_FORMAT_MAX) {
        if(1 != cmp_option(argv[1], 'W')) {
            DPRINT(_HERE, 6, "\nWAITING for any initial settings...");        // DELETE
            sleep(1);   // NEEDED to allow processing of any LASIC commands piped before or at app start (eg. to change settings like FB_DISPLAY before init)
            DPRINT(_HERE, 6, "\nDONE WAITING for initial settings");        
            appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, LASIC_CMD_PLAY, 0, false);  // This is non-blocking to prevent deadlock (main is the only consumer of the AppQ)
        }
        else
            DPRINT(_HERE, 6, "\nSTARTED IN SUSPENDED STATE (to begin, type 'echo \"PLAY\" > %s')", pipename);
    }
    else if(exitAfterTest) 
        exit(1);

    /* App-event-process loop of main thread */
    while(1)
    {
        appQ_event_data_t *eventinfo;
        OMX_BUFFERHEADERTYPE *pBuff, *iFrame;
        wait_omx_event_info_t recvEvtData;
        int i, j, locked; 
        char tempstr[15];
        unsigned int waitcnt=0, skippedIFrameCnt=0;

        eventinfo = appQ_get_event(true);  // Block until some activity
        DPRINT(_HERE, 8, "\nmain(): SWITCH BLOCK BEGIN : APP_EVENT_COUNT=%d", keepAliveTick);
        switch(eventinfo->event) {
        case APP_EVENT_CMD_CHANGE_STATE:
            if(bInputEosReached || bOutputEosReached) {
                DPRINT(_HERE, 6, "\nmain(): RECEIVED APP_CHANGE_STATE after EOS [Cmd=%d / AppState=%d ; EOS_IN=%d / EOS_OUT=%d]...IGNORED", 
                       eventinfo->data, geAppState, bInputEosReached, bOutputEosReached);
                break;
            }
            switch(eventinfo->data) {    // Check the new app state 
            case LASIC_CMD_PLAY: {
                DPRINT(_HERE, 8, "\nmain(): RECEIVED APP_CHANGE_STATE with Cmd: LASIC_CMD_PLAY, AppState=%d", geAppState);
                switch(geAppState) {    // Check current app state
                case APP_STATE_Meditating:  
                    APP_CHANGE_STATE(APP_STATE_Walking);
                    watchdog_switch(true);   // Turn on watchdog
                    DPRINT(_HERE, 6, "\nAPP_STATE_PAUSE (BEFORE any processing for resume): ETB_Cnt=%d, InputUnitCnt=%d, fbd_cnt=%d, OutputUnitCnt=%d\n", gnEtbCnt, gnInputUnitCnt, fbd_cnt, gnOutputUnitCnt);
                    DPRINT(_HERE, 6, "\nLASIC_CMD_PLAY : APP_STATE_PAUSE : OMX_SendCommand Decoder [Paused->Executing]\n");
                    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
                    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
                        ERROR_ACTION(0, 61);
                    CHECK_OMX_STATE(OMX_StateExecuting); DPRINT(_HERE, 6, "DONE: PAUSED->EXECUTING");
                    if(false == gbForceFeedBuffsOnResume) {
                        int usedin, usedout;
                        LOCK_MUTEX(&gtGlobalVarMutex);
                            usedin = dump_omx_buffers(1, USEDINBUF);
                            usedout = dump_omx_buffers(1, USEDOUTBUF);
                        UNLOCK_MUTEX(&gtGlobalVarMutex);
                        /* If buffer collection was ON during OMX_Exec->Paused / OMX_Paused then codec may not be holding any buffers now. 
                        Hence on resume play (OMX_Paused->Exec), buffers should be refed to codec */
                        if(0 == usedin || 0 == usedout || gbCollectBuffsInPaused) {
                            DPRINT(_HERE, 6, "(DONE: PAUSED->EXECUTING) INbufsUsed=%d, OUTbufsUsed=%d, ForceFeedBuffsOnResume=%s, CollectBuffsInPaused=%s ...REFEEDING by calling fill/empty buffers...", 
                                   usedin, usedout, (gbForceFeedBuffsOnResume ? "ON" : "OFF"), (gbCollectBuffsInPaused ? "ON" : "OFF"));
                            /* Falls-thru to case APP_State_Walking below... */
                        }
                        /* If buffer collection was OFF during OMX_Exec->Paused / OMX_Paused then the app would have refed all in/out buffers 
                        during EBD/FBD processing in Paused state & hence codec should be holding buffers now. 
                        Hence on resume play (OMX_Paused->Exec), the codec should continue giving EBD/FBDs */
                        else {
                            DPRINT(_HERE, 6, "(DONE: PAUSED->EXECUTING) INbufsUsed=%d, OUTbufsUsed=%d, ForceFeedBuffsOnResume=%s, CollectBuffsInPaused=%s ...WAITING for EBDs/FBDs...", 
                                   usedin, usedout, (gbForceFeedBuffsOnResume ? "ON" : "OFF"), (gbCollectBuffsInPaused ? "ON" : "OFF"));
                            break;
                        }
                    }
                    else {
                        DPRINT(_HERE, 6, "(DONE: PAUSED->EXECUTING) gbForceFeedBuffsOnResume=%d, FORCE BUFFER REFEED using Exec->Idle->Exec...", gbForceFeedBuffsOnResume);
                        APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
                        CHANGE_OMX_STATE(OMX_StateIdle);
                        CHANGE_OMX_STATE(OMX_StateExecuting);
                    }
                    /* Fall-thru is intentional */
                case APP_STATE_Walking: 
                case APP_STATE_SprintingBackwards:
                case APP_STATE_Sprinting:
#ifndef USE_INPUT_FILE_MUTEX
                    /* NOTE : Since we're not using mutex sync between app main thread & EBD callback (to avoid performance decay 'cos every EBD
                    will have to take mutex before input file read), when a play cmd is given while EBD/FBD callbacks maybe going on, it's best
                    to wait for a fraction of a sec for buffers to be returned & then call fill_buffers() or read_and_empty_buffers() */
                    APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
                    if((j = wait_for_all_buffers(500000ul, 99, &gtGlobalVarMutex)) != 0) {
                        DPRINT(_HERE, 0, "WARNING: LASIC_CMD_PLAY(was already decoding): Didn't get back all buffers(ret=%d)...", j);
                    }
#endif
                    if(APP_STATE_Walking != geAppState)    
                        APP_CHANGE_STATE(APP_STATE_Walking);
                    fill_buffers(gnTotalOutputBufs, 1, 1);    
                    read_and_empty_buffers(gnTotalInputBufs, 0, 2, 100000);
                    break;

                case APP_STATE_Foetus:  // App is dormant & play command given, so start everything
                    DPRINT(_HERE, 8, "\nmain() LASIC_CMD_PLAY : STARTING from APP_STATE_IDLE...\n");
                    pthread_cond_init(&cond, 0);
                    pthread_mutex_init(&lock, 0);
                    if(gbWaitFirstFrameDec) {
                        APP_CHANGE_STATE(APP_STATE_WaitFirstFrame);      // 05/21/09
                    }
                    else {
                        APP_CHANGE_STATE(APP_STATE_Walking);                // 02/04/09
                    }

                    gnEBDLatency = gnFBDLatency = 0;
                    (void)gettimeofday(&gtInitStartTimeVal, NULL);
                    if(geFormat != VIDEO_DISPLAY_FORMAT_MAX) {
                        pthread_t tid;

                        if(Init_Decoder() != 0x00)
                        {
                            DPRINT(_HERE, 8, "Decoder Init failed\n");
                            return -1;
                        }
                        DPRINT(_HERE, 8, "\nBEFORE PLAY: DUMPING GLOBALS...");
                        dump_global_data();
                        /* 05/21/09 : Moved this to be called BEFORE Play_Decoder() */
                        DPRINT(_HERE, 8, "\nAfter Play_Decoder()... STARTING WATCHDOG THREAD...");
                        watchdog_switch(true);   // Turn on watchdog
                        if(pthread_create(&tid, NULL, watchdog_thread, NULL) != 0) {
                            DPRINT(_HERE, 0, "ERROR: Watchdog thread create failed !");
                            ERROR_ACTION(3, 205);
                        }
                        result = Play_Decoder(geFormat);
                        if(result != 0x00)
                        {
                            DPRINT(_HERE, 8, "Play_Decoder() failed\n");
                            return -1;
                        }
                    } /* if(geFormat != ... */
                    else
                        DPRINT(_HERE, 6, "\nERROR: Invalid format found...set correct format first");

                    break;  /* case APP_STATE_Foetus... */

                default:
                    DPRINT(_HERE, 6, "\nERROR: (on LASIC_CMD_PLAY) DEFAULT case reached while checking app state [Cmd=%d / AppState=%d] ", 
                           eventinfo->data, geAppState);
                    ERROR_ACTION(0, 222);
                    break;
                } /* switch(geAppState)... */
                }
                break;  /* case LASIC_CMD_PLAY... */

            case LASIC_CMD_FFWD:
                /* Algorithm : Flush decoder if needed, search for input I-frame skipping the required nr. of I-frames, send this I-frame for 
                decode along with required nr. of subsequent frames. Then after the corresponding nr. of FillBufferDone events are recvd, the 
                FillBufferDone() callback will trigger this sequence again (by sending LASIC_CMD_FFWD command to app thread) */
                DPRINT(_HERE, 8, "\nmain(): RECEIVED APP_CHANGE_STATE with Cmd: LASIC_CMD_FFWD, AppState=%d", geAppState);
                if( bInputEosReached || bOutputEosReached
                    || (APP_STATE_Walking != geAppState && APP_STATE_Sprinting != geAppState && APP_STATE_Meditating != geAppState) ) {
                    DPRINT(_HERE, 6, "\nmain(): RECEIVED LASIC_CMD_FFWD in invalid state [Cmd=%d / AppState=%d ; EOS_IN=%d / EOS_OUT=%d]...IGNORED", 
                           eventinfo->data, geAppState, bInputEosReached, bOutputEosReached);
                    break;
                }
                /* Set app state to flushwait to avoid race conditions with input file read in EmptyBufferDone which may be happening concurrently
                to this thread if FlushBeforeISeek is OFF */
                APP_CHANGE_STATE(APP_STATE_WaitOmxFlush);    // 03/18/09
                if(gbFfwd_FlushBeforeISeek) {
                    OMX_STATETYPE omx_state;
                    ++gnFlushCount;
                    DPRINT(_HERE, 8, "\nLASIC_CMD_FFWD: OMX_SendCommand Decoder -> FLUSH ALL [%d]\n", gnFlushCount);
                    (void)gettimeofday(&flushStartTime, NULL);
                    reset_all_event_data(); OMX_SendCommand(avc_dec_handle, OMX_CommandFlush, OMX_ALL, 0);      
                    DPRINT(_HERE, 8, "\nLASIC_CMD_FFWD: FLUSH ALL [%d]: waiting for flush complete...\n", gnFlushCount);
                    // Only flush of ALL ports supported so wait for 2 event completes (since 2 ports on this component)
                    expect_some_event(OMX_EventCmdComplete, OMX_CommandFlush, 0, &recvEvtData);    // Wait until flush completes
                    DPRINT(_HERE, 8, "\nLASIC_CMD_FFWD: OMX_SendCommand Decoder : FLUSH ALL [%d] COMPLETE on PORT [%d]\n", gnFlushCount, recvEvtData.nData2);
                    expect_some_event(OMX_EventCmdComplete, OMX_CommandFlush, 0, &recvEvtData);    // Wait until flush completes
                    DPRINT(_HERE, 8, "\nLASIC_CMD_FFWD: OMX_SendCommand Decoder : FLUSH ALL [%d] COMPLETE on PORT [%d]\n", gnFlushCount, recvEvtData.nData2);
                    (void)gettimeofday(&flushEndTime, NULL);
                    gnFlushLatency += diff_timeofday(&flushEndTime, &flushStartTime);
                    get_omx_state(&omx_state);
                    CHECK_BUFFERS_RETURN(1000ul, 99, true, 1, &gtGlobalVarMutex);    // 05/19/09
                    if(OMX_StateExecuting != omx_state) 
                        CHANGE_OMX_STATE(OMX_StateExecuting);

                    APP_CHANGE_STATE(APP_STATE_Sprinting);   
                    watchdog_switch(true);   // Turn on watchdog
                    /* This shud be done BEFORE any input frames are searched to ensure there's atleast one outbuf available if EOS input was found 
                    & fake EOS input buffer was sent to decoder */
                    fill_buffers(gnTotalOutputBufs, 1, 1);  
                }

                /* At this point all input buffers should be free (if flush was done), so get next I-frame after skipping reqd. nr. of I-frames */
                ATOMIC_DO_GLOBAL(gnFfwd_UnitsDecoded = 0);  
                pBuff = NULL;
                for(i=0; i <= gnFfwd_IFrameSkipCnt; i++) {
                    locked = true; LOCK_MUTEX(&gtGlobalVarMutex);
                    /* If Flush before I-frame seek is disabled then there may not be free input buffs available so wait till one is avlbl */
                    for(j=0; NULL == pBuff && NULL == (pBuff = get_free_omx_inbuffer(pInputBufHdrs, gnTotalInputBufs)); j++) {
                        if(gbFfwd_FlushBeforeISeek) {
                            locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);
                            DPRINT(_HERE, 0, "\nERROR: FFWD: No free input buffers after flush");
                            ERROR_ACTION(1, 65);
                            goto BAIL_FFWD;
                        }
                        else if(j == 200) {
                            locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);
                            DPRINT(_HERE, 0, "\nERROR: FFWD: Timed out waiting for free input buffers[Tries=%d]", i);
                            ERROR_ACTION(3, 211);
                            goto BAIL_FFWD;
                        } 
                        else {
                            locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);
                            DPRINT(_HERE, 1, "\nWARNING: FFWD: NO FREE INPUT BUFFERS to read data for I-frame search. WAITING[%d x 10k usecs done]...", i);
                            usleep(10000);
                            // When the next iteration happens, mutex should be locked
                            locked = true; LOCK_MUTEX(&gtGlobalVarMutex);  
                        }
                    } /* for(j=0; ...*/
                    // Mutex should be locked if control reaches here
                    update_used_buffer_list(INBUF, USED, pBuff);     // Mark buffer as used
                    locked = false; UNLOCK_MUTEX(&gtGlobalVarMutex);

                    iFrame = get_next_iframe(pBuff); // 03/18/09
                    // Set app state here & not earlier to avoid race conditions with input file read in EmptyBufferDone() etc
                    if(0 == gbFfwd_FlushBeforeISeek) {
                        APP_CHANGE_STATE(APP_STATE_Sprinting);   
                        fill_buffers(gnTotalOutputBufs, 1, 1);  
                    }
                    if(NULL == iFrame) {
                        DPRINT(_HERE, 6, "LASIC_CMD_FFWD [FlushCnt=%d]: EOS Input found while seeking IFrame [InUnits=%d / OutUnits=%d / IFrameCnt=%d]...", gnFlushCount, gnInputUnitCnt, gnOutputUnitCnt, gnIFrameCount);
                        // (03/19/09) Empty a single buffer with EOS flag set & send APP_EVENT_EOS_INPUT
                        process_inputread_eos(pBuff, true);
                        break;
                    }
                    else if(i != gnFfwd_IFrameSkipCnt) {
                        // Reset buffer
                        pBuff->nFilledLen = pBuff->nFlags = pBuff->nOffset = 0;
                        DPRINT(_HERE, 8, "\nFFWD: SKIPPING I-Frame #%d [NAL Unit#%d], I-frame skip count=%d\n", gnIFrameCount, gnNALUnitCnt, ++skippedIFrameCnt);
                    }
                } /* for(i=0... */

BAIL_FFWD:      if (NULL == pBuff || NULL == iFrame)    // EOS or no-free-buffer found while searching for I-frames above, so do nothing more
                    break;
                /* At this point we have an Iframe so decode it & also send next few frames for decode
                   Only after all of these are decoded will the FillBufferDone() callback again trigger this same FFD sequence */
                DPRINT(_HERE, 8, "\nmain(): LASIC_CMD_FFWD [FlushCnt=%d]: Decoding SEEK_POINT : I-frame #%d / InputUnit #%d [NalUnit #%d]...", gnFlushCount, gnIFrameCount, gnInputUnitCnt, gnNALUnitCnt);

                /* DEBUG: When dumping output I-frames, also dump the corresponding input I-frame */
                if(gbDumpDecodedIframes && outputEncodedIFramesFile) {
                    DPRINT(_HERE, 6, "\nDUMPING I-frame INPUT BUFFER (IFrame#%d, Header=0x%x, Buffer=0x%x, Size=%d)...", 
                                         gnIFrameCount, pBuff, pBuff->pBuffer, pBuff->nFilledLen);
                    fwrite((const char *)pBuff->pBuffer,
                                        1, pBuff->nFilledLen, outputEncodedIFramesFile);
                }
                if(gbFfwd_PauseOnIframeDec) {   /* Frame dump to be done BEFORE buffer is consumed */
                    DPRINT(_HERE, 4, "\nWARNING: Sent seek-point I-FRAME for decode & PAUSED (SET_PARAM h264_dec geAppState=3 to continue)...");
                    DPRINT(_HERE, 6, "\nDUMPING I-frame INPUT BUFFER (Header=0x%x, Buffer=0x%x, Size=%d)...", 
                            pBuff, pBuff->pBuffer, pBuff->nFilledLen);
                    DPRINT(_HERE, 6, "\n#################### P A C K E T   H E X   D U M P ######################");
                    hex_dump_packet(gtMyStdout, 0, pBuff->pBuffer, pBuff->nFilledLen, 30);
                    DPRINT(_HERE, 6, "\n#########################################################################");
                }
                /* END DEBUG */

                /* 04/20/09 : Moved fill_buffers() call to just after flush above */
                pBuff->nFlags |= OMX_BUFFERFLAG_STARTTIME;  // Ref. 3.1.2.7.1 Pg 68 of OMX IL 1.1 spec
                pBuff->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
                if(OMX_ErrorNone == EMPTY_BUFFER(avc_dec_handle, pBuff)) {    // Empty the I-Frame that was found
                    ATOMIC_DO_GLOBAL(++gnInputUnitCnt);
                    unitType = get_frame_type(file_type_option, pBuff->pBuffer + pBuff->nOffset, pBuff->nFilledLen - pBuff->nOffset);
                    // After successful ETB, buffer is with decoder hence do not access any buffer data except header & buffptr
                    DPRINT(_HERE, 8, "\nINFO: EMPTY_BUFFER: DONE EMPTY BUFFER(UnitType=%d, I-Frame#%d) BuffHdr=0x%x, BuffPtr=0x%x\n", 
                           unitType, gnIFrameCount, pBuff, pBuff->pBuffer);
                }
                else {
                    ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pBuff));
                    DPRINT(_HERE, 0, "LASIC_CMD_FFWD: ERROR: EMPTY_BUFFER() for I-frame FAILED !");
                    ERROR_ACTION(1, 102);
                    break;
                }

                /* DEBUG : If relevant flag is set, suspend app when iFrame is found */
                if(gbFfwd_PauseOnIframeDec) {   /* Actual app pause to be done AFTER calling Empty buffer */
                    APP_CHANGE_STATE(APP_STATE_DEBUG_BLOCKED);
                    while(APP_STATE_DEBUG_BLOCKED == geAppState)
                        usleep(100000);
                }
                /* END DEBUG */

                read_and_empty_buffers(gnTotalInputBufs-1, 0, 2, 100000);                 // Read & empty more buffers
                break; /* case LASIC_CMD_FFWD */

            case LASIC_CMD_PAUSE:
                DPRINT(_HERE, 8, "\nmain(): RECEIVED APP_CHANGE_STATE with Cmd: LASIC_CMD_PAUSE, AppState=%d, OutUnits=%d", geAppState, gnOutputUnitCnt);
                switch(geAppState) {    // Check current app state
                case APP_STATE_Walking:
                case APP_STATE_Sprinting:
                case APP_STATE_SprintingBackwards:
                    watchdog_switch(false);   // Turn off watchdog
                    DPRINT(_HERE, 6, "\nLASIC_CMD_PAUSE : OMX_SendCommand Decoder [OMX_Executing->OMX_Paused]\n");
                    if(gbCollectBuffsInPaused) {
                        DPRINT(_HERE, 6, "\nLASIC_CMD_PAUSE : COLLECTING BUFFERS during [OMX_Executing->OMX_Paused]...gbCollectBuffsInPaused=%d\n", gbCollectBuffsInPaused);
                        APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
                    }
                    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StatePause,0); 
                    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
                        ERROR_ACTION(0, 61);
                    /* Make sure app state change is done AFTER OMX_Paused is done (after OMX_Paused, no EBD/FBD callbacks should come) */
                    CHECK_OMX_STATE(OMX_StatePause); DPRINT(_HERE, 6, "DONE: EXECUTING->PAUSED");
                    APP_CHANGE_STATE(APP_STATE_Meditating);
                    DPRINT(_HERE, 6, "\nLASIC_CMD_PAUSE : OMX_SendCommand Decoder -> Pausing (DONE) : OutUnits=%d\n", gnOutputUnitCnt);
                    if(gbMarkBuffsFreeAfterPause) {
                        DPRINT(_HERE, 5, "\nWARNING: LASIC_CMD_PAUSE : Marking ALL BUFFERS as FREE [gbMarkBuffsFreeAfterPause=%d]", gbMarkBuffsFreeAfterPause);
                        ATOMIC_DO_GLOBAL(gnUsedInputBuffsMap = gnUsedOutputBuffsMap = 0);
                    }
                    ATOMIC_DO_GLOBAL(j = dump_omx_buffers(1, 199));
                    DPRINT(_HERE, 6, "\nAPP_STATE_PAUSE (AFTER ALL processing): TotalUsedBuffs=%d, ETB_Cnt=%d, InputUnitCnt=%d, fbd_cnt=%d, OutputUnitCnt=%d\n", j, gnEtbCnt, gnInputUnitCnt, fbd_cnt, gnOutputUnitCnt);
                    break;
                default:
                    break;
                } /* switch(geAppState... */
            break;  /* case LASIC_CMD_PAUSE */

            case LASIC_CMD_RWND:
                DPRINT(_HERE, 8, "\nmain(): RECEIVED APP_CHANGE_STATE with Cmd: LASIC_CMD_RWND, AppState=%d", geAppState);
                switch(geAppState) {
                case APP_STATE_Meditating:
                case APP_STATE_Sprinting:
                case APP_STATE_Walking:
                    DPRINT(_HERE, 8, "\nLASIC_CMD_RWND : RESET file pointer to START... ");
                    /* Input file reset & read should be thread-safe */
#ifdef USE_INPUT_FILE_MUTEX
                    LOCK_MUTEX(&gtInputFileMutex);
#endif
                        fseek(inputBufferFile, 0L, SEEK_SET);
#ifdef USE_INPUT_FILE_MUTEX
                    UNLOCK_MUTEX(&gtInputFileMutex);
#endif
                    LOCK_MUTEX(&gtGlobalVarMutex);
                        bInputEosReached = bOutputEosReached = gbGotBufferFlagEvent = false;    
                    UNLOCK_MUTEX(&gtGlobalVarMutex);
                    DPRINT(_HERE, 8, "\nLASIC_CMD_RWND : DONE ");
                    break;
                default:
                    break;
                } /* switch(geAppState... */
                break;  /* case LASIC_CMD_RWND */
                } /* switch(eventinfo->data)... */
            break; /* case APP_EVENT_CMD_CHANGE_STATE... */

        case APP_EVENT_OMX_STATE_SET: {
            OMX_STATETYPE newstate = (OMX_STATETYPE)eventinfo->data;
            char namecurr[30];
            char namenew[30];
            OMX_STATETYPE currstate;
            if(! map_omx_state_name(0, &newstate, namenew)) {
                DPRINT(_HERE, 6, "ERROR: APP_EVENT_OMX_STATE_SET: Got Invalid OMX_State Val=%d", (int)newstate);
                break;
            }
            CHANGE_OMX_STATE(newstate);
        }
        break;  /* case APP_EVENT_OMX_STATE_SET */

        case APP_EVENT_EOS_INPUT: {
            int bailout = 0;
            switch(geAppState) {
            case APP_STATE_Foetus:
                break;
            default:
                /* If Looped-play-without-EOS is disabled then wait for EOS on output buffer */
                if(! gbNoEosInLoopedPlay && ! gbNoWaitEosOnLastBuff) {
                    DPRINT(_HERE, 8, "\nmain(): EVENT_EOS_INPUT...waiting for EOS on OUTPUT buffer (gbNoWaitEosOnLastBuff=%d, geAppState=%d)", gbNoWaitEosOnLastBuff, geAppState);
                    /* If APP_EVENT_EOS_OUTPUT doesn't come then watchdog will timeout */
                    bailout = 1;
                    break; 
                }
            }   /* switch(geAppState... */
            if(bailout)
                break;
        }   /* case APP_EVENT_EOS_INPUT... */
            /* Fall-through intentional here (case APP_EVENT_EOS_INPUT... */
        case APP_EVENT_END:
            /* Fall-through intentional here */
        case APP_EVENT_STOP:
            /* Fall-through intentional here */
        case APP_EVENT_EOS_OUTPUT:    
            /* Since there was fall-thru from above, determine what the actual event was... */
            if(APP_EVENT_EOS_INPUT == eventinfo->event)
                strcpy(tempstr, "EOS_INPUT");
            else if(APP_EVENT_END == eventinfo->event)
                strcpy(tempstr, "END");
            else if(APP_EVENT_STOP == eventinfo->event)
                strcpy(tempstr, "STOP");
            else if(APP_EVENT_EOS_OUTPUT == eventinfo->event)
                strcpy(tempstr, "EOS_OUTPUT");
            else 
                strcpy(tempstr, "<ERROR>");

            /* For cases other than Stop/End, the DecEnd time is already being saved (in EBD / FBD callbacks) */
            if(APP_EVENT_END == eventinfo->event || APP_EVENT_STOP == eventinfo->event) {
                LOCK_MUTEX(&gtGlobalVarMutex);
                    (void)gettimeofday(&gtDecEndTimeVal, NULL);
                UNLOCK_MUTEX(&gtGlobalVarMutex);
            }

            DPRINT(_HERE, 6, "\nHANDLING APP_EVENT_%s event=%d, AppState=%d, bInputEosReached=%d, bOutputEosReached=%d", tempstr, eventinfo->event, geAppState, bInputEosReached, bOutputEosReached);
            if(APP_STATE_Foetus == geAppState)
                break;

            /* If EOS flag checking is not OFF then check for BufferFlagEvent for EOS */
            if(! gbNoEosInLoopedPlay && ! gbNoBufferFlagEosWait) {
                DPRINT(_HERE, 8, "\nmain(): EVENT_EOS_INPUT...waiting for BufferFlag EOS event (gbNoBufferFlagEosWait=%d, geAppState=%d)", gbNoBufferFlagEosWait, geAppState);
                if(0 == timed_wait_for_val((volatile unsigned long *)&gbGotBufferFlagEvent, 1ul, 0, NULL) && false == gbGotBufferFlagEvent) {
                    DPRINT(_HERE, 6, "*****************************************\n");
                    DPRINT(_HERE, 6, "\nAPP_EVENT_EOS_INPUT: ERROR: DID NOT GET BufferFlag Event for EOS !");
                    DPRINT(_HERE, 6, "*****************************************\n");
                    ERROR_ACTION(2, 54);
                }
            }
            /* Fall-thru intentional */
        case APP_EVENT_CONTINUE_LOOP:
            if(APP_EVENT_END != eventinfo->event && APP_EVENT_STOP != eventinfo->event && APP_STATE_Foetus != geAppState)
            {
                // If Looped playback is enabled then reset input file pointer & continue
                if(gbLoopedPlay) {
                    int cmd = (geAppState == APP_STATE_Sprinting) ? LASIC_CMD_FFWD : LASIC_CMD_PLAY;  // TODO: Rewind not implemented (no need)
                    int retval, tempstate;

                    if(gbForceFeedBuffsOnLooping && APP_STATE_Walking == geAppState) {
                        DPRINT(_HERE, 6, "\nLOOPING ON (EOS_IN=%d / EOS_OUT=%d / gbNoEosInLoopedPlay=%d / gbForceFeedBuffsOnLooping=%d): Doing Exec->Idle->Exec...", 
                               bInputEosReached, bOutputEosReached, gbNoEosInLoopedPlay, gbForceFeedBuffsOnLooping);
                        CHANGE_OMX_STATE(OMX_StateIdle);
                        CHANGE_OMX_STATE(OMX_StateExecuting);
                    } 
                    else {
                        /* 04/22/09 */
                        DPRINT(_HERE, 6, "\nLOOPING ON after EOS (EOS_IN=%d / EOS_OUT=%d / gbNoEosInLoopedPlay=%d / gbForceFeedBuffsOnLooping=%d): Collecting input buffers...", 
                               bInputEosReached, bOutputEosReached, gbNoEosInLoopedPlay, gbForceFeedBuffsOnLooping);
                        ATOMIC_DO_GLOBAL(tempstate = geAppState);
                        APP_CHANGE_STATE(APP_STATE_CollectOmxBuffers);
                        retval = wait_for_all_buffers(500000ul, INBUF, &gtGlobalVarMutex);
                        APP_CHANGE_STATE(tempstate);
                        DPRINT(_HERE, 6, "Finished collecting input buffers [Result=%d/%s]...Re-setting input file...", retval, retval ? "FAILED" : "SUCCESS");
                        /* DPRINT(_HERE, 6, "Finished collecting buffers [Collected : %s]", -1 == retval ? "OUTBUFS" 
                               : (-2 == retval ? "INBUFS" : ( -3 == retval ? "NONE" : "IN+OUTBUFS")))); */
                    }
                    /* Input file reset & read should be thread-safe */
#ifdef USE_INPUT_FILE_MUTEX
                    LOCK_MUTEX(&gtInputFileMutex);
#endif
     /* 04/22/09 */ LOCK_MUTEX(&gtGlobalVarMutex);
                        fseek(inputBufferFile, 0L, SEEK_SET);
                    // 04/22/09 UNLOCK_MUTEX(&gtInputFileMutex);
                    //04/22/09 LOCK_MUTEX(&gtGlobalVarMutex);
                        bInputEosReached = bOutputEosReached = gbGotBufferFlagEvent = false;    
                        gnLoopCount++;
                    UNLOCK_MUTEX(&gtGlobalVarMutex);
#ifdef USE_INPUT_FILE_MUTEX
     /* 04/22/09 */ UNLOCK_MUTEX(&gtInputFileMutex);
#endif
                    /* 04/22/09 : At this point most or all buffers should be with the client (because either Exec->Idle was done above OR
                    EOS was found & subsequent EBD/FBDs returned the buffers), hence refeed buffers */
                    DPRINT(_HERE, 6, "\nLOOPING ON: After file-reset...REFEEDING unused buffers by sending APP_CMD ffwd/play...");
                    appQ_post_event(APP_EVENT_CMD_CHANGE_STATE, cmd, 0, false); // This is non-blocking to prevent deadlock (main is the only consumer of the AppQ)
                    break;
                }
            }

            /* Start watchdog afresh to ensure the entire cleanup operation finishes */
            watchdog_switch(true);   // Turn on watchdog

            if(APP_STATE_Foetus != geAppState && avc_dec_handle) {
                stop_and_cleanup(APP_EVENT_END == eventinfo->event ? false : true);
#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
                QPERF_END(client_decode);
                QPERF_SET_ITERATION(client_decode, fbd_cnt);
    #ifdef TARGET_ARCH_8K
                QPERF_TERMINATE(client_decode);
                if(fb_display) {
                     QPERF_TERMINATE(render_fb);
                }
    #else
                QPERF_SHOW_STATISTIC(client_decode);
                if(fb_display) {
                     QPERF_SHOW_STATISTIC(render_fb);
                }
    #endif                

#endif
                LOCK_MUTEX(&gtGlobalVarMutex);
                    fbd_cnt = 0; ebd_cnt=0; 
                    bInputEosReached = false;
                    bOutputEosReached = false;
                    gnDumpedDecIframeCnt = 0;
                    geAppState = APP_STATE_Foetus;      
                    gnFfwd_UnitsDecoded = 0;
                    gnIFrameCount = gnOutputUnitCnt = gnInputUnitCnt = gnNALUnitCnt = gnFlushCount = gbGotBufferFlagEvent = 0;
                UNLOCK_MUTEX(&gtGlobalVarMutex);
                
                error = OMX_ErrorNone;
            }
            if(APP_EVENT_END == eventinfo->event || exitAfterTest) {
                DPRINT(_HERE, 6, "\nDELETING APP RESOURCES and EXITING...");
                pthread_mutex_destroy(&gtAppQMutex);
                pthread_cond_destroy(&gtAppQCond);
                pthread_mutex_destroy(&gtWaitOmxEventMutex);
                pthread_cond_destroy(&gtWaitOmxEventCond);
                pthread_mutex_destroy(&gtGlobalVarMutex);
                pthread_mutex_destroy(&gtInputFileMutex);
                pthread_mutex_destroy(&gtWatchdogMutex);
                pthread_mutex_destroy(&gtPrintMutex);
                exit(0);
            }
            else if(gbRepeatMenu) 
                goto LABEL_MENU;

            break;  /* case APP_EVENT_EOS_INPUT... */
        case APP_EVENT_PORT_RECONFIG:
            DPRINT(_HERE, 8, "\nmain(): RECEIVED APP_EVENT_PORT_RECONFIG...");
            if(error = (OMX_ERRORTYPE)handle_port_settings_changed(eventinfo->data)) {
                DPRINT(_HERE, 6, "\nERROR: handle_port_settings_changed() retval=%d", error);
                ATOMIC_DO_GLOBAL(gbDuringOutPortReconfig = false);
                ERROR_ACTION(0, 58);
            }
            DPRINT(_HERE, 8, "\nmain(): APP_EVENT_PORT_RECONFIG : Done...");
            /* Quick n dirty fix for timing issue: codec can start sending EBDs the moment OutPort is enabled. But main thread may not have 
            finished processing the port reconfig (ie. handle_port_settings_changed() may still be in progress & that disrupts things if EBD comes) */
            ATOMIC_DO_GLOBAL(gbDuringOutPortReconfig = false);
            read_and_empty_buffers(gnTotalInputBufs, 0, 1, 0);  // In case of EBDs during port reconfig, buffers were returned. Refeed them now
            break;

        case APP_EVENT_EMPTY_BUFFER_DONE: {
            EmptyBufferDone((eventinfo->omx_ebd_params).hComponent, (eventinfo->omx_ebd_params).pAppData, (eventinfo->omx_ebd_params).pBuffer);
            }
            break;
        case APP_EVENT_FILL_BUFFER_DONE: {
            // FBD callback has same prototype as EBD
            FillBufferDone((eventinfo->omx_ebd_params).hComponent, (eventinfo->omx_ebd_params).pAppData, (eventinfo->omx_ebd_params).pBuffer);
            }
            break;

        default :
            break;
            }   /* switch(eventinfo->event)... */
        DPRINT(_HERE, 8, "\nSWITCH BLOCK END : APP_EVENT_COUNT=%d", keepAliveTick++);
    }   /* while(1) ... */
    DPRINT(_HERE, 6, "\nERROR: Came out of while(1) ... check code ...\n");
    ERROR_ACTION(0, 222);
    return 1;
}   /* main()... */

static int 
run_tests(video_display_format_type format)
{
    int bufCnt=0;
    DPRINT(_HERE, 8, "Inside %s num[%d] format[%d]\n", __FUNCTION__,curr_test_num,format);

    set_read_buffer_func();

    DPRINT(_HERE, 8, "file_type_option %d!\n", file_type_option);

    switch(file_type_option)
    {
        case FILE_TYPE_DAT_PER_AU:
        case FILE_TYPE_ARBITRARY_BYTES:
        case FILE_TYPE_264_NAL_SIZE_LENGTH:
        case FILE_TYPE_PICTURE_START_CODE:
        case FILE_TYPE_RCV:
        case FILE_TYPE_VC1:
        case FILE_TYPE_VP_6:
            if(Init_Decoder() != 0x00)
            {
                DPRINT(_HERE, 8, "Decoder Init failed\n");
                return -1;
            }
            if(Play_Decoder(format) != 0x00)
            {
                DPRINT(_HERE, 8, "Play_Decoder for test case [%d] failed\n",curr_test_num);
                return -1;
            }
            break; 
        default:
            DPRINT(_HERE, 8, "Invalid Entry...%d\n",file_type_option);
            break;
    }

    // Wait till EOS is reached...
    while(1)
    {
        if(bOutputEosReached)
        {
	    DPRINT(_HERE, 8, "\nMoving the decoder to idle state \n");
	    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
	    wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, 0);
        CHECK_OMX_STATE(OMX_StateIdle);

	    DPRINT(_HERE, 8, "\nMoving the decoder to loaded state \n");
	    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateLoaded,0);

	    printf ("\nFillBufferDone: Deallocating i/p and o/p buffers \n");
	    for(bufCnt=0; bufCnt < gnTotalInputBufs; ++bufCnt) {
	        OMX_FreeBuffer(avc_dec_handle, 0, pInputBufHdrs[bufCnt]);
            }

	    for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt) {
	        OMX_FreeBuffer(avc_dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
	    }
        wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, 0);
        CHECK_OMX_STATE(OMX_StateLoaded);

	    printf ("\nFillBufferDone: Free handle avc decoder\n");

	    OMX_ERRORTYPE result = OMX_FreeHandle(avc_dec_handle);
	    if (result != OMX_ErrorNone) {
	        printf ("\nOMX_FreeHandle error. Error code: %d\n", result);
	    }

	    /* Deinit OpenMAX */
    	    printf ("\nFillBufferDone: De-initializing OMX \n");
	    OMX_Deinit();

	    DPRINT(_HERE, 8, "\nFillBufferDone: closing all files\n");
	    fclose(inputBufferFile);
	    file_write && outputBufferFile && fclose(outputBufferFile);
        avc_dec_handle = NULL;  // this should be here instead of before OMX_FreeHandle etc
        fbd_cnt = 0; ebd_cnt=0; 
        bInputEosReached = false;
        bOutputEosReached = false;
	    pthread_cond_destroy(&cond);
	    pthread_mutex_destroy(&lock);
            DPRINT(_HERE, 8, "*****************************************\n");
            DPRINT(_HERE, 8, "******...TEST COMPLETED...***************\n");
            DPRINT(_HERE, 8, "*****************************************\n");
	    break;
	}
    }
    return 0;
}

static int Init_Decoder()
{
    DPRINT(_HERE, 6, "\nEntered...");
    OMX_ERRORTYPE omxresult;
    OMX_U32 total = 0;
    OMX_U8 vdecCompNames[50];
    typedef OMX_U8* OMX_U8_PTR;
    char *role ="video_decoder";

    static OMX_CALLBACKTYPE call_back = {&EventHandler, &EBD_Handler, &FBD_Handler};

    int i = 0;
    long bufCnt = 0;

    /* Init. the OpenMAX Core */
    DPRINT(_HERE, 6, "\nInitializing OpenMAX Core....\n");
    omxresult = OMX_Init();

    if(OMX_ErrorNone != omxresult) {
        DPRINT(_HERE, 0, "\n Failed to Init OpenMAX core");
      	return -1;
    }
    else {
        DPRINT(_HERE, 6, "\nOpenMAX Core Init Done\n");
    }
	
    /* Query for video decoders*/
    OMX_GetComponentsOfRole(role, &total, 0);
    DPRINT(_HERE, 6, "\nTotal components of role=%s :%d", role, total);

    if(total) 
    {
        /* Allocate memory for pointers to component name */
        OMX_U8** vidCompNames = (OMX_U8**)malloc((sizeof(OMX_U8*))*total);

        for (i = 0; i < total; ++i) {
            vidCompNames[i] = (OMX_U8*)malloc(sizeof(OMX_U8)*OMX_MAX_STRINGNAME_SIZE);
        }
        OMX_GetComponentsOfRole(role, &total, vidCompNames);
        DPRINT(_HERE, 6, "\nComponents of Role:%s\n", role);
        for (i = 0; i < total; ++i) {
            DPRINT(_HERE, 6, "\nComponent Name [%s]\n",vidCompNames[i]);
            free(vidCompNames[i]);
        }
        free(vidCompNames);
    }
    else {
        DPRINT(_HERE, 0, "No components found with Role:%s", role);
    }

    if (codec_format_option == CODEC_FORMAT_H264) 
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.avc", 27);
    }
    else if (codec_format_option == CODEC_FORMAT_MP4) 
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.mpeg4", 29);
    }
    else if (codec_format_option == CODEC_FORMAT_H263) 
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.h263", 28); 
    }
    else if (codec_format_option == CODEC_FORMAT_VC1) 
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.vc1", 27); 
    }
     else if (codec_format_option == CODEC_FORMAT_DIVX)
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.divx", 28);
    }
     else if (codec_format_option == CODEC_FORMAT_VP)
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.vp", 26);
    }
    else if (codec_format_option == CODEC_FORMAT_SPARK0 || codec_format_option == CODEC_FORMAT_SPARK1)
    {
      strncpy((char*)vdecCompNames, "OMX.qcom.video.decoder.spark", 29);
    }
    else
    {
      DPRINT(_HERE, 0, "Error: Unsupported codec %d\n", codec_format_option);
      return -1;
    }

    omxresult = OMX_GetHandle((OMX_HANDLETYPE*)(&avc_dec_handle), 
                              (OMX_STRING)vdecCompNames, NULL, &call_back);
    if (FAILED(omxresult)) {
        DPRINT(_HERE, 0, "\nFailed to Load the component:%s\n", vdecCompNames);
        return -1;
    } 
    else 
    {
        DPRINT(_HERE, 6, "\nComponent %s is in LOADED state\n", vdecCompNames);
    }

    /* Get the port information */
    CONFIG_VERSION_SIZE(portParam);
    omxresult = OMX_GetParameter(avc_dec_handle, OMX_IndexParamVideoInit, 
                                (OMX_PTR)&portParam);

    if(FAILED(omxresult)) {
        DPRINT(_HERE, 0, "\nFailed to get Port Param\n");
        return -1;
    } 
    else 
    {
        DPRINT(_HERE, 6, "\nportParam.nPorts:%d\n", portParam.nPorts);
        DPRINT(_HERE, 6, "\nportParam.nStartPortNumber:%d\n", portParam.nStartPortNumber);
    }

    memset(&portFmt, 0, sizeof(portFmt));   /* 05/21/09 */
/*    DPRINT(_HERE, 6, "Set parameter immediately followed by getparameter");
    omxresult = OMX_SetParameter(avc_dec_handle,
                               OMX_IndexParamPortDefinition,
                               &portFmt);

    if(OMX_ErrorNone != omxresult)
    {
        DPRINT(_HERE, 0, "Set parameter failed");
    }
*/

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
    else if (codec_format_option == CODEC_FORMAT_DIVX)
    {
      portFmt.format.video.eCompressionFormat = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingDivx;
    }
    else
    {
      DPRINT(_HERE, 0, "Error: Unsupported codec %d\n", codec_format_option);
    }
    
	DPRINT(_HERE, 6, "\nLeaving...");
    return 0;
}


static int 
Play_Decoder(video_display_format_type format)
{
    int i, bufCnt;
    int frameSize=0;
    DPRINT(_HERE, 6, "\nEntered...");
    OMX_ERRORTYPE ret;
	
    DPRINT(_HERE, 6, "sizeof[%d]\n", sizeof(OMX_BUFFERHEADERTYPE));
	
    /* open the i/p and o/p files based on the video file format passed */
    if(open_video_file(format, gsInputFilename, gsOutputFilename)) {      
        DPRINT(_HERE, 0, "\nERROR: open_video_file() FAILED !");
	return -1;
    } 	

#ifdef TARGET_ARCH_8K
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
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Arbitrary;
        break;
      }

      case FILE_TYPE_264_NAL_SIZE_LENGTH:
      {
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_OnlyOneCompleteSubFrame;
        break;
      }

      default:
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Unspecified;
        DPRINT(_HERE, 0, "\nWARNING: inputPortFmt.nFramePackingFormat is UNSPECIFIED");
    } 
    if(gbFramePackArbitrary) {
        DPRINT(_HERE, 6, "Frame Packing forced set to ARBITRARY (gbFramePackArbitrary=%d)", gbFramePackArbitrary);
        inputPortFmt.nFramePackingFormat = OMX_QCOM_FramePacking_Arbitrary;
    }
    OMX_SetParameter(avc_dec_handle, (OMX_INDEXTYPE)OMX_QcomIndexPortDefn,
                     (OMX_PTR)&inputPortFmt);
#endif

    /* Query the decoder outport's min buf requirements */
    CONFIG_VERSION_SIZE(portFmt);

    /* Port for which the Client needs to obtain info */
    portFmt.nPortIndex = portParam.nStartPortNumber; 

    OMX_GetParameter(avc_dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    DPRINT(_HERE, 6, "\nDec: Min Buffer Count %d\n", portFmt.nBufferCountMin);
    DPRINT(_HERE, 6, "\nDec: Buffer Size %d\n", portFmt.nBufferSize);

    if(OMX_DirInput != portFmt.eDir) {
        DPRINT(_HERE, 0, "\nERROR : Expected an Input Port, got something else\n");
	return -1;
    }

    bufCnt = 0;
    portFmt.format.video.nFrameHeight = height;
    portFmt.format.video.nFrameWidth  = width;
    OMX_SetParameter(avc_dec_handle,OMX_IndexParamPortDefinition,
                                                       (OMX_PTR)&portFmt); 
    OMX_GetParameter(avc_dec_handle,OMX_IndexParamPortDefinition,
                                                               &portFmt);
    DPRINT(_HERE, 6, "\nSetParam -> GetParam : Got Min Buffer Count %d", portFmt.nBufferCountMin);


    DPRINT(_HERE, 6, "\nVideo format, height = %d", portFmt.format.video.nFrameHeight);
    DPRINT(_HERE, 6, "\nVideo format, width = %d\n", portFmt.format.video.nFrameWidth);
    gnTotalInputBufs = portFmt.nBufferCountMin;

    if(codec_format_option == CODEC_FORMAT_H264)
    {
        OMX_VIDEO_CONFIG_NALSIZE naluSize;
        naluSize.nNaluBytes = nalSize;
#ifdef TARGET_ARCH_8K
        OMX_SetConfig(avc_dec_handle,OMX_IndexConfigVideoNalSize,(OMX_PTR)&naluSize);
#else
        OMX_SetConfig(avc_dec_handle,(OMX_INDEXTYPE)0,(OMX_PTR)&naluSize);
#endif
        DPRINT(_HERE, 6, "**********************************\n");
        DPRINT(_HERE, 6, "SETTING THE NAL SIZE to %d\n",naluSize.nNaluBytes);
        DPRINT(_HERE, 6, "**********************************\n");
    }else if(codec_format_option == CODEC_FORMAT_DIVX) {
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
        OMX_SetParameter(avc_dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoDivx,
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
        OMX_SetParameter(avc_dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoVp,
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
        OMX_SetParameter(avc_dec_handle,(OMX_INDEXTYPE)OMX_QcomIndexParamVideoSpark,
                     (OMX_PTR)&paramSpark);
    }


    
    DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder [Loaded->Idle]\n");
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateIdle,0);
    
    gnTotalInputBufs = portFmt.nBufferCountMin;
    /* Allocate buffer on decoder's i/p port */
    error = Allocate_Buffer(avc_dec_handle, &pInputBufHdrs, portFmt.nPortIndex, 
                            portFmt.nBufferCountMin, portFmt.nBufferSize); 
    if (error != OMX_ErrorNone) {
        DPRINT(_HERE, 0, "\nERROR: OMX_AllocateBuffer for Input buffer FAILED !\n");
	return -1;
    } 
    else {
        DPRINT(_HERE, 6, "\nOMX_AllocateBuffer Input buffer success\n");
    }
	if(CODEC_FORMAT_H264 == codec_format_option && 0 != TEST_UTILS_H264_AllocateRBSPBuffer(portFmt.nBufferSize * 2)) {
        DPRINT(_HERE, 0, "\nERROR: TEST_UTILS_H264_AllocateRBSPBuffer for H264 input buffer FAILED !\n");
		return -1;
	}
    else if(CODEC_FORMAT_H264 == codec_format_option)
        DPRINT(_HERE, 6, "TEST_UTILS_H264_AllocateRBSPBuffer() done ...");

    portFmt.nPortIndex = portParam.nStartPortNumber+1; 
    /* Port for which the Client needs to obtain info */

    OMX_GetParameter(avc_dec_handle,OMX_IndexParamPortDefinition,&portFmt);
    DPRINT(_HERE, 6, "\nMin Buffer Count=%d", portFmt.nBufferCountMin);
    DPRINT(_HERE, 6, "\nBuffer Size=%d", portFmt.nBufferSize);
    gnTotalOutputBufs = portFmt.nBufferCountMin;
    if(OMX_DirOutput != portFmt.eDir) {
        DPRINT(_HERE, 0, "\nERROR : Expected an Output Port, got something else\n");
        return -1;
    }

    /* Allocate buffer on decoder's o/p port */
    error = Allocate_Buffer(avc_dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountMin, portFmt.nBufferSize); 
    if (error != OMX_ErrorNone) {
        DPRINT(_HERE, 0, "\nOMX_AllocateBuffer Output buffer FAILED !\n");
	return -1;
    } 
    else 
    {
        DPRINT(_HERE, 6, "\nOMX_AllocateBuffer Output buffer success\n");
    }

    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);
    CHECK_OMX_STATE(OMX_StateIdle); DPRINT(_HERE, 6, "\nDONE: LOADED->IDLE");

    DPRINT(_HERE, 6, "Transition to Idle State succesful...\n");
    DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder [Idle->Executing]\n");
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandStateSet, OMX_StateExecuting,0);
    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);
    CHECK_OMX_STATE(OMX_StateExecuting); DPRINT(_HERE, 6, "\nDONE: IDLE->EXECUTING");

    (void)gettimeofday(&gtInitEndTimeVal, NULL);
    (void)gettimeofday(&gtDecStartTimeVal, NULL);

    /* code moved to function fill_buffers() ... */
    DPRINT(_HERE, 6, "\nCALLING fill_buffers()\n");
    fill_buffers(gnTotalOutputBufs, 1, 0);
    DPRINT(_HERE, 6, "\nDONE fill_buffers()\n");

    i = 0;

#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
    QPERF_START(client_decode);
#endif  // 05/27/09
    /* 03/23/09 : NAL clips can be read in the usual way (no need for sps/pps etc) but sps/pps needed if waitFirstFrame enabled */
    /*if (gbFramePackArbitrary && gbWaitFirstFrameDec && (codec_format_option == CODEC_FORMAT_H264) && (file_type_option == FILE_TYPE_264_NAL_SIZE_LENGTH))
    {
        int filledLen = 0;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, USED, pInputBufHdrs[0]));
        // Set sps, pps, and first i-frame NAL
        // SPS
        pInputBufHdrs[0]->nOffset = pInputBufHdrs[0]->nFilledLen = pInputBufHdrs[0]->nFlags = 0;    // nFilledLen is updated in Read_Buffer
        filledLen = Read_Buffer_From_Size_Nal(pInputBufHdrs[0]);

        // PPS
        //pInputBufHdrs[0]->nOffset = pInputBufHdrs[0]->nFilledLen;
        pInputBufHdrs[0]->nOffset = filledLen;
        frameSize = Read_Buffer_From_Size_Nal(pInputBufHdrs[0]);
        filledLen += frameSize;

        // I-Frame NAL
        //pInputBufHdrs[0]->nOffset = pInputBufHdrs[0]->nFilledLen;
        pInputBufHdrs[0]->nOffset = filledLen;
        frameSize = Read_Buffer_From_Size_Nal(pInputBufHdrs[0]);
        filledLen += frameSize;
        pInputBufHdrs[0]->nFilledLen = filledLen;
        //pInputBufHdrs[0]->nFilledLen += frameSize;

        pInputBufHdrs[0]->nInputPortIndex = 0;
        pInputBufHdrs[0]->nOffset = 0;
        ret = EMPTY_BUFFER(avc_dec_handle, pInputBufHdrs[0]);
        if (OMX_ErrorNone != ret) {
            ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pInputBufHdrs[0]));
            DPRINT(_HERE, 0, "ERROR: EMPTY_BUFFER failed with result %d\n", ret);
            return -1;
        } 
        else {
            ATOMIC_DO_GLOBAL(++gnInputUnitCnt); 
            DPRINT(_HERE, 8, "\nINFO: EMPTY_BUFFER: DONE EMPTY BUFFER (H264 SPS/PPS / Buffer#%d, Iframecnt=%d), BuffHdr=0x%x, BuffPtr=0x%x\n", 
                   i+1, gnIFrameCount, pInputBufHdrs[0], pInputBufHdrs[0]->pBuffer);        
        }
        i = 1;
    }
    /* else if (false == gbFramePackArbitrary && gbWaitFirstFrameDec && (codec_format_option == CODEC_FORMAT_H264) && (file_type_option == FILE_TYPE_264_NAL_SIZE_LENGTH))
    {
            // Send sps, pps, and first i-frame NAL for decode
            read_and_empty_buffers(3, 0, 1, 1000);      // Empty first buffer
            i += 3;
    } 
    else */ 
    if ((codec_format_option == CODEC_FORMAT_VC1) && (file_type_option == FILE_TYPE_RCV)) 
    {
        int filledLen = 0;

        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, USED, pInputBufHdrs[0]));
        pInputBufHdrs[0]->nOffset = pInputBufHdrs[0]->nFilledLen = pInputBufHdrs[0]->nFlags = 0;
        frameSize = Read_Buffer_From_RCV_File_Seq_Layer(pInputBufHdrs[0]);
        filledLen = pInputBufHdrs[0]->nFilledLen = frameSize;
        DPRINT(_HERE, 6, "After Read_Buffer_From_RCV_File_Seq_Layer frameSize %d\n", frameSize);

        pInputBufHdrs[0]->nOffset = pInputBufHdrs[0]->nFilledLen;
        frameSize = Read_Buffer(pInputBufHdrs[0]);
        //pInputBufHdrs[0]->nFilledLen += frameSize;
        filledLen += frameSize;
        DPRINT(_HERE, 6, "After Read_Buffer frameSize %d\n", frameSize);

        pInputBufHdrs[0]->nFilledLen = filledLen;
        pInputBufHdrs[0]->nInputPortIndex = 0;
        pInputBufHdrs[0]->nOffset = 0;
        ret = EMPTY_BUFFER(avc_dec_handle, pInputBufHdrs[0]);
        if (OMX_ErrorNone != ret) {
            ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pInputBufHdrs[0]));
            DPRINT(_HERE, 0, "ERROR: EMPTY_BUFFER FAILED with result %d\n", ret);
            return -1;
        } 
        else {
            ATOMIC_DO_GLOBAL(++gnInputUnitCnt);
            // After successful ETB, buffer is with decoder hence do not access any buffer data except header & buffptr
            DPRINT(_HERE, 8, "\nINFO: EMPTY_BUFFER: DONE EMPTY BUFFER (RCV SPS/PPS / Buffer#%d, Iframecnt=%d), BuffHdr=0x%x, BuffPtr=0x%x\n", 
                   i+1, gnIFrameCount, pInputBufHdrs[0], pInputBufHdrs[0]->pBuffer);        
        }
        i = 1;
    }
    else 
    {
        i = 0;
    }

    if(gbSendEosAtStart) {
        // This will cause the last input buffer to be free so the EOS can be sent using that 
        i++;
    }
    /* code moved to function read_and_empty_buffers() ... */

    /* After first input frame is given, either an FBD will come or a Port Reconfig event */
    if(gbWaitFirstFrameDec) {
        DPRINT(_HERE, 6, "\nFIRST FRAME WAIT: CALLING read_and_empty_buffers()\n");
        read_and_empty_buffers(gnTotalInputBufs - i, 0, 1, 0);
        DPRINT(_HERE, 6, "\nFIRST FRAME WAIT: DONE read_and_empty_buffers()\n");

        DPRINT(_HERE, 8, "\nINFO : WAITING FOR FIRST FRAME TO BE PARSED...");
        while(APP_STATE_WaitFirstFrame == geAppState)
            usleep(50000);
        if(APP_STATE_OutPortReconfig == geAppState) {
            if(error = (OMX_ERRORTYPE)handle_port_settings_changed(1)) {
                DPRINT(_HERE, 6, "\nERROR: handle_port_settings_changed() retval=%d", error);
                ERROR_ACTION(0, 58);
                return -1; 
            }
            DPRINT(_HERE, 8, "\nHANDLE_PORT_RECONFIG : Done...");
            APP_CHANGE_STATE(APP_STATE_Walking);
            DPRINT(_HERE, 8, "\nINFO : FIRST FRAME PARSING [and Port Reconfig] DONE...continuing...");
        }
        else
            DPRINT(_HERE, 8, "\nINFO : FIRST FRAME PARSING [NO PortReconfig] DONE...continuing...");

    } /* if(gbWaitFirstFrameDec...*/

    /* IF first frame wait was enabled then all EBDs until now have returned input buffers to app */
    DPRINT(_HERE, 6, "\nCALLING read_and_empty_buffers()\n");
    read_and_empty_buffers(gnTotalInputBufs - i, 0, 1, 0);
    DPRINT(_HERE, 6, "\nDONE read_and_empty_buffers()\n");

    if(gbSendEosAtStart) {
        i = gnTotalInputBufs - 1;
        ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, USED, pInputBufHdrs[i]));
        DPRINT(_HERE, 6, "\nINFO: SENDING EOS amongst first few buffers (gbSendEOSOnStart=%d)...", gbSendEosAtStart);
        pInputBufHdrs[i]->nFilledLen = pInputBufHdrs[i]->nOffset = pInputBufHdrs[i]->nInputPortIndex = 0;
        pInputBufHdrs[i]->nFlags = OMX_BUFFERFLAG_EOS;
        if(OMX_ErrorNone != EMPTY_BUFFER(avc_dec_handle, pInputBufHdrs[i])) {
           ATOMIC_DO_GLOBAL(update_used_buffer_list(INBUF, FREE, pInputBufHdrs[i]));
        }
        else {
            ATOMIC_DO_GLOBAL(++gnInputUnitCnt);
        }
    }
    /* code moved to function handle_port_settings_changed() & Port reconfig event handled in main() */

    DPRINT(_HERE, 6, "\nLeaving...");
    return 0;
}

/* Name sayz it all */
static int 
handle_port_settings_changed(int port)
{
    int i, bufCnt;
    int frameSize=0;
    DPRINT(_HERE, 8, "Inside %s \n", __FUNCTION__);
    OMX_ERRORTYPE ret;
    app_state_t curr_app_state, saved_app_state=APP_STATE_Walking;

    DPRINT(_HERE, 6, "**** RECIEVED PORT SETTINGS CHANGED EVENT ****\n");

    if(port != 1) {
        DPRINT(_HERE, 0, "\nERROR: PORT_SETTINGS_CHANGED on PortNr=%d (expected only on PortNr=1)", port);
        ERROR_ACTION(2, 58);
        return -1;
    }

    if(false == gbWaitFirstFrameDec) {
        if(geAppState != APP_STATE_Walking) {
            DPRINT(_HERE, 0, "ERROR: Received PORT_RECONFIG when AppState is NOT PLAY/PortReconfig (geAppState=%d)...trying to recover...", geAppState);
            ERROR_ACTION(4, 222);
            saved_app_state = geAppState;
        }
        curr_app_state = APP_STATE_Walking;
        DPRINT(_HERE, 6, "Current AppState=%d ", geAppState);
        APP_CHANGE_STATE(APP_STATE_CollectOmxOutBuffers);
    }
    /* else do nothing since if gbWaitFirstFrameDec was TRUE then AppState has already been changed to WaitFirstFrame */

    // Send DISABLE command
    DPRINT(_HERE, 6, "\nOMX_SendCommand Decoder -> Port DISABLE[1]");
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandPortDisable, 1, 0);

    DPRINT(_HERE, 6, "******************************************\n");
    DPRINT(_HERE, 6, "FREEING BUFFERS\n");
    DPRINT(_HERE, 6, "******************************************\n");
    // Free output Buffer 
    for(bufCnt=0; bufCnt < portFmt.nBufferCountMin; ++bufCnt) {
        OMX_FreeBuffer(avc_dec_handle, 1, pOutYUVBufHdrs[bufCnt]);
    }
    // wait for Disable event to come back
    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);
    DPRINT(_HERE, 6, "\n**** DONE: Decoder -> PORT DISABLE[1] ****");

    if(false == gbWaitFirstFrameDec) {
        if(curr_app_state != saved_app_state) {
            APP_CHANGE_STATE(saved_app_state);    // Restore App state
        }
        else {
        APP_CHANGE_STATE(curr_app_state);    // Restore App state
        }
        DPRINT(_HERE, 6, "\nAppState reset to %d", geAppState);
    }
    // Send Enable command
    DPRINT(_HERE, 6, "\n**** OMX_SendCommand Decoder -> Port ENABLE[1] ****");
    clear_event(); OMX_SendCommand(avc_dec_handle, OMX_CommandPortEnable, 1, 0);

    // 03/23/09 : Code moved here
    /* Reset width & height to correct values after reconfig */
    portFmt.nPortIndex = 1;
    DPRINT(_HERE, 6, "\nOLD Min Buffer Count=%d, Buffer Size=%d\n", portFmt.nBufferCountMin, portFmt.nBufferSize);
    if(OMX_ErrorNone != (ret = OMX_GetParameter(avc_dec_handle,OMX_IndexParamPortDefinition,&portFmt))) {
        DPRINT(_HERE, 0, "\nERROR: OMX_GetParam() failed with ret=%d !", ret);
        ERROR_ACTION(1, 110);
    }
    else {
        DPRINT(_HERE, 6, "\nNEW MinBufferCount=%d, BufferCountActual=%d, Buffer Size=%d\n", portFmt.nBufferCountMin, 
               portFmt.nBufferCountActual, portFmt.nBufferSize);
        LOCK_MUTEX(&gtGlobalVarMutex);
            gnTotalOutputBufs = portFmt.nBufferCountMin;
            DPRINT(_HERE, 6, "\n(OLD height=%d, width=%d)", height, width);
            height = portFmt.format.video.nFrameHeight;
            width = portFmt.format.video.nFrameWidth;
            DPRINT(_HERE, 6, "\nNEW height=%d, width=%d", height, width);
        UNLOCK_MUTEX(&gtGlobalVarMutex);
    }

    DPRINT(_HERE, 6, "******************************************\n");
    DPRINT(_HERE, 6, "RE-ALLOCATING NEW BUFFERS\n");
    DPRINT(_HERE, 6, "******************************************\n");

    // AllocateBuffers
    /* Allocate buffer on decoder's o/p port */
    error = Allocate_Buffer(avc_dec_handle, &pOutYUVBufHdrs, portFmt.nPortIndex,
                            portFmt.nBufferCountActual, portFmt.nBufferSize); 
    if (error != OMX_ErrorNone) {
        DPRINT(_HERE, 0, "\nERROR: OMX_AllocateBuffer on Output buffer FAILED !\n");
    return -1;
    } 
    else 
    {
        DPRINT(_HERE, 6, "\nOMX_AllocateBuffer Output buffer success\n");
    }

    // wait for enable event to come back
    if(-1 == wait_for_event(gnTimeoutSecs, gnTimeoutUSecs, true))
        ERROR_ACTION(0, 61);

    DPRINT(_HERE, 6, "\n**** DONE: Decoder -> PORT ENABLE[1] ****");

    ATOMIC_DO_GLOBAL(gnOutputUnitCnt = fbd_cnt = gnFBDLatency = gnFfwd_UnitsDecoded = 0);

    // Handle error case of Port reconfig event during Flush for FFWD
    if(APP_STATE_WaitOmxFlush == curr_app_state) {
        DPRINT(_HERE, 0, "[ERROR: got PORT_RECONFIG during FLUSH] Sending fake flush complete event & returning...");
        received_some_event(OMX_EventCmdComplete, OMX_CommandFlush, 1, 0);  // Send fake flush complete event in case main thread was waiting
        return 0;
    }

    if(gbWaitFirstFrameDec)
         APP_CHANGE_STATE(APP_STATE_Walking);

    DPRINT(_HERE, 6, "\nCalling fill_buffers on new output buffers...");
    /* code moved to function fill_buffers() ... */
    ret = (OMX_ERRORTYPE)fill_buffers(gnTotalOutputBufs, 1, 0);
    DPRINT(_HERE, 6, "\nReturning from handle_port_settings_changed()...");
    return (ret == (OMX_ERRORTYPE)portFmt.nBufferCountMin ? 0 : (int)ret);
}


static OMX_ERRORTYPE 
Allocate_Buffer ( OMX_COMPONENTTYPE *avc_dec_handle, 
                                       OMX_BUFFERHEADERTYPE  ***pBuffs, 
                                       OMX_U32 nPortIndex, 
                                       long bufCntMin, long bufSize) 
{
    DPRINT(_HERE, 6, "Entered... portNum=%d, bufCnt=%d, bufSize=%d\n", nPortIndex, bufCntMin, bufSize);
    OMX_ERRORTYPE error=OMX_ErrorNone;
    long bufCnt=0;

    *pBuffs= (OMX_BUFFERHEADERTYPE **) 
                   malloc(sizeof(OMX_BUFFERHEADERTYPE)*bufCntMin);  // shud technically be sizeof(OMX..TYPE *) * bufCntMin to save memry

    for(bufCnt=0; bufCnt < bufCntMin; ++bufCnt) {
        DPRINT(_HERE, 8, "\n OMX_AllocateBuffer No %d \n", bufCnt);
        error = OMX_AllocateBuffer(avc_dec_handle, &((*pBuffs)[bufCnt]), 
                                   nPortIndex, NULL, bufSize);
        if(OMX_ErrorNone != error) break;  
    }
    DPRINT(_HERE, 6, "Leaving... \n");
    return error;
}

//Mahesh

static int Read_Buffer_From_FrameSize_File(OMX_BUFFERHEADERTYPE  *pBufHdr)
{
    unsigned int  size = 0, readSize =0;
    unsigned int readOffset = 0;
    unsigned char* pBuf = pBufHdr->pBuffer + pBufHdr->nOffset;

    // read the vop size bytes
    readOffset = fread(&size, 1, 4, inputBufferFile);
    if (readOffset != 4)
    {
         DPRINT(_HERE, 8, "ReadBufferUsingVopSize failed to read vop size  bytes\n");
        return 0;
    }

    DPRINT(_HERE, 8, "vop Size=%d bytes_read=%d",size,readOffset);

    readSize =( (size > pBufHdr->nAllocLen)?pBufHdr->nAllocLen:size);
    // read the vop
    readOffset = fread(pBuf, 1, readSize, inputBufferFile);

    if (readOffset != readSize)
    {
         DPRINT(_HERE, 8, "ReadBufferUsingVopSize failed to read vop %d bytes\n", size);
         return 0;
    }
    if(readSize < size)
    {
      /* reseek to beginning of next frame */
       fseek(inputBufferFile, size-readOffset, SEEK_CUR);
    }

    if(!(0x80 & pBuf[0]))
    {
        pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
        ATOMIC_DO_GLOBAL(++gnIFrameCount);
    }

    pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;;
    pBufHdr->nFilledLen = readOffset;

    ++gnNALUnitCnt;
    return readOffset;
}
/* Reads NAL Unit from input file into OMX buffer 
   Sets nFilledLen and nTimeStamp fields appropriately in the buffer
   RETURNS: 0 if EOS found (the buffer is populated with partial frame if present) 
            Nr. of bytes added to buffer otherwise 
*/
// 03/19/09 
static int Read_Buffer_From_Size_Nal(OMX_BUFFERHEADERTYPE  *pBufHdr) 
{
    // NAL unit stream processing
    char temp_size[SIZE_NAL_FIELD_MAX];
    int i = 0;
    int j = 0;
    unsigned int orig_size = 0;   // Need to make sure that uint32 has SIZE_NAL_FIELD_MAX (4) bytes
    unsigned int size = 0;
    int bytes_read = 0;
    int isnewframe = true;
    int frametype = -1;
    pBufHdr->nFilledLen = pBufHdr->nFlags = 0;

    pBufHdr->nTimeStamp = time(NULL);

    // read the "size_nal_field"-byte size field
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset, 1, nalSize, inputBufferFile);
    if (bytes_read < nalSize) 
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "Failed to read frame or it might be EOF\n");
      DPRINT(_HERE, 6, "\nEND_INPUT: Bytes_read [%d] < nalSize [%d] (BufHdr=0x%x, Buffr=0x%x)", bytes_read, nalSize, pBufHdr, pBufHdr->pBuffer);
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
    orig_size = (unsigned int)(*((unsigned int *)(temp_size)));
    size = MIN(orig_size, pBufHdr->nAllocLen - pBufHdr->nOffset - nalSize);  // 07/10/09 : just a safeguard
    if(size != orig_size)
        DPRINT(_HERE, 5, "WARNING: Original input frame size LARGER than allocated OMX inbuf size (INframe truncated)");

    // now read the data
    bytes_read = fread(pBufHdr->pBuffer + pBufHdr->nOffset + nalSize, 1, size, inputBufferFile);
    if (bytes_read != size) 
    {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR, "Failed to read frame\n");
      DPRINT(_HERE, 6, "\nEND_INPUT: Bytes_read [%d] < FrameSize [%d] (BufHdr=0x%x, Buffr=0x%x )", bytes_read, size, pBufHdr, pBufHdr->pBuffer);
    }
    pBufHdr->nFilledLen = (bytes_read + nalSize);

    ATOMIC_DO_GLOBAL(gnNALUnitCnt++);
    if(bytes_read && -1 == (isnewframe = TEST_UTILS_H264_IsNewFrame(pBufHdr->pBuffer + pBufHdr->nOffset, pBufHdr->nFilledLen, nalSize))) {
        DPRINT(_HERE, 6, "ERROR: Test Utils not initialized (check code)");
        ERROR_ACTION(0, 222);
    }
    if(isnewframe)
        gnInNewIFrame = true;
    if(bytes_read && gnIFrameType == (frametype = get_frame_type(file_type_option, pBufHdr->pBuffer + pBufHdr->nOffset, bytes_read)) 
       && gnInNewIFrame) {
            pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
            ATOMIC_DO_GLOBAL(++gnIFrameCount);
            gnInNewIFrame = false;
    }
    DPRINT(_HERE, 8, "Got H264-IsNewFrame=%d, FrameType=%d %s, IframeCount=%d", isnewframe, frametype, ((5 == frametype) ? "(IFrame)" : ""), gnIFrameCount);

    ++gnNALUnitCnt;
    return bytes_read + nalSize;
}


static int 
Read_Buffer_From_DAT_File(OMX_BUFFERHEADERTYPE  *pBuff) 
{
    // deleted unused var
    long frameSize=0;
    char temp_buffer[10];
    char temp_byte;
    int bytes_read=0;
    int i=0;
    char c = '1'; //initialize to anything except '\0'(0)
    char inputFrameSize[10];
    int count =0; char cnt =0; 
    memset(temp_buffer, 0, sizeof(temp_buffer));
    pBuff->nFilledLen = pBuff->nFlags = 0;

    /* pBuff->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval; */
    pBuff->nTimeStamp = time(NULL); // 04/29/09

    DPRINT(_HERE, 8, "Inside %s \n", __FUNCTION__);
    while (cnt < 10) 
    /* Check the input file format, may result in infinite loop */
    {
        count  = fread(&inputFrameSize[cnt], 1, 1, inputBufferFile); 
        if(count == 0 || inputFrameSize[cnt] == '\0' ) // check count also
            break;
        cnt++;
    }
    inputFrameSize[cnt]='\0';
    frameSize = atoi(inputFrameSize);

    /* get the frame length */
    fseek(inputBufferFile, -1, SEEK_CUR);
    bytes_read = fread(pBuff->pBuffer, 1, frameSize,  inputBufferFile);

    DPRINT(_HERE, 8, "\nActual frame Size [%d] bytes_read using fread[%d]\n", 
               frameSize, bytes_read);

    pBuff->nFilledLen = bytes_read;

    if(bytes_read == 0 || bytes_read < frameSize ) {
        DPRINT(_HERE, 8, "\nEND_INPUT: Bytes read < framesize After Read frame Size (BufHdr=0x%x, Buffr=0x%x)\n", pBuff, pBuff->pBuffer);
        return 0;
    }

    if(get_frame_type(file_type_option, pBuff->pBuffer + pBuff->nOffset, pBuff->nFilledLen - pBuff->nOffset) == gnIFrameType) {
            ATOMIC_DO_GLOBAL(gnIFrameCount++);
    }
    ++gnNALUnitCnt;
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
    pBufHdr->nFlags = pBufHdr->nFilledLen = 0;

    /* pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval; */
    pBufHdr->nTimeStamp = time(NULL); // 04/29/09

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);
    bytes_read = fread(pBufHdr->pBuffer, 1, NUMBER_OF_ARBITRARYBYTES_READ,  inputBufferFile);

    if(bytes_read <= 0 /* 04/14/09 < NUMBER_OF_ARBITRARYBYTES_READ*/ ) {
        QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                     "Bytes read Zero After Read frame Size \n");
        DPRINT(_HERE, 6, "END_INPUT: Bytes read <= 0 [bytes_read=%d] (BufHdr=0x%x, Buffr=0x%x)\n", bytes_read, pBufHdr, pBufHdr->pBuffer);
    }
    pBufHdr->nFilledLen = (bytes_read > 0 ? bytes_read : 0);
    ++gnNALUnitCnt;
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

    DPRINT(_HERE, 6,"Inside %s \n", __FUNCTION__);

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
          DPRINT(_HERE, 6,"Bytes read Zero \n");
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
    pBufHdr->nFilledLen = ret = ((readOffset > pBufHdr->nAllocLen)?pBufHdr->nAllocLen:readOffset);

    if(!(*(pBufHdr->pBuffer+4) & 0xC0))
    {
           pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
           ATOMIC_DO_GLOBAL(++gnIFrameCount);
    }

    ++gnNALUnitCnt;
    return ret;
}

static int Read_Buffer_From_RCV_File_Seq_Layer(OMX_BUFFERHEADERTYPE  *pBufHdr) 
{
    unsigned int readOffset = 0;
    unsigned int size_struct_C = 0;
    unsigned int startcode = 0;
    pBufHdr->nFilledLen = 0;
    pBufHdr->nFlags = 0;

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    /*pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;*/
    pBufHdr->nTimeStamp = time(NULL); // 04/29/09

    if(fread(&startcode, 4, 1, inputBufferFile) < 1) {
        DPRINT(_HERE, 6, "\nEND_INPUT: Couldn't read Startcode (bytes_read < 4) (BufHdr=0x%x, Buffr=0x%x)...", pBufHdr, pBufHdr->pBuffer);
        return 0;
    }

    /* read size of struct C as it need not be 4 always*/
    fread(&size_struct_C, 1, 4, inputBufferFile);

    /* reseek to beginning of sequence header */
    fseek(inputBufferFile, -8, SEEK_CUR);

    if ((startcode & 0xFF000000) == 0xC5000000) 
    {
      // .RVC file
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
      DPRINT(_HERE, 0, "ERROR: (END_INPUT): Unknown VC1 clip format 0x%x (BufHdr=0x%x, Buffr=0x%x)\n", startcode, pBufHdr, pBufHdr->pBuffer);
    }
    ++gnNALUnitCnt;
    return (pBufHdr->nFilledLen = readOffset);
}

static int Read_Buffer_From_RCV_File(OMX_BUFFERHEADERTYPE  *pBufHdr) 
{
    unsigned int readOffset = 0;
    unsigned int orig_len = 0;
    unsigned int len = 0;
    unsigned int key = 0;
    pBufHdr->nFilledLen = pBufHdr->nFlags = 0;

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Read_Buffer_From_RCV_File - nOffset %d\n", pBufHdr->nOffset);
    if(rcv_v1)
    {
      /* for the case of RCV V1 format, the frame header is only of 4 bytes and has
         only the frame size information */
      if((readOffset = fread(&orig_len, 1, 4, inputBufferFile)) < 4) {
         DPRINT(_HERE, 6, "\nEND_INPUT: Couldn't read len (bytes_read=%d, needed=4) (BufHdr=0x%x, Buffr=0x%x)...", readOffset, 
                pBufHdr, pBufHdr->pBuffer);
         return (pBufHdr->nFilledLen = 0);
      }
      QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "Read_Buffer_From_RCV_File - framesize %d %x\n", orig_len, orig_len);
    }
    else
    {
        /* for a regular RCV file, 3 bytes comprise the frame size and 1 byte for key*/
        if((readOffset = fread(&orig_len, 1, 3, inputBufferFile)) < 3) {
           DPRINT(_HERE, 6, "\nEND_INPUT: Couldn't read len (bytes_read=%d, needed=3) (BufHdr=0x%x, Buffr=0x%x)...", readOffset, 
                  pBufHdr, pBufHdr->pBuffer);
           return (pBufHdr->nFilledLen = 0);
        }
        QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                      "Read_Buffer_From_RCV_File - framesize %d %x\n", orig_len, orig_len);
        if((readOffset = fread(&key, 1, 1, inputBufferFile)) < 1) {
           DPRINT(_HERE, 6, "\nEND_INPUT: Couldn't read key (bytes_read [%d] < 1) (BufHdr=0x%x, Buffr=0x%x)", readOffset, 
                  pBufHdr, pBufHdr->pBuffer);
           return (pBufHdr->nFilledLen = 0);
        }
        if (key & 0x80)
        {
           pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME; // Mark frame as IDR
           ATOMIC_DO_GLOBAL(++gnIFrameCount);
        }
    }

    len = MIN(orig_len, pBufHdr->nAllocLen - pBufHdr->nOffset);     // 07/10/09 : just a safeguard
    if(len != orig_len) {    
        DPRINT(_HERE, 5, "WARNING: Original input frame size LARGER than allocated OMX inbuf size (INframe truncated)");
    }

    if(!rcv_v1 && (readOffset = fread(&pBufHdr->nTimeStamp, 1, 4, inputBufferFile)) < 4) {
        DPRINT(_HERE, 6, "\nEND_INPUT: Couldn't read timestamp (bytes_read [%d] < 4) (BufHdr=0x%x, Buffr=0x%x)", readOffset, 
               pBufHdr, pBufHdr->pBuffer);
        return (pBufHdr->nFilledLen = 0);
    }
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                  "Read_Buffer_From_RCV_File - timeStamp %d\n", pBufHdr->nTimeStamp);

    pBufHdr->nTimeStamp = time(NULL);   

    readOffset = fread(pBufHdr->pBuffer + pBufHdr->nOffset, 1, len, inputBufferFile);
    if (readOffset != len) 
    {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
                    "EOS reach or Reading error %d, %s \n", readOffset, readOffset <0 ? strerror( errno ) : "[Fileread OK]"); 
      DPRINT(_HERE, 6, "END_INPUT: Reached EOS or Read error [readOffset=%d / Expected=%d, ErrStr='%s'] (BufHdr=0x%x, Buffr=0x%x)", readOffset, 
             len, readOffset <0 ? strerror( errno ) : "[Fileread OK]", pBufHdr, pBufHdr->pBuffer);
      pBufHdr->nFilledLen = (readOffset > 0 ? readOffset : 0);
      return 0;
    }

    pBufHdr->nFilledLen = (readOffset > 0 ? readOffset : 0);
    ++gnNALUnitCnt;
    return readOffset;
}

static int Read_Buffer_From_VC1_File(OMX_BUFFERHEADERTYPE  *pBufHdr) 
{
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"Inside %s \n", __FUNCTION__);

    unsigned int readOffset = 0;
    int bytes_read = 0;
    unsigned int code = 0;
    pBufHdr->nFilledLen = pBufHdr->nFlags = 0;	

    do
    {
      //Start codes are always byte aligned.
      bytes_read = fread(&pBufHdr->pBuffer[readOffset],1, 1,inputBufferFile);
      if(!bytes_read)
      {
          QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"\n Bytes read Zero \n");
          DPRINT(_HERE, 6, "END_INPUT: Bytes read Zero [bytes_read=%d] (BufHdr=0x%x, Buffr=0x%x)", bytes_read, pBufHdr, pBufHdr->pBuffer);
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

          if(0x0000010E == code)
          {
              pBufHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
              ATOMIC_DO_GLOBAL(++gnIFrameCount);
          }

          while(pBufHdr->pBuffer[readOffset-1] == 0)
            readOffset--;

          break;
        }
      }
      readOffset++;
      if(readOffset == pBufHdr->nAllocLen - pBufHdr->nOffset) {    // 07/10/09 : just a safeguard
          DPRINT(_HERE, 5, "WARNING: Original input frame size LARGER than allocated OMX inbuf size (INframe truncated)");
          break;
      }
    }while (1);

    /*pBufHdr->nTimeStamp = timeStampLfile;
    timeStampLfile += timestampInterval;*/
    pBufHdr->nTimeStamp = time(NULL); // 04/29/09

    pBufHdr->nFilledLen = readOffset;
    ++gnNALUnitCnt;
    return readOffset;
}

static int 
open_video_file (video_display_format_type format, char *infile, char *outfile)
{
    int error_code = 0;
    char *inputfile = ((infile && *infile) ? infile : (char*)video_input_file_names[format]);
    char *outputfile = ((outfile && *outfile) ? outfile : (char*)video_output_file_names[format]);

    DPRINT(_HERE, 6, "Input file='%s', Output file='%s'\n", inputfile, outputfile);

    inputBufferFile = fopen (inputfile, "rb");
    if (inputBufferFile == NULL) {
        DPRINT(_HERE, 8, "\ni/p file %s could NOT be opened\n",inputfile);
        return -1;
    } 
    DPRINT(_HERE, 8, "\nI/p file %s is opened \n", inputfile);

    if(file_write) {
        outputBufferFile = fopen (outputfile, "wb");
        if (outputBufferFile == NULL) {
            DPRINT(_HERE, 8, "\no/p file %s could NOT be opened\n", 
                                         outputfile);
            error_code = -1;
        } 
        else 
        {
            DPRINT(_HERE, 8, "\nO/p file %s is opened \n", 
                                         outputfile);
        }
    }

    if(gbDumpInputBuffer) {
        char *inbufdump_file = (char*)malloc(strlen(inputfile)+15);

        if(! inbufdump_file) {
            DPRINT(_HERE, 6, "\nERROR: malloc FAILED !");
            ERROR_ACTION(0, 245);
            return -1;
        }
        sprintf(inbufdump_file, "%s_INBUF_DUMP", inputfile);
        if(! (inBufferDumpFile = fopen(inbufdump_file, "wb"))) {
            DPRINT(_HERE, 8, "\nERROR: INPUT frames dump file '%s' couldn't be opened for write", inbufdump_file);
            error_code = -1;
        }
        else DPRINT(_HERE, 8, "\nINFO: OPENED INPUT FRAMES DUMP FILE %s", inbufdump_file);

        if(inbufdump_file)
            free(inbufdump_file);
    }

    if(gbDumpDecodedIframes) {
        char *out_iffile = (char*)malloc(strlen(outputfile)+12);
        char *in_iffile = (char*)malloc(strlen(inputfile)+12);
        if(! out_iffile || ! in_iffile) {
            DPRINT(_HERE, 6, "\nERROR: malloc FAILED !");
            ERROR_ACTION(0, 245);
            return -1;
        }
        sprintf(out_iffile, "IFRAMES_%s", outputfile);
        if(! (outputIFramesFile = fopen(out_iffile, "wb"))) {
            DPRINT(_HERE, 6, "\nERROR: OUTPUT I-frames dump file '%s' couldn't be opened for write", out_iffile);
            ERROR_ACTION(3, 202);
            error_code = -1;
        }
        else DPRINT(_HERE, 8, "\nINFO: OPENED I-FRAME DUMP FILE");

        sprintf(in_iffile, "IFRAMES_%s", inputfile);
        if(! (outputEncodedIFramesFile = fopen(in_iffile, "wb"))) {
            DPRINT(_HERE, 8, "\nERROR: INPUT I-frames dump file '%s' couldn't be opened for write", in_iffile);
            error_code = -1;
        }
        else DPRINT(_HERE, 8, "\nINFO: OPENED INPUT-I-FRAME DUMP FILE");

        if(out_iffile)
            free(out_iffile);
        if(in_iffile)
            free(in_iffile);
    }
    error_code = display_start_check();  // Init FB if display is set to on
    return error_code;
}

#if defined(TARGET_ARCH_8K) || defined(TARGET_ARCH_KARURA)
void render_fb(struct OMX_BUFFERHEADERTYPE *pBufHdr)
{
    int ret;
  #ifdef TARGET_ARCH_8K
    unsigned int addr = 0;
    OMX_OTHER_EXTRADATATYPE *pExtraData = 0;
    OMX_QCOM_EXTRADATA_FRAMEINFO *pExtraFrameInfo = 0;
    unsigned int destx, desty,destW, destH;
    unsigned int end = (unsigned int)(pBufHdr->pBuffer + pBufHdr->nAllocLen);
  #endif
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;
  #ifdef _ANDROID_
    MemoryHeapBase *vheap = NULL;
  #endif
    
	struct mdp_blit_req *e;
	union {
		char dummy[sizeof(struct mdp_blit_req_list) +
			   sizeof(struct mdp_blit_req) * 1];
		struct mdp_blit_req_list list;
	} img;

	if (fb_fd < 0)
	{
		DPRINT(_HERE, 2, 
               "ERROR: /dev/fb0 is not opened!\n");
		return;
	}

	img.list.count = 1;
	e = &img.list.req[0];

// 07/13/09
  #ifdef TARGET_ARCH_8K
    addr = (unsigned int)(pBufHdr->pBuffer + pBufHdr->nFilledLen);
    // align to a 4 byte boundary
    addr = (addr + 3) & (~3);

    // read to the end of existing extra data sections
    pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;	

    while (addr < end && pExtraData->eType != OMX_ExtraDataFrameInfo)
    {			
            addr += pExtraData->nSize;	
            pExtraData = (OMX_OTHER_EXTRADATATYPE*)addr;				
    }

    if (pExtraData->eType != OMX_ExtraDataFrameInfo)
    {
       QTV_MSG_PRIO2(QTVDIAG_GENERAL,QTVDIAG_PRIO_ERROR,
                  "pExtraData->eType %d pExtraData->nSize %d\n",pExtraData->eType,pExtraData->nSize);
    }
    pExtraFrameInfo = (OMX_QCOM_EXTRADATA_FRAMEINFO *)pExtraData->data;
  #endif

   pPMEMInfo  = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                    pBufHdr->pPlatformPrivate)->entryList->entry;
  #ifdef _ANDROID_
    vheap = (MemoryHeapBase *)pPMEMInfo->pmem_fd;
  #endif

    DPRINT(_HERE, 8, "[DecWidth %d / DecHeight %d] [DispWidth %d / DispHeight %d]\n", portFmt.format.video.nStride,
           portFmt.format.video.nSliceHeight, portFmt.format.video.nFrameWidth, portFmt.format.video.nFrameHeight);

  e->src.width = portFmt.format.video.nStride;
  e->src.height = portFmt.format.video.nSliceHeight;
  e->src.format = MDP_Y_CBCR_H2V2;
  e->src.offset = pPMEMInfo->offset;
  #ifdef _ANDROID_
	e->src.memory_id = vheap->getHeapID();
  #else
	e->src.memory_id = pPMEMInfo->pmem_fd;
  #endif

    DPRINT(_HERE, 8, 
                  "NEWDISP [pmemOffset %d / pmemID %d]\n",e->src.offset,e->src.memory_id);

   e->dst.width = (finfo.line_length * 8) / (vinfo.bits_per_pixel);
	e->dst.height = vinfo.yres;
	e->dst.format = MDP_RGBA_8888;
	e->dst.offset = 0;
	e->dst.memory_id = fb_fd;
	e->transp_mask = 0xffffffff;
    e->flags = 0;
    e->alpha = 0xff;
  #ifdef TARGET_ARCH_8K
    QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                  "Frame interlace type %d!\n", pExtraFrameInfo->interlaceType);
    if(pExtraFrameInfo->interlaceType != OMX_QCOM_InterlaceFrameProgressive)
    {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_HIGH,
                  "Intrelaced Frame!\n");
       e->flags = MDP_DEINTERLACE;
    }

    switch(gnDisplayWindow)
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


    e->dst_rect.x = destx;
    e->dst_rect.y = desty;
    e->dst_rect.w = destW;
    e->dst_rect.h = destH;
  #else
	e->dst_rect.x = 0;
	e->dst_rect.y = 0;
	e->dst_rect.w = portFmt.format.video.nFrameWidth;
	e->dst_rect.h = portFmt.format.video.nFrameHeight;
  #endif

    e->src_rect.x = 0;
	e->src_rect.y = 0;
    e->src_rect.w = portFmt.format.video.nFrameWidth;
	e->src_rect.h = portFmt.format.video.nFrameHeight;


	if (ret = ioctl(fb_fd, MSMFB_BLIT, &img)) {
        DPRINT(_HERE, 0, "ERROR: MSMFB_BLIT failed! fb_fd=%d, retval=%d\n", fb_fd, ret);
        ERROR_ACTION(1, 157);
		return;
	}

    vinfo.activate = FB_ACTIVATE_VBL;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;

    if ((ret = ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo)) < 0) {
        DPRINT(_HERE, 0, "ERROR: FBIOPAN_DISPLAY failed! fb_fd=%d, ret=%d\n", fb_fd, ret);
//        ERROR_ACTION(1, 159);
		return;
	}

    DPRINT(_HERE, 8, "render_fb complete!\n");
}
#else
/* Function to write every decoded Frame(YUV) into the Framebuffer device (7k h/w) */
static void 
display_frame_7k(void* pBuffer)
{
  int ret;
  struct mdp_blit_req* e = NULL;
  OMX_BUFFERHEADERTYPE *pOutBuffer = (OMX_BUFFERHEADERTYPE*)pBuffer;
  OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pPMEMInfo = NULL;
  
  if(!pOutBuffer || !pOutBuffer->pPlatformPrivate)
  {
     DPRINT(_HERE, 6, "\n\n display_frame_7k(): Invalid Input parameter\n\n");
     ERROR_ACTION(1, 216);
     return;
  }

  pPMEMInfo  = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *)
                 ((OMX_QCOM_PLATFORM_PRIVATE_LIST *)
                 pOutBuffer->pPlatformPrivate)->entryList->entry;
   
  if(!pPMEMInfo)
  {
     DPRINT(_HERE, 6, "\n\n display_frame_7k(): Invalid pPMEMInfo parameter\n\n");
     ERROR_ACTION(1, 216);
     return;
  }
  
  yuv.list.count = 1;

  e = &yuv.list.req[0];

  if(e)
  {
     /* Fill the Source image attributes with pmem_fd and its offset */
     e->src.width  = width;
     e->src.height = height;
     e->src.format = MDP_Y_CBCR_H2V2;
     e->src.offset = pPMEMInfo->offset;
     e->src.memory_id = pPMEMInfo->pmem_fd;

     /* Fill the Destination image attributes with fb_fd and its offset */
     e->dst.width  = vinfo.xres;
     e->dst.height = vinfo.yres;
     e->dst.format = MDP_RGB_565;
     e->dst.offset = 0;
     e->dst.memory_id = fb_fd;

     e->transp_mask = 0xffffffff;
     e->flags = 0;
     e->alpha  = 0xff;

     e->dst_rect.x = 0;
     e->dst_rect.y = 0;
     e->dst_rect.w = width;
     e->dst_rect.h = height;

     e->src_rect.x = 0;
     e->src_rect.y = 0;
     e->src_rect.w = width;
     e->src_rect.h = height;

     /* Call MSMFB_BLIT ioctl to write the YUV frame from pmem_adsp into the FrameBuffer */
     if ((ret=ioctl(fb_fd, MSMFB_BLIT, &yuv.list)) < 0)  
     {
       DPRINT(_HERE, 0, "ERROR: MSM_FBIOBLT failed! fb_fd=%d, retval=%d\n", fb_fd, ret);
       ERROR_ACTION(1, 303);
       return;
     }
  
     vinfo.activate = FB_ACTIVATE_VBL;
     vinfo.xoffset = 0;
     vinfo.yoffset = 0;

     if((ret=ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo)) < 0) 
     {
       DPRINT(_HERE, 0, "ERROR: FBIOPAN_DISPLAY failed! fb_fd=%d, ret=%d\n", fb_fd, ret);
       ERROR_ACTION(1, 304);
       return;
     }
  }
}
#endif /* #ifdef TARGET_ARCH_8K... */


/* *********************************************************************************************
 DEFUNCT CODE (remove later, keep for now) 
** *********************************************************************************************
*/

/* END DEFUNCT CODE */
