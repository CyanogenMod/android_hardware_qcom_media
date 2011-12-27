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

#ifndef _VENC_TEST_ITEST_CASE_H
#define _VENC_TEST_ITEST_CASE_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"
#include "venctest_ComDef.h"

namespace venctest
{

  class Thread;     // forward declaration

  /**
   * @brief A test case interface
   */
  class ITestCase
  {
    public:

      /**
       * @brief Constructor
       *
       * Must explicitly be called by all derived classes
       */
      ITestCase();

      /**
       * @brief Destructor
       */
      virtual ~ITestCase();

    public:

      /**
       * @brief Start the test asynchronously
       */
      OMX_ERRORTYPE Start(OMX_STRING pConfigFileName,
          OMX_S32 nTestNum);

      /**
       * @brief Block until the test case is finished
       *
       * @return The final test result
       */
      OMX_ERRORTYPE Finish();

    protected:

      /**
       * @brief Checks for an error and sets the final test result.
       *
       * When the test case exection is complete, the first error encountered
       * will be returned.
       *
       * @returns eTestResult
       */
      OMX_ERRORTYPE CheckError(OMX_ERRORTYPE eTestResult);

    private:

      /**
       * @brief The execution function for the test case
       *
       */
      virtual OMX_ERRORTYPE Execute(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig,
          OMX_S32 nTestNum) = 0;

      /**
       * @brief Validates test case specific configuration assumptions.
       *
       * Required to be implemented by each test case. If no assumptions are
       * made, simply return OMX_ErrorNone.
       */
      virtual OMX_ERRORTYPE ValidateAssumptions(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig) = 0;

      /**
       * @brief Thread entry
       *
       * Invokes the child class's Execute method
       */
      static OMX_ERRORTYPE ThreadEntry(OMX_PTR pThreadData);

    private:
      Thread* m_pThread;
      EncoderConfigType m_sConfig;
      DynamicConfigType m_sDynamicConfig;
      OMX_ERRORTYPE m_eTestResult;
      OMX_S32 m_nTestNum;
  };

} // namespace venctest

#endif // #ifndef  _VENC_TEST_ITEST_CASE_H
