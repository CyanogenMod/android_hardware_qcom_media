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
#include "venctest_Mutex.h"
#include "venctest_Queue.h"
#include "venctest_Signal.h"
#include "venctest_SignalQueue.h"

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  SignalQueue::SignalQueue()
  {
    VENC_TEST_MSG_ERROR("default constructor should not be here (private)");
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  SignalQueue::SignalQueue(OMX_S32 nMaxQueueSize,
      OMX_S32 nMaxDataSize)
    :  m_pSignal(new Signal()),
    m_pMutex(new Mutex()),
    m_pQueue(new Queue(nMaxQueueSize, nMaxDataSize))
  {
    VENC_TEST_MSG_LOW("constructor %ld %ld", nMaxQueueSize, nMaxDataSize);
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  SignalQueue::~SignalQueue()
  {
    VENC_TEST_MSG_LOW("destructor");
    if (m_pMutex != NULL)
      delete m_pMutex;
    if (m_pSignal != NULL)
      delete m_pSignal;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE SignalQueue::Pop(OMX_PTR pData,
      OMX_S32 nDataSize,
      OMX_S32 nTimeoutMillis)
  {
    VENC_TEST_MSG_LOW("Pop");
    OMX_ERRORTYPE result = OMX_ErrorNone;

    // wait for signal or for data to come into queue
    while (GetSize() == 0 && result == OMX_ErrorNone)
    {
      result = m_pSignal->Wait(nTimeoutMillis);
    }

    // did we timeout?
    if (result == OMX_ErrorNone)
    {
      // lock mutex
      m_pMutex->Lock();

      result = m_pQueue->Pop(pData, nDataSize);

      // unlock mutex
      m_pMutex->UnLock();
    }
    else if (result != OMX_ErrorTimeout)
    {
      VENC_TEST_MSG_ERROR("Error waiting for signal");
      result = OMX_ErrorUndefined;
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE SignalQueue::Push(OMX_PTR pData,
      OMX_S32 nDataSize)
  {
    VENC_TEST_MSG_LOW("Push");
    OMX_ERRORTYPE result = OMX_ErrorNone;

    // lock mutex
    m_pMutex->Lock();

    result = m_pQueue->Push(pData, nDataSize);

    // unlock mutex
    m_pMutex->UnLock();


    if (result == OMX_ErrorNone)
    {
      m_pSignal->Set();
    }

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE SignalQueue::Peek(OMX_PTR pData,
      OMX_S32 nDataSize)
  {
    VENC_TEST_MSG_LOW("Peek");
    OMX_ERRORTYPE result = OMX_ErrorNone;

    // lock mutex
    m_pMutex->Lock();

    result = m_pQueue->Peek(pData, nDataSize);

    // unlock mutex
    m_pMutex->UnLock();

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_S32 SignalQueue::GetSize()
  {
    return m_pQueue->GetSize();
  }

} // namespace venctest
