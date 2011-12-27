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
#include "venctest_File.h"
#include "venctest_Time.h"
#include "venctest_FileSink.h"
#include "venctest_Parser.h"

namespace venctest
{
  static const OMX_S32 MAX_BUFFER_ASSUME = 16;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  FileSink::FileSink()
    : m_nFrames(0),
    m_pFile(),
    m_pBufferQueue(new SignalQueue(MAX_BUFFER_ASSUME, sizeof(OMX_BUFFERHEADERTYPE*))),
    m_pThread(new Thread()),
    m_bStarted(OMX_FALSE),
    m_pFrameReleaseFn(NULL)
  {
    VENC_TEST_MSG_LOW("created sink");
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  FileSink::~FileSink()
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
  OMX_ERRORTYPE FileSink::Configure(OMX_S32 nFrames,
      OMX_STRING pFileName,
      OMX_S32 nTestNum,
      FrameReleaseFnType pFrameReleaseFn)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (nFrames >= 0 &&
        pFrameReleaseFn != NULL)
    {
      m_nFrames = nFrames;
      m_pFrameReleaseFn = pFrameReleaseFn;

      if (pFileName != NULL &&
          Parser::StringICompare((OMX_STRING)"", pFileName) != 0)
      {
        (void) Parser::AppendTestNum(pFileName, nTestNum);

        m_pFile = new File();
        if (m_pFile != NULL)
        {
          VENC_TEST_MSG_MEDIUM("Opening output file...");
          result = m_pFile->Open(pFileName, OMX_FALSE);
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
      else
      {
        VENC_TEST_MSG_MEDIUM("No output file");
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("Bad param(s)");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSink::Write(OMX_BUFFERHEADERTYPE* pBufferHdr)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pBufferHdr != NULL && pBufferHdr->pBuffer != NULL)
    {
      result = m_pBufferQueue->Push(&pBufferHdr,
          sizeof(OMX_BUFFERHEADERTYPE**));
      if (result != OMX_ErrorNone)
      {
        VENC_TEST_MSG_ERROR("failed to push buffer");
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
  OMX_ERRORTYPE FileSink::Start()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    // make sure we've been configured
    if (m_nFrames >= 0)
    {
      VENC_TEST_MSG_MEDIUM("starting thread...");
      result = m_pThread->Start(SinkThreadEntry,  // thread fn
          this,             // thread data
          0);               // thread priority
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
      VENC_TEST_MSG_ERROR("source has not been configured");
      result = OMX_ErrorUndefined;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSink::Finish()
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
          VENC_TEST_MSG_ERROR("sink thread execution error");
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
  OMX_ERRORTYPE  FileSink::SinkThreadEntry(OMX_PTR pThreadData)
  {
    OMX_ERRORTYPE result = OMX_ErrorBadParameter;
    if (pThreadData)
    {
      result = ((FileSink*) pThreadData)->SinkThread();
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE FileSink::SinkThread()
  {
    OMX_BOOL bKeepGoing = OMX_TRUE;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    VENC_TEST_MSG_MEDIUM("thread has started");

    for (OMX_S32 i = 0; i < m_nFrames && bKeepGoing == OMX_TRUE; i++)
    {

      OMX_BUFFERHEADERTYPE* pBufferHdr = NULL;

      result = m_pBufferQueue->Pop(&pBufferHdr,
          sizeof(pBufferHdr),
          0); // wait forever

      if (result == OMX_ErrorNone)
      {
        if (pBufferHdr != NULL)
        {
          OMX_U32 nBytes;

          if (m_pFile != NULL)
          {
            if (pBufferHdr->nFilledLen > 0)
            {
              VENC_TEST_MSG_HIGH("writing frame %ld with %lu bytes...", i, pBufferHdr->nFilledLen);
              result = m_pFile->Write((OMX_U8 *)pBufferHdr->pBuffer,
                  pBufferHdr->nFilledLen,
                  &nBytes);

              if (result != OMX_ErrorNone)
              {
                VENC_TEST_MSG_ERROR("error writing to file...");
              }
              else if (nBytes != pBufferHdr->nFilledLen)
              {
                VENC_TEST_MSG_ERROR("mismatched number of bytes in file write");
                result = OMX_ErrorUndefined;
              }
            }
            else
            {
              VENC_TEST_MSG_HIGH("skipping frame %ld...",i);
            }

          }
          else
          {
            VENC_TEST_MSG_MEDIUM("received frame %ld...",i);
          }

          if (pBufferHdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
          {
            // this is just the syntax header, not a frame so increase loop count
            VENC_TEST_MSG_HIGH("got codecconfig");
            ++m_nFrames;
          }

          if (pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)
          {
            // this is the last frame. note that we may get fewer frames
            // than expected if RC is enabled with frame skips
            VENC_TEST_MSG_HIGH("got eos");
            bKeepGoing = OMX_FALSE;
          }

          m_pFrameReleaseFn(pBufferHdr);
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
    VENC_TEST_MSG_HIGH("Sink thread is exiting...");
    return result;
  }

} // namespace venctest
