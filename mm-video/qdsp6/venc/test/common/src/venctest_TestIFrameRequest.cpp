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
#include "venctest_TestIFrameRequest.h"
#include "venctest_Time.h"
#include "venctest_Encoder.h"
#include "venctest_Queue.h"
#include "venctest_SignalQueue.h"
#include "venctest_File.h"

namespace venctest
{
  static const OMX_U32 PORT_INDEX_IN = 0;
  static const OMX_U32 PORT_INDEX_OUT = 1;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestIFrameRequest::TestIFrameRequest()
    : ITestCase(),    // invoke the base class constructor
    m_pEncoder(NULL),
    m_pInputQueue(NULL),
    m_pOutputQueue(NULL),
    m_pSignalQueue(NULL),
    m_pSource(NULL),
    m_pSink(NULL)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestIFrameRequest::~TestIFrameRequest()
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::ValidateAssumptions(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pConfig->eControlRate == OMX_Video_ControlRateVariableSkipFrames ||
        pConfig->eControlRate == OMX_Video_ControlRateConstantSkipFrames)
    {
      VENC_TEST_MSG_ERROR("Frame skip must be disabled for this to work");
      result = CheckError(OMX_ErrorUndefined);
    }

    if (pDynamicConfig->nIFrameRequestPeriod <= 0)
    {
      VENC_TEST_MSG_ERROR("Need a valid iframe request period");
      result = CheckError(OMX_ErrorUndefined);
    }

    if (pDynamicConfig->nIFrameRequestPeriod > pConfig->nFrames)
    {
      VENC_TEST_MSG_ERROR("nIFrameRequestPeriod must be less than nFrames");
      result = CheckError(OMX_ErrorUndefined);
    }

    if (pConfig->nIntraPeriod > pConfig->nFrames)
    {
      VENC_TEST_MSG_ERROR("nIntraPeriod must be less than nFrames");
      result = CheckError(OMX_ErrorUndefined);
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::Execute(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig,
      OMX_S32 nTestNum)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (result == OMX_ErrorNone)
    {
      //==========================================
      // Create signal queue
      VENC_TEST_MSG_HIGH("Creating signal queue...");
      m_pSignalQueue = new SignalQueue(32, sizeof(OMX_BUFFERHEADERTYPE*)); // max 32 messages

      //==========================================
      // Create input buffer queue
      VENC_TEST_MSG_HIGH("Creating input buffer queue...");
      m_pInputQueue = new Queue(pConfig->nInBufferCount,
          sizeof(OMX_BUFFERHEADERTYPE*));

      //==========================================
      // Create output buffer queue
      VENC_TEST_MSG_HIGH("Creating output buffer queue...");
      m_pOutputQueue = new Queue(pConfig->nOutBufferCount,
          sizeof(OMX_BUFFERHEADERTYPE*));
    }

    //==========================================
    // Create and open yuv file
    if (pConfig->cInFileName[0] != (char) 0)
    {
      VENC_TEST_MSG_HIGH("Creating file source...");
      m_pSource = new File();
      result = m_pSource->Open(pConfig->cInFileName, OMX_TRUE);
    }
    else
    {
      VENC_TEST_MSG_HIGH("Not reading from input file");
    }

    //==========================================
    // Create and open m4v file
    if (result == OMX_ErrorNone)
    {
      if (pConfig->cOutFileName[0] != (char) 0)
      {
        VENC_TEST_MSG_HIGH("Creating file sink...");
        m_pSink = new File();
        result = m_pSink->Open(pConfig->cOutFileName, OMX_FALSE);
      }
      else
      {
        VENC_TEST_MSG_HIGH("Not writing to output file");
      }
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

      if (result == OMX_ErrorNone)
      {
        result = m_pEncoder->EnableUseBufferModel(pConfig->bInUseBuffer, pConfig->bOutUseBuffer);
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
    // Get the allocated input buffers
    if (result == OMX_ErrorNone)
    {
      OMX_BUFFERHEADERTYPE** ppInputBuffers;
      ppInputBuffers = m_pEncoder->GetBuffers(OMX_TRUE);
      for (int i = 0; i < pConfig->nInBufferCount; i++)
      {
        ppInputBuffers[i]->pAppPrivate = m_pEncoder; // set the encoder as the private app data
        result = m_pInputQueue->Push(&ppInputBuffers[i], sizeof(ppInputBuffers[i])); // store buffers in queue
        if (result != OMX_ErrorNone)
        {
          break;
        }
      }
    }

    //==========================================
    // Get the allocated output buffers
    if (result == OMX_ErrorNone)
    {
      OMX_BUFFERHEADERTYPE** ppOutputBuffers;
      ppOutputBuffers = m_pEncoder->GetBuffers(OMX_FALSE);
      for (int i = 0; i < pConfig->nOutBufferCount; i++)
      {
        ppOutputBuffers[i]->pAppPrivate = m_pEncoder; // set the encoder as the private app data
        result = m_pOutputQueue->Push(&ppOutputBuffers[i], sizeof(ppOutputBuffers[i])); // store buffers in queue
        if (result != OMX_ErrorNone)
        {
          break;
        }
      }
    }

    //==========================================
    // Encode all frames
    if (result == OMX_ErrorNone)
    {
      result = CheckError(EncodeFrames(pConfig, pDynamicConfig));
    }

    //==========================================
    // Tear down the encoder (also deallocate buffers)
    if (m_pEncoder != NULL)
    {
      VENC_TEST_MSG_HIGH("Go to loaded state...");
      result = CheckError(m_pEncoder->GoToLoadedState());
    }

    //==========================================
    // Close the yuv file
    if (m_pSource != NULL)
    {
      result = m_pSource->Close();
    }

    //==========================================
    // Close the m4v file
    if (m_pSink != NULL)
    {
      result = m_pSink->Close();
    }

    //==========================================
    // Free our helper classes
    if (m_pEncoder)
      delete m_pEncoder;
    if (m_pInputQueue)
      delete m_pInputQueue;
    if (m_pOutputQueue)
      delete m_pOutputQueue;
    if (m_pSignalQueue)
      delete m_pSignalQueue;
    if (m_pSource)
      delete m_pSource;
    if (m_pSink)
      delete m_pSink;

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::ProcessSyntaxHeader()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pBuffer;

    VENC_TEST_MSG_HIGH("waiting for syntax header...");
    result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));

    if (result == OMX_ErrorNone)
    {
      result = CheckError(m_pEncoder->DeliverOutput(pBuffer));
      if (result == OMX_ErrorNone)
      {
        result = CheckError(m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
              sizeof(pBuffer),
              1000)); // wait for 1 second max
        if (result == OMX_ErrorNone)
        {
          if ((pBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) != 0)
          {
            OMX_U32 nBytesWritten;
            result = m_pSink->Write(pBuffer->pBuffer,
                pBuffer->nFilledLen,
                &nBytesWritten);
            (void) m_pOutputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));

            if (result != OMX_ErrorNone)
            {
              VENC_TEST_MSG_ERROR("error writing to file");
              result = OMX_ErrorUndefined;
            }
          }
          else
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
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to pop output");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::EncodeFrames(EncoderConfigType* pEncoderConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BOOL bKeepRunning = OMX_TRUE;
    OMX_S32 nInputDone = 0;
    OMX_S32 nOutputDone = 0;
    OMX_S32 nConsecIframes = 0;
    OMX_BOOL bRequestedIFrame = OMX_FALSE;

    OMX_TICKS nTimeStamp = 0;

    // Let's get the syntax header unless H.263
    if (pEncoderConfig->nFrames > 0 &&
        pEncoderConfig->eCodec != OMX_VIDEO_CodingH263)
    {
      result = CheckError(ProcessSyntaxHeader());
    }

    while (bKeepRunning == OMX_TRUE && result == OMX_ErrorNone)
    {
      OMX_BUFFERHEADERTYPE* pBuffer;

      //==========================================
      // see if we are finished
      if (nInputDone == pEncoderConfig->nFrames &&
          nOutputDone == pEncoderConfig->nFrames)
      {
        VENC_TEST_MSG_HIGH("Finished encoding %ld frames", pEncoderConfig->nFrames);
        bKeepRunning = OMX_FALSE;
      }

      //==========================================
      // See if both the input buffer and output buffer have been released.
      // This will serialize the encoding.
      else if (nInputDone == nOutputDone)
      {

        result = m_pInputQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer));
        if (result != OMX_ErrorNone)
        {
          // shouldn't happen
          VENC_TEST_MSG_ERROR("failed to pop input");
          break;
        }

        OMX_S32 nBytesIn = pEncoderConfig->nFrameWidth * pEncoderConfig->nFrameHeight * 3 / 2;
        if (m_pSource != NULL)
        {
          //==========================================
          // read some yuv data
          OMX_S32 nBytesOut = 0;
          result = m_pSource->Read(pBuffer->pBuffer,
              nBytesIn,
              &nBytesOut);

          if (nBytesIn != nBytesOut || result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_HIGH("yuv file is too small. seeking to start.");
            result = m_pSource->SeekStart(0);
            if (result != OMX_ErrorNone)
            {
              // shouldn't happen
              VENC_TEST_MSG_ERROR("failed to seek");
              break;
            }

            result = m_pSource->Read(pBuffer->pBuffer,
                nBytesIn,
                &nBytesOut);
            if (result != OMX_ErrorNone)
            {
              // shouldn't happen
              VENC_TEST_MSG_ERROR("failed to read");
              break;
            }
          }
        }

        if (nInputDone % pDynamicConfig->nIFrameRequestPeriod == 0 &&
            nInputDone != 0)
        {
          VENC_TEST_MSG_HIGH("requesting iframe...");
          bRequestedIFrame = OMX_TRUE;
          result = CheckError(m_pEncoder->RequestIntraVOP());

          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed to request iframe");
            break;
          }
        }

        //==========================================
        // deliver input
        VENC_TEST_MSG_HIGH("Delivering input frame=%ld %p", nInputDone, pBuffer->pBuffer);
        pBuffer->nTimeStamp = nTimeStamp;
        pBuffer->nFilledLen = (OMX_U32) nBytesIn;
        nTimeStamp = nTimeStamp + (1000000 / pEncoderConfig->nFramerate);
        result = m_pEncoder->DeliverInput(pBuffer);

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed to deliver input");
          break;
        }

        //==========================================
        // deliver output
        result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer));
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed to pop output");
          break;
        }

        VENC_TEST_MSG_HIGH("Delivering output frame=%ld %p", nOutputDone, pBuffer->pBuffer);
        result = CheckError(m_pEncoder->DeliverOutput(pBuffer));
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed to deliver output");
          break;
        }
      }

      if (bKeepRunning == OMX_TRUE)
      {

        //==========================================
        // wait for a signal
        result = CheckError(m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
              sizeof(pBuffer),
              0)); // wait infinitely
        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed popping");
          break;
        }

        if (pBuffer->nInputPortIndex == PORT_INDEX_IN)
        {
          //==========================================
          // push the buffer on the back of the buffer queue
          VENC_TEST_MSG_HIGH("Received input frame=%ld", nInputDone);
          result = m_pInputQueue->Push(&pBuffer, sizeof(pBuffer));
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed pushing");
            break;
          }
          ++nInputDone;
        }
        else
        {
          // we are expecting an iframe
          if (bRequestedIFrame == OMX_TRUE ||                   // 1. we requested one
              nOutputDone % pEncoderConfig->nIntraPeriod == 0)  // 2. intra period expired
          {
            // is this an iframe?
            if ((pBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) == 0)
            {
              // perhaps we got an iframe the last frame which is why we did not get one
              if (nConsecIframes == 0)
              {
                VENC_TEST_MSG_ERROR("got pframe but expecting an iframe");
                result = CheckError(OMX_ErrorUndefined);
                break;
              }
            }
            else
            {
              VENC_TEST_MSG_HIGH("got iframe as expected");
            }
          }
          else if ((pBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) != 0)
          {
            // we got an iframe but were not expecting one
            VENC_TEST_MSG_ERROR("got iframe but not expecting one");
            result = CheckError(OMX_ErrorUndefined);
            break;
          }

          bRequestedIFrame = OMX_FALSE;

          if ((pBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME) != 0)
          {
            ++nConsecIframes;
          }
          else
          {
            nConsecIframes = 0;
          }

          if (nConsecIframes > 1)
          {
            VENC_TEST_MSG_ERROR("got more than one consecutive iframe");
            result = CheckError(OMX_ErrorUndefined);
            break;
          }

          //==========================================
          // push the buffer on the back of the buffer queue
          VENC_TEST_MSG_HIGH("Received output frame=%ld", nOutputDone);
          result = m_pOutputQueue->Push(&pBuffer, sizeof(pBuffer));
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed pushing");
            break;
          }
          ++nOutputDone;

          //==========================================
          // write to file
          if (m_pSink != NULL)
          {
            OMX_U32 nBytesWritten;
            result = m_pSink->Write(pBuffer->pBuffer,
                pBuffer->nFilledLen,
                &nBytesWritten);

            if (result != OMX_ErrorNone)
            {
              VENC_TEST_MSG_ERROR("error writing to file");
            }
            else if (nBytesWritten != pBuffer->nFilledLen)
            {
              VENC_TEST_MSG_ERROR("mismatched number of bytes in file write");
              result = CheckError(OMX_ErrorUndefined);
              break;
            }
          }
        }
      }
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::EBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return ((TestIFrameRequest*) pAppData)->m_pSignalQueue->Push(&pBuffer, sizeof(pBuffer));
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestIFrameRequest::FBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return ((TestIFrameRequest*) pAppData)->m_pSignalQueue->Push(&pBuffer, sizeof(pBuffer));
  }

} // namespace venctest
