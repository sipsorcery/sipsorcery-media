//-----------------------------------------------------------------------------
// Filename: MFSampleGrabber.h
//
// Description: A custom sink for the Windows Media Foundation sample grabber
// https://docs.microsoft.com/en-us/windows/win32/medfound/using-the-sample-grabber-sink:
// "The Sample Grabber Sink is a media sink that forwards the data it receives to an 
// application callback interface."
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??	          Aaron Clauson	  Created.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#pragma once

#include <stdio.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Shlwapi.h>
#include <wmsdkidl.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <iostream>
#include <string>

using namespace System;
using namespace System::Runtime::InteropServices;

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "Shlwapi")

#define CHECKHR_GOTO(x, y) if(FAILED(x)) goto y

#define INTERNAL_GUID_TO_STRING( _Attribute, _skip ) \
if (Attr == _Attribute) \
{ \
	pAttrStr = #_Attribute; \
	C_ASSERT((sizeof(#_Attribute) / sizeof(#_Attribute[0])) > _skip); \
	pAttrStr += _skip; \
	goto done; \
} \

template <class T> void SafeRelease(T * *ppT)
{
  if (*ppT)
  {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

#define CHECK_HR(x) if (FAILED(x)) { goto done; }
#define CHECK_HR_ERROR(x, y) if (FAILED(x)) { std::cout << y << std::endl; goto done; }

#if defined(__cplusplus)
extern "C" {

  // See https://social.msdn.microsoft.com/Forums/en-US/8a4adc97-7f74-44bf-8bae-144a273e62fe/guid-6d703461767a494db478f29d25dc9037?forum=os_windowsprotocols and
  // https://msdn.microsoft.com/en-us/library/dd757766(v=vs.85).aspx
  DEFINE_GUID(MFMPEG4Format_MP4A, 0x6d703461, 0x767a, 0x494d, 0xb4, 0x78, 0xf2, 0x9d, 0x25, 0xdc, 0x90, 0x37);
}
#endif

// Type definitions for native callbacks.
typedef void(__stdcall* OnClockStartFunc)(MFTIME, LONGLONG);
typedef void(__stdcall* OnProcessSampleFunc)(REFGUID, DWORD, LONGLONG, LONGLONG, const BYTE*, DWORD dwSampleSize);
typedef void(__stdcall* OnVideoResolutionChangedFunc)(UINT32, UINT32, UINT32);

// Forward function definitions.
HRESULT CreateMediaSource(PCWSTR pszURL, IMFMediaSource** ppSource);
HRESULT CreateTopology(IMFMediaSource* pSource, IMFActivate* pVideoSink, IMFActivate* pAudioSink, IMFTopology** ppTopo);
HRESULT RunSession(IMFMediaSession* pSession, IMFTopology* pTopology, OnVideoResolutionChangedFunc onVideoResolutionChanged);

namespace SIPSorceryMedia 
{
  public delegate void OnClockStartDelegate(MFTIME, LONGLONG);
  public delegate void OnProcessSampleDelegateNative(REFGUID guidMajorMediaType, DWORD dwSampleFlags, LONGLONG llSampleTime, LONGLONG llSampleDuration, const BYTE * pSampleBuffer, DWORD dwSampleSize);
  public delegate void OnProcessSampleDelegateManaged(int mediaTypeID, DWORD dwSampleFlags, LONGLONG llSampleTime, LONGLONG llSampleDuration, DWORD dwSampleSize, array<Byte> ^% buffer);
  public delegate void OnVideoResolutionChangedDelegate(UINT32 width, UINT32 height, UINT32 stride);

  /**
  Managed class that wraps the native sample grabber callback class. It serves as the
  interface between the managed consumers of this class the Media Foundation functions.
  */
  public ref class MFSampleGrabber
  {
  public:
    const int VIDEO_TYPE_ID = 0;
    const int AUDIO_TYPE_ID = 1;

    property bool Paused {
      bool get() { return _paused; }
    }

    MFSampleGrabber();
    ~MFSampleGrabber();
    HRESULT Run(System::String^ path, bool loop); // Initialises and starts the session (no need to call Start, it's done automatically).
    HRESULT Pause();                              // Pauses the media session.
    HRESULT Start();                              // Restarts the session after pausing.
    HRESULT StopAndExit();                        // Stops and exits the session. Cannot be restarted (use pause if restart is required).

    event OnClockStartDelegate^ OnClockStartEvent;
    event OnProcessSampleDelegateManaged^ OnProcessSampleEvent;
    event OnVideoResolutionChangedDelegate^ OnVideoResolutionChangedEvent;

    void OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
    void OnProcessSample(REFGUID guidMajorMediaType, DWORD dwSampleFlags, LONGLONG llSampleTime, LONGLONG llSampleDuration, const BYTE * pSampleBuffer, DWORD dwSampleSize);
    void OnVideoResolutionChanged(UINT32 width, UINT32 height, UINT32 stride);

  private:
    bool _exit = false;
    bool _paused = false;
    IMFMediaSession * _pcliSession = nullptr;
  };
}

/**
 Native class that implements the sample grabber sink callback interface.
*/
class SampleGrabberCB: public IMFSampleGrabberSinkCallback
{
  long m_cRef;

  SampleGrabberCB(): m_cRef(1) {}

public:
  static HRESULT CreateInstance(SampleGrabberCB **ppCB);

  IMFMediaSession* _session = nullptr;

  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IMFClockStateSink methods
  STDMETHODIMP OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
  STDMETHODIMP OnClockStop(MFTIME hnsSystemTime);
  STDMETHODIMP OnClockPause(MFTIME hnsSystemTime);
  STDMETHODIMP OnClockRestart(MFTIME hnsSystemTime);
  STDMETHODIMP OnClockSetRate(MFTIME hnsSystemTime, float flRate);

  // IMFSampleGrabberSinkCallback methods
  STDMETHODIMP OnSetPresentationClock(IMFPresentationClock* pClock);
  STDMETHODIMP OnProcessSample(REFGUID guidMajorMediaType, DWORD dwSampleFlags,
    LONGLONG llSampleTime, LONGLONG llSampleDuration, const BYTE * pSampleBuffer,
    DWORD dwSampleSize);
  STDMETHODIMP OnShutdown();

  void SetHandlers(OnClockStartFunc onClockStartFunc, OnProcessSampleFunc onProcessSampleFunc)
  {
    _onClockStartFunc = onClockStartFunc;
    _onProcessSampleFunc = onProcessSampleFunc;
  }

private:
  OnClockStartFunc _onClockStartFunc = nullptr;
  OnProcessSampleFunc _onProcessSampleFunc = nullptr;
};
