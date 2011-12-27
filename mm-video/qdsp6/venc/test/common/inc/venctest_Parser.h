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

#ifndef _VENC_TEST_PARSER_H
#define _VENC_TEST_PARSER_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"

namespace venctest
{
  class ParserStrVector
  {
    public:
      ParserStrVector();

      ~ParserStrVector();

      OMX_S32 size();

      void clear();

      OMX_STRING operator[] (OMX_S32 i);

      void push_back(OMX_STRING s);

    private:
      static const int ParserMaxStrings = 256;
      OMX_STRING pStr[ParserMaxStrings];
      OMX_S32 count;
  };

  class File; // forward declaration

  /**
   * @brief String and file parser utility class
   */
  class Parser
  {
    public:

      /**
       * @brief Reads a line from the specified file
       *
       * @param pFile The file to read from. The file must be opened in read mode.
       * @param nMaxRead Maximium number of bytes to read (size of buffer).
       * @param pBuffer The buffer to read into
       *
       * @return Number of chars in the line
       */
      static OMX_S32 ReadLine(File* pFile,
          OMX_S32 nMaxRead,
          OMX_STRING pBuf);

      /**
       * @brief Converts a string to lowercase
       *
       * @param pStr The buffer containing input text
       * @param pLowerStr The pre-allocated buffer to store lowercase text into
       */
      static OMX_ERRORTYPE StringToLower(OMX_STRING pStr,
          OMX_STRING pLowerStr);

      /**
       * @brief Compares to strings case insensitive
       *
       * @param pStr1 The first string to compare
       * @param pStr2 The second string to compare
       *
       * @return 0 if equal
       */
      static OMX_S32 StringICompare(OMX_STRING pStr1,
          OMX_STRING pStr2);

      /**
       * @brief Removes '#' style comments from the specified string thru null termination
       *
       * NOTE: Modifies the specified string.
       *
       * @param pStr The string to remove comments from
       */
      static OMX_ERRORTYPE RemoveComments(OMX_STRING pStr);

      /**
       * @brief Tokenizes the string according to the specified delimiters
       *
       * NOTE: Modifies the specified string.
       *
       * @param pTokens
       * @param pStr
       * @param pDelims
       */
      static OMX_S32 TokenizeString(ParserStrVector* pTokens,
          OMX_STRING pStr,
          OMX_STRING pDelims);

      /**
       * @brief Removes all leading and trailing whitespace from the string
       *
       * NOTE: Modifies the specified string.
       *
       * @param pStr The string to trim
       *
       * @return Repositioned pointer of the the specified string
       */
      static OMX_STRING Trim(OMX_STRING pStr);

      /**
       * @brief Returns a pointer to the first instance of the specified delimiters
       *
       * @param pStr The string to search
       * @param pDelims The string of delimiters to search for
       *
       * @return Repositioned pointer of the the specified string
       */
      static OMX_STRING StrChrs(OMX_STRING pStr,
          OMX_STRING pDelims);

      /**
       * @brief Appends test number to a file name. Useful for bitexactness.
       *
       * This method will modify the contents of pFileName.
       *
       * Example 1: If pFileName = "test.m4v" and nTestNum is 2, then the
       * result would be test_00002.m4v.
       *
       * Example 2: If pFileName = "test" and nTestNum is 2, then the
       * result would be test_00002.
       *
       * @param pFileName The original name of the file.
       * @param nTestNum The test number
       *
       */
      static OMX_ERRORTYPE AppendTestNum(OMX_STRING pFileName,
          OMX_S32 nTestNum);
    private:
      Parser() {}
      ~Parser() {}
  };
}

#endif // #ifndef _VENC_TEST_PARSER_H
