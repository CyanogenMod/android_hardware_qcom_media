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

#ifndef _VENC_TEST_FILE_SOURCE_H
#define _VENC_TEST_FILE_SOURCE_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"

namespace venctest
{
  class File;          // forward declaration
  class SignalQueue;   // forward declaration
  class Thread;        // forward declaration

  /**
   * @brief Delivers YUV data in two different modes.
   *
   * In live mode, buffers are pre-populated with YUV data at the time
   * of configuration. The source will loop through and deliver the
   * pre-populated buffers throughout the life of the session. Frames will be
   * delivered at the configured frame rate. This mode is useful for gathering
   * performance statistics as no file reads are performed at run-time.
   *
   * In  non-live mode, buffers are populated with YUV data at run time.
   * Buffers are delivered downstream as they become available. Timestamps
   * are based on the configured frame rate, not on the system clock.
   *
   */
  class FileSource
  {
    public:

      /**
       * @brief Frame callback for YUV buffer deliver
       */
      typedef void (*FrameDeliveryFnType) (OMX_BUFFERHEADERTYPE* pBuffer);

    public:

      /**
       * @brief Constructor
       */
      FileSource();

      /**
       * @brief Destructor
       */
      ~FileSource();

    public:

      /**
       * @brief Configures the source
       *
       * @param nFrames The number of frames to to play.
       * @param nFramerate The frame rate to emulate.
       * @param nFrameWidth The frame width.
       * @param nFrameHeight The frame height.
       * @param nBuffers The number of buffers allocated for the session.
       * @param pFrameDeliverFn Frame delivery callback.
       * @param pFileName Name of the file to read from
       * @param nDVSXOffset Smaller frame pixel offset in the x direction
       * @param nDVSYOffset Smaller frame pixel offset in the y direction
       * @param bEnableLiveMode If true will encode in real time.
       */
      OMX_ERRORTYPE Configure(OMX_S32 nFrames,
          OMX_S32 nFramerate,
          OMX_S32 nFrameWidth,
          OMX_S32 nFrameHeight,
          OMX_S32 nBuffers,
          FrameDeliveryFnType pFrameDeliverFn,
          OMX_STRING pFileName,
          OMX_S32 nDVSXOffset,
          OMX_S32 nDVSYOffset,
          OMX_BOOL bLiveMode);

      /**
       * @brief Changes the frame rate
       *
       * The frame rate will take effect immediately.
       *
       * @param nFramerate The new frame rate.
       */
      OMX_ERRORTYPE ChangeFrameRate(OMX_S32 nFramerate);

      /**
       * @brief Starts the delivery of buffers.
       *
       * All buffers should be registered through the SetFreeBuffer function
       * prior to calling this function.
       *
       * Source will continue to deliver buffers at the specified
       * rate until the specified number of frames have been delivered.
       *
       * Note that Source will not deliver buffers unless it has ownership
       * of atleast 1 buffer. If Source does not have ownership of buffer when
       * timer expires, it will block until a buffer is given to the component.
       * Free buffers should be given to the Source through SetFreeBuffer
       * function.
       */
      OMX_ERRORTYPE Start();

      /**
       * @brief Wait for the thread to finish.
       *
       * Function will block until the Source has delivered all frames.
       */
      OMX_ERRORTYPE Finish();

      /**
       * @brief Gives ownership of the buffer to the source.
       *
       * All bufffers must be registered with the Source prior
       * to invoking Start. This will give initial ownership to
       * the source.
       *
       * After Start is called, buffers must be given ownership
       * when yuv buffers are free.
       *
       * @param pBuffer Pointer to the buffer
       */
      OMX_ERRORTYPE SetFreeBuffer(OMX_BUFFERHEADERTYPE* pBuffer);

    private:
      static OMX_ERRORTYPE SourceThreadEntry(OMX_PTR pThreadData);
      OMX_ERRORTYPE SourceThread();

    private:
      OMX_S32 m_nFrames;
      OMX_S32 m_nFramesRegistered;
      OMX_S32 m_nFramerate;
      OMX_S32 m_nFrameWidth;
      OMX_S32 m_nFrameHeight;
      OMX_S32 m_nBuffers;
      OMX_S32 m_nDVSXOffset;
      OMX_S32 m_nDVSYOffset;
      File* m_pFile;
      SignalQueue* m_pBufferQueue;
      Thread* m_pThread;
      OMX_BOOL m_bStarted;
      FrameDeliveryFnType m_pFrameDeliverFn;
      OMX_BOOL m_bLiveMode;
  };

} // namespace venctest

#endif // #ifndef _VENC_TEST_FILE_SOURCE_H
