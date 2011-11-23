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
#include "venctest_File.h"
#include "venc_file.h"

namespace venctest
{
  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  File::File()
    : m_pFile(NULL),
    m_bReadOnly(OMX_TRUE)
  {
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  File::~File()
  {
    if (m_pFile)
      venc_file_close(m_pFile);
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE File::Open(OMX_STRING pFileName,
      OMX_BOOL bReadOnly)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (pFileName != NULL)
    {
      if (m_pFile == NULL)
      {
        int ret;
        m_bReadOnly = bReadOnly;
        if (bReadOnly == OMX_TRUE)
        {
          ret = venc_file_open(&m_pFile, (char*) pFileName, 1);
        }
        else
        {
          ret = venc_file_open(&m_pFile, (char*) pFileName, 0);
        }

        if (ret != 0)
        {
          VENC_TEST_MSG_ERROR("Unable to open file");
          result = OMX_ErrorUndefined;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("File is already open");
        result = OMX_ErrorUndefined;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("Null param");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE File::Read(OMX_U8* pBuffer,
      OMX_S32 nBytes,
      OMX_S32* pBytesRead)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (m_bReadOnly == OMX_TRUE)
    {
      if (pBuffer != NULL)
      {
        if (nBytes > 0)
        {
          if (pBytesRead != NULL)
          {
            *pBytesRead = (OMX_S32) venc_file_read(m_pFile, (void*) pBuffer, (int) nBytes);
          }
          else
          {
            VENC_TEST_MSG_ERROR("Null param");
            result = OMX_ErrorBadParameter;
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("Bytes must be > 0");
          result = OMX_ErrorBadParameter;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("Null param");
        result = OMX_ErrorBadParameter;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("File is open for writing");
      result = OMX_ErrorUndefined;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE File::Write(OMX_U8* pBuffer,
      OMX_U32 nBytes,
      OMX_U32* pBytesWritten)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (m_bReadOnly == OMX_FALSE)
    {
      if (pBuffer != NULL)
      {
        if (nBytes > 0)
        {
          if (pBytesWritten != NULL)
          {
            *pBytesWritten = venc_file_write(m_pFile, (void*) pBuffer, (int) nBytes);
          }
          else
          {
            VENC_TEST_MSG_ERROR("Null param");
            result = OMX_ErrorBadParameter;
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("Bytes must be > 0");
          result = OMX_ErrorBadParameter;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("Null param");
        result = OMX_ErrorBadParameter;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("File is open for reading");
      result = OMX_ErrorUndefined;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE File::SeekStart(OMX_S32 nBytes)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (nBytes >= 0)
    {
      if (venc_file_seek_start(m_pFile, (int) nBytes) != 0)
      {
        VENC_TEST_MSG_ERROR("failed to seek");
        result = OMX_ErrorUndefined;
      }
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE File::Close()
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (m_pFile != NULL)
    {
      venc_file_close(m_pFile);
      m_pFile = NULL;
    }
    else
    {
      VENC_TEST_MSG_ERROR("File was already closed");
      result = OMX_ErrorUndefined;
    }
    return result;
  }
} // namespace venctest
