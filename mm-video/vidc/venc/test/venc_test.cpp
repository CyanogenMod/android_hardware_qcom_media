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
                    V E N C _ T E S T. C P P

DESCRIPTION

 This is the OMX test app .

REFERENCES

============================================================================*/

//usage
// FILE QVGA MP4 24 384000 100 enc_qvga.yuv QVGA_24.m4v
// FILE QCIF MP4 15 96000 0 foreman.qcif.yuv output_qcif.m4v
// FILE VGA MP4 24 1200000 218 enc_vga.yuv vga_output.m4v
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
//#include <sys/time.h>
#include <time.h>
#include <sys/ioctl.h>
#include <string.h>
//#include <sys/stat.h>
#include "OMX_QCOMExtns.h"
#include "OMX_Core.h"


#define QCOM_EXT 1

#include "OMX_Core.h"
#include "OMX_Video.h"
#include "OMX_Component.h"
#include "camera_test.h"
#include "fb_test.h"
#include "venc_util.h"

//////////////////////////
// MACROS
//////////////////////////

#define CHK(result) if (result != OMX_ErrorNone) { E("*************** error *************"); exit(0); }
#define TEST_LOG
#ifdef VENC_SYSLOG
#include "cutils/log.h"
/// Debug message macro
#define D(fmt, ...) LOGE("venc_test Debug %s::%d "fmt"\n",              \
                         __FUNCTION__, __LINE__,                        \
                         ## __VA_ARGS__)

/// Error message macro
#define E(fmt, ...) LOGE("venc_test Error %s::%d "fmt"\n",            \
                         __FUNCTION__, __LINE__,                      \
                         ## __VA_ARGS__)

#else
     #ifdef TEST_LOG
       #define D(fmt, ...) fprintf(stderr, "venc_test Debug %s::%d "fmt"\n",   \
                            __FUNCTION__, __LINE__,                     \
                            ## __VA_ARGS__)

     /// Error message macro
      #define E(fmt, ...) fprintf(stderr, "venc_test Error %s::%d "fmt"\n", \
                            __FUNCTION__, __LINE__,                   \
                            ## __VA_ARGS__)
     #else
      #define D(fmt, ...)
      #define E(fmt, ...)
         #endif

#endif

//////////////////////////
// CONSTANTS
//////////////////////////
static const int MAX_MSG = 100;
//#warning do not hardcode these use port definition
static const int PORT_INDEX_IN = 0;
static const int PORT_INDEX_OUT = 1;

static const int NUM_IN_BUFFERS = 10;
static const int NUM_OUT_BUFFERS = 10;

unsigned int num_in_buffers = 0;
unsigned int num_out_buffers = 0;

//////////////////////////
/* MPEG4 profile and level table*/
static const unsigned int mpeg4_profile_level_table[][5]=
{
    /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileSimple},
    {99,1485,128000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileSimple},
    {396,5940,128000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileSimple},
    {396,11880,384000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileSimple},
    {1200,36000,4000000,OMX_VIDEO_MPEG4Level4a,OMX_VIDEO_MPEG4ProfileSimple},
    {1620,40500,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple},
    {3600,108000,14000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple},

    {99,2970,128000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {99,2970,128000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {396,5940,384000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {396,11880,768000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {792,23760,3000000,OMX_VIDEO_MPEG4Level4,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {1620,48600,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {3600,108000,14000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {0     ,0       ,0                 ,0     ,0                  }
};

/* H264 profile and level table*/
static const unsigned int h264_profile_level_table[][5]=
{
     /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileBaseline},
    {99,1485,128000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileBaseline},
    {396,3000,192000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileBaseline},
    {396,6000,384000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileBaseline},
    {396,11880,768000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileBaseline},
    {396,11880,2000000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileBaseline},
    {792,19800,4000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileBaseline},
    {1620,20250,4000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileBaseline},
    {1620,40500,10000000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileBaseline},
    {3600,108000,14000000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileBaseline},

    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileHigh},
    {99,1485,160000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileHigh},
    {396,3000,240000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileHigh},
    {396,6000,480000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileHigh},
    {396,11880,960000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileHigh},
    {396,11880,2500000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileHigh},
    {792,19800,5000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileHigh},
    {1620,20250,5000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileHigh},
    {1620,40500,12500000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileHigh},
    {3600,108000,17500000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileHigh},
    {0     ,0       ,0                 ,0                    }
};

/* H263 profile and level table*/
static const unsigned int h263_profile_level_table[][5]=
{
    /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_H263Level10,OMX_VIDEO_H263ProfileBaseline},
    {396,5940,128000,OMX_VIDEO_H263Level20,OMX_VIDEO_H263ProfileBaseline},
    {396,11880,384000,OMX_VIDEO_H263Level30,OMX_VIDEO_H263ProfileBaseline},
    {396,11880,2048000,OMX_VIDEO_H263Level40,OMX_VIDEO_H263ProfileBaseline},
    {99,1485,128000,OMX_VIDEO_H263Level45,OMX_VIDEO_H263ProfileBaseline},
    {396,19800,4096000,OMX_VIDEO_H263Level50,OMX_VIDEO_H263ProfileBaseline},
    {810,40500,8192000,OMX_VIDEO_H263Level60,OMX_VIDEO_H263ProfileBaseline},
    {1620,81000,16384000,OMX_VIDEO_H263Level70,OMX_VIDEO_H263ProfileBaseline},
    {0    , 0      , 0               , 0                       }
};

//////////////////////////
// TYPES
//////////////////////////
struct ProfileType
{
   OMX_VIDEO_CODINGTYPE eCodec;
   OMX_VIDEO_MPEG4LEVELTYPE eLevel;
   OMX_VIDEO_CONTROLRATETYPE eControlRate;
   OMX_VIDEO_AVCSLICEMODETYPE eSliceMode;
   OMX_U32 nFrameWidth;
   OMX_U32 nFrameHeight;
   OMX_U32 nFrameBytes;
   OMX_U32 nBitrate;
   OMX_U32 nFramerate;
   char* cInFileName;
   char* cOutFileName;
};

enum MsgId
{
   MSG_ID_OUTPUT_FRAME_DONE,
   MSG_ID_INPUT_FRAME_DONE,
   MSG_ID_MAX
};
union MsgData
{
   struct
   {
      OMX_BUFFERHEADERTYPE* pBuffer;
   } sBitstreamData;
};
struct Msg
{
   MsgId id;
   MsgData data;
};
struct MsgQ
{
   Msg q[MAX_MSG];
   int head;
   int size;
};

enum Mode
{
   MODE_PREVIEW,
   MODE_DISPLAY,
   MODE_PROFILE,
   MODE_FILE_ENCODE,
   MODE_LIVE_ENCODE
};

//////////////////////////
// MODULE VARS
//////////////////////////
static pthread_mutex_t m_mutex;
static pthread_cond_t m_signal;
static MsgQ m_sMsgQ;

//#warning determine how many buffers we really have
OMX_STATETYPE m_eState = OMX_StateInvalid;
OMX_COMPONENTTYPE m_sComponent;
OMX_HANDLETYPE m_hHandle = NULL;
OMX_BUFFERHEADERTYPE* m_pOutBuffers[NUM_OUT_BUFFERS] = {NULL};
OMX_BUFFERHEADERTYPE* m_pInBuffers[NUM_IN_BUFFERS] = {NULL};
OMX_BOOL m_bInFrameFree[NUM_IN_BUFFERS];

ProfileType m_sProfile;

static int m_nFramePlay = 0;
static int m_eMode = MODE_PREVIEW;
static int m_nInFd = -1;
static int m_nOutFd = -1;
static int m_nTimeStamp = 0;
static int m_nFrameIn = 0; // frames pushed to encoder
static int m_nFrameOut = 0; // frames returned by encoder
static int m_nAVCSliceMode = 0;

static bool m_bWatchDogKicked = false;

/* Statistics Logging */
static long long tot_bufsize = 0;
int ebd_cnt=0, fbd_cnt=0;

//////////////////////////
// MODULE FUNCTIONS
//////////////////////////

void* PmemMalloc(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* pMem, int nSize)
{
   void *pvirt = NULL;

   if (!pMem)
      return NULL;

   pMem->pmem_fd = open("/dev/pmem_adsp", O_RDWR | O_SYNC);
   if ((int)(pMem->pmem_fd) < 0)
      return NULL;
   nSize = (nSize + 4095) & (~4095);
   pMem->offset = 0;
   pvirt = mmap(NULL, nSize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED, pMem->pmem_fd, pMem->offset);
   if (pvirt == (void*) MAP_FAILED)
   {
      close(pMem->pmem_fd);
	  pMem->pmem_fd = -1;
	  return NULL;
   }
   D("allocated pMem->fd = %d pvirt=0x%x, pMem->phys=0x%x, size = %d", pMem->pmem_fd,
       pvirt, pMem->offset, nSize);
   return pvirt;
}

int PmemFree(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* pMem, void* pvirt, int nSize)
{
   if (!pMem || !pvirt)
      return -1;

   nSize = (nSize + 4095) & (~4095);
   munmap(pvirt, nSize);
   close(pMem->pmem_fd);
   pMem->pmem_fd = -1;
   return 0;
}

void SetState(OMX_STATETYPE eState)
{
#define GOTO_STATE(eState)                      \
   case eState:                                 \
      {                                         \
         D("Going to state " # eState"...");            \
         OMX_SendCommand(m_hHandle,                     \
                         OMX_CommandStateSet,           \
                         (OMX_U32) eState,              \
                         NULL);                         \
         while (m_eState != eState)                     \
         {                                              \
            sleep(1);                               \
         }                                              \
         D("Now in state " # eState);                   \
         break;                                         \
      }

   switch (eState)
   {
      GOTO_STATE(OMX_StateLoaded);
      GOTO_STATE(OMX_StateIdle);
      GOTO_STATE(OMX_StateExecuting);
      GOTO_STATE(OMX_StateInvalid);
      GOTO_STATE(OMX_StateWaitForResources);
      GOTO_STATE(OMX_StatePause);
   }
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE ConfigureEncoder()
{
   OMX_ERRORTYPE result = OMX_ErrorNone;
   unsigned const int *profile_tbl = NULL;
   OMX_U32 mb_per_sec, mb_per_frame;
   bool profile_level_found = false;
   OMX_U32 eProfile,eLevel;

   OMX_PARAM_PORTDEFINITIONTYPE portdef; // OMX_IndexParamPortDefinition
#ifdef QCOM_EXT
      OMX_QCOM_PARAM_PORTDEFINITIONTYPE qPortDefnType;
#endif

   portdef.nPortIndex = (OMX_U32) 0; // input
   result = OMX_GetParameter(m_hHandle,
                             OMX_IndexParamPortDefinition,
                             &portdef);
   E("\n OMX_IndexParamPortDefinition Get Paramter on input port");
   CHK(result);
   portdef.format.video.nFrameWidth = m_sProfile.nFrameWidth;
   portdef.format.video.nFrameHeight = m_sProfile.nFrameHeight;

   E ("\n Height %d width %d bit rate %d",portdef.format.video.nFrameHeight
      ,portdef.format.video.nFrameWidth,portdef.format.video.nBitrate);
   result = OMX_SetParameter(m_hHandle,
                             OMX_IndexParamPortDefinition,
                             &portdef);
   E("\n OMX_IndexParamPortDefinition Set Paramter on input port");
   CHK(result);
   portdef.nPortIndex = (OMX_U32) 1; // output
   result = OMX_GetParameter(m_hHandle,
                             OMX_IndexParamPortDefinition,
                             &portdef);
   E("\n OMX_IndexParamPortDefinition Get Paramter on output port");
   CHK(result);
   portdef.format.video.nFrameWidth = m_sProfile.nFrameWidth;
   portdef.format.video.nFrameHeight = m_sProfile.nFrameHeight;
   portdef.format.video.nBitrate = m_sProfile.nBitrate;
   portdef.format.video.xFramerate = m_sProfile.nFramerate << 16;
   result = OMX_SetParameter(m_hHandle,
                             OMX_IndexParamPortDefinition,
                             &portdef);
   E("\n OMX_IndexParamPortDefinition Set Paramter on output port");
   CHK(result);

#ifdef QCOM_EXT

qPortDefnType.nPortIndex = PORT_INDEX_IN;
qPortDefnType.nMemRegion = OMX_QCOM_MemRegionEBI1;
qPortDefnType.nSize = sizeof(OMX_QCOM_PARAM_PORTDEFINITIONTYPE);

result = OMX_SetParameter(m_hHandle,
                             (OMX_INDEXTYPE)OMX_QcomIndexPortDefn,
                             &qPortDefnType);

#endif
   //validate the ht,width,fps,bitrate and set the appropriate profile and level
   if(m_sProfile.eCodec == OMX_VIDEO_CodingMPEG4)
   {
     profile_tbl = (unsigned int const *)mpeg4_profile_level_table;
   }
   else if(m_sProfile.eCodec == OMX_VIDEO_CodingAVC)
   {
     profile_tbl = (unsigned int const *)h264_profile_level_table;
   }
   else if(m_sProfile.eCodec == OMX_VIDEO_CodingH263)
   {
     profile_tbl = (unsigned int const *)h263_profile_level_table;
   }

   mb_per_frame = ((m_sProfile.nFrameHeight+15)>>4)*
                ((m_sProfile.nFrameWidth+15)>>4);

   mb_per_sec = mb_per_frame*(m_sProfile.nFramerate);

   do{
      if(mb_per_frame <= (int)profile_tbl[0])
      {
          if(mb_per_sec <= (int)profile_tbl[1])
          {
            if(m_sProfile.nBitrate <= (int)profile_tbl[2])
            {
              eLevel = (int)profile_tbl[3];
              eProfile = (int)profile_tbl[4];
              E("\n profile and level found \n");
              profile_level_found = true;
              break;
            }
          }
      }
      profile_tbl = profile_tbl + 5;
   }while(profile_tbl[0] != 0);

   if ( profile_level_found != true )
   {
     E("\n Error: Unsupported profile/level\n");
     return OMX_ErrorNone;
   }

   if (m_sProfile.eCodec == OMX_VIDEO_CodingH263)
   {
      D("Configuring H263...");

      OMX_VIDEO_PARAM_H263TYPE h263;
      result = OMX_GetParameter(m_hHandle,
                                OMX_IndexParamVideoH263,
                                &h263);
      CHK(result);
      h263.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
      h263.nPFrames = m_sProfile.nFramerate * 2 - 1; // intra period
      h263.nBFrames = 0;
      h263.eProfile = (OMX_VIDEO_H263PROFILETYPE)eProfile;
      h263.eLevel = (OMX_VIDEO_H263LEVELTYPE)eLevel;
      h263.bPLUSPTYPEAllowed = OMX_FALSE;
      h263.nAllowedPictureTypes = 2;
      h263.bForceRoundingTypeToZero = OMX_TRUE; ///@todo determine what this should be
      h263.nPictureHeaderRepetition = 0; ///@todo determine what this should be
      h263.nGOBHeaderInterval = 0; ///@todo determine what this should be
      result = OMX_SetParameter(m_hHandle,
                                OMX_IndexParamVideoH263,
                                &h263);
   }
   else
   {
      D("Configuring MP4/H264...");

      OMX_VIDEO_PARAM_PROFILELEVELTYPE profileLevel; // OMX_IndexParamVideoProfileLevelCurrent
      profileLevel.eProfile = eProfile;
      profileLevel.eLevel =  eLevel;
      result = OMX_SetParameter(m_hHandle,
                                OMX_IndexParamVideoProfileLevelCurrent,
                                &profileLevel);
      E("\n OMX_IndexParamVideoProfileLevelCurrent Set Paramter port");
      CHK(result);
      //profileLevel.eLevel = (OMX_U32) m_sProfile.eLevel;
      result = OMX_GetParameter(m_hHandle,
                                OMX_IndexParamVideoProfileLevelCurrent,
                                &profileLevel);
      E("\n OMX_IndexParamVideoProfileLevelCurrent Get Paramter port");
      D ("\n Profile = %d level = %d",profileLevel.eProfile,profileLevel.eLevel);
      CHK(result);

     /*OMX_VIDEO_PARAM_MPEG4TYPE mp4;

      result = OMX_GetParameter(m_hHandle,
                                OMX_IndexParamVideoMpeg4,
                                &mp4);
      E("\n OMX_IndexParamVideoMpeg4 Set Paramter port");
      CHK(result);

      mp4.nTimeIncRes = m_sProfile.nFramerate * 2;
      mp4.nPFrames = mp4.nTimeIncRes - 1; // intra period

      result = OMX_SetParameter(m_hHandle,
                                OMX_IndexParamVideoMpeg4,
                                &mp4);
      CHK(result);*/

//       OMX_VIDEO_PARAM_MPEG4TYPE mp4; // OMX_IndexParamVideoMpeg4
//       result = OMX_GetParameter(m_hHandle,
//                                 OMX_IndexParamVideoMpeg4,
//                                 &mp4);
//       CHK(result);
//       mp4.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
//       mp4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
//       mp4.eLevel = m_sProfile.eLevel;
//       mp4.nSliceHeaderSpacing = 0;
//       mp4.bSVH = OMX_FALSE;
//       mp4.bGov = OMX_FALSE;
//       mp4.nPFrames = m_sProfile.nFramerate * 2 - 1; // intra period
//       mp4.bACPred = OMX_TRUE;
//       mp4.nTimeIncRes = m_sProfile.nFramerate * 2; // delta = 2 @ 15 fps
//       mp4.nAllowedPictureTypes = 2; // pframe and iframe
//       result = OMX_SetParameter(m_hHandle,
//                                 OMX_IndexParamVideoMpeg4,
//                                 &mp4);
//       CHK(result);
   }

   if (m_sProfile.eCodec == OMX_VIDEO_CodingAVC)
   {
      OMX_VIDEO_PARAM_AVCSLICEFMO avcslicefmo;
      avcslicefmo.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
      result = OMX_GetParameter(m_hHandle,
                             OMX_IndexParamVideoSliceFMO,
                             &avcslicefmo);
      E("\n OMX_IndexParamVideoSliceFMO Get Paramter port");
      CHK(result);

      avcslicefmo.eSliceMode = m_sProfile.eSliceMode;
      result = OMX_SetParameter(m_hHandle,
                                OMX_IndexParamVideoSliceFMO,
                                &avcslicefmo);
      E("\n OMX_IndexParamVideoSliceFMO Set Paramter port");
      CHK(result);
   }

   OMX_VIDEO_PARAM_BITRATETYPE bitrate; // OMX_IndexParamVideoBitrate
   bitrate.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
   result = OMX_GetParameter(m_hHandle,
                             OMX_IndexParamVideoBitrate,
                             &bitrate);
   E("\n OMX_IndexParamVideoBitrate Get Paramter port");
   CHK(result);
   bitrate.eControlRate = m_sProfile.eControlRate;
   bitrate.nTargetBitrate = m_sProfile.nBitrate;
   result = OMX_SetParameter(m_hHandle,
                             OMX_IndexParamVideoBitrate,
                             &bitrate);
   E("\n OMX_IndexParamVideoBitrate Set Paramter port");
   CHK(result);

   OMX_VIDEO_PARAM_PORTFORMATTYPE framerate; // OMX_IndexParamVidePortFormat
   framerate.nPortIndex = 0;
   result = OMX_GetParameter(m_hHandle,
                             OMX_IndexParamVideoPortFormat,
                             &framerate);
   E("\n OMX_IndexParamVideoPortFormat Get Paramter port");
   CHK(result);
   framerate.xFramerate = m_sProfile.nFramerate << 16;
   result = OMX_SetParameter(m_hHandle,
                             OMX_IndexParamVideoPortFormat,
                             &framerate);
   E("\n OMX_IndexParamVideoPortFormat Set Paramter port");
   CHK(result);

#if 1
   OMX_CONFIG_FRAMERATETYPE enc_framerate; // OMX_IndexConfigVideoFramerate
   enc_framerate.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
   result = OMX_GetConfig(m_hHandle,
                          OMX_IndexConfigVideoFramerate,
                          &enc_framerate);
   E("\n OMX_IndexConfigVideoFramerate Get config port");
   CHK(result);
   enc_framerate.xEncodeFramerate = m_sProfile.nFramerate << 16;
   result = OMX_SetConfig(m_hHandle,
                          OMX_IndexConfigVideoFramerate,
                          &enc_framerate);
   E("\n OMX_IndexConfigVideoFramerate Set config port");
   CHK(result);
#endif
   return OMX_ErrorNone;
}
////////////////////////////////////////////////////////////////////////////////
void SendMessage(MsgId id, MsgData* data)
{
   pthread_mutex_lock(&m_mutex);
   if (m_sMsgQ.size >= MAX_MSG)
   {
      E("main msg m_sMsgQ is full");
      return;
   }
   m_sMsgQ.q[(m_sMsgQ.head + m_sMsgQ.size) % MAX_MSG].id = id;
   if (data)
      m_sMsgQ.q[(m_sMsgQ.head + m_sMsgQ.size) % MAX_MSG].data = *data;
   ++m_sMsgQ.size;
   pthread_cond_signal(&m_signal);
   pthread_mutex_unlock(&m_mutex);
}
////////////////////////////////////////////////////////////////////////////////
void PopMessage(Msg* msg)
{
   pthread_mutex_lock(&m_mutex);
   while (m_sMsgQ.size == 0)
   {
      pthread_cond_wait(&m_signal, &m_mutex);
   }
   *msg = m_sMsgQ.q[m_sMsgQ.head];
   --m_sMsgQ.size;
   m_sMsgQ.head = (m_sMsgQ.head + 1) % MAX_MSG;
   pthread_mutex_unlock(&m_mutex);
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EVT_CB(OMX_IN OMX_HANDLETYPE hComponent,
                     OMX_IN OMX_PTR pAppData,
                     OMX_IN OMX_EVENTTYPE eEvent,
                     OMX_IN OMX_U32 nData1,
                     OMX_IN OMX_U32 nData2,
                     OMX_IN OMX_PTR pEventData)
{
#define SET_STATE(eState)                                   \
   case eState:                                             \
      {                                                     \
         D("" # eState " complete");                        \
         m_eState = eState;                                 \
         break;                                             \
      }

   if (eEvent == OMX_EventCmdComplete)
   {
      if ((OMX_COMMANDTYPE) nData1 == OMX_CommandStateSet)
      {
         switch ((OMX_STATETYPE) nData2)
         {
            SET_STATE(OMX_StateLoaded);
            SET_STATE(OMX_StateIdle);
            SET_STATE(OMX_StateExecuting);
            SET_STATE(OMX_StateInvalid);
            SET_STATE(OMX_StateWaitForResources);
            SET_STATE(OMX_StatePause);
         default:
            E("invalid state %d", (int) nData2);
          }
      }
   }

   else if (eEvent == OMX_EventError)
   {
      E("OMX_EventError");
   }

   else
   {
      E("unexpected event %d", (int) eEvent);
   }
   return OMX_ErrorNone;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE EBD_CB(OMX_IN OMX_HANDLETYPE hComponent,
                     OMX_IN OMX_PTR pAppData,
                     OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
   D("Got EBD callback ts=%lld", pBuffer->nTimeStamp);

   for (int i = 0; i < num_in_buffers; i++)
   {
      // mark this buffer ready for use again
      if (m_pInBuffers[i] == pBuffer)
      {

         D("Marked input buffer idx %d as free, buf %p", i, pBuffer->pBuffer);
         m_bInFrameFree[i] = OMX_TRUE;
         break;
      }
   }

   if (m_eMode == MODE_LIVE_ENCODE)
   {
      CameraTest_ReleaseFrame(pBuffer->pBuffer,
                              ((OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO*)pBuffer->pAppPrivate));
   }
   else
   {
      // wake up main thread and tell it to send next frame
      MsgData data;
      data.sBitstreamData.pBuffer = pBuffer;
      SendMessage(MSG_ID_INPUT_FRAME_DONE,
                  &data);

   }
   return OMX_ErrorNone;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE FBD_CB(OMX_OUT OMX_HANDLETYPE hComponent,
                     OMX_OUT OMX_PTR pAppData,
                     OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer)
{
   D("Got FBD callback ts=%lld", pBuffer->nTimeStamp);

   static long long prevTime = 0;
   long long currTime = GetTimeStamp();

   m_bWatchDogKicked = true;

   /* Empty Buffers should not be counted */
   if(pBuffer->nFilledLen !=0)
   {
      /* Counting Buffers supplied from OpneMax Encoder */
      fbd_cnt++;
      tot_bufsize += pBuffer->nFilledLen;
   }
   if (prevTime != 0)
   {
      long long currTime = GetTimeStamp();
      D("FBD_DELTA = %lld\n", currTime - prevTime);
   }
   prevTime = currTime;

   if (m_eMode == MODE_PROFILE)
   {
      // if we are profiling we are not doing file I/O
      // so just give back to encoder
      if (OMX_FillThisBuffer(m_hHandle, pBuffer) != OMX_ErrorNone)
      {
         E("empty buffer failed for profiling");
      }
   }
   else
   {
      // wake up main thread and tell it to write to file
      MsgData data;
      data.sBitstreamData.pBuffer = pBuffer;
      SendMessage(MSG_ID_OUTPUT_FRAME_DONE,
                  &data);
   }
   return OMX_ErrorNone;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE VencTest_Initialize()
{
   OMX_ERRORTYPE result = OMX_ErrorNone;
   static OMX_CALLBACKTYPE sCallbacks = {EVT_CB, EBD_CB, FBD_CB};
   int i;

   for (i = 0; i < num_in_buffers; i++)
   {
      m_pInBuffers[i] = NULL;
   }

   result = OMX_Init();
   CHK(result);

   if (m_sProfile.eCodec == OMX_VIDEO_CodingMPEG4)
   {
        result = OMX_GetHandle(&m_hHandle,
                             "OMX.qcom.video.encoder.mpeg4",
                             NULL,
                             &sCallbacks);
     // CHK(result);
   }
   else if (m_sProfile.eCodec == OMX_VIDEO_CodingH263)
   {
      result = OMX_GetHandle(&m_hHandle,
                             "OMX.qcom.video.encoder.h263",
                             NULL,
                             &sCallbacks);
      CHK(result);
   }
   else
   {
      result = OMX_GetHandle(&m_hHandle,
                             "OMX.qcom.video.encoder.avc",
                             NULL,
                             &sCallbacks);
      CHK(result);
   }


   result = ConfigureEncoder();
   CHK(result);

   return result;
}

////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE VencTest_RegisterYUVBuffer(OMX_BUFFERHEADERTYPE** ppBufferHeader,
                                         OMX_U8 *pBuffer,
                                         OMX_PTR pAppPrivate)
{
   OMX_ERRORTYPE result = OMX_ErrorNone;
#if 0
   D("register buffer");
   if ((result = OMX_AllocateBuffer(m_hHandle,
                               ppBufferHeader,
                               (OMX_U32) PORT_INDEX_IN,
                               pAppPrivate,
                               m_sProfile.nFrameBytes
                               )) != OMX_ErrorNone)
   {
      E("use buffer failed");
   }
   else
   {
     E("Allocate Buffer Success %x", (*ppBufferHeader)->pBuffer);
   }
  #endif
   D("register buffer");
   D("Calling UseBuffer for Input port");
   if ((result = OMX_UseBuffer(m_hHandle,
                               ppBufferHeader,
                               (OMX_U32) PORT_INDEX_IN,
                               pAppPrivate,
                               m_sProfile.nFrameBytes,
                               pBuffer)) != OMX_ErrorNone)
   {
      E("use buffer failed");
   }

   return result;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE VencTest_EncodeFrame(void* pYUVBuff,
                                   long long nTimeStamp)
{
   OMX_ERRORTYPE result = OMX_ErrorUndefined;
   D("calling OMX empty this buffer");
   for (int i = 0; i < num_in_buffers; i++)
   {
      if (pYUVBuff == m_pInBuffers[i]->pBuffer)
      {
         m_pInBuffers[i]->nTimeStamp = nTimeStamp;
    D("Sending Buffer - %x", m_pInBuffers[i]->pBuffer);
         result = OMX_EmptyThisBuffer(m_hHandle,
                                      m_pInBuffers[i]);
         /* Counting Buffers supplied to OpenMax Encoder */
         if(OMX_ErrorNone == result)
            ebd_cnt++;
         CHK(result);
         break;
      }
   }
   return result;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE VencTest_Exit(void)
{
   int i;
   OMX_ERRORTYPE result = OMX_ErrorNone;
   D("trying to exit venc");

   D("going to idle state");
   SetState(OMX_StateIdle);


   D("going to loaded state");
   //SetState(OMX_StateLoaded);
      OMX_SendCommand(m_hHandle,
                      OMX_CommandStateSet,
                      (OMX_U32) OMX_StateLoaded,
                       NULL);

      for (i = 0; i < num_in_buffers; i++)
   {
      D("free buffer");
      if (m_pInBuffers[i]->pBuffer)
      {
        // free(m_pInBuffers[i]->pBuffer);
         result = OMX_FreeBuffer(m_hHandle,
                                 PORT_INDEX_IN,
                                 m_pInBuffers[i]);
         CHK(result);
      }
      else
      {
         E("buffer %d is null", i);
         result = OMX_ErrorUndefined;
         CHK(result);
      }
   }
   for (i = 0; i < num_out_buffers; i++)
   {
      D("free buffer");
      if (m_pOutBuffers[i]->pBuffer)
      {
         free(m_pOutBuffers[i]->pBuffer);
         result = OMX_FreeBuffer(m_hHandle,
                                 PORT_INDEX_OUT,
                                 m_pOutBuffers[i]);
         CHK(result);

      }
      else
      {
         E("buffer %d is null", i);
         result = OMX_ErrorUndefined;
         CHK(result);
      }
   }

     while (m_eState != OMX_StateLoaded)
     {
        sleep(1);
     }
   D("component_deinit...");
   result = OMX_Deinit();
   CHK(result);

   D("venc is exiting...");
   return result;
}
////////////////////////////////////////////////////////////////////////////////
OMX_ERRORTYPE VencTest_ReadAndEmpty(OMX_BUFFERHEADERTYPE* pYUVBuffer)
{
   OMX_ERRORTYPE result = OMX_ErrorNone;
#ifdef T_ARM
   if (read(m_nInFd,
            pYUVBuffer->pBuffer,
            m_sProfile.nFrameBytes) != m_sProfile.nFrameBytes)
   {
      return OMX_ErrorUndefined;
   }
#else
   {
	  char * pInputbuf = (char *)(pYUVBuffer->pBuffer) ;
	      read(m_nInFd,pInputbuf,m_sProfile.nFrameBytes) ;

   }
#endif
   D("about to call VencTest_EncodeFrame...");
   pthread_mutex_lock(&m_mutex);
   ++m_nFrameIn;
   pYUVBuffer->nFilledLen = m_sProfile.nFrameBytes;
   D("Called Buffer with Data filled length %d",pYUVBuffer->nFilledLen);

      result = VencTest_EncodeFrame(pYUVBuffer->pBuffer,
                                 m_nTimeStamp);

   m_nTimeStamp += (1000000) / m_sProfile.nFramerate;
   CHK(result);
   pthread_mutex_unlock(&m_mutex);
   return result;
}
////////////////////////////////////////////////////////////////////////////////
void PreviewCallback(int nFD,
                     int nOffset,
                     void* pPhys,
                     void* pVirt,
                     long long nTimeStamp)
{

   D("================= preview frame %d, phys=0x%x, nTimeStamp(millis)=%lld",
     m_nFrameIn+1, pPhys, (nTimeStamp / 1000));

   if (m_nFrameIn == m_nFramePlay &&
       m_nFramePlay != 0)
   {
      // we will stop camera after last frame is encoded.
      // for now just ignore input frames

      CameraTest_ReleaseFrame(pPhys, pVirt);
      return;
   }

   // see if we should stop
   pthread_mutex_lock(&m_mutex);
   ++m_nFrameIn;
   pthread_mutex_unlock(&m_mutex);


   if (m_eMode == MODE_LIVE_ENCODE)
   {

      OMX_ERRORTYPE result;

      // register new camera buffers with encoder
      int i;
      for (i = 0; i < num_in_buffers; i++)
      {
         if (m_pInBuffers[i] != NULL &&
             m_pInBuffers[i]->pBuffer == pPhys)
         {
            break;
         }
         else if (m_pInBuffers[i] == NULL)
         {
            D("registering buffer...");
            result = VencTest_RegisterYUVBuffer(&m_pInBuffers[i],
                                                (OMX_U8*) pPhys,
                                                (OMX_PTR) pVirt); // store virt in app private field
            D("register done");
            CHK(result);
            break;
         }
      }

      if (i == num_in_buffers)
      {
         E("There are more camera buffers than we thought");
         CHK(1);
      }

      // encode the yuv frame

      D("StartEncodeTime=%lld", GetTimeStamp());
      result = VencTest_EncodeFrame(pPhys,
                                    nTimeStamp);
      CHK(result);
     // FBTest_DisplayImage(nFD, nOffset);
   }
   else
   {
     // FBTest_DisplayImage(nFD, nOffset);
      CameraTest_ReleaseFrame(pPhys, pVirt);
   }
}
////////////////////////////////////////////////////////////////////////////////
void usage(char* filename)
{
   char* fname = strrchr(filename, (int) '/');
   fname = (fname == NULL) ? filename : fname;

   fprintf(stderr, "usage: %s LIVE <QCIF|QVGA> <MP4|H263> <FPS> <BITRATE> <NFRAMES> <OUTFILE>\n", fname);
   fprintf(stderr, "usage: %s FILE <QCIF|QVGA> <MP4|H263 <FPS> <BITRATE> <NFRAMES> <INFILE> <OUTFILE> ", fname);
   fprintf(stderr, "<Rate Control - optional> <AVC Slice Mode - optional\n", fname);
   fprintf(stderr, "usage: %s PROFILE <QCIF|QVGA> <MP4|H263 <FPS> <BITRATE> <NFRAMES> <INFILE>\n", fname);
   fprintf(stderr, "usage: %s PREVIEW <QCIF|QVGA> <FPS> <NFRAMES>\n", fname);
   fprintf(stderr, "usage: %s DISPLAY <QCIF|QVGA> <FPS> <NFRAMES> <INFILE>\n", fname);
   fprintf(stderr, "\n       BITRATE - bitrate in kbps\n");
   fprintf(stderr, "       FPS - frames per second\n");
   fprintf(stderr, "       NFRAMES - number of frames to play, 0 for infinite\n");
   fprintf(stderr, "       RateControl (Values 0 - 4 for RC_OFF, RC_CBR_CFR, RC_CBR_VFR, RC_VBR_CFR, RC_VBR_VFR\n");
   exit(1);
}
////////////////////////////////////////////////////////////////////////////////
void parseArgs(int argc, char** argv)
{

   if (argc == 1)
   {
      usage(argv[0]);
   }
   else if (strcmp("PREVIEW", argv[1]) == 0 ||
            strcmp("preview", argv[1]) == 0)
   {
      m_eMode = MODE_PREVIEW;
      if (argc != 5)
      {
         usage(argv[0]);
      }
   }
   else if (strcmp("DISPLAY", argv[1]) == 0 ||
            strcmp("display", argv[1]) == 0)
   {
      m_eMode = MODE_DISPLAY;
      if (argc != 6)
      {
         usage(argv[0]);
      }
      m_sProfile.cInFileName = argv[5];
      m_sProfile.cOutFileName = NULL;
   }
   else if (strcmp("LIVE", argv[1]) == 0 ||
            strcmp("live", argv[1]) == 0)
   {//263
      m_eMode = MODE_LIVE_ENCODE;
      if (argc != 8)
      {
         usage(argv[0]);
      }
      m_sProfile.cInFileName = NULL;
      m_sProfile.cOutFileName = argv[7];
   }
   else if (strcmp("FILE", argv[1]) == 0 ||
            strcmp("file", argv[1]) == 0)
   {//263
      m_eMode = MODE_FILE_ENCODE;

      if(argc < 9 || argc > 11)
      {
          usage(argv[0]);
      }
      else
      {
         if ((argc == 10))
         {
           m_sProfile.eControlRate = OMX_Video_ControlRateVariable;
            int RC = atoi(argv[9]);

            switch (RC)
            {
            case 0:
               m_sProfile.eControlRate  = OMX_Video_ControlRateDisable ;//VENC_RC_NONE
               break;
            case 1:
               m_sProfile.eControlRate  = OMX_Video_ControlRateConstant;//VENC_RC_CBR_CFR
               break;

            case 2:
               m_sProfile.eControlRate  = OMX_Video_ControlRateConstantSkipFrames;//VENC_RC_CBR_VFR
               break;

            case 3:
               m_sProfile.eControlRate  =OMX_Video_ControlRateVariable ;//VENC_RC_VBR_CFR
               break;

            case 4:
               m_sProfile.eControlRate  = OMX_Video_ControlRateVariableSkipFrames;//VENC_RC_VBR_VFR
               break;

           default:
               E("invalid rate control selection");
               m_sProfile.eControlRate = OMX_Video_ControlRateVariable; //VENC_RC_VBR_CFR
               break;
            }
         }

         if (argc == 11)
         {
            if(!strcmp(argv[3], "H264") || !strcmp(argv[3], "h264"))
            {
               E("\nSetting AVCSliceMode ... ");
               int AVCSliceMode = atoi(argv[10]);
               switch(AVCSliceMode)
               {
               case 0:
                  m_sProfile.eSliceMode = OMX_VIDEO_SLICEMODE_AVCDefault;
                  break;

               case 1:
                  m_sProfile.eSliceMode = OMX_VIDEO_SLICEMODE_AVCMBSlice;
                  break;

               case 2:
                  m_sProfile.eSliceMode = OMX_VIDEO_SLICEMODE_AVCByteSlice;
                  break;

               default:
                  E("invalid Slice Mode");
                  m_sProfile.eSliceMode = OMX_VIDEO_SLICEMODE_AVCDefault;
                  break;
              }
            }
            else
            {
               E("SliceMode support only for H.264 codec");
               usage(argv[0]);
            }
         }
      }
      m_sProfile.cInFileName = argv[7];
      m_sProfile.cOutFileName = argv[8];
   }
   else if (strcmp("PROFILE", argv[1]) == 0 ||
            strcmp("profile", argv[1]) == 0)
   {//263
      m_eMode = MODE_PROFILE;
      if (argc != 8)
      {
         usage(argv[0]);
      }
      m_sProfile.cInFileName = argv[7];
      m_sProfile.cOutFileName = NULL;
   }
   else
   {
      usage(argv[0]);
   }


   if (strcmp("QCIF", argv[2]) == 0 ||
       strcmp("qcif", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 176;
      m_sProfile.nFrameHeight = 144;
      m_sProfile.nFrameBytes = 176*144*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level0;
   }
   else if (strcmp("QVGA", argv[2]) == 0 ||
            strcmp("qvga", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 320;
      m_sProfile.nFrameHeight = 240;
      m_sProfile.nFrameBytes = 320*240*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level1;
   }


    else if (strcmp("VGA", argv[2]) == 0 ||
            strcmp("vga", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 640;
      m_sProfile.nFrameHeight = 480;
      m_sProfile.nFrameBytes = 640*480*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level1;
   }

    else if (strcmp("WVGA", argv[2]) == 0 ||
            strcmp("wvga", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 800;
      m_sProfile.nFrameHeight = 480;
      m_sProfile.nFrameBytes = 800*480*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level1;
   }
  else if (strcmp("CIF", argv[2]) == 0 ||
            strcmp("CIF", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 352;
      m_sProfile.nFrameHeight = 288;
      m_sProfile.nFrameBytes = 352*288*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level1;
   }
   else if (strcmp("720", argv[2]) == 0 ||
            strcmp("720", argv[2]) == 0)
   {
      m_sProfile.nFrameWidth = 1280;
      m_sProfile.nFrameHeight = 720;
      m_sProfile.nFrameBytes = 720*1280*3/2;
      m_sProfile.eLevel = OMX_VIDEO_MPEG4Level1;
   }
   else
   {
      usage(argv[0]);
   }

   if (m_eMode == MODE_DISPLAY ||
       m_eMode == MODE_PREVIEW)
   {
      m_sProfile.nFramerate = atoi(argv[3]);
      m_nFramePlay = atoi(argv[4]);

   }
   else if (m_eMode == MODE_LIVE_ENCODE ||
            m_eMode == MODE_FILE_ENCODE ||
            m_eMode == MODE_PROFILE)
   {//263
      if ((!strcmp(argv[3], "MP4")) || (!strcmp(argv[3], "mp4")))
      {
         m_sProfile.eCodec = OMX_VIDEO_CodingMPEG4;
      }
      else if ((!strcmp(argv[3], "H263")) || (!strcmp(argv[3], "h263")))
      {
         m_sProfile.eCodec = OMX_VIDEO_CodingH263;
      }
      else if ((!strcmp(argv[3], "H264")) || (!strcmp(argv[3], "h264")))
      {
         m_sProfile.eCodec = OMX_VIDEO_CodingAVC;
      }
      else
      {
         usage(argv[0]);
      }

      m_sProfile.nFramerate = atoi(argv[4]);
      m_sProfile.nBitrate = atoi(argv[5]);
//      m_sProfile.eControlRate = OMX_Video_ControlRateVariable;
      m_nFramePlay = atoi(argv[6]);
   }
}


void* Watchdog(void* data)
{
   while (1)
   {
      sleep(1000);
      if (m_bWatchDogKicked == true)
         m_bWatchDogKicked = false;
      else
         E("watchdog has not been kicked. we may have a deadlock");
   }
   return NULL;
}

int main(int argc, char** argv)
{
   OMX_U8* pvirt = NULL;
   int result;
   float enc_time_sec=0.0,enc_time_usec=0.0;

   m_nInFd = -1;
   m_nOutFd = -1;
   m_nTimeStamp = 0;
   m_nFrameIn = 0;
   m_nFrameOut = 0;

   memset(&m_sMsgQ, 0, sizeof(MsgQ));
   parseArgs(argc, argv);

   D("fps=%d, bitrate=%d, width=%d, height=%d",
     m_sProfile.nFramerate,
     m_sProfile.nBitrate,
     m_sProfile.nFrameWidth,
     m_sProfile.nFrameHeight);


   //if (m_eMode != MODE_PREVIEW && m_eMode != MODE_DISPLAY)
   //{
     // pthread_t wd;
     // pthread_create(&wd, NULL, Watchdog, NULL);
   //}

   for (int x = 0; x < num_in_buffers; x++)
   {
      // mark all buffers as ready to use
      m_bInFrameFree[x] = OMX_TRUE;
   }


    if (m_eMode != MODE_PROFILE)
   {
      #if T_ARM
	   m_nOutFd = open(m_sProfile.cOutFileName, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
      #else
	  m_nOutFd = open(m_sProfile.cOutFileName,0);
      #endif
      if (m_nOutFd < 0)
      {
         E("could not open output file %s", m_sProfile.cOutFileName);
         CHK(1);
      }
   }

   pthread_mutex_init(&m_mutex, NULL);
   pthread_cond_init(&m_signal, NULL);

   if (m_eMode != MODE_PREVIEW)
   {
      VencTest_Initialize();
   }

   ////////////////////////////////////////
   // Camera + Encode
   ////////////////////////////////////////
   if (m_eMode == MODE_LIVE_ENCODE)
   {
     CameraTest_Initialize(m_sProfile.nFramerate,
                            m_sProfile.nFrameWidth,
                            m_sProfile.nFrameHeight,
                            PreviewCallback);
      CameraTest_Run();
   }

   if (m_eMode == MODE_FILE_ENCODE ||
       m_eMode == MODE_PROFILE)
   {
      int i;
      #if T_ARM
      m_nInFd = open(m_sProfile.cInFileName, O_RDONLY);
      #else
      m_nInFd = open(m_sProfile.cInFileName,1);
      #endif
	  if (m_nInFd < 0)
      {
         E("could not open input file");
         CHK(1);
      }
      D("going to idle state");
      //SetState(OMX_StateIdle);
      OMX_SendCommand(m_hHandle,
                      OMX_CommandStateSet,
                      (OMX_U32) OMX_StateIdle,
                       NULL);

      OMX_PARAM_PORTDEFINITIONTYPE portDef;

      portDef.nPortIndex = 0;
      result = OMX_GetParameter(m_hHandle, OMX_IndexParamPortDefinition, &portDef);
      CHK(result);

      D("allocating output buffers");

      D("allocating Input buffers");
      num_in_buffers = portDef.nBufferCountActual;
      for (i = 0; i < portDef.nBufferCountActual; i++)
      {
         OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* pMem = new OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO;
         pvirt = (OMX_U8*)PmemMalloc(pMem, m_sProfile.nFrameBytes);

         if(pvirt == NULL)
         {
            CHK(1);
         }
         result = VencTest_RegisterYUVBuffer(&m_pInBuffers[i],
                                             (OMX_U8*) pvirt,
                                             (OMX_PTR) pMem);
         CHK(result);
      }
   }
   else if (m_eMode == MODE_LIVE_ENCODE)
   {
       D("going to idle state");
       //SetState(OMX_StateIdle);
       OMX_SendCommand(m_hHandle,
                       OMX_CommandStateSet,
                       (OMX_U32) OMX_StateIdle,
                        NULL);
   }

   int i;
   OMX_PARAM_PORTDEFINITIONTYPE portDef;

   portDef.nPortIndex = 1;
   result = OMX_GetParameter(m_hHandle, OMX_IndexParamPortDefinition, &portDef);
   CHK(result);

   D("allocating output buffers");
   D("Calling UseBuffer for Output port");
   num_out_buffers = portDef.nBufferCountActual;
   for (i = 0; i < portDef.nBufferCountActual; i++)
   {
      void* pBuff;

      pBuff = malloc(portDef.nBufferSize);
     D("portDef.nBufferSize = %d ",portDef.nBufferSize);
      result = OMX_UseBuffer(m_hHandle,
                             &m_pOutBuffers[i],
                             (OMX_U32) PORT_INDEX_OUT,
                             NULL,
                             portDef.nBufferSize,
                             (OMX_U8*) pBuff);
      CHK(result);
   }
   D("allocate done");

        // D("Going to state " # eState"...");

         while (m_eState != OMX_StateIdle)
         {
            sleep(1);
         }
         //D("Now in state " # eState);


   D("going to executing state");
   SetState(OMX_StateExecuting);

   for (i = 0; i < num_out_buffers; i++)
   {
      D("filling buffer %d", i);
      result = OMX_FillThisBuffer(m_hHandle, m_pOutBuffers[i]);
      //sleep(1000);
      CHK(result);
   }

   if (m_eMode == MODE_FILE_ENCODE)
   {
      // encode the first frame to kick off the whole process
      VencTest_ReadAndEmpty(m_pInBuffers[0]);
    //  FBTest_DisplayImage(((PmemBuffer*) m_pInBuffers[0]->pAppPrivate)->fd,0);
   }

   if (m_eMode == MODE_PROFILE)
   {
      int i;

      // read several frames into memory
      D("reading frames into memory");
      for (i = 0; i < num_in_buffers; i++)
      {
        D("[%d] address 0x%x",i, m_pInBuffers[i]->pBuffer);
         read(m_nInFd,
              m_pInBuffers[i]->pBuffer,
              m_sProfile.nFrameBytes);

      }

     // FBTest_Initialize(m_sProfile.nFrameWidth, m_sProfile.nFrameHeight);

      // loop over the mem-resident frames and encode them
      D("beging playing mem-resident frames...");
      for (i = 0; m_nFramePlay == 0 || i < m_nFramePlay; i++)
      {
         int idx = i % num_in_buffers;
         if (m_bInFrameFree[idx] == OMX_FALSE)
         {
            int j;
            E("the expected buffer is not free, but lets find another");

            idx = -1;

            // lets see if we can find another free buffer
            for (j = 0; j < num_in_buffers; j++)
            {
               if(m_bInFrameFree[j])
               {
                  idx = j;
                  break;
               }
            }
         }

         // if we have a free buffer let's encode it
         if (idx >= 0)
         {
            D("encode frame %d...m_pInBuffers[idx]->pBuffer=0x%x", i,m_pInBuffers[idx]->pBuffer);
            m_bInFrameFree[idx] = OMX_FALSE;
            VencTest_EncodeFrame(m_pInBuffers[idx]->pBuffer,
                                 m_nTimeStamp);
            D("display frame %d...", i);
        //    FBTest_DisplayImage(((PmemBuffer*) m_pInBuffers[idx]->pAppPrivate)->fd,0);
            m_nTimeStamp += 1000000 / m_sProfile.nFramerate;
         }
         else
         {
            E("wow, no buffers are free, performance "
              "is not so good. lets just sleep some more");

         }
         D("sleep for %d microsec", 1000000/m_sProfile.nFramerate);
         sleep (1000000 / m_sProfile.nFramerate);
      }
     // FBTest_Exit();
   }

   Msg msg;
   bool bQuit = false;
   while ((m_eMode == MODE_FILE_ENCODE || m_eMode == MODE_LIVE_ENCODE) &&
          !bQuit)
   {
      PopMessage(&msg);
      switch (msg.id)
      {
      //////////////////////////////////
      // FRAME IS ENCODED
      //////////////////////////////////
      case MSG_ID_INPUT_FRAME_DONE:
         /*pthread_mutex_lock(&m_mutex);
         ++m_nFrameOut;
         if (m_nFrameOut == m_nFramePlay && m_nFramePlay != 0)
         {
            bQuit = true;
         }
         pthread_mutex_unlock(&m_mutex);*/

         if (!bQuit && m_eMode == MODE_FILE_ENCODE)
         {
            D("pushing another frame down to encoder");
            if (VencTest_ReadAndEmpty(msg.data.sBitstreamData.pBuffer))
            {
               // we have read the last frame
               D("main is exiting...");
               bQuit = true;
            }
         }
       break;
      case MSG_ID_OUTPUT_FRAME_DONE:
         D("================ writing frame %d = %d bytes to output file",
           m_nFrameOut+1,
           msg.data.sBitstreamData.pBuffer->nFilledLen);
         D("StopEncodeTime=%lld", GetTimeStamp());


		 write(m_nOutFd,
               msg.data.sBitstreamData.pBuffer->pBuffer,
               msg.data.sBitstreamData.pBuffer->nFilledLen);


         result = OMX_FillThisBuffer(m_hHandle,
                                     msg.data.sBitstreamData.pBuffer);

         if (result != OMX_ErrorNone)
         {
            CHK(result);
         }

         pthread_mutex_lock(&m_mutex);
         ++m_nFrameOut;
         if (m_nFrameOut == m_nFramePlay && m_nFramePlay != 0)
         {
            bQuit = true;
         }
         pthread_mutex_unlock(&m_mutex);
         break;

      default:
         E("invalid msg id %d", (int) msg.id);
      } // end switch (msg.id)
   } // end while (!bQuit)


   if (m_eMode == MODE_LIVE_ENCODE)
   {
      CameraTest_Exit();
      close(m_nOutFd);
   }
   else if (m_eMode == MODE_FILE_ENCODE ||
            m_eMode == MODE_PROFILE)
   {
      // deallocate pmem buffers
      for (int i = 0; i < num_in_buffers; i++)
      {
         PmemFree((OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO*)m_pInBuffers[i]->pAppPrivate,
                  m_pInBuffers[i]->pBuffer,
                  m_sProfile.nFrameBytes);
         delete (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO*) m_pInBuffers[i]->pAppPrivate;
      }
      close(m_nInFd);

      if (m_eMode == MODE_FILE_ENCODE)
      {
         close(m_nOutFd);
      }
   }

   if (m_eMode != MODE_PREVIEW)
   {
      D("exit encoder test");
      VencTest_Exit();
   }

   pthread_mutex_destroy(&m_mutex);
   pthread_cond_destroy(&m_signal);

   /* Time Statistics Logging */
   if(0 != m_sProfile.nFramerate)
   {
      enc_time_usec = m_nTimeStamp - (1000000 / m_sProfile.nFramerate);
      enc_time_sec =enc_time_usec/1000000;
      if(0 != enc_time_sec)
      {
         printf("Total Frame Rate: %f",ebd_cnt/enc_time_sec);
         printf("\nEncoder Bitrate :%lf Kbps",(tot_bufsize*8)/(enc_time_sec*1000));
      }
   }
   else
   {
      printf("\n\n Encode Time is zero");
   }
   printf("\nTotal Number of Frames :%d",ebd_cnt);
   printf("\nNumber of dropped frames during encoding:%d\n",ebd_cnt-fbd_cnt);
   /* End of Time Statistics Logging */

   D("main has exited");
   return 0;
}
