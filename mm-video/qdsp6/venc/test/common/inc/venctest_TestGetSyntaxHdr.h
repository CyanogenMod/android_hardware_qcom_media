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

#ifndef _VENC_TEST_GET_SYNTAX_HDR
#define _VENC_TEST_GET_SYNTAX_HDR

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"
#include "venctest_ITestCase.h"

namespace venctest
{
  class Encoder;          // forward declaration
  class SignalQueue;      // forward declaration

  /**
   * @brief Test case for in-band and out-of-band syntax header.
   */
  class TestGetSyntaxHdr : public ITestCase
  {
    public:

      /**
       * @brief Constructor
       */
      TestGetSyntaxHdr() ;

      /**
       * @brief Destructor
       */
      virtual ~TestGetSyntaxHdr();

    private:

      virtual OMX_ERRORTYPE Execute(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig,
          OMX_S32 nTestNum);

      virtual OMX_ERRORTYPE ValidateAssumptions(EncoderConfigType* pConfig,
          DynamicConfigType* pDynamicConfig);

      OMX_ERRORTYPE GetInBandSyntaxHeader(OMX_BUFFERHEADERTYPE* pBuffer);

      static OMX_ERRORTYPE EBD(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

      static OMX_ERRORTYPE FBD(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

    private:
      Encoder* m_pEncoder;
      SignalQueue* m_pSignalQueue;
  };

} // namespace venctest

#endif // #ifndef _VENC_TEST_GET_SYNTAX_HDR
