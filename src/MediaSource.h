//-----------------------------------------------------------------------------
// Filename: MediaSource.h
//
// Description: Provides audio and/or video media sources that can derive from
// both live capture devices or files.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??						Aaron Clauson		Created, Hobart, Australia.
// 31 Jan 2020  Aaron Clauson	  Renamed from MFVideoSampler to MediaSource.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#pragma once

#include "MediaCommon.h"
#include "VideoSubTypes.h"

#include <stdio.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mftransform.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <errors.h>
#include <wmcodecdsp.h>
#include <wmsdkidl.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;

namespace SIPSorceryMedia {

	/**
	* Represents a source of audio and/or video samples. The source can be 
	* from live capture devices for from a file.
	*/
	public ref class MediaSource
	{
	public:

		/*
		* Width of the frame set on the video capture device.
		*/
		property int Width {
			int get() { return _width; }
		}

		/*
		* Height of the frame set on the video capture device.
		*/
		property int Height {
			int get() { return _height; }
		}

		/*
		* Stride for the frame set on the video capture device.
		*/
		property int Stride {
			int get() { return _stride; }
		}

		/*
		* Default constructor.
		*/
		MediaSource();

		/*
		* Default destructor.
		*/
		~MediaSource();

		/*
		* Initialises the media source reader with an MP4 file.
		* @param[in] path: the path to the MP4 file to load.
		* @param[in] loop: if true then the source should loop back to the start
		*  when the end is reached.
		* @@Returns: S_OK if successful or throw if a failure occurs.
		*/
		HRESULT Init(String^ path, bool loop);

		/*
		* Initialises the media source reader with audio and video capture devices.
		* @param[in] audioDeviceIndex: the index for the audio capture device.
		* @param[in] videoDeviceIndex: the index for the video capture device.
		* @param[in] videoSubType: the pixel format to attempt to set on the video capture
		*  device.
		* @param[in] width: the frame width to attempt to set on the video capture
		*  device.
		* @param[in] height: the frame height to attempt to set on the video capture
		*  device.
		* @@Returns: S_OK if successful or throws if a failure occurs.
		*/
		HRESULT Init(int audioDeviceIndex, int videoDeviceIndex, VideoSubTypesEnum videoSubType, UInt32 width, UInt32 height);

		/*
		* Requests a media sample from the source reader.
		*/
		MediaSampleProperties^ GetSample(/* out */ array<Byte>^% buffer);

		/*
		* Attempts to retrieve a list of the video capture devices available on the system.
		* @param[out] devices: if successful this parameter will be populated with a list of
		*  video output devices.
		* @@Returns: S_OK if successful or throws if a failure occurs.
		*/
		HRESULT GetVideoDevices(/* out */ List<VideoMode^>^% devices);

		/*
		* Attempts to find a matching media type for the video source reader.
		* @param[in] pReader: the source reader to look for the matching video type on.
		* @param[in] mediaSubType: the video pixel format to match.
		* @param[in] width: the video frame width to match.
		* @param[in] height: the video frame height to match.
		* @param[out] ppFoundType: if successful this parameter will be populated with a pointer
		*  to the matching media type.
		* @@Returns: S_OK if successful or throws if a failure occurs.
		*/
		HRESULT FindVideoMode(IMFSourceReader *pReader, const GUID mediaSubType, UInt32 width, UInt32 height, /* out */ IMFMediaType **pFoundType);

		/*
		* Attempts to set the audio and video stream indexes for the live capture source reader.
		* When the source reader has multiple streams the safe way to identify the sample type
		* is the stream index.
		* @@Returns: S_OK if successful or throws if a failure occurs.
		*/
		HRESULT SetStreamIndexes();

		/*
		* Shuts down the source reader. After calling this method sampling is no longer
		* possible.
		*/
		void Shutdown();

	private:

		static BOOL _isInitialised = false;
		const unsigned int MAX_STREAM_INDEX = 10;
		const int TIMESTAMP_MILLISECOND_DIVISOR = 10000;	// Media sample timestamps are given in 100's of nano seconds.

		IMFSourceReader * _sourceReader = NULL;
		DWORD videoStreamIndex;
		int _width, _height, _stride;
		int _audioStreamIndex = -1, _videoStreamIndex = -1;
		bool _isLiveSource = false;
		bool _loop = false;
		LONGLONG _prevSampleTs = 0;
		std::chrono::time_point<std::chrono::steady_clock>* _previousSampleAt = nullptr;
	};
}

