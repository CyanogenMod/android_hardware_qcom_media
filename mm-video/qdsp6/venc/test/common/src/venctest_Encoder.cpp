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

/*========================================================================
  Include Files
 ==========================================================================*/
#include "venctest_Encoder.h"
#include "venctest_Debug.h"
#include "venctest_Signal.h"
#include "venctest_Pmem.h"
#include "venctest_SignalQueue.h"
#include "venctest_StatsThread.h"
#include "OMX_Component.h"
#include "OMX_QCOMExtns.h"

#define OMX_SPEC_VERSION 0x00000101
#define OMX_INIT_STRUCT(_s_, _name_)            \
   memset((_s_), 0x0, sizeof(_name_));          \
   (_s_)->nSize = sizeof(_name_);               \
   (_s_)->nVersion.nVersion = OMX_SPEC_VERSION

namespace venctest
{

  static const OMX_U32 PORT_INDEX_IN  = 0;
  static const OMX_U32 PORT_INDEX_OUT = 1;


  struct CmdType
  {
    OMX_EVENTTYPE   eEvent;
    OMX_COMMANDTYPE eCmd;
    OMX_U32         sCmdData;
    OMX_ERRORTYPE   eResult;
  };

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Encoder::Encoder()
  {
    VENC_TEST_MSG_ERROR("Should not be here this is a private constructor");
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Encoder::Encoder(EmptyDoneCallbackType pEmptyDoneFn,
      FillDoneCallbackType pFillDoneFn,
      OMX_PTR pAppData,
      OMX_VIDEO_CODINGTYPE eCodec)
    : m_pSignalQueue(new SignalQueue(32, sizeof(CmdType))),
    m_pInMem(0),
    m_pOutMem(0),
    m_pStats(new StatsThread(2000)), // 2 second statistics intervals
    m_pEventFn(NULL),
    m_pEmptyDoneFn(pEmptyDoneFn),
    m_pFillDoneFn(pFillDoneFn),
    m_pAppData(pAppData),
    m_bInUseBuffer(OMX_FALSE),
    m_bOutUseBuffer(OMX_FALSE),
    m_pInputBuffers(NULL),
    m_pOutputBuffers(NULL),
    m_hEncoder(NULL),
    m_eState(OMX_StateLoaded),
    m_nInputBuffers(0),
    m_nOutputBuffers(0),
    m_nInputBufferSize(0),
    m_nOutputBufferSize(0),
    m_eCodec(eCodec),
    m_nInFrameIn(0),
    m_nOutFrameIn(0),
    m_nInFrameOut(0),
    m_nOutFrameOut(0)
  {
    static OMX_CALLBACKTYPE callbacks = {EventCallback,
      EmptyDoneCallback,
      FillDoneCallback};

    if (m_pEmptyDoneFn == NULL)
    {
      VENC_TEST_MSG_ERROR("Empty buffer fn is NULL");
    }
    if (m_pFillDoneFn == NULL)
    {
      VENC_TEST_MSG_ERROR("Fill buffer fn is NULL");
    }

    // MPEG4
    if (eCodec == OMX_VIDEO_CodingMPEG4)
    {
      VENC_TEST_MSG_MEDIUM("Getting handle for mpeg4");
      if (OMX_GetHandle(&m_hEncoder,
            (OMX_STRING)"OMX.qcom.video.encoder.mpeg4",
            this,
            &callbacks) != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("Error getting component handle");
      }
    }
    // H263
    else if (eCodec == OMX_VIDEO_CodingH263)
    {
      VENC_TEST_MSG_MEDIUM("Getting handle for h263");
      if (OMX_GetHandle(&m_hEncoder,
            (OMX_STRING)"OMX.qcom.video.encoder.h263",
            this,
            &callbacks) != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("Error getting component handle");
      }
    }
    // H264
    else if (eCodec == OMX_VIDEO_CodingAVC)
    {
      VENC_TEST_MSG_MEDIUM("Getting handle for avc");
      if (OMX_GetHandle(&m_hEncoder,
            (OMX_STRING)"OMX.qcom.video.encoder.avc",
            this,
            &callbacks) != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("Error getting component handle");
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("Invalid codec selection");
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Encoder::~Encoder()
  {
    OMX_FreeHandle(m_hEncoder);
    if (m_pSignalQueue)
    {
      delete m_pSignalQueue;
    }
    if (m_pStats)
    {
      delete m_pStats;
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::Configure(EncoderConfigType* pConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pConfig == NULL)
    {
      result = OMX_ErrorBadParameter;
    }
    else
    {
      m_nInputBuffers = pConfig->nInBufferCount;
      m_nOutputBuffers = pConfig->nOutBufferCount;
    }

    //////////////////////////////////////////
    // port format (set the codec)
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_VIDEO_PARAM_PORTFORMATTYPE fmt;
      OMX_INIT_STRUCT(&fmt, OMX_VIDEO_PARAM_PORTFORMATTYPE);
      fmt.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetParameter(m_hEncoder,
          OMX_IndexParamVideoPortFormat,
          (OMX_PTR) &fmt);
      if (result == OMX_ErrorNone)
      {
        fmt.eCompressionFormat = m_eCodec;
        result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamVideoPortFormat,
            (OMX_PTR) &fmt);
      }
    }

    //////////////////////////////////////////
    // codec specific config
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      if (m_eCodec == OMX_VIDEO_CodingMPEG4)
      {
        OMX_VIDEO_PARAM_MPEG4TYPE mp4;
        OMX_INIT_STRUCT(&mp4, OMX_VIDEO_PARAM_MPEG4TYPE);
        mp4.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoMpeg4,
            (OMX_PTR) &mp4);

        if (result == OMX_ErrorNone)
        {
          mp4.nTimeIncRes = pConfig->nTimeIncRes;
          mp4.nPFrames = pConfig->nIntraPeriod - 1;

          mp4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
          mp4.eLevel = OMX_VIDEO_MPEG4Level5;
/*
          if (pConfig->nOutputFrameWidth > 176 &&
              pConfig->nOutputFrameHeight > 144)
          {
            mp4.eLevel = OMX_VIDEO_MPEG4Level2;
          }
          else
          {
            mp4.eLevel = OMX_VIDEO_MPEG4Level0;
          }
*/
          mp4.bACPred = OMX_TRUE;
          mp4.bSVH = pConfig->bEnableShortHeader;
          mp4.nHeaderExtension = pConfig->nHECInterval;

          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoMpeg4,
              (OMX_PTR) &mp4);
        }
      }
      else if (m_eCodec == OMX_VIDEO_CodingH263)
      {
        OMX_VIDEO_PARAM_H263TYPE h263;
        OMX_INIT_STRUCT(&h263, OMX_VIDEO_PARAM_H263TYPE);
        h263.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoH263,
            (OMX_PTR) &h263);

        if (result == OMX_ErrorNone)
        {
          h263.nPFrames = pConfig->nIntraPeriod - 1;
          h263.nBFrames = 0;

          h263.eProfile = OMX_VIDEO_H263ProfileBaseline;

          h263.eLevel = OMX_VIDEO_H263Level70;
          h263.bPLUSPTYPEAllowed = OMX_FALSE;
          h263.nAllowedPictureTypes = 2;
          h263.bForceRoundingTypeToZero = OMX_TRUE;
          h263.nPictureHeaderRepetition = 0;


          if (pConfig->eResyncMarkerType == RESYNC_MARKER_GOB)
          {
            h263.nGOBHeaderInterval = pConfig->nResyncMarkerSpacing;
          }
          else if (pConfig->eResyncMarkerType == RESYNC_MARKER_MB)
          {
            OMX_VIDEO_PARAM_MPEG4TYPE mp4;
            OMX_INIT_STRUCT(&mp4, OMX_VIDEO_PARAM_MPEG4TYPE);
            mp4.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
            result = OMX_GetParameter(m_hEncoder,
                OMX_IndexParamVideoMpeg4,
                (OMX_PTR) &mp4);

            if (result == OMX_ErrorNone)
            {
              // annex k
              mp4.nSliceHeaderSpacing = pConfig->nResyncMarkerSpacing;

              result = OMX_SetParameter(m_hEncoder,
                  OMX_IndexParamVideoMpeg4,
                  (OMX_PTR) &mp4);
            }

            h263.nGOBHeaderInterval = 0;
          }
          else
          {
            h263.nGOBHeaderInterval = 0;
          }

          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoH263,
              (OMX_PTR) &h263);
        }
      }
      else if (m_eCodec == OMX_VIDEO_CodingAVC)
      {
        OMX_VIDEO_PARAM_AVCTYPE avc;
        OMX_INIT_STRUCT(&avc, OMX_VIDEO_PARAM_AVCTYPE);
        avc.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoAvc,
            (OMX_PTR) &avc);

        if (result == OMX_ErrorNone)
        {
          avc.nPFrames = pConfig->nIntraPeriod - 1;
          avc.nBFrames = 0;
          avc.eProfile = OMX_VIDEO_AVCProfileBaseline;
          avc.eLevel = OMX_VIDEO_AVCLevel31;
          if (pConfig->nOutputFrameWidth * pConfig->nOutputFrameHeight / 256 < 792)
          {
            avc.eLevel = OMX_VIDEO_AVCLevel21;
          }
          else if (pConfig->nOutputFrameWidth * pConfig->nOutputFrameHeight / 256  < 1620)
          {
            avc.eLevel = OMX_VIDEO_AVCLevel22;
          }

          if (pConfig->eResyncMarkerType == RESYNC_MARKER_MB)
          {
            avc.nSliceHeaderSpacing = pConfig->nResyncMarkerSpacing;
          }
          else
          {
            avc.nSliceHeaderSpacing = 0;
          }

          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoAvc,
              (OMX_PTR) &avc);
        }
      }

    }

    //////////////////////////////////////////
    // error resilience
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      // see if we already configured error resilience (which is the case for h263)
      if (m_eCodec == OMX_VIDEO_CodingMPEG4)
      {
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE errorCorrection; //OMX_IndexParamVideoErrorCorrection
        OMX_INIT_STRUCT(&errorCorrection, OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE);
        errorCorrection.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoErrorCorrection,
            (OMX_PTR) &errorCorrection);

        if (result == OMX_ErrorNone)
        {
          if (m_eCodec == OMX_VIDEO_CodingMPEG4)
          {
            errorCorrection.bEnableHEC = pConfig->nHECInterval == 0 ? OMX_FALSE : OMX_TRUE;
          }
          else
          {
            errorCorrection.bEnableHEC = OMX_FALSE;
          }

          if (pConfig->eResyncMarkerType == RESYNC_MARKER_BITS)
          {
            errorCorrection.bEnableResync = OMX_TRUE;
            errorCorrection.nResynchMarkerSpacing = pConfig->nResyncMarkerSpacing;
          }
          else
          {
            errorCorrection.bEnableResync = OMX_FALSE;
            errorCorrection.nResynchMarkerSpacing = 0;
          }

          errorCorrection.bEnableDataPartitioning = OMX_FALSE;    // MP4 Resync

          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoErrorCorrection,
              (OMX_PTR) &errorCorrection);
        }
      }
    }
#ifdef QCOM_OMX_EXT
    //////////////////////////////////////////
    // qp
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_QCOM_VIDEO_CONFIG_QPRANGE qp; // OMX_QcomIndexConfigVideoQPRange
      qp.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
      result = OMX_GetConfig(m_hEncoder,
          (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoQPRange,
          (OMX_PTR) &qp);
      if (result == OMX_ErrorNone)
      {
        qp.nMinQP = (OMX_U32) pConfig->nMinQp;
        qp.nMaxQP = (OMX_U32) pConfig->nMaxQp;
        result = OMX_SetConfig(m_hEncoder,
            (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoQPRange,
            (OMX_PTR) &qp);
      }
    }
#endif
    //////////////////////////////////////////
    // bitrate
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_VIDEO_PARAM_BITRATETYPE bitrate; // OMX_IndexParamVideoBitrate
      OMX_INIT_STRUCT(&bitrate, OMX_VIDEO_PARAM_BITRATETYPE);
      bitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetParameter(m_hEncoder,
          OMX_IndexParamVideoBitrate,
          (OMX_PTR) &bitrate);
      if (result == OMX_ErrorNone)
      {
        bitrate.eControlRate = pConfig->eControlRate;
        bitrate.nTargetBitrate = pConfig->nBitrate;
        result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamVideoBitrate,
            (OMX_PTR) &bitrate);
      }
    }

    //////////////////////////////////////////
    // quantization
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      if (pConfig->eControlRate == OMX_Video_ControlRateDisable)
      {
        ///////////////////////////////////////////////////////////
        // QP Config
        ///////////////////////////////////////////////////////////
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE qp; // OMX_IndexParamVideoQuantization
        OMX_INIT_STRUCT(&qp, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
        qp.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamVideoQuantization,
            (OMX_PTR) &qp);
        if (result == OMX_ErrorNone)
        {
          if (m_eCodec == OMX_VIDEO_CodingAVC)
          {
            qp.nQpI = 30;
            qp.nQpP = 30;
          }
          else
          {
            qp.nQpI = 14;
            qp.nQpP = 14;
          }
          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoQuantization,
              (OMX_PTR) &qp);
        }
      }
    }

    //////////////////////////////////////////
    // frame rate
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_CONFIG_FRAMERATETYPE framerate; // OMX_IndexConfigVideoFramerate
      framerate.nPortIndex = (OMX_U32) PORT_INDEX_IN; // input
      result = OMX_GetConfig(m_hEncoder,
          OMX_IndexConfigVideoFramerate,
          (OMX_PTR) &framerate);
      if (result == OMX_ErrorNone)
      {
        framerate.xEncodeFramerate = pConfig->nFramerate << 16;
        result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate,
            (OMX_PTR) &framerate);
      }
    }
    //////////////////////////////////////////
    //slice bit mode
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_VIDEO_CONFIG_NALSIZE nal; // OMX_IndexConfigVideoNalSize
      nal.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetConfig(m_hEncoder,
          OMX_IndexConfigVideoNalSize,
          (OMX_PTR) &nal);
      if (result == OMX_ErrorNone && (pConfig->eResyncMarkerType == RESYNC_MARKER_BITS))
      {
        nal.nNaluBytes = pConfig->nResyncMarkerSpacing;
        result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoNalSize,
            (OMX_PTR) &nal);
       }
     }

    //////////////////////////////////////////
    // rotation
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_CONFIG_ROTATIONTYPE framerotate; // OMX_IndexConfigCommonRotate
      framerotate.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetConfig(m_hEncoder,
          OMX_IndexConfigCommonRotate,
          (OMX_PTR) &framerotate);
      if (result == OMX_ErrorNone)
      {

        framerotate.nRotation = pConfig->nRotation;

        result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigCommonRotate,
            (OMX_PTR) &framerotate);
      }
    }

    //////////////////////////////////////////
    // intra refresh
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_VIDEO_PARAM_INTRAREFRESHTYPE ir; // OMX_IndexParamVideoIntraRefresh
      OMX_INIT_STRUCT(&ir, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
      ir.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetParameter(m_hEncoder,
          OMX_IndexParamVideoIntraRefresh,
          (OMX_PTR) &ir);
      if (result == OMX_ErrorNone)
      {
        if (pConfig->bEnableIntraRefresh == OMX_TRUE)
        {
          /// @todo need an extension dynamic for intra refresh configuration
          ir.eRefreshMode = OMX_VIDEO_IntraRefreshCyclic;
          ir.nCirMBs = 5;

          result = OMX_SetParameter(m_hEncoder,
              OMX_IndexParamVideoIntraRefresh,
              (OMX_PTR) &ir);
        }
      }
    }

    //////////////////////////////////////////
    // input buffer requirements
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_PARAM_PORTDEFINITIONTYPE inPortDef; // OMX_IndexParamPortDefinition
      OMX_INIT_STRUCT(&inPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
      inPortDef.nPortIndex = (OMX_U32) PORT_INDEX_IN; // input
      result = OMX_GetParameter(m_hEncoder,
          OMX_IndexParamPortDefinition,
          (OMX_PTR) &inPortDef);
      if (result == OMX_ErrorNone)
      {
        inPortDef.nBufferCountActual = m_nInputBuffers;
        inPortDef.format.video.nFrameWidth = pConfig->nFrameWidth;
        inPortDef.format.video.nFrameHeight = pConfig->nFrameHeight;
        result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition,
            (OMX_PTR) &inPortDef);
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition,
            (OMX_PTR) &inPortDef);

        if (m_nInputBuffers != (OMX_S32) inPortDef.nBufferCountActual)
        {
          VENC_TEST_MSG_ERROR("Buffer reqs dont match...");
        }
      }

      m_nInputBufferSize = (OMX_S32) inPortDef.nBufferSize;
    }

    //////////////////////////////////////////
    // output buffer requirements
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_PARAM_PORTDEFINITIONTYPE outPortDef; // OMX_IndexParamPortDefinition
      OMX_INIT_STRUCT(&outPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
      outPortDef.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
      result = OMX_GetParameter(m_hEncoder,
          OMX_IndexParamPortDefinition,
          (OMX_PTR) &outPortDef);
      if (result == OMX_ErrorNone)
      {
        outPortDef.nBufferCountActual = m_nOutputBuffers;
        outPortDef.format.video.nFrameWidth = pConfig->nOutputFrameWidth;
        outPortDef.format.video.nFrameHeight = pConfig->nOutputFrameHeight;
        result = OMX_SetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition,
            (OMX_PTR) &outPortDef);
        result = OMX_GetParameter(m_hEncoder,
            OMX_IndexParamPortDefinition,
            (OMX_PTR) &outPortDef);

        if (m_nOutputBuffers != (OMX_S32) outPortDef.nBufferCountActual)
        {
          VENC_TEST_MSG_ERROR("Buffer reqs dont match...");
        }
      }

      m_nOutputBufferSize = (OMX_S32) outPortDef.nBufferSize;
    }


    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::EnableUseBufferModel(OMX_BOOL bInUseBuffer, OMX_BOOL bOutUseBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    m_bInUseBuffer = bInUseBuffer;
    m_bOutUseBuffer = bOutUseBuffer;
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::GoToExecutingState()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_eState == OMX_StateLoaded)
    {
      ///////////////////////////////////////
      // 1. send idle state command
      // 2. allocate input buffers
      // 3. allocate output buffers
      // 4. wait for idle state complete
      // 5. send executing state command and wait for complete
      ///////////////////////////////////////

      m_nInFrameIn = 0;
      m_nOutFrameIn = 0;
      m_nInFrameOut = 0;
      m_nOutFrameOut = 0;

      // send idle state comand
      VENC_TEST_MSG_MEDIUM("going to state OMX_StateIdle...");
      result = SetState(OMX_StateIdle, OMX_FALSE);

      if (result == OMX_ErrorNone)
      {
        result = AllocateBuffers();
      }

      // wait for idle state complete
      if (result == OMX_ErrorNone)
      {
        result = WaitState(OMX_StateIdle);
      }

      // send executing state command and wait for complete
      if (result == OMX_ErrorNone)
      {
        VENC_TEST_MSG_MEDIUM("going to state OMX_StateExecuting...");
        result = SetState(OMX_StateExecuting, OMX_TRUE);
      }

    }
    else
    {
      VENC_TEST_MSG_ERROR("invalid state");
      result = OMX_ErrorIncorrectStateTransition;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::GetOutOfBandSyntaxHeader(OMX_U8* pSyntaxHdr,
      OMX_S32 nSyntaxHdrLen,
      OMX_S32* pSyntaxHdrFilledLen)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
#ifdef QCOM_OMX_EXT
    if (pSyntaxHdr != NULL &&
        nSyntaxHdrLen > 0 &&
        pSyntaxHdrFilledLen != NULL)
    {
      OMX_QCOM_VIDEO_PARAM_SYNTAXHDRTYPE syntax;
      OMX_INIT_STRUCT(&syntax, OMX_QCOM_VIDEO_PARAM_SYNTAXHDRTYPE);
      syntax.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
      syntax.pBuff = pSyntaxHdr;
      syntax.nBuffLen = (OMX_U32) nSyntaxHdrLen;
      result = OMX_GetParameter(m_hEncoder,
          (OMX_INDEXTYPE) OMX_QcomIndexParamVideoSyntaxHdr,
          (OMX_PTR) &syntax);
      if (result == OMX_ErrorNone)
      {
        *pSyntaxHdrFilledLen = (OMX_S32) syntax.nFilledLen;
      }
      else
      {
        VENC_TEST_MSG_ERROR("failed to get syntax header");
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad param(s)");
      result = OMX_ErrorBadParameter;
    }
#endif
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::GoToLoadedState()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    ///////////////////////////////////////
    // 1. send idle state command and wait for complete
    // 2. send loaded state command
    // 3. free input buffers
    // 4. free output buffers
    // 5. wait for loaded state complete
    ///////////////////////////////////////

    // send idle state comand and wait for complete
    if (m_eState == OMX_StateExecuting ||
        m_eState == OMX_StatePause)
    {
      VENC_TEST_MSG_MEDIUM("going to state OMX_StateIdle...");
      result = SetState(OMX_StateIdle, OMX_TRUE);
    }


    // send loaded state command
    VENC_TEST_MSG_MEDIUM("going to state OMX_StateLoaded...");
    result = SetState(OMX_StateLoaded, OMX_FALSE);

    if (result == OMX_ErrorNone)
    {
      result = FreeBuffers();

      // wait for loaded state complete
      result = WaitState(OMX_StateLoaded);
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::DeliverInput(OMX_BUFFERHEADERTYPE* pBuffer)
  {
    VENC_TEST_MSG_HIGH(" deliver  input frame %ld", m_nInFrameIn);
    ++m_nInFrameIn;

    if (m_pStats != NULL)
    {
      (void) m_pStats->SetInputStats(pBuffer);
    }

    return OMX_EmptyThisBuffer(m_hEncoder, pBuffer);
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::DeliverOutput(OMX_BUFFERHEADERTYPE* pBuffer)
  {
    VENC_TEST_MSG_HIGH("deliver output frame %ld", m_nOutFrameIn);
    ++m_nOutFrameIn;
    pBuffer->nFlags = 0;
    return OMX_FillThisBuffer(m_hEncoder, pBuffer);
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_BUFFERHEADERTYPE** Encoder::GetBuffers(OMX_BOOL bIsInput)
  {
    OMX_BUFFERHEADERTYPE** ppBuffers;
    if (m_eState == OMX_StateExecuting)
    {
      ppBuffers = (bIsInput == OMX_TRUE) ? m_pInputBuffers : m_pOutputBuffers;
    }
    else
    {
      ppBuffers = NULL;
    }
    return ppBuffers;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::RequestIntraVOP()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_CONFIG_INTRAREFRESHVOPTYPE vop;

    vop.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
    result = OMX_GetConfig(m_hEncoder,
        OMX_IndexConfigVideoIntraVOPRefresh,
        (OMX_PTR) &vop);

    if (result == OMX_ErrorNone)
    {
      vop.IntraRefreshVOP = OMX_TRUE;
      result = OMX_SetConfig(m_hEncoder,
          OMX_IndexConfigVideoIntraVOPRefresh,
          (OMX_PTR) &vop);
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to get state");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::SetIntraPeriod(OMX_S32 nIntraPeriod)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
#ifdef QCOM_OMX_EXT
    OMX_QCOM_VIDEO_CONFIG_INTRAPERIODTYPE intra;

    intra.nPortIndex = (OMX_U32) PORT_INDEX_OUT; // output
    result = OMX_GetConfig(m_hEncoder,
        (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoIntraperiod,
        (OMX_PTR) &intra);

    if (result == OMX_ErrorNone)
    {
      intra.nPFrames = (OMX_U32) nIntraPeriod - 1;
      result = OMX_SetConfig(m_hEncoder,
          (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoIntraperiod,
          (OMX_PTR) &intra);
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to get state");
    }
#endif
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::ChangeQuality(OMX_S32 nFramerate,
      OMX_S32 nBitrate,
      OMX_S32 nMinQp,
      OMX_S32 nMaxQp)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    (void) nMinQp;
    (void) nMaxQp;

    //////////////////////////////////////////
    // frame rate
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_CONFIG_FRAMERATETYPE framerate; // OMX_IndexConfigVideoFramerate
      framerate.nPortIndex = (OMX_U32) PORT_INDEX_IN; // input
      result = OMX_GetConfig(m_hEncoder,
          OMX_IndexConfigVideoFramerate,
          (OMX_PTR) &framerate);
      if (result == OMX_ErrorNone)
      {
        framerate.xEncodeFramerate = ((OMX_U32) nFramerate) << 16;
        result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoFramerate,
            (OMX_PTR) &framerate);
      }
    }

    //////////////////////////////////////////
    // bitrate
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_VIDEO_CONFIG_BITRATETYPE bitrate; // OMX_IndexConfigVideoFramerate
      bitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
      result = OMX_GetConfig(m_hEncoder,
          OMX_IndexConfigVideoBitrate,
          (OMX_PTR) &bitrate);
      if (result == OMX_ErrorNone)
      {
        bitrate.nEncodeBitrate = (OMX_U32) nBitrate;
        result = OMX_SetConfig(m_hEncoder,
            OMX_IndexConfigVideoBitrate,
            (OMX_PTR) &bitrate);
      }
    }
#ifdef QCOM_OMX_EXT
    //////////////////////////////////////////
    // qp
    //////////////////////////////////////////
    if (result == OMX_ErrorNone)
    {
      OMX_QCOM_VIDEO_CONFIG_QPRANGE qp; // OMX_QcomIndexConfigVideoQPRange
      qp.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
      result = OMX_GetConfig(m_hEncoder,
          (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoQPRange,
          (OMX_PTR) &qp);
      if (result == OMX_ErrorNone)
      {
        qp.nMinQP = (OMX_U32) nMinQp;
        qp.nMaxQP = (OMX_U32) nMaxQp;
        result = OMX_SetConfig(m_hEncoder,
            (OMX_INDEXTYPE) OMX_QcomIndexConfigVideoQPRange,
            (OMX_PTR) &qp);
      }
    }
#endif
    if (result != OMX_ErrorNone)
    {
      VENC_TEST_MSG_ERROR("error changing quality");
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::AllocateBuffers()
  {
    int i;
    OMX_U8 *pBuffer;
    struct venc_pmem *pMem;
    void *pVirt;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    m_pInputBuffers = new OMX_BUFFERHEADERTYPE*[m_nInputBuffers];
    m_pInBufPvtList = new OMX_QCOM_PLATFORM_PRIVATE_LIST[m_nInputBuffers];
    m_pInBufPmemInfo = new OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO[m_nInputBuffers];
    m_pInBufPvtEntry = new OMX_QCOM_PLATFORM_PRIVATE_ENTRY[m_nInputBuffers];

    m_pOutputBuffers = new OMX_BUFFERHEADERTYPE*[m_nOutputBuffers];
    m_pOutBufPvtList = new OMX_QCOM_PLATFORM_PRIVATE_LIST[m_nOutputBuffers];
    m_pOutBufPmemInfo = new OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO[m_nOutputBuffers];
    m_pOutBufPvtEntry = new OMX_QCOM_PLATFORM_PRIVATE_ENTRY[m_nOutputBuffers];
    //////////////////////////////////////////
    //  USE BUFFER MODEL
    //////////////////////////////////////////

    if (m_bInUseBuffer == OMX_TRUE)
    {
      VENC_TEST_MSG_MEDIUM("USE BUFFER INPUT: allocating input buffer");
      // allocate input buffers
      m_pInMem = new Pmem(m_nInputBuffers);
      for (i = 0; i < m_nInputBuffers; i++)
      {

        result = m_pInMem->Allocate(&pBuffer, m_nInputBufferSize, VENC_PMEM_EBI1);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error allocating input buffer");
          break;
        }

        pMem = (venc_pmem*)pBuffer;
        pVirt = pMem->virt;
        /* VENC_TEST_MSG_MEDIUM("allocating input buffer fd:%d, offset:%d ", pBuffer->fd, pBuffer->offset); */
        result = OMX_UseBuffer(m_hEncoder,
            &m_pInputBuffers[i],
            PORT_INDEX_IN, // port index
            (OMX_PTR)pBuffer, // pAppPrivate
            m_nInputBufferSize,
            (OMX_U8 *)pVirt);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error using input buffer");
          break;
        }

        VENC_TEST_MSG_HIGH("Register pmem buffer: hdr=%p, fd%d, offset0x%x",
            &m_pInputBuffers[i], pMem->fd, pMem->offset);
        // Set the PMEM Info structure
        m_pInBufPmemInfo[i].pmem_fd = pMem->fd;
        m_pInBufPmemInfo[i].offset = pMem->offset;

        //Link the entry structure pmem Info
        m_pInBufPvtEntry[i].type = OMX_QCOM_PLATFORM_PRIVATE_PMEM;
        m_pInBufPvtEntry[i].entry = &m_pInBufPmemInfo[i];

        // Initialize the List structure to have one entry
        m_pInBufPvtList[i].nEntries = 1;
        m_pInBufPvtList[i].entryList = &m_pInBufPvtEntry[i];

        OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* private_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* )malloc(sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO));
        private_info->pmem_fd =  pMem->fd;
        private_info->offset = pMem->offset;
        m_pInputBuffers[i]->pPlatformPrivate = (OMX_PTR)private_info;

        VENC_TEST_MSG_HIGH("Test App: entry = %p\n",m_pInBufPvtEntry[i].entry);

      }
    } else {
      VENC_TEST_MSG_MEDIUM("ALLOCATE BUFFER INPUT: allocating input buffer");
      for (i = 0; i < m_nInputBuffers; i++)
      {
        result = OMX_AllocateBuffer(m_hEncoder,
            &m_pInputBuffers[i],
            PORT_INDEX_IN, // port index
            this, // pAppPrivate
            m_nInputBufferSize);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error allocating input buffer");
          break;
        }
      }
    }

    if (m_bOutUseBuffer == OMX_TRUE) {
      VENC_TEST_MSG_MEDIUM("USE BUFFER OUTPUT: allocating output");
      // allocate output buffers
      m_pOutMem = new Pmem(m_nOutputBuffers);
      for (i = 0; i < m_nOutputBuffers; i++)
      {
        result = m_pOutMem->Allocate(&pBuffer, m_nOutputBufferSize, VENC_PMEM_EBI1);

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error allocating output buffer");
          break;
        }

        pMem = (venc_pmem*)pBuffer;
        pVirt = pMem->virt;
        result = OMX_UseBuffer(m_hEncoder,
            &m_pOutputBuffers[i],
            PORT_INDEX_OUT, // port index
            (OMX_PTR)pBuffer, // pAppPrivate
            m_nOutputBufferSize,
            (OMX_U8 *)pVirt);

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error using output buffer");
          break;
        }

        VENC_TEST_MSG_HIGH("Register pmem buffer: hdr=%p, fd%d, offset0x%x",
            &m_pOutputBuffers[i], pMem->fd, pMem->offset);
        // Set the PMEM Info structure
        m_pOutBufPmemInfo[i].pmem_fd = pMem->fd;
        m_pOutBufPmemInfo[i].offset = pMem->offset;

        //Link the entry structure pmem Info
        m_pOutBufPvtEntry[i].type = OMX_QCOM_PLATFORM_PRIVATE_PMEM;
        m_pOutBufPvtEntry[i].entry = &m_pOutBufPmemInfo[i];

        // Initialize the List structure to have one entry
        m_pOutBufPvtList[i].nEntries = 1;
        m_pOutBufPvtList[i].entryList = &m_pOutBufPvtEntry[i];

        OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* private_info = (OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO* )malloc(sizeof(OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO));
        private_info->pmem_fd =  pMem->fd;
        private_info->offset = pMem->offset;
        m_pOutputBuffers[i]->pPlatformPrivate = (OMX_PTR)private_info;
      }
    } else {
      VENC_TEST_MSG_MEDIUM("ALLOCATE BUFFER OUTPUT: allocating output");
      // allocate output buffers
      for (i = 0; i < m_nOutputBuffers; i++)
      {
        result = OMX_AllocateBuffer(m_hEncoder,
            &m_pOutputBuffers[i],
            PORT_INDEX_OUT, // port index
            this, // pAppPrivate
            m_nOutputBufferSize);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error allocating output buffer");
          break;
        }
      }
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::FreeBuffers()
  {
    int i;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_pInputBuffers)
    {
      for (i = 0; i < m_nInputBuffers; i++)
      {
        VENC_TEST_MSG_MEDIUM("freeing input buffer %d", i);

        result = OMX_FreeBuffer(m_hEncoder,
            PORT_INDEX_IN, // port index
            m_pInputBuffers[i]);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error freeing input buffer");
        }
        if (m_bInUseBuffer == OMX_TRUE)
        {
          result = m_pInMem->Free((OMX_U8 *)m_pInputBuffers[i]->pAppPrivate);
        }
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error freeing input pmem");
        }
      }

      delete [] m_pInputBuffers;
      delete [] m_pInBufPvtList;
      delete [] m_pInBufPmemInfo;
      delete [] m_pInBufPvtEntry;

      m_pInputBuffers = NULL;
      m_pInBufPvtList = NULL;
      m_pInBufPmemInfo = NULL;
      m_pInBufPvtEntry = NULL;
    }

    // free output buffers
    if (m_pOutputBuffers)
    {
      for (i = 0; i < m_nOutputBuffers; i++)
      {
        VENC_TEST_MSG_MEDIUM("freeing output buffer %d", i);
        result = OMX_FreeBuffer(m_hEncoder,
            PORT_INDEX_OUT, // port index
            m_pOutputBuffers[i]);
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error freeing output buffer");
        }

        if (m_bOutUseBuffer == OMX_TRUE)
        {
          result = m_pOutMem->Free((OMX_U8*)m_pOutputBuffers[i]->pAppPrivate);
        }
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error freeing output pmem");
        }
      }
      delete [] m_pOutputBuffers;
      m_pOutputBuffers = NULL;
      delete [] m_pOutBufPvtList;
      m_pOutBufPvtList = NULL;
      delete [] m_pOutBufPmemInfo;
      m_pOutBufPmemInfo = NULL;
      delete [] m_pOutBufPvtEntry;
      m_pOutBufPvtEntry = NULL;
    }

    if (m_pInMem)
      delete m_pInMem;
    m_pInMem = NULL;

    if (m_pOutMem)
      delete m_pOutMem;
    m_pOutMem = NULL;

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::SetState(OMX_STATETYPE eState,
      OMX_BOOL bSynchronous)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    result = OMX_SendCommand(m_hEncoder,
        OMX_CommandStateSet,
        (OMX_U32) eState,
        NULL);

    if (result == OMX_ErrorNone)
    {
      if (result == OMX_ErrorNone)
      {
        if (bSynchronous == OMX_TRUE)
        {
          result = WaitState(eState);
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed to wait state");
          }
        }
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to set state");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::WaitState(OMX_STATETYPE eState)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    CmdType cmd;

    (void) m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0); // infinite wait
    result = cmd.eResult;

    if (cmd.eEvent == OMX_EventCmdComplete)
    {
      if (cmd.eCmd == OMX_CommandStateSet)
      {
        if ((OMX_STATETYPE) cmd.sCmdData == eState)
        {
          m_eState = (OMX_STATETYPE) cmd.sCmdData;
        }
        else
        {
          VENC_TEST_MSG_ERROR("wrong state (%d)", (int) cmd.sCmdData);
          result = OMX_ErrorUndefined;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("expecting state change");
        result = OMX_ErrorUndefined;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("expecting cmd complete");
      result = OMX_ErrorUndefined;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::Flush()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    result = OMX_SendCommand(m_hEncoder,
        OMX_CommandFlush,
        OMX_ALL,
        NULL);
    if (result == OMX_ErrorNone)
    {
      CmdType cmd;
      if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("error popping signal");
      }
      result = cmd.eResult;
      if (cmd.eEvent != OMX_EventCmdComplete || cmd.eCmd != OMX_CommandFlush)
      {
        VENC_TEST_MSG_ERROR("expecting flush");
        result = OMX_ErrorUndefined;
      }

      if (result != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("failed to wait for flush");
        result = OMX_ErrorUndefined;
      }
      else
      {
        if (m_pSignalQueue->Pop(&cmd, sizeof(cmd), 0) != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("error popping signal");
        }
        else
        {
          result = cmd.eResult;
          if (cmd.eEvent != OMX_EventCmdComplete || cmd.eCmd != OMX_CommandFlush)
          {
            VENC_TEST_MSG_ERROR("expecting flush");
            result = OMX_ErrorUndefined;
          }
        }

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed to wait for flush");
          result = OMX_ErrorUndefined;
        }
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to set state");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::EventCallback(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_EVENTTYPE eEvent,
      OMX_IN OMX_U32 nData1,
      OMX_IN OMX_U32 nData2,
      OMX_IN OMX_PTR pEventData)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (eEvent == OMX_EventCmdComplete)
    {
      if ((OMX_COMMANDTYPE) nData1 == OMX_CommandStateSet)
      {
        VENC_TEST_MSG_MEDIUM("Event callback with state change");

        switch ((OMX_STATETYPE) nData2)
        {
          case OMX_StateLoaded:
            VENC_TEST_MSG_HIGH("state status OMX_StateLoaded");
            break;
          case OMX_StateIdle:
            VENC_TEST_MSG_HIGH("state status OMX_StateIdle");
            break;
          case OMX_StateExecuting:
            VENC_TEST_MSG_HIGH("state status OMX_StateExecuting");
            break;
          case OMX_StatePause:
            VENC_TEST_MSG_HIGH("state status OMX_StatePause");
            break;
          case OMX_StateInvalid:
            VENC_TEST_MSG_HIGH("state status OMX_StateInvalid");
            break;
          case OMX_StateWaitForResources:
            VENC_TEST_MSG_HIGH("state status OMX_StateWaitForResources");
            break;
	  default:
            VENC_TEST_MSG_HIGH("state status Invalid");
	    break;
        }

        CmdType cmd;
        cmd.eEvent = OMX_EventCmdComplete;
        cmd.eCmd = OMX_CommandStateSet;
        cmd.sCmdData = nData2;
        cmd.eResult = result;

        if (((Encoder*) pAppData)->m_eState != OMX_StateExecuting &&
            (OMX_STATETYPE) nData2 == OMX_StateExecuting)
        {
          // we are entering the execute state
          ((Encoder*) pAppData)->m_pStats->Start();
        }
        else if (((Encoder*) pAppData)->m_eState == OMX_StateExecuting &&
            (OMX_STATETYPE) nData2 != OMX_StateExecuting)
        {
          // we are leaving the execute state
          ((Encoder*) pAppData)->m_pStats->Finish();
        }

        result = ((Encoder*) pAppData)->m_pSignalQueue->Push(&cmd, sizeof(cmd));
      }
      else if ((OMX_COMMANDTYPE) nData1 == OMX_CommandFlush)
      {
        VENC_TEST_MSG_MEDIUM("Event callback with flush status");
        VENC_TEST_MSG_HIGH("flush status");

        CmdType cmd;
        cmd.eEvent = OMX_EventCmdComplete;
        cmd.eCmd = OMX_CommandFlush;
        cmd.sCmdData = 0;
        cmd.eResult = result;
        result = ((Encoder*) pAppData)->m_pSignalQueue->Push(&cmd, sizeof(cmd));
      }
      else
      {
        VENC_TEST_MSG_HIGH("error status");
        VENC_TEST_MSG_ERROR("Unimplemented command");
      }
    }
    else if (eEvent == OMX_EventError)
    {
      VENC_TEST_MSG_ERROR("async error");
      CmdType cmd;
      cmd.eEvent = OMX_EventError;
      cmd.eCmd = OMX_CommandMax;
      cmd.sCmdData = 0;
      cmd.eResult = (OMX_ERRORTYPE) nData1;
      result = ((Encoder*) pAppData)->m_pSignalQueue->Push(&cmd, sizeof(cmd));
    }
    else if (eEvent == OMX_EventBufferFlag)
    {
      VENC_TEST_MSG_MEDIUM("got buffer flag event");
    }
    else
    {
      VENC_TEST_MSG_ERROR("Unimplemented event");
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::EmptyDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    VENC_TEST_MSG_MEDIUM("releasing input frame %ld", ((Encoder*) pAppData)->m_nInFrameOut);
    if (((Encoder*) pAppData)->m_pEmptyDoneFn)
    {
      ((Encoder*) pAppData)->m_pEmptyDoneFn(hComponent,
        ((Encoder*) pAppData)->m_pAppData, // forward the client from constructor
        pBuffer);
    }
    ++((Encoder*) pAppData)->m_nInFrameOut;
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Encoder::FillDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    VENC_TEST_MSG_MEDIUM("releasing output frame %ld", ((Encoder*) pAppData)->m_nOutFrameOut);

    if (((Encoder*) pAppData)->m_pStats != NULL)
    {
      (void) ((Encoder*) pAppData)->m_pStats->SetOutputStats(pBuffer);
    }

    if (((Encoder*) pAppData)->m_pFillDoneFn)
    {
      ((Encoder*) pAppData)->m_pFillDoneFn(hComponent,
        ((Encoder*) pAppData)->m_pAppData, // forward the client from constructor
        pBuffer);
    }
    ++((Encoder*) pAppData)->m_nOutFrameOut;
    return result;
  }

} // namespace venctest
