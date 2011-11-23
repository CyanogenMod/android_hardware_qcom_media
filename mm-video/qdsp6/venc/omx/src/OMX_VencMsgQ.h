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

#ifndef OMX_VENC_MSG_Q_H
#define OMX_VENC_MSG_Q_H

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OMX_Core.h"
#include "venc_debug.h"
#include <linux/msm_q6venc.h>


/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 * Class Definitions
 ---------------------------------------------------------------------------*/

///@todo document
class VencMsgQ
{
  public:
    /// max size for component thread message queue
    static const int MAX_MSG_QUEUE_SIZE = 50;

    /// Ids for thread messages
    enum MsgIdType
    {
      MSG_ID_EXIT,         ///< Thread exit command
      MSG_ID_STATE_CHANGE, ///< State change command
      MSG_ID_FLUSH,        ///< Flush command
      MSG_ID_PORT_DISABLE, ///< Port disable command
      MSG_ID_PORT_ENABLE,  ///< Port enable command
      MSG_ID_MARK_BUFFER,  ///< Mark buffer command
      MSG_ID_EMPTY_BUFFER, ///< Empty buffer
      MSG_ID_FILL_BUFFER,  ///< Fill buffer
      MSG_ID_DRIVER_MSG    ///< Async driver status msg
    };

    /// Data for thread messages
    union MsgDataType
    {
      OMX_STATETYPE eState; ///< State associated with MSG_ID_STATE_CHANGE
      OMX_U32 nPortIndex;   ///< Port index for MSG_ID_FLUSH,
      ///<                MSG_ID_PORT_ENABLE
      ///<                MSG_ID_PORT_DISABLE

      /// anonymous structure for MSG_ID_MARK_BUFFER
      struct
      {
        OMX_U32 nPortIndex;      ///< Corresponding port for command
        OMX_MARKTYPE sMarkData;  ///< Mark data structure
      } sMarkBuffer;

      OMX_BUFFERHEADERTYPE* pBuffer; ///< For MSG_ID_EMPTY_BUFFER and MSG_ID_FILL_BUFFER

      ///
      struct venc_msg sDriverMsg;
    };

    /// Msgs for component thread
    struct MsgType
    {
      MsgIdType id;     ///< message id
      MsgDataType data; ///< message data
    };

  public:

    VencMsgQ() :
      m_nHead(0),
      m_nSize(0)
  {
    memset(m_aMsgQ, 0, sizeof(MsgType) * MAX_MSG_QUEUE_SIZE);
    (void) pthread_mutex_init(&m_mutex, NULL);
    (void) pthread_cond_init(&m_signal, NULL);
  }

    ~VencMsgQ()
    {
      (void) pthread_mutex_destroy(&m_mutex);
      (void) pthread_cond_destroy(&m_signal);
    }

    OMX_ERRORTYPE PushMsg(MsgIdType eMsgId,
        const MsgDataType* pMsgData)
    {
      QC_OMX_MSG_LOW("pushing msg...", 0, 0, 0);
      pthread_mutex_lock(&m_mutex);
      // find the tail of the queue
      int idx = (m_nHead + m_nSize) % MAX_MSG_QUEUE_SIZE;

      if (m_nSize >= MAX_MSG_QUEUE_SIZE)
      {
        QC_OMX_MSG_ERROR("msg q is full...");
        pthread_mutex_unlock(&m_mutex);
        return OMX_ErrorInsufficientResources;
      }

      // put data at tail of queue
      if (pMsgData != NULL)
      {
        memcpy(&m_aMsgQ[idx].data, pMsgData, sizeof(MsgDataType));
      }
      m_aMsgQ[idx].id = eMsgId;

      // update queue size
      m_nSize++;

      QC_OMX_MSG_LOW("push msg after size: %d", m_nSize, 0, 0);
      // unlock queue
      pthread_cond_signal(&m_signal);
      pthread_mutex_unlock(&m_mutex);
      // signal the component thread
      // pthread_cond_signal(&m_signal);
      QC_OMX_MSG_LOW("push msg done", 0, 0, 0);
      return OMX_ErrorNone;
    }

    OMX_ERRORTYPE PopMsg(MsgType* pMsg)
    {
      // listen for a message...
      QC_OMX_MSG_LOW("waiting for msg", 0, 0, 0);
      pthread_mutex_lock(&m_mutex);

      while (m_nSize == 0)
      {
        pthread_cond_wait(&m_signal, &m_mutex);
      }
      QC_OMX_MSG_LOW("got & copy msg", 0, 0, 0);

      // get msg at head of queue
      memcpy(pMsg, &m_aMsgQ[m_nHead], sizeof(MsgType));

      // pop message off the queue
      m_nHead = (m_nHead + 1) % MAX_MSG_QUEUE_SIZE;
      m_nSize--;
      QC_OMX_MSG_LOW("pop msg after size: %d", m_nSize, 0, 0);
      pthread_mutex_unlock(&m_mutex);
      QC_OMX_MSG_LOW("PopMsg done", 0, 0, 0);
      return OMX_ErrorNone;
    }

  private:

    MsgType m_aMsgQ[MAX_MSG_QUEUE_SIZE];  ///< queue data
    int m_nHead;   ///< head of the queue
    int m_nSize;   ///< size of the queue

    pthread_mutex_t m_mutex;   ///< message q lock
    pthread_cond_t m_signal;  ///< message q data available signal
};
#endif // #ifndef OMX_VENC_MSG_Q_H
