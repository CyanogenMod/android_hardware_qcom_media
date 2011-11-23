/*--------------------------------------------------------------------------
Copyright (c) 2009, Code Aurora Forum. All rights reserved.

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
/*============================================================================
                            O p e n M A X   w r a p p e r s
                             O p e n  M A X   C o r e

*//** @file omx_vdec.c
  This module contains the implementation of the OpenMAX core & component.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include "omx_vdec_linux.h"
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

void *get_omx_component_factory_fn(void)
{
   return (new omx_vdec_linux);
}

void *message_thread(void *input)
{
   unsigned char id;
   int n;
   omx_vdec_linux *omx = reinterpret_cast < omx_vdec_linux * >(input);
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "omx_vdec: message thread start\n");
   while (1) {
      n = read(omx->m_pipe_in, &id, 1);
      if (0 == n)
         break;
      if (1 == n) {
         omx->process_event_cb(&(omx->m_vdec_cfg), id);
      }
#ifdef QLE_BUILD
      if (n < 0)
         break;
#else
      if ((n < 0) && (errno != EINTR))
         break;
#endif
   }
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
           "omx_vdec: message thread stop\n");
   return 0;
}

omx_vdec_linux::omx_vdec_linux()
{
   m_pipe_in = -1;
   m_pipe_out = -1;
   is_thread_created = false;
   pthread_mutexattr_init(&m_lock_attr);
   pthread_mutex_init(&m_lock, &m_lock_attr);
   sem_init(&m_cmd_lock, 0, 0);
}

omx_vdec_linux::~omx_vdec_linux()
{
   if (m_pipe_in) {
      close(m_pipe_in);
      m_pipe_in = -1;
   }

   if (m_pipe_out) {
      close(m_pipe_out);
      m_pipe_out = -1;
   }
   if (is_thread_created) {
      pthread_join(msg_thread_id,NULL);
      is_thread_created = false;
   }

   pthread_mutexattr_destroy(&m_lock_attr);
   pthread_mutex_destroy(&m_lock);
   sem_destroy(&m_cmd_lock);
}

OMX_ERRORTYPE omx_vdec_linux::create_msg_thread()
{
   int fds[2];
   OMX_ERRORTYPE eRet = OMX_ErrorNone;
   if (fds == NULL || pipe(fds)) {
      //QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,"pipe creation failed\n");
      eRet = OMX_ErrorInsufficientResources;
   } else {
      m_pipe_in = fds[0];
      m_pipe_out = fds[1];
      if (fcntl(m_pipe_out, F_SETFL, O_NONBLOCK) == -1) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "Error - Set pipe out to NONBLOCK is failed\n");
      }
      if (pthread_create(&msg_thread_id, 0, message_thread, this) < 0)
         eRet = OMX_ErrorInsufficientResources;
   }
   if (eRet == OMX_ErrorNone)
	   is_thread_created = true;
   return eRet;
}

void omx_vdec_linux::post_message(unsigned char id)
{
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "omx_vdec: post_message %d\n", id);
   int eRet = 0;

   eRet = write(m_pipe_out, &id, 1);

   if (eRet == -1) {
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "errno %d - %s\n", errno, strerror(errno));
   }

}

void omx_vdec_linux::mutex_lock()
{
   pthread_mutex_lock(&m_lock);
}

void omx_vdec_linux::mutex_unlock()
{
   pthread_mutex_unlock(&m_lock);
}

void omx_vdec_linux::semaphore_wait()
{
   sem_wait(&m_cmd_lock);
}

void omx_vdec_linux::semaphore_post()
{
   sem_post(&m_cmd_lock);
}
