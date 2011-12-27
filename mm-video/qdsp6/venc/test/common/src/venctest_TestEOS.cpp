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
#include "venctest_TestEOS.h"
#include "venctest_Time.h"
#include "venctest_Encoder.h"
#include "venctest_Queue.h"
#include "venctest_SignalQueue.h"
#include "venctest_File.h"
#include "venctest_Sleeper.h"

namespace venctest
{
  static const OMX_U32 PORT_INDEX_IN = 0;
  static const OMX_U32 PORT_INDEX_OUT = 1;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestEOS::TestEOS()
    : ITestCase(),    // invoke the base class constructor
    m_pEncoder(NULL),
    m_pInputQueue(NULL),
    m_pOutputQueue(NULL),
    m_pSignalQueue(NULL),
    m_pSource(NULL),
    m_nTimeStamp(0)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestEOS::~TestEOS()
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::ValidateAssumptions(EncoderConfigType* m_pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_pConfig->eControlRate == OMX_Video_ControlRateVariableSkipFrames ||
        m_pConfig->eControlRate == OMX_Video_ControlRateConstantSkipFrames)
    {
      VENC_TEST_MSG_ERROR("Frame skip must be disabled for this to work");
      result = OMX_ErrorUndefined;
    }

    if (m_pConfig->nInBufferCount != m_pConfig->nOutBufferCount)
    {
      VENC_TEST_MSG_ERROR("Need matching number of input and output buffers");
      result = OMX_ErrorUndefined;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::Execute(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    m_pConfig = pConfig;

    if (result == OMX_ErrorNone)
    {
      //==========================================
      // Create signal queue
      VENC_TEST_MSG_HIGH("Creating signal queue...");
      m_pSignalQueue = new SignalQueue(32, sizeof(OMX_BUFFERHEADERTYPE*)); // max 32 messages

      //==========================================
      // Create input buffer queue
      VENC_TEST_MSG_HIGH("Creating input buffer queue...");
      m_pInputQueue = new Queue(m_pConfig->nInBufferCount,
          sizeof(OMX_BUFFERHEADERTYPE*));

      //==========================================
      // Create output buffer queue
      VENC_TEST_MSG_HIGH("Creating output buffer queue...");
      m_pOutputQueue = new Queue(m_pConfig->nOutBufferCount,
          sizeof(OMX_BUFFERHEADERTYPE*));
    }

    //==========================================
    // Create and open yuv file
    if (m_pConfig->cInFileName[0] != (char) 0)
    {
      VENC_TEST_MSG_HIGH("Creating file source...");
      m_pSource = new File();
      result = m_pSource->Open(m_pConfig->cInFileName, OMX_TRUE);
    }
    else
    {
      VENC_TEST_MSG_HIGH("Not reading from input file");
    }

    //==========================================
    // Create and configure the encoder
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Creating encoder...");
      m_pEncoder = new Encoder(EBD,
          FBD,
          this, // set the test case object as the callback app data
          m_pConfig->eCodec);
      result = m_pEncoder->Configure(m_pConfig);

      if (result == OMX_ErrorNone)
      {
        result = m_pEncoder->EnableUseBufferModel(m_pConfig->bInUseBuffer, m_pConfig->bOutUseBuffer);
      }
    }

    //==========================================
    // Go to executing state (also allocate buffers)
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Go to executing state...");
      result = m_pEncoder->GoToExecutingState();
    }

    //==========================================
    // Get the allocated input buffers
    if (result == OMX_ErrorNone)
    {
      OMX_BUFFERHEADERTYPE** ppInputBuffers;
      ppInputBuffers = m_pEncoder->GetBuffers(OMX_TRUE);
      for (int i = 0; i < m_pConfig->nInBufferCount; i++)
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
      for (int i = 0; i < m_pConfig->nOutBufferCount; i++)
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
    // Get the syntax header
    if (result == OMX_ErrorNone)
    {
      // let's get the syntax header
      result = ProcessSyntaxHeader();
    }

    //==========================================
    // run eos test, send no input buffers
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestSessionStart with eos attached to valid input...");
      result = EOSTestSessionStart(OMX_FALSE);
    }

    //==========================================
    // run eos test, send no input buffers
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestSessionStart with eos not attached to valid input...");
      result = EOSTestSessionStart(OMX_TRUE);
    }

    //==========================================
    // run eos test, make encoder wait until it gets an output buffer (last frame length!=0)
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestDelayOutput with eos attached to valid input...");
      result = EOSTestDelayOutput(OMX_FALSE);
    }

    //==========================================
    // run eos test, make encoder wait until it gets an output buffer (last frame length=0)
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestDelayOutput with eos not attached to valid input...");
      result = EOSTestDelayOutput(OMX_TRUE);
    }

    //==========================================
    // run eos test, send multiple frames with last frame length!=0
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestRapidFire with eos attached to valid input...");
      result = EOSTestRapidFire(OMX_FALSE);
    }

    //==========================================
    // run eos test, send multiple frames with last frame length=0
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Running EOSTestRapidFire with eos not attached to valid input...");
      result = EOSTestRapidFire(OMX_TRUE);
    }

    //==========================================
    // Tear down the encoder (also deallocate buffers)
    if (m_pEncoder != NULL)
    {
      VENC_TEST_MSG_HIGH("Go to loaded state...");
      result = m_pEncoder->GoToLoadedState();
    }

    //==========================================
    // Close the yuv file
    if (m_pSource != NULL)
    {
      result = m_pSource->Close();
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

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::ProcessSyntaxHeader()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pBuffer;

    VENC_TEST_MSG_HIGH("waiting for syntax header...");
    result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));

    if (result == OMX_ErrorNone)
    {
      pBuffer->nFilledLen = 0;
      pBuffer->nFlags = 0;
      result = m_pEncoder->DeliverOutput(pBuffer);
      if (result == OMX_ErrorNone)
      {
        result = m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer),
            1000); // wait for 1 second max
        if (result == OMX_ErrorNone)
        {
          if ((pBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) == 0)
          {
            VENC_TEST_MSG_ERROR("expecting codeconfig flag");
            result = OMX_ErrorUndefined;
          }
          (void) m_pOutputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));
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
  OMX_ERRORTYPE TestEOS::EOSTestSessionStart(OMX_BOOL bEmptyEOSBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pBuffer;

    result = m_pInputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
    if (result == OMX_ErrorNone)
    {
      // deliver input
      if (bEmptyEOSBuffer == OMX_TRUE)
      {
        pBuffer->nFilledLen = 0;
      }
      else
      {
        pBuffer->nFilledLen = m_pConfig->nFrameWidth *
          m_pConfig->nFrameHeight * 3 / 2;
      }
      pBuffer->nFlags = OMX_BUFFERFLAG_EOS;
      VENC_TEST_MSG_HIGH("delivering input with eos");
      pBuffer->nTimeStamp = NextTimeStamp(m_pConfig->nFramerate);
      result = m_pEncoder->DeliverInput(pBuffer);
    }

    if (result == OMX_ErrorNone)
    {
      result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
    }

    if (result == OMX_ErrorNone)
    {
      // deliver output
      pBuffer->nFlags = 0;
      pBuffer->nFilledLen = 0;
      VENC_TEST_MSG_HIGH("delivering output");

      result = m_pEncoder->DeliverOutput(pBuffer);
    }

    if (result == OMX_ErrorNone)
    {
      // wait for input and output buffer
      for (int i = 0; i < 2; i++)
      {
        VENC_TEST_MSG_HIGH("waiting for buffer");
        result = m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer),
            1000); // wait 1 second max
        if (result == OMX_ErrorNone)
        {
          if (pBuffer->nInputPortIndex == PORT_INDEX_IN)
          {
            VENC_TEST_MSG_HIGH("got input");

            // put it back on the queue
            (void) m_pInputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));
          }
          else
          {
            VENC_TEST_MSG_HIGH("got output");

            // put it back on the queue
            (void) m_pOutputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));

            // make sure we get EOS with len == 0
            if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS)
            {
              if (bEmptyEOSBuffer == OMX_TRUE &&
                  pBuffer->nFilledLen != 0)
              {
                VENC_TEST_MSG_ERROR("expected a length of 0 but got %d",
                    (int) pBuffer->nFilledLen);
                result = OMX_ErrorUndefined;
              }
              else if (bEmptyEOSBuffer == OMX_FALSE &&
                  pBuffer->nFilledLen == 0)
              {
                VENC_TEST_MSG_ERROR("expected a non zero length");
                result = OMX_ErrorUndefined;
              }

              if (result == OMX_ErrorNone)
              {
                VENC_TEST_MSG_HIGH("eos test passed");
              }

            }
            else
            {
              VENC_TEST_MSG_ERROR("failed to get eos");
              result = OMX_ErrorUndefined;
            }
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to pop msg");
          break;
        }
      }
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::EOSTestRapidFire(OMX_BOOL bEmptyEOSBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pBuffer;

    OMX_S32 nInputToDeliver = m_pInputQueue->GetSize();
    OMX_S32 nOutputToDeliver = m_pOutputQueue->GetSize();

    // empty all output buffers
    if (result == OMX_ErrorNone)
    {
      for (OMX_S32 i = 0; i < nOutputToDeliver; i++)
      {
        result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
        if (result == OMX_ErrorNone)
        {
          pBuffer->nFilledLen = 0;
          pBuffer->nFlags = 0;
          result = m_pEncoder->DeliverOutput(pBuffer);
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed to deliver input");
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to pop buffer");
        }

        if (result != OMX_ErrorNone)
        {
          break;
        }
      }
    }

    // empty all input buffers
    if (result == OMX_ErrorNone)
    {
      for (OMX_S32 i = 0; i < nInputToDeliver; i++)
      {
        result = m_pInputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
        if (result == OMX_ErrorNone)
        {
          if (bEmptyEOSBuffer == OMX_TRUE &&
              i == nInputToDeliver - 1)
          {
            // this is the last frame, and empty eos buffer
            pBuffer->nFlags = OMX_BUFFERFLAG_EOS;
            pBuffer->nFilledLen = 0;
          }
          else if (bEmptyEOSBuffer == OMX_FALSE &&
              i == nInputToDeliver - 1)
          {
            // this is the last frame, and non-empty eos buffer
            pBuffer->nFlags = OMX_BUFFERFLAG_EOS;
            pBuffer->nFilledLen = m_pConfig->nFrameWidth *
              m_pConfig->nFrameHeight * 3 / 2;
          }
          else
          {
            // this is not the last frame
            pBuffer->nFlags = 0;
            pBuffer->nFilledLen = m_pConfig->nFrameWidth *
              m_pConfig->nFrameHeight * 3 / 2;
          }

          // deliver input
          pBuffer->nTimeStamp = NextTimeStamp(m_pConfig->nFramerate);
          result = m_pEncoder->DeliverInput(pBuffer);
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("failed to deliver input");
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to pop buffer");
        }

        if (result != OMX_ErrorNone)
        {
          break;
        }
      }
    }

    // lets wait for all buffers to be released from encoder
    OMX_S32 nLastFlags = 0;
    OMX_S32 nLastFilledLen = 0;
    if (result == OMX_ErrorNone)
    {
      OMX_S32 nLooop = nInputToDeliver + nOutputToDeliver;
      OMX_S32 nOutputRemaining = nOutputToDeliver;

      for (OMX_S32 i = 0; i < nLooop; i++)
      {
        result = m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer),
            1000); // wait 1 second max
        if (result == OMX_ErrorNone)
        {
          if (pBuffer->nInputPortIndex == PORT_INDEX_IN)
          {
            m_pInputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));
          }
          else
          {
            nLastFlags = (OMX_S32) pBuffer->nFlags;
            nLastFilledLen = (OMX_S32) pBuffer->nFilledLen;
            m_pOutputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));
            --nOutputRemaining;
            if (nOutputRemaining != 0)
            {
              // length should not be zero
              if (nLastFilledLen == 0)
              {
                VENC_TEST_MSG_ERROR("unexpected len = 0");
                result = OMX_ErrorUndefined;
                break;
              }

              // we should not have eos
              if (nLastFlags & OMX_BUFFERFLAG_EOS)
              {
                VENC_TEST_MSG_ERROR("unexpected eos");
                result = OMX_ErrorUndefined;
                break;
              }
            }
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to pop buffer %d", (int) result);
          break;
        }
      }
    }

    if (result == OMX_ErrorNone)
    {
      // we are expecting eos flag on last buffer
      if (nLastFlags & OMX_BUFFERFLAG_EOS)
      {
        // see if we get the correct length
        if (bEmptyEOSBuffer == OMX_TRUE && nLastFilledLen != 0)
        {
          VENC_TEST_MSG_ERROR("was expecting any data, length=%d",
              (int) nLastFilledLen);
          result = OMX_ErrorUndefined;
        }
        else if (bEmptyEOSBuffer == OMX_FALSE && nLastFilledLen == 0)
        {
          VENC_TEST_MSG_ERROR("was expecting length=0");
          result = OMX_ErrorUndefined;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("did not get eos");
        result = OMX_ErrorUndefined;
      }
    }


    if (result == OMX_ErrorNone)
    {
      if (nInputToDeliver != m_pInputQueue->GetSize())
      {
        VENC_TEST_MSG_ERROR("we dont have all our input buffers");
        result = OMX_ErrorUndefined;
      }
      if (nOutputToDeliver != m_pOutputQueue->GetSize())
      {
        VENC_TEST_MSG_ERROR("we dont have all our output buffers");
        result = OMX_ErrorUndefined;
      }
    }

    // verify that both queues are now full

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::EOSTestDelayOutput(OMX_BOOL bEmptyEOSBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pBuffer;

    result = m_pInputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
    if (result == OMX_ErrorNone)
    {
      // deliver input
      if (bEmptyEOSBuffer == OMX_TRUE)
      {
        pBuffer->nFilledLen = 0;
      }
      else
      {
        pBuffer->nFilledLen = m_pConfig->nFrameWidth *
          m_pConfig->nFrameHeight * 3 / 2;
      }
      pBuffer->nFlags = OMX_BUFFERFLAG_EOS;
      VENC_TEST_MSG_HIGH("delivering input with eos");
      pBuffer->nTimeStamp = NextTimeStamp(m_pConfig->nFramerate);
      result = m_pEncoder->DeliverInput(pBuffer);
    }

    if (result == OMX_ErrorNone)
    {
      result = m_pOutputQueue->Pop((OMX_PTR) &pBuffer, sizeof(pBuffer));
    }

    if (result == OMX_ErrorNone)
    {
      (void) Sleeper::Sleep(2000); // sleep for 2 seconds

      if (m_pSignalQueue->GetSize() > 1)
      {
        VENC_TEST_MSG_ERROR("we should only have one buffer queued");
        result = OMX_ErrorUndefined;
      }
    }

    if (result == OMX_ErrorNone)
    {
      if (m_pSignalQueue->GetSize() == 1)
      {
        VENC_TEST_MSG_HIGH("peeking at buffer to make sure it is input");

        // if we have an input buffer let's make sure it
        // is not an output buffer since we have not delivered
        // an output buffer
        result = m_pSignalQueue->Peek((OMX_PTR) &pBuffer,
            sizeof(pBuffer));

        if (result == OMX_ErrorNone)
        {
          if (pBuffer->nInputPortIndex != PORT_INDEX_IN)
          {
            VENC_TEST_MSG_ERROR("this should be an input buffer");
            result = OMX_ErrorUndefined;
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to peek at buffer");
        }
      }
    }

    if (result == OMX_ErrorNone)
    {
      // deliver output
      pBuffer->nFilledLen = 0;
      pBuffer->nFlags = 0;
      VENC_TEST_MSG_HIGH("delivering output");

      result = m_pEncoder->DeliverOutput(pBuffer);
    }

    if (result == OMX_ErrorNone)
    {
      // wait for input and output buffer
      for (int i = 0; i < 2; i++)
      {
        VENC_TEST_MSG_HIGH("waiting for buffer");
        result = m_pSignalQueue->Pop((OMX_PTR) &pBuffer,
            sizeof(pBuffer),
            1000); // wait 1 second max
        if (result == OMX_ErrorNone)
        {
          if (pBuffer->nInputPortIndex == PORT_INDEX_IN)
          {
            VENC_TEST_MSG_HIGH("got input");

            // put it back on the queue
            (void) m_pInputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));
          }
          else
          {
            VENC_TEST_MSG_HIGH("got output");

            // put it back on the queue
            (void) m_pOutputQueue->Push((OMX_PTR) &pBuffer, sizeof(pBuffer));

            // make sure we get EOS with len == 0
            if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS)
            {
              if (bEmptyEOSBuffer == OMX_TRUE &&
                  pBuffer->nFilledLen != 0)
              {
                VENC_TEST_MSG_ERROR("expected a length of 0 but got %d",
                    (int) pBuffer->nFilledLen);
                result = OMX_ErrorUndefined;
              }
              else if (bEmptyEOSBuffer == OMX_FALSE &&
                  pBuffer->nFilledLen == 0)
              {
                VENC_TEST_MSG_ERROR("expected a non zero length");
                result = OMX_ErrorUndefined;
              }

              if (result == OMX_ErrorNone)
              {
                VENC_TEST_MSG_HIGH("eos test passed");
              }

            }
            else
            {
              VENC_TEST_MSG_ERROR("failed to get eos");
              result = OMX_ErrorUndefined;
            }
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to pop msg %d", (int) result);
          break;
        }
      }
    }
    return result;
  }

  OMX_TICKS TestEOS::NextTimeStamp(OMX_S32 nFramerate)
  {
    OMX_TICKS nTimeStamp = m_nTimeStamp;
    // increment by the corresponding number of microseconds
    m_nTimeStamp = m_nTimeStamp + (1000000 / nFramerate);
    return nTimeStamp;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::EBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return ((TestEOS*) pAppData)->m_pSignalQueue->Push(&pBuffer, sizeof(pBuffer));
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestEOS::FBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return ((TestEOS*) pAppData)->m_pSignalQueue->Push(&pBuffer, sizeof(pBuffer));
  }

} // namespace venctest
