/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "qcom_audio_hw_hal"
//#define LOG_NDEBUG 0

#include <stdint.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <hardware_legacy/AudioHardwareInterface.h>
#include <hardware_legacy/AudioSystemLegacy.h>

namespace android_audio_legacy {

extern "C" {

struct qcom_audio_module {
    struct audio_module module;
};

struct qcom_audio_device {
    struct audio_hw_device device;

    struct AudioHardwareInterface *hwif;
};

struct qcom_stream_out {
    struct audio_stream_out stream;

    AudioStreamOut *qcom_out;
};

struct qcom_stream_in {
    struct audio_stream_in stream;

    AudioStreamIn *qcom_in;
};

/** audio_stream_out implementation **/
static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->sampleRate();
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);

    LOGE("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement this */
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->bufferSize();
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->channels();
}

static int out_get_format(const struct audio_stream *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->format();
}

static int out_set_format(struct audio_stream *stream, int format)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    LOGE("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement me */
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->standby();
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    Vector<String16> args;
    return out->qcom_out->dump(fd, args);
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->setParameters(String8(kvpairs));
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    String8 s8;
    s8 = out->qcom_out->getParameters(String8(keys));
    return strdup(s8.string());
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->latency();
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->setVolume(left, right);
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    struct qcom_stream_out *out =
        reinterpret_cast<struct qcom_stream_out *>(stream);
    return out->qcom_out->write(buffer, bytes);
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    const struct qcom_stream_out *out =
        reinterpret_cast<const struct qcom_stream_out *>(stream);
    return out->qcom_out->getRenderPosition(dsp_frames);
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->sampleRate();
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);

    LOGE("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement this */
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->bufferSize();
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->channels();
}

static int in_get_format(const struct audio_stream *stream)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->format();
}

static int in_set_format(struct audio_stream *stream, int format)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    LOGE("(%s:%d) %s: Implement me!", __FILE__, __LINE__, __func__);
    /* TODO: implement me */
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct qcom_stream_in *in = reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->standby();
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    Vector<String16> args;
    return in->qcom_in->dump(fd, args);
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->setParameters(String8(kvpairs));
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    String8 s8;
    s8 = in->qcom_in->getParameters(String8(keys));
    return strdup(s8.string());
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->setGain(gain);
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->read(buffer, bytes);
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);
    return in->qcom_in->getInputFramesLost();
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->addAudioEffect(effect);
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    const struct qcom_stream_in *in =
        reinterpret_cast<const struct qcom_stream_in *>(stream);
    return in->qcom_in->removeAudioEffect(effect);
}

/** audio_hw_device implementation **/
static inline struct qcom_audio_device * to_ladev(struct audio_hw_device *dev)
{
    return reinterpret_cast<struct qcom_audio_device *>(dev);
}

static inline const struct qcom_audio_device * to_cladev(const struct audio_hw_device *dev)
{
    return reinterpret_cast<const struct qcom_audio_device *>(dev);
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    /* XXX: The old AudioHardwareInterface interface is not smart enough to
     * tell us this, so we'll lie and basically tell AF that we support the
     * below input/output devices and cross our fingers. To do things properly,
     * audio hardware interfaces that need advanced features (like this) should
     * convert to the new HAL interface and not use this wrapper. */
	 //return (AUDIO_DEVICE_OUT_DEFAULT | AUDIO_DEVICE_OUT_DEFAULT);
    return (/* OUT */
            AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_ALL_SCO |
#ifdef HAVE_FM_RADIO
            AUDIO_DEVICE_OUT_FM |
            AUDIO_DEVICE_OUT_FM_TX |
            AUDIO_DEVICE_OUT_DIRECTOUTPUT |
#endif
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_VOICE_CALL |
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_AUX_DIGITAL |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_ALL_SCO |
#ifdef HAVE_FM_RADIO
            AUDIO_DEVICE_IN_FM_RX |
            AUDIO_DEVICE_IN_FM_RX_A2DP |
#endif
            AUDIO_DEVICE_IN_DEFAULT);
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);

    return qadev->hwif->initCheck();
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setVoiceVolume(volume);
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setMasterVolume(volume);
}

#ifdef HAVE_FM_RADIO
static int adev_set_fm_volume(struct audio_hw_device *dev, float volume)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setFmVolume(volume);
}
#endif

static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setMode(mode);
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setMicMute(state);
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    return qadev->hwif->getMicMute(state);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    return qadev->hwif->setParameters(String8(kvpairs));
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    String8 s8;

    s8 = qadev->hwif->getParameters(String8(keys));
    return strdup(s8.string());
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         uint32_t sample_rate, int format,
                                         int channel_count)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    return qadev->hwif->getInputBufferSize(sample_rate, format, channel_count);
}

#ifdef WITH_QCOM_LPA
static int adev_open_output_session(struct audio_hw_device *dev,
                                   uint32_t devices,
                                   int *format,
                                   int sessionId,
                                   struct audio_stream_out **stream_out)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    status_t status;
    struct qcom_stream_out *out;
    int ret;

    out = (struct qcom_stream_out *)calloc(1, sizeof(*out));
    if (!out)
        return -ENOMEM;

    out->qcom_out = qadev->hwif->openOutputSession(devices, format,&status,sessionId);
    if (!out->qcom_out) {
        ret = status;
        goto err_open;
    }

    out->stream.common.standby = out_standby;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.set_volume = out_set_volume;

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}
#endif
static int adev_open_output_stream(struct audio_hw_device *dev,
                                   uint32_t devices,
                                   int *format,
                                   uint32_t *channels,
                                   uint32_t *sample_rate,
                                   struct audio_stream_out **stream_out)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    status_t status;
    struct qcom_stream_out *out;
    int ret;

    out = (struct qcom_stream_out *)calloc(1, sizeof(*out));
    if (!out)
        return -ENOMEM;

    out->qcom_out = qadev->hwif->openOutputStream(devices, format, channels,
                                                    sample_rate, &status);
    if (!out->qcom_out) {
        ret = status;
        goto err_open;
    }

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out* stream)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    struct qcom_stream_out *out = reinterpret_cast<struct qcom_stream_out *>(stream);

    qadev->hwif->closeOutputStream(out->qcom_out);
    free(out);
}

/** This method creates and opens the audio hardware input stream */
static int adev_open_input_stream(struct audio_hw_device *dev,
                                  uint32_t devices, int *format,
                                  uint32_t *channels, uint32_t *sample_rate,
                                  audio_in_acoustics_t acoustics,
                                  struct audio_stream_in **stream_in)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    status_t status;
    struct qcom_stream_in *in;
    int ret;

    in = (struct qcom_stream_in *)calloc(1, sizeof(*in));
    if (!in)
        return -ENOMEM;

    in->qcom_in = qadev->hwif->openInputStream(devices, format, channels,
                                    sample_rate, &status,
                                    (AudioSystem::audio_in_acoustics)acoustics);
    if (!in->qcom_in) {
        ret = status;
        goto err_open;
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    *stream_in = &in->stream;
    return 0;

err_open:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                               struct audio_stream_in *stream)
{
    struct qcom_audio_device *qadev = to_ladev(dev);
    struct qcom_stream_in *in =
        reinterpret_cast<struct qcom_stream_in *>(stream);

    qadev->hwif->closeInputStream(in->qcom_in);
    free(in);
}

static int adev_dump(const struct audio_hw_device *dev, int fd)
{
    const struct qcom_audio_device *qadev = to_cladev(dev);
    Vector<String16> args;

    return qadev->hwif->dumpState(fd, args);
}

static int qcom_adev_close(hw_device_t* device)
{
    struct audio_hw_device *hwdev =
                        reinterpret_cast<struct audio_hw_device *>(device);
    struct qcom_audio_device *qadev = to_ladev(hwdev);

    if (!qadev)
        return 0;

    if (qadev->hwif)
        delete qadev->hwif;

    free(qadev);
    return 0;
}

static int qcom_adev_open(const hw_module_t* module, const char* name,
                            hw_device_t** device)
{
    struct qcom_audio_device *qadev;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    qadev = (struct qcom_audio_device *)calloc(1, sizeof(*qadev));
    if (!qadev)
        return -ENOMEM;

    qadev->device.common.tag = HARDWARE_DEVICE_TAG;
    qadev->device.common.version = 0;
    qadev->device.common.module = const_cast<hw_module_t*>(module);
    qadev->device.common.close = qcom_adev_close;

    qadev->device.get_supported_devices = adev_get_supported_devices;
    qadev->device.init_check = adev_init_check;
    qadev->device.set_voice_volume = adev_set_voice_volume;
    qadev->device.set_master_volume = adev_set_master_volume;
#ifdef HAVE_FM_RADIO
    qadev->device.set_fm_volume = adev_set_fm_volume;
#endif
    qadev->device.set_mode = adev_set_mode;
    qadev->device.set_mic_mute = adev_set_mic_mute;
    qadev->device.get_mic_mute = adev_get_mic_mute;
    qadev->device.set_parameters = adev_set_parameters;
    qadev->device.get_parameters = adev_get_parameters;
    qadev->device.get_input_buffer_size = adev_get_input_buffer_size;
    qadev->device.open_output_stream = adev_open_output_stream;
#ifdef WITH_QCOM_LPA
    qadev->device.open_output_session = adev_open_output_session;
#endif
    qadev->device.close_output_stream = adev_close_output_stream;
    qadev->device.open_input_stream = adev_open_input_stream;
    qadev->device.close_input_stream = adev_close_input_stream;
    qadev->device.dump = adev_dump;

    qadev->hwif = createAudioHardware();
    if (!qadev->hwif) {
        ret = -EIO;
        goto err_create_audio_hw;
    }

    *device = &qadev->device.common;

    return 0;

err_create_audio_hw:
    free(qadev);
    return ret;
}

static struct hw_module_methods_t qcom_audio_module_methods = {
        open: qcom_adev_open
};

struct qcom_audio_module HAL_MODULE_INFO_SYM = {
    module: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: AUDIO_HARDWARE_MODULE_ID,
            name: "QCOM Audio HW HAL",
            author: "Code Aurora Forum",
            methods: &qcom_audio_module_methods,
            dso : NULL,
            reserved : {0},
        },
    },
};

}; // extern "C"

}; // namespace android_audio_legacy
