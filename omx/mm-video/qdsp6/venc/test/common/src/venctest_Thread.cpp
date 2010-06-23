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
#include "venctest_Debug.h"
#include "venctest_Thread.h"
#include "venc_thread.h"

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Thread::Thread()
    : m_pFn(NULL),
    m_nPriority(0),
    m_pThread(NULL),
    m_pThreadArgs(NULL)
  {
    VENC_TEST_MSG_LOW("constructor");
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Thread::~Thread()
  {
    VENC_TEST_MSG_LOW("destructor");
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Thread::Start(StartFnType pFn,
      OMX_PTR pThreadArgs,
      OMX_S32 nPriority)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    VENC_TEST_MSG_LOW("Start");
    m_pThreadArgs = pThreadArgs;
    m_pFn = pFn;

    if (venc_thread_create(&m_pThread, ThreadEntry, this, (int) nPriority) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to create thread");
      result = OMX_ErrorUndefined;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Thread::Join(OMX_ERRORTYPE* pThreadResult)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    int thread_result;

    VENC_TEST_MSG_LOW("Join");

    if (venc_thread_destroy(m_pThread, &thread_result) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to destroy thread");
    }

    if (pThreadResult != NULL)
    {
      *pThreadResult = (OMX_ERRORTYPE) thread_result;
    }

    m_pThread = NULL;

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  int Thread::ThreadEntry(void* pThreadData)
  {
    Thread* pThread = (Thread*) pThreadData;
    VENC_TEST_MSG_LOW("ThreadEntry");
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pThread != NULL)
    {
      result = pThread->m_pFn(pThread->m_pThreadArgs);
    }
    else
    {
      VENC_TEST_MSG_ERROR("failed to create thread");
      result = OMX_ErrorUndefined;
    }


    return (int) result;
  }
} // namespace venctest
