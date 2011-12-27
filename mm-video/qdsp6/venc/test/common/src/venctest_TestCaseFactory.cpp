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
#include "venctest_TestCaseFactory.h"
#include "venctest_ITestCase.h"
#include "venctest_Thread.h"
#include "venctest_Debug.h"
#include "venctest_Parser.h"

// test case object headers
#include "venctest_TestChangeIntraPeriod.h"
#include "venctest_TestChangeQuality.h"
// #include "venctest_TestEOS.h"
#include "venctest_TestGetSyntaxHdr.h"
#include "venctest_TestIFrameRequest.h"
// #include "venctest_TestPmem.h"
// #include "venctest_TestProfileEncode.h"
#include "venctest_TestEncode.h"
#include "venctest_TestStateExecutingToIdle.h"
#include "venctest_TestStatePause.h"
#include "venctest_TestFlush.h"

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  ITestCase* TestCaseFactory::AllocTest(OMX_STRING pTestName)
  {
    ITestCase* pTest = NULL;

    if (Parser::StringICompare(pTestName, (OMX_STRING)"CHANGE_INTRA_PERIOD") == 0)
    {
      pTest = new TestChangeIntraPeriod;
    }
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"CHANGE_QUALITY") == 0)
    {
      pTest = new TestChangeQuality;
    }
    /* else if (Parser::StringICompare(pTestName, (OMX_STRING)"EOS") == 0)
       {
       pTest = new TestEOS;
       } */
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"GET_SYNTAX_HDR") == 0)
    {
      pTest = new TestGetSyntaxHdr;
    }
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"IFRAME_REQUEST") == 0)
    {
      pTest = new TestIFrameRequest;
    }
    /* else if (Parser::StringICompare(pTestName, (OMX_STRING)"PMEM") == 0)
       {
       pTest = new TestPmem;
       }
       else if (Parser::StringICompare(pTestName, (OMX_STRING)"PROFILE_ENCODE") == 0)
       {
       pTest = new TestProfileEncode;
       } */
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"ENCODE") == 0)
    {
      pTest = new TestEncode;
    }
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"STATE_EXECUTING_TO_IDLE") == 0)
    {
      pTest = new TestStateExecutingToIdle;
    }
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"FLUSH") == 0)
    {
      pTest = new TestFlush;
    }
    else if (Parser::StringICompare(pTestName, (OMX_STRING)"STATE_PAUSE") == 0)
    {
      pTest = new TestStatePause;
    }
    else
    {
      VENC_TEST_MSG_ERROR("invalid test name");
    }

    return pTest;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE TestCaseFactory::DestroyTest(ITestCase* pTest)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (pTest)
    {
      delete pTest;
    }
    else
    {
      VENC_TEST_MSG_ERROR("NULL param");
      result = OMX_ErrorBadParameter;
    }

    return result;
  }

} // namespace venctest
