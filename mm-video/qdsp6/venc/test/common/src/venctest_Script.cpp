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
#include "venctest_Script.h"
#include "venctest_File.h"
#include "venctest_Parser.h"
#include "venctest_ComDef.h"
#include <stdio.h>
#include <stdlib.h>

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Script::Script()
    : m_pFile(new File)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Script::~Script()
  {
    if (m_pFile)
    {
      delete m_pFile;
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Script::Configure(OMX_STRING pFileName)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (pFileName != NULL)
    {
      if (m_pFile)
      {
        result = m_pFile->Open(pFileName, OMX_TRUE);

        if (result != OMX_ErrorNone)
        {
          VENC_TEST_MSG_ERROR("failed to open file");
        }
      }
      else
      {
        result = OMX_ErrorUndefined;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("null param");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Script::NextTest(TestDescriptionType* pTestDescription)
  {
    static const OMX_S32 maxFieldName = 64;
    static const OMX_S32 maxLineLen = maxFieldName * 2;

    OMX_ERRORTYPE result = OMX_ErrorNone;

    ParserStrVector v;
    OMX_S32 nLine = 0;

    while (result != OMX_ErrorNoMore)
    {
      char pBuf[maxLineLen];
      char* pTrimmed;
      OMX_S32 nChars = Parser::ReadLine(m_pFile, maxLineLen, pBuf);
      v.clear();
      if (nChars > 0)
      {
        (void) Parser::RemoveComments(pBuf);
        pTrimmed = Parser::Trim(pBuf);

        // No empty lines
        if (strlen(pTrimmed) != 0)
        {
          (void) Parser::TokenizeString(&v, pTrimmed, (OMX_STRING)"\t ");
          if (v.size() == 3)
          {
            break;
          }
          else
          {
            VENC_TEST_MSG_ERROR("badly formatted script file. %ld tokens on line %ld", v.size(), nLine);
            result = OMX_ErrorUndefined;
          }
        }
      }
      else if (nChars < 0)
      {
        result = OMX_ErrorNoMore;
      }
      ++nLine;
    }

    if (result == OMX_ErrorNone)
    {
      memcpy(pTestDescription->cTestName, v[0], strlen(v[0])+1);

      memcpy(pTestDescription->cConfigFile, v[1], strlen(v[1])+1);

      pTestDescription->nSession = (OMX_S32) atoi(v[2]);
    }

    return result;
  }

} // namespace venctest
