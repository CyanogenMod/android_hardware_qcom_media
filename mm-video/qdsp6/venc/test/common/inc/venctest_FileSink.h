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

#ifndef _VENC_TEST_FILE_SINK_H
#define _VENC_TEST_FILE_SINK_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"

namespace venctest
{

  class SignalQueue;   // forward declaration

  /**
   * @brief Writes encoded bitstream to file.
   *
   * Frames are written to file asynchronously as they are delivered.
   */
  class FileSink
  {
    public:

      /**
       * @brief Frame callback for bitstream buffer release
       */
      typedef void (*FrameReleaseFnType) (OMX_BUFFERHEADERTYPE* pBuffer);

    public:

      /**
       * @brief Constructor
       */
      FileSink();

      /**
       * @brief Destructor
       */
      ~FileSink();

    public:

      /**
       * @brief Configure the file writer
       *
       * @param nFrames The number of frames to write. Thread exits when nFrames are written.
       * @param pFileName The name of the output file. Specify NULL for no file I/O.
       * @param nTestNum The test number
       * @param pFrameReleaseFn The callback to invoke when after frames are written
       */
      OMX_ERRORTYPE Configure(OMX_S32 nFrames,
          OMX_STRING pFileName,
          OMX_S32 nTestNum,
          FrameReleaseFnType pFrameReleaseFn);

      /**
       * @brief Asynchronously write an encoded frame to file.
       *
       * @param pBuffer The frame to write.
       */
      OMX_ERRORTYPE Write(OMX_BUFFERHEADERTYPE* pBuffer);

      /**
       * @brief Starts the writer thread.
       *
       */
      OMX_ERRORTYPE Start();

      /**
       * @brief Wait for writer thread to finish.
       *
       * Function will block until the Sink has received and written all frames.
       */
      OMX_ERRORTYPE Finish();

    private:
      static OMX_ERRORTYPE SinkThreadEntry(OMX_PTR pThreadData);
      OMX_ERRORTYPE SinkThread();

    private:
      OMX_S32 m_nFrames;
      File* m_pFile;
      SignalQueue* m_pBufferQueue;
      Thread* m_pThread;
      OMX_BOOL m_bStarted;
      FrameReleaseFnType m_pFrameReleaseFn;
  };

} // namespace venctest

#endif // #ifndef _VENC_TEST_FILE_SINK_H
