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

  @file OMX_Venc.cpp
  This module contains the implementation of the OpenMAX core & component.

=========================================================================*/

/*----------------------------------------------------------------------------
* Include Files
* -------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/android_pmem.h>
#include <fcntl.h>

#include "OMX_Venc.h"

#include "OMX_VencBufferManager.h"
#include "OMX_VencMsgQ.h"
#include "qc_omx_common.h"
#include "venc_debug.h"

// #include "Diag_LSM.h"

#define DeviceIoControl ioctl
/*----------------------------------------------------------------------------
* Preprocessor Definitions and Constants
* -------------------------------------------------------------------------*/

#define OMX_SPEC_VERSION 0x00000101
#define OMX_INIT_STRUCT(_s_, _name_)            \
   memset((_s_), 0x0, sizeof(_name_));          \
   (_s_)->nSize = sizeof(_name_);               \
   (_s_)->nVersion.nVersion = OMX_SPEC_VERSION

#define BITS_SHIFT(index) 1 << index
#define BITMASK_SET(val, index) (val |= (1 << index))
#define BITMASK_CLEAR(val, index) (val &= (OMX_U32) ~(1 << index))
#define BITMASK_PRESENT(val, index) (val & (1 << index))

static const char pRoleMPEG4[] = "video_encoder.mpeg4";
static const char pRoleH263[] = "video_encoder.h263";
static const char pRoleAVC[] = "video_encoder.avc";
/*----------------------------------------------------------------------------
* Type Declarations
* -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
* Global Data Definitions
* -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
* Static Variable Definitions
* -------------------------------------------------------------------------*/
Venc* Venc::g_pVencInstance = NULL;

#define VEN_QCIF_DX                 176
#define VEN_QCIF_DY                 144

#define VEN_CIF_DX                  352
#define VEN_CIF_DY                  288

#define VEN_VGA_DX                  640
#define VEN_VGA_DY                  480

#define VEN_PAL_DX                  720
#define VEN_PAL_DY                  576

#define VEN_WQVGA_DX                400
#define VEN_WVQGA_DY                240

#define VEN_D1_DX                   720
#define VEN_D1_DY                   480

#define VEN_WVGA_DX                 800
#define VEN_WVGA_DY                 480

#define VEN_HD720P_DX               1280
#define VEN_HD720P_DY               720

#define VEN_FRAME_SIZE_IN_RANGE(w1, h1, w2, h2)  \
    ((w1) * (h1) <= (w2) * (h2))

/*----------------------------------------------------------------------------
* Static Function Declarations and Definitions
* -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
* Externalized Function Definitions
* -------------------------------------------------------------------------*/
extern "C" {
   void* get_omx_component_factory_fn(void)
   {
     return Venc::get_instance();
   }
}
#define GetLastError() 1
typedef struct OMXComponentCapabilityFlagsType
{
  ////////////////// OMX COMPONENT CAPABILITY RELATED MEMBERS
  OMX_BOOL iIsOMXComponentMultiThreaded;
  OMX_BOOL iOMXComponentSupportsExternalOutputBufferAlloc;
  OMX_BOOL iOMXComponentSupportsExternalInputBufferAlloc;
  OMX_BOOL iOMXComponentSupportsMovableInputBuffers;
  OMX_BOOL iOMXComponentSupportsPartialFrames;
  OMX_BOOL iOMXComponentUsesNALStartCodes;
  OMX_BOOL iOMXComponentCanHandleIncompleteFrames;
  OMX_BOOL iOMXComponentUsesFullAVCFrames;

} OMXComponentCapabilityFlagsType;
#define OMX_COMPONENT_CAPABILITY_TYPE_INDEX 0xFF7A347
///////////////////////////////////////////////////////////////////////////////
// Refer to the header file for detailed method descriptions
///////////////////////////////////////////////////////////////////////////////
Venc::Venc() :
   m_ComponentThread(NULL),
   m_ReaderThread(NULL),
   m_pInBufferMgr(NULL),
   m_pOutBufferMgr(NULL),
   m_pMsgQ(NULL),
   m_pComponentName(NULL),
   m_pDevice(NULL),
   m_nInBuffAllocated(0),
   m_nOutBuffAllocated(0)
{

  QC_OMX_MSG_HIGH("constructor (opening driver)");
  memset(&m_sCallbacks, 0, sizeof(m_sCallbacks));

  m_nFd = ven_device_open(&m_pDevice);

  memset(&m_pPrivateInPortData, 0, sizeof(m_pPrivateInPortData));
  memset(&m_pPrivateOutPortData, 0, sizeof(m_pPrivateOutPortData));
  memset(&m_sErrorCorrection, 0, sizeof(m_sErrorCorrection));
  sem_init(&m_cmd_lock,0,0);
}

Venc::~Venc()
{
  g_pVencInstance = NULL;
  QC_OMX_MSG_HIGH("deconstructor (closing driver)");
  sem_destroy(&m_cmd_lock);
  ven_device_close(m_pDevice);
}

int roundingup( double val )
{
   int ret = (int) val;
   val -= ret;
   if(val >= 0.5)
      return (int) ++ret;
   else
      return ret;
}

OMX_ERRORTYPE Venc::component_init(OMX_IN OMX_STRING pComponentName)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  OMX_BOOL bCThread = OMX_FALSE;
  OMX_BOOL bRThread = OMX_FALSE;
  OMX_BOOL bMutex = OMX_FALSE;
  OMX_VIDEO_CODINGTYPE eCodec;

  int nNameLen = (int) strlen(pComponentName);

  QC_OMX_MSG_LOW("initializing component");
  if (pComponentName && nNameLen < OMX_MAX_STRINGNAME_SIZE)
  {
    if (strcmp(pComponentName, "OMX.qcom.video.encoder.mpeg4") == 0)
    {
      m_pComponentName = (OMX_STRING) malloc(OMX_MAX_STRINGNAME_SIZE);
      memcpy(m_cRole, pRoleMPEG4, strlen(pRoleMPEG4) + 1);
      memcpy(m_pComponentName, pComponentName, nNameLen);
      eCodec = OMX_VIDEO_CodingMPEG4;
    }
    else if (strcmp(pComponentName, "OMX.qcom.video.encoder.h263") == 0)
    {
      m_pComponentName = (OMX_STRING) malloc(OMX_MAX_STRINGNAME_SIZE);
      memcpy(m_cRole, pRoleH263, strlen(pRoleH263) + 1);
      memcpy(m_pComponentName, pComponentName, nNameLen);
      eCodec = OMX_VIDEO_CodingH263;
    }
    else if (strcmp(pComponentName, "OMX.qcom.video.encoder.avc") == 0)
    {
      m_pComponentName = (OMX_STRING) malloc(OMX_MAX_STRINGNAME_SIZE);
      memcpy(m_cRole, pRoleAVC, strlen(pRoleAVC) + 1);
      memcpy(m_pComponentName, pComponentName, nNameLen);
      eCodec = OMX_VIDEO_CodingAVC;
    }
    else
    {
      return OMX_ErrorBadParameter;
    }
  }
  else
  {
    return OMX_ErrorBadParameter;
  }

  QC_OMX_MSG_HIGH("initializing component codec=%d ", (int) m_pComponentName);

  /// allocate message queue
  m_pMsgQ = new VencMsgQ;
  if (m_pMsgQ == NULL)
  {
    QC_OMX_MSG_ERROR("failed to allocate message queue");
    result = OMX_ErrorInsufficientResources;
    goto bail;
  }
  m_eState = OMX_StateLoaded;

  OMX_INIT_STRUCT(&m_sPortParam, OMX_PORT_PARAM_TYPE);
  m_sPortParam.nPorts = 0x2;
  m_sPortParam.nStartPortNumber = (OMX_U32) PORT_INDEX_IN;

  OMX_INIT_STRUCT(&m_sParamBitrate, OMX_VIDEO_PARAM_BITRATETYPE);
  m_sParamBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sParamBitrate.eControlRate = OMX_Video_ControlRateVariable;
  m_sParamBitrate.nTargetBitrate = 64000;

  OMX_INIT_STRUCT(&m_sConfigBitrate, OMX_VIDEO_CONFIG_BITRATETYPE);
  m_sConfigBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigBitrate.nEncodeBitrate = 64000;

  OMX_INIT_STRUCT(&m_sConfigFramerate, OMX_CONFIG_FRAMERATETYPE);
  m_sConfigFramerate.nPortIndex = (OMX_U32) PORT_INDEX_IN;
  m_sConfigFramerate.xEncodeFramerate = 15 << 16;

  OMX_INIT_STRUCT(&m_sParamProfileLevel, OMX_VIDEO_PARAM_PROFILELEVELTYPE);
  m_sParamProfileLevel.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  switch (eCodec)
  {
    case OMX_VIDEO_CodingMPEG4 :
      m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple;
      m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_MPEG4Level0;
      break;
    case OMX_VIDEO_CodingAVC :
      m_sParamProfileLevel.eProfile = (OMX_U32)OMX_VIDEO_AVCProfileBaseline;
      m_sParamProfileLevel.eLevel = (OMX_U32)OMX_VIDEO_AVCLevel1;
      break;
    case OMX_VIDEO_CodingH263 :
      m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_H263ProfileBaseline;
      m_sParamProfileLevel.eLevel = (OMX_U32)OMX_VIDEO_H263Level10;
      break;
    default:
      QC_OMX_MSG_ERROR("Error in CodecType");
      break;
  }

  // Initialize the video parameters for input port
  OMX_INIT_STRUCT(&m_sInPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
  m_sInPortDef.nPortIndex = (OMX_U32) PORT_INDEX_IN;
  m_sInPortDef.bEnabled = OMX_TRUE;
  m_sInPortDef.bPopulated = OMX_FALSE;
  m_sInPortDef.eDomain = OMX_PortDomainVideo;
  m_sInPortDef.eDir = OMX_DirInput;
  m_sInPortDef.format.video.cMIMEType = (OMX_STRING)"YUV420";
  m_sInPortDef.format.video.nFrameWidth = 176;
  m_sInPortDef.format.video.nFrameHeight = 144;
  m_sInPortDef.format.video.nStride = 0;
  m_sInPortDef.format.video.nSliceHeight = 0;
  m_sInPortDef.format.video.nBitrate = 0;
  m_sInPortDef.format.video.xFramerate = 15 << 16;
  m_sInPortDef.format.video.eColorFormat =  OMX_COLOR_FormatYUV420SemiPlanar;
  m_sInPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingUnused;

  // Initialize the video parameters for output port
  OMX_INIT_STRUCT(&m_sOutPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
  m_sOutPortDef.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sOutPortDef.bEnabled = OMX_TRUE;
  m_sOutPortDef.bPopulated = OMX_FALSE;
  m_sOutPortDef.eDomain = OMX_PortDomainVideo;
  m_sOutPortDef.eDir = OMX_DirOutput;
  switch (eCodec)
  {
    case OMX_VIDEO_CodingMPEG4:
      m_sOutPortDef.format.video.cMIMEType = (OMX_STRING)"MPEG4";
      break;
    case OMX_VIDEO_CodingAVC:
      m_sOutPortDef.format.video.cMIMEType = (OMX_STRING)"H264";
      break;
    case OMX_VIDEO_CodingH263:
      m_sOutPortDef.format.video.cMIMEType = (OMX_STRING)"H263";
      break;
    default:
      QC_OMX_MSG_ERROR("Error in MIME Type");
      return OMX_ErrorBadParameter;
  }

  m_sOutPortDef.format.video.nFrameWidth = 176;
  m_sOutPortDef.format.video.nFrameHeight = 144;
  m_sOutPortDef.format.video.nBitrate = 64000;
  m_sOutPortDef.format.video.xFramerate = 15 << 16;
  m_sOutPortDef.format.video.eColorFormat =  OMX_COLOR_FormatUnused;
  m_sOutPortDef.format.video.eCompressionFormat =  eCodec;

  // Initialize the video color format for input port
  OMX_INIT_STRUCT(&m_sInPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
  m_sInPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_IN;
  m_sInPortFormat.nIndex = 0;
  m_sInPortFormat.eColorFormat =  OMX_COLOR_FormatYUV420SemiPlanar;
  m_sInPortFormat.eCompressionFormat = OMX_VIDEO_CodingUnused;

  // Initialize the compression format for output port
  OMX_INIT_STRUCT(&m_sOutPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
  m_sOutPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sOutPortFormat.nIndex = 0;
  m_sOutPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
  m_sOutPortFormat.eCompressionFormat =  eCodec;

  OMX_INIT_STRUCT(&m_sPriorityMgmt, OMX_PRIORITYMGMTTYPE); ///@todo determine if we need this

  OMX_INIT_STRUCT(&m_sInBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
  m_sInBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_IN;

  OMX_INIT_STRUCT(&m_sOutBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
  m_sOutBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

  // Initialize error reslience
  OMX_INIT_STRUCT(&m_sErrorCorrection, OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE);
  m_sErrorCorrection.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sErrorCorrection.bEnableHEC = OMX_FALSE;
  m_sErrorCorrection.bEnableResync = OMX_FALSE;
  m_sErrorCorrection.nResynchMarkerSpacing = OMX_FALSE;
  m_sErrorCorrection.bEnableDataPartitioning = OMX_FALSE;
  m_sErrorCorrection.bEnableRVLC = OMX_FALSE;

  // rotation
  OMX_INIT_STRUCT(&m_sConfigFrameRotation, OMX_CONFIG_ROTATIONTYPE);
  m_sConfigFrameRotation.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigFrameRotation.nRotation = 0;

  // mp4 specific init
  OMX_INIT_STRUCT(&m_sParamMPEG4, OMX_VIDEO_PARAM_MPEG4TYPE);
  m_sParamMPEG4.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sParamMPEG4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
  m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level0;
  m_sParamMPEG4.nSliceHeaderSpacing = 0;
  m_sParamMPEG4.bSVH = OMX_FALSE;
  m_sParamMPEG4.bGov = OMX_FALSE;
  m_sParamMPEG4.nPFrames = 29; // 2 second intra period for default 15 fps
  m_sParamMPEG4.bACPred = OMX_TRUE;
  m_sParamMPEG4.nTimeIncRes = 30; // delta = 2 @ 15 fps
  m_sParamMPEG4.nAllowedPictureTypes = 2; // pframe and iframe
  m_sParamMPEG4.nHeaderExtension = 1; // number of video packet headers per vop
  m_sParamMPEG4.bReversibleVLC = OMX_FALSE;

  // h264 (avc) specific init
  OMX_INIT_STRUCT(&m_sParamAVC, OMX_VIDEO_PARAM_AVCTYPE);
  m_sParamAVC.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sParamAVC.eProfile = OMX_VIDEO_AVCProfileBaseline;
  m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel1;
  m_sParamAVC.nSliceHeaderSpacing = 0;
  m_sParamAVC.nPFrames = 29;
  m_sParamAVC.nBFrames = 0;
  m_sParamAVC.bUseHadamard = OMX_TRUE;
  m_sParamAVC.nRefFrames = 1;
  m_sParamAVC.nRefIdx10ActiveMinus1 = 0;
  m_sParamAVC.nRefIdx11ActiveMinus1 = 0;
  m_sParamAVC.bEnableUEP = OMX_FALSE;
  m_sParamAVC.bEnableFMO = OMX_FALSE;
  m_sParamAVC.bEnableASO = OMX_FALSE;
  m_sParamAVC.bEnableRS = OMX_FALSE;
  m_sParamAVC.nAllowedPictureTypes = 2;
  m_sParamAVC.bFrameMBsOnly = OMX_TRUE;
  m_sParamAVC.bMBAFF = OMX_FALSE;
  m_sParamAVC.bEntropyCodingCABAC = OMX_FALSE;
  m_sParamAVC.bWeightedPPrediction = OMX_FALSE;
  m_sParamAVC.nWeightedBipredicitonMode = OMX_FALSE;
  m_sParamAVC.bconstIpred = OMX_FALSE;
  m_sParamAVC.bDirect8x8Inference = OMX_FALSE;
  m_sParamAVC.bDirectSpatialTemporal = OMX_FALSE;
  m_sParamAVC.nCabacInitIdc = 0;
  m_sParamAVC.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable; // enabled by default

  // h263 specific init
  OMX_INIT_STRUCT(&m_sParamH263, OMX_VIDEO_PARAM_H263TYPE);
  m_sParamH263.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sParamH263.nPFrames = 29;
  m_sParamH263.nBFrames = 0;
  m_sParamH263.eProfile = OMX_VIDEO_H263ProfileBaseline;
  m_sParamH263.eLevel = OMX_VIDEO_H263Level10;
  m_sParamH263.bPLUSPTYPEAllowed = OMX_FALSE;
  m_sParamH263.nAllowedPictureTypes = 2;
  m_sParamH263.bForceRoundingTypeToZero = OMX_TRUE; ///@todo determine what this should be
  m_sParamH263.nPictureHeaderRepetition = 0; ///@todo determine what this should be
  m_sParamH263.nGOBHeaderInterval = 0; ///@todo determine what this should be

  OMX_INIT_STRUCT(&m_sParamQPs, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
  m_sParamQPs.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  if (eCodec == OMX_VIDEO_CodingAVC)
  {
    m_sParamQPs.nQpI = 30;
    m_sParamQPs.nQpP = 30;
    m_sParamQPs.nQpB = 0; // unsupported
  }
  else
  {
    m_sParamQPs.nQpI = 14;
    m_sParamQPs.nQpP = 14;
    m_sParamQPs.nQpB = 0; // unsupported
  }

  OMX_INIT_STRUCT(&m_sConfigIntraRefreshVOP, OMX_CONFIG_INTRAREFRESHVOPTYPE);
  m_sConfigIntraRefreshVOP.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigIntraRefreshVOP.IntraRefreshVOP = OMX_FALSE;

#ifdef QCOM_OMX_VENC_EXT
  OMX_INIT_STRUCT(&m_sConfigQpRange, QOMX_VIDEO_TEMPORALSPATIALTYPE);
  m_sConfigQpRange.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigQpRange.nTSFactor = 100;

  OMX_INIT_STRUCT(&m_sConfigIntraPeriod, QOMX_VIDEO_INTRAPERIODTYPE);
  m_sConfigIntraPeriod.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigIntraPeriod.nPFrames = 29;
#endif

  OMX_INIT_STRUCT(&m_sConfigNAL, OMX_VIDEO_CONFIG_NALSIZE);
  m_sConfigNAL.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sConfigNAL.nNaluBytes = 0;

  OMX_INIT_STRUCT(&m_sParamIntraRefresh, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
  m_sParamIntraRefresh.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
  m_sParamIntraRefresh.eRefreshMode = OMX_VIDEO_IntraRefreshMax;
  m_sParamIntraRefresh.nAirMBs = 0;
  m_sParamIntraRefresh.nAirRef = 0;
  m_sParamIntraRefresh.nCirMBs = 0;

  if (driver_set_default_config() != OMX_ErrorNone)
  {
    goto bail;
  }
  if (driver_get_buffer_reqs(&m_sInPortDef, PORT_INDEX_IN) != OMX_ErrorNone)
  {
    goto bail;
  }
  if (driver_get_buffer_reqs(&m_sOutPortDef, PORT_INDEX_OUT) != OMX_ErrorNone)
  {
    goto bail;
  }

  /// used buffer managers
  QC_OMX_MSG_MEDIUM("creating used input bm");
  m_pInBufferMgr = new VencBufferManager(&result);
  if (m_pInBufferMgr == NULL)
  {
    QC_OMX_MSG_ERROR("failed to create used input buffer manager");
    result = OMX_ErrorInsufficientResources;
    goto bail;
  }
  else if (result != OMX_ErrorNone)
  {
    QC_OMX_MSG_ERROR("failed to create used input buffer manager");
    goto bail;
  }
  QC_OMX_MSG_MEDIUM("creating used output Buffer Manager");
  m_pOutBufferMgr = new VencBufferManager(&result);
  if (result != OMX_ErrorNone)
  {
    QC_OMX_MSG_ERROR("failed to create used output buffer manager");
    goto bail;
  }
  else if (m_pOutBufferMgr == NULL)
  {
    QC_OMX_MSG_ERROR("failed to create used output buffer manager");
    result = OMX_ErrorInsufficientResources;
    goto bail;
  }

  QC_OMX_MSG_MEDIUM("creating component thread");
  if (pthread_create(&m_ComponentThread,
        NULL,
        component_thread,
        (void *)this) != 0)
  {
    QC_OMX_MSG_ERROR("failed to create component thread");
    result = OMX_ErrorInsufficientResources;
    goto bail;
  }
  else
  {
    bCThread = OMX_TRUE;
  }

  QC_OMX_MSG_MEDIUM("creating reader thread");
  if (pthread_create(&m_ReaderThread,
        NULL,
        reader_thread_entry,
        (void *)this) != 0)
  {
    QC_OMX_MSG_ERROR("failed to create reader thread");
    result = OMX_ErrorInsufficientResources;
    goto bail;
  }
  else
  {
    bRThread = OMX_TRUE;
  }

  QC_OMX_MSG_MEDIUM("we are now in the OMX_StateLoaded state");

  m_eState = OMX_StateLoaded;

  return result;

bail:
  if (bCThread)
  {
    if (m_pMsgQ->PushMsg(VencMsgQ::MSG_ID_EXIT, NULL) == OMX_ErrorNone)
    {
      if (pthread_join(m_ComponentThread, NULL) != 0)
      {
        QC_OMX_MSG_ERROR("failed to destroy thread");
      }
    }
  }
  if (bRThread)
  {
    if (DeviceIoControl(m_nFd,
          VENC_IOCTL_CMD_STOP_READ_MSG) == 0)
    {
      if (pthread_join(m_ReaderThread, NULL) != 0)
      {
        QC_OMX_MSG_ERROR("failed to destroy thread");
      }
    }
  }
  if (m_pMsgQ != NULL)
  {
    delete m_pMsgQ;
  }
  if (m_pInBufferMgr)
  {
    delete m_pInBufferMgr;
  }
  if (m_pOutBufferMgr)
  {
    delete m_pOutBufferMgr;
  }
  if (m_pComponentName)
  {
    free(m_pComponentName);
  }
  return result;
}

Venc* Venc::get_instance()
{
  QC_OMX_MSG_HIGH("getting instance...");
  if (g_pVencInstance)
  {
    QC_OMX_MSG_ERROR("Singleton Class can't created more than one instance");
    return NULL;
  }
  g_pVencInstance = new Venc();
  return g_pVencInstance;
}

OMX_ERRORTYPE Venc::get_component_version(OMX_IN  OMX_HANDLETYPE hComponent,
                                          OMX_OUT OMX_STRING pComponentName,
                                          OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
                                          OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
                                          OMX_OUT OMX_UUIDTYPE* pComponentUUID)
{
  if (hComponent == NULL ||
      pComponentName == NULL ||
      pComponentVersion == NULL ||
      pSpecVersion == NULL ||
      pComponentUUID == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  memcpy(pComponentName, m_pComponentName, OMX_MAX_STRINGNAME_SIZE);
  pSpecVersion->nVersion = OMX_SPEC_VERSION;
  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::send_command(OMX_IN  OMX_HANDLETYPE hComponent,
                                 OMX_IN  OMX_COMMANDTYPE Cmd,
                                 OMX_IN  OMX_U32 nParam1,
                                 OMX_IN  OMX_PTR pCmdData)
{

  VencMsgQ::MsgIdType msgId;
  VencMsgQ::MsgDataType msgData;
  if(m_eState == OMX_StateInvalid)
  {
    return OMX_ErrorInvalidState;
  }

  switch (Cmd)
  {
    case OMX_CommandStateSet:
      QC_OMX_MSG_HIGH("sending command MSG_ID_STATE_CHANGE");
      msgId = VencMsgQ::MSG_ID_STATE_CHANGE;
      msgData.eState = (OMX_STATETYPE) nParam1;
      break;

    case OMX_CommandFlush:
      QC_OMX_MSG_HIGH("sending command MSG_ID_FLUSH");
      msgId = VencMsgQ::MSG_ID_FLUSH;
      msgData.nPortIndex = nParam1;
      break;

    case OMX_CommandPortDisable:
      if((nParam1 != (OMX_U32)PORT_INDEX_IN) && (nParam1 != (OMX_U32)PORT_INDEX_OUT) &&
          (nParam1 != (OMX_U32)PORT_INDEX_BOTH))
      {
        QC_OMX_MSG_ERROR("bad port index to call OMX_CommandPortDisable");
        return OMX_ErrorBadPortIndex;
      }
      QC_OMX_MSG_HIGH("sending command MSG_ID_PORT_DISABLE");
      msgId = VencMsgQ::MSG_ID_PORT_DISABLE;
      msgData.nPortIndex = nParam1;
      break;

    case OMX_CommandPortEnable:
      if((nParam1 != (OMX_U32)PORT_INDEX_IN) && (nParam1 != (OMX_U32)PORT_INDEX_OUT) &&
          (nParam1 != (OMX_U32)PORT_INDEX_BOTH))
      {
        QC_OMX_MSG_ERROR("bad port index to call OMX_CommandPortEnable");
        return OMX_ErrorBadPortIndex;
      }
      QC_OMX_MSG_HIGH("sending command MSG_ID_PORT_ENABLE");
      msgId = VencMsgQ::MSG_ID_PORT_ENABLE;
      msgData.nPortIndex = nParam1;
      break;

    case OMX_CommandMarkBuffer:
      QC_OMX_MSG_HIGH("sending command MSG_ID_MARK_BUFFER");
      msgId = VencMsgQ::MSG_ID_MARK_BUFFER;
      msgData.sMarkBuffer.nPortIndex = nParam1;
      memcpy(&msgData.sMarkBuffer.sMarkData,
          pCmdData,
          sizeof(OMX_MARKTYPE));
      break;
    default:
      QC_OMX_MSG_ERROR("invalid command %d", (int) Cmd);
      return OMX_ErrorBadParameter;
  }

  m_pMsgQ->PushMsg(msgId, &msgData);
  sem_wait(&m_cmd_lock);
  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::get_parameter(OMX_IN  OMX_HANDLETYPE hComponent,
                                  OMX_IN  OMX_INDEXTYPE nParamIndex,
                                  OMX_INOUT OMX_PTR pCompParam)
{
  ///////////////////////////////////////////////////////////////////////////////
  // Supported Param Index                         Type
  // ============================================================================
  // OMX_IndexParamVideoPortFormat                 OMX_VIDEO_PARAM_PORTFORMATTYPE
  // OMX_IndexParamPortDefinition                  OMX_PARAM_PORTDEFINITIONTYPE
  // OMX_IndexParamVideoInit                       OMX_PORT_PARAM_TYPE
  // OMX_IndexParamVideoBitrate                    OMX_VIDEO_PARAM_BITRATETYPE
  // OMX_IndexParamVideoMpeg4                      OMX_VIDEO_PARAM_MPEG4TYPE
  // OMX_IndexParamVideoProfileLevelQuerySupported OMX_VIDEO_PARAM_PROFILELEVEL
  // OMX_IndexParamVideoProfileLevelCurrent        OMX_VIDEO_PARAM_PROFILELEVEL
  //OMX_IndexParamVideoErrorCorrection             OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE
  ///////////////////////////////////////////////////////////////////////////////
  if (pCompParam == NULL)
  {
    QC_OMX_MSG_ERROR("param is null");
    return OMX_ErrorBadParameter;
  }

  if (m_eState != OMX_StateLoaded)
  {
    QC_OMX_MSG_ERROR("we must be in the loaded state");
    return OMX_ErrorIncorrectStateOperation;
  }

  OMX_ERRORTYPE result = OMX_ErrorNone;
  switch (nParamIndex)
  {
    case OMX_IndexParamVideoPortFormat:
      {
        OMX_VIDEO_PARAM_PORTFORMATTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_PORTFORMATTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoPortFormat");
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
        {
          memcpy(pParam, &m_sInPortFormat, sizeof(m_sInPortFormat));
        }
        else if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
        {
          memcpy(pParam, &m_sOutPortFormat, sizeof(m_sOutPortFormat));
        }
        else
        {
          QC_OMX_MSG_ERROR("GetParameter called on Bad Port Index");
          result = OMX_ErrorBadPortIndex;
        }
        break;

      }
    case OMX_IndexParamPortDefinition:
      {
        OMX_PARAM_PORTDEFINITIONTYPE* pParam = reinterpret_cast<OMX_PARAM_PORTDEFINITIONTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamPortDefinition");
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
        {
          memcpy(pParam, &m_sInPortDef, sizeof(m_sInPortDef));
        }
        else if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
        {
          memcpy(pParam, &m_sOutPortDef, sizeof(m_sOutPortDef));
        }
        else
        {
          QC_OMX_MSG_ERROR("GetParameter called on Bad Port Index");
          result = OMX_ErrorBadPortIndex;
        }
        break;
      }
    case OMX_IndexParamVideoInit:
      {
        OMX_PORT_PARAM_TYPE* pParam = reinterpret_cast<OMX_PORT_PARAM_TYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoInit");
        memcpy(pParam, &m_sPortParam, sizeof(m_sPortParam));
        break;
      }
    case OMX_IndexParamVideoBitrate:
      {
        OMX_VIDEO_PARAM_BITRATETYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_BITRATETYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoBitrate");
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
        {
          memcpy(pParam, &m_sParamBitrate, sizeof(m_sParamBitrate));
        }
        else
        {
          QC_OMX_MSG_ERROR("bitrate is an output port param");
          return OMX_ErrorBadPortIndex;
        }
        break;
      }
    case OMX_IndexParamVideoMpeg4:
      {
        OMX_VIDEO_PARAM_MPEG4TYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_MPEG4TYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoMpeg4");
        memcpy(pParam, &m_sParamMPEG4, sizeof(m_sParamMPEG4));
        break;
      }
    case OMX_IndexParamVideoProfileLevelCurrent:
      {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_PROFILELEVELTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoProfileLevelCurrent");
        memcpy(pParam, &m_sParamProfileLevel, sizeof(m_sParamProfileLevel));
        break;
      }
    case OMX_IndexParamVideoH263:
      {
        OMX_VIDEO_PARAM_H263TYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_H263TYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoH263");
        memcpy(pParam, &m_sParamH263, sizeof(m_sParamH263));
        break;
      }
    case OMX_IndexParamVideoAvc:
      {
        OMX_VIDEO_PARAM_AVCTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_AVCTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoAvc");
        memcpy(pParam, &m_sParamAVC, sizeof(m_sParamAVC));
        break;
      }
#ifdef QCOM_OMX_VENC_EXT
    case QOMX_IndexParamVideoSyntaxHdr:  // QCOM extensions added
      {
        QOMX_VIDEO_SYNTAXHDRTYPE * pParam =
          reinterpret_cast<QOMX_VIDEO_SYNTAXHDRTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("QOMX_IndexParamVideoSyntaxHdr");
        if (pParam != NULL &&
            pParam->data != NULL &&
            pParam->nBytes != 0)
        {
          int nOutSize;
          struct ven_seq_header sequence;
          struct venc_pmem sbuf;
          sequence.pHdrBuf = (unsigned char*) pParam->data;
          sequence.nBufSize = (unsigned long) pParam->nBytes;
          sequence.nHdrLen = 0;

          if (sequence.nBufSize > 0 ) {
            pmem_alloc(&sbuf, pParam->nBytes, VENC_PMEM_EBI1);
            if (!ven_get_sequence_hdr(m_pDevice, &sbuf, &nOutSize))
            {
              pParam->nBytes = (OMX_U32) sequence.nBufSize;
              memcpy(pParam->data, sbuf.virt, nOutSize);
            }
            else
            {
              QC_OMX_MSG_ERROR("failed to get syntax header");
              result = translate_driver_error(GetLastError());
            }
            pmem_free(&sbuf);
          }
        }
        else
        {
          QC_OMX_MSG_ERROR("Invalid param(s)");
          result = OMX_ErrorBadParameter;
        }
        break;
      }
#endif
    case OMX_IndexParamVideoProfileLevelQuerySupported:
      {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_PROFILELEVELTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoProfileLevelQuerySupported");
        if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
        {
          static const OMX_U32 MPEG4Profile[][2] =
          { { (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple, (OMX_U32) OMX_VIDEO_MPEG4Level0 },
            { (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple, (OMX_U32) OMX_VIDEO_MPEG4Level0b },
            { (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple, (OMX_U32) OMX_VIDEO_MPEG4Level1 },
            { (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple, (OMX_U32) OMX_VIDEO_MPEG4Level2 },
            { (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple, (OMX_U32) OMX_VIDEO_MPEG4Level3 }};

          static const OMX_U32 nSupport = sizeof(MPEG4Profile) / (sizeof(OMX_U32) * 2);
          if (pParam->nProfileIndex < nSupport)
          {
            pParam->eProfile = (OMX_VIDEO_MPEG4PROFILETYPE) MPEG4Profile[pParam->nProfileIndex][0];
            pParam->eLevel = (OMX_VIDEO_MPEG4LEVELTYPE) MPEG4Profile[pParam->nProfileIndex][1];
          }
          else
          {
            result = OMX_ErrorNoMore;
          }
        }
        else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC)
        {
          static const OMX_U32 H264Profile[][2] =
          { { (OMX_U32) OMX_VIDEO_AVCProfileBaseline, (OMX_U32) OMX_VIDEO_AVCLevel1 },
            { (OMX_U32) OMX_VIDEO_AVCProfileBaseline, (OMX_U32) OMX_VIDEO_AVCLevel1b },
            { (OMX_U32) OMX_VIDEO_AVCProfileBaseline, (OMX_U32) OMX_VIDEO_AVCLevel11 },
            { (OMX_U32) OMX_VIDEO_AVCProfileBaseline, (OMX_U32) OMX_VIDEO_AVCLevel12 },
            { (OMX_U32) OMX_VIDEO_AVCProfileBaseline, (OMX_U32) OMX_VIDEO_AVCLevel13 }};

          static const OMX_U32 nSupport = sizeof(H264Profile) / (sizeof(OMX_U32) * 2);
          if (pParam->nProfileIndex < nSupport)
          {
            pParam->eProfile = (OMX_VIDEO_AVCPROFILETYPE) H264Profile[pParam->nProfileIndex][0];
            pParam->eLevel = (OMX_VIDEO_AVCLEVELTYPE) H264Profile[pParam->nProfileIndex][1];
            QC_OMX_MSG_MEDIUM("get_parameter: h264: level supported(%d)", pParam->eLevel);
          }
          else
          {
            result = OMX_ErrorNoMore;
          }

        }
        else
        {
          static const OMX_U32 H263Profile[][2] =
          { { (OMX_U32) OMX_VIDEO_H263ProfileBaseline, (OMX_U32) OMX_VIDEO_H263Level10 },
            { (OMX_U32) OMX_VIDEO_H263ProfileBaseline, (OMX_U32) OMX_VIDEO_H263Level20 },
            { (OMX_U32) OMX_VIDEO_H263ProfileBaseline, (OMX_U32) OMX_VIDEO_H263Level30 },
            { (OMX_U32) OMX_VIDEO_H263ProfileBaseline, (OMX_U32) OMX_VIDEO_H263Level45 },
            { (OMX_U32) OMX_VIDEO_H263ProfileISWV2, (OMX_U32) OMX_VIDEO_H263Level10 },
            { (OMX_U32) OMX_VIDEO_H263ProfileISWV2, (OMX_U32) OMX_VIDEO_H263Level45 }};

          static const OMX_U32 nSupport = sizeof(H263Profile) / (sizeof(OMX_U32) * 2);
          if (pParam->nProfileIndex < nSupport)
          {
            pParam->eProfile = (OMX_VIDEO_H263PROFILETYPE) H263Profile[pParam->nProfileIndex][0];
            pParam->eLevel = (OMX_VIDEO_H263LEVELTYPE) H263Profile[pParam->nProfileIndex][1];
          }
          else
          {
            result = OMX_ErrorNoMore;
          }
        }
        break;
      }
    case OMX_IndexParamVideoErrorCorrection:
      {
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoErrorCorrection");
        memcpy(pParam, &m_sErrorCorrection, sizeof(m_sErrorCorrection));
        break;
      }
    case OMX_IndexParamVideoQuantization:
      {
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_QUANTIZATIONTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoQuantization");
        memcpy(pParam, &m_sParamQPs, sizeof(m_sParamQPs));
        break;
      }
    case OMX_IndexParamVideoIntraRefresh:
      {
        OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam = reinterpret_cast<OMX_VIDEO_PARAM_INTRAREFRESHTYPE*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoIntraRefresh");
        memcpy(pParam, &m_sParamIntraRefresh, sizeof(m_sParamIntraRefresh));
        break;
      }
    case OMX_COMPONENT_CAPABILITY_TYPE_INDEX:
      {
        OMXComponentCapabilityFlagsType *pParam = reinterpret_cast<OMXComponentCapabilityFlagsType*>(pCompParam);
        QC_OMX_MSG_HIGH("OMX_COMPONENT_CAPABILITY_TYPE_INDEX");
        pParam->iIsOMXComponentMultiThreaded = OMX_TRUE;
        pParam->iOMXComponentSupportsExternalOutputBufferAlloc = OMX_FALSE;
        pParam->iOMXComponentSupportsExternalInputBufferAlloc = OMX_TRUE;
        pParam->iOMXComponentSupportsMovableInputBuffers = OMX_TRUE;
        pParam->iOMXComponentUsesNALStartCodes = OMX_TRUE;
        pParam->iOMXComponentSupportsPartialFrames = OMX_FALSE;
        pParam->iOMXComponentCanHandleIncompleteFrames = OMX_FALSE;
        pParam->iOMXComponentUsesFullAVCFrames = OMX_FALSE;

        //   m_bIsQcomPvt = OMX_TRUE;

        QC_OMX_MSG_MEDIUM("Supporting capability index in encoder node");
        break;
      }
    default:
      QC_OMX_MSG_ERROR("unsupported index 0x%x", (int) nParamIndex);
      result = OMX_ErrorUnsupportedIndex;
      break;
  }
  return result;
}

OMX_ERRORTYPE Venc::driver_get_buffer_reqs(OMX_PARAM_PORTDEFINITIONTYPE* pPortDef, PortIndexType eIndex)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  if (pPortDef != NULL)
  {
    struct ven_allocator_property property;
    int nOutSize;
    int rc = 0;

    if (eIndex == PORT_INDEX_IN)
    {
      QC_OMX_MSG_HIGH("get input buffer requirements...");
      rc = ven_get_input_req(m_pDevice,&property);
    }
    else
    {
      QC_OMX_MSG_HIGH("get output buffer requirements...");
      rc = ven_get_output_req(m_pDevice,&property);
    }

    if (!rc)
    {
      pPortDef->nBufferCountMin        = property.min_count;
      pPortDef->nBufferCountActual     = property.actual_count;
      pPortDef->nBufferSize            = property.data_size;
      pPortDef->nBufferAlignment       = property.alignment;
      pPortDef->bBuffersContiguous     = OMX_FALSE;
      QC_OMX_MSG_HIGH("nBufferCountMin = %d",pPortDef->nBufferCountMin);
      QC_OMX_MSG_HIGH("nBufferCountActual = %d",pPortDef->nBufferCountActual);
      QC_OMX_MSG_HIGH("nBufferSize = %d",pPortDef->nBufferSize);
      QC_OMX_MSG_HIGH("nBufferAlignment = %d",pPortDef->nBufferAlignment);
    }
    else
    {
      QC_OMX_MSG_ERROR("failed to get buffer req");
      result = translate_driver_error(GetLastError());
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }

  return result;
}

OMX_ERRORTYPE Venc::translate_profile(unsigned int* pDriverProfile,
                                      OMX_U32 eProfile,
                                      OMX_VIDEO_CODINGTYPE eCodec)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (eCodec == OMX_VIDEO_CodingMPEG4)
  {
    switch ((OMX_VIDEO_MPEG4PROFILETYPE) eProfile)
    {
      case OMX_VIDEO_MPEG4ProfileSimple:
        *pDriverProfile = VEN_PROFILE_MPEG4_SP;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported profile");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else if (eCodec == OMX_VIDEO_CodingH263)
  {
    switch ((OMX_VIDEO_H263PROFILETYPE) eProfile)
    {
      case OMX_VIDEO_H263ProfileBaseline:
        *pDriverProfile = VEN_PROFILE_H263_BASELINE;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported profile");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else if (eCodec == OMX_VIDEO_CodingAVC)
  {
    switch ((OMX_VIDEO_AVCPROFILETYPE) eProfile)
    {
      case OMX_VIDEO_AVCProfileBaseline:
        *pDriverProfile = VEN_PROFILE_H264_BASELINE;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported profile");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("unsupported codec");
    result = OMX_ErrorBadParameter;
  }

  return result;
}

OMX_ERRORTYPE Venc::translate_level(unsigned int* pDriverLevel,
                                    OMX_U32 eLevel,
                                    OMX_VIDEO_CODINGTYPE eCodec)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (eCodec == OMX_VIDEO_CodingMPEG4)
  {

    switch ((OMX_VIDEO_MPEG4LEVELTYPE) eLevel)
    {
      case OMX_VIDEO_MPEG4Level0:
        *pDriverLevel = VEN_LEVEL_MPEG4_0;
        break;
      case OMX_VIDEO_MPEG4Level0b:
        *pDriverLevel = VEN_LEVEL_MPEG4_0B;
        break;
      case OMX_VIDEO_MPEG4Level1:
        *pDriverLevel = VEN_LEVEL_MPEG4_1;
        break;
      case OMX_VIDEO_MPEG4Level2:
        *pDriverLevel = VEN_LEVEL_MPEG4_2;
        break;
      case OMX_VIDEO_MPEG4Level3:
        *pDriverLevel = VEN_LEVEL_MPEG4_3;
        break;
      case OMX_VIDEO_MPEG4Level4:
        *pDriverLevel = VEN_LEVEL_MPEG4_4;
        break;
      case OMX_VIDEO_MPEG4Level4a:
        *pDriverLevel = VEN_LEVEL_MPEG4_4A;
        break;
      case OMX_VIDEO_MPEG4Level5:
        *pDriverLevel = VEN_LEVEL_MPEG4_6;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported level");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else if (eCodec == OMX_VIDEO_CodingH263)
  {

    switch ((OMX_VIDEO_H263LEVELTYPE) eLevel)
    {
      case OMX_VIDEO_H263Level10:
        *pDriverLevel = VEN_LEVEL_H263_10;
        break;
      case OMX_VIDEO_H263Level20:
        *pDriverLevel = VEN_LEVEL_H263_20;
        break;
      case OMX_VIDEO_H263Level30:
        *pDriverLevel = VEN_LEVEL_H263_30;
        break;
      case OMX_VIDEO_H263Level40:
        *pDriverLevel = VEN_LEVEL_H263_40;
        break;
      case OMX_VIDEO_H263Level45:
        *pDriverLevel = VEN_LEVEL_H263_45;
        break;
      case OMX_VIDEO_H263Level50:
        *pDriverLevel = VEN_LEVEL_H263_50;
        break;
      case OMX_VIDEO_H263Level60:
        *pDriverLevel = VEN_LEVEL_H263_60;
        break;
      case OMX_VIDEO_H263Level70:
        *pDriverLevel = VEN_LEVEL_H263_70;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported level");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else if (eCodec == OMX_VIDEO_CodingAVC)
  {

    switch ((OMX_VIDEO_AVCLEVELTYPE) eLevel)
    {
      case OMX_VIDEO_AVCLevel1:
        *pDriverLevel = VEN_LEVEL_H264_1;
        break;
      case OMX_VIDEO_AVCLevel1b:
        *pDriverLevel = VEN_LEVEL_H264_1B;
        break;
      case OMX_VIDEO_AVCLevel11:
        *pDriverLevel = VEN_LEVEL_H264_1P1;
        break;
      case OMX_VIDEO_AVCLevel12:
        *pDriverLevel = VEN_LEVEL_H264_1P2;
        break;
      case OMX_VIDEO_AVCLevel13:
        *pDriverLevel = VEN_LEVEL_H264_1P3;
        break;
      case OMX_VIDEO_AVCLevel2:
        *pDriverLevel = VEN_LEVEL_H264_2;
        break;
      case OMX_VIDEO_AVCLevel21:
        *pDriverLevel = VEN_LEVEL_H264_2P1;
        break;
      case OMX_VIDEO_AVCLevel22:
        *pDriverLevel = VEN_LEVEL_H264_2P2;
        break;
      case OMX_VIDEO_AVCLevel3:
        *pDriverLevel = VEN_LEVEL_H264_3;
        break;
      case OMX_VIDEO_AVCLevel31:
        *pDriverLevel = VEN_LEVEL_H264_3P1;
        break;
      default:
        QC_OMX_MSG_ERROR("unsupported level");
        result = OMX_ErrorBadParameter;
        break;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("unsupported codec");
    result = OMX_ErrorBadParameter;
  }

  return result;
}



OMX_ERRORTYPE Venc::translate_driver_error(int driverResult)
{
  static const int nAppErrorCodeBit = 0x20000000;
  OMX_ERRORTYPE result = OMX_ErrorNone;

  QC_OMX_MSG_HIGH("translate_driver_error...");
  // see if this is encoder specific error
  if (driverResult & nAppErrorCodeBit)
  {
    switch (driverResult & ~nAppErrorCodeBit)
    {
      case VENC_S_SUCCESS:
        QC_OMX_MSG_ERROR("unexpected success");
        result = OMX_ErrorNone;
        break;
      case VENC_S_EFAIL:
        result = OMX_ErrorUndefined;
        break;
      case VENC_S_EBADPARAM:
        result = OMX_ErrorBadParameter;
        break;
      case VENC_S_EINVALSTATE:
        result = OMX_ErrorInvalidState;
        break;
      case VENC_S_ENOSWRES:
        result = OMX_ErrorInsufficientResources;
        break;
      case VENC_S_ENOHWRES:
        result = OMX_ErrorInsufficientResources;
        break;
      case VENC_S_ETIMEOUT:
        result = OMX_ErrorTimeout;
        break;
      case VENC_S_ENOTSUPP:
        result = OMX_ErrorUnsupportedSetting;
        break;
      case VENC_S_ENOTIMPL:
        result = OMX_ErrorNotImplemented;
        break;
      case VENC_S_EFLUSHED:
      case VENC_S_EINSUFBUF:
      case VENC_S_ENOTPMEM:
      case VENC_S_ENOPREREQ:
      case VENC_S_ENOREATMPT:
      case VENC_S_ECMDQFULL:
      case VENC_S_EINVALCMD:
      case VENC_S_EBUFFREQ:
        result = OMX_ErrorUndefined;
        break;
      default:
        QC_OMX_MSG_ERROR("unexpected error code");
        result = OMX_ErrorUndefined;
        break;
    }
  }
  else if (driverResult == 0)
  {
    QC_OMX_MSG_ERROR("not expecting success");
    result = OMX_ErrorUndefined;
  }
  else
  {
    result = OMX_ErrorUndefined;
  }
  return result;
}

OMX_ERRORTYPE Venc::is_multi_slice_mode_supported()
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  if(!strncmp(m_pComponentName,"OMX.qcom.video.encoder.avc",strlen("OMX.qcom.video.encoder.avc"))
	&& m_sInPortDef.format.video.nFrameHeight * m_sInPortDef.format.video.nFrameWidth <= (VEN_VGA_DX * VEN_VGA_DY / 2) ) {
	  ret = OMX_ErrorNone;
  } else {
	  ret = OMX_ErrorUnsupportedSetting;
  }
  return ret;
}

OMX_ERRORTYPE Venc::driver_set_default_config()
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int nOutSize;
  int rc = 0;
  struct ven_base_cfg baseCfg;
  ////////////////////////////////////////
  // set base config
  ////////////////////////////////////////
  QC_OMX_MSG_HIGH("driver_set_default_config: pcofig: %p", &(m_pDevice->config.base_config));


  baseCfg.input_width = m_sInPortDef.format.video.nFrameWidth;
  baseCfg.input_height = m_sInPortDef.format.video.nFrameHeight;
  baseCfg.dvs_width = m_sOutPortDef.format.video.nFrameWidth;
  baseCfg.dvs_height = m_sOutPortDef.format.video.nFrameHeight;
  if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
  {
    baseCfg.codec_type = VEN_CODEC_MPEG4;
  }
  else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingH263)
  {
    baseCfg.codec_type = VEN_CODEC_H263;
  }
  else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC)
  {
    baseCfg.codec_type = VEN_CODEC_H264;
  }
  baseCfg.fps_num = m_sConfigFramerate.xEncodeFramerate >> 16;
  baseCfg.fps_den = 1; /// @integrate need to figure out how to set this
  baseCfg.target_bitrate = m_sParamBitrate.nTargetBitrate;
  baseCfg.input_format= VEN_INPUTFMT_NV21; /// @integrate need to make this configurable

  if (ven_set_base_cfg(m_pDevice, &baseCfg))
  {
    QC_OMX_MSG_ERROR("failed to set base config");
    result = translate_driver_error(GetLastError());
  }

  ////////////////////////////////////////
  // set profile
  ////////////////////////////////////////
  /* if (result == OMX_ErrorNone)
     {
     struct ven_profile profile;
     translate_profile(&profile.profile,
     m_sParamProfileLevel.eProfile,
     m_sOutPortDef.format.video.eCompressionFormat);
     rc = ven_set_codec_profile(m_pDevice,&profile);
     if (rc)
     {
     QC_OMX_MSG_ERROR("failed to set profile");
     result = translate_driver_error(GetLastError());
     }
     } */

  ////////////////////////////////////////
  // set level
  ////////////////////////////////////////
  /*
      if (result == OMX_ErrorNone)
      {
      struct ven_profile_level level;
      translate_level(&level.level,
      m_sParamProfileLevel.eLevel,
      m_sOutPortDef.format.video.eCompressionFormat);
      rc = ven_set_profile_level(m_pDevice, &level);
      if (rc)
      {
      QC_OMX_MSG_ERROR("failed to set level");
      result = translate_driver_error(GetLastError());
      }
      } */

  ////////////////////////////////////////
  // set rate control
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_rate_ctrl_cfg rateControl;
    if (m_sParamBitrate.eControlRate == OMX_Video_ControlRateDisable)
    {
      rateControl.rc_mode = VEN_RC_OFF;
    }
    else if (m_sParamBitrate.eControlRate == OMX_Video_ControlRateVariable)
    {
      rateControl.rc_mode = VEN_RC_VBR_CFR;
    }
    else if (m_sParamBitrate.eControlRate == OMX_Video_ControlRateVariableSkipFrames)
    {
      rateControl.rc_mode = VEN_RC_VBR_VFR;
    }
    else if (m_sParamBitrate.eControlRate == OMX_Video_ControlRateConstantSkipFrames)
    {
      rateControl.rc_mode = VEN_RC_CBR_VFR;
    }
    else
    {
      QC_OMX_MSG_ERROR("invalid rc");
    }

    if (ven_set_rate_control(m_pDevice, &rateControl) != 0 )
    {
      QC_OMX_MSG_ERROR("failed to set rate control");
    }
  }

  ////////////////////////////////////////
  // set slice config
  ////////////////////////////////////////

   if (result == OMX_ErrorNone)
   {
     struct ven_multi_slice_cfg slice;
     slice.mslice_mode = VENC_SLICE_MODE_DEFAULT; // default to no slicing
     slice.mslice_size = 0;
     rc = ven_set_multislice_cfg(m_pDevice, &slice);
     if(rc)
     {
       QC_OMX_MSG_ERROR("failed to set slice config");
       result = translate_driver_error(GetLastError());
     }
   }

  ////////////////////////////////////////
  // set rotation
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_rotation rotation;
    rotation.rotation = m_sConfigFrameRotation.nRotation;

    rc = ven_set_rotation(m_pDevice, &rotation);
    if (rc)
    {
      QC_OMX_MSG_ERROR("failed to set rotation");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set vop time increment
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_vop_timing_cfg timeInc;
    timeInc.vop_time_resolution = m_sParamMPEG4.nTimeIncRes;

    rc = ven_set_vop_timing(m_pDevice, &timeInc);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set time resolution");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set intra period
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_intra_period intraPeriod;

#if 0
    if (m_sParamMPEG4.bGov)
    {
#endif
      intraPeriod.num_pframes = m_sParamMPEG4.nPFrames;
#if 0
    }
    else
    {
      intraPeriod.num_pframes = 0;
    }
#endif

    if (ven_set_intra_period(m_pDevice,&intraPeriod) !=0)
    {
      QC_OMX_MSG_ERROR("failed to set intra period");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set session qp's
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    if (m_sParamBitrate.eControlRate == OMX_Video_ControlRateDisable)
    {
      struct ven_session_qp sessionQP;
      sessionQP.iframe_qp = m_sParamQPs.nQpI;
      sessionQP.pframe_qp = m_sParamQPs.nQpP;

      rc = ven_set_session_qp(m_pDevice, &sessionQP);
      if(rc)
      {
        QC_OMX_MSG_ERROR("failed to set session QPs");
        result = translate_driver_error(GetLastError());
      }
    }
  }

#ifdef QCOM_OMX_VENC_EXT
  ////////////////////////////////////////
  // set min/max qp's
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_qp_range qpRange;
    qpRange.min_qp = 2;
    if(m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4 ||
	m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingH263) {
      qpRange.max_qp = 8 + (int) roundingup(m_sConfigQpRange.nTSFactor * 0.23);
    }else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC){
      qpRange.max_qp = 33 + roundingup(m_sConfigQpRange.nTSFactor * 0.18);
    }

    rc = ven_set_qp_range(m_pDevice, &qpRange);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set qp range");
      result = translate_driver_error(GetLastError());
    }
  }
#endif

  ////////////////////////////////////////
  // set ac prediction
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_switch acPred;

    if (m_sParamMPEG4.bACPred == OMX_TRUE &&
        m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
    {
      acPred.status = 1;
    }
    else
    {
      acPred.status = 0;
    }
    rc = ven_set_ac_prediction(m_pDevice, &acPred);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set ac prediction");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set short header
  ////////////////////////////////////////

  if (result == OMX_ErrorNone)
  {
    struct ven_switch shortHeader;
    shortHeader.status = m_sParamMPEG4.bSVH == OMX_TRUE ? 1 : 0;
    rc = ven_set_short_hdr(m_pDevice, &shortHeader);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set short header");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set header extension coding
  ////////////////////////////////////////
  if (result == OMX_ErrorNone)
  {
    struct ven_header_extension hex; // for default config
    memset(&hex, 0, sizeof(ven_header_extension));
    if (m_sErrorCorrection.bEnableHEC == OMX_TRUE)
    {
      hex.hec_interval = 1; // for default config
      if (m_sParamMPEG4.nHeaderExtension > 0)
      {
        hex.hec_interval = m_sParamMPEG4.nHeaderExtension;
      }
    }
    rc = ven_set_hec(m_pDevice, &hex);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set hex");
      result = translate_driver_error(GetLastError());
    }
  }

  ////////////////////////////////////////
  // set intra refresh
  ////////////////////////////////////////
#if 0
  if (result == OMX_ErrorNone)
  {
    struct ven_intra_refresh ir;
    if (m_sParamIntraRefresh.eRefreshMode == OMX_VIDEO_IntraRefreshCyclic)
    {
      ir.ir_mode = VEN_IR_RANDOM;
      ir.mb_count = 0;
    }
    else
    {
      ir.ir_mode = VEN_IR_OFF;
      ir.mb_count = 0;
    }

    rc = ven_set_intra_refresh_rate(m_pDevice, &ir);
    if(rc)
    {
      QC_OMX_MSG_ERROR("failed to set intra refresh");
      result = translate_driver_error(GetLastError());
    }
  }
#endif

  return result;
}

OMX_ERRORTYPE Venc::update_param_port_fmt(OMX_IN OMX_VIDEO_PARAM_PORTFORMATTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      if (pParam->xFramerate != 0)
      {
        QC_OMX_MSG_HIGH("Frame rate is for input port (refer to OMX IL spec)");
        //  result = OMX_ErrorUnsupportedSetting;
      }
    }

    int nOutSize;
    if (result == OMX_ErrorNone)
    {
      struct ven_base_cfg baseCfg;
      baseCfg.input_width = m_sInPortDef.format.video.nFrameWidth;
      baseCfg.input_height = m_sInPortDef.format.video.nFrameHeight;
      baseCfg.dvs_width = m_sOutPortDef.format.video.nFrameWidth;
      baseCfg.dvs_height = m_sOutPortDef.format.video.nFrameHeight;
      if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
      {
        baseCfg.codec_type = VEN_CODEC_MPEG4;
      }
      else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingH263)
      {
        baseCfg.codec_type = VEN_CODEC_H263;
      }
      else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC)
      {
        baseCfg.codec_type = VEN_CODEC_H264;
      }
      baseCfg.fps_num = m_sConfigFramerate.xEncodeFramerate >> 16;
      baseCfg.fps_den = 1; /// @integrate need to figure out how to set this
      baseCfg.target_bitrate = m_sParamBitrate.nTargetBitrate;
      baseCfg.input_format= VEN_INPUTFMT_NV21; /// @integrate need to make this configurable

      rc = ven_set_base_cfg(m_pDevice, &baseCfg);
      if(rc)
      {
        QC_OMX_MSG_ERROR("failed to set base config");
        result = OMX_ErrorUndefined;
      }

      if (result == OMX_ErrorNone)
      {
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
        {
          QC_OMX_MSG_HIGH("update input port format");
          memcpy(&m_sInPortFormat, pParam, sizeof(m_sInPortFormat));
          m_sConfigFramerate.xEncodeFramerate = pParam->xFramerate;
          m_sInPortDef.format.video.xFramerate = pParam->xFramerate;
        }
        else
        {
          QC_OMX_MSG_HIGH("update output port format");
          memcpy(&m_sOutPortFormat, pParam, sizeof(m_sOutPortFormat));
        }

      }
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_port_def(OMX_IN OMX_PARAM_PORTDEFINITIONTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    int code;
    struct ven_allocator_property property;
    int rc = 0;
    OMX_PARAM_PORTDEFINITIONTYPE* pLocalDef;

    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      pLocalDef = &m_sOutPortDef;
      if (pParam->format.video.xFramerate != 0)
      {
        QC_OMX_MSG_HIGH("Frame rate is for input port (refer to OMX IL spec)");
        //            result = OMX_ErrorUnsupportedSetting;

      }
    }
    else
    {
      pLocalDef = &m_sInPortDef;
      if (pParam->format.video.nBitrate != 0)
      {
        QC_OMX_MSG_ERROR("Bitrate rate is for output port");
        result = OMX_ErrorUnsupportedSetting;
      }
    }

    ////////////////////////////////////////
    // set base config
    ////////////////////////////////////////
    int nOutSize;
    if (result == OMX_ErrorNone)
    {
      struct ven_base_cfg baseCfg;
      OMX_PARAM_PORTDEFINITIONTYPE* pInDef;
      OMX_PARAM_PORTDEFINITIONTYPE* pOutDef;
      if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
      {
        pInDef = pParam;
        pOutDef = &m_sOutPortDef;
      }
      else
      {
        pInDef = &m_sInPortDef;
        pOutDef = pParam;
      }
      baseCfg.input_width = pInDef->format.video.nFrameWidth;
      baseCfg.input_height = pInDef->format.video.nFrameHeight;
      baseCfg.dvs_width = pOutDef->format.video.nFrameWidth;
      baseCfg.dvs_height = pOutDef->format.video.nFrameHeight;
      if (pOutDef->format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
      {
        baseCfg.codec_type = VEN_CODEC_MPEG4;
      }
      else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingH263)
      {
        baseCfg.codec_type = VEN_CODEC_H263;
      }
      else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC)
      {
        baseCfg.codec_type = VEN_CODEC_H264;
      }

      baseCfg.fps_num = pInDef->format.video.xFramerate >> 16;
      baseCfg.fps_den = 1; /// @integrate need to figure out how to set this
      baseCfg.target_bitrate = pOutDef->format.video.nBitrate;
      baseCfg.input_format= VEN_INPUTFMT_NV21; /// @integrate need to make this configurable

      rc = ven_set_base_cfg(m_pDevice, &baseCfg);
      if(rc)
      {
        QC_OMX_MSG_ERROR("failed to set base config");
        result = translate_driver_error(GetLastError());
      }

    }

    if (result == OMX_ErrorNone)
    {
      OMX_PARAM_PORTDEFINITIONTYPE tmpReq;

      // copy our local definition into temp copy
      memcpy(&tmpReq, pLocalDef, sizeof(tmpReq));

      // get the driver buffer requirements since may have changed when base config has changed
      result = driver_get_buffer_reqs(&tmpReq, (PortIndexType) pParam->nPortIndex);

      if (result == OMX_ErrorNone)
      {
        // update local copy of buffer requirements with driver requirements
        pLocalDef->nBufferCountMin        = tmpReq.nBufferCountMin;
        pLocalDef->nBufferCountActual     = tmpReq.nBufferCountActual;
        pLocalDef->nBufferSize            = tmpReq.nBufferSize;
        pLocalDef->nBufferAlignment       = tmpReq.nBufferAlignment;
        pLocalDef->bBuffersContiguous     = OMX_FALSE;
      }
    }

    if (result == OMX_ErrorNone)
    {
      if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
      {
        pLocalDef = &m_sInPortDef;
      }
      else
      {
        pLocalDef = &m_sOutPortDef;
      }

      /// @integrate this is a hack. how to handle buff req change due to frame size change
      pParam->nBufferSize = pLocalDef->nBufferSize;

      QC_OMX_MSG_LOW("Before update buffer: actual: %d, min: %d",
          pLocalDef->nBufferCountActual, pLocalDef->nBufferCountMin);
      QC_OMX_MSG_LOW("After update buffer: actual:%d, min: %d",
          pParam->nBufferCountActual, pParam->nBufferCountMin);

      QC_OMX_MSG_LOW("Before update buffer: size: %d, alignment: %d",
          pLocalDef->nBufferSize, pLocalDef->nBufferAlignment);
      QC_OMX_MSG_LOW("After update buffer: size:%d, alignment: %d",
          pParam->nBufferSize, pParam->nBufferAlignment);
      if (pParam->nBufferCountActual >= pLocalDef->nBufferCountMin &&    // must be greater or equal to min requirement
          pParam->nBufferCountMin == pLocalDef->nBufferCountMin &&       // min count is read only!
          pParam->nBufferSize >= pLocalDef->nBufferSize &&               // must be greater or equal to min buffer size
          pParam->nBufferAlignment == pLocalDef->nBufferAlignment)       // alignment is read only!
      {

        QC_OMX_MSG_LOW("Before update buffer: min: %d, actual: %d",
            pLocalDef->nBufferCountMin, pLocalDef->nBufferCountMin);
        QC_OMX_MSG_LOW("After update buffer: actual:%d, min: %d",
            pParam->nBufferCountActual, pParam->nBufferCountMin);

        property.min_count   = pParam->nBufferCountMin;
        property.actual_count      = pParam->nBufferCountActual;
        property.data_size   = pParam->nBufferSize;
        property.alignment  = pParam->nBufferAlignment;
        property.suffix_size = 0;

        ////////////////////////////////////////
        // set buffer requirements
        ////////////////////////////////////////
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
        {
          rc = ven_set_input_req(m_pDevice, &property);
        }
        else
        {
          rc = ven_set_output_req(m_pDevice, &property);
        }
        if(!rc)
        {
          if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
          {
            memcpy(&m_sInPortDef, pParam, sizeof(m_sInPortDef));
            m_sInPortFormat.xFramerate = pParam->format.video.xFramerate;
          }
          else
          {
            memcpy(&m_sOutPortDef, pParam, sizeof(m_sOutPortDef));
            m_sConfigBitrate.nEncodeBitrate = pParam->format.video.nBitrate;
            m_sParamBitrate.nTargetBitrate = pParam->format.video.nBitrate;
          }
        }
        else
        {
          QC_OMX_MSG_ERROR("failed to set buffer req");
          result = OMX_ErrorUndefined;
        }
      }
      else
      {
        QC_OMX_MSG_ERROR("violation of buffer requirements");
      }
    }

  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::adjust_profile_level( )
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  OMX_U32 nFrameHeight = m_sInPortDef.format.video.nFrameHeight;
  OMX_U32 nFrameWidth = m_sInPortDef.format.video.nFrameWidth;

  if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
  {
    if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY))
    {
      m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level0;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_CIF_DX, VEN_CIF_DY))
    {
      m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level2;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_VGA_DX, VEN_VGA_DY))
    {
      m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level4a;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_HD720P_DX, VEN_HD720P_DY))
    {
      m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level5;
    }
    m_sParamProfileLevel.eLevel = (OMX_U32)m_sParamMPEG4.eLevel;
  }
  else if (m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC)
  {
    if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY))
    {
      m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel1;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_CIF_DX, VEN_CIF_DY))
    {
      m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel12;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_VGA_DX, VEN_VGA_DY))
    {
      m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel22;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_D1_DX, VEN_D1_DY))
    {
      m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel3;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_HD720P_DX, VEN_HD720P_DY))
    {
      m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel31;
    }
    m_sParamProfileLevel.eLevel = (OMX_U32)m_sParamAVC.eLevel;
  }
  else {
    if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY))
    {
      m_sParamH263.eLevel = OMX_VIDEO_H263Level10;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_CIF_DX, VEN_CIF_DY))
    {
      m_sParamH263.eLevel = OMX_VIDEO_H263Level20;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_VGA_DX, VEN_VGA_DY))
    {
      m_sParamH263.eLevel = OMX_VIDEO_H263Level30;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_D1_DX, VEN_D1_DY))
    {
      m_sParamH263.eLevel = OMX_VIDEO_H263Level60;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_HD720P_DX, VEN_HD720P_DY))
    {
      m_sParamH263.eLevel = OMX_VIDEO_H263Level70;
    }
    m_sParamProfileLevel.eLevel = (OMX_U32)m_sParamH263.eLevel;
  }
  QC_OMX_MSG_MEDIUM("adjust_profile_level: level: %d", m_sParamProfileLevel.eLevel);
  return result;
}

OMX_ERRORTYPE Venc::update_param_video_init(OMX_IN OMX_PORT_PARAM_TYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  if (pParam != NULL)
  {
    memcpy(&m_sPortParam, pParam, sizeof(m_sPortParam));
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_bitrate(OMX_IN OMX_VIDEO_PARAM_BITRATETYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // set bitrate
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_target_bitrate bitrate;
        bitrate.target_bitrate = pParam->nTargetBitrate;

        if (ven_set_target_bitrate(m_pDevice,&bitrate) != 0)
        {
          QC_OMX_MSG_ERROR("error setting bitrate");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set rate control
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_rate_ctrl_cfg rateControl;

        if (pParam->eControlRate == OMX_Video_ControlRateDisable)
        {
          rateControl.rc_mode = VEN_RC_OFF;
        }
        else if (pParam->eControlRate == OMX_Video_ControlRateVariable)
        {
          rateControl.rc_mode = VEN_RC_VBR_CFR;
        }
        else if (pParam->eControlRate == OMX_Video_ControlRateVariableSkipFrames)
        {
          rateControl.rc_mode = VEN_RC_VBR_VFR;
        }
        else if (pParam->eControlRate == OMX_Video_ControlRateConstantSkipFrames)
        {
          rateControl.rc_mode = VEN_RC_CBR_VFR;
        }
        else
        {
          QC_OMX_MSG_ERROR("invalid rc selection");
          result = OMX_ErrorUnsupportedSetting;
        }

        rc = ven_set_rate_control(m_pDevice, &rateControl);
        if(rc)
        {
          QC_OMX_MSG_ERROR("error setting rate control");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamBitrate, pParam, sizeof(m_sParamBitrate));

        // also need to set configuration bitrate
        m_sConfigBitrate.nEncodeBitrate = pParam->nTargetBitrate;
        m_sParamBitrate.nTargetBitrate = pParam->nTargetBitrate;
        m_sOutPortDef.format.video.nBitrate = pParam->nTargetBitrate;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_mpeg4(OMX_IN OMX_VIDEO_PARAM_MPEG4TYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {

      ////////////////////////////////////////
      // set short header
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_switch shortHeader;
        shortHeader.status = pParam->bSVH == OMX_TRUE ? 1 : 0;
        rc = ven_set_short_hdr(m_pDevice, &shortHeader);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set short header");
          result = translate_driver_error(GetLastError());
        }
      }

#ifdef QCOM_OMX_VENC_EXT
      ////////////////////////////////////////
      // set intra period
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_intra_period intraPeriod;

#if 0
        if (pParam->bGov)
        {
#endif
          intraPeriod.num_pframes = pParam->nPFrames;
#if 0
        }
        else
        {
          intraPeriod.num_pframes = 0;
        }
#endif

        if (ven_set_intra_period(m_pDevice,&intraPeriod)  != 0 )
        {
          QC_OMX_MSG_ERROR("failed to set intra period");
          result = translate_driver_error(GetLastError());
        }
      }
#endif

      ////////////////////////////////////////
      // set ac prediction
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_switch acPred;
        acPred.status = pParam->bACPred == OMX_TRUE ? 1 : 0;

        rc = ven_set_ac_prediction(m_pDevice, &acPred);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set ac prediction");
          result = translate_driver_error(GetLastError());
        }
      }


      ////////////////////////////////////////
      // set vop time increment
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_vop_timing_cfg timeInc;
        timeInc.vop_time_resolution = pParam->nTimeIncRes;

        rc = ven_set_vop_timing(m_pDevice, &timeInc);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set time resolution");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set profile
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile profile;
        translate_profile(&profile.profile,
            pParam->eProfile,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_codec_profile(m_pDevice, &profile);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set profile");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set level
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile_level level;
        translate_level(&level.level,
            pParam->eLevel,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_profile_level(m_pDevice, &level);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set level");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set header extension coding in update_param_mpeg4(...)
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_header_extension hex;
        memset(&hex, 0, sizeof(ven_header_extension));
        if (pParam->nHeaderExtension > 0)
        {
          hex.hec_interval = pParam->nHeaderExtension;
          /* keep nHeaderExtension suplied by client with component */
		  m_sParamMPEG4.nHeaderExtension = pParam->nHeaderExtension;
		  m_sErrorCorrection.bEnableHEC = OMX_TRUE;
        }
        rc = ven_set_hec(m_pDevice, &hex);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set hex");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamMPEG4, pParam, sizeof(m_sParamMPEG4));
        m_sParamProfileLevel.eLevel = (OMX_U32) pParam->eLevel;
        m_sParamProfileLevel.eProfile = (OMX_U32) pParam->eProfile;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_profile_level(OMX_IN OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {

      ////////////////////////////////////////
      // set profile
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_profile profile;
        translate_profile(&profile.profile,
            m_sParamProfileLevel.eProfile,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_codec_profile(m_pDevice, &profile);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set profile");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set level
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile_level level;
        translate_level(&level.level,
            m_sParamProfileLevel.eLevel,
            m_sOutPortDef.format.video.eCompressionFormat);

        rc = ven_set_profile_level(m_pDevice, &level);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set level");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamProfileLevel, pParam, sizeof(m_sParamProfileLevel));
        m_sParamH263.eProfile = (OMX_VIDEO_H263PROFILETYPE) pParam->eProfile;
        m_sParamAVC.eProfile = (OMX_VIDEO_AVCPROFILETYPE) pParam->eProfile;
        m_sParamMPEG4.eProfile = (OMX_VIDEO_MPEG4PROFILETYPE) pParam->eProfile;
        m_sParamH263.eLevel = (OMX_VIDEO_H263LEVELTYPE) pParam->eLevel;
        m_sParamAVC.eLevel = (OMX_VIDEO_AVCLEVELTYPE) pParam->eLevel;
        m_sParamMPEG4.eLevel = (OMX_VIDEO_MPEG4LEVELTYPE) pParam->eLevel;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_err_correct(OMX_IN OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {

      ////////////////////////////////////////
      // set slice config
      ////////////////////////////////////////
      int nOutSize;
      if(pParam->bEnableResync) {
	result = is_multi_slice_mode_supported();
	if(OMX_ErrorNone == result) {
          struct ven_multi_slice_cfg slice;
          slice.mslice_mode = pParam->bEnableResync == OMX_TRUE ? VENC_SLICE_MODE_BIT : VENC_SLICE_MODE_DEFAULT;
          slice.mslice_size = pParam->nResynchMarkerSpacing;
	  rc = ven_set_multislice_cfg(m_pDevice, &slice);
	  if(rc) {
	    QC_OMX_MSG_ERROR("failed to set slice config");
	    result = translate_driver_error(GetLastError());
	  }
	}
      }

      ////////////////////////////////////////
      // set header extension coding in update_param_err_correct(...)
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_header_extension hex;
        memset(&hex, 0, sizeof(ven_header_extension));
        if (pParam->bEnableHEC == OMX_TRUE)
        {
          hex.hec_interval = 1;
          /* keep bEnableHEC suplied by client with component */
          m_sErrorCorrection.bEnableHEC = pParam->bEnableHEC;

          /* by this time, nHeaderExtension value supplied by client (through index OMX_IndexParamVideoMpeg4) */
          /* should reflect in nHeaderExtension, if not default value we set it as 1 will be used */
          if (m_sParamMPEG4.nHeaderExtension > 0)
          {
            hex.hec_interval = m_sParamMPEG4.nHeaderExtension;
          }
        }
        else
        {
          /* reset component HEC parameters */
          m_sErrorCorrection.bEnableHEC = OMX_FALSE;
          m_sParamMPEG4.nHeaderExtension = 0;
        }
        rc = ven_set_hec(m_pDevice, &hex);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set hex");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sErrorCorrection, pParam, sizeof(m_sErrorCorrection));
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_h263(OMX_IN OMX_VIDEO_PARAM_H263TYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // set intra period
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_intra_period intraPeriod;
        intraPeriod.num_pframes = pParam->nPFrames;
        if (ven_set_intra_period(m_pDevice,&intraPeriod) != 0 )

        {
          QC_OMX_MSG_ERROR("failed to set intra period");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set profile
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile profile;
        translate_profile(&profile.profile,
            m_sParamProfileLevel.eProfile,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_codec_profile(m_pDevice, &profile);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set profile");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set level
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile_level level;
        translate_level(&level.level,
            m_sParamProfileLevel.eLevel,
            m_sOutPortDef.format.video.eCompressionFormat);

        rc = ven_set_profile_level(m_pDevice, &level);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set level");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set slice
      ////////////////////////////////////////
      if (result == OMX_ErrorNone && pParam->nGOBHeaderInterval)
      {
	result = is_multi_slice_mode_supported();
	if(OMX_ErrorNone == result) {
          struct ven_multi_slice_cfg slice;
          slice.mslice_mode = pParam->nGOBHeaderInterval == 0 ? VENC_SLICE_MODE_DEFAULT : VENC_SLICE_MODE_GOB;
          slice.mslice_size = pParam->nGOBHeaderInterval;
          rc = ven_set_multislice_cfg(m_pDevice, &slice);
          if(rc) {
            QC_OMX_MSG_ERROR("failed to set slice config");
            result = translate_driver_error(GetLastError());
          }
	}
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamH263, pParam, sizeof(m_sParamH263));
        m_sParamProfileLevel.eProfile = pParam->eProfile;
        m_sParamProfileLevel.eLevel = pParam->eLevel;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_avc(OMX_IN OMX_VIDEO_PARAM_AVCTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // set intra period
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_intra_period intraPeriod;
        intraPeriod.num_pframes = pParam->nPFrames;
        if (ven_set_intra_period(m_pDevice,&intraPeriod) != 0 )
        {
          QC_OMX_MSG_ERROR("failed to set intra period");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set profile
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile profile;
        translate_profile(&profile.profile,
            pParam->eProfile,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_codec_profile(m_pDevice, &profile);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set profile");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set level
      ////////////////////////////////////////
      if (result == OMX_ErrorNone)
      {
        struct ven_profile_level level;
        translate_level(&level.level,
            pParam->eLevel,
            m_sOutPortDef.format.video.eCompressionFormat);
        rc = ven_set_profile_level(m_pDevice, &level);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set level");
          result = translate_driver_error(GetLastError());
        }
      }

      ////////////////////////////////////////
      // set slice
      ////////////////////////////////////////
      if (result == OMX_ErrorNone && pParam->nSliceHeaderSpacing)
      {
	result = is_multi_slice_mode_supported();
	if(OMX_ErrorNone == result) {
          struct ven_multi_slice_cfg slice;
          slice.mslice_mode = pParam->nSliceHeaderSpacing == 0 ? VENC_SLICE_MODE_DEFAULT : VENC_SLICE_MODE_MB;
          slice.mslice_size = pParam->nSliceHeaderSpacing;
          rc = ven_set_multislice_cfg(m_pDevice, &slice);
          if(rc)
          {
            QC_OMX_MSG_ERROR("failed to set slice config");
            result = translate_driver_error(GetLastError());
          }
	}
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamAVC, pParam, sizeof(m_sParamAVC));
        m_sParamProfileLevel.eProfile = pParam->eProfile;
        m_sParamProfileLevel.eLevel = pParam->eLevel;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_quantization(OMX_IN OMX_VIDEO_PARAM_QUANTIZATIONTYPE* pParam)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // set intra period
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_session_qp sessionQP;
        sessionQP.iframe_qp = pParam->nQpI;
        sessionQP.pframe_qp = pParam->nQpP;

        rc = ven_set_session_qp(m_pDevice, &sessionQP);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set session QPs");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamQPs, pParam, sizeof(m_sParamQPs));
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::update_param_intra_refresh(OMX_IN OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam)
{
  int rc = 0;
  OMX_ERRORTYPE result = OMX_ErrorNone;
  if (pParam != NULL)
  {
    if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // set intra refresh
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_intra_refresh ir;
        if (pParam->eRefreshMode == OMX_VIDEO_IntraRefreshCyclic)
        {
          ir.ir_mode = VEN_IR_RANDOM;
          ir.mb_count = 5;
        }
        else
        {
          ir.ir_mode = VEN_IR_OFF;
          ir.mb_count = 0;
        }

        rc = ven_set_intra_refresh_rate(m_pDevice, &ir);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to set intra refresh");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sParamIntraRefresh, pParam, sizeof(m_sParamIntraRefresh));
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

OMX_ERRORTYPE Venc::set_parameter(OMX_IN  OMX_HANDLETYPE hComponent,
                                  OMX_IN  OMX_INDEXTYPE nIndex,
                                  OMX_IN  OMX_PTR pCompParam)
{
  ///////////////////////////////////////////////////////////////////////////////
  // Supported Param Index                         Type
  // ============================================================================
  // OMX_IndexParamVideoPortFormat                 OMX_VIDEO_PARAM_PORTFORMATTYPE
  // OMX_IndexParamPortDefinition                  OMX_PARAM_PORTDEFINITIONTYPE
  // OMX_IndexParamVideoInit                       OMX_PORT_PARAM_TYPE
  // OMX_IndexParamVideoBitrate                    OMX_VIDEO_PARAM_BITRATETYPE
  // OMX_IndexParamVideoMpeg4                      OMX_VIDEO_PARAM_MPEG4TYPE
  // OMX_IndexParamVideoProfileLevelCurrent        OMX_VIDEO_PARAM_PROFILELEVEL
  ///////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (pCompParam == NULL)
  {
    QC_OMX_MSG_ERROR("param is null");
    return OMX_ErrorBadParameter;
  }

  if (m_eState != OMX_StateLoaded)
  {
    QC_OMX_MSG_ERROR("set_parameter must be in the loaded state");
    return OMX_ErrorIncorrectStateOperation;
  }

  switch (nIndex)
  {
    case OMX_IndexParamVideoPortFormat:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoPortFormat");
        result = update_param_port_fmt(reinterpret_cast<OMX_VIDEO_PARAM_PORTFORMATTYPE*>(pCompParam));
        if (result == OMX_ErrorNone)
            result = adjust_profile_level();
        break;
      }
    case OMX_IndexParamPortDefinition:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamPortDefinition");
        result = update_param_port_def(reinterpret_cast<OMX_PARAM_PORTDEFINITIONTYPE*>(pCompParam));
        if (result == OMX_ErrorNone)
            result = adjust_profile_level();
        break;
      }
    case OMX_IndexParamVideoInit:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoInit");
        result = update_param_video_init(reinterpret_cast<OMX_PORT_PARAM_TYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoBitrate:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoBitrate");
        result = update_param_bitrate(reinterpret_cast<OMX_VIDEO_PARAM_BITRATETYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoMpeg4:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoMpeg4");
        result = update_param_mpeg4(reinterpret_cast<OMX_VIDEO_PARAM_MPEG4TYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoProfileLevelCurrent:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoProfileLevelCurrent");
        result = update_param_profile_level(reinterpret_cast<OMX_VIDEO_PARAM_PROFILELEVELTYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoErrorCorrection:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoErrorCorrection");
        result = update_param_err_correct(reinterpret_cast<OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoH263:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoH263");
        result = update_param_h263(reinterpret_cast<OMX_VIDEO_PARAM_H263TYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoAvc:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoAvc");
        result = update_param_avc(reinterpret_cast<OMX_VIDEO_PARAM_AVCTYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoQuantization:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoQuantization");
        result = update_param_quantization(reinterpret_cast<OMX_VIDEO_PARAM_QUANTIZATIONTYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamVideoIntraRefresh:
      {
        QC_OMX_MSG_HIGH("OMX_IndexParamVideoIntraRefresh");
        result = update_param_intra_refresh(reinterpret_cast<OMX_VIDEO_PARAM_INTRAREFRESHTYPE*>(pCompParam));
        break;
      }
    case OMX_IndexParamStandardComponentRole:
      {
        OMX_PARAM_COMPONENTROLETYPE *comp_role;
        comp_role = (OMX_PARAM_COMPONENTROLETYPE *) pCompParam;
        QC_OMX_MSG_HIGH("OMX_IndexParamStandardComponentRole");
        if(!strncmp(m_pComponentName,"OMX.qcom.video.encoder.mpeg4",strlen("OMX.qcom.video.encoder.mpeg4")))
        {
          if(!strcmp((const char*)comp_role->cRole, pRoleMPEG4))
          {
            memcpy(comp_role->cRole, m_cRole, strlen((char*) m_cRole) + 1);
          }
          else
          {
            QC_OMX_MSG_ERROR("unknown Index");
            return OMX_ErrorUnsupportedSetting;
          }
        }
        else if(!strncmp(m_pComponentName,"OMX.qcom.video.encoder.h263",strlen("OMX.qcom.video.encoder.h263")))
        {
          if(!strcmp((const char*)comp_role->cRole, pRoleH263))
          {
            memcpy(comp_role->cRole, m_cRole, strlen((char*) m_cRole) + 1);
          }
          else
          {
            QC_OMX_MSG_ERROR("unknown Index");
            return OMX_ErrorUnsupportedSetting;
          }
        }
        else if(!strncmp(m_pComponentName,"OMX.qcom.video.encoder.avc",strlen("OMX.qcom.video.encoder.avc")))
        {
          if(!strcmp((const char*)comp_role->cRole, pRoleAVC))
          {
            memcpy(comp_role->cRole, m_cRole, strlen((char*) m_cRole) + 1);
          }
          else
          {
            QC_OMX_MSG_ERROR("unknown Index");
            return OMX_ErrorUnsupportedSetting;
          }
        }
        else
        {
          QC_OMX_MSG_ERROR("unknown param");
          return OMX_ErrorInvalidComponentName;
        }
        break;
      }

    case OMX_QcomIndexPlatformPvt:
      QC_OMX_MSG_HIGH("OMX_QcomIndexPlatformPvt");
      result = OMX_ErrorNone;
      break;
    default:
      QC_OMX_MSG_ERROR("unsupported index 0x%x", (int) nIndex);
      result = OMX_ErrorUnsupportedIndex;
      break;
  }
  return result;
}


OMX_ERRORTYPE Venc::get_config(OMX_IN  OMX_HANDLETYPE hComponent,
                               OMX_IN  OMX_INDEXTYPE nIndex,
                               OMX_INOUT OMX_PTR pCompConfig)
{
  ////////////////////////////////////////////////////////////////
  // Supported Config Index           Type
  // =============================================================
  // OMX_IndexConfigVideoBitrate      OMX_VIDEO_CONFIG_BITRATETYPE
  // OMX_IndexConfigVideoFramerate    OMX_CONFIG_FRAMERATETYPE
  // OMX_IndexConfigCommonRotate      OMX_CONFIG_ROTATIONTYPE
  ////////////////////////////////////////////////////////////////

  if (pCompConfig == NULL)
  {
    QC_OMX_MSG_ERROR("param is null");
    return OMX_ErrorBadParameter;
  }

  if (m_eState == OMX_StateInvalid)
  {
    QC_OMX_MSG_ERROR("can't be in invalid state");
    return OMX_ErrorIncorrectStateOperation;
  }

  //@todo need to validate params
  switch (nIndex)
  {
    case OMX_IndexConfigVideoBitrate:
      {
        OMX_VIDEO_CONFIG_BITRATETYPE* pParam = reinterpret_cast<OMX_VIDEO_CONFIG_BITRATETYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoBitrate");
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
        {
          memcpy(pParam, &m_sConfigBitrate, sizeof(m_sConfigBitrate));
        }
        else
        {
          QC_OMX_MSG_ERROR("bitrate is for output port");
          return OMX_ErrorBadPortIndex;
        }
        break;
      }
    case OMX_IndexConfigVideoFramerate:
      {
        OMX_CONFIG_FRAMERATETYPE* pParam = reinterpret_cast<OMX_CONFIG_FRAMERATETYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoFramerate");
        if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_IN)
        {
          memcpy(pParam, &m_sConfigFramerate, sizeof(m_sConfigFramerate));
        }
        else
        {
          QC_OMX_MSG_ERROR("framerate is for input port (refer to OMX IL spec)");
          return OMX_ErrorBadPortIndex;
        }
        break;
      }
    case OMX_IndexConfigCommonRotate:
      {
        OMX_CONFIG_ROTATIONTYPE* pParam = reinterpret_cast<OMX_CONFIG_ROTATIONTYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("OMX_IndexConfigCommonRotate");
        if(m_eState != OMX_StateLoaded)
        {
          // we only allow this at init time!
          QC_OMX_MSG_ERROR("rotation can only be configured in loaded state");
          return OMX_ErrorIncorrectStateOperation;
        }
        memcpy(pParam, &m_sConfigFrameRotation, sizeof(m_sConfigFrameRotation));
        break;
      }
    case OMX_IndexConfigVideoIntraVOPRefresh:
      {
        OMX_CONFIG_INTRAREFRESHVOPTYPE* pParam = reinterpret_cast<OMX_CONFIG_INTRAREFRESHVOPTYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoIntraVOPRefresh");
        memcpy(pParam, &m_sConfigIntraRefreshVOP, sizeof(m_sConfigIntraRefreshVOP));
        break;
      }
#ifdef QCOM_OMX_VENC_EXT
    case QOMX_IndexConfigVideoTemporalSpatialTradeOff:
      {
        QOMX_VIDEO_TEMPORALSPATIALTYPE* pParam = reinterpret_cast<QOMX_VIDEO_TEMPORALSPATIALTYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("QOMX_IndexConfigVideoTemporalSpatialTradeOff");
        memcpy(pParam, &m_sConfigQpRange, sizeof(m_sConfigQpRange));
        break;
      }
    case QOMX_IndexConfigVideoIntraperiod:
      {
        QOMX_VIDEO_INTRAPERIODTYPE* pParam = reinterpret_cast<QOMX_VIDEO_INTRAPERIODTYPE*>(pCompConfig);
        QC_OMX_MSG_HIGH("QOMX_IndexConfigVideoIntraperiod");
        memcpy(pParam, &m_sConfigIntraPeriod, sizeof(m_sConfigIntraPeriod));
        break;
      }
#endif
    case OMX_IndexConfigVideoNalSize:
      {
        OMX_VIDEO_CONFIG_NALSIZE* pParam = reinterpret_cast<OMX_VIDEO_CONFIG_NALSIZE*>(pCompConfig);
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoNalSize");
        memcpy(pParam, &m_sConfigNAL, sizeof(m_sConfigNAL));
        break;
      }
    default:
      QC_OMX_MSG_ERROR("unsupported index %d", (int) nIndex);
      return OMX_ErrorUnsupportedIndex;
      break;
  }
  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::update_config_bitrate(OMX_IN  OMX_VIDEO_CONFIG_BITRATETYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {

      ////////////////////////////////////////
      // set bitrate
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_target_bitrate bitrate;
        bitrate.target_bitrate = pConfig->nEncodeBitrate;

        if (ven_set_target_bitrate(m_pDevice,&bitrate ) != 0)
        {
          QC_OMX_MSG_ERROR("error setting bitrate");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sConfigBitrate, pConfig, sizeof(m_sConfigBitrate));
        // also need to set other params + config
        m_sConfigBitrate.nEncodeBitrate = pConfig->nEncodeBitrate;
        m_sParamBitrate.nTargetBitrate = pConfig->nEncodeBitrate;
        m_sOutPortDef.format.video.nBitrate = pConfig->nEncodeBitrate;
      }

    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  return result;
}

OMX_ERRORTYPE Venc::update_config_frame_rate(OMX_IN  OMX_CONFIG_FRAMERATETYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_IN)
    {
      ////////////////////////////////////////
      // set bitrate
      ////////////////////////////////////////
      int nOutSize;
      {
        struct ven_frame_rate frameRate;
        frameRate.fps_numerator = pConfig->xEncodeFramerate >> 16;  /// @integrate how to get the denominator from this
        frameRate.fps_denominator = 1;

        rc  = ven_set_frame_rate(m_pDevice, &frameRate);
        if(rc)
        {
          QC_OMX_MSG_ERROR("error setting frame rate");
          result = translate_driver_error(GetLastError());
        }
      }

      if (result == OMX_ErrorNone)
      {
        memcpy(&m_sConfigFramerate, pConfig, sizeof(m_sConfigFramerate));
        m_sConfigFramerate.xEncodeFramerate = pConfig->xEncodeFramerate;
        m_sInPortDef.format.video.xFramerate = pConfig->xEncodeFramerate;
        m_sInPortFormat.xFramerate = pConfig->xEncodeFramerate;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index. frame rate is for input port (refer to OMX IL spec)");
      result = OMX_ErrorBadPortIndex;
    }
  }
  return result;
}

OMX_ERRORTYPE Venc::update_config_rotate(OMX_IN  OMX_CONFIG_ROTATIONTYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      if(m_eState == OMX_StateLoaded)
      {

        ////////////////////////////////////////
        // set rotation
        ////////////////////////////////////////
        int nOutSize;
        {
          struct ven_rotation rotation;

          switch (pConfig->nRotation)
          {
            case 0:
              rotation.rotation = VEN_ROTATION_0;
              break;
            case 90:
              rotation.rotation = VEN_ROTATION_90;
              break;
            case 180:
              rotation.rotation = VEN_ROTATION_180;
              break;
            case 270:
              rotation.rotation = VEN_ROTATION_270;
              break;
            default:
              QC_OMX_MSG_ERROR("invalid rotation %d", (int) pConfig->nRotation);
              result = OMX_ErrorBadParameter;
              break;
          }

          if (result == OMX_ErrorNone)
          {
            rc = ven_set_rotation(m_pDevice, &rotation);
            if(rc)
            {
              QC_OMX_MSG_ERROR("failed to set rotation");
              result = translate_driver_error(GetLastError());
            }
          }
        }

        if (result == OMX_ErrorNone)
        {
          memcpy(&m_sConfigFrameRotation, pConfig, sizeof(m_sConfigFrameRotation));
        }
      }
      else
      {
        // we only allow this at init time!
        QC_OMX_MSG_ERROR("frame rate can only be configured in loaded state");
        result = OMX_ErrorIncorrectStateOperation;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  return result;
}

OMX_ERRORTYPE Venc::update_config_intra_vop_refresh(OMX_IN  OMX_CONFIG_INTRAREFRESHVOPTYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;
  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // request iframe
      ////////////////////////////////////////
      int nOutSize;
      if (pConfig->IntraRefreshVOP == OMX_TRUE)
      {
        QC_OMX_MSG_HIGH("request iframe");
        rc = ven_request_iframe(m_pDevice);
        if(rc)
        {
          QC_OMX_MSG_ERROR("failed to request iframe");
          result = translate_driver_error(GetLastError());
        }
      }
      else
      {
        QC_OMX_MSG_ERROR("why indicate you dont want an intra frame?");
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

#ifdef QCOM_OMX_VENC_EXT
OMX_ERRORTYPE Venc::update_config_qp_range(OMX_IN  QOMX_VIDEO_TEMPORALSPATIALTYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  if (pConfig != NULL)
  {
     if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
     {
       if (pConfig->nTSFactor > 0 && pConfig->nTSFactor <= 100) {
         int nOutSize;
	 struct ven_qp_range qp;
	 qp.min_qp = 2;
	 if(m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingMPEG4 ||
	    m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingH263) {
	   qp.max_qp = 8 + (int) roundingup(pConfig->nTSFactor * 0.23);
	 } else if(m_sOutPortDef.format.video.eCompressionFormat == OMX_VIDEO_CodingAVC) {
	   qp.max_qp = 33 + roundingup(pConfig->nTSFactor * 0.18);
	 }
	 QC_OMX_MSG_HIGH("setting qp range");
	 if (ven_set_qp_range(m_pDevice, &qp) != 0)
	 {
	   QC_OMX_MSG_ERROR("failed to set qp range");
	   result = translate_driver_error(GetLastError());
	 }
	 else
	 {
	   memcpy(&m_sConfigQpRange, pConfig, sizeof(m_sConfigQpRange));
	 }
       }
     }
     else
     {
       QC_OMX_MSG_ERROR("bad port index");
       result = OMX_ErrorBadPortIndex;
     }
   }
   else
   {
     QC_OMX_MSG_ERROR("null param");
     result = OMX_ErrorBadParameter;
   }
   return result;
}
#endif

OMX_ERRORTYPE Venc::update_config_nal_size(OMX_IN  OMX_VIDEO_CONFIG_NALSIZE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int rc = 0;

  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      result = is_multi_slice_mode_supported();
      if(OMX_ErrorNone == result) {
        int nOutSize;
        struct ven_multi_slice_cfg slice;
        slice.mslice_mode = VENC_SLICE_MODE_BIT;
	slice.mslice_size = (unsigned long) pConfig->nNaluBytes * 8;
        rc = ven_set_multislice_cfg(m_pDevice, &slice);
	if(rc)
	{
	  QC_OMX_MSG_ERROR("failed to set slice config");
	  result = translate_driver_error(GetLastError());
	}
	else
	{
	  memcpy(&m_sConfigNAL, pConfig, sizeof(m_sConfigNAL));
	}
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}

#ifdef QCOM_OMX_VENC_EXT
OMX_ERRORTYPE Venc::update_config_intra_period(OMX_IN  QOMX_VIDEO_INTRAPERIODTYPE* pConfig)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (pConfig != NULL)
  {
    if (pConfig->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
    {
      ////////////////////////////////////////
      // request iframe
      ////////////////////////////////////////
      int nOutSize;
      struct ven_intra_period intra;
      intra.num_pframes = (unsigned long) pConfig->nPFrames;
      QC_OMX_MSG_HIGH("setting intra period");
      if (ven_set_intra_period(m_pDevice, &intra)  != 0 )
      {
        QC_OMX_MSG_ERROR("failed to set intra period");
        result = translate_driver_error(GetLastError());
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("bad port index");
      result = OMX_ErrorBadPortIndex;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null param");
    result = OMX_ErrorBadParameter;
  }
  return result;
}
#endif

OMX_ERRORTYPE Venc::set_config(OMX_IN  OMX_HANDLETYPE hComponent,
                               OMX_IN  OMX_INDEXTYPE nIndex,
                               OMX_IN  OMX_PTR pCompConfig)
{

  ////////////////////////////////////////////////////////////////
  // Supported Config Index           Type
  // =============================================================
  // OMX_IndexConfigVideoBitrate      OMX_VIDEO_CONFIG_BITRATETYPE
  // OMX_IndexConfigVideoFramerate    OMX_CONFIG_FRAMERATETYPE
  // OMX_IndexConfigCommonRotate      OMX_CONFIG_ROTATIONTYPE
  ////////////////////////////////////////////////////////////////

  if (pCompConfig == NULL)
  {
    QC_OMX_MSG_ERROR("param is null");
    return OMX_ErrorBadParameter;
  }

  OMX_ERRORTYPE result = OMX_ErrorNone;

  switch (nIndex)
  {
    case OMX_IndexConfigVideoBitrate:
      {
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoBitrate");
        result = update_config_bitrate(reinterpret_cast<OMX_VIDEO_CONFIG_BITRATETYPE*>(pCompConfig));
        break;
      }
    case OMX_IndexConfigVideoFramerate:
      {
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoFramerate");
        result = update_config_frame_rate(reinterpret_cast<OMX_CONFIG_FRAMERATETYPE*>(pCompConfig));
        break;
      }
    case OMX_IndexConfigCommonRotate:
      {
        QC_OMX_MSG_HIGH("OMX_IndexConfigCommonRotate");
        result = update_config_rotate(reinterpret_cast<OMX_CONFIG_ROTATIONTYPE*>(pCompConfig));
        break;
      }
    case OMX_IndexConfigVideoIntraVOPRefresh:
      {
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoIntraVOPRefresh");
        result = update_config_intra_vop_refresh(reinterpret_cast<OMX_CONFIG_INTRAREFRESHVOPTYPE*>(pCompConfig));
        break;
      }
#ifdef QCOM_OMX_VENC_EXT
    case QOMX_IndexConfigVideoTemporalSpatialTradeOff:
      {
        QC_OMX_MSG_HIGH("QOMX_IndexConfigVideoTemporalSpatialTradeOff");
        result = update_config_qp_range(reinterpret_cast<QOMX_VIDEO_TEMPORALSPATIALTYPE*>(pCompConfig));
        break;
      }
    case QOMX_IndexConfigVideoIntraperiod:
      {
        QC_OMX_MSG_HIGH("QOMX_IndexConfigVideoIntraperiod");
        result = update_config_intra_period(reinterpret_cast<QOMX_VIDEO_INTRAPERIODTYPE*>(pCompConfig));
        break;
      }
#endif
    case OMX_IndexConfigVideoNalSize:
      {
        QC_OMX_MSG_HIGH("OMX_IndexConfigVideoNalSize");
        result = update_config_nal_size(reinterpret_cast<OMX_VIDEO_CONFIG_NALSIZE*>(pCompConfig));
        break;
      }
    default:
      QC_OMX_MSG_ERROR("unsupported index %d", (int) nIndex);
      result = OMX_ErrorUnsupportedIndex;
      break;
  }

  return result;
}


OMX_ERRORTYPE Venc::get_extension_index(OMX_IN  OMX_HANDLETYPE hComponent,
                                        OMX_IN  OMX_STRING cParameterName,
                                        OMX_OUT OMX_INDEXTYPE* pIndexType)
{
  return OMX_ErrorNotImplemented;
}


OMX_ERRORTYPE Venc::get_state(OMX_IN  OMX_HANDLETYPE hComponent,
                              OMX_OUT OMX_STATETYPE* pState)
{
  (void) hComponent;
  if (pState == NULL)
  {
    return OMX_ErrorBadParameter;
  }
  *pState = m_eState;
  return OMX_ErrorNone;
}


OMX_ERRORTYPE Venc::component_tunnel_request(OMX_IN  OMX_HANDLETYPE hComp,
                                             OMX_IN  OMX_U32 nPort,
                                             OMX_IN  OMX_HANDLETYPE hTunneledComp,
                                             OMX_IN  OMX_U32 nTunneledPort,
                                             OMX_INOUT  OMX_TUNNELSETUPTYPE* pTunnelSetup)
{
  (void) hComp;
  (void) nPort;
  (void) hTunneledComp;
  (void) nTunneledPort;
  (void) pTunnelSetup;
  return OMX_ErrorTunnelingUnsupported;
}

OMX_ERRORTYPE Venc::use_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                               OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
                               OMX_IN OMX_U32 nPortIndex,
                               OMX_IN OMX_PTR pAppPrivate,
                               OMX_IN OMX_U32 nSizeBytes,
                               OMX_IN OMX_U8* pBuffer)
{

  if (ppBufferHdr == NULL || nSizeBytes == 0 || pBuffer == NULL)
  {
    QC_OMX_MSG_ERROR("bad param %p %lu %p",ppBufferHdr, nSizeBytes, pBuffer);
    return OMX_ErrorBadParameter;
  }

  if(m_eState == OMX_StateInvalid)
  {
    QC_OMX_MSG_ERROR("UseBuffer - called in Invalid State");
    return OMX_ErrorInvalidState;
  }
  if (nPortIndex == (OMX_U32) PORT_INDEX_IN)
  {
    QC_OMX_MSG_HIGH("client allocated input buffer %p for component", pBuffer);
    if (nSizeBytes != m_sInPortDef.nBufferSize)
    {
      QC_OMX_MSG_ERROR("buffer size(%lu) does not match our requirements(%lu)",
          nSizeBytes,m_sInPortDef.nBufferSize);
      return OMX_ErrorBadParameter;
    }

    if (m_nInBuffAllocated == 0)
    {
      m_pPrivateInPortData = new PrivatePortData[m_sInPortDef.nBufferCountActual];
      m_pInBuffHeaders = new OMX_BUFFERHEADERTYPE[m_sInPortDef.nBufferCountActual];
      memset(m_pPrivateInPortData, 0, sizeof(PrivatePortData) * m_sInPortDef.nBufferCountActual);
      memset(m_pInBuffHeaders, 0, sizeof(OMX_BUFFERHEADERTYPE) * m_sInPortDef.nBufferCountActual);
    }

#if 0
    int bFail;
    int nOutSize;

    QC_OMX_MSG_LOW("Input buffer: fd %d, offset: %d", ((struct venc_pmem *)pAppPrivate)->fd,
        ((struct venc_pmem *)pAppPrivate)->offset);

    bFail = DeviceIoControl(m_nFd,
        VENC_IOCTL_SET_INPUT_BUFFER,
        pAppPrivate);
    if (bFail)
    {
      QC_OMX_MSG_ERROR("failed to set buffer");
      return OMX_ErrorUndefined;
    }
#endif

    OMX_U32 i;
    for (i = 0; i < m_sInPortDef.nBufferCountActual; i++)
    {
      if (m_pInBuffHeaders[i].nAllocLen == 0)
      {
        OMX_INIT_STRUCT(&m_pInBuffHeaders[i], OMX_BUFFERHEADERTYPE);
        m_pInBuffHeaders[i].pBuffer = pBuffer;
        m_pInBuffHeaders[i].nAllocLen = nSizeBytes;
        m_pInBuffHeaders[i].pAppPrivate = pAppPrivate;
        m_pInBuffHeaders[i].nInputPortIndex = (OMX_U32) PORT_INDEX_IN;
        m_pInBuffHeaders[i].nOutputPortIndex = (OMX_U32) PORT_INDEX_NONE;
        m_pInBuffHeaders[i].pInputPortPrivate = &m_pPrivateInPortData[i];
        m_pPrivateInPortData[i].bComponentAllocated = OMX_FALSE;

        *ppBufferHdr = &m_pInBuffHeaders[i];

        ++m_nInBuffAllocated;
        break;
      }
    }
    if (m_nInBuffAllocated == m_sInPortDef.nBufferCountActual)
    {
      QC_OMX_MSG_HIGH("I/P port populated");
      m_sInPortDef.bPopulated = OMX_TRUE;
    }
    if (i == m_sInPortDef.nBufferCountActual)
    {
      QC_OMX_MSG_ERROR("could not find free buffer");
      return OMX_ErrorUndefined;
    }
  }
  else if (nPortIndex == (OMX_U32) PORT_INDEX_OUT)
  {
    QC_OMX_MSG_HIGH("client allocated output buffer 0x%x for component",(int) pBuffer);
    if (nSizeBytes != m_sOutPortDef.nBufferSize)
    {
      QC_OMX_MSG_ERROR("buffer size does not match our requirements");
      return OMX_ErrorBadParameter;
    }

    if (m_nOutBuffAllocated == 0)
    {
      m_pPrivateOutPortData = new PrivatePortData[m_sOutPortDef.nBufferCountActual];
      m_pOutBuffHeaders = new OMX_BUFFERHEADERTYPE[m_sOutPortDef.nBufferCountActual];
      memset(m_pPrivateOutPortData, 0, sizeof(PrivatePortData) * m_sOutPortDef.nBufferCountActual);
      memset(m_pOutBuffHeaders, 0, sizeof(OMX_BUFFERHEADERTYPE) * m_sOutPortDef.nBufferCountActual);
    }

#if 0
    int bFail;
    int nOutSize;

    bFail = DeviceIoControl(m_nFd,
        VENC_IOCTL_SET_OUTPUT_BUFFER,
        pAppPrivate);
    if (bFail)
    {
      QC_OMX_MSG_ERROR("failed to set buffer");
      return OMX_ErrorUndefined;
    }
#endif
    OMX_U32 i;
    for (i = 0; i < m_sOutPortDef.nBufferCountActual; i++)
    {
      if (m_pOutBuffHeaders[i].nAllocLen == 0)
      {
        OMX_INIT_STRUCT(&m_pOutBuffHeaders[i], OMX_BUFFERHEADERTYPE);
        m_pOutBuffHeaders[i].pBuffer = pBuffer;
        m_pOutBuffHeaders[i].nAllocLen = nSizeBytes;
        m_pOutBuffHeaders[i].pAppPrivate = pAppPrivate;
        m_pOutBuffHeaders[i].nInputPortIndex = (OMX_U32) PORT_INDEX_NONE;
        m_pOutBuffHeaders[i].nOutputPortIndex = (OMX_U32) PORT_INDEX_OUT;
        m_pOutBuffHeaders[i].pOutputPortPrivate = &m_pPrivateOutPortData[i];
        m_pPrivateOutPortData[i].bComponentAllocated = OMX_FALSE;

        *ppBufferHdr = &m_pOutBuffHeaders[i];

        ++m_nOutBuffAllocated;
        break;
      }
    }
    if (m_nOutBuffAllocated == m_sOutPortDef.nBufferCountActual)
    {
      QC_OMX_MSG_HIGH("I/P port populated");
      m_sOutPortDef.bPopulated = OMX_TRUE;
    }

    if (i == m_sOutPortDef.nBufferCountActual)
    {
      QC_OMX_MSG_ERROR("could not find free buffer");
      return OMX_ErrorUndefined;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("invalid port index");
    return OMX_ErrorBadPortIndex;
  }
  if (BITMASK_PRESENT(m_nFlags, OMX_COMPONENT_IDLE_PENDING) &&
      m_sInPortDef.bPopulated && m_sOutPortDef.bPopulated)
  {
    QC_OMX_MSG_HIGH("Ports populated, go to idle state");
    m_eState = OMX_StateIdle;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StateIdle, NULL);
  }

  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::allocate_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** ppBuffer,
                                    OMX_IN OMX_U32 nPortIndex,
                                    OMX_IN OMX_PTR pAppPrivate,
                                    OMX_IN OMX_U32 nSizeBytes)
{
  QC_OMX_MSG_HIGH("Attempt to allocate buffer of %d bytes", (int) nSizeBytes);
  int cnt, i, bFail;
  OMX_ERRORTYPE result = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE* pBufferHdr = NULL;
  PrivatePortData* pPrivateData;

  if (ppBuffer == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  if(m_eState == OMX_StateInvalid)
  {
    QC_OMX_MSG_ERROR("AllocateBuffer - called in Invalid State");
    return OMX_ErrorInvalidState;
  }

  if (nPortIndex == (OMX_U32) PORT_INDEX_OUT)
  {
    if (nSizeBytes == m_sOutPortDef.nBufferSize)
    {
      cnt = (int) m_sOutPortDef.nBufferCountActual;

      if (m_nOutBuffAllocated == 0)
      {
        m_pPrivateOutPortData = new PrivatePortData[m_sOutPortDef.nBufferCountActual];
        m_pOutBuffHeaders = new OMX_BUFFERHEADERTYPE[m_sOutPortDef.nBufferCountActual];
        memset(m_pPrivateOutPortData, 0, sizeof(PrivatePortData) * m_sOutPortDef.nBufferCountActual);
        memset(m_pOutBuffHeaders, 0, sizeof(OMX_BUFFERHEADERTYPE) * m_sOutPortDef.nBufferCountActual);
      }

      for (i = 0; i < cnt; i++)
      {
        if (m_pOutBuffHeaders[i].pBuffer == NULL)
        {
          OMX_INIT_STRUCT(&m_pOutBuffHeaders[i], OMX_BUFFERHEADERTYPE);
          pBufferHdr = &m_pOutBuffHeaders[i];
          pPrivateData = &m_pPrivateOutPortData[i];
          break;
        }
      }

      if (pBufferHdr != NULL)
      {

        result = pmem_alloc(&pPrivateData->sPmemInfo,
            m_sOutPortDef.nBufferSize, VENC_PMEM_EBI1);
        if (result != OMX_ErrorNone) {
          QC_OMX_MSG_ERROR("Failed to allocate pmem buffer");
        }

        bFail = DeviceIoControl(m_nFd,
            VENC_IOCTL_SET_OUTPUT_BUFFER,
            &(pPrivateData->sPmemInfo));

        if (!bFail)
        {
          pBufferHdr->pBuffer =(OMX_U8 *)(pPrivateData->sPmemInfo).virt;
          pBufferHdr->nAllocLen = m_sOutPortDef.nBufferSize;
          pBufferHdr->nAllocLen = nSizeBytes;
          //pBufferHdr->pAppPrivate = pAppPrivate;
          OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* private_info =
            (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* )malloc(
                sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO));
          private_info->pmem_fd = pPrivateData->sPmemInfo.fd;
          private_info->offset = pPrivateData->sPmemInfo.offset;
          pBufferHdr->pPlatformPrivate = (OMX_PTR)private_info;
          pBufferHdr->nInputPortIndex = (OMX_U32) PORT_INDEX_NONE;
          pBufferHdr->nOutputPortIndex = (OMX_U32) PORT_INDEX_OUT;
          pBufferHdr->pOutputPortPrivate = pPrivateData;
          pPrivateData->bComponentAllocated = OMX_TRUE;
          *ppBuffer = pBufferHdr;
          ++m_nOutBuffAllocated;
        }
        else
        {
          QC_OMX_MSG_ERROR("unable to set output  buffer");
          result = OMX_ErrorUndefined;
        }
      } // pBufferHdr
      if (m_nOutBuffAllocated == m_sOutPortDef.nBufferCountActual)
      {
        QC_OMX_MSG_HIGH("O/P port populated");
        m_sOutPortDef.bPopulated = OMX_TRUE;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("buffer size does not match our requirements %lu %lu",
          nSizeBytes, m_sOutPortDef.nBufferSize);
      result = OMX_ErrorBadParameter;
    }
  }

  if (nPortIndex == (OMX_U32) PORT_INDEX_IN)
  {
    if (nSizeBytes == m_sInPortDef.nBufferSize)
    {
      cnt = (int) m_sInPortDef.nBufferCountActual;

      if (m_nInBuffAllocated == 0)
      {
        m_pPrivateInPortData = new PrivatePortData[m_sInPortDef.nBufferCountActual];
        m_pInBuffHeaders = new OMX_BUFFERHEADERTYPE[m_sInPortDef.nBufferCountActual];
        memset(m_pPrivateInPortData, 0, sizeof(PrivatePortData) * m_sInPortDef.nBufferCountActual);
        memset(m_pInBuffHeaders, 0, sizeof(OMX_BUFFERHEADERTYPE) * m_sInPortDef.nBufferCountActual);
      }

      for (i = 0; i < cnt; i++)
      {
        if (m_pInBuffHeaders[i].pBuffer == NULL)
        {
          OMX_INIT_STRUCT(&m_pInBuffHeaders[i], OMX_BUFFERHEADERTYPE);
          pBufferHdr = &m_pInBuffHeaders[i];
          pPrivateData = &m_pPrivateInPortData[i];
          break;
        }
      }

      if (pBufferHdr != NULL)
      {

        result = pmem_alloc(&pPrivateData->sPmemInfo,
            m_sInPortDef.nBufferSize, VENC_PMEM_EBI1);
        if (result != OMX_ErrorNone) {
          QC_OMX_MSG_ERROR("Failed to allocate pmem buffer");
        }
        bFail = DeviceIoControl(m_nFd,
            VENC_IOCTL_SET_INPUT_BUFFER,
            &(pPrivateData->sPmemInfo));

        if (!bFail)
        {
          pBufferHdr->pBuffer = (OMX_U8 *)(pPrivateData->sPmemInfo).virt;
          pBufferHdr->nAllocLen = m_sInPortDef.nBufferSize;
          //pBufferHdr->pAppPrivate = pAppPrivate;
          OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* private_info =
            (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* )malloc(
                sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO));
          private_info->pmem_fd = pPrivateData->sPmemInfo.fd;
          private_info->offset = pPrivateData->sPmemInfo.offset;
          pBufferHdr->pPlatformPrivate = (OMX_PTR)private_info;
          pBufferHdr->nInputPortIndex = (OMX_U32) PORT_INDEX_IN;
          pBufferHdr->nOutputPortIndex = (OMX_U32) PORT_INDEX_NONE;
          pBufferHdr->pInputPortPrivate = pPrivateData;
          pPrivateData->bComponentAllocated = OMX_TRUE;
          *ppBuffer = pBufferHdr;
          ++m_nInBuffAllocated;
        }
        else
        {
          QC_OMX_MSG_ERROR("unable to set input buffer");
          result = OMX_ErrorUndefined;
        }
      } // pBufferHdr

      if (m_nInBuffAllocated == m_sInPortDef.nBufferCountActual)
      {
        QC_OMX_MSG_HIGH("I/P port populated");
        m_sInPortDef.bPopulated = OMX_TRUE;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("buffer size does not match our requirements %lu %lu",
          nSizeBytes, m_sInPortDef.nBufferSize);
      result = OMX_ErrorBadParameter;
    }
  }

  if (BITMASK_PRESENT(m_nFlags, OMX_COMPONENT_IDLE_PENDING) &&
      m_sInPortDef.bPopulated && m_sOutPortDef.bPopulated)
  {
    QC_OMX_MSG_HIGH("Ports populated, go to idle state");
    m_eState = OMX_StateIdle;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StateIdle, NULL);
  }
  return result;
}

OMX_ERRORTYPE Venc::free_buffer(OMX_IN  OMX_HANDLETYPE hComponent,
                                OMX_IN  OMX_U32 nPortIndex,
                                OMX_IN  OMX_BUFFERHEADERTYPE* pBufferHdr)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (! pBufferHdr || !(pBufferHdr->pBuffer))
  {
    QC_OMX_MSG_ERROR("null param");
    return OMX_ErrorBadParameter;
  }

  QC_OMX_MSG_MEDIUM("freeing 0x%x", pBufferHdr->pBuffer);
  if (nPortIndex == PORT_INDEX_OUT)
  {
    PrivatePortData* pPortData =
      ((PrivatePortData*) pBufferHdr->pOutputPortPrivate);

    if (pPortData->bComponentAllocated) {
      result = pmem_free(&pPortData->sPmemInfo);
      if (result != OMX_ErrorNone) {
        QC_OMX_MSG_ERROR("failed to free buffer");
      }
      free(pBufferHdr->pPlatformPrivate);
      pBufferHdr->pPlatformPrivate = NULL;
    }
    else {
      QC_OMX_MSG_HIGH("No need to free output buffer allocated by clients");
    }

    // indicate buffer is available for alloc again
    pBufferHdr->pBuffer = NULL;
    pBufferHdr->nAllocLen = 0;
    m_sOutPortDef.bPopulated = OMX_FALSE;
    --m_nOutBuffAllocated;

    if (m_nOutBuffAllocated == 0)
    {
      delete [] m_pPrivateOutPortData;
      delete [] m_pOutBuffHeaders;
    }
    QC_OMX_MSG_HIGH("Done with free_buffer, output");
  }
  else if (nPortIndex == PORT_INDEX_IN)
  {
    PrivatePortData* pPortData =
      ((PrivatePortData*) pBufferHdr->pInputPortPrivate);

    if (pPortData->bComponentAllocated) {
      result = pmem_free(&pPortData->sPmemInfo);
      if (result != OMX_ErrorNone) {
        QC_OMX_MSG_ERROR("failed to free buffer");
      }
      free(pBufferHdr->pPlatformPrivate);
      pBufferHdr->pPlatformPrivate = NULL;
    }
    else {
      QC_OMX_MSG_HIGH("No need to free input buffer allocated by clients");
    }

    // indicate buffer is available for alloc again
    pBufferHdr->pBuffer = NULL;
    pBufferHdr->nAllocLen = 0;
    m_sInPortDef.bPopulated = OMX_FALSE;
    --m_nInBuffAllocated;

    if (m_nInBuffAllocated == 0)
    {
      delete [] m_pPrivateInPortData;
      delete [] m_pInBuffHeaders;
    }
    QC_OMX_MSG_HIGH("Done with free_buffer, input");
  }
  else
  {
    QC_OMX_MSG_ERROR("invalid port");
    result = OMX_ErrorBadParameter;
  }
  if (BITMASK_PRESENT((m_nFlags), OMX_COMPONENT_LOADING_PENDING) &&
      (m_nInBuffAllocated == 0) && (m_nOutBuffAllocated == 0))
  {
    QC_OMX_MSG_PROFILE("free buffer done, switch to loaded state");
    m_eState = OMX_StateLoaded;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        m_eState,
        NULL);
  }
  return result;
}

OMX_ERRORTYPE Venc::allocate_q6_buffers(struct venc_buffers *pbufs)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int i, size, nReconSize, nWbSize, nCmdSize, nVlcSize;
  int width, height;

  QC_OMX_MSG_HIGH("Attempt to allocate q6 buffers ");
  
  if (m_pDevice->config.rotation.rotation == VEN_ROTATION_90 ||
      m_pDevice->config.rotation.rotation == VEN_ROTATION_270)
  {
    height = m_sOutPortDef.format.video.nFrameWidth;
    width  = m_sOutPortDef.format.video.nFrameHeight;
  }
  else
  {
    width  = m_sOutPortDef.format.video.nFrameWidth;
    height = m_sOutPortDef.format.video.nFrameHeight;
  }
  
  if(OMX_ErrorNone != is_multi_slice_mode_supported()) {
    nCmdSize = width * height * 3 / 2;
  } else {
    /* AVC codec and resolution is less than VGA */
    /* slice mode is supported here, so allocate */
    /* extra command buffer size                 */
    nCmdSize = width * height * 4;
  }
  
  /* nVlcSize = max coefficients in a MB row (all co-effs are non zero) */
  /* Luma MB co-eff's      = 16*16 */ 
  /* 2 Chroma MBs co-eff's = 2*8*8 */
  /* total of 384 * 8  = 3072 bits */
  nVlcSize = 3072 * width / 16;
  
  /* aligning width to 2 MBs, requirement from 720p optimizations */
  nReconSize = nWbSize = (((width + 31) >> 5) << 5) * height * 3 / 2;
  
  // allocate recon buffers
  for (i = 0; i < VENC_MAX_RECON_BUFFERS && result == OMX_ErrorNone; i++)
  {
    result = pmem_alloc(&(pbufs->recon_buf[i]), nReconSize, VENC_PMEM_SMI);
    if (result == OMX_ErrorNone)
    {
      QC_OMX_MSG_HIGH("allocated recon_buff[%d]: pVirt=0x%x, nPhy=0x%x",i,
          pbufs->recon_buf[i].virt,
          pbufs->recon_buf[i].phys);
      QC_OMX_MSG_HIGH("allocated recon_buf[%d]: fd=%d, offset=%d src=%d",i,
          pbufs->recon_buf[i].fd,
          pbufs->recon_buf[i].offset,
	  pbufs->recon_buf[i].src);
    }
    else
    {
      QC_OMX_MSG_ERROR("failed to allocate recon buffer");
    }
  }

  // allocate wb buffer
  if (result == OMX_ErrorNone)
  {
    result = pmem_alloc(&(pbufs->wb_buf), nWbSize, VENC_PMEM_SMI);
    if (result == OMX_ErrorNone)
    {
      QC_OMX_MSG_HIGH("allocated wb buffer: pVirt=0x%x, nPhy=0x%x",
          pbufs->wb_buf.virt,
          pbufs->wb_buf.phys);
      QC_OMX_MSG_HIGH("allocated wb buffer: fd=%d, offset=%d src=%d",
          pbufs->wb_buf.fd,
          pbufs->wb_buf.offset,
	  pbufs->wb_buf.src);
    }
    else
    {
      QC_OMX_MSG_ERROR("failed to allocate wb buffer");
      goto err_wb_buf_allocation;
    }
  }

  if (result == OMX_ErrorNone)
  {
    result = pmem_alloc(&(pbufs->cmd_buf), nCmdSize, VENC_PMEM_SMI);
    if (result == OMX_ErrorNone)
    {
      QC_OMX_MSG_HIGH("allocated cmd buffer: pVirt=0x%x, nPhy=0x%x",
          pbufs->cmd_buf.virt,
          pbufs->cmd_buf.phys);
      QC_OMX_MSG_HIGH("allocated cmd buffer: fd=%d, offset=%d src=%d",
          pbufs->cmd_buf.fd,
          pbufs->cmd_buf.offset,
	  pbufs->cmd_buf.src);
    }
    else
    {
      QC_OMX_MSG_ERROR("failed to allocate cmd buffer");
      goto err_cmd_buf_allocation;
    }
  }

  if (result == OMX_ErrorNone)
  {
    result = pmem_alloc(&(pbufs->vlc_buf), nVlcSize, VENC_PMEM_EBI1);
    if (result == OMX_ErrorNone)
    {
      QC_OMX_MSG_HIGH("allocated vlc buffer: pVirt=0x%x, nPhy=0x%x",
          pbufs->vlc_buf.virt,
          pbufs->vlc_buf.phys);
      QC_OMX_MSG_HIGH("allocated vlc buffer: fd=%d, offset=%d src=%d",
          pbufs->vlc_buf.fd,
          pbufs->vlc_buf.offset,
	  pbufs->vlc_buf.src);
    }
    else
    {
      QC_OMX_MSG_ERROR("failed to allocate vlc buffer");
      goto err_vlc_buf_allocation;
    }
  }
  return result;

err_vlc_buf_allocation:
  (void)pmem_free(&(pbufs->cmd_buf));
err_cmd_buf_allocation:
  (void)pmem_free(&(pbufs->wb_buf));
err_wb_buf_allocation:
  for (i = 0; i < VENC_MAX_RECON_BUFFERS; i++)
  {
    if (pbufs->recon_buf[i].virt != NULL)
    {
      (void) pmem_free(&(pbufs->recon_buf[i]));
    }
  }
  return result;
}


OMX_ERRORTYPE Venc::free_q6_buffers(struct venc_buffers *pbufs)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  int i;

  QC_OMX_MSG_HIGH("free q6 buffers");
  for (i = 0; i < VENC_MAX_RECON_BUFFERS; i++)
  {
    result = pmem_free(&(pbufs->recon_buf[i]));

    if (result != OMX_ErrorNone)
    {
      QC_OMX_MSG_ERROR("failed to free recon buf");
    }
  }

  result = pmem_free(&(pbufs->wb_buf));
  if (result != OMX_ErrorNone)
  {
    QC_OMX_MSG_ERROR("failed to free wb buf");
  }

  result = pmem_free(&(pbufs->cmd_buf));
  if (result != OMX_ErrorNone)
  {
    QC_OMX_MSG_ERROR("failed to free command buf");
  }

  result = pmem_free(&(pbufs->vlc_buf));
  if (result != OMX_ErrorNone)
  {
    QC_OMX_MSG_ERROR("failed to free VLC buf");
  }
  return result;
}


OMX_ERRORTYPE Venc::empty_this_buffer(OMX_IN  OMX_HANDLETYPE hComponent,
                                      OMX_IN  OMX_BUFFERHEADERTYPE* pInBuffer)
{
  (void) hComponent;

  QC_OMX_MSG_LOW("emptying buffer...");

  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (pInBuffer != NULL)
  {
    VencMsgQ::MsgDataType msgData;
    msgData.pBuffer = pInBuffer;
    result = m_pMsgQ->PushMsg(VencMsgQ::MSG_ID_EMPTY_BUFFER, &msgData);
  }
  else
  {
    QC_OMX_MSG_ERROR("buffer header is null");
    result = OMX_ErrorBadParameter;
  }

  return result;
}

OMX_ERRORTYPE Venc::fill_this_buffer(OMX_IN  OMX_HANDLETYPE hComponent,
                                     OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer)
{
  (void) hComponent;

  QC_OMX_MSG_LOW("Fill buffer with hdr:0x%x",pBuffer);

  OMX_ERRORTYPE result = OMX_ErrorNone;

  if (pBuffer != NULL)
  {
    VencMsgQ::MsgDataType msgData;
    msgData.pBuffer = pBuffer;
    result = m_pMsgQ->PushMsg(VencMsgQ::MSG_ID_FILL_BUFFER, &msgData);
  }
  else
  {
    QC_OMX_MSG_ERROR("buffer header is null");
    result = OMX_ErrorBadParameter;
  }

  return result;
}

OMX_ERRORTYPE Venc::set_callbacks(OMX_IN  OMX_HANDLETYPE hComponent,
                                  OMX_IN  OMX_CALLBACKTYPE* pCallbacks,
                                  OMX_IN  OMX_PTR pAppData)
{
  if (pCallbacks == NULL ||
      hComponent == NULL ||
      pCallbacks->EmptyBufferDone == NULL ||
      pCallbacks->FillBufferDone == NULL ||
      pCallbacks->EventHandler == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  m_sCallbacks.EventHandler = pCallbacks->EventHandler;
  m_sCallbacks.EmptyBufferDone = pCallbacks->EmptyBufferDone;
  m_sCallbacks.FillBufferDone = pCallbacks->FillBufferDone;
  m_pAppData = pAppData;
  m_hSelf = hComponent;

  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::component_deinit(OMX_IN  OMX_HANDLETYPE hComponent)
{
  OMX_ERRORTYPE result = OMX_ErrorNone;
  QC_OMX_MSG_HIGH("deinitializing component...");

  if (m_pMsgQ->PushMsg(VencMsgQ::MSG_ID_EXIT,
        NULL) == OMX_ErrorNone)
  {
    pthread_join(m_ComponentThread, NULL);
  }
  else
  {
    QC_OMX_MSG_ERROR("failed to send thread exit msg");
    return OMX_ErrorUndefined;
  }

  if (DeviceIoControl(m_nFd,
        VENC_IOCTL_CMD_STOP_READ_MSG,
        NULL,
        0,
        NULL,
        0,
        NULL,
        NULL) == 0)
  {
    (void) pthread_join(m_ReaderThread, NULL);
    QC_OMX_MSG_MEDIUM("Reader thread joined");
  }
  else
  {
    QC_OMX_MSG_ERROR("error killing reader thread");
  }

  if (m_pMsgQ)
    delete m_pMsgQ;
  if (m_pOutBufferMgr)
    delete m_pOutBufferMgr;
  if (m_pInBufferMgr)
    delete m_pInBufferMgr;
  if (m_pComponentName)
    free(m_pComponentName);

  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::use_EGL_image(OMX_IN OMX_HANDLETYPE hComponent,
                                  OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
                                  OMX_IN OMX_U32 nPortIndex,
                                  OMX_IN OMX_PTR pAppPrivate,
                                  OMX_IN void* eglImage)
{
  return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE Venc::component_role_enum(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_OUT OMX_U8 *cRole,
                                        OMX_IN OMX_U32 nIndex)
{

  static const char* roles[] = {"*",
    "video_encoder",
    "video_encoder.mpeg4",
    "video_encoder.avc",
    "video_encoder.h263"};
  static const OMX_U32 nRoles = sizeof(roles) / sizeof(char*);

  if (cRole == NULL)
  {
    return OMX_ErrorBadParameter;
  }

  if (nIndex < nRoles)
  {
    memcpy((char*) cRole, roles[nIndex], strlen(roles[nIndex]) + 1);
    return OMX_ErrorNone;
  }
  else
  {
    return OMX_ErrorNoMore;
  }
}

void *Venc::component_thread(void *pClassObj)
{
  OMX_BOOL bRunning = OMX_TRUE;
  Venc* pVenc = reinterpret_cast<Venc*>(pClassObj);

  QC_OMX_MSG_MEDIUM("component thread has started");

  if (pVenc == NULL)
  {
    QC_OMX_MSG_ERROR("thread param is null. exiting.");
    return 0;
  }

  while (bRunning)
  {
    VencMsgQ::MsgType msg;
    QC_OMX_MSG_LOW("component thread is checking msg q...");
    if (pVenc->m_pMsgQ->PopMsg(&msg) != OMX_ErrorNone)
    {
      QC_OMX_MSG_ERROR("failed to pop msg");
    }

    QC_OMX_MSG_LOW("Component thread got msg");

    switch (msg.id)
    {
      case VencMsgQ::MSG_ID_EXIT:
        QC_OMX_MSG_HIGH("got MSG_ID_EXIT");

        bRunning = OMX_FALSE;
        break;
      case VencMsgQ::MSG_ID_MARK_BUFFER:
        QC_OMX_MSG_HIGH("got MSG_ID_MARK_BUFFER for port %d", (int) msg.data.sMarkBuffer.nPortIndex);
        pVenc->process_mark_buffer(msg.data.sMarkBuffer.nPortIndex,
            &msg.data.sMarkBuffer.sMarkData);
        break;
      case VencMsgQ::MSG_ID_PORT_ENABLE:
        QC_OMX_MSG_HIGH("got MSG_ID_PORT_ENABLE for port %d", (int) msg.data.nPortIndex);
        pVenc->process_port_enable(msg.data.nPortIndex);
        break;
      case VencMsgQ::MSG_ID_PORT_DISABLE:
        QC_OMX_MSG_HIGH("got MSG_ID_PORT_DISABLE for port %d", (int) msg.data.nPortIndex);
        pVenc->process_port_disable(msg.data.nPortIndex);
        break;
      case VencMsgQ::MSG_ID_FLUSH:
        QC_OMX_MSG_HIGH("got MSG_ID_FLUSH for port %d", (int) msg.data.nPortIndex);
        pVenc->process_flush(msg.data.nPortIndex);
        break;
      case VencMsgQ::MSG_ID_STATE_CHANGE:
        QC_OMX_MSG_HIGH("got MSG_ID_STATE_CHANGE");
        pVenc->process_state_change(msg.data.eState);
        break;
      case VencMsgQ::MSG_ID_EMPTY_BUFFER:
        QC_OMX_MSG_LOW("got MSG_ID_EMPTY_BUFFER");
        pVenc->process_empty_buffer(msg.data.pBuffer);
        break;
      case VencMsgQ::MSG_ID_FILL_BUFFER:
        QC_OMX_MSG_LOW("got MSG_ID_FILL_BUFFER");
        pVenc->process_fill_buffer(msg.data.pBuffer);
        break;
      case VencMsgQ::MSG_ID_DRIVER_MSG:
        QC_OMX_MSG_LOW("got MSG_ID_DRIVER_MSG");
        pVenc->process_driver_msg(&msg.data.sDriverMsg);
        break;
      default:
        QC_OMX_MSG_ERROR("invalid msg id %d", msg.id);
        break;
    }
  }
  QC_OMX_MSG_HIGH("component thread is exiting");
  return 0;
}

void *Venc::reader_thread_entry(void *pClassObj)
{
  if (pClassObj)
  {
    QC_OMX_MSG_HIGH("reader thread entry");
    (reinterpret_cast<Venc*>(pClassObj))->reader_thread();
  }
  else
  {
    QC_OMX_MSG_ERROR("null thread data");
  }

  return NULL;
}

void Venc::reader_thread()
{
  int bExecute = OMX_TRUE;

  while (bExecute == OMX_TRUE)
  {
    int driverRet;
    struct ven_timeout timeout;
    struct venc_msg msg;
    VencMsgQ::MsgDataType data;
    timeout.millisec = VEN_TIMEOUT_INFINITE;

    QC_OMX_MSG_LOW("reader thread for next message");
    /* driverRet = DeviceIoControl(m_pDevice,
       VENC_IOCTL_CMD_READ_NEXT_MSG,
       &timeout,
       sizeof(timeout),
       &msg,
       sizeof(msg),
       NULL,
       NULL); */
    driverRet = DeviceIoControl(m_nFd,
        VENC_IOCTL_CMD_READ_NEXT_MSG,
        &msg);

    if (!driverRet)
    {
      if (msg.msg_code == VENC_MSG_STOP_READING_MSG)
      {
        QC_OMX_MSG_HIGH("reader thread exiting");
        bExecute = OMX_FALSE;
      }
      else
      {
        QC_OMX_MSG_LOW("Receive msg");
        data.sDriverMsg = msg;
        m_pMsgQ->PushMsg(VencMsgQ::MSG_ID_DRIVER_MSG, &data);
      }
    }
    else
    {
      QC_OMX_MSG_ERROR("reader thread error");
      bExecute = OMX_FALSE;
    }
  }
}

void Venc::process_state_change(OMX_STATETYPE eState)
{
  int rc = 0;
#define GOTO_STATE(eState)                                              \
  {                                                                    \
    m_eState = eState;                                                \
    m_sCallbacks.EventHandler(m_hSelf,                               \
        m_pAppData,                            \
        OMX_EventCmdComplete,                  \
        OMX_CommandStateSet,                   \
        eState,                                \
        NULL);                                 \
  }

  // -susan: comment out for now
  /* if (eState == m_eState)
     {
     QC_OMX_MSG_ERROR("attempted to change to the same state");
     m_sCallbacks.EventHandler(m_hSelf,
     m_pAppData,
     OMX_EventError,
     OMX_ErrorSameState,
     0 , NULL);
     return;
     } */


  // We will issue OMX_EventCmdComplete when we get device layer
  // status callback for the corresponding state.

  switch (eState)
  {
    case OMX_StateInvalid:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StateInvalid");
        m_eState = eState;
        m_sCallbacks.EventHandler(m_hSelf,
            m_pAppData,
            OMX_EventError,
            OMX_ErrorInvalidState,
            0 , NULL);
        m_sCallbacks.EventHandler(m_hSelf,
            m_pAppData,
            OMX_EventCmdComplete,
            OMX_CommandStateSet,
            m_eState, NULL);
        break;
      }
    case OMX_StateLoaded:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StateLoaded");
        if (m_eState == OMX_StateIdle)
        {
          if ((m_nInBuffAllocated == 0) && (m_nOutBuffAllocated == 0))
          {
            GOTO_STATE(OMX_StateLoaded);
          }
          else {
            BITMASK_SET(m_nFlags, OMX_COMPONENT_LOADING_PENDING);
          }
        }
        else
        {
          if (m_eState != OMX_StateLoaded) {
            QC_OMX_MSG_ERROR("invalid state transition to OMX_StateLoaded");
            m_sCallbacks.EventHandler(m_hSelf,
                m_pAppData,
                OMX_EventError,
                OMX_ErrorIncorrectStateTransition,
                0 , NULL);
          }
        }
        break;
      }
    case OMX_StateIdle:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StateIdle");
        if (m_eState == OMX_StateLoaded)
        {
          if (m_sInPortDef.bPopulated && m_sOutPortDef.bPopulated)
          {
            GOTO_STATE(OMX_StateIdle);
          }
          else {
            BITMASK_SET(m_nFlags, OMX_COMPONENT_IDLE_PENDING);
          }
        }
        else if (m_eState == OMX_StateExecuting || m_eState == OMX_StatePause)
        {
          int driverRet;
          driverRet = ioctl(m_nFd, VENC_IOCTL_CMD_STOP);
          if (driverRet)
          {
            QC_OMX_MSG_ERROR("Error sending stop command to driver");
            m_sCallbacks.EventHandler(m_hSelf,
                m_pAppData,
                OMX_EventError,
                OMX_ErrorIncorrectStateTransition,
                0 , NULL);
          }
        }
        else
        {
#if 0
          // susan: temp fix
          QC_OMX_MSG_ERROR("invalid state transition to OMX_StateIdle");
          m_sCallbacks.EventHandler(m_hSelf,
              m_pAppData,
              OMX_EventError,
              OMX_ErrorIncorrectStateTransition,
              0 , NULL);
#endif
        }
        break;
      }
    case OMX_StateExecuting:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StateExecuting");
        if (m_eState == OMX_StateIdle)
        {
          int driverRet;
          allocate_q6_buffers(&m_sQ6Buffers);

          driverRet = ven_start(m_pDevice, &m_sQ6Buffers);
          if (driverRet)
          {
            QC_OMX_MSG_ERROR("Error sending start command to driver");
            m_sCallbacks.EventHandler(m_hSelf,
                m_pAppData,
                OMX_EventError,
                (OMX_U32) OMX_ErrorUndefined,
                (OMX_U32) 0 , NULL);
          }
        }
        else if (m_eState == OMX_StatePause)
        {
          int driverRet;
          driverRet = ioctl(m_nFd, VENC_IOCTL_CMD_RESUME);
          if (driverRet)
          {
            QC_OMX_MSG_ERROR("Error sending resume command to driver");
            m_sCallbacks.EventHandler(m_hSelf,
                m_pAppData,
                OMX_EventError,
                (OMX_U32) OMX_ErrorUndefined,
                (OMX_U32) 0 , NULL);
          }
        }
        else
        {
          QC_OMX_MSG_ERROR("invalid state transition to OMX_StateExecuting");
          m_sCallbacks.EventHandler(m_hSelf,
              m_pAppData,
              OMX_EventError,
              OMX_ErrorIncorrectStateTransition,
              0 , NULL);
        }
        break;
      }
    case OMX_StatePause:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StatePause");
        if (m_eState == OMX_StateExecuting)
        {
          int driverRet;
          driverRet = ioctl(m_nFd, VENC_IOCTL_CMD_PAUSE);
          if (driverRet)
          {
            QC_OMX_MSG_ERROR("Error sending start command to driver");
            m_sCallbacks.EventHandler(m_hSelf,
                m_pAppData,
                OMX_EventError,
                (OMX_U32) OMX_ErrorUndefined,
                (OMX_U32) 0 , NULL);
          }
        }
        else
        {
          QC_OMX_MSG_ERROR("invalid state transition to OMX_StatePause");
          m_sCallbacks.EventHandler(m_hSelf,
              m_pAppData,
              OMX_EventError,
              OMX_ErrorIncorrectStateTransition,
              0 , NULL);
        }
        break;
      }
    case OMX_StateWaitForResources:
      {
        QC_OMX_MSG_HIGH("Attempt to go to OMX_StateWaitForResources");
        /// @todo determine what to do with this transition, for now return an error
        QC_OMX_MSG_ERROR("Transitioning to OMX_StateWaitForResources");
        m_sCallbacks.EventHandler(m_hSelf,
            m_pAppData,
            OMX_EventError,
            OMX_ErrorIncorrectStateTransition,
            0 , NULL);

        break;
      }
    default:
      {
        QC_OMX_MSG_ERROR("invalid state %d", (int) eState);
        m_sCallbacks.EventHandler(m_hSelf,
            m_pAppData,
            OMX_EventError,
            OMX_ErrorIncorrectStateTransition,
            0 , NULL);
        break;
      }
  }
  sem_post(&m_cmd_lock);
#undef GOTO_STATE
}

void Venc::process_flush(OMX_U32 nPortIndex)
{
  QC_OMX_MSG_HIGH("flushing...");


  if (nPortIndex == (OMX_U32) OMX_ALL)
  {
    int driverRet;
    struct venc_buffer_flush flush;
    flush.flush_mode = VENC_FLUSH_ALL;
    driverRet = ioctl(m_nFd, VENC_IOCTL_CMD_FLUSH, &flush);
    if (driverRet)
    {
      QC_OMX_MSG_ERROR("failed to issue flush");
    }
  }
  else
  {
    QC_OMX_MSG_HIGH("we only support flushing all ports");
  }

}

void Venc::process_port_enable(OMX_U32 nPortIndex)
{
  /// @todo implement
}

void Venc::process_port_disable(OMX_U32 nPortIndex)
{
  /// @todo implement
}

void Venc::process_mark_buffer(OMX_U32 nPortIndex,
                               const OMX_MARKTYPE* pMarkData)
{
  /// @todo implement
}
void Venc::process_empty_buffer(OMX_BUFFERHEADERTYPE* pBufferHdr)
{
   int result = VENC_S_SUCCESS;
   struct venc_buffer input;
   struct venc_pmem pmem_input;
   OMX_QCOM_PLATFORM_PRIVATE_LIST *pParam1;
   OMX_QCOM_PLATFORM_PRIVATE_ENTRY *pParam2;
   OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pParam;

   if (pBufferHdr == NULL) {
     QC_OMX_MSG_ERROR("Empty input buffer");
   }

   // if (m_bIsQcomPvt) {
   // pParam1 = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_LIST *>(pBufferHdr->pPlatformPrivate);
   // pParam2 = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_ENTRY *>(pParam1->entryList);
   // pParam = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *>(pParam2->entry);

   //input.ptr_buffer = (unsigned char *)pBufferHdr->pAppPrivate;


   pParam = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *>(pBufferHdr->pPlatformPrivate);
   pmem_input.fd = pParam->pmem_fd;
   pmem_input.offset = pParam->offset;
   pmem_input.src = VENC_PMEM_EBI1;
   pmem_input.size =  m_sInPortDef.nBufferSize;
   input.ptr_buffer = (unsigned char *)&pmem_input;

   input.len = pBufferHdr->nFilledLen;
   input.size = pBufferHdr->nAllocLen;
   input.time_stamp = pBufferHdr->nTimeStamp;
   if (pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)
   {
     input.flags = VENC_FLAG_EOS;
   }
   else
   {
     input.flags = 0;
   }
   input.client_data = (unsigned long) pBufferHdr;
   result = DeviceIoControl(m_nFd,
       VENC_IOCTL_CMD_ENCODE_FRAME,
       &input);
   if (result)
   {
     QC_OMX_MSG_ERROR("failed to encode frame");
     if (m_sCallbacks.EmptyBufferDone(m_hSelf,
          m_pAppData,
          pBufferHdr) != OMX_ErrorNone)
     {
       QC_OMX_MSG_ERROR("EBD failed");
     }
   }
   else
   {
     QC_OMX_MSG_LOW("Push buffer %p", pBufferHdr);
     // this should not happen but print an error just in case
     if (m_pInBufferMgr->PushBuffer(pBufferHdr) != OMX_ErrorNone)
     {
       QC_OMX_MSG_ERROR("failed to push input buffer");
     }
   }
}

void Venc::process_fill_buffer(OMX_BUFFERHEADERTYPE* pBufferHdr)
{
      int driverRet;
      struct venc_buffer output;
      struct venc_pmem pmem_output;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *pParam1;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *pParam2;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *pParam;

      if (pBufferHdr == NULL) {
        QC_OMX_MSG_ERROR("Empty input buffer");
      }

      // if (m_bIsQcomPvt) {
      //pParam1 = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_LIST *>(pBufferHdr->pPlatformPrivate);
      //pParam2 = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_ENTRY *>(pParam1->entryList);
      //pParam = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *>(pParam2->entry);

      /* pOutput = &((struct venc_pmem *)output.ptr_buffer);
   pOutput->fd = pParam->pmem_fd;
   pOutput->offset = pParam->offset; */

      QC_OMX_MSG_LOW("Fill_output: client_data:0x%x", pBufferHdr);
      // output.ptr_buffer = (unsigned char *)pBufferHdr->pAppPrivate;

      pParam = reinterpret_cast<OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *>(pBufferHdr->pPlatformPrivate);
      pmem_output.fd = pParam->pmem_fd;
      pmem_output.offset = pParam->offset;
      /*We should enhance OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO to store
       * pmem source info. Hardcoding for now.*/
      pmem_output.src = VENC_PMEM_EBI1;
      pmem_output.size =  m_sOutPortDef.nBufferSize;
      output.ptr_buffer = (unsigned char *)&pmem_output;

      output.client_data = (unsigned long) pBufferHdr;
      driverRet = DeviceIoControl(m_nFd,
          VENC_IOCTL_CMD_FILL_OUTPUT_BUFFER,
          &output);
      if (driverRet)
      {
        QC_OMX_MSG_ERROR("failed to encode frame");
	if (m_sCallbacks.FillBufferDone(m_hSelf,
          m_pAppData,
          pBufferHdr) != OMX_ErrorNone)
        {
          QC_OMX_MSG_ERROR("FBD failed");
        }
      }
      else
      {
        QC_OMX_MSG_LOW("Push output buffer %p", pBufferHdr);
        // this should not happen but print an error just in case
        if (m_pOutBufferMgr->PushBuffer(pBufferHdr) != OMX_ErrorNone)
        {
          QC_OMX_MSG_ERROR("failed to push output buffer");
        }
      }

}

void Venc::process_driver_msg(struct venc_msg* pMsg)
{
  /// @todo implement
  if (pMsg != NULL)
  {
    switch (pMsg->msg_code)
    {
      case VENC_MSG_INPUT_BUFFER_DONE:
        QC_OMX_MSG_LOW("received VENC_MSG_INPUT_BUFFER_DONE from driver");
        process_status_input_buffer_done(&pMsg->msg_data.buf, pMsg->status_code);
        break;
      case VENC_MSG_OUTPUT_BUFFER_DONE:
        QC_OMX_MSG_LOW("received VENC_MSG_OUTPUT_BUFFER_DONE from driver");
        process_status_output_buffer_done(&pMsg->msg_data.buf, pMsg->status_code);
        break;
      case VENC_MSG_FLUSH:
        QC_OMX_MSG_HIGH("received VENC_MSG_FLUSH from driver");
        process_status_flush_done(&pMsg->msg_data.flush_ret, pMsg->status_code);
        break;
      case VENC_MSG_START:
        QC_OMX_MSG_HIGH("received VENC_MSG_START from driver");
        process_status_start_done(pMsg->status_code);
        break;
      case VENC_MSG_STOP:
        QC_OMX_MSG_HIGH("received VENC_MSG_STOP from driver");
        process_status_stop_done(pMsg->status_code);
        break;
      case VENC_MSG_PAUSE:
        QC_OMX_MSG_HIGH("received VENC_MSG_PAUSE from driver");
        process_status_pause_done(pMsg->status_code);
        break;
      case VENC_MSG_RESUME:
        QC_OMX_MSG_HIGH("received VENC_MSG_RESUME from driver");
        process_status_resume_done(pMsg->status_code);
        break;
      case VENC_MSG_STOP_READING_MSG:   // fallthrough
      case VENC_MSG_NEED_OUTPUT_BUFFER: // fallthrough
        QC_OMX_MSG_ERROR("unsupported msg %d", pMsg->msg_code);
        break;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("null msg");
  }
}

void Venc::process_status_input_buffer_done(void* pArg, unsigned long nStatus)
{
  struct venc_buffer *pData = (struct venc_buffer *)pArg;
  OMX_BUFFERHEADERTYPE* pBufferHdr;

  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("Fill output buffer failed (%lu) \n", nStatus);
  }
  if (pArg == NULL)
  {
    QC_OMX_MSG_ERROR("Empty args from output buffer done \n");
  }

  pBufferHdr = (OMX_BUFFERHEADERTYPE*)(pData->client_data);

  QC_OMX_MSG_LOW("Pop buffer %p", pBufferHdr);
  if (m_pInBufferMgr->PopBuffer(pBufferHdr) == OMX_ErrorNone)
  {
    if (m_sCallbacks.EmptyBufferDone(m_hSelf,
          m_pAppData,
          pBufferHdr) != OMX_ErrorNone)
    {
      QC_OMX_MSG_ERROR("EBD failed");
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("failed to pop buffer");
  }

}
void Venc::process_status_output_buffer_done(void* pArg, unsigned long nStatus)
{
  struct venc_buffer *pData = (struct venc_buffer *)pArg;
  OMX_BUFFERHEADERTYPE* pBufferHdr;

  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("Fill output buffer failed (%lu) \n", nStatus);
  }

  if (pArg == NULL)
  {
    QC_OMX_MSG_ERROR("Empty args from output buffer done \n");
  }

  pBufferHdr = (OMX_BUFFERHEADERTYPE*)(pData->client_data);
  QC_OMX_MSG_LOW("Pop buffer %p", pBufferHdr);
  if (m_pOutBufferMgr->PopBuffer(pBufferHdr) == OMX_ErrorNone)
  {
    pBufferHdr->nFilledLen = pData->len;
    pBufferHdr->nTimeStamp = pData->time_stamp;
    pBufferHdr->nFlags = 0;
    if (pData->flags & VENC_FLAG_SYNC_FRAME)
    {
      QC_OMX_MSG_LOW("got iframe");
      pBufferHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
    }
    if (pData->flags & VENC_FLAG_EOS)
    {
      QC_OMX_MSG_MEDIUM("got eos");
      pBufferHdr->nFlags |= OMX_BUFFERFLAG_EOS;
    }
    if (pData->flags & VENC_FLAG_END_OF_FRAME)
    {
      pBufferHdr->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    }
    if (pData->flags & VENC_FLAG_CODEC_CONFIG)
    {
      QC_OMX_MSG_LOW("got syntax syntax header");
      pBufferHdr->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
    }

    QC_OMX_MSG_LOW("Before calling FillBufferDone callback");
    if (m_sCallbacks.FillBufferDone(m_hSelf,
          m_pAppData,
          pBufferHdr) != OMX_ErrorNone)
    {
      QC_OMX_MSG_ERROR("FBD failed");
    }
  }
  else
  {
    QC_OMX_MSG_ERROR("failed to pop buffer");
  }
}

void Venc::process_status_flush_done(struct venc_buffer_flush* pData, unsigned long nStatus)
{
  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("async flush failed");
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventError,
        OMX_ErrorUndefined,
        0,
        NULL);
  }
  else
  {

    if (pData != NULL)
    {
      OMX_U32 portIndex = pData->flush_mode == VENC_FLUSH_INPUT ?
        PORT_INDEX_IN : PORT_INDEX_OUT;
      m_sCallbacks.EventHandler(m_hSelf,
          m_pAppData,
          OMX_EventCmdComplete,
          OMX_CommandFlush,
          portIndex,
          NULL);
    }
    else
    {
      QC_OMX_MSG_ERROR("null data");
    }
  }
}

void Venc::process_status_start_done(unsigned long nStatus)
{
  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("async start failed %lu", nStatus);
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventError,
        OMX_ErrorUndefined,
        0,
        NULL);
  }
  else
  {
    m_eState = OMX_StateExecuting;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StateExecuting,
        NULL);
  }
}

void Venc::process_status_stop_done(unsigned long nStatus)
{
  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("async stop failed");
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventError,
        OMX_ErrorUndefined,
        0,
        NULL);
  }
  else
  {
    free_q6_buffers(&m_sQ6Buffers);
    m_eState = OMX_StateIdle;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StateIdle,
        NULL);
  }
}

void Venc::process_status_pause_done(unsigned long nStatus)
{
  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("async pause failed");
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventError,
        OMX_ErrorUndefined,
        0,
        NULL);
  }
  else
  {
    m_eState = OMX_StatePause;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StatePause,
        NULL);
  }
}

void Venc::process_status_resume_done(unsigned long nStatus)
{
  if (nStatus != VENC_S_SUCCESS)
  {
    QC_OMX_MSG_ERROR("async resume failed");
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventError,
        OMX_ErrorUndefined,
        0,
        NULL);
  }
  else
  {
    m_eState = OMX_StateExecuting;
    m_sCallbacks.EventHandler(m_hSelf,
        m_pAppData,
        OMX_EventCmdComplete,
        OMX_CommandStateSet,
        OMX_StateExecuting,
        NULL);
  }
}

OMX_ERRORTYPE Venc::pmem_alloc(struct venc_pmem *pBuf, int size, int pmem_region_id)
{
  struct pmem_region region;

  QC_OMX_MSG_MEDIUM("Opening pmem files with size 0x%x...",size);
  if (pmem_region_id == VENC_PMEM_EBI1)
    pBuf->fd = open("/dev/pmem_adsp", O_RDWR);
  else if (pmem_region_id == VENC_PMEM_SMI)
    pBuf->fd = open("/dev/pmem_smipool", O_RDWR);
  else {
    QC_OMX_MSG_ERROR("Pmem region id not supported \n", pmem_region_id);
    return OMX_ErrorBadParameter;
  }

  if (pBuf->fd < 0) {
    QC_OMX_MSG_ERROR("error could not open pmem device %d\n", pmem_region_id);
    return OMX_ErrorInsufficientResources;
  }

  pBuf->src = pmem_region_id;
  pBuf->offset = 0;
  pBuf->size = (size + 4095) & (~4095);

  QC_OMX_MSG_HIGH("Allocate pmem of size:0x%x, fd:%d", size, pBuf->fd);
  pBuf->virt = mmap(NULL, pBuf->size, PROT_READ | PROT_WRITE,
      MAP_SHARED, pBuf->fd, 0);

  if (pBuf->virt == MAP_FAILED) {
    QC_OMX_MSG_ERROR("error mmap failed with size:%d \n",size);
    close(pBuf->fd);
    pBuf->fd = -1;
    return OMX_ErrorInsufficientResources;
  }

  return OMX_ErrorNone;
}

OMX_ERRORTYPE Venc::pmem_free(struct venc_pmem* pBuf)
{
  QC_OMX_MSG_HIGH("Free pmem of fd: %d, size:%d", pBuf->fd, pBuf->size);
  close(pBuf->fd);
  pBuf->fd = -1;
  munmap(pBuf->virt, pBuf->size);
  pBuf->offset = 0;
  pBuf->phys = pBuf->virt = NULL;
  return OMX_ErrorNone;
}
