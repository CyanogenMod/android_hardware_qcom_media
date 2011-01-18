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

#ifndef OMX_VENC_H
#define OMX_VENC_H

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "qc_omx_component.h"
#include "OMX_Types.h"
#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"

#include "venc_device.h"
#include <pthread.h>
#include <linux/msm_q6venc.h>
#include <semaphore.h>

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/
class VencBufferManager; // forward declaration
class VencMsgQ; // forward declaration

/*---------------------------------------------------------------------------
 * Class Definitions
 ---------------------------------------------------------------------------*/


/**
 * The omx mpeg4 video encoder class.
 */
class Venc : public qc_omx_component
{
  public:

    /**********************************************************************//**
     * @brief Class constructor
     *************************************************************************/
    Venc();

    /**********************************************************************//**
     * @brief Class destructor
     *************************************************************************/
    virtual ~Venc();

    /**********************************************************************//**
     * @brief Initializes the component
     *
     * @return error if unsuccessful.
     *************************************************************************/
    virtual OMX_ERRORTYPE component_init(OMX_IN OMX_STRING pComponentName);

    //////////////////////////////////////////////////////////////////////////
    /// For the following methods refer to corresponding function descriptions
    /// in the OMX_COMPONENTTYPE structure in OMX_Componenent.h
    //////////////////////////////////////////////////////////////////////////

    virtual OMX_ERRORTYPE get_component_version(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_STRING pComponentName,
        OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
        OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
        OMX_OUT OMX_UUIDTYPE* pComponentUUID);

    virtual OMX_ERRORTYPE send_command(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_COMMANDTYPE Cmd,
        OMX_IN  OMX_U32 nParam1,
        OMX_IN  OMX_PTR pCmdData);

    virtual OMX_ERRORTYPE get_parameter(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nParamIndex,
        OMX_INOUT OMX_PTR pComponentParameterStructure);


    virtual OMX_ERRORTYPE set_parameter(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_IN  OMX_PTR pComponentParameterStructure);


    virtual OMX_ERRORTYPE get_config(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_INOUT OMX_PTR pComponentConfigStructure);


    virtual OMX_ERRORTYPE set_config(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_INDEXTYPE nIndex,
        OMX_IN  OMX_PTR pComponentConfigStructure);


    virtual OMX_ERRORTYPE get_extension_index(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_STRING cParameterName,
        OMX_OUT OMX_INDEXTYPE* pIndexType);


    virtual OMX_ERRORTYPE get_state(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_STATETYPE* pState);


    virtual OMX_ERRORTYPE component_tunnel_request(
        OMX_IN  OMX_HANDLETYPE hComp,
        OMX_IN  OMX_U32 nPort,
        OMX_IN  OMX_HANDLETYPE hTunneledComp,
        OMX_IN  OMX_U32 nTunneledPort,
        OMX_INOUT  OMX_TUNNELSETUPTYPE* pTunnelSetup);

    virtual OMX_ERRORTYPE use_buffer(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN OMX_U32 nSizeBytes,
        OMX_IN OMX_U8* pBuffer);

    virtual OMX_ERRORTYPE allocate_buffer(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBuffer,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN OMX_U32 nSizeBytes);

    virtual OMX_ERRORTYPE free_buffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_U32 nPortIndex,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer);

    virtual OMX_ERRORTYPE empty_this_buffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer);

    virtual OMX_ERRORTYPE fill_this_buffer(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer);

    virtual OMX_ERRORTYPE set_callbacks(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_CALLBACKTYPE* pCallbacks,
        OMX_IN  OMX_PTR pAppData);

    virtual OMX_ERRORTYPE component_deinit(
        OMX_IN  OMX_HANDLETYPE hComponent);

    virtual OMX_ERRORTYPE use_EGL_image(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
        OMX_IN OMX_U32 nPortIndex,
        OMX_IN OMX_PTR pAppPrivate,
        OMX_IN void* eglImage);

    virtual OMX_ERRORTYPE component_role_enum(
        OMX_IN OMX_HANDLETYPE hComponent,
        OMX_OUT OMX_U8 *cRole,
        OMX_IN OMX_U32 nIndex);

  private:
    /// Max number of input buffers (assumed)
    static const OMX_S32 MAX_IN_BUFFERS = 16;

    /// Max number of output buffers (assumed)
    static const OMX_S32 MAX_OUT_BUFFERS = 16;

    /// Port indexes according to the OMX IL spec
    enum PortIndexType
    {
      PORT_INDEX_IN = 0,
      PORT_INDEX_OUT = 1,
      PORT_INDEX_BOTH = -1,
      PORT_INDEX_NONE = -2
    };

    // Bit Positions
    enum flags_bit_positions
    {
      // Defer transition to IDLE
      OMX_COMPONENT_IDLE_PENDING = 0x1,
      // Defer transition to LOADING
      OMX_COMPONENT_LOADING_PENDING = 0x2,
      // Defer transition to Enable
      OMX_COMPONENT_INPUT_ENABLE_PENDING = 0x3,
      // Defer transition to Enable
      OMX_COMPONENT_OUTPUT_ENABLE_PENDING = 0x4,
      // Defer transition to Disable
      OMX_COMPONENT_INPUT_DISABLE_PENDING = 0x5,
      // Defer transition to Disable
      OMX_COMPONENT_OUTPUT_DISABLE_PENDING = 0x6,
      //Defer going to the Invalid state
      OMX_COMPONENT_INVALID_PENDING = 0x9,
      //Defer going to Pause state
      OMX_COMPONENT_PAUSE_PENDING = 0xA
    };
    /// Private data for buffer header field pInputPortPrivate
    struct PrivatePortData
    {
      OMX_BOOL bComponentAllocated; ///< Did we allocate this buffer?
      struct venc_pmem sPmemInfo;
    };

  private:

    /**********************************************************************//**
     * @brief encode frame function.
     *
     * Method will check to see if there is an available input
     * and output buffer. If so, then it will encode, otherwise it will
     * queue the specified buffer until it receives another buffer.
     *
     * @param pBuffer: pointer to the input or output buffer.
     * @return NULL (ignore)
     *************************************************************************/
    //   void process_encode_frame(OMX_BUFFERHEADERTYPE* pBuffer);
    OMX_ERRORTYPE encode_frame(OMX_BUFFERHEADERTYPE* pBuffer);
    /**********************************************************************//**
     * @brief Component thread main function.
     *
     * @param pClassObj: Pointer to the Venc instance.
     * @return NULL (ignore)
     *************************************************************************/
    static void *component_thread(void* pClassObj);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     *************************************************************************/
    void process_state_change(OMX_STATETYPE eState);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_FLUSH
     *
     * @param nPortIndex: The port index
     *************************************************************************/
    void process_flush(OMX_U32 nPortIndex);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     *************************************************************************/
    void process_port_enable(OMX_U32 nPortIndex);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     *************************************************************************/
    void process_port_disable(OMX_U32 nPortIndex);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     * @param pMarkData: The mark buffer data
     *************************************************************************/
    void process_mark_buffer(OMX_U32 nPortIndex,
        const OMX_MARKTYPE* pMarkData);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     * @param pMarkData: The mark buffer data
     *************************************************************************/
    void process_empty_buffer(OMX_BUFFERHEADERTYPE* pBuffer);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     * @param pMarkData: The mark buffer data
     *************************************************************************/
    void process_fill_buffer(OMX_BUFFERHEADERTYPE* pBuffer);

    /**********************************************************************//**
     * @brief Process thread message MSG_ID_STATE_CHANGE
     *
     * @param nPortIndex: The port index
     * @param pMarkData: The mark buffer data
     *************************************************************************/
    void process_driver_msg(struct venc_msg* pMsg);

    OMX_ERRORTYPE is_multi_slice_mode_supported();
    // driver status messages
    void process_status_input_buffer_done(void* pData, unsigned long nStatus);
    void process_status_output_buffer_done(void* pData, unsigned long nStatus);
    void process_status_flush_done(struct venc_buffer_flush* pData, unsigned long nStatus);
    void process_status_start_done(unsigned long nStatus);
    void process_status_stop_done(unsigned long nStatus);
    void process_status_pause_done(unsigned long nStatus);
    void process_status_resume_done(unsigned long nStatus);

    static void *reader_thread_entry(void *);
    void reader_thread();


    OMX_ERRORTYPE translate_profile(unsigned int* pDriverProfile,
        OMX_U32 eProfile,
        OMX_VIDEO_CODINGTYPE eCodec);
    OMX_ERRORTYPE translate_level(unsigned int* pDriverLevel,
        OMX_U32 eLevel,
        OMX_VIDEO_CODINGTYPE eCodec);

    OMX_ERRORTYPE translate_driver_error(int);
    OMX_ERRORTYPE driver_set_default_config();
    OMX_ERRORTYPE adjust_profile_level();
    OMX_ERRORTYPE driver_get_buffer_reqs(OMX_PARAM_PORTDEFINITIONTYPE* pPortDef, PortIndexType eIndex);

    /**********************************************************************//**
     * @brief update the port format
     *************************************************************************/
    OMX_ERRORTYPE update_param_port_fmt(OMX_IN OMX_VIDEO_PARAM_PORTFORMATTYPE* pParam);

    /**********************************************************************//**
     * @brief update the port definition
     *************************************************************************/
    OMX_ERRORTYPE update_param_port_def(OMX_IN OMX_PARAM_PORTDEFINITIONTYPE* pParam);

    /**********************************************************************//**
     * @brief update video init params
     *************************************************************************/
    OMX_ERRORTYPE update_param_video_init(OMX_IN OMX_PORT_PARAM_TYPE* pParam);

    /**********************************************************************//**
     * @brief update bitrate
     *************************************************************************/
    OMX_ERRORTYPE update_param_bitrate(OMX_IN OMX_VIDEO_PARAM_BITRATETYPE* pParam);

    /**********************************************************************//**
     * @brief update mpeg4 params
     *************************************************************************/
    OMX_ERRORTYPE update_param_mpeg4(OMX_IN OMX_VIDEO_PARAM_MPEG4TYPE* pParam);

    /**********************************************************************//**
     * @brief update profile/level params
     *************************************************************************/
    OMX_ERRORTYPE update_param_profile_level(OMX_IN OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam);

    /**********************************************************************//**
     * @brief update error correction
     *************************************************************************/
    OMX_ERRORTYPE update_param_err_correct(OMX_IN OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam);

    /**********************************************************************//**
     * @brief update h263 params
     *************************************************************************/
    OMX_ERRORTYPE update_param_h263(OMX_IN OMX_VIDEO_PARAM_H263TYPE* pParam);

    /**********************************************************************//**
     * @brief update avc params
     *************************************************************************/
    OMX_ERRORTYPE update_param_avc(OMX_IN OMX_VIDEO_PARAM_AVCTYPE* pParam);

    /**********************************************************************//**
     * @brief update quant params
     *************************************************************************/
    OMX_ERRORTYPE update_param_quantization(OMX_IN OMX_VIDEO_PARAM_QUANTIZATIONTYPE* pParam);

    /**********************************************************************//**
     * @brief update intra refresh
     *************************************************************************/
    OMX_ERRORTYPE update_param_intra_refresh(OMX_IN OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam);

    /**********************************************************************//**
     * @brief update bitrate
     *************************************************************************/
    OMX_ERRORTYPE update_config_bitrate(OMX_VIDEO_CONFIG_BITRATETYPE* pConfig);

    /**********************************************************************//**
     * @brief update frame rate
     *************************************************************************/
    OMX_ERRORTYPE update_config_frame_rate(OMX_CONFIG_FRAMERATETYPE* pConfig);

    /**********************************************************************//**
     * @brief update rotation
     *************************************************************************/
    OMX_ERRORTYPE update_config_rotate(OMX_CONFIG_ROTATIONTYPE* pConfig);

    /**********************************************************************//**
     * Check for the actual buffer allocate/deallocate complete
     *
     *************************************************************************/
    OMX_BOOL allocate_done(void);
    OMX_BOOL release_done();
    /**********************************************************************//**
     * @brief request intra vop
     *************************************************************************/
    OMX_ERRORTYPE update_config_intra_vop_refresh(OMX_IN  OMX_CONFIG_INTRAREFRESHVOPTYPE* pConfig);

#ifdef QCOM_OMX_VENC_EXT
    /**********************************************************************//**
     * @brief change qp range
     *************************************************************************/
    OMX_ERRORTYPE update_config_qp_range(OMX_IN  QOMX_VIDEO_TEMPORALSPATIALTYPE* pConfig);

    /**********************************************************************//**
     * @brief change the intra period
     *************************************************************************/
    OMX_ERRORTYPE update_config_intra_period(OMX_IN  QOMX_VIDEO_INTRAPERIODTYPE* pConfig);
#endif

    /**********************************************************************//**
     * @brief change the nal size
     *************************************************************************/
    OMX_ERRORTYPE update_config_nal_size(OMX_IN  OMX_VIDEO_CONFIG_NALSIZE* pConfig);

    OMX_ERRORTYPE allocate_q6_buffers(struct venc_buffers *);
    OMX_ERRORTYPE free_q6_buffers(struct venc_buffers *);

    OMX_ERRORTYPE pmem_alloc(struct venc_pmem *ptr, int size, int pmem_region_id);
    OMX_ERRORTYPE pmem_free(struct venc_pmem *);
  private:

    /// thread object
    pthread_t m_ComponentThread;

    /// thread object
    pthread_t m_ReaderThread;

    /// Input buffer manager
    VencBufferManager* m_pInBufferMgr;

    /// Output buffer manager
    VencBufferManager* m_pOutBufferMgr;

    /// Message Q
    VencMsgQ* m_pMsgQ;

    /// Private input buffer data
    PrivatePortData* m_pPrivateInPortData;

    /// Private output buffer data
    PrivatePortData* m_pPrivateOutPortData;

    struct venc_buffers m_sQ6Buffers;
  private:

    OMX_STATETYPE m_eState;
    OMX_CALLBACKTYPE m_sCallbacks;
    OMX_PTR m_pAppData;
    OMX_HANDLETYPE m_hSelf;
    OMX_PORT_PARAM_TYPE m_sPortParam;
    OMX_PARAM_PORTDEFINITIONTYPE m_sInPortDef;
    OMX_PARAM_PORTDEFINITIONTYPE m_sOutPortDef;
    OMX_U32 m_nFlags;
    OMX_VIDEO_PARAM_PORTFORMATTYPE m_sInPortFormat;
    OMX_VIDEO_PARAM_PORTFORMATTYPE m_sOutPortFormat;
    OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE m_sErrorCorrection;
    OMX_PRIORITYMGMTTYPE m_sPriorityMgmt;
    OMX_PARAM_BUFFERSUPPLIERTYPE m_sInBufSupplier;
    OMX_PARAM_BUFFERSUPPLIERTYPE m_sOutBufSupplier;
    OMX_VIDEO_PARAM_MPEG4TYPE m_sParamMPEG4;
    OMX_VIDEO_PARAM_AVCTYPE m_sParamAVC;
    OMX_VIDEO_PARAM_BITRATETYPE m_sParamBitrate;
    OMX_VIDEO_PARAM_PROFILELEVELTYPE m_sParamProfileLevel;
    OMX_VIDEO_PARAM_INTRAREFRESHTYPE m_sParamIntraRefresh;
    OMX_VIDEO_CONFIG_BITRATETYPE m_sConfigBitrate;
    OMX_CONFIG_FRAMERATETYPE m_sConfigFramerate;
    OMX_CONFIG_ROTATIONTYPE m_sConfigFrameRotation;
    OMX_VIDEO_PARAM_H263TYPE m_sParamH263;
    OMX_VIDEO_PARAM_QUANTIZATIONTYPE m_sParamQPs;
    OMX_CONFIG_INTRAREFRESHVOPTYPE m_sConfigIntraRefreshVOP;
#ifdef QCOM_OMX_VENC_EXT
    QOMX_VIDEO_TEMPORALSPATIALTYPE m_sConfigQpRange;
    QOMX_VIDEO_INTRAPERIODTYPE m_sConfigIntraPeriod;
#endif
    OMX_VIDEO_CONFIG_NALSIZE m_sConfigNAL;
    OMX_BUFFERHEADERTYPE* m_pInBuffHeaders;
    OMX_BUFFERHEADERTYPE* m_pOutBuffHeaders;
    OMX_STRING m_pComponentName;
    OMX_U8 m_cRole[OMX_MAX_STRINGNAME_SIZE];
    struct ven_device *m_pDevice;
    OMX_S32 m_nFd;

    OMX_U32 m_nInBuffAllocated;
    OMX_U32 m_nOutBuffAllocated;
    sem_t   m_cmd_lock;

};

#endif
