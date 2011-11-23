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

#ifndef _VENC_TEST_ENCODER_H
#define _VENC_TEST_ENCODER_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include "OMX_Core.h"
#include "venctest_ComDef.h"
#include "qc_omx_common.h"

namespace venctest
{

  class Signal;        // forward declaration
  class Pmem;          // forward declaration
  class SignalQueue;   // forward declaration
  class StatsThread;   // forward declaration

  class Encoder
  {
    public:

      /**
       * @brief Event cb type. Refer to OMX IL spec for param details.
       */
      typedef OMX_ERRORTYPE (*EventCallbackType)(
          OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_EVENTTYPE eEvent,
          OMX_IN OMX_U32 nData1,
          OMX_IN OMX_U32 nData2,
          OMX_IN OMX_PTR pEventData);

      /**
       * @brief Empty buffer done cb type. Refer to OMX IL spec for param details.
       */
      typedef OMX_ERRORTYPE (*EmptyDoneCallbackType)(
          OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

      /**
       * @brief Fill buffer done cb type. Refer to OMX IL spec for param details.
       */
      typedef OMX_ERRORTYPE (*FillDoneCallbackType)(
          OMX_OUT OMX_HANDLETYPE hComponent,
          OMX_OUT OMX_PTR pAppData,
          OMX_OUT OMX_BUFFERHEADERTYPE* pBuffer);
    public:

      /**
       * @brief Constructor
       *
       * @param pEmptyDoneFn Empty buffer done callback. Refer to OMX IL spec
       * @param pFillDoneFn Fill buffer done callback. Refer to OMX IL spec
       * @param pAppData Client data passed to buffer and event callbacks
       * @param eCodec The codec
       */
      Encoder(EmptyDoneCallbackType pEmptyDoneFn,
          FillDoneCallbackType pFillDoneFn,
          OMX_PTR pAppData,
          OMX_VIDEO_CODINGTYPE eCodec);

      /**
       * @brief Destructor
       */
      ~Encoder();

    private:

      /**
       * @brief Private default constructor. Use public constructor.
       */
      Encoder();


    public:

      ///////////////////////////////////////////////////////////
      ///////////////////////////////////////////////////////////
      // Init time methods
      ///////////////////////////////////////////////////////////
      ///////////////////////////////////////////////////////////

      /**
       * @brief Configure the encoder
       *
       * @param pConfig The encoder configuration
       */
      OMX_ERRORTYPE Configure(EncoderConfigType* pConfig);

      /**
       * @brief Enables OMX_UseBuffer allocation scheme. Default is OMX_AllocateBuffer.
       *
       * @param bUseBuffer Set to OMX_TRUE for OMX_UseBuffer model
       */
      OMX_ERRORTYPE EnableUseBufferModel(OMX_BOOL bInUseBuffer, OMX_BOOL bOutUseBuffer);

      /**
       * @brief Synchronously transitions the encoder to OMX_StateExecuting.
       *
       * Only valid in OMX_StateLoaded;
       */
      OMX_ERRORTYPE GoToExecutingState();

      /**
       * @brief Get the out of band syntax header
       *
       * Only valid in OMX_StateLoaded;
       */
      OMX_ERRORTYPE GetOutOfBandSyntaxHeader(OMX_U8* pSyntaxHdr,
          OMX_S32 nSyntaxHdrLen,
          OMX_S32* pSyntaxHdrFilledLen);

      ///////////////////////////////////////////////////////////
      ///////////////////////////////////////////////////////////
      // Run time methods
      ///////////////////////////////////////////////////////////
      ///////////////////////////////////////////////////////////

      /**
       * @brief Synchronously transitions the encoder to OMX_StateLoaded.
       *
       * Only valid in OMX_StateExecuting;
       */
      OMX_ERRORTYPE GoToLoadedState();

      /**
       * @brief Deliver an input (yuv) buffer to the encoder.
       *
       * @param pBuffer The populated input buffer.
       */
      OMX_ERRORTYPE DeliverInput(OMX_BUFFERHEADERTYPE* pBuffer);

      /**
       * @brief Deliver an output (bitstream) buffer to the encoder.
       *
       * @param pBuffer The un-populated output buffer.
       */
      OMX_ERRORTYPE DeliverOutput(OMX_BUFFERHEADERTYPE* pBuffer);

      /**
       * @brief Get array of input or output buffer pointers header.
       *
       * Only valid in the executing state after all buffers have been allocated.
       *
       * @param bIsInput Set to OMX_TRUE for input OMX_FALSE for output.
       * @return NULL upon failure, array of buffer header pointers otherwise.
       */
      OMX_BUFFERHEADERTYPE** GetBuffers(OMX_BOOL bIsInput);

      /**
       * @brief Request for an iframe to be generated.
       */
      OMX_ERRORTYPE RequestIntraVOP();

      /**
       * @brief Set the intra period. It is valid to change this configuration at run-time
       *
       * @param nIntraPeriod The iframe interval in units of frames
       */
      OMX_ERRORTYPE SetIntraPeriod(OMX_S32 nIntraPeriod);

      /**
       * @brief Change the encoding quality
       *
       * @param nFramerate The updated frame rate
       * @param nBitrate The updated bitrate
       * @param nMinQp The updated min qp
       * @param nMaxQp The updated max qp
       */
      OMX_ERRORTYPE ChangeQuality(OMX_S32 nFramerate,
          OMX_S32 nBitrate,
          OMX_S32 nMinQp,
          OMX_S32 nMaxQp);

      /**
       * @brief Set the encoder state
       *
       * This method can be asynchronous or synchronous. If asynchonous,
       * WaitState can be called to wait for the corresponding state
       * transition to complete.
       *
       * @param eState The state to enter
       * @param bSynchronous If OMX_TRUE, synchronously wait for the state transition to complete
       */
      OMX_ERRORTYPE SetState(OMX_STATETYPE eState,
          OMX_BOOL bSynchronous);

      /**
       * @brief Wait for the corresponding state transition to complete
       *
       * @param eState The state to wait for
       */
      OMX_ERRORTYPE WaitState(OMX_STATETYPE eState);

      /**
       * @brief Allocate all input and output buffers
       */
      OMX_ERRORTYPE AllocateBuffers();

      /**
       * @brief Free all input and output buffers
       */
      OMX_ERRORTYPE FreeBuffers();

      /**
       * @brief Flush the encoder
       */
      OMX_ERRORTYPE Flush();

    private:

      static OMX_ERRORTYPE EventCallback(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_EVENTTYPE eEvent,
          OMX_IN OMX_U32 nData1,
          OMX_IN OMX_U32 nData2,
          OMX_IN OMX_PTR pEventData);

      static OMX_ERRORTYPE EmptyDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

      static OMX_ERRORTYPE FillDoneCallback(OMX_IN OMX_HANDLETYPE hComponent,
          OMX_IN OMX_PTR pAppData,
          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer);

    private:
      SignalQueue* m_pSignalQueue;
      Pmem* m_pInMem;
      Pmem* m_pOutMem;
      StatsThread* m_pStats;
      EncoderConfigType m_sConfig;
      EventCallbackType m_pEventFn;
      EmptyDoneCallbackType m_pEmptyDoneFn;
      FillDoneCallbackType m_pFillDoneFn;
      OMX_PTR m_pAppData;
      OMX_BOOL m_bInUseBuffer;
      OMX_BOOL m_bOutUseBuffer;
      OMX_BUFFERHEADERTYPE** m_pInputBuffers;
      OMX_BUFFERHEADERTYPE** m_pOutputBuffers;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *m_pInBufPvtList;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *m_pInBufPmemInfo;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *m_pInBufPvtEntry;
      OMX_QCOM_PLATFORM_PRIVATE_LIST *m_pOutBufPvtList;
      OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *m_pOutBufPmemInfo;
      OMX_QCOM_PLATFORM_PRIVATE_ENTRY *m_pOutBufPvtEntry;
      OMX_HANDLETYPE m_hEncoder;
      OMX_STATETYPE m_eState;
      OMX_S32 m_nInputBuffers;
      OMX_S32 m_nOutputBuffers;
      OMX_S32 m_nInputBufferSize;
      OMX_S32 m_nOutputBufferSize;
      OMX_VIDEO_CODINGTYPE m_eCodec;
      OMX_S32 m_nInFrameIn;
      OMX_S32 m_nOutFrameIn;
      OMX_S32 m_nInFrameOut;
      OMX_S32 m_nOutFrameOut;


  };

} // namespace venctest

#endif // #ifndef  _VENC_TEST_ENCODER_H
