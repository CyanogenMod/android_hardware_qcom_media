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

#ifndef __OMX_VDEC_LINUX_H__
#define __OMX_VDEC_LINUX_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Decoder

*//** @file comx_vdec.h
  This module contains the class definition for openMAX decoder component.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

/* Uncomment out below line
#define LOG_NDEBUG 0 if we want to see all LOG Verbose messaging */

#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include "omx_vdec.h"

class omx_vdec_linux:public omx_vdec {
      public:
   omx_vdec_linux();   // constructor
   virtual ~ omx_vdec_linux();   // destructor
   virtual OMX_ERRORTYPE create_msg_thread();
   virtual void post_message(unsigned char id);
   virtual void mutex_lock();
   virtual void mutex_unlock();
   virtual void semaphore_wait();
   virtual void semaphore_post();

   int m_pipe_in;      // for communicating with the message thread
   int m_pipe_out;      // for communicating with the message thread
   pthread_t msg_thread_id;

      private:
    pthread_mutex_t m_lock;
   pthread_mutexattr_t m_lock_attr;
   bool is_thread_created;
   //sem to handle the minimum procesing of commands
   sem_t m_cmd_lock;
};

#endif // __OMX_VDEC_LINUX_H__
