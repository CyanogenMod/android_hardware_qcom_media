/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 *  File: QCMediaPlayer.java
 *  Description: Snapdragon SDK for Android support class.
 *               Provides access to QC-provided MediaPlayer APIs and interfaces

 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Copyright (C) 2006 The Android Open Source Project
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

package com.qualcomm.qcmedia;

import android.media.MediaPlayer;
import android.util.Log;
import android.media.TimedText;
import java.lang.ref.WeakReference;
import java.io.*;
import com.qualcomm.qcmedia.QCTimedText;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Parcel;

/**
* QCMediaPlayer extends MediaPlayer from package android.media and provides
* extended APIs and interfaces to get and set MPD attributes for DASH protocol
* in compatible Snapdragon builds.
*{@hide}
*/

public class QCMediaPlayer extends MediaPlayer
{
  private final static String TAG = "QCMediaPlayer";
  private QCMediaEventHandler mEventHandler;

  public QCMediaPlayer()
  {
    super();
    Looper looper;
    if ((looper = Looper.myLooper()) != null)
    {
       mEventHandler = new QCMediaEventHandler(this, looper);
    }
    else if ((looper = Looper.getMainLooper()) != null)
    {
       mEventHandler = new QCMediaEventHandler(this, looper);
    }
    else
    {
       mEventHandler = null;
    }
    Log.d(TAG, "QCMediaPlayer::QCMediaPlayer");
  }

  private void callOnPreparedListener()
  {
    Log.d(TAG, "callOnPreparedListener");
    if (mQCOnPreparedListener != null)
      mQCOnPreparedListener.onPrepared(this);
  }

  private void callOnMPDAttributeListener()
  {
    Log.d(TAG, "callOnMPDAttributeListener");
    String mpdAttributes = getStringParameter(OnMPDAttributeListener.ATTRIBUTES_TYPE_MPD);
    if (mOnMPDAttributeListener != null)
      mOnMPDAttributeListener.onMPDAttribute(OnMPDAttributeListener.ATTRIBUTES_TYPE_MPD, mpdAttributes, this);
  }
  private void callQCTimedTextListener(QCTimedText text)
  {
    if(mOnQCTimedTextListener != null)
    {
      mOnQCTimedTextListener.onQCTimedText(this, text);
    }
  }
  private void callOnQOEEventListener(int key,Parcel parcel)
  {
    Log.d(TAG, "callOnQOEEventListener");
    if (mOnQOEEventListener != null)
    {
      mOnQOEEventListener.onQOEAttribute(key,parcel,this);
    }
  }

  /**
  * Additional propreitary interface definition of a callback to be invoked
  * when a Qtimed text is available for display.
  * {@hide}
  */
  public interface OnQCTimedTextListener
  {
        /**
        * Called to indicate an avaliable timed text
        *
        * @param mp             the QCMediaPlayer associated with this
        *                       callback
        * @param text           the Qtimed text sample which contains the text
        *                       needed to be displayed and the display format.
        * {@hide}
        */
        public void onQCTimedText(QCMediaPlayer mp, QCTimedText text);
  }

  /**
  * Register a callback to be invoked when a Qtimed text is available
  * for display.
  *
  * @param listener the callback that will be run
  * {@hide}
  */
  public void setOnQCTimedTextListener(OnQCTimedTextListener listener)
  {
    mOnQCTimedTextListener = listener;
  }
  private OnQCTimedTextListener mOnQCTimedTextListener;

  /**
  * Register a callback to be invoked when the media source is ready
  * for playback.
  *
  * @param listener the callback that will be run
  */
  public void setOnPreparedListener(OnPreparedListener listener)
  {
    mQCOnPreparedListener = listener;
    Log.d(TAG, "QCMediaPlayer::setOnPreparedListener");
  }
  private OnPreparedListener mQCOnPreparedListener;

  /**
  * Interface definition for a callback to be invoked when the media
  * source is ready for MPD attribute retrieval
  */
  public interface OnMPDAttributeListener
  {
     /**
     * Key to identify type of MPD attributes
     */
     public static final int ATTRIBUTES_TYPE_MPD = 8002;
     /**
     * Key to be used to retrieve complete MPD.
     */
     public static final int ATTRIBUTES_WHOLE_MPD = 8003;
     /**
     * Called when attributes are available.
     *
     * @param attributekey   the key identifying the type of attributes available
     * @param value          the value for the attribute
     * @param mp             the MediaPlayer to which MPD attribute is applicable
     *
     */
     public void onMPDAttribute(int attributekey, String value, QCMediaPlayer mp);
  }

  /**
  * Register a callback to be invoked when MPD attributes are avaialble
  *
  * @param listener          the callback that will be run
  */
  public void setOnMPDAttributeListener(OnMPDAttributeListener listener)
  {
    mOnMPDAttributeListener = listener;
  }

  private OnMPDAttributeListener mOnMPDAttributeListener;

  /**
  * Process the parameter indicated by key.
  * @param key              the key idicated the parameter to be processed
  * @param value            the value for the parameter identified by key
  * @return                 true if successful in processing the parameter else returns false
  * @see OnMPDAttributeListener the interface for valid keys to be used
  *
  */
  public boolean processMPDAttribute(int key, String value)
  {
    return setParameter(key, value);
  }
  public String  QCGetParameter(int key)
  {
    return getStringParameter(key);
  }
  public Parcel QCPeriodicParameter(int key)
  {
      if(key == mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_PERIODIC)
      {
        Log.d(TAG, "QCMediaPlayer :  QCGetParameter(int key , Parcel& data)");
          return getParcelParameter(key);
      }
    return null;
  }
  public boolean QCSetParameter(int key, int value)
  {
    Log.d(TAG, "QCMediaPlayer : QCSetParameter");
    return setParameter(key, value);
  }
  /**
  * Interface definition for a callback to be invoked when the media
  * source is ready for QOE data retrieval.
  */
  public interface OnQOEEventListener
  {
     /**
     * Key to identify type of QOE Event
     */
     public static final int ATTRIBUTES_QOE_EVENT_REG       = 8004;
     public static final int ATTRIBUTES_QOE_EVENT_PLAY      = 8005;
     public static final int ATTRIBUTES_QOE_EVENT_STOP      = 8006;
     public static final int ATTRIBUTES_QOE_EVENT_SWITCH    = 8007;
     public static final int ATTRIBUTES_QOE_EVENT_PERIODIC  = 8008;

     /**
     * Called when attributes are available.
     *
     * @param attributekey   the key identifying the type of attributes available
     * @param value          the value for the attribute
     * @param mp             the MediaPlayer to which QOE event is
     *                       applicable
     *
     */
     public void onQOEAttribute(int key, Parcel value,QCMediaPlayer mp);
  }
  /**
  * Register a callback to be invoked when QOE event happens
  * @param listener          the callback that will be run
  */
  public void setOnQOEEventListener(OnQOEEventListener listener)
  {
    mOnQOEEventListener = listener;
  }

  private OnQOEEventListener mOnQOEEventListener;
  /* Do not change these values without updating their counterparts
  * in include/media/mediaplayer.h!
  */
  private static final int MEDIA_NOP = 0; // interface test message
  private static final int MEDIA_PREPARED = 1;
  private static final int MEDIA_PLAYBACK_COMPLETE = 2;
  private static final int MEDIA_BUFFERING_UPDATE = 3;
  private static final int MEDIA_SEEK_COMPLETE = 4;
  private static final int MEDIA_SET_VIDEO_SIZE = 5;
  private static final int MEDIA_TIMED_TEXT = 99;
  private static final int MEDIA_ERROR = 100;
  private static final int MEDIA_INFO = 200;
  private static final int MEDIA_QOE = 300;

/* This sequence needs to be same as defined in NuPlayer.h*/
  private static final int QOEPlay = 1;
  private static final int QOEStop = 2;
  private static final int QOESwitch = 3;
  private static final int QOEPeriodic =4;

  /*enum QOEEvent{
        QOE,
        QOEPlay,
        QOEStop,
        QOESwitch,
        QOEPeriodic
  };*/

  private class QCMediaEventHandler extends Handler
  {
        private QCMediaPlayer mQCMediaPlayer;

        public QCMediaEventHandler(QCMediaPlayer mp, Looper looper){
                super(looper);
                Log.d(TAG, "QCMediaEventHandler calling mp.mEventHandler.sendMessage()m");
                mQCMediaPlayer = mp;
        }

        public void handleMessage(Message msg)
        {
          Log.d(TAG, "QCMediaPlayer::QCMediaEventHandler::handleMessage");
          switch(msg.what)
          {
            case MEDIA_PREPARED:
                Log.d(TAG, "QCMediaEventHandler::handleMessage::MEDIA_PREPARED calling callOnMPDAttributeListener");
                callOnMPDAttributeListener();
                Log.d(TAG, "QCMediaEventHandler::handleMessage::MEDIA_PREPARED calling callOnPreparedListener");
                callOnPreparedListener();
                return;

                case MEDIA_TIMED_TEXT:
                Log.d(TAG, "QCMediaEventHandler::handleMessage::MEDIA_TIMED_TEXT");
                if(mOnQCTimedTextListener != null)
                {
                  if (msg.obj instanceof Parcel) {
                      Parcel parcel = (Parcel)msg.obj;
                      QCTimedText text = new QCTimedText(parcel);
                      callQCTimedTextListener(text);
                  }
                }
                return;

            case MEDIA_QOE:
                Log.d(TAG, "QCMediaEventHandler::handleMessage::MEDIA_QOE Received " + msg.arg2);
                if(mOnQOEEventListener != null)
                {
                  if (msg.obj instanceof Parcel)
                  {
                    int key = 0;
                    Parcel parcel = (Parcel)msg.obj;
                    if(msg.arg2 == /*(int)QOEEvent.*/QOEPlay)
                    {
                      key = mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_PLAY;
                    }else if(msg.arg2 == /*(int)QOEEvent.*/QOEPeriodic)
                    {
                      key = mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_PERIODIC;
                    }else if(msg.arg2 == /*(int)QOEEvent.*/QOESwitch)
                    {
                      key = mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_SWITCH;
                    }else if(msg.arg2 == /*(int)QOEEvent.*/QOEStop)
                    {
                      key = mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_STOP;
                    }
                    callOnQOEEventListener(key,parcel);
                  }
                }
                return;

                default:
                  Log.d(TAG, "Unknown message type " + msg.what);
                  return;
          }
        }
  }

  /**
  * Called from native code when an interesting event happens.  This method
  * just uses the EventHandler system to post the event back to the main app thread.
  * We use a weak reference to the original QCMediaPlayer object so that the native
  * code is safe from the object disappearing from underneath it.  (This is
  * the cookie passed to native_setup().)
  */
  private static void QCMediaPlayerNativeEventHandler(Object mediaplayer_ref,
                                                      int what, int arg1, int arg2, Object obj)
  {
        Log.d(TAG, "QCMediaPlayerNativeEventHandler");
        QCMediaPlayer mp = (QCMediaPlayer)((WeakReference)mediaplayer_ref).get();
        if (mp == null)
        {
           Log.d(TAG, "QCMediaPlayerNativeEventHandler mp == null");
           return;
        }
        if (mp.mEventHandler != null)
        {
           Message m = mp.mEventHandler.obtainMessage(what, arg1, arg2, obj);
           Log.d(TAG, "QCMediaPlayerNativeEventHandler calling mp.mEventHandler.sendMessage()");
           mp.mEventHandler.sendMessage(m);
        }
  }
}
