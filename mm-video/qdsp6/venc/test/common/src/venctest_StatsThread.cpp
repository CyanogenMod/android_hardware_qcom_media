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
#include "venctest_Signal.h"
#include "venctest_Mutex.h"
#include "venctest_Time.h"
#include "venc_mutex.h"
#include "venc_signal.h"
#include "venc_thread.h"
#include "venctest_StatsThread.h"

namespace venctest
{
  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  StatsThread::StatsThread(OMX_S32 nSamplePeriodMillis)
    : m_pThread(NULL),
    m_pSignal(NULL),
    m_pMutex(NULL),
    m_nSamplePeriodMillis(nSamplePeriodMillis),
    m_nInputFrames(0),
    m_nOutputFrames(0),
    m_nBits(0)
  {
    if (venc_mutex_create(&m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to create mutex");
    }

    if (venc_signal_create(&m_pSignal) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to create signal");
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  StatsThread::~StatsThread()
  {
    venc_signal_destroy(m_pSignal);
    venc_mutex_destroy(m_pMutex);
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE StatsThread::Start()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    m_nInputFrames = 0;
    m_nOutputFrames = 0;
    m_nBits = 0;

    VENC_TEST_MSG_MEDIUM("starting stats thread...");
    if (venc_thread_create(&m_pThread,
          StatsThreadEntry,  // thread fn
          this, 0) != 0)  // arg

    {
      VENC_TEST_MSG_ERROR("failed to start stats thread");
      result = OMX_ErrorUndefined;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE StatsThread::Finish()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (m_pThread != NULL)
    {

      VENC_TEST_MSG_MEDIUM("waiting for stats thread to finish...");

      venc_thread_destroy(m_pThread, (int *)&result);

    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE StatsThread::SetInputStats(OMX_BUFFERHEADERTYPE* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (venc_mutex_lock(m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("mutex lock failure");
    }

    if (pBuffer != NULL)
    {
      if (pBuffer->nFilledLen > 0)
      {
        ++m_nInputFrames;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("null buffer");
      result = OMX_ErrorBadParameter;
    }

    if (venc_mutex_unlock(m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("mutex unlock failure");
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE StatsThread::SetOutputStats(OMX_BUFFERHEADERTYPE* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (venc_mutex_lock(m_pMutex) != 0 )
    {
      VENC_TEST_MSG_ERROR("mutex lock failure");
    }

    if (pBuffer != NULL)
    {
      m_nBits = m_nBits + pBuffer->nFilledLen * 8;

      if ((pBuffer->nFilledLen > 0) &&
          ((pBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) == 0) &&
          ((pBuffer->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) != 0))
      {
        ++m_nOutputFrames;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("null buffer");
      result = OMX_ErrorBadParameter;
    }

    if (venc_mutex_unlock(m_pMutex)!= 0)
    {
      VENC_TEST_MSG_ERROR("mutex unlock failure");
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  int StatsThread::StatsThreadEntry(void * pThreadData)
  {
    OMX_ERRORTYPE result = OMX_ErrorBadParameter;
    if (pThreadData)
    {
      result = ((StatsThread*) pThreadData)->StatsThreadFn();
    }
    return 0;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE StatsThread::StatsThreadFn()
  {
    OMX_BOOL bKeepGoing = OMX_TRUE;
    OMX_ERRORTYPE result = OMX_ErrorNone;

    VENC_TEST_MSG_MEDIUM("Stats thread has started");
#if 0
    while (bKeepGoing == OMX_TRUE)
    {
      struct timespec time;
      int result = 0;

      clock_gettime(CLOCK_REALTIME, &time);
      time.tv_sec += STATS_WAIT_TIME_SEC;
      result = pthread_cond_timedwait(m_pSignal, m_pMutex, &time);
      if (result == ETIMEDOUT)
      {
        OMX_S32 nBitrate;
        OMX_S32 nInputFPS;
        OMX_S32 nOutputFPS;

        if (venc_mutex_lock(m_pMutex) != 0 )
        {
          VENC_TEST_MSG_ERROR("mutex lock failure");
        }

        if (nWaitTime > 0 && (nWaitTime / 1000 > 0))
        {
          nBitrate = (OMX_S32) (m_nBits / (nWaitTime / 1000));
          nInputFPS = (OMX_S32) (m_nInputFrames / (nWaitTime / 1000));
          nOutputFPS = (OMX_S32) (m_nOutputFrames / (nWaitTime / 1000));
        }
        else
        {
          nBitrate = 0;
          nInputFPS = 0;
          nOutputFPS = 0;
        }

        m_nInputFrames = 0;
        m_nOutputFrames = 0;
        m_nBits = 0;

        if (venc_mutex_unlock(m_pMutex) != 0)
        {
          VENC_TEST_MSG_ERROR("mutex unlock failure");
        }

        VENC_TEST_MSG_PROFILE("Time=%d millis, Input FPS=%d",
            (int) nWaitTime, (int) nInputFPS);
        VENC_TEST_MSG_PROFILE("Output FPS=%d, Bitrate=%d",
            (int) nOutputFPS, (int) nBitrate);
      }
      else
      {
        bKeepGoing = OMX_FALSE;
      }
    }
#endif
    VENC_TEST_MSG_HIGH("Stats thread is exiting...");
    return result;
  }

} // namespace venctest
