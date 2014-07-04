/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 *  File: QCMediaPlayer.java
 *  Description: Snapdragon SDK for Android support class.
 *
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
import com.qualcomm.qcmedia.QCTimedText;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.Parcel;
import java.util.HashSet;
import java.io.IOException;
import android.content.Context;
import android.net.Uri;
import java.util.Map;

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

  //Stores the supported keys for getParameter invoke call
  public static final HashSet<Integer> VALID_GET_PARAM_KEYS = new HashSet<Integer>();
  static
  {
    VALID_GET_PARAM_KEYS.add(new Integer(OnMPDAttributeListener.ATTRIBUTES_WHOLE_MPD));
    VALID_GET_PARAM_KEYS.add(new Integer(OnMPDAttributeListener.INVOKE_ID_GET_ATTRIBUTES_TYPE_MPD));
    VALID_GET_PARAM_KEYS.add(new Integer(OnQOEEventListener.ATTRIBUTES_QOE_EVENT_PERIODIC));
    VALID_GET_PARAM_KEYS.add(new Integer(QCMediaPlayer.KEY_DASH_REPOSITION_RANGE));
  }

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

    ePlayerState = MediaPlayerState.PLAYER_IDLE;

    Log.d(TAG, "QCMediaPlayer::QCMediaPlayer");
  }

  public void setDataSource(Context context, Uri uri)
          throws IOException, IllegalArgumentException, SecurityException, IllegalStateException
  {
      Log.d(TAG, "setDataSource");
      super.setDataSource(context, uri);
      ePlayerState = MediaPlayerState.PLAYER_INITIALIZED;
      TurnOnOffTimedTextListener();
  }

  public void setDataSource(Context context, Uri uri, Map<String, String> headers)
          throws IOException, IllegalArgumentException, SecurityException, IllegalStateException
  {
      Log.d(TAG, "setDataSource");
      super.setDataSource(context, uri, headers);
      ePlayerState = MediaPlayerState.PLAYER_INITIALIZED;
      TurnOnOffTimedTextListener();
  }

  public void setDataSource(String path)
          throws IOException, IllegalArgumentException, SecurityException, IllegalStateException
  {
      Log.d(TAG, "setDataSource");
      super.setDataSource(path);
      ePlayerState = MediaPlayerState.PLAYER_INITIALIZED;
      TurnOnOffTimedTextListener();
  }

  public void setDataSource(String path, Map<String, String> headers)
        throws IOException, IllegalArgumentException, SecurityException, IllegalStateException
  {
      Log.d(TAG, "setDataSource");
      super.setDataSource(path, headers);
      ePlayerState = MediaPlayerState.PLAYER_INITIALIZED;
      TurnOnOffTimedTextListener();
  }

  public void reset() {
      Log.d(TAG, "reset");
      super.reset();
      ePlayerState = MediaPlayerState.PLAYER_IDLE;
  }

  public void release() {
      Log.d(TAG, "release");

      mQCOnPreparedListener = null;
      mOnMPDAttributeListener = null;
      mOnQCTimedTextListener = null;
      mOnQOEEventListener = null;

      super.release();

      ePlayerState = MediaPlayerState.PLAYER_IDLE;
  }

  public void TurnOnOffTimedTextListener()
  {
      //Turn on/off timedtext registered flag in dashplayer. setOnQCTimedTextListener can be called
      //anytime from app. Below qcsetparameter call uses invoke() which needs to be called only
      //after mediaplayer.cpp transitioned out of IDLE state
      if(ePlayerState != MediaPlayerState.PLAYER_IDLE)
      {
          int val = (mOnQCTimedTextListener == null) ? 0 : 1;
          Log.d(TAG, "TurnOnOffTimedTextListener set listener flag" + val);
          QCsetParameter(KEY_QCTIMEDTEXT_LISTENER, val);
      }
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
    if (mOnMPDAttributeListener != null)
    {
        String mpdAttributes = QCgetStringParameter(OnMPDAttributeListener.INVOKE_ID_GET_ATTRIBUTES_TYPE_MPD);
        mOnMPDAttributeListener.onMPDAttribute(OnMPDAttributeListener.ATTRIBUTES_TYPE_MPD, mpdAttributes, this);
    }
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
    Log.d(TAG, "setOnQCTimedTextListener");

    mOnQCTimedTextListener = listener;
    TurnOnOffTimedTextListener();
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
    Log.d(TAG, "setOnPreparedListener");
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
     * Key to Get MPD attributes
     */
     public static final int INVOKE_ID_GET_ATTRIBUTES_TYPE_MPD = 8010;

     /**
     * Key to Set MPD attributes
     */
     public static final int INVOKE_ID_SET_ATTRIBUTES_TYPE_MPD = 8011;

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
    boolean retval = false;
    if(key == OnMPDAttributeListener.ATTRIBUTES_TYPE_MPD)
    {
      key = OnMPDAttributeListener.INVOKE_ID_SET_ATTRIBUTES_TYPE_MPD;
    }
    return QCsetStringParameter(key, value);
  }
  public String  QCGetParameter(int key)
  {
    if(key == OnMPDAttributeListener.ATTRIBUTES_TYPE_MPD)
    {
      key = OnMPDAttributeListener.INVOKE_ID_GET_ATTRIBUTES_TYPE_MPD;
    }

    return QCgetStringParameter(key);
  }
  public Parcel QCPeriodicParameter(int key)
  {
      if(key == mOnQOEEventListener.ATTRIBUTES_QOE_EVENT_PERIODIC)
      {
        return QCgetParcelParameter(key);
      }
    return null;
  }
  public boolean QCSetParameter(int key, int value)
  {
    Log.d(TAG, "QCMediaPlayer : QCSetParameter");
    return QCsetParameter(key, value);
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

  /**
   * Key to query reposition range. Value needs to be same as defined in DashPlayer.h
   */
  public static final int KEY_DASH_REPOSITION_RANGE = 9000;

  /**
   * Keys for dash playback modes. Value needs to be same as defined in DashPlayer.h
   */
  public static final int KEY_DASH_SEEK_EVENT = 7001;
  public static final int KEY_DASH_PAUSE_EVENT = 7002;
  public static final int KEY_DASH_RESUME_EVENT = 7003;

  public static final int KEY_QCTIMEDTEXT_LISTENER = 6000;

  enum MediaPlayerState {
      PLAYER_IDLE,
      PLAYER_INITIALIZED
  };

  public MediaPlayerState ePlayerState;

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

  private String QCgetStringParameter(int key)
  {
        if(!VALID_GET_PARAM_KEYS.contains(new Integer(key))) {
            Log.d(TAG, "QCgetStringParameter Unsupported key "+key+" Return null");
            return null;
        }

    Parcel request = newRequest();
    Parcel reply = Parcel.obtain();
	reply.setDataPosition(0);
    request.writeInt(key);
    invoke(request, reply);
    String ret = reply.readString();
    request.recycle();
    reply.recycle();
    return ret;
  }

  public boolean QCsetStringParameter(int key, String value) {
        boolean retval = false;
        Parcel request = newRequest();
        Parcel reply = Parcel.obtain();
        request.writeInt(key);
        request.writeString(value);
        invoke(request, reply);
        retval = reply.readInt() > 0 ? true : false;
        request.recycle();
        reply.recycle();
        return retval;
    }

  public boolean QCsetParameter(int key, int value) {
        boolean retval = false;
        Parcel request = newRequest();
        Parcel reply = Parcel.obtain();
        request.writeInt(key);
        request.writeInt(value);
        invoke(request, reply);
        retval = reply.readInt() > 0 ? true : false;
        request.recycle();
        reply.recycle();
        return retval;
    }

  public Parcel QCgetParcelParameter(int key) {

        if(!VALID_GET_PARAM_KEYS.contains(new Integer(key))) {
            Log.d(TAG, "QCgetParcelParameter Unsupported key "+key+" Return null");
            return null;
        }
        Parcel request = newRequest();
        Parcel reply = Parcel.obtain();
        request.writeInt(key);
        invoke(request, reply);
        request.recycle();
        return reply;
    }
}
