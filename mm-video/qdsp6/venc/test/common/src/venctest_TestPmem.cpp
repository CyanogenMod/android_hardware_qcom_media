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
#include "venctest_TestPmem.h"
#include "venctest_Time.h"
#include "venctest_FileSource.h"
#include "venctest_FileSink.h"
#include "venctest_Encoder.h"

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestPmem::TestPmem()
    : ITestCase(),    // invoke the base class constructor
    m_pEncoder(NULL)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  TestPmem::~TestPmem()
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestPmem::ValidateAssumptions(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestPmem::Execute(EncoderConfigType* pConfig,
      DynamicConfigType* pDynamicConfig)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    //==========================================
    // Create and configure the encoder
    if (result == OMX_ErrorNone)
    {
      VENC_TEST_MSG_HIGH("Creating encoder...");
      m_pEncoder = new Encoder(EBD,
          FBD,
          this, // set the test case object as the callback app data
          pConfig->eCodec);
      result = m_pEncoder->Configure(pConfig);
    }

    if (result == OMX_ErrorNone)
    {
      for (OMX_S32 i = 0; i < 2; i++)
      {
        //==========================================
        // Allocate or UseBuffer model?
        if (result == OMX_ErrorNone)
        {
          result = m_pEncoder->EnableUseBufferModel(i % 2 == 0 ? OMX_FALSE : OMX_TRUE, i % 2 == 0 ? OMX_FALSE : OMX_TRUE);
        }

        //==========================================
        // Go to executing state (also allocate buffers)
        if (result == OMX_ErrorNone)
        {
          VENC_TEST_MSG_HIGH("Go to executing state...");
          result = m_pEncoder->GoToExecutingState();

          if (result == OMX_ErrorNone)
          {
            //==========================================
            // Get the allocated input buffers
            OMX_BUFFERHEADERTYPE** ppInputBuffers;
            ppInputBuffers = m_pEncoder->GetBuffers(OMX_TRUE);
            for (int i = 0; i < pConfig->nInBufferCount; i++)
            {
              if (ppInputBuffers[i]->pBuffer == NULL ||
                  ppInputBuffers[i]->nAllocLen == 0)
              {
                result = OMX_ErrorUndefined;
                break;
              }
            }


            if (result = OMX_ErrorNone)
            {
              //==========================================
              // Get the allocated output buffers
              OMX_BUFFERHEADERTYPE** ppOutputBuffers;
              ppOutputBuffers = m_pEncoder->GetBuffers(OMX_FALSE);
              for (int i = 0; i < pConfig->nOutBufferCount; i++)
              {
                if (ppOutputBuffers[i]->pBuffer == NULL ||
                    ppOutputBuffers[i]->nAllocLen == 0)
                {
                  result = OMX_ErrorUndefined;
                  break;
                }
              }
            }
          }
        }

        //==========================================
        // Tear down the encoder (also deallocate buffers)
        VENC_TEST_MSG_HIGH("Go to loaded state...");
        result = m_pEncoder->GoToLoadedState();

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_HIGH("failed to go to loaded state");
          break;
        }

      } // end of loop
    }

    //==========================================
    // Free our helper classes
    if (m_pEncoder)
      delete m_pEncoder;

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestPmem::EBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return OMX_ErrorNone;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestPmem::FBD(OMX_IN OMX_HANDLETYPE hComponent,
      OMX_IN OMX_PTR pAppData,
      OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
  {
    return OMX_ErrorNone;
  }

} // namespace venctest
