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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "venctest_Script.h"
#include "venctest_Debug.h"
#include "venctest_ComDef.h"
#include "venctest_ITestCase.h"
#include "venctest_TestCaseFactory.h"

void RunScript(OMX_STRING pScriptFile)
{
  OMX_S32 nPass = 0;
  OMX_S32 nFail = 0;
  OMX_S32 nTestCase = 0;
  OMX_ERRORTYPE result = OMX_ErrorNone;


  printf("Start in RunScript \n");

  venctest::Script script;
  script.Configure(pScriptFile);

  printf("Done config script file \n");

  if (result == OMX_ErrorNone)
  {
    venctest::TestDescriptionType testDescription;
    do
    {
      result = script.NextTest(&testDescription);

      printf("Finish NextTest with name %s\n", testDescription.cTestName);
      if (result == OMX_ErrorNone)
      {
        for (OMX_S32 i = 0; i < testDescription.nSession; i++)
        {
          venctest::ITestCase* pTest =
            venctest::TestCaseFactory::AllocTest((OMX_STRING) testDescription.cTestName);

          printf("Before Running test \n");
          if (pTest != NULL)
          {
            VENC_TEST_MSG_HIGH("Running test %ld", nPass + nFail);
            result = pTest->Start(testDescription.cConfigFile, i);

            printf("After running test \n");
            if (result == OMX_ErrorNone)
            {
              result = pTest->Finish();
              if (result == OMX_ErrorNone)
              {
                ++nPass;
              }
              else
              {
                VENC_TEST_MSG_ERROR("test %ld failed", nPass + nFail);
                ++nFail;
              }
            }
            else
            {
              VENC_TEST_MSG_ERROR("error starting test");
              ++nFail;
            }

            (void) venctest::TestCaseFactory::DestroyTest(pTest);
          }
          else
          {
            VENC_TEST_MSG_ERROR("unable to alloc test");
          }

        }
      }
      else if (result != OMX_ErrorNoMore)
      {
        VENC_TEST_MSG_ERROR("error parsing script");
      }

    } while (result != OMX_ErrorNoMore);
  }

  VENC_TEST_MSG_HIGH("passed %ld out of %ld tests", nPass, nPass + nFail);

}

void RunTest(OMX_STRING pTestName,
             OMX_STRING pConfigFile,
             OMX_S32 nSession)
{
  OMX_S32 nPass = 0;
  OMX_S32 nFail = 0;
  OMX_ERRORTYPE result = OMX_ErrorNone;

  for (OMX_S32 i = 0; i < nSession; i++)
  {
    venctest::ITestCase* pTest =
      venctest::TestCaseFactory::AllocTest((OMX_STRING) pTestName);

    if (pTest != NULL)
    {
      VENC_TEST_MSG_HIGH("Running test %ld", nPass + nFail);
      result = pTest->Start(pConfigFile, i);

      if (result == OMX_ErrorNone)
      {
        result = pTest->Finish();
        if (result == OMX_ErrorNone)
        {
          ++nPass;
        }
        else
        {
          ++nFail;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("error starting test");
      }

      (void) venctest::TestCaseFactory::DestroyTest(pTest);
    }
    else
    {
      VENC_TEST_MSG_ERROR("unable to alloc test");
    }
  }

  VENC_TEST_MSG_HIGH("passed %ld out of %ld tests", nPass, nPass + nFail);
}

int main(int argc, char* argv[])
{
  OMX_Init();

  if (argc == 2)
  {
    OMX_STRING pScriptFile = (OMX_STRING) argv[1];

    RunScript(pScriptFile);
  }
  else if (argc == 4)
  {
    OMX_STRING pTestName = (OMX_STRING) argv[1];
    OMX_STRING pConfigFile = (OMX_STRING) argv[2];
    OMX_STRING pNumSession = (OMX_STRING) argv[3];
    OMX_S32 nSession;

    nSession = atoi((char*) pNumSession);

    RunTest(pTestName, pConfigFile, nSession);
  }
  else
  {
    VENC_TEST_MSG_ERROR("invalid number of command args %d", argc);
    VENC_TEST_MSG_ERROR("./mm-venc-omx-test ENCODE Config.cfg 1");
  }

  OMX_Deinit();
}
