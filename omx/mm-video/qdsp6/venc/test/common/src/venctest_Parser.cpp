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
#include "venctest_Parser.h"
#include "venctest_File.h"
#include "venctest_Debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <cctype>
#include <string.h>

namespace venctest
{

  ParserStrVector::ParserStrVector()
    : count(0)
  {
    clear();
  }

  ParserStrVector::~ParserStrVector()
  {
    clear();
  }

  OMX_S32 ParserStrVector::size() { return count; }

  void ParserStrVector::clear()
  {
    count = 0;
  }

  OMX_STRING ParserStrVector::operator[] (OMX_S32 i)
  {
    if (i < count)
    {
      return pStr[i];
    }
    else
    {
      return (OMX_STRING) NULL;
    }
  }

  void ParserStrVector::push_back(OMX_STRING s)
  {
    if (count < ParserMaxStrings)
    {
      pStr[count++] = s;
    }
  }


  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_S32 Parser::ReadLine(File* pFile,
      OMX_S32 nMaxRead,
      OMX_STRING pBuf)
  {
    OMX_S32 nTotal = 0;   // total number of chars read
    OMX_S32 nRead = 0;    // fread return value
    OMX_S32 numChars = 0;   // number of chars read excluding \r\n
    OMX_S32 keepGoing = 1;

    pFile->Read((OMX_U8*) &pBuf[nTotal], 1, &nRead);
    while (keepGoing &&
        nTotal < nMaxRead &&
        nRead > 0)
    {
      switch (pBuf[nTotal])
      {
        case '\n':
          keepGoing = 0;
        case '\r':
          pBuf[nTotal] = (char) 0;
          break;
        default:
          numChars++;
      }

      nTotal++;
      if (keepGoing)
      {
        pFile->Read((OMX_U8*) &pBuf[nTotal], 1, &nRead);
      }
    }

    // terminate string
    pBuf[numChars] = 0;

    // we have reached the end of the file
    // and nothing was read
    if (nRead <= 0 && nTotal == 0)
    {
      return -1;
    }

    // several cases:
    // 1. end of line with chars read (return n)
    // 2. end of line with no chars read (return 0)
    // 3. end of file with chars read (return n)
    //    next time function is called -1 will be returned
    return numChars;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Parser::StringToLower(OMX_STRING pStr,
      OMX_STRING pLowerStr)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_S32 i, l;
    if (!pStr)
    {
      return OMX_ErrorBadParameter;
    }

    l = (OMX_S32) strlen(pStr);

    for (i = 0; i < l; i++)
    {
      pLowerStr[i] = (char) tolower(pStr[i]);
    }
    pLowerStr[i] = (char) 0;

    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_S32 Parser::StringICompare(OMX_STRING pStr1,
      OMX_STRING pStr2)
  {
    OMX_S32 result = 0;
    OMX_S32 strl1,strl2;

    strl1 = (OMX_S32) strlen(pStr1);
    strl2 = (OMX_S32) strlen(pStr2);

    if (strl1 != strl2)
    {
      result = 1; // not equal if lenghts are different
    }
    else if (strl1 == 0 && strl2 == 0)
    {
      result = 0; // equal if empty string
    }
    else
    {

      OMX_STRING pLower1 = (OMX_STRING) malloc(strl1+1);
      OMX_STRING pLower2 = (OMX_STRING) malloc(strl2+1);

      memcpy(pLower1, pStr1, strl1+1);
      memcpy(pLower2, pStr2, strl2+1);

      StringToLower(pStr1, pLower1);
      StringToLower(pStr2, pLower2);

      result = (OMX_S32) strcmp(pLower1, pLower2);

      free(pLower1);
      free(pLower2);
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Parser::RemoveComments(OMX_STRING pStr)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    OMX_STRING p;
    if (!pStr)
    {
      return OMX_ErrorBadParameter;
    }
    if ((p = StrChrs(pStr, (OMX_STRING)"#")) != NULL)
    {
      *p = 0;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_S32 Parser::TokenizeString(ParserStrVector* pTokens,
      OMX_STRING pStr,
      OMX_STRING pDelims)
  {
    char* pOut;
    char* pIn = pStr;

    if (!pTokens || !(pIn = Trim(pIn)) || !pDelims)
    {
      return 0;
    }

    if (*pIn)
    {
      pTokens->push_back(pIn);
      while (*pIn && (pOut = StrChrs(pIn, pDelims)))
      {
        if (*pIn == '"' ||
            *pIn == '\'')
        {
          // ignore delimeters in quotes
          char* pQuote = pIn;
          int len = (int) strlen(pIn);
          for (int i = 0; i < len; i++)
          {
            ++pIn;
            if (*pQuote == *pIn)
            {
              *pIn = (char) 0;
              ++pIn;
              break;
            }
          }
        }
        else
        {
          // advance past the last delimeter
          for (int i = 0; i < (int) strlen(pDelims); i++)
          {
            if (*pOut && *pOut == pDelims[i])
            {
              *pOut = 0;
              pOut++;
              i = -1;
            }
          }

          if (strlen(pOut))
          {
            if (*pOut == '\'' || *pOut == '"')
            {
              // remove the quotation mark
              pTokens->push_back(pOut+1);
            }
            else
            {
              pTokens->push_back(pOut);
            }
          }
          pIn = pOut;
        }
      }

      return (OMX_S32) pTokens->size();
    }
    return 0;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_STRING Parser::Trim(OMX_STRING pStr)
  {
    OMX_STRING pEnd;
    OMX_STRING pStart;
    OMX_STRING p;

    if (!pStr)
    {
      return 0;
    }

    pStart = pStr;
    pEnd = pStr + strlen(pStr) - 1;

    // remove leading whitespace
    while (*pStart && (*pStart == ' ' || *pStart == '\t'))
    {
      pStart++;
    }

    // remove trailing whitespace
    p = pEnd;
    while ((*p) && (*p == ' ' || *p == '\t'))
    {
      if (p == pStart)
      {
        break;
      }
      *p = 0;
      p--;
    }

    return pStart;
  }

  /////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  OMX_STRING Parser::StrChrs(OMX_STRING pStr,
      OMX_STRING pDelims)
  {
    OMX_S32 nDelim = (OMX_S32) strlen(pDelims);
    OMX_S32 len = (OMX_S32) strlen(pStr);
    OMX_STRING ret = 0;
    OMX_S32 i = 0, j = 0;

    if (len == 0 || nDelim == 0)
      return ret;

    for (i = 0; i < len; i++)
    {
      for (j = 0; j < nDelim; j++)
      {
        if (pDelims[j] == pStr[i])
        {
          ret = &pStr[i];
          return ret;
        }
      }
    }

    return ret;
  }

  OMX_ERRORTYPE Parser::AppendTestNum(OMX_STRING pFileName,
      OMX_S32 nTestNum)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;


    if (pFileName != NULL)
    {
      static const OMX_S32 nMaxExtnLen = 64;

      OMX_S32 nMaxFileNameLen = VENC_TEST_MAX_STRING;
      OMX_STRING pInsert = StrChrs(pFileName, (OMX_STRING) ".");

      // ensure the extension is less than 64 chars
      if (pInsert != NULL && strlen(pInsert) >= nMaxExtnLen - 1)
      {
        // if the file extension is too big then just ignore it
        // and advance to the end of the string
        pInsert = NULL;
      }

      if (pInsert != NULL)
      {
        char pExtnCopy[nMaxExtnLen];

        // make a copy of the file extension
        memcpy(pExtnCopy, (char*) pInsert, strlen((char*) pInsert) + 1);

        nMaxFileNameLen -= ((OMX_S32) pInsert) - ((OMX_S32) pFileName);

        // append the number and the file extension
        sprintf((char* ) pInsert, "_%05d%s",
            (int) nTestNum, (char*) pExtnCopy);
      }
      else
      {
        // we have no file extension so just append the number
        pInsert = pFileName + strlen((char*) pFileName);

        nMaxFileNameLen -= ((OMX_S32) pInsert) - ((OMX_S32) pFileName);

        sprintf((char* ) pInsert, "_%05d", (int) nTestNum);
      }
    }
    else
    {
      result = OMX_ErrorBadParameter;
    }

    return result;
  }

} ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// namespace venctest
