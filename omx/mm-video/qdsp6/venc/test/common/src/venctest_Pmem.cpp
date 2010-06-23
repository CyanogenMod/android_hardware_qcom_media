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
#include "venctest_Pmem.h"
#include "venctest_Debug.h"

#include <string.h>

// pmem include files
/*
extern "C"
{
   #include "pmem.h"
}
*/

namespace venctest
{

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Pmem::Pmem(OMX_S32 nBuffers)
    : m_nBuffers(nBuffers),
    m_nBuffersAlloc(0),
    m_pBufferInfo(new venc_pmem[nBuffers])
  {
    if (m_pBufferInfo != NULL)
    {
      memset(m_pBufferInfo, 0, sizeof(struct venc_pmem) * nBuffers);
    }
    // do nothing
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  Pmem::~Pmem()
  {
    if (m_pBufferInfo != NULL)
    {
      delete [] m_pBufferInfo;
    }
  }

  OMX_ERRORTYPE Pmem::pmem_alloc(struct venc_pmem *pBuf, int size, int pmem_region_id)
  {
    struct pmem_region region;

    QC_OMX_MSG_HIGH("Opening pmem files with size 0x%x...",size,0,0);

    if (pmem_region_id == VENC_PMEM_EBI1)
	pBuf->fd = open("/dev/pmem_adsp", O_RDWR);
    else if (pmem_region_id == VENC_PMEM_SMI)
	pBuf->fd = open("/dev/pmem_smipool", O_RDWR);
    else {
	QC_OMX_MSG_ERROR("Pmem region id not supported \n", pmem_region_id);
	return OMX_ErrorBadParameter;
    }

    if (pBuf->fd < 0) {
      QC_OMX_MSG_ERROR("error could not open pmem device");
      return OMX_ErrorInsufficientResources;
    }

    pBuf->offset = 0;
    pBuf->size = (size + 4095) & (~4095);

    /* QC_OMX_MSG_HIGH("Allocate pmem of size:0x%x, fd:%d \n", pBuf->size, pBuf->fd, 0); */
    pBuf->virt = mmap(NULL, pBuf->size, PROT_READ | PROT_WRITE,
        MAP_SHARED, pBuf->fd, 0);

    QC_OMX_MSG_HIGH("Allocate pmem of size:0x%x, fd:%d  virt:%p\n", pBuf->size, pBuf->fd, pBuf->virt);
    if (pBuf->virt == MAP_FAILED) {
      QC_OMX_MSG_ERROR("error mmap failed with size:%d",size);
      close(pBuf->fd);
      pBuf->fd = -1;
      return OMX_ErrorInsufficientResources;
    }

    return OMX_ErrorNone;
  }

  OMX_ERRORTYPE Pmem::pmem_free(struct venc_pmem* pBuf)
  {
    QC_OMX_MSG_HIGH("Free pmem of size:0x%x, fd:%d \n",pBuf->size, pBuf->fd, 0);
    close(pBuf->fd);
    pBuf->fd = -1;
    munmap(pBuf->virt, pBuf->size);
    pBuf->offset = 0;
    pBuf->phys = pBuf->virt = NULL;
    return OMX_ErrorNone;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Pmem::Allocate(OMX_U8 ** ppBuffer,
      OMX_S32 nBytes, int pmem_region_id)
  {
    void *pVirt;
    OMX_ERRORTYPE result = OMX_ErrorNone;
    static const OMX_S32 PMEM_ALIGN = 4096;
    static const OMX_U32 PMEM_ALIGN_MASK = 0xFFFFF000;
    OMX_S32 nBytesAlign;

    if (ppBuffer != NULL && nBytes > 0)
    {
      if (m_nBuffersAlloc < m_nBuffers)
      {
        OMX_S32 i = 0;
        struct venc_pmem* pBuffer = NULL;

        // look for a free info structure
        while (pBuffer == NULL)
        {
          if (m_pBufferInfo[i].virt == NULL)
          {
            pBuffer = &m_pBufferInfo[i];
          }
          ++i;
        }

        if (pBuffer != NULL)
        {
          nBytesAlign = (nBytes + 4095) & (~4095);
          pmem_alloc(pBuffer, nBytesAlign, pmem_region_id);

          pVirt = pBuffer->virt;
          if (pVirt != NULL)
          {
            /* pInfo->pVirt = (OMX_U8*) (((OMX_U32) (pVirt + PMEM_ALIGN)) & PMEM_ALIGN_MASK);
               pInfo->pVirtBase = pBuf->virt;
               pInfo->nBytes = nBytes;
               pInfo->nBytesAlign = nBytesAlign; */

            VENC_TEST_MSG_HIGH("Allocate buffer: fd:%d, offset:%d, pVirt=%p \n", pBuffer->fd,
                pBuffer->offset, pBuffer->virt);
            // --susan
            *ppBuffer = (OMX_U8 *)pBuffer;
            /* *ppBuffer = (OMX_U8 *)pInfo->virt; */
            ++m_nBuffersAlloc;
          }
          else
          {
            VENC_TEST_MSG_ERROR("pmem alloc failed");
            result = OMX_ErrorUndefined;
          }
        }
        else
        {
          VENC_TEST_MSG_ERROR("error finding buffer");
          result = OMX_ErrorUndefined;
        }
      }
      else
      {
        VENC_TEST_MSG_ERROR("ran out of buffers");
        result = OMX_ErrorUndefined;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad params");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  OMX_ERRORTYPE Pmem::Free(OMX_U8* pBuffer)
  {
    OMX_ERRORTYPE result = OMX_ErrorNone;
    if (pBuffer != NULL)
    {
      OMX_S32 i = 0;
      struct venc_pmem* pInfo = NULL;

      // look for a free info structure
      while (i < m_nBuffers )
      {
        if (&m_pBufferInfo[i] == (struct venc_pmem *)pBuffer)
        {
          pInfo = &m_pBufferInfo[i];
          break;
        }
        ++i;
      }

      if (pInfo != NULL)
      {
        pmem_free(pInfo);
        --m_nBuffersAlloc;
      }
      else
      {
        VENC_TEST_MSG_ERROR("error finding buffer");
        result = OMX_ErrorUndefined;
      }
    }
    else
    {
      VENC_TEST_MSG_ERROR("bad params");
      result = OMX_ErrorBadParameter;
    }
    return result;
  }

} // namespace venctest
