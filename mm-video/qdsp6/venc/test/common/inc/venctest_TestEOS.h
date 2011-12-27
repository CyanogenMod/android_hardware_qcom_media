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

#ifndef _VENC_TEST_EOS_H
#define _VENC_TEST_EOS_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"
#include "venctest_ITestCase.h"
#include "venctest_ComDef.h"

namespace venctest
{
  class Encoder;          // forward declaration
  class Queue;            // forward declaration
  class SignalQueue;      // forward declaration
  class File;             // forward declaration

  /**
   * @brief Test case that encodes 1 frame at a time (serially).
   */
  class TestEOS : public ITestCase
  {
    public:

      /**
       * @brief Constructor
       */
      TestEOS() ;

      /**
       * @brief Destructor
       */
      virtual ~TestEOS();

    private:

      virtual OMX_ERRORTYPE Execute(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig);

      /**
       * @brief Delivers an output buffer and waits for the syntax header
       */
      OMX_ERRORTYPE ProcessSyntaxHeader();

      virtual OMX_ERRORTYPE ValidateAssumptions(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig);

      /**
       * @brief Expects eos complete at start of session before yuv delivery
       *
       * @param bEmptyEOSBuffer If true, eos will be attached to an empty input buffer
       */
      OMX_ERRORTYPE EOSTestSessionStart(OMX_BOOL bEmptyEOSBuffer);

      /**
       * @brief Rapidly sends multiple frames to encoder with last frame indicating eos
       *
       * @param bEmptyEOSBuffer If true, eos will be attached to an empty input buffer
       */
      OMX_ERRORTYPE EOSTestRapidFire(OMX_BOOL bEmptyEOSBuffer);

      /**
       * @brief Ensures that encoder will not deliver eos when it doesn't have any output buffers
       *
       * @param bEmptyEOSBuffer If true, eos will be attached to an empty input buffer
       */
      OMX_ERRORTYPE EOSTestDelayOutput(OMX_BOOL bEmptyEOSBuffer);

      /**
       * @brief Returns the next timestamp.
       *
       * This must be called for every frame that is delivered to the encoder!
       */
      OMX_TICKS NextTimeStamp(OMX_S32 nFramerate);

      static OMX_ERRORTYPE EBD(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

      static OMX_ERRORTYPE FBD(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

    private:
      Encoder* m_pEncoder;
      Queue* m_pInputQueue;
      Queue* m_pOutputQueue;
      SignalQueue* m_pSignalQueue;
      File* m_pSource;
      EncoderConfigType* m_pConfig;
      OMX_TICKS m_nTimeStamp;
  };

} // namespace venctest

#endif // #ifndef _VENC_TEST_FILE_ENCODER_H
