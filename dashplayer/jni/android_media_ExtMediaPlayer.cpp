/*Copyright (c) 2013, 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "ExtMediaPlayer-JNI"

#include "android_media_ExtMediaPlayer.h"
#include <dlfcn.h>

using namespace android;


extern "C" MediaPlayerListener*
CreateJNIExtMediaPlayerListener(JNIEnv* env, jobject thiz, jobject weak_thiz,const sp<MediaPlayerListener>& listener) {
    return (new JNIExtMediaPlayerListener(env, thiz, weak_thiz, listener));
}

extern "C" bool
checkExtMedia(JNIEnv *env, jobject thiz){
    jclass clazz = NULL;
    bool nRet = false;
    clazz = env->FindClass("com/qualcomm/qcmedia/QCMediaPlayer");
    if (clazz != NULL) {
        if (env->IsInstanceOf(thiz,clazz)) {
            nRet = true;
            ALOGD("QCMediaPlayer mediaplayer present");
        } else {
            ALOGE("env->IsInstanceOf fails");
        }
    } else {
        //Clear the exception as QCMediaPlayer is optional
        env->ExceptionClear();
        ALOGE("QCMediaPlayer could not be located....");
    }
    return nRet;
}

JNIExtMediaPlayerListener::JNIExtMediaPlayerListener(JNIEnv* env, jobject thiz, jobject weak_thiz,const sp<MediaPlayerListener>& listener) {
    jclass clazz = env->GetObjectClass(thiz);
    if (clazz == NULL) {
        ALOGE("Can't find android/media/MediaPlayer");
        jniThrowException(env, "java/lang/Exception", NULL);
        return;
    }
    mpListener = listener;
    extfields.ext_post_event = env->GetStaticMethodID(clazz, "QCMediaPlayerNativeEventHandler",
                             "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    mClass = (jclass)env->NewGlobalRef(clazz);

    // We use a weak reference so the MediaPlayer object can be garbage collected.
    // The reference is only used as a proxy for callbacks.
    mObject  = env->NewGlobalRef(weak_thiz);
}

JNIExtMediaPlayerListener::~JNIExtMediaPlayerListener() {
    // remove global references
    JNIEnv *env = AndroidRuntime::getJNIEnv();
    env->DeleteGlobalRef(mObject);
    env->DeleteGlobalRef(mClass);
}


void JNIExtMediaPlayerListener::notify(int msg, int ext1, int ext2, const Parcel *obj) {
    JNIEnv *env = AndroidRuntime::getJNIEnv();
    if (env && obj && obj->dataSize() > 0) {
        jobject jParcel = createJavaParcelObject(env);
        if (jParcel != NULL) {
            if((extfields.ext_post_event != NULL) &&
            ((msg == MEDIA_PREPARED) || (msg == MEDIA_TIMED_TEXT) || (msg == MEDIA_QOE))) {
                ALOGE("JNIExtMediaPlayerListener::notify calling ext_post_event");
                Parcel* nativeParcel = parcelForJavaObject(env, jParcel);
                if(nativeParcel != NULL) {
                    nativeParcel->setData(obj->data(), obj->dataSize());
                    env->CallStaticVoidMethod(mClass, extfields.ext_post_event, mObject,
                    msg, ext1, ext2, jParcel);
                    env->DeleteLocalRef(jParcel);
                    ALOGD("JNIExtMediaPlayerListener::notify ext_post_event done");
                }
            } else {
                ALOGD("JNIExtMediaPlayerListener::notify calling for generic event");
                mpListener->notify(msg, ext1, ext2, obj);
            }
        }
    } else {
        if((extfields.ext_post_event != NULL) &&
            ((msg == MEDIA_PREPARED) || (msg == MEDIA_TIMED_TEXT) ||(msg == MEDIA_QOE)))
        {
            ALOGD("JNIExtMediaPlayerListener::notify calling ext_post_events");
            env->CallStaticVoidMethod(mClass, extfields.ext_post_event, mObject, msg, ext1, ext2, NULL);
        } else {
            ALOGD("JNIExtMediaPlayerListener::notify for generic events");
            mpListener->notify(msg, ext1, ext2, obj);
        }
    }
    return;
}


extern "C" MediaPlayer *CreateNativeQCMediaPlayer()
{
    const char* QCMEDIAPLAYER_LIB = "libqcmediaplayer.so";
    const char* QCMEDIAPLAYER_CREATE_FN = "CreateQCMediaPlayer";
    void* pQCMediaPlayerLib = NULL;

    typedef MediaPlayer* (*CreateQCMediaPlayerFn)();

    /* Open library */
    ALOGI("calling dlopen on QCMEDIAPLAYER_LIB");
    pQCMediaPlayerLib = ::dlopen(QCMEDIAPLAYER_LIB, RTLD_LAZY);

    if (pQCMediaPlayerLib == NULL) {
        ALOGE("Failed to open QCMEDIAPLAYER_LIB Error : %s ",::dlerror());
        return NULL;
    }

    CreateQCMediaPlayerFn pCreateFnPtr = NULL;
    ALOGI("calling dlsym on pQCMediaPlayerLib for QCMEDIAPLAYER_CREATE_FN ");
    pCreateFnPtr = (CreateQCMediaPlayerFn) dlsym(pQCMediaPlayerLib, QCMEDIAPLAYER_CREATE_FN);

    if (pCreateFnPtr == NULL) {
        ALOGE("Could not locate CreateQCMediaPlayerFn pCreateFnPtr");
        return NULL;
    }

    MediaPlayer* pQCMediaPlayer = pCreateFnPtr();

    if(pQCMediaPlayer==NULL) {
        ALOGE("Failed to invoke CreateQCMediaPlayerFn...");
        return NULL;
    }

    return pQCMediaPlayer;
}

