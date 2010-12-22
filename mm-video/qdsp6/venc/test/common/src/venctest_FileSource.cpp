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
#include "venctest_Thread.h"
#include "venctest_SignalQueue.h"
#include "venctest_Sleeper.h"
#include "venctest_File.h"
#include "venctest_Time.h"
#include "venctest_FileSource.h"

namespace venctest
{
  static const OMX_S32 MAX_BUFFER_ASSUME = 16;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  FileSource::FileSource()
    : m_nFrames(0),
    m_nFramesRegistered(0),
    m_nFramerate(0),
    m_nFrameWidth(0),
    m_nFrameHeight(0),
    m_nBuffers(0),
    m_nDVSXOffset(0),
    m_nDVSYOffset(0),
    m_pFile(NULL),
    m_pBufferQueue(new SignalQueue(MAX_BUFFER_ASSUME, sizeof(OMX_BUFFERHEADERTYPE*))),
    m_pThread(new Thread()),
    m_bStarted(OMX_FALSE),
    m_pFrameDeliverFn(NULL)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  FileSource::~FileSource()
  {
    if (m_pFile != NULL)
    {
      (void) m_pFile->Close();
      delete m_pFile;
    }
    if (m_pBufferQueue != NULL)
    {
      delete m_pBufferQueue;
    }
    if (m_pThread != NULL)
    {
      delete m_pThread;
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::Configure(OMX_S32 nFrames,
      OMX_S32 nFramerate,
      OMX_S32 nFrameWidth,
      OMX_S32 nFrameHeight,
      OMX_S32 nBuffers,
      FrameDeliveryFnType pFrameDeliverFn,
      OMX_STRING pFileName,
      OMX_S32 nDVSXOffset,
      OMX_S32 nDVSYOffset,
      OMX_BOOL bLiveMode)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (nFrames >= 0 &&
        nFramerate > 0 &&
        nFrameWidth > 0 &&
        nFrameHeight > 0 &&
        nDVSXOffset >= 0 &&
        nDVSYOffset >= 0 &&
        nBuffers > 0 &&
        nBuffers <= MAX_BUFFER_ASSUME &&
        pFrameDeliverFn != NULL)
    {
      m_nFrames = nFrames;
      m_nFramerate = nFramerate;
      m_nFrameWidth = nFrameWidth;
      m_nFrameHeight = nFrameHeight;
      m_nBuffers = nBuffers;
      m_nDVSXOffset = nDVSXOffset;
      m_nDVSYOffset = nDVSYOffset;
      m_pFrameDeliverFn = pFrameDeliverFn;
      m_bLiveMode = bLiveMode;

      if (pFileName != NULL)
      {
        m_pFile = new File();
        if (m_pFile != NULL)
        {
          result = m_pFile->Open(pFileName, OMX_TRUE);
          if (result != OMX_ErrorNone)
          {
            VENC_TEST_MSG_ERROR("Failed to open file");
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("Failed to allocate file");
          result = OMX_ErrorInsufficientResources;
        }
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad params");
      result = OMX_ErrorBadParameter;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::ChangeFrameRate(OMX_S32 nFramerate)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (nFramerate > 0)
    {
      m_nFramerate = nFramerate;
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad frame rate");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::Start()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    // make sure we've been configured
    if (m_nFrames >= 0 &&
        m_nFramerate >= 0 &&
        m_nBuffers > 0)
    {
      if (m_nFramesRegistered == m_nBuffers)
      {

        VENC_TEST_MSG_MEDIUM("starting thread...");

        result = m_pThread->Start(SourceThreadEntry,  // thread fn
            this,               // thread data
            0);                 // thread priority

        if (result == OMX_ErrorNone)
        {
          m_bStarted = OMX_TRUE;
        }
        else
        {
          VENC_TEST_MSG_ERROR("failed to start thread");
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("need to register all buffers with the source");
        result = OMX_ErrorUndefined;
      }

    }
    else
    {
      VENC_TEST_MSG_ERROR("source has not been configured");
      result = OMX_ErrorUndefined;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::Finish()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_bStarted == OMX_TRUE)
    {
      if (m_pThread != NULL)
      {
        OMX_ERRORTYPE threadResult;

        VENC_TEST_MSG_MEDIUM("waiting for thread to finish...");

        // wait for thread to exit
        result = m_pThread->Join(&threadResult);

        if (result == OMX_ErrorNone)
        {
          result = threadResult;
        }

        if (threadResult != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("source thread execution error");
        }
      }

      m_bStarted = OMX_FALSE;
    }
    else
    {
      VENC_TEST_MSG_ERROR("already stopped");
      result = OMX_ErrorIncorrectStateTransition;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::SetFreeBuffer(OMX_BUFFERHEADERTYPE* pBufferHdr)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pBufferHdr != NULL && pBufferHdr->pBuffer != NULL)
    {
      // if we have not started then the client is registering buffers
      if (m_bStarted == OMX_FALSE)
      {
        // need to fill the buffer with YUV data upon registration
        if (m_nFramesRegistered < m_nBuffers &&
            m_bLiveMode == OMX_TRUE)
        {

          VENC_TEST_MSG_MEDIUM("register buffer");

          if (m_pFile)
          {
            OMX_S32 nFrameBytes = m_nFrameWidth * m_nFrameHeight * 3 / 2;
            OMX_S32 nBytesRead;
            result = m_pFile->Read((OMX_U8*)pBufferHdr->pBuffer,
                nFrameBytes,
                &nBytesRead);
            if (result != OMX_ErrorNone ||
                nBytesRead != nFrameBytes)
            {
              VENC_TEST_MSG_HIGH("yuv file is too small"
                  "result(%d) nFrameBytes(%ld) nBytesRead(%ld)",
                  result, nFrameBytes, nBytesRead);
              result = m_pFile->SeekStart(0);
              result = m_pFile->Read((OMX_U8 *)pBufferHdr->pBuffer,
                  nFrameBytes,
                  &nBytesRead);
            }
          }
        }

        ++m_nFramesRegistered;
      }
      result = m_pBufferQueue->Push(&pBufferHdr,
          sizeof(OMX_BUFFERHEADERTYPE**));
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad params");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::SourceThreadEntry(OMX_PTR pThreadData)
  {
    OMX_ERRORTYPE result = OMX_ErrorBadParameter;
    if (pThreadData)
    {
      result = ((FileSource*) pThreadData)->SourceThread();
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSource::SourceThread()
  {
    OMX_BOOL bKeepGoing = OMX_TRUE;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_TICKS nTimeStamp = 0;

    VENC_TEST_MSG_MEDIUM("thread has started");

    for (OMX_S32 i = 0; i < m_nFrames && bKeepGoing == OMX_TRUE; i++)
    {

      OMX_BUFFERHEADERTYPE* pBufferHdr = NULL;
      const OMX_S32 nFrameBytes = m_nFrameWidth * m_nFrameHeight * 3 / 2;

      // Since frame rate can change at any time, let's make sure that we use
      // the same frame rate for the duration of this loop iteration
      OMX_S32 nFramerate = m_nFramerate;

      // If in live mode we deliver frames in a real-time fashion
      if (m_bLiveMode == OMX_TRUE)
      {
        Sleeper::Sleep(1000 / nFramerate);

        if (m_pBufferQueue->GetSize() <= 0)
        {
          VENC_TEST_MSG_MEDIUM("No buffers. Block until buffer available...");
        }

        VENC_TEST_MSG_MEDIUM("Wait for free buffer...");

        result = m_pBufferQueue->Pop(&pBufferHdr,
            sizeof(pBufferHdr),
            0); // wait forever
      }

      // if not in live mode, we deliver frames as they become available
      else
      {
        result = m_pBufferQueue->Pop(&pBufferHdr,
            sizeof(pBufferHdr),
            0); // wait forever
        if (m_pFile != NULL)
        {
          OMX_S32 nBytesRead;
          result = m_pFile->Read((OMX_U8 *)pBufferHdr->pBuffer,
              nFrameBytes,
              &nBytesRead);
          if (result != OMX_ErrorNone ||
              nBytesRead != nFrameBytes)
          {
            VENC_TEST_MSG_HIGH("yuv file is too small"
                "result:%d,nFrameBytse(%ld),nBytesRead(%ld)",
                result, nFrameBytes, nBytesRead);

            VENC_TEST_MSG_HIGH("buffer virt %p \n", pBufferHdr->pBuffer);
            result = m_pFile->SeekStart(0);
            result = m_pFile->Read((OMX_U8 *)pBufferHdr->pBuffer,
                nFrameBytes,
                &nBytesRead);
          }
        }
      }

      if (result == OMX_ErrorNone)
      {
        if (pBufferHdr != NULL)
        {
          VENC_TEST_MSG_MEDIUM("delivering frame %ld...",i);

          if (m_bLiveMode == OMX_TRUE)
          {
            nTimeStamp = (OMX_TICKS) Time::GetTimeMicrosec();
          }
          else
          {
            nTimeStamp = nTimeStamp + (OMX_TICKS) (1000000 / nFramerate);
          }

          pBufferHdr->nFilledLen = nFrameBytes;
          pBufferHdr->nTimeStamp = nTimeStamp;

          // set the EOS flag if this is the last frame
          pBufferHdr->nFlags = 0;
          if (i == m_nFrames - 1)
          {
            pBufferHdr->nFlags = OMX_BUFFERFLAG_EOS;
          }

          pBufferHdr->nOffset = ((m_nFrameWidth * m_nDVSYOffset) + m_nDVSXOffset) * 3 / 2;
          m_pFrameDeliverFn(pBufferHdr);
        }
        else
        {
          VENC_TEST_MSG_ERROR("Buffer is null");
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("Error getting buffer");
        bKeepGoing = OMX_FALSE;
      }
    }
    VENC_TEST_MSG_HIGH("Source thread is exiting...");
    return result;
  }
} // namespace venctest
