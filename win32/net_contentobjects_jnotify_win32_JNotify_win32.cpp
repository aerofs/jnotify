/*******************************************************************************
 * JNotify - Allow java applications to register to File system events.
 * 
 * Copyright (C) 2005 - Content Objects
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 ******************************************************************************
 *
 * Content Objects, Inc., hereby disclaims all copyright interest in the
 * library `JNotify' (a Java library for file system events). 
 * 
 * Yahali Sherman, 21 November 2005
 *    Content Objects, VP R&D.
 *    
 ******************************************************************************
 * Author : Omry Yadan
 ******************************************************************************/


#include "net_contentobjects_jnotify_win32_JNotify_win32.h"

#include <windows.h>
#include <winbase.h>
#include <winnt.h>
#include <string>
#include "Win32FSHook.h"
#include "Logger.h"
#include "Lock.h"

Win32FSHook *_win32FSHook;

JavaVM *_jvm = 0;

enum INIT_STATE
{
    NOT_INITIALIZED,
    INITIALIZED,
    ATTACHED,
    FAILED
} _initialized = NOT_INITIALIZED;

JNIEnv *_env = 0;
jclass _clazz = 0;
jmethodID _callback = 0;

void getErrorDescription(int errorCode, WCHAR *buffer, int len);

void ChangeCallbackImpl(int watchID, int action, const WCHAR* filePath, int len)
{
    if (_initialized == INITIALIZED) {
        _jvm->AttachCurrentThreadAsDaemon((void **)&_env, NULL);
        _initialized = ATTACHED;
    }
    jstring jFilePath = _env->NewString((jchar*)filePath, len);
    _env->CallStaticVoidMethod(_clazz, _callback, watchID, action, jFilePath);
    // we need to delete these or Java will hold them until the thread exits
    _env->DeleteLocalRef(jFilePath);
}

/*
 * Class:     net_contentobjects_jnotify_win32_JNotify_win32
 * Method:    nativeInitLogger
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_net_contentobjects_jnotify_win32_JNotify_1win32_nativeInitLogger
  (JNIEnv *env, jclass clazz, jbyteArray path, jboolean debug)
{
    (void)clazz;
    char *str = (char*)env->GetPrimitiveArrayCritical(path, NULL);
    if (str != NULL) {
        initLog(str, debug == JNI_TRUE);
    } else {
        // TODO: exception
    }

    env->ReleasePrimitiveArrayCritical(path, str, JNI_ABORT);
}


/*
 * Class:     net_contentobjects_fshook_win32_Win32FSHook
 * Method:    nativeInit
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_net_contentobjects_jnotify_win32_JNotify_1win32_nativeInit
  (JNIEnv *env, jclass clazz)
{
    (void)clazz;
    static Lock lock;
    lock.lock();
    if (_initialized == NOT_INITIALIZED) {
        bool failed = false;
        char className[] = "net/contentobjects/jnotify/win32/JNotify_win32";
        _clazz = env->FindClass(className);
        if (_clazz == NULL) {
            log("class %s not found ", className);
            failed = true;
        }
        _clazz = (jclass) env->NewGlobalRef(_clazz);
        
        if (!failed) {
            _callback = env->GetStaticMethodID(_clazz, "callbackProcessEvent", "(IILjava/lang/String;)V");
            if (_callback == NULL) {
                log("callbackProcessEvent not found");
                failed = true;
            }
        }
        
        if (!failed) {
            _initialized = INITIALIZED;
        } else {
            _initialized = FAILED;
        }
    }
    lock.unlock();
    if (_initialized != INITIALIZED) return -1;

    try {
        _win32FSHook = new Win32FSHook();
        _win32FSHook->init(&ChangeCallbackImpl);
        return 0;
    } catch (int err) {
        return err;
    }
}

/*
 * Class:     net_contentobjects_fshook_win32_Win32FSHook
 * Method:    nativeAddWatch
 * Signature: (Ljava/lang/String;IZ)I
 */
JNIEXPORT jint JNICALL Java_net_contentobjects_jnotify_win32_JNotify_1win32_nativeAddWatch
  (JNIEnv *env, jclass clazz, jstring path, jint notifyFilter, jboolean watchSubdir)
{
    (void)clazz;
    const WCHAR *cstr = (const WCHAR*)env->GetStringChars(path, NULL);
    if (cstr == NULL)  {
        return -1; /* OutOfMemoryError already thrown */
    }
    DWORD error = 0;
    int watchId = _win32FSHook->add_watch(cstr, notifyFilter, watchSubdir == JNI_TRUE, error);
    env->ReleaseStringChars(path, (const jchar*)cstr);
    if (watchId == 0) {
        return -(jint)(error);
    } else {
        return watchId;
    }
}

/*
 * Class:     net_contentobjects_fshook_win32_Win32FSHook
 * Method:    nativeRemoveWatch
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_net_contentobjects_jnotify_win32_JNotify_1win32_nativeRemoveWatch
  (JNIEnv *env, jclass clazz, jint watchId)
{
    (void)env;
    (void)clazz;
    _win32FSHook->remove_watch(watchId);
}

/*
 * Class:     net_contentobjects_fshook_win32_Win32FSHook
 * Method:    getErrorDesc
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_net_contentobjects_jnotify_win32_JNotify_1win32_getErrorDesc
  (JNIEnv *env, jclass clazz, jint errorCode)
{
    (void)clazz;
    WCHAR buffer[1024];
    getErrorDescription(errorCode, buffer, sizeof(buffer) / sizeof(WCHAR));
    return env->NewString((jchar*)buffer, wcslen(buffer));
}

void getErrorDescription(int errorCode, WCHAR *buffer, int len)
{
    static Lock lock;
    lock.lock();
    
    LPVOID lpMsgBuf;
    FormatMessageW( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM | 
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        0, // Default language
        (LPWSTR) &lpMsgBuf,
        0,
        NULL 
    );

    _snwprintf(buffer, len, L"Error %d : %s", errorCode, (LPCTSTR)lpMsgBuf);
    int len1 = wcslen(buffer);
    if (len1 >= 2) {
        buffer[len1 - 2] = '\0';
    }
    
    LocalFree( lpMsgBuf );
    
    lock.unlock();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    (void)reserved;
    _jvm = jvm;
    return JNI_VERSION_1_2;
}
