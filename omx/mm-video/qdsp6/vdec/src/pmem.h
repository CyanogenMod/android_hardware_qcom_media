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
#ifndef _PMEM_SIMPLE_H_
#define _PMEM_SIMPLE_H_

#define pmem_alloc vdec_pmem_alloc
#define pmem_free vdec_pmem_free

struct pmem {
   void *data;
   int fd;

   unsigned phys;
   unsigned size;
};

int pmem_alloc(struct pmem *out, unsigned sz);
void pmem_free(struct pmem *pmem);

#ifdef USE_PMEM_ADSP_CACHED

//Cache operation to perform on the pmem region
typedef enum {
  PMEM_CACHE_FLUSH = 0,
  PMEM_CACHE_INVALIDATE,
  PMEM_CACHE_FLUSH_INVALIDATE,
  PMEM_CACHE_INVALID_OP
} PMEM_CACHE_OP;

/**
  * This method is used to perform cache operations on the pmem regoin
  * in the decoder.
  *
  * Prerequisite: pmem_alloc should have been called.
  *
  *  @param[in] pmem_id
  *     id of the pmem region to use.
  *
  *  @param[in] addr
  *     The virtual addr of the pmem region
  *
  *  @param[in] size
  *     The size of the region
  *
  *  @param[in] op
  *     Cache operation to perform as defined by PMEM_CACHE_OP
  */
void pmem_cachemaint(int pmem_id, void *addr, unsigned size, PMEM_CACHE_OP op);
#endif
#endif
