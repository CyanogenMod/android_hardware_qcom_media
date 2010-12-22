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
                            O p e n M A X   Component
                        Video Decoder Input buffer class

*//** @file omx_vdec_inpbuf.cpp
  This module contains the class definition for openMAX input buffer related logic.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <omx_vdec.h>

/* ======================================================================
FUNCTION
  omx_vdec_inpbuf constructor

DESCRIPTION
  Constructor.

PARAMETERS
  None

RETURN VALUE
  None

========================================================================== */
omx_vdec_inpbuf::omx_vdec_inpbuf():m_read(0), m_write(0), m_last(-1),
    m_size(0)
{
   memset(m_pend_q, 0, sizeof(m_pend_q));
   memset(m_flags, 0, sizeof(m_flags));
}


/* ======================================================================
FUNCTION
  omx_vdec_inpbuf destructor

DESCRIPTION
  Destructor.

PARAMETERS
  None

RETURN VALUE
  None

========================================================================== */
omx_vdec_inpbuf::~omx_vdec_inpbuf()
{
       // empty due to the lack of dynamic members
}


/* ======================================================================
FUNCTION
  omx_vdec::add_entry

DESCRIPTION
  Add new entry to the pending queue.

PARAMETERS
  1. index   -- Input buffer index to be added.

RETURN VALUE
  true/false depending on whether the entry is successfully added or not?

========================================================================== */
bool omx_vdec_inpbuf::add_entry(unsigned int index)
{
   bool ret = false;
   if (m_size < MAX_NUM_INPUT_BUFFERS)
       {
      m_size++;
      ret = true;
      m_pend_q[m_write] = index;
      m_write = (m_write + 1) % MAX_NUM_INPUT_BUFFERS;
      BITMASK_SET(m_flags, index);
      }
   return ret;
}


/* ======================================================================
FUNCTION
  omx_vdec::push_back_entry

DESCRIPTION
  Push back the last entry deleted back to the pending queue.

PARAMETERS
  1.index - Index of the entry which needs to be re-inserted at the top

RETURN VALUE
  true/false depending on the situation.

========================================================================== */
bool omx_vdec_inpbuf::push_back_entry(unsigned int index)
{
   bool ret = false;
   if (m_size == 0)
       {

          // If the queue is empty execute the add_entry
          ret = add_entry(index);
      }
       // Make sure that last entry deleted is same
       else if ((m_size > 0) && (m_size < MAX_NUM_INPUT_BUFFERS) &&
           (m_last >= 0) && (m_last == index))
       {
      m_size++;
      ret = true;
          // Read index need to go back here
          if (m_read > 0)
          {
         m_read--;
         }
      else
          {
         m_read = MAX_NUM_INPUT_BUFFERS - 1;
         }
      m_pend_q[m_read] = index;
      BITMASK_SET(m_flags, index);
      }
   else
       {
      QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                "Error - push_back_entry didn't push anything m_size %d, m_last %d, index %d\n",
                m_size, m_last, index);
      }
   return ret;
}


/* ======================================================================
FUNCTION
  omx_vdec::remove_top_entry

DESCRIPTION
  Knock off the top entry from the pending queue.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
int omx_vdec_inpbuf::remove_top_entry(void)
{
   int ret = -1;
   if (m_size > 0)
       {
      m_size--;
      ret = m_pend_q[m_read];
      m_read = (m_read + 1) % MAX_NUM_INPUT_BUFFERS;
      BITMASK_CLEAR(m_flags, ret);
      m_last = ret;
      }
   return ret;
}


/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlBufferDoneCb

DESCRIPTION
  Buffer done callback from the decoder.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec_inpbuf::is_pending(void)
{
   return (m_size > 0);
}


/* ======================================================================
FUNCTION
  omx_vdec::OMXCntrlBufferDoneCb

DESCRIPTION
  Buffer done callback from the decoder.

PARAMETERS
  1. ctxt(I)   -- Context information to the self.
  2. cookie(I) -- Context information related to the specific input buffer

RETURN VALUE
  true/false

========================================================================== */
bool omx_vdec_inpbuf::is_pending(unsigned int index)
{
   return (BITMASK_PRESENT(m_flags, index) > 0);
}


/* ======================================================================
FUNCTION
  omx_vdec::get_first_pending_index

DESCRIPTION
  Get first pending index from the queue. Peek the first entry in
  the queue if there is any.

PARAMETERS
  None

RETURN VALUE
  Returns the top input buffer index

========================================================================== */
int omx_vdec_inpbuf::get_first_pending_index(void)
{
   int ret = -1;
   if (m_size > 0)
       {
      ret = m_pend_q[m_read];
      }
   return ret;
}
