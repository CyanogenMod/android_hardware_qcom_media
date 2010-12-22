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

#ifndef _VENC_TEST_STATS_THREAD_H
#define _VENC_TEST_STATS_THREAD_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"

namespace venctest
{

  class Thread;   // forward declaration
  class Signal;   // forward declaration
  class Mutex;    // forward declaration

  /**
   * @brief Collects frame statistics and periodically displays the information
   *
   */
  class StatsThread
  {
    public:

      /**
       * @brief Constructor
       */
      StatsThread(OMX_S32 nSamplePeriodMillis);

      /**
       * @brief Destructor
       */
      ~StatsThread();

    public:

      /**
       * @brief Starts the stats thread
       *
       */
      OMX_ERRORTYPE Start();

      /**
       * @brief Tell stats thread to finish
       *
       * Function will block until the stats thread has exited
       */
      OMX_ERRORTYPE Finish();

      /**
       * @brief Set input frame statistics info
       */
      OMX_ERRORTYPE SetInputStats(OMX_BUFFERHEADERTYPE* pBuffer);

      /**
       * @brief Set output frame statistics info
       */
      OMX_ERRORTYPE SetOutputStats(OMX_BUFFERHEADERTYPE* pBuffer);

    private:
      static int StatsThreadEntry(OMX_PTR pThreadData);
      OMX_ERRORTYPE StatsThreadFn();

    private:
      void *m_pThread;
      void *m_pSignal;
      void *m_pMutex;
      OMX_S32 m_nSamplePeriodMillis;
      OMX_S32 m_nInputFrames;
      OMX_S32 m_nOutputFrames;
      OMX_S64 m_nBits;
  };

} // namespace venctest

#endif // #ifndef _VENC_TEST_STATS_THREAD_H
