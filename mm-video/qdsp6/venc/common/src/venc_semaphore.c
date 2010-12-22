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
#include "venc_semaphore.h"
#include "venc_debug.h"
#include <semaphore.h>
#include <time.h>
#include <errno.h>

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_semaphore_create(void** handle, int init_count, int max_count)
{
  int result = 0;

  (void) max_count;

  if (handle)
  {
    sem_t* sem = (sem_t*) malloc(sizeof(sem_t));

    if (sem)
    {
      if (sem_init(sem, 0, 0) == 0)
      {
        *handle = (void*) sem;
      }
      else
      {
        VENC_MSG_ERROR("failed to create semaphore");
        free((void*) sem);
        result = 1;
      }
    }
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
int venc_semaphore_destroy(void* handle)
{
  int result = 0;

  if (handle)
  {
    if (sem_destroy((sem_t*) handle) == 0)
    {
      free(handle);
    }
    else
    {
      VENC_MSG_ERROR("failed to destroy sem %s", strerror(errno));
      result = 1;
    }
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
int venc_semaphore_wait(void* handle, int timeout)
{
  int result = 0;

  if (handle)
  {
    if (timeout > 0)
    {
      struct timespec ts;
      if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
      {
        int ret;
        // get the number of seconds in timeout (from millis)
        ts.tv_sec = ts.tv_sec + (time_t) (timeout / 1000);

        // get the number of nanoseconds left over
        ts.tv_nsec = ts.tv_nsec + (long) ((timeout % 1000) * 1000);

        if (sem_timedwait((sem_t*) handle, &ts) != 0)
        {
          if (errno == ETIMEDOUT)
          {
            result = 2;
          }
          else
          {
            VENC_MSG_ERROR("error waiting for sem");
            result = 1;
          }
        }
      }

    }
    else // no timeout
    {
      if (sem_wait((sem_t*) handle) != 0)
      {
        VENC_MSG_ERROR("error waiting for sem");
        result = 1;
      }
    }
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
int venc_semaphore_post(void* handle)
{
  int result = 0;

  if (handle)
  {
    if (sem_post((sem_t*) handle) != 0)
    {
      VENC_MSG_ERROR("Failed to post semaphore");
      result = 1;
    }
  }
  else
  {
    VENC_MSG_ERROR("handle is null");
    result = 1;
  }

  return result;
}
