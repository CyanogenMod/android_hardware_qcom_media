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
#include "venctest_TestGetSyntaxHdr.h"
#include "venctest_Encoder.h"
#include "venctest_SignalQueue.h"

namespace venctest
{
  static const OMX_S32 PORT_INDEX_IN = 0;
  static const OMX_S32 PORT_INDEX_OUT = 1;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestGetSyntaxHdr::TestGetSyntaxHdr()
    : ITestCase(),    // invoke the base class constructor
    m_pEncoder(NULL)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestGetSyntaxHdr::~TestGetSyntaxHdr()
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestGetSyntaxHdr::ValidateAssumptions(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestGetSyntaxHdr::Execute(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig,
      OMX_S32 nTestNum)
  {
    static const OMX_S32 nSyntaxHdrLen = 4096;

    OMX_ERRORTYPE result = OMX_ErrorNone;

    OMX_U8* pSyntaxHdr = NULL;
    OMX_S32 nSyntaxHdrFilledLen;

    if (result == OMX_ErrorNone)
    {
      //==========================================
      // Create signal queue
      VENC_TEST_MSG_HIGH("Creating signal queue...");
      m_pSignalQueue = new SignalQueue(32, sizeof(OMX_BUFFERHEADERTYPE*)); // max 32 messages
    }

    //==========================================
    // Create and configure the encoder
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Creating encoder...");
      m_pEncoder = new Encoder(EBD,
          FBD,
          this, // set the test case object as the callback app data
          pConfig->eCodec);
      result = CheckError(m_pEncoder->Configure(pConfig));
    }

    //==========================================
    // Allocate the syntax header buffer
    if (result == OMX_ErrorNone)
    {
      pSyntaxHdr = new OMX_U8[nSyntaxHdrLen];
    }

    //==========================================
    // Get the out-of-band syntax hdr
    if (result == OMX_ErrorNone)
    {
      result = CheckError(m_pEncoder->GetOutOfBandSyntaxHeader(
            pSyntaxHdr, nSyntaxHdrLen, &nSyntaxHdrFilledLen));

      if (result != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("failed to get syntax hdr");
      }
    }

    //==========================================
    // Go to executing state (also allocate buffers)
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Go to executing state...");
      result = CheckError(m_pEncoder->GoToExecutingState());
    }

    //==========================================
    // Get the allocated output buffers
    OMX_BUFFERHEADERTYPE** ppOutputBuffers;
    if (result == OMX_ErrorNone)
    {
      ppOutputBuffers = m_pEncoder->GetBuffers(OMX_FALSE);
      for (OMX_S32 i = 0; i < pConfig->nOutBufferCount; i++)
      {
        ppOutputBuffers[i]->pAppPrivate = m_pEncoder; // set the encoder as the private app data
      }
    }


    //==========================================
    // Get the in-band syntax hdr
    if (result == OMX_ErrorNone)
    {
      // get the in band syntax header
      result = CheckError(GetInBandSyntaxHeader(ppOutputBuffers[0]));

      if (result == OMX_ErrorNone)
      {
        // are in-band and out-of-band headers the same size
        if (nSyntaxHdrFilledLen > 0)
        {
          if (nSyntaxHdrFilledLen == (OMX_S32) ppOutputBuffers[0]->nFilledLen)
          {
            // are in-band and out-of-band headers bit-exact?
            for (OMX_S32 i = 0; i < nSyntaxHdrFilledLen; i++)
            {
              if (pSyntaxHdr[i] != ppOutputBuffers[0]->pBuffer[i])
              {
                VENC_TEST_MSG_ERROR("Byte %ld does not match", i);
                result = CheckError(OMX_ErrorUndefined);
                break;
              }
            }

            if (result == OMX_ErrorNone)
            {
              VENC_TEST_MSG_HIGH("Syntax header looks good with size of %ld",
                  nSyntaxHdrFilledLen);
            }
          }
          else
          {
            VENC_TEST_MSG_ERROR("Syntax header sizes are different %ld != %ld",
                nSyntaxHdrFilledLen, ppOutputBuffers[0]->nFilledLen);
            result = CheckError(OMX_ErrorUndefined);
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("Syntax header size is 0");
          result = CheckError(OMX_ErrorUndefined);
        }
      }
    }

    //==========================================
    // Tear down the encoder (also deallocate buffers)
    if (m_pEncoder != NULL)
    {
      VENC_TEST_MSG_HIGH("Go to loaded state...");
      result = CheckError(m_pEncoder->GoToLoadedState());
    }

    if (pSyntaxHdr != NULL)
    {
      delete [] pSyntaxHdr;
    }

    //==========================================
    // Free our helper classes
    if (m_pEncoder)
      delete m_pEncoder;
    if (m_pSignalQueue)
      delete m_pSignalQueue;

    return result;

  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestGetSyntaxHdr::GetInBandSyntaxHeader(OMX_BUFFERHEADERTYPE* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    VENC_TEST_MSG_HIGH("waiting for syntax header...");
    result = CheckError(m_pEncoder->DeliverOutput(pBuffer));
    if (result == OMX_ErrorNone)
    {
      result = CheckError(m_pSignalQueue->Pop(
            (OMX_PTR) &pBuffer, sizeof(pBuffer), 1000)); // wait for 1 second max
      if (result == OMX_ErrorNone)
      {
        if ((pBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) == 0)
        {
          VENC_TEST_MSG_ERROR("expecting codeconfig flag");
          result = CheckError(OMX_ErrorUndefined);
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("failed popping");
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to deliver output");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestGetSyntaxHdr::EBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return OMX_ErrorNone;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestGetSyntaxHdr::FBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return ((TestGetSyntaxHdr*) pAppData)->m_pSignalQueue->Push(&pBuffer, sizeof(pBuffer));
  }

} // namespace venctest
