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

#ifndef _VENC_TEST_THREAD_H
#define _VENC_TEST_THREAD_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"
#include "venc_thread.h"

namespace venctest
{

  /**
   * @brief Thread class
   */
  class Thread
  {
    public:

      /**
       * @brief Thread start function pointer
       */
      typedef OMX_ERRORTYPE (*StartFnType) (OMX_PTR pThreadData);

    public:

      /**
       * @brief Constructor (NOTE: does not create the thread!)
       */
      Thread();

      /**
       * @brief Destructor (NOTE: does not destroty the thread!)
       */
      ~Thread();

      /**
       * @brief Starts the thread at the specified function
       *
       * @param pFn Pointer to the thread start function
       * @param pThreadArgs Arguments passed in to the thread (null is valid)
       * @param nPriority Priority of the thread, unique to each platform.
       */
      OMX_ERRORTYPE Start(StartFnType pFn,
          OMX_PTR pThreadArgs,
          OMX_S32 nPriority);

      /**
       * @brief Joins the thread
       *
       * Function will block until the thread exits.
       *
       * @param pThreadResult If not NULL the threads status will be store here
       */
      OMX_ERRORTYPE Join(OMX_ERRORTYPE* pThreadResult);

    private:
      static int ThreadEntry(void* pThreadData);

    private:
      StartFnType m_pFn;
      OMX_S32 m_nPriority;
      void* m_pThread;
      OMX_PTR m_pThreadArgs;
  };
}

#endif // #ifndef _VENC_TEST_THREAD_H
