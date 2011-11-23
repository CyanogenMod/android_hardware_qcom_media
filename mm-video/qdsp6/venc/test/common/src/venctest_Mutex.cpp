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
#include "venctest_Mutex.h"
#include "venctest_Debug.h"
#include "venc_mutex.h"

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Mutex::Mutex() :
    m_pMutex(NULL)
  {
    if (venc_mutex_create(&m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to init mutex");
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Mutex::~Mutex()
  {
    if (venc_mutex_destroy(m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to destroy mutex");
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Mutex::Lock()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (venc_mutex_lock(m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to lock mutex");
      result = OMX_ErrorUndefined;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Mutex::UnLock()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (venc_mutex_unlock(m_pMutex) != 0)
    {
      VENC_TEST_MSG_ERROR("failed to unlock mutex");
      result = OMX_ErrorUndefined;
    }
    return result;
  }
}
