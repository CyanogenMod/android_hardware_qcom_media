/*--------------------------------------------------------------------------
Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
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
#include "omx_swvenc_hevc.h"
#include <string.h>
#include <stdio.h>
#include <media/hardware/HardwareAPI.h>
#include <gralloc_priv.h>
#include <media/msm_media_info.h>

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

#define OMX_SPEC_VERSION 0x00000101
#define OMX_INIT_STRUCT(_s_, _name_)            \
    memset((_s_), 0x0, sizeof(_name_));          \
(_s_)->nSize = sizeof(_name_);               \
(_s_)->nVersion.nVersion = OMX_SPEC_VERSION

extern int m_pipe;

// factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
    return(new omx_swvenc);
}

//constructor

omx_swvenc::omx_swvenc()
{
#ifdef _ANDROID_ICS_
    meta_mode_enable = false;
    memset(meta_buffer_hdr,0,sizeof(meta_buffer_hdr));
    memset(meta_buffers,0,sizeof(meta_buffers));
    memset(opaque_buffer_hdr,0,sizeof(opaque_buffer_hdr));
    mUseProxyColorFormat = false;
    get_syntaxhdr_enable = false;
#endif
    char property_value[PROPERTY_VALUE_MAX] = {0};
    property_get("vidc.debug.level", property_value, "0");
    debug_level = atoi(property_value);
    property_value[0] = '\0';
    m_pSwVenc = NULL;
}

omx_swvenc::~omx_swvenc()
{
    get_syntaxhdr_enable = false;
    //nothing to do
}

/* ======================================================================
   FUNCTION
   omx_swvenc::ComponentInit

   DESCRIPTION
   Initialize the component.

   PARAMETERS
   ctxt -- Context information related to the self.
   id   -- Event identifier. This could be any of the following:
   1. Command completion event
   2. Buffer done callback event
   3. Frame done callback event

   RETURN VALUE
   None.

   ========================================================================== */
OMX_ERRORTYPE omx_swvenc::component_init(OMX_STRING role)
{

    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    int fds[2];
    int r;

    OMX_VIDEO_CODINGTYPE codec_type;

    DEBUG_PRINT_HIGH("omx_swvenc(): Inside component_init()");
    // Copy the role information which provides the decoder m_nkind
    strlcpy((char *)m_nkind,role,OMX_MAX_STRINGNAME_SIZE);
    secure_session = false;

    if (!strncmp((char *)m_nkind,"OMX.qti.video.encoder.hevc",\
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.hevc",\
                OMX_MAX_STRINGNAME_SIZE);
        codec_type = (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingHevc;
    }
    else {
        DEBUG_PRINT_ERROR("ERROR: Unknown Component");
        eRet = OMX_ErrorInvalidComponentName;
    }


    if (eRet != OMX_ErrorNone) {
        return eRet;
    }
#ifdef ENABLE_GET_SYNTAX_HDR
    get_syntaxhdr_enable = true;
    DEBUG_PRINT_HIGH("Get syntax header enabled");
#endif

    OMX_INIT_STRUCT(&m_sParamHEVC, OMX_VIDEO_PARAM_HEVCTYPE);
    m_sParamHEVC.eProfile = OMX_VIDEO_HEVCProfileMain;
    m_sParamHEVC.eLevel = OMX_VIDEO_HEVCMainTierLevel3;

    // Init for SWCodec
    DEBUG_PRINT_HIGH("\n:Initializing SwVenc");
    SWVENC_INITPARAMS swVencParameter;
    memset(&swVencParameter, 0, sizeof(SWVENC_INITPARAMS));
    swVencParameter.sDimensions.nWidth = 176;
    swVencParameter.sDimensions.nHeight = 144;
    swVencParameter.uProfile.eHevcProfile = SWVENC_HEVC_MAIN_PROFILE;
    //sSwVencParameter.nNumWorkerThreads = 3;

    m_callBackInfo.FillBufferDone   = swvenc_fill_buffer_done_cb;
    m_callBackInfo.EmptyBufferDone  = swvenc_input_buffer_done_cb;
    m_callBackInfo.HandleEvent      = swvenc_handle_event_cb;
    m_callBackInfo.pClientHandle    = this;
    SWVENC_STATUS sRet = SwVenc_Init(&swVencParameter, &m_callBackInfo, &m_pSwVenc);
    if (sRet != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("ERROR: SwVenc_Init failed");
        return OMX_ErrorInsufficientResources;
    }


    //Intialise the OMX layer variables
    memset(&m_pCallbacks,0,sizeof(OMX_CALLBACKTYPE));

    OMX_INIT_STRUCT(&m_sPortParam, OMX_PORT_PARAM_TYPE);
    m_sPortParam.nPorts = 0x2;
    m_sPortParam.nStartPortNumber = (OMX_U32) PORT_INDEX_IN;

    OMX_INIT_STRUCT(&m_sPortParam_audio, OMX_PORT_PARAM_TYPE);
    m_sPortParam_audio.nPorts = 0;
    m_sPortParam_audio.nStartPortNumber = 0;

    OMX_INIT_STRUCT(&m_sPortParam_img, OMX_PORT_PARAM_TYPE);
    m_sPortParam_img.nPorts = 0;
    m_sPortParam_img.nStartPortNumber = 0;

    OMX_INIT_STRUCT(&m_sParamBitrate, OMX_VIDEO_PARAM_BITRATETYPE);
    m_sParamBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamBitrate.eControlRate = OMX_Video_ControlRateVariableSkipFrames;
    m_sParamBitrate.nTargetBitrate = 64000;

    OMX_INIT_STRUCT(&m_sConfigBitrate, OMX_VIDEO_CONFIG_BITRATETYPE);
    m_sConfigBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigBitrate.nEncodeBitrate = 64000;

    OMX_INIT_STRUCT(&m_sConfigFramerate, OMX_CONFIG_FRAMERATETYPE);
    m_sConfigFramerate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigFramerate.xEncodeFramerate = 30 << 16;

    OMX_INIT_STRUCT(&m_sConfigIntraRefreshVOP, OMX_CONFIG_INTRAREFRESHVOPTYPE);
    m_sConfigIntraRefreshVOP.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigIntraRefreshVOP.IntraRefreshVOP = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sConfigFrameRotation, OMX_CONFIG_ROTATIONTYPE);
    m_sConfigFrameRotation.nPortIndex = (OMX_U32) PORT_INDEX_IN;
    m_sConfigFrameRotation.nRotation = 0;

    OMX_INIT_STRUCT(&m_sSessionQuantization, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
    m_sSessionQuantization.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionQuantization.nQpI = 9;
    m_sSessionQuantization.nQpP = 6;
    m_sSessionQuantization.nQpB = 2;

    OMX_INIT_STRUCT(&m_sSessionQPRange, OMX_QCOM_VIDEO_PARAM_QPRANGETYPE);
    m_sSessionQPRange.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionQPRange.minQP = 2;
    if (codec_type == OMX_VIDEO_CodingAVC)
        m_sSessionQPRange.maxQP = 51;
    else
        m_sSessionQPRange.maxQP = 31;

    OMX_INIT_STRUCT(&m_sAVCSliceFMO, OMX_VIDEO_PARAM_AVCSLICEFMO);
    m_sAVCSliceFMO.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sAVCSliceFMO.eSliceMode = OMX_VIDEO_SLICEMODE_AVCDefault;
    m_sAVCSliceFMO.nNumSliceGroups = 0;
    m_sAVCSliceFMO.nSliceGroupMapType = 0;
    OMX_INIT_STRUCT(&m_sParamProfileLevel, OMX_VIDEO_PARAM_PROFILELEVELTYPE);
    m_sParamProfileLevel.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

    OMX_INIT_STRUCT(&m_sIntraperiod, QOMX_VIDEO_INTRAPERIODTYPE);
    m_sIntraperiod.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sIntraperiod.nPFrames = (m_sConfigFramerate.xEncodeFramerate * 2) - 1;

    OMX_INIT_STRUCT(&m_sErrorCorrection, OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE);
    m_sErrorCorrection.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sErrorCorrection.bEnableDataPartitioning = OMX_FALSE;
    m_sErrorCorrection.bEnableHEC = OMX_FALSE;
    m_sErrorCorrection.bEnableResync = OMX_FALSE;
    m_sErrorCorrection.bEnableRVLC = OMX_FALSE;
    m_sErrorCorrection.nResynchMarkerSpacing = 0;

    OMX_INIT_STRUCT(&m_sIntraRefresh, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
    m_sIntraRefresh.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sIntraRefresh.eRefreshMode = OMX_VIDEO_IntraRefreshMax;

    // Initialize the video parameters for input port
    OMX_INIT_STRUCT(&m_sInPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sInPortDef.nPortIndex= (OMX_U32) PORT_INDEX_IN;
    m_sInPortDef.bEnabled = OMX_TRUE;
    m_sInPortDef.bPopulated = OMX_FALSE;
    m_sInPortDef.eDomain = OMX_PortDomainVideo;
    m_sInPortDef.eDir = OMX_DirInput;
    m_sInPortDef.format.video.cMIMEType = (char *)"YUV420";
    m_sInPortDef.format.video.nFrameWidth = OMX_CORE_QCIF_WIDTH;
    m_sInPortDef.format.video.nFrameHeight = OMX_CORE_QCIF_HEIGHT;
    m_sInPortDef.format.video.nStride = OMX_CORE_QCIF_WIDTH;
    m_sInPortDef.format.video.nSliceHeight = OMX_CORE_QCIF_HEIGHT;
    m_sInPortDef.format.video.nBitrate = 64000;
    m_sInPortDef.format.video.xFramerate = 15 << 16;
    m_sInPortDef.format.video.eColorFormat = (OMX_COLOR_FORMATTYPE)
        QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
    m_sInPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingUnused;

    if (dev_get_buf_req(&m_sInPortDef.nBufferCountMin,
                &m_sInPortDef.nBufferCountActual,
                &m_sInPortDef.nBufferSize,
                m_sInPortDef.nPortIndex) != true) {
        eRet = OMX_ErrorUndefined;
    }

    // Initialize the video parameters for output port
    OMX_INIT_STRUCT(&m_sOutPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sOutPortDef.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sOutPortDef.bEnabled = OMX_TRUE;
    m_sOutPortDef.bPopulated = OMX_FALSE;
    m_sOutPortDef.eDomain = OMX_PortDomainVideo;
    m_sOutPortDef.eDir = OMX_DirOutput;
    m_sOutPortDef.format.video.nFrameWidth = OMX_CORE_QCIF_WIDTH;
    m_sOutPortDef.format.video.nFrameHeight = OMX_CORE_QCIF_HEIGHT;
    m_sOutPortDef.format.video.nBitrate = 64000;
    m_sOutPortDef.format.video.xFramerate = 15 << 16;
    m_sOutPortDef.format.video.eColorFormat =  OMX_COLOR_FormatUnused;
    if (codec_type ==  (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingHevc) {
        m_sOutPortDef.format.video.eCompressionFormat =   (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingHevc;
    }
    if (dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                &m_sOutPortDef.nBufferCountActual,
                &m_sOutPortDef.nBufferSize,
                m_sOutPortDef.nPortIndex) != true) {
        eRet = OMX_ErrorUndefined;
    }

    // Initialize the video color format for input port
    OMX_INIT_STRUCT(&m_sInPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sInPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_IN;
    m_sInPortFormat.nIndex = 0;
    m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
        QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
    m_sInPortFormat.eCompressionFormat = OMX_VIDEO_CodingUnused;


    // Initialize the compression format for output port
    OMX_INIT_STRUCT(&m_sOutPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sOutPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sOutPortFormat.nIndex = 0;
    m_sOutPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    if (codec_type ==  (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingHevc) {
        m_sOutPortFormat.eCompressionFormat =   (OMX_VIDEO_CODINGTYPE)QOMX_VIDEO_CodingHevc;
    };


    // mandatory Indices for kronos test suite
    OMX_INIT_STRUCT(&m_sPriorityMgmt, OMX_PRIORITYMGMTTYPE);

    OMX_INIT_STRUCT(&m_sInBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
    m_sInBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_IN;

    OMX_INIT_STRUCT(&m_sOutBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
    m_sOutBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_OUT;


    OMX_INIT_STRUCT(&m_sParamLTRMode, QOMX_VIDEO_PARAM_LTRMODE_TYPE);
    m_sParamLTRMode.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamLTRMode.eLTRMode = QOMX_VIDEO_LTRMode_Disable;

    OMX_INIT_STRUCT(&m_sParamLTRCount, QOMX_VIDEO_PARAM_LTRCOUNT_TYPE);
    m_sParamLTRCount.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamLTRCount.nCount = 0;

    OMX_INIT_STRUCT(&m_sConfigDeinterlace, OMX_VIDEO_CONFIG_DEINTERLACE);
    m_sConfigDeinterlace.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigDeinterlace.nEnable = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sHierLayers, QOMX_VIDEO_HIERARCHICALLAYERS);
    m_sHierLayers.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sHierLayers.nNumLayers = 0;
    m_sHierLayers.eHierarchicalCodingType = QOMX_HIERARCHICALCODING_P;

    m_state                   = OMX_StateLoaded;
    m_sExtraData = 0;

    if (eRet == OMX_ErrorNone) {
        if (pipe(fds)) {
            DEBUG_PRINT_ERROR("ERROR: pipe creation failed");
            eRet = OMX_ErrorInsufficientResources;
        } else {
            if (fds[0] == 0 || fds[1] == 0) {
                if (pipe(fds)) {
                    DEBUG_PRINT_ERROR("ERROR: pipe creation failed");
                    eRet = OMX_ErrorInsufficientResources;
                }
            }
            if (eRet == OMX_ErrorNone) {
                m_pipe_in = fds[0];
                m_pipe_out = fds[1];
            }
        }
        msg_thread_created = true;
        r = pthread_create(&msg_thread_id,0, enc_message_thread, this);
        if (r < 0) {
            eRet = OMX_ErrorInsufficientResources;
            msg_thread_created = false;
        }
    }

    DEBUG_PRINT_HIGH("Component_init return value = 0x%x", eRet);
    return eRet;
}


/* ======================================================================
   FUNCTION
   omx_swvenc::Setparameter

   DESCRIPTION
   OMX Set Parameter method implementation.

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if successful.

   ========================================================================== */
OMX_ERRORTYPE  omx_swvenc::set_parameter(OMX_IN OMX_HANDLETYPE     hComp,
        OMX_IN OMX_INDEXTYPE paramIndex,
        OMX_IN OMX_PTR        paramData)
{
    (void)hComp;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;


    if (m_state == OMX_StateInvalid) {
        DEBUG_PRINT_ERROR("ERROR: Set Param in Invalid State");
        return OMX_ErrorInvalidState;
    }
    if (paramData == NULL) {
        DEBUG_PRINT_ERROR("ERROR: Get Param in Invalid paramData");
        return OMX_ErrorBadParameter;
    }

    /*set_parameter can be called in loaded state
      or disabled port */
    if (m_state == OMX_StateLoaded
            || m_sInPortDef.bEnabled == OMX_FALSE
            || m_sOutPortDef.bEnabled == OMX_FALSE) {
        DEBUG_PRINT_LOW("Set Parameter called in valid state");
    } else {
        DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
        return OMX_ErrorIncorrectStateOperation;
    }

    switch ((int)paramIndex) {
        case OMX_IndexParamPortDefinition:
            {
                OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
                portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d",
                        (int)portDefn->format.video.nFrameHeight,
                        (int)portDefn->format.video.nFrameWidth);

                SWVENC_PROP prop;
                prop.ePropId = SWVENC_PROP_ID_DIMENSIONS;
                prop.uProperty.sDimensions.nWidth = portDefn->format.video.nFrameWidth;
                prop.uProperty.sDimensions.nHeight= portDefn->format.video.nFrameHeight;
                SWVENC_STATUS status = SwVenc_SetProperty(m_pSwVenc,&prop);
                if (status != SWVENC_S_SUCCESS) {
                    DEBUG_PRINT_ERROR("ERROR: (In_PORT) dimension not supported %d x %d",
                            portDefn->format.video.nFrameWidth, portDefn->format.video.nFrameHeight);
                    return OMX_ErrorUnsupportedSetting;
                }

                if (PORT_INDEX_IN == portDefn->nPortIndex) {
                    if (!dev_is_video_session_supported(portDefn->format.video.nFrameWidth,
                                portDefn->format.video.nFrameHeight)) {
                        DEBUG_PRINT_ERROR("video session not supported");
                        omx_report_unsupported_setting();
                        return OMX_ErrorUnsupportedSetting;
                    }
                    DEBUG_PRINT_LOW("i/p actual cnt requested = %u", portDefn->nBufferCountActual);
                    DEBUG_PRINT_LOW("i/p min cnt requested = %u", portDefn->nBufferCountMin);
                    DEBUG_PRINT_LOW("i/p buffersize requested = %u", portDefn->nBufferSize);
                    if (portDefn->nBufferCountMin > portDefn->nBufferCountActual) {
                        DEBUG_PRINT_ERROR("ERROR: (In_PORT) Min buffers (%u) > actual count (%u)",
                                portDefn->nBufferCountMin, portDefn->nBufferCountActual);
                        return OMX_ErrorUnsupportedSetting;
                    }

                    DEBUG_PRINT_LOW("i/p previous actual cnt = %u", m_sInPortDef.nBufferCountActual);
                    DEBUG_PRINT_LOW("i/p previous min cnt = %u", m_sInPortDef.nBufferCountMin);
                    memcpy(&m_sInPortDef, portDefn,sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
                    DEBUG_PRINT_LOW("i/p COLOR FORMAT = %u", portDefn->format.video.eColorFormat);
#ifdef _ANDROID_ICS_
                    if (portDefn->format.video.eColorFormat ==
                            (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FormatAndroidOpaque) {
                        m_sInPortDef.format.video.eColorFormat = (OMX_COLOR_FORMATTYPE)
                            QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
                        if (!mUseProxyColorFormat) {
                            if (!c2d_conv.init()) {
                                DEBUG_PRINT_ERROR("C2D init failed");
                                return OMX_ErrorUnsupportedSetting;
                            }
                            DEBUG_PRINT_LOW("C2D init is successful");
                        }
                        mUseProxyColorFormat = true;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB_OPQ;
                    } else
                        mUseProxyColorFormat = false;
#endif
                    /*Query Input Buffer Requirements*/
                    dev_get_buf_req   (&m_sInPortDef.nBufferCountMin,
                            &m_sInPortDef.nBufferCountActual,
                            &m_sInPortDef.nBufferSize,
                            m_sInPortDef.nPortIndex);

                    /*Query ouput Buffer Requirements*/
                    dev_get_buf_req   (&m_sOutPortDef.nBufferCountMin,
                            &m_sOutPortDef.nBufferCountActual,
                            &m_sOutPortDef.nBufferSize,
                            m_sOutPortDef.nPortIndex);
                    m_sInPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                } else if (PORT_INDEX_OUT == portDefn->nPortIndex) {
                    DEBUG_PRINT_LOW("o/p actual cnt requested = %u", portDefn->nBufferCountActual);
                    DEBUG_PRINT_LOW("o/p min cnt requested = %u", portDefn->nBufferCountMin);
                    DEBUG_PRINT_LOW("o/p buffersize requested = %u", portDefn->nBufferSize);
                    if (portDefn->nBufferCountMin > portDefn->nBufferCountActual) {
                        DEBUG_PRINT_ERROR("ERROR: (Out_PORT) Min buffers (%u) > actual count (%u)",
                                portDefn->nBufferCountMin, portDefn->nBufferCountActual);
                        return OMX_ErrorUnsupportedSetting;
                    }

                    /*Query ouput Buffer Requirements*/
                    dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                            &m_sOutPortDef.nBufferCountActual,
                            &m_sOutPortDef.nBufferSize,
                            m_sOutPortDef.nPortIndex);

                    memcpy(&m_sOutPortDef,portDefn,sizeof(struct OMX_PARAM_PORTDEFINITIONTYPE));
                    update_profile_level(); //framerate , bitrate

                    DEBUG_PRINT_LOW("o/p previous actual cnt = %u", m_sOutPortDef.nBufferCountActual);
                    DEBUG_PRINT_LOW("o/p previous min cnt = %u", m_sOutPortDef.nBufferCountMin);
                    m_sOutPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Set_parameter: Bad Port idx %d",
                            (int)portDefn->nPortIndex);
                    eRet = OMX_ErrorBadPortIndex;
                }
                m_sConfigFramerate.xEncodeFramerate = portDefn->format.video.xFramerate;
                m_sConfigBitrate.nEncodeBitrate = portDefn->format.video.nBitrate;
                m_sParamBitrate.nTargetBitrate = portDefn->format.video.nBitrate;
            }
            break;

        case OMX_IndexParamVideoPortFormat:
            {
                OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
                    (OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d",
                        portFmt->eColorFormat);
                //set the driver with the corresponding values
                if (PORT_INDEX_IN == portFmt->nPortIndex) {

                    DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d",
                            portFmt->eColorFormat);

                    SWVENC_PROP prop;
                    prop.uProperty.nFrameRate = portFmt->xFramerate;
                    prop.ePropId = SWVENC_PROP_ID_FRAMERATE;
                    SwVenc_SetProperty(m_pSwVenc, &prop);

                    update_profile_level(); //framerate

#ifdef _ANDROID_ICS_
                    if (portFmt->eColorFormat ==
                            (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FormatAndroidOpaque) {
                        m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
                            QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
                        if (!mUseProxyColorFormat) {
                            if (!c2d_conv.init()) {
                                DEBUG_PRINT_ERROR("C2D init failed");
                                return OMX_ErrorUnsupportedSetting;
                            }
                            DEBUG_PRINT_LOW("C2D init is successful");
                        }
                        mUseProxyColorFormat = true;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB_OPQ;
                    } else
#endif
                    {
                        m_sInPortFormat.eColorFormat = portFmt->eColorFormat;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB;
                        mUseProxyColorFormat = false;
                    }
                    m_sInPortFormat.xFramerate = portFmt->xFramerate;
                }
                //TODO if no use case for O/P port,delet m_sOutPortFormat
            }
            break;
        case OMX_IndexParamVideoInit:
            { //TODO, do we need this index set param
                OMX_PORT_PARAM_TYPE* pParam = (OMX_PORT_PARAM_TYPE*)(paramData);
                DEBUG_PRINT_LOW("Set OMX_IndexParamVideoInit called");
                break;
            }

        case OMX_IndexParamVideoBitrate:
            {
                OMX_VIDEO_PARAM_BITRATETYPE* pParam = (OMX_VIDEO_PARAM_BITRATETYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoBitrate");

                SWVENC_PROP prop;
                prop.uProperty.nFrameRate = pParam->nTargetBitrate;
                  prop.ePropId = SWVENC_PROP_ID_BITRATE;
                SwVenc_SetProperty(m_pSwVenc, &prop);

                prop.uProperty.nRcOn = pParam->eControlRate;
                prop.ePropId = SWVENC_PROP_ID_RC_ON;
                SwVenc_SetProperty(m_pSwVenc, &prop);

                m_sParamBitrate.nTargetBitrate = pParam->nTargetBitrate;
                m_sParamBitrate.eControlRate = pParam->eControlRate;
                update_profile_level(); //bitrate
                m_sConfigBitrate.nEncodeBitrate = pParam->nTargetBitrate;
                m_sInPortDef.format.video.nBitrate = pParam->nTargetBitrate;
                m_sOutPortDef.format.video.nBitrate = pParam->nTargetBitrate;
                DEBUG_PRINT_LOW("bitrate = %u", m_sOutPortDef.format.video.nBitrate);
                break;
            }
        case OMX_IndexParamVideoMpeg4:
        case OMX_IndexParamVideoH263:
        case OMX_IndexParamVideoAvc:
        case (OMX_INDEXTYPE)OMX_IndexParamVideoVp8:
            return OMX_ErrorUnsupportedSetting;
        case (OMX_INDEXTYPE)OMX_IndexParamVideoHevc:
            {
                OMX_VIDEO_PARAM_HEVCTYPE* pParam = (OMX_VIDEO_PARAM_HEVCTYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoHevc");
                if (pParam->eProfile != OMX_VIDEO_HEVCProfileMain ||
                    (pParam->eLevel != OMX_VIDEO_HEVCMainTierLevel1 &&
                     pParam->eLevel != OMX_VIDEO_HEVCMainTierLevel2 &&
                     pParam->eLevel != OMX_VIDEO_HEVCMainTierLevel21 &&
                     pParam->eLevel != OMX_VIDEO_HEVCMainTierLevel3))
                {
                    return OMX_ErrorBadParameter;
                }
                m_sParamHEVC.eProfile = OMX_VIDEO_HEVCProfileMain;
                m_sParamHEVC.eLevel = pParam->eLevel;
                break;
            }
        case OMX_IndexParamVideoProfileLevelCurrent:
            {
                OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam = (OMX_VIDEO_PARAM_PROFILELEVELTYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoProfileLevelCurrent");

                m_sParamProfileLevel.eProfile = pParam->eProfile;
                m_sParamProfileLevel.eLevel = pParam->eLevel;

                if (!strncmp((char *)m_nkind, "OMX.qti.video.encoder.hevc",\
                            OMX_MAX_STRINGNAME_SIZE)) {

                    // DEBUG_PRINT_LOW("HEVC profile = %d, level = %d");
                }
                break;
            }
        case OMX_IndexParamStandardComponentRole:
            {
                OMX_PARAM_COMPONENTROLETYPE *comp_role;
                comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamStandardComponentRole %s",
                        comp_role->cRole);

                if ((m_state == OMX_StateLoaded)&&
                        !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING)) {
                    DEBUG_PRINT_LOW("Set Parameter called in valid state");
                } else {
                    DEBUG_PRINT_ERROR("Set Parameter called in Invalid State");
                    return OMX_ErrorIncorrectStateOperation;
                }

                if (!strncmp((char*)m_nkind, "OMX.qti.video.encoder.hevc",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((char*)comp_role->cRole,"video_encoder.hevc",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.hevc",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet =OMX_ErrorUnsupportedSetting;
                    }
                }
                else {
                    DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %s", m_nkind);
                    eRet = OMX_ErrorInvalidComponentName;
                }
                break;
            }

        case OMX_IndexParamPriorityMgmt:
            {
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt");
                if (m_state != OMX_StateLoaded) {
                    DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
                    return OMX_ErrorIncorrectStateOperation;
                }
                OMX_PRIORITYMGMTTYPE *priorityMgmtype = (OMX_PRIORITYMGMTTYPE*) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt %u",
                        priorityMgmtype->nGroupID);

                DEBUG_PRINT_LOW("set_parameter: priorityMgmtype %u",
                        priorityMgmtype->nGroupPriority);

                m_sPriorityMgmt.nGroupID = priorityMgmtype->nGroupID;
                m_sPriorityMgmt.nGroupPriority = priorityMgmtype->nGroupPriority;

                break;
            }

        case OMX_IndexParamCompBufferSupplier:
            {
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier");
                OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier %d",
                        bufferSupplierType->eBufferSupplier);
                if (bufferSupplierType->nPortIndex == 0 || bufferSupplierType->nPortIndex ==1)
                    m_sInBufSupplier.eBufferSupplier = bufferSupplierType->eBufferSupplier;

                else

                    eRet = OMX_ErrorBadPortIndex;

                break;

            }
        case OMX_IndexParamVideoQuantization:
            {
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoQuantization");
                OMX_VIDEO_PARAM_QUANTIZATIONTYPE *session_qp = (OMX_VIDEO_PARAM_QUANTIZATIONTYPE*) paramData;
                if (session_qp->nPortIndex == PORT_INDEX_OUT) {
                    SWVENC_PROP prop;
                    prop.uProperty.nQp = session_qp->nQpI;
                    prop.ePropId = SWVENC_PROP_ID_QP;
                    SwVenc_SetProperty(m_pSwVenc, &prop);

                    m_sSessionQuantization.nQpI = session_qp->nQpI;
                    m_sSessionQuantization.nQpP = session_qp->nQpP;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for Session QP setting");
                    eRet = OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_QcomIndexParamVideoQPRange:
            {
                DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexParamVideoQPRange");
                OMX_QCOM_VIDEO_PARAM_QPRANGETYPE *qp_range = (OMX_QCOM_VIDEO_PARAM_QPRANGETYPE*) paramData;
                if (qp_range->nPortIndex == PORT_INDEX_OUT) {
                    m_sSessionQPRange.minQP= qp_range->minQP;
                    m_sSessionQPRange.maxQP= qp_range->maxQP;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for QP range setting");
                    eRet = OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_QcomIndexPortDefn:
            {
                OMX_QCOM_PARAM_PORTDEFINITIONTYPE* pParam =
                    (OMX_QCOM_PARAM_PORTDEFINITIONTYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexPortDefn");
                if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_IN) {
                    if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                            pParam->nMemRegion < OMX_QCOM_MemRegionMax) {
                        m_use_input_pmem = OMX_TRUE;
                    } else {
                        m_use_input_pmem = OMX_FALSE;
                    }
                } else if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                            pParam->nMemRegion < OMX_QCOM_MemRegionMax) {
                        m_use_output_pmem = OMX_TRUE;
                    } else {
                        m_use_output_pmem = OMX_FALSE;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: SetParameter called on unsupported Port Index for QcomPortDefn");
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_IndexParamVideoErrorCorrection:
            {
                DEBUG_PRINT_LOW("OMX_IndexParamVideoErrorCorrection");
                OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam =
                    (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE*)paramData;
                memcpy(&m_sErrorCorrection,pParam, sizeof(m_sErrorCorrection));
                break;
            }
        case OMX_IndexParamVideoIntraRefresh:
            {
                DEBUG_PRINT_LOW("set_param:OMX_IndexParamVideoIntraRefresh");
                OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam =
                    (OMX_VIDEO_PARAM_INTRAREFRESHTYPE*)paramData;

                memcpy(&m_sIntraRefresh, pParam, sizeof(m_sIntraRefresh));
                break;
            }

        case OMX_QcomIndexParamVideoMetaBufferMode:
            {
                StoreMetaDataInBuffersParams *pParam =
                    (StoreMetaDataInBuffersParams*)paramData;
                DEBUG_PRINT_HIGH("set_parameter:OMX_QcomIndexParamVideoMetaBufferMode: "
                    "port_index = %u, meta_mode = %d", pParam->nPortIndex, pParam->bStoreMetaData);
                if (pParam->nPortIndex == PORT_INDEX_IN)
                {
                    meta_mode_enable = pParam->bStoreMetaData;
                }
                else
                {
                    if (pParam->bStoreMetaData)
                    {
                        DEBUG_PRINT_ERROR("set_parameter: metamode is "
                            "valid for input port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                    }
                }
            }
        break;
        default:
            {
                DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %d", paramIndex);
                eRet = OMX_ErrorUnsupportedIndex;
                break;
            }
    }
    return eRet;
}

bool omx_swvenc::update_profile_level()
{
    if (!strncmp((char *)m_nkind, "OMX.qti.video.encoder.hevc",\
                OMX_MAX_STRINGNAME_SIZE))
    {
        if (m_sParamHEVC.eProfile != OMX_VIDEO_HEVCProfileMain)
        {
            return false;
        }
        SWVENC_PROP prop;
        prop.ePropId = SWVENC_PROP_ID_PROFILE;
        prop.uProperty.nProfile = SWVENC_HEVC_MAIN_PROFILE;
        if (SwVenc_SetProperty(m_pSwVenc, &prop) != SWVENC_S_SUCCESS)
        {
            DEBUG_PRINT_ERROR("ERROR: failed to set profile");
        }

        int level = 0;
        if (m_sParamHEVC.eLevel == OMX_VIDEO_HEVCMainTierLevel1)
        {
            level = SWVENC_HEVC_LEVEL_1;
        }
        else if (m_sParamHEVC.eLevel == OMX_VIDEO_HEVCMainTierLevel2)
        {
            level = SWVENC_HEVC_LEVEL_2;
        }
        else if (m_sParamHEVC.eLevel == OMX_VIDEO_HEVCMainTierLevel21)
        {
            level = SWVENC_HEVC_LEVEL_2_1;
        }
        else if (m_sParamHEVC.eLevel == OMX_VIDEO_HEVCMainTierLevel3)
        {
            level = SWVENC_HEVC_LEVEL_3;
        }

        if (level)
        {
            prop.ePropId = SWVENC_PROP_ID_LEVEL;
            prop.uProperty.nLevel = (SWVENC_HEVC_LEVEL)level;
            if (SwVenc_SetProperty(m_pSwVenc, &prop) != SWVENC_S_SUCCESS)
            {
                DEBUG_PRINT_ERROR("ERROR: failed to set level %d", level);
            }
        }
    }

    return true;
}
/* ======================================================================
   FUNCTION
   omx_video::SetConfig

   DESCRIPTION
   OMX Set Config method implementation

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if successful.
   ========================================================================== */
OMX_ERRORTYPE  omx_swvenc::set_config(OMX_IN OMX_HANDLETYPE      hComp,
        OMX_IN OMX_INDEXTYPE configIndex,
        OMX_IN OMX_PTR        configData)
{
    (void)hComp;
    if (configData == NULL) {
        DEBUG_PRINT_ERROR("ERROR: param is null");
        return OMX_ErrorBadParameter;
    }

    if (m_state == OMX_StateInvalid) {
        DEBUG_PRINT_ERROR("ERROR: config called in Invalid state");
        return OMX_ErrorIncorrectStateOperation;
    }

    // params will be validated prior to venc_init
    switch ((int)configIndex) {
        case OMX_IndexConfigVideoBitrate:
            {
                OMX_VIDEO_CONFIG_BITRATETYPE* pParam =
                    reinterpret_cast<OMX_VIDEO_CONFIG_BITRATETYPE*>(configData);
                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoBitrate (%u)", pParam->nEncodeBitrate);

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    SWVENC_PROP prop;
                    prop.uProperty.nBitrate = pParam->nEncodeBitrate;
                    prop.ePropId = SWVENC_PROP_ID_BITRATE;
                    SwVenc_SetProperty(m_pSwVenc, &prop);


                    m_sConfigBitrate.nEncodeBitrate = pParam->nEncodeBitrate;
                    m_sParamBitrate.nTargetBitrate = pParam->nEncodeBitrate;
                    m_sOutPortDef.format.video.nBitrate = pParam->nEncodeBitrate;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }
        case OMX_IndexConfigVideoFramerate:
            {
                OMX_CONFIG_FRAMERATETYPE* pParam =
                    reinterpret_cast<OMX_CONFIG_FRAMERATETYPE*>(configData);
                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoFramerate (0x%x)", pParam->xEncodeFramerate);

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    SWVENC_PROP prop;
                    prop.uProperty.nFrameRate = pParam->xEncodeFramerate;
                    prop.ePropId = SWVENC_PROP_ID_FRAMERATE;
                    SwVenc_SetProperty(m_pSwVenc, &prop);

                    m_sConfigFramerate.xEncodeFramerate = pParam->xEncodeFramerate;
                    m_sOutPortDef.format.video.xFramerate = pParam->xEncodeFramerate;
                    m_sOutPortFormat.xFramerate = pParam->xEncodeFramerate;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }
        case QOMX_IndexConfigVideoIntraperiod:
            {
                QOMX_VIDEO_INTRAPERIODTYPE* pParam =
                    reinterpret_cast<QOMX_VIDEO_INTRAPERIODTYPE*>(configData);

                DEBUG_PRINT_HIGH("set_config(): QOMX_IndexConfigVideoIntraperiod");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                   if (pParam->nBFrames > 0) {
                        DEBUG_PRINT_ERROR("B frames not supported");
                        return OMX_ErrorUnsupportedSetting;
                    }
                    DEBUG_PRINT_HIGH("Old: P/B frames = %u/%u, New: P/B frames = %u/%u",
                            m_sIntraperiod.nPFrames, m_sIntraperiod.nBFrames,
                            pParam->nPFrames, pParam->nBFrames);
                    if (m_sIntraperiod.nBFrames != pParam->nBFrames) {
                        DEBUG_PRINT_HIGH("Dynamically changing B-frames not supported");
                        return OMX_ErrorUnsupportedSetting;
                    }

                    SWVENC_PROP prop;
                    prop.uProperty.sIntraPeriod.pFrames = pParam->nPFrames;
                    prop.uProperty.sIntraPeriod.bFrames = pParam->nBFrames;
                    prop.ePropId = SWVENC_PROP_ID_INTRA_PERIOD;
                    SwVenc_SetProperty(m_pSwVenc, &prop);

                    m_sIntraperiod.nPFrames = pParam->nPFrames;
                    m_sIntraperiod.nBFrames = pParam->nBFrames;
                    m_sIntraperiod.nIDRPeriod = pParam->nIDRPeriod;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: (QOMX_IndexConfigVideoIntraperiod) Unsupported port index: %u", pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }

        case OMX_IndexConfigVideoIntraVOPRefresh:
            {
                OMX_CONFIG_INTRAREFRESHVOPTYPE* pParam =
                    reinterpret_cast<OMX_CONFIG_INTRAREFRESHVOPTYPE*>(configData);

                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoIntraVOPRefresh");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    SWVENC_PROP prop;
                    prop.ePropId = SWVENC_PROP_ID_IDR_INSERTION;
                    SwVenc_SetProperty(m_pSwVenc, &prop);
                    DEBUG_PRINT_HIGH("Setting SWVENC OMX_IndexConfigVideoIntraVOPRefresh");
                    m_sConfigIntraRefreshVOP.IntraRefreshVOP = pParam->IntraRefreshVOP;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }
        case OMX_QcomIndexConfigVideoFramePackingArrangement:
            {
                DEBUG_PRINT_HIGH("set_config(): OMX_QcomIndexConfigVideoFramePackingArrangement");
                if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingAVC) {
                    OMX_QCOM_FRAME_PACK_ARRANGEMENT *configFmt =
                        (OMX_QCOM_FRAME_PACK_ARRANGEMENT *) configData;
                    extra_data_handle.set_frame_pack_data(configFmt);
                } else {
                    DEBUG_PRINT_ERROR("ERROR: FramePackingData not supported for non AVC compression");
                }
                break;
            }
        default:
            DEBUG_PRINT_ERROR("ERROR: unsupported index %d", (int) configIndex);
            return OMX_ErrorUnsupportedSetting;
    }

    return OMX_ErrorNone;
}

/* ======================================================================
   FUNCTION
   omx_swvenc::ComponentDeInit

   DESCRIPTION
   Destroys the component and release memory allocated to the heap.

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if everything successful.

   ========================================================================== */
OMX_ERRORTYPE  omx_swvenc::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    (void) hComp;
    OMX_U32 i = 0;
    DEBUG_PRINT_HIGH("omx_swvenc(): Inside component_deinit()");
    if (OMX_StateLoaded != m_state) {
        DEBUG_PRINT_ERROR("WARNING:Rxd DeInit,OMX not in LOADED state %d",\
                m_state);
    }
    if (m_out_mem_ptr) {
        DEBUG_PRINT_LOW("Freeing the Output Memory");
        for (i=0; i< m_sOutPortDef.nBufferCountActual; i++ ) {
            free_output_buffer (&m_out_mem_ptr[i]);
        }
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
    }

    /*Check if the input buffers have to be cleaned up*/
    if (m_inp_mem_ptr
#ifdef _ANDROID_ICS_
            && !meta_mode_enable
#endif
       ) {
        DEBUG_PRINT_LOW("Freeing the Input Memory");
        for (i=0; i<m_sInPortDef.nBufferCountActual; i++ ) {
            free_input_buffer (&m_inp_mem_ptr[i]);
        }


        free(m_inp_mem_ptr);
        m_inp_mem_ptr = NULL;
    }

    // Reset counters in mesg queues
    m_ftb_q.m_size=0;
    m_cmd_q.m_size=0;
    m_etb_q.m_size=0;
    m_ftb_q.m_read = m_ftb_q.m_write =0;
    m_cmd_q.m_read = m_cmd_q.m_write =0;
    m_etb_q.m_read = m_etb_q.m_write =0;

#ifdef _ANDROID_
    // Clear the strong reference
    DEBUG_PRINT_HIGH("Calling m_heap_ptr.clear()");
    m_heap_ptr.clear();
#endif // _ANDROID_

    DEBUG_PRINT_HIGH("Calling SwVenc_Stop()");
    SWVENC_STATUS ret = SwVenc_Stop(m_pSwVenc);
    if (ret != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("SwVenc_Stop Command failed in venc destructor");
    }

    DEBUG_PRINT_HIGH("Deleting m_pSwVenc HANDLE[%p]", m_pSwVenc);
    SwVenc_DeInit(m_pSwVenc);
    m_pSwVenc = NULL;

    DEBUG_PRINT_HIGH("omx_swvenc:Component Deinit");
    return OMX_ErrorNone;
}


OMX_U32 omx_swvenc::dev_stop( void)
{
    SwVenc_Stop(m_pSwVenc);
    post_event (0,0,OMX_COMPONENT_GENERATE_STOP_DONE);
    return SWVENC_S_SUCCESS;
}


OMX_U32 omx_swvenc::dev_pause(void)
{
    return SWVENC_S_SUCCESS;
}

OMX_U32 omx_swvenc::dev_start(void)
{
    SwVenc_Start(m_pSwVenc);
    post_event (0,0,OMX_COMPONENT_GENERATE_START_DONE);
    return SWVENC_S_SUCCESS;
}

OMX_U32 omx_swvenc::dev_flush(unsigned port)
{
    if (port == PORT_INDEX_IN)
    {
        return SWVENC_S_EUNSUPPORTED;
    }

    DEBUG_PRINT_HIGH("SwVenc_Flush port %d", port);
    return SwVenc_Flush(m_pSwVenc);
}

OMX_U32 omx_swvenc::dev_resume(void)
{
    return SWVENC_S_SUCCESS;
}

OMX_U32 omx_swvenc::dev_start_done(void)
{
    return SWVENC_S_SUCCESS;
}

OMX_U32 omx_swvenc::dev_set_message_thread_id(pthread_t tid)
{
    (void) tid;
    return SWVENC_S_SUCCESS;
}

bool omx_swvenc::dev_use_buf(void *buf_addr,unsigned port,unsigned index)
{
    struct pmem* buf = (struct pmem*)buf_addr;
    unsigned long index1 = (unsigned long) index;
    if (port == PORT_INDEX_IN)
    {
        // m_pSwVencIpBuffer[index].nSize = buf->size;
        m_pSwVencIpBuffer[index].pBuffer = (unsigned char*)buf->buffer;
        m_pSwVencIpBuffer[index].pClientBufferData = (void*)index1;

        DEBUG_PRINT_LOW("dev_use_buf input %p, index %d userData %p",
            m_pSwVencIpBuffer[index].pBuffer, index, m_pSwVencIpBuffer[index].pClientBufferData);
    }
    else
    {
        m_pSwVencOpBuffer[index].nSize = buf->size;
        m_pSwVencOpBuffer[index].pBuffer = (unsigned char*)buf->buffer;
        m_pSwVencOpBuffer[index].pClientBufferData = (void *)index1;
        DEBUG_PRINT_LOW("dev_use_buf output %p, index %d userData %p",
            m_pSwVencIpBuffer[index].pBuffer, index, m_pSwVencIpBuffer[index].pClientBufferData);
    }
    return true;
}

bool omx_swvenc::dev_free_buf(void *buf_addr,unsigned port)
{
    struct pmem* buf = (struct pmem*)buf_addr;
    int i = 0;
    if (port == PORT_INDEX_IN)
    {
        for (; i<32;i++)
        {
            if (m_pSwVencIpBuffer[i].pBuffer == buf->buffer)
            {
                m_pSwVencIpBuffer[i].pBuffer = NULL;
                // m_pSwVencIpBuffer[i].nSize = 0;
            }
        }
    }
    else
    {
        for (; i<32;i++)
        {
            if (m_pSwVencOpBuffer[i].pBuffer == buf->buffer)
            {
                m_pSwVencOpBuffer[i].pBuffer = NULL;
                m_pSwVencOpBuffer[i].nSize = 0;
            }
        }
    }
    return true;
}

void dump_buffer(unsigned char* buffer, int stride, int scanlines, int width, int height)
{
    static FILE* pFile = NULL;
    static int count = 0;
    if (count++ >= 100) return;

    if (pFile == NULL)
    {
        pFile = fopen("/data/input.yuv", "wb");
        if(!pFile) {
            DEBUG_PRINT_ERROR("%s : Error opening file!",__func__);
        }
    }
    if (buffer && pFile)
    {
        char *temp = (char *)buffer;
        int i;
        int bytes_written = 0;
        int bytes = 0;

        for (i = 0; i < height; i++) {
            bytes_written = fwrite(temp, width, 1, pFile);
            temp += stride;
            if (bytes_written >0)
                bytes += bytes_written * width;
        }
        temp = (char *)buffer + stride * scanlines;
        int stride_c = stride;
        for(i = 0; i < height/2; i++) {
            bytes_written = fwrite(temp, width, 1, pFile);
            temp += stride_c;
            if (bytes_written >0)
                bytes += bytes_written * width;
        }

        DEBUG_PRINT_ERROR("stride %d, scanlines %d, frame_height %d bytes_written %d",
            stride, scanlines, height, bytes);
    }
}

static FILE* gYUV = NULL;

bool omx_swvenc::dev_empty_buf(void *buffer, void *pmem_data_buf,unsigned index,unsigned fd)
{
    SWVENC_STATUS status;
    SWVENC_IPBUFFER ipbuffer;
    (void) pmem_data_buf;
    OMX_BUFFERHEADERTYPE *bufHdr = (OMX_BUFFERHEADERTYPE *)buffer;

    if (meta_mode_enable)
    {
       unsigned int size = 0, offset = 0;
       encoder_media_buffer_type *meta_buf = NULL;
       meta_buf = (encoder_media_buffer_type *)bufHdr->pBuffer;
       if (meta_buf)
       {
          if (meta_buf->buffer_type == kMetadataBufferTypeCameraSource)
          {
              offset = meta_buf->meta_handle->data[1];
              size = meta_buf->meta_handle->data[2];
          }
          else if (meta_buf->buffer_type == kMetadataBufferTypeGrallocSource)
          {
              private_handle_t *handle = (private_handle_t *)meta_buf->meta_handle;
              size = handle->size;
          }
       }

       ipbuffer.pBuffer = (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, offset);
       ipbuffer.nFilledLen = size;
       DEBUG_PRINT_LOW("mapped meta buf fd %d size %d %p", fd, size, ipbuffer.pBuffer);
    }
    else
    {
       ipbuffer.pBuffer = bufHdr->pBuffer;
       ipbuffer.nFilledLen = bufHdr->nFilledLen;
    }

    ipbuffer.nFlags = bufHdr->nFlags;
    ipbuffer.nIpTimestamp = bufHdr->nTimeStamp;
    ipbuffer.pClientBufferData = (unsigned char *)bufHdr;

    DEBUG_PRINT_LOW("SwVenc_EmptyThisBuffer index %d pBuffer %p", index, ipbuffer.pBuffer);
    status = SwVenc_EmptyThisBuffer(m_pSwVenc, &ipbuffer);

    if (status != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("SwVenc_EmptyThisBuffer failed");
        post_event ((unsigned long)buffer,0,OMX_COMPONENT_GENERATE_EBD);
        pending_output_buffers--;
    }

    return status == SWVENC_S_SUCCESS;
}

bool omx_swvenc::dev_fill_buf(void *buffer, void *pmem_data_buf,unsigned index,unsigned fd)
{
    SWVENC_STATUS status;
    (void) fd;
    OMX_BUFFERHEADERTYPE* bufHdr = (OMX_BUFFERHEADERTYPE*)buffer;

    DEBUG_PRINT_LOW("SwVenc_FillThisBuffer index %d pBuffer %p pmem_data_buf %p",
        index, bufHdr->pBuffer, pmem_data_buf);
    status = SwVenc_FillThisBuffer(m_pSwVenc, &m_pSwVencOpBuffer[index]);

    if (status != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("SwVenc_FillThisBuffer failed");
        post_event ((unsigned long)buffer,0,OMX_COMPONENT_GENERATE_FBD);
        pending_output_buffers--;
    }

    return status == SWVENC_S_SUCCESS;
}

bool omx_swvenc::dev_get_seq_hdr(void *buffer, unsigned size, unsigned *hdrlen)
{
    (void) buffer;
    (void) size;
    (void) hdrlen;
    return false;
}

bool omx_swvenc::dev_get_capability_ltrcount(OMX_U32 *min, OMX_U32 *max, OMX_U32 *step_size)
{
    (void) min;
    (void) max;
    (void) step_size;
    return true;
}

bool omx_swvenc::dev_loaded_start()
{
    return true;
}

bool omx_swvenc::dev_loaded_stop()
{
    return true;
}

bool omx_swvenc::dev_loaded_start_done()
{
    return true;
}

bool omx_swvenc::dev_loaded_stop_done()
{
    return true;
}


bool omx_swvenc::dev_get_performance_level(OMX_U32 *perflevel)
{
    (void) perflevel;
    DEBUG_PRINT_ERROR("Get performance level is not supported");
    return false;
}

bool omx_swvenc::dev_get_vui_timing_info(OMX_U32 *enabled)
{
    (void) enabled;
    DEBUG_PRINT_ERROR("Get vui timing information is not supported");
    return false;
}

bool omx_swvenc::dev_get_peak_bitrate(OMX_U32 *peakbitrate)
{
    (void) peakbitrate;
    DEBUG_PRINT_ERROR("Get peak bitrate is not supported");
    return false;
}

bool omx_swvenc::dev_get_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    SWVENC_STATUS sRet = SWVENC_S_SUCCESS;
    SWVENC_PROP property;
    property.ePropId = (port == 0) ? SWVENC_PROP_ID_IPBUFFREQ : SWVENC_PROP_ID_OPBUFFREQ;

    sRet = SwVenc_GetProperty(m_pSwVenc, &property);
    if (sRet == SWVENC_S_SUCCESS)
    {
        if (port == 0)
        {
            *min_buff_count = property.uProperty.sIpBuffReq.nMinCount;
            *buff_size = property.uProperty.sIpBuffReq.nSize;
            *actual_buff_count = property.uProperty.sIpBuffReq.nMinCount;
            DEBUG_PRINT_HIGH("SwVenc input buffer Size =%u Count = %u", *buff_size, *actual_buff_count);
        }
        else
        {
            *min_buff_count = property.uProperty.sOpBuffReq.nMinCount;
            *buff_size = property.uProperty.sOpBuffReq.nSize;
            *actual_buff_count = property.uProperty.sOpBuffReq.nMinCount;
            DEBUG_PRINT_HIGH("SwVenc output buffer Size =%u Count = %u", property.uProperty.sOpBuffReq.nSize, property.uProperty.sOpBuffReq.nMinCount);
        }
    }

    return (sRet == SWVENC_S_SUCCESS);
}

bool omx_swvenc::dev_set_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    SWVENC_PROP property;
    SWVENC_STATUS sRet = SWVENC_S_SUCCESS;

    if (port != PORT_INDEX_IN || port != PORT_INDEX_OUT) return false;
    if (*min_buff_count > *actual_buff_count) return false;

    if(port == PORT_INDEX_IN)
    {
        property.ePropId = SWVENC_PROP_ID_IPBUFFREQ;
        property.uProperty.sIpBuffReq.nSize = *buff_size;;
        property.uProperty.sIpBuffReq.nMaxCount = *actual_buff_count;
        property.uProperty.sIpBuffReq.nMinCount = *actual_buff_count;
        DEBUG_PRINT_HIGH("Set SwVenc input Buffer Size =%d Count = %d",property.uProperty.sIpBuffReq.nSize, *actual_buff_count);
    }
    else if (port == PORT_INDEX_OUT)
    {
        property.ePropId = SWVENC_PROP_ID_OPBUFFREQ;
        property.uProperty.sOpBuffReq.nSize = *buff_size;
        property.uProperty.sOpBuffReq.nMaxCount = *actual_buff_count;
        property.uProperty.sOpBuffReq.nMinCount = *actual_buff_count;
        DEBUG_PRINT_HIGH("Set SwVenc output Buffer Size =%d and Count = %d",property.uProperty.sOpBuffReq.nSize, *actual_buff_count);
    }

    sRet = SwVenc_SetProperty(m_pSwVenc, &property);
    if (sRet != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("Set buffer requirements from ARM codec failed");
    }

    return sRet == SWVENC_S_SUCCESS;
}

bool omx_swvenc::dev_is_video_session_supported(OMX_U32 width, OMX_U32 height)
{
    if ((width * height) > (1280 * 720))
        return false;
    return true;
}


int omx_swvenc::dev_handle_extradata(void *buffer, int index)
{
    (void) buffer;
    (void) index;
    return SWVENC_S_EUNSUPPORTED;
}

int omx_swvenc::dev_set_format(int color)
{
    (void) color;
    return SWVENC_S_SUCCESS;
}

bool omx_swvenc::dev_color_align(OMX_BUFFERHEADERTYPE *buffer,
                OMX_U32 width, OMX_U32 height)
{
    (void) buffer;
    (void) width;
    (void) height;
    if(secure_session) {
        DEBUG_PRINT_ERROR("Cannot align colors in secure session.");
        return OMX_FALSE;
    }
    return true;
}

bool omx_swvenc::is_secure_session()
{
    return secure_session;
}

bool omx_swvenc::dev_get_output_log_flag()
{
    return false;
}

int omx_swvenc::dev_output_log_buffers(const char *buffer, int bufferlen)
{
    (void) buffer;
    (void) bufferlen;
    return 0;
}

int omx_swvenc::dev_extradata_log_buffers(char *buffer)
{
    (void) buffer;
    return 0;
}

SWVENC_STATUS omx_swvenc::swvenc_input_buffer_done_cb
(
    SWVENC_HANDLE pSwEnc,
    SWVENC_IPBUFFER *pIpBuffer,
    void *pClientHandle
)
{
    (void) pSwEnc;
    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    omx_swvenc *omx = reinterpret_cast<omx_swvenc*>(pClientHandle);

    if (pIpBuffer == NULL)
    {
        eRet = SWVENC_S_EFAIL;
    }
    else
    {
        omx->swvenc_input_buffer_done(pIpBuffer);
    }

    return eRet;
}

void omx_swvenc::swvenc_input_buffer_done(SWVENC_IPBUFFER *pIpBuffer)
{
    OMX_BUFFERHEADERTYPE *bufHdr = (OMX_BUFFERHEADERTYPE *)pIpBuffer->pClientBufferData;

    if (meta_mode_enable)
    {
        omx_release_meta_buffer(bufHdr);

        // fd is not dupped, nothing needs to be done here
        munmap(pIpBuffer->pBuffer, pIpBuffer->nFilledLen);
    }

    DEBUG_PRINT_LOW("swvenc_empty_buffer_done bufHdr %p pBuffer %p = %p nFilledLen %d nFlags %x",
        bufHdr, bufHdr->pBuffer, pIpBuffer->pBuffer, pIpBuffer->nFilledLen, pIpBuffer->nFlags);

    post_event((unsigned long)(bufHdr), SWVENC_S_SUCCESS, OMX_COMPONENT_GENERATE_EBD);
}

SWVENC_STATUS omx_swvenc::swvenc_fill_buffer_done_cb
(
    SWVENC_HANDLE pSwEnc,
    SWVENC_OPBUFFER *m_pSWVencOpBuffer,
    void *pClientHandle
)
{
    (void) pSwEnc;
    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    omx_swvenc *omx = reinterpret_cast<omx_swvenc*>(pClientHandle);

    if (m_pSWVencOpBuffer == NULL)
    {
        eRet = SWVENC_S_EFAIL;
    }
    else
    {
        omx->swvenc_fill_buffer_done(m_pSWVencOpBuffer);
    }
    return eRet;
}

void omx_swvenc::swvenc_fill_buffer_done(SWVENC_OPBUFFER *pOpBuffer)
{
    unsigned long index = (unsigned long)pOpBuffer->pClientBufferData;
    OMX_BUFFERHEADERTYPE *bufHdr = m_out_mem_ptr + index;

    bufHdr->nOffset = 0;
    bufHdr->nFilledLen = pOpBuffer->nFilledLen;
    bufHdr->nFlags = pOpBuffer->nFlags;
    bufHdr->nTimeStamp = pOpBuffer->nOpTimestamp;

    if (bufHdr->nFlags & OMX_BUFFERFLAG_EOS)
    {
        DEBUG_PRINT_ERROR("swvenc output EOS reached\n");
    }

    /*Use buffer case*/
    if (output_use_buffer && !m_use_output_pmem &&
        !output_flush_progress && pOpBuffer->nFilledLen)
    {
        DEBUG_PRINT_LOW("memcpy for o/p Heap UseBuffer size %d", pOpBuffer->nFilledLen);
        memcpy(bufHdr->pBuffer, pOpBuffer->pBuffer, pOpBuffer->nFilledLen);
    }

    DEBUG_PRINT_LOW("swvenc_fill_buffer_done bufHdr %p pBuffer %p = %p idx %d nFilledLen %d nFlags %x\n",
        bufHdr, bufHdr->pBuffer, pOpBuffer->pBuffer, index, pOpBuffer->nFilledLen, pOpBuffer->nFlags);
    post_event((unsigned long)bufHdr, SWVENC_S_SUCCESS, OMX_COMPONENT_GENERATE_FBD);
}

SWVENC_STATUS omx_swvenc::swvenc_handle_event_cb
(
    SWVENC_HANDLE pSwEnc,
    SWVENC_EVENTHANDLER* pEventHandler,
    void *pClientHandle
)
{
   (void) pSwEnc;
    omx_swvenc *omx = reinterpret_cast<omx_swvenc*>(pClientHandle);
    omx->swvenc_handle_event(pEventHandler);
    return SWVENC_S_SUCCESS;
}

void omx_swvenc::swvenc_handle_event(SWVENC_EVENTHANDLER *pEvent)
{
    switch(pEvent->eEvent)
    {
    case SWVENC_FLUSH_DONE:
        DEBUG_PRINT_HIGH("SWVENC_FLUSH_DONE input_flush_progress %d output_flush_progress %d\n",
            input_flush_progress, output_flush_progress);
        if (input_flush_progress)
        {
            post_event ((unsigned)NULL, SWVENC_S_SUCCESS, OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH);
        }
        if (output_flush_progress)
        {
            post_event ((unsigned)NULL, SWVENC_S_SUCCESS, OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
        }
        break;
    case SWVENC_ERROR:
        break;

    default:
        break;
    }

}


