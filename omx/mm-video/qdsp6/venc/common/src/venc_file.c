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
#include "venc_file.h"
#include "venc_debug.h"
#include <stdio.h>

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_file_open(void** handle,
                   char* filename,
                   int readonly)
{
  int result = 0;
  FILE* f = NULL;

  if (handle != NULL && filename != NULL && (readonly == 0 || readonly == 1))
  {
    if (readonly == 1)
    {
      f = fopen(filename, "rb");
    }
    else
    {
      f = fopen(filename, "wb");
    }

    if (f == NULL)
    {
      VENC_MSG_ERROR("Unable to open file");
      result = 1;
    }

    *handle = f;
  }
  else
  {
    VENC_MSG_ERROR("bad param");
    result = 1;
  }
  return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_file_close(void* handle)
{
  int result = 0;

  if (handle != NULL)
  {
    fclose((FILE*) handle);
  }
  else
  {
    VENC_MSG_ERROR("handle is null");
    result = 1;
  }
  return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_file_read(void* handle,
                   void* buffer,
                   int bytes)
{
  int result = 0;

  if (buffer != NULL)
  {
    if (bytes > 0)
    {
      result = (int) fread(buffer, 1, bytes, (FILE*) handle);
    }
    else
    {
      VENC_MSG_ERROR("Bytes must be > 0");
      result = -1;
    }
  }
  else
  {
    VENC_MSG_ERROR("Null param");
    result = -1;
  }

  return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_file_write(void* handle,
                    void* buffer,
                    int bytes)
{
  int result = 0;

  if (buffer != NULL)
  {
    if (bytes > 0)
    {
      result = (int) fwrite(buffer, 1, bytes, (FILE*) handle);
      /* fflush((FILE *)handle); */
    }
    else
    {
      VENC_MSG_ERROR("Bytes must be > 0");
      result = -1;
    }
  }
  else
  {
    VENC_MSG_ERROR("Null param");
    result = -1;
  }

  return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_file_seek_start(void* handle,
                         int bytes)
{
  int result = 0;

  if (bytes >= 0)
  {
    if (fseek((FILE*) handle, bytes, SEEK_SET) != 0)
    {
      VENC_MSG_ERROR("failed to seek");
      result = 1;
    }
  }

  return result;
}
