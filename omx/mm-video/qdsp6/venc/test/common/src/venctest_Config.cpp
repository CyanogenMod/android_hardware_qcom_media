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
#include "venctest_ComDef.h"
#include "venctest_Debug.h"
#include "venctest_Config.h"
#include "venctest_Parser.h"
#include "venctest_File.h"
#include <stdio.h>
#include <stdlib.h>

namespace venctest
{
  struct ConfigEnum
  {
    const OMX_STRING pEnumName;
    OMX_S32 eEnumVal;
  };

  // Codecs
  static ConfigEnum g_pCodecEnums[] =
  {
    {(OMX_STRING)"MP4",  (int) OMX_VIDEO_CodingMPEG4},
    {(OMX_STRING)"H263", (int) OMX_VIDEO_CodingH263},
    {(OMX_STRING)"H264", (int) OMX_VIDEO_CodingAVC},
    {(OMX_STRING)0, 0}
  };

  // Rate control flavors
  static ConfigEnum g_pVencRCEnums[] =
  {
    {(OMX_STRING)"RC_OFF",     (int) OMX_Video_ControlRateDisable},
    {(OMX_STRING)"RC_VBR_VFR", (int) OMX_Video_ControlRateVariableSkipFrames},       // Camcorder
    {(OMX_STRING)"RC_VBR_CFR", (int) OMX_Video_ControlRateVariable},                 // Camcorder
    {(OMX_STRING)"RC_CBR_VFR", (int) OMX_Video_ControlRateConstantSkipFrames},       // QVP
    {(OMX_STRING)0, 0}
  };

  // Resync marker types
  static ConfigEnum m_pResyncMarkerType[] =
  {
    {(OMX_STRING)"NONE",        (int) RESYNC_MARKER_NONE},
    {(OMX_STRING)"BITS",        (int) RESYNC_MARKER_BITS},
    {(OMX_STRING)"MB",          (int) RESYNC_MARKER_MB},
    {(OMX_STRING)"GOB",         (int) RESYNC_MARKER_GOB},
    {(OMX_STRING)0, 0}
  };


  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_S32 ParseEnum(ConfigEnum* pConfigEnum,
      OMX_STRING pEnumName)
  {
    OMX_S32 idx = 0;
    while (pConfigEnum[idx].pEnumName)
    {
      if (Parser::StringICompare(pEnumName,
            pConfigEnum[idx].pEnumName) == 0)
      {
        return pConfigEnum[idx].eEnumVal;
      }
      idx++;
    }
    VENC_TEST_MSG_ERROR("error could not find enum");
    return -1;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Config::Config()
  {
    static const OMX_STRING pInFileName = (OMX_STRING)"";

    // set some default values
    memset(&m_sEncoderConfig, 0, sizeof(m_sEncoderConfig));
    m_sEncoderConfig.eCodec = OMX_VIDEO_CodingMPEG4;
    m_sEncoderConfig.eControlRate = OMX_Video_ControlRateDisable;
    m_sEncoderConfig.nFrameWidth = 640;
    m_sEncoderConfig.nFrameHeight = 480;
    m_sEncoderConfig.nOutputFrameWidth = m_sEncoderConfig.nFrameWidth;
    m_sEncoderConfig.nOutputFrameHeight = m_sEncoderConfig.nFrameHeight;
    m_sEncoderConfig.nDVSXOffset = 0;
    m_sEncoderConfig.nDVSYOffset = 0;
    m_sEncoderConfig.nBitrate = 768000;
    m_sEncoderConfig.nFramerate = 30;
    m_sEncoderConfig.nTimeIncRes = 30;
    m_sEncoderConfig.nRotation = 0;
    m_sEncoderConfig.nInBufferCount = 3;
    m_sEncoderConfig.nOutBufferCount = 3;
    m_sEncoderConfig.nHECInterval = 0;
    m_sEncoderConfig.nResyncMarkerSpacing = 0;
    m_sEncoderConfig.eResyncMarkerType = RESYNC_MARKER_NONE;
    m_sEncoderConfig.bEnableIntraRefresh = OMX_FALSE;
    m_sEncoderConfig.nFrames = 30;
    m_sEncoderConfig.bEnableShortHeader = OMX_FALSE;
    m_sEncoderConfig.nIntraPeriod = m_sEncoderConfig.nFramerate * 2;
    m_sEncoderConfig.nMinQp = 2;
    m_sEncoderConfig.nMaxQp = 31;
    m_sEncoderConfig.bProfileMode = OMX_FALSE;
    m_sEncoderConfig.bInUseBuffer= OMX_FALSE;
    m_sEncoderConfig.bOutUseBuffer= OMX_FALSE;

    // default dynamic config
    m_sDynamicConfig.nIFrameRequestPeriod = 0;
    m_sDynamicConfig.nUpdatedBitrate = m_sEncoderConfig.nBitrate;
    m_sDynamicConfig.nUpdatedFramerate = m_sEncoderConfig.nFramerate;
    m_sDynamicConfig.nUpdatedMinQp = m_sEncoderConfig.nMinQp;
    m_sDynamicConfig.nUpdatedMaxQp = m_sEncoderConfig.nMaxQp;
    m_sDynamicConfig.nUpdatedFrames = m_sEncoderConfig.nFrames;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Config::~Config()
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Config::Parse(OMX_STRING pFileName,
      EncoderConfigType* pEncoderConfig,
      DynamicConfigType* pDynamicConfig)
  {
    static const OMX_S32 maxLineLen = 256;

    OMX_ERRORTYPE result = OMX_ErrorNone;


    OMX_S32 nLine = 0;

    ParserStrVector v;
    char pBuf[maxLineLen];
    char* pTrimmed;

    File f;
    result = f.Open(pFileName,
        OMX_TRUE);

    if (result != OMX_ErrorNone)
    {
      VENC_TEST_MSG_ERROR("error opening file");
      return OMX_ErrorBadParameter;
    }

    while (Parser::ReadLine(&f, maxLineLen, pBuf) != -1)
    {
      OMX_S32 nTok = 0;
      (void) Parser::RemoveComments(pBuf);
      pTrimmed = Parser::Trim(pBuf);
      if (strlen(pTrimmed) != 0)
        nTok = Parser::TokenizeString(&v, pTrimmed, (OMX_STRING)"\t =");

      VENC_TEST_MSG_LOW("ntok = %ld", nTok);
      switch (v.size())
      {
        case 0: // blank line
          break;
        case 1: // use default value
          break;
        case 2: // user has a preferred config

          ///////////////////////////////////////////
          ///////////////////////////////////////////
          // Encoder config
          ///////////////////////////////////////////
          ///////////////////////////////////////////
          if (Parser::StringICompare((OMX_STRING)"FrameWidth", v[0]) == 0)
          {
            m_sEncoderConfig.nFrameWidth = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("FrameWidth = %ld", m_sEncoderConfig.nFrameWidth);
          }
          else if (Parser::StringICompare((OMX_STRING)"FrameHeight", v[0]) == 0)
          {
            m_sEncoderConfig.nFrameHeight = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("FrameHeight = %ld", m_sEncoderConfig.nFrameHeight);
          }
          else if (Parser::StringICompare((OMX_STRING)"OutputFrameWidth", v[0]) == 0)
          {
            m_sEncoderConfig.nOutputFrameWidth = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("OutputFrameWidth = %ld", m_sEncoderConfig.nOutputFrameWidth);
          }
          else if (Parser::StringICompare((OMX_STRING)"OutputFrameHeight", v[0]) == 0)
          {
            m_sEncoderConfig.nOutputFrameHeight = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("OutputFrameHeight = %ld", m_sEncoderConfig.nOutputFrameHeight);
          }
          else if (Parser::StringICompare((OMX_STRING)"DVSXOffset", v[0]) == 0)
          {
            m_sEncoderConfig.nDVSXOffset = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("DVSXOffset = %ld", m_sEncoderConfig.nDVSXOffset);
          }
          else if (Parser::StringICompare((OMX_STRING)"DVSYOffset", v[0]) == 0)
          {
            m_sEncoderConfig.nDVSYOffset = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("DVSYOffset = %ld", m_sEncoderConfig.nDVSYOffset);
          }
          else if (Parser::StringICompare((OMX_STRING)"Rotation", v[0]) == 0)
          {
            m_sEncoderConfig.nRotation = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("Rotation = %ld", m_sEncoderConfig.nRotation);
          }
          else if (Parser::StringICompare((OMX_STRING)"FPS", v[0]) == 0)
          {
            m_sEncoderConfig.nFramerate = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("FPS = %ld", m_sEncoderConfig.nFramerate);
          }
          else if (Parser::StringICompare((OMX_STRING)"Bitrate", v[0]) == 0)
          {
            m_sEncoderConfig.nBitrate = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("Bitrate = %ld", m_sEncoderConfig.nBitrate);
          }
          else if (Parser::StringICompare((OMX_STRING)"RC", v[0]) == 0)
          {
            m_sEncoderConfig.eControlRate =
              (OMX_VIDEO_CONTROLRATETYPE) ParseEnum(g_pVencRCEnums, v[1]);
            VENC_TEST_MSG_LOW("RC = %d", m_sEncoderConfig.eControlRate);
          }
          else if (Parser::StringICompare((OMX_STRING)"Codec", v[0]) == 0)
          {
            m_sEncoderConfig.eCodec = (OMX_VIDEO_CODINGTYPE) ParseEnum(g_pCodecEnums, v[1]);
            VENC_TEST_MSG_LOW("Codec = %d", m_sEncoderConfig.eCodec);
          }
          else if (Parser::StringICompare((OMX_STRING)"InBufferCount", v[0]) == 0)
          {
            m_sEncoderConfig.nInBufferCount = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("InBufferCount = %ld", m_sEncoderConfig.nInBufferCount);
          }
          else if (Parser::StringICompare((OMX_STRING)"OutBufferCount", v[0]) == 0)
          {
            m_sEncoderConfig.nOutBufferCount = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("OutBufferCount = %ld", m_sEncoderConfig.nOutBufferCount);
          }
          else if (Parser::StringICompare((OMX_STRING)"HECInterval", v[0]) == 0)
          {
            m_sEncoderConfig.nHECInterval = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("HECInterval = %d\n", (int) m_sEncoderConfig.nHECInterval);
          }
          else if (Parser::StringICompare((OMX_STRING)"ResyncMarkerSpacing", v[0]) == 0)
          {
            m_sEncoderConfig.nResyncMarkerSpacing = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("ResyncMarkerSpacing = %d\n", (int) m_sEncoderConfig.nResyncMarkerSpacing);
          }
          else if (Parser::StringICompare((OMX_STRING)"ResyncMarkerType", v[0]) == 0)
          {
            m_sEncoderConfig.eResyncMarkerType = (ResyncMarkerType) ParseEnum(m_pResyncMarkerType, v[1]);
            VENC_TEST_MSG_LOW("ResyncMarkerType = %d\n", (int) m_sEncoderConfig.eResyncMarkerType);
          }
          else if (Parser::StringICompare((OMX_STRING)"EnableIntraRefresh", v[0]) == 0)
          {
            m_sEncoderConfig.bEnableIntraRefresh = (OMX_BOOL) atoi(v[1]) == 1 ? OMX_TRUE : OMX_FALSE;
            VENC_TEST_MSG_LOW("EnableIntraRefresh = %d\n", (int) m_sEncoderConfig.bEnableIntraRefresh);
          }
          else if (Parser::StringICompare((OMX_STRING)"TimeIncRes", v[0]) == 0)
          {
            m_sEncoderConfig.nTimeIncRes = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("TimeIncRes = %d\n", (int) m_sEncoderConfig.nTimeIncRes);
          }
          else if (Parser::StringICompare((OMX_STRING)"EnableShortHeader", v[0]) == 0)
          {
            m_sEncoderConfig.bEnableShortHeader = (OMX_BOOL) atoi(v[1]);
            VENC_TEST_MSG_LOW("EnableShortHeader = %d\n", (int) m_sEncoderConfig.bEnableShortHeader);
          }
          else if (Parser::StringICompare((OMX_STRING)"IntraPeriod", v[0]) == 0)
          {
            m_sEncoderConfig.nIntraPeriod = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("IntraPeriod = %d\n", (int) m_sEncoderConfig.nIntraPeriod);
          }

          else if (Parser::StringICompare((OMX_STRING)"InFile", v[0]) == 0)
          {
            memcpy(m_sEncoderConfig.cInFileName, v[1], (strlen(v[1])+1));
            VENC_TEST_MSG_LOW("InFile");
          }
          else if (Parser::StringICompare((OMX_STRING)"OutFile", v[0]) == 0)
          {
            memcpy(m_sEncoderConfig.cOutFileName, v[1], (strlen(v[1])+1));
            VENC_TEST_MSG_LOW("OutFile");
          }
          else if (Parser::StringICompare((OMX_STRING)"NumFrames", v[0]) == 0)
          {
            m_sEncoderConfig.nFrames = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("NumFrames = %ld", m_sEncoderConfig.nFrames);
          }
          else if (Parser::StringICompare((OMX_STRING)"MinQp", v[0]) == 0)
          {
            m_sEncoderConfig.nMinQp = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("MinQp = %ld", m_sEncoderConfig.nMinQp);
          }
          else if (Parser::StringICompare((OMX_STRING)"MaxQp", v[0]) == 0)
          {
            m_sEncoderConfig.nMaxQp = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("MaxQp = %ld", m_sEncoderConfig.nMaxQp);
          }

          ///////////////////////////////////////////
          ///////////////////////////////////////////
          // Dynamic config
          ///////////////////////////////////////////
          ///////////////////////////////////////////
          else if (Parser::StringICompare((OMX_STRING)"IFrameRequestPeriod", v[0]) == 0)
          {
            m_sDynamicConfig.nIFrameRequestPeriod = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("IFrameRequestPeriod = %d\n", (int) m_sDynamicConfig.nIFrameRequestPeriod);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedBitrate", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedBitrate = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedBitrate = %d\n", (int) m_sDynamicConfig.nUpdatedBitrate);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedFPS", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedFramerate = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedFPS = %d\n", (int) m_sDynamicConfig.nUpdatedFramerate);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedMinQp", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedMinQp = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedMinQp = %d\n", (int) m_sDynamicConfig.nUpdatedMinQp);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedMaxQp", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedMaxQp = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedMaxQp = %d\n", (int) m_sDynamicConfig.nUpdatedMaxQp);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedNumFrames", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedFrames = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedNumFrames = %d\n", (int) m_sDynamicConfig.nUpdatedFrames);
          }
          else if (Parser::StringICompare((OMX_STRING)"UpdatedIntraPeriod", v[0]) == 0)
          {
            m_sDynamicConfig.nUpdatedIntraPeriod = (OMX_S32) atoi(v[1]);
            VENC_TEST_MSG_LOW("UpdatedIntraPeriod = %d\n", (int) m_sDynamicConfig.nUpdatedIntraPeriod);
          }
          else if (Parser::StringICompare((OMX_STRING)"ProfileMode", v[0]) == 0)
          {
            m_sEncoderConfig.bProfileMode = (OMX_BOOL) atoi(v[1]);
            VENC_TEST_MSG_LOW("ProfileMode = %d\n", (int) m_sEncoderConfig.bProfileMode);
          }
          else if (Parser::StringICompare((OMX_STRING)"InUseBuffer", v[0]) == 0)
          {
            m_sEncoderConfig.bInUseBuffer = (OMX_BOOL)atoi(v[1]);
          }
          else if (Parser::StringICompare((OMX_STRING)"OutUseBuffer", v[0]) == 0)
          {
            m_sEncoderConfig.bOutUseBuffer = (OMX_BOOL)atoi(v[1]);
          }
          else
          {
            VENC_TEST_MSG_ERROR("invalid config file key line %ld", nLine);
          }
          break;
        default:
          VENC_TEST_MSG_ERROR("error with config parsing line %ld", nLine);
          break;
      }
      v.clear();

      ++nLine;
    }

    (void) f.Close();

    memcpy(pEncoderConfig, &m_sEncoderConfig, sizeof(m_sEncoderConfig));

    if (pDynamicConfig != NULL) // optional param
    {
      memcpy(pDynamicConfig, &m_sDynamicConfig, sizeof(m_sDynamicConfig));
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Config::GetEncoderConfig(EncoderConfigType* pEncoderConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (pEncoderConfig != NULL)
    {
      memcpy(pEncoderConfig, &m_sEncoderConfig, sizeof(m_sEncoderConfig));
    }
    else
    {
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Config::GetDynamicConfig(DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (pDynamicConfig != NULL)
    {
      memcpy(pDynamicConfig, &m_sDynamicConfig, sizeof(m_sDynamicConfig));
    }
    else
    {
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

} // namespace venctest
