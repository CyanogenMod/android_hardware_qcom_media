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

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "OMX_VencBufferManager.h"
#include "venc_debug.h"
#include <stdio.h>
#include <string.h>

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Global Data Definitions
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Static Variable Definitions
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Static Function Declarations and Definitions
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Externalized Function Definitions
 * -------------------------------------------------------------------------*/

VencBufferManager::VencBufferManager(OMX_ERRORTYPE* pResult)
   : m_pHead(NULL),
     m_nBuffers(0)
{


  if (pResult == NULL)
  {
    QC_OMX_MSG_ERROR("result is null");
    return;
  }
  *pResult = OMX_ErrorNone;
  (void) pthread_mutex_init(&m_mutex, NULL);
  memset(&m_sFreeNodePool, 0, sizeof(m_sFreeNodePool));
}

VencBufferManager::~VencBufferManager()
{
  (void) pthread_mutex_destroy(&m_mutex);
}

OMX_ERRORTYPE
VencBufferManager::PopBuffer(OMX_BUFFERHEADERTYPE* pBuffer)
{
  Node *pSave = NULL;
  Node *pCurr;
  pCurr = m_pHead;

  if (!pBuffer)
  {
    QC_OMX_MSG_ERROR("null buffer");
    return OMX_ErrorBadParameter;
  }
  else if (m_pHead == NULL)
  {
    QC_OMX_MSG_ERROR("list is empty");
    return OMX_ErrorUndefined;
  }
  else
  {
    pthread_mutex_lock(&m_mutex);
    while (pCurr != NULL &&
        pCurr->pBuffer != pBuffer)
    {
      pSave = pCurr;
      pCurr = pCurr->pNext;
    }

    if (pCurr && pCurr->pBuffer == pBuffer)
    {
      if (pCurr == m_pHead)
      {
        pCurr = pCurr->pNext;
        FreeNode(m_pHead);
        m_pHead = pCurr;
      }
      else
      {
        pSave->pNext = pCurr->pNext;
        FreeNode(pCurr);
      }
      --m_nBuffers;
      pthread_mutex_unlock(&m_mutex);

      return OMX_ErrorNone;
    }
    else
    {
      pthread_mutex_unlock(&m_mutex);
      QC_OMX_MSG_ERROR("error here");
      return OMX_ErrorUndefined;
    }
  }
}

OMX_ERRORTYPE
VencBufferManager::PopFirstBuffer(OMX_BUFFERHEADERTYPE** ppBuffer)
{
  if (!ppBuffer)
  {
    return OMX_ErrorBadParameter;
  }
  else
  {
    pthread_mutex_lock(&m_mutex);
    if (m_pHead)
    {
      OMX_ERRORTYPE result;
      result = PopBuffer(m_pHead->pBuffer);
      *ppBuffer = m_pHead->pBuffer;
      pthread_mutex_unlock(&m_mutex);
      return result;
    }
    pthread_mutex_unlock(&m_mutex);
    QC_OMX_MSG_ERROR("list is empty");
    return OMX_ErrorUndefined;
  }
}

OMX_ERRORTYPE
VencBufferManager::PushBuffer(OMX_BUFFERHEADERTYPE* pBuffer)
{
  Node *pSave;
  Node *pCurr;
  Node *pTemp;

  pthread_mutex_lock(&m_mutex);
  pTemp = AllocNode();
  if (!pTemp)
  {
    pthread_mutex_unlock(&m_mutex);
    QC_OMX_MSG_ERROR("no more buffers to allocate");
    return OMX_ErrorInsufficientResources;
  }

  pTemp->pBuffer = pBuffer;
  pTemp->pNext = NULL;

  if (m_pHead == NULL)
  {
    m_pHead = pTemp;
  }
  else
  {
    pCurr = m_pHead;
    while (pCurr != NULL)
    {
      pSave = pCurr;
      pCurr = pCurr->pNext;
    }
    pSave->pNext = pTemp;
  }
  m_nBuffers++;

  pthread_mutex_unlock(&m_mutex);
  return OMX_ErrorNone;
}

OMX_ERRORTYPE
VencBufferManager::GetNumBuffers(OMX_U32* pnBuffers)
{
  if (!pnBuffers)
    return OMX_ErrorBadParameter;

  *pnBuffers = m_nBuffers;
  return OMX_ErrorNone;
}

VencBufferManager::Node*
VencBufferManager::AllocNode()
{
  for (int i = 0; i < MAX_FREE_BUFFERS; i++)
  {
    if (m_sFreeNodePool[i].pBuffer == NULL)
    {
      return &m_sFreeNodePool[i];
    }
  }
  return NULL;
}

void
VencBufferManager:: FreeNode(Node* pNode)
{
  for (int i = 0; i < MAX_FREE_BUFFERS; i++)
  {
    if (pNode == &m_sFreeNodePool[i])
    {
      pNode->pBuffer = NULL;
      pNode->pNext = NULL;
      return;
    }
  }

  QC_OMX_MSG_ERROR("invalid buffer");  // will never happen by nature of calling function
}
