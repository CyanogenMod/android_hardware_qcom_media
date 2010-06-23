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
#include <pthread.h>
#include "venc_debug.h"
#include "venc_thread.h"
#include <stdlib.h>

typedef struct venc_thread_type
{
  thread_fn_type pfn;
  int priority;
  pthread_t thread;
  void* thread_data;
} venc_thread_type;


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
static void* venc_thread_entry(void* thread_data)
{
  venc_thread_type* thread = (venc_thread_type*) thread_data;
  return (void*) thread->pfn(thread->thread_data);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_thread_create(void** handle,
                       thread_fn_type pfn,
                       void* thread_data,
                       int priority)
{
  int result = 0;
  venc_thread_type* thread = (venc_thread_type*) malloc(sizeof(venc_thread_type));
  *handle = thread;

  if (thread)
  {
    int create_result;
    thread->pfn = pfn;
    thread->thread_data = thread_data;
    thread->priority = priority;
    create_result = pthread_create(&thread->thread,
        NULL,
        venc_thread_entry,
        (void*) thread);
    if (create_result != 0)
    {
      VENC_MSG_ERROR("failed to create thread");
      result = 1;
    }
  }
  else
  {
    VENC_MSG_ERROR("failed to allocate thread");
    result = 1;
  }
  return result;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int venc_thread_destroy(void* handle, int* thread_result)
{
  int result = 0;
  venc_thread_type* thread = (venc_thread_type*) handle;

  if (thread)
  {
    if (pthread_join(thread->thread, (void**) thread_result) != 0)
    {
      VENC_MSG_ERROR("failed to join thread");
      result = 1;
    }
    free(thread);
  }
  else
  {
    VENC_MSG_ERROR("handle is null");
    result = 1;
  }
  return result;
}
