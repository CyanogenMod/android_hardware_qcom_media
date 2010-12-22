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

#ifndef OMX_VENC_BUFFER_MANAGER_H
#define OMX_VENC_BUFFER_MANAGER_H

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include <pthread.h>
#include "OMX_Core.h"

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 * Class Definitions
 ---------------------------------------------------------------------------*/

/// Buffer manager class
class VencBufferManager
{
  public:
    /// Constructor
    VencBufferManager(OMX_ERRORTYPE* pResult);
    /// Destructor
    ~VencBufferManager();
    /// Pops the specified buffer
    OMX_ERRORTYPE PopBuffer(OMX_BUFFERHEADERTYPE* pBuffer);
    /// Pops the first buffer in the list
    OMX_ERRORTYPE PopFirstBuffer(OMX_BUFFERHEADERTYPE** ppBuffer);
    /// Pushes a buffer in the list
    OMX_ERRORTYPE PushBuffer(OMX_BUFFERHEADERTYPE* pBuffer);
    /// Get the number of buffers in the list
    OMX_ERRORTYPE GetNumBuffers(OMX_U32* pnBuffers);

  private:
    /// List node
    struct Node
    {
      OMX_BUFFERHEADERTYPE* pBuffer;
      Node* pNext;
    };

  private:
    /// Default constructor unallowed
    VencBufferManager() {}

    /// Allocate node from free node pool
    Node* AllocNode();

    /// Put node back in the free node pool
    void FreeNode(Node* pNode);

  private:

    /// head of list
    Node * m_pHead;

    /// number of buffers in list
    int m_nBuffers;

    // max buffer number assumption
    static const int MAX_FREE_BUFFERS = 50;

    /// free node pool
    Node m_sFreeNodePool[MAX_FREE_BUFFERS];

    /// mutex for the list
    pthread_mutex_t m_mutex;
};

#endif // #ifndef OMX_VENC_DEVICE_CALLBACK_H
