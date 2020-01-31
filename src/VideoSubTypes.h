//-----------------------------------------------------------------------------
// Filename: VideoSubTypes.h
//
// Description: Helper to map between Windows Media Foundation and ffmpeg
// pixel formats.
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

#include <mfapi.h>

extern "C"
{
	#include "libswscale/swscale.h"
}

namespace SIPSorceryMedia {

	/*
	* The video pixel formats this library understands.
	*/
	public enum class VideoSubTypesEnum
	{
		I420,
		RGB24,
		RGB32,
		YUY2,
		BGR24,
	};

	/*
	* Helper class to convert between different pixel format enums and GUIDs.
	*/
	public ref class VideoSubTypes
	{
	public:

		/*
		* Returns the Media Foundation GUID for a video pixel format. 
		* @param[in] videoSubType: The video pixel format to look up the GUID for.
		* @@Returns: The Media Foundation GUID for the pixel format. Throws if the 
		*  pixel format is not recognised.
		*/
		static GUID GetGuidForVideoSubType(VideoSubTypesEnum videoSubType)
		{
			switch (videoSubType)
			{
				case VideoSubTypesEnum::I420: return MFVideoFormat_I420;
				case VideoSubTypesEnum::RGB24: return MFVideoFormat_RGB24;
				case VideoSubTypesEnum::RGB32: return MFVideoFormat_RGB32;
				case VideoSubTypesEnum::YUY2: return MFVideoFormat_YUY2;
				case VideoSubTypesEnum::BGR24: return MFVideoFormat_RGB24;
				default: throw gcnew System::ApplicationException("Video mode not recognised in GetGuidForVideoSubType.");
			}
		};

		/*
		* Returns the video pixel format got a Media Foundation GUID.
		* @param[in] guid: The Media Foundation GUID to look up the pixel format for.
		* @@Returns: The video pixel format for the Media Foundation GUID. Throws if
		*  the GUID is not recognised.
		*/
		static VideoSubTypesEnum GetVideoSubTypeForGuid(REFGUID guid)
		{
			if (guid == MFVideoFormat_I420) return VideoSubTypesEnum::I420;
			if (guid == MFVideoFormat_RGB24) return VideoSubTypesEnum::RGB24;
			if (guid == MFVideoFormat_RGB32) return VideoSubTypesEnum::RGB32;
			if (guid == MFVideoFormat_YUY2) return  VideoSubTypesEnum::YUY2;
			if( guid == MFVideoFormat_RGB24) return VideoSubTypesEnum::BGR24;
			
			throw gcnew System::ApplicationException("GUID not recognised in GetVideoSubTypeForGuid.");
		};

		/*
		* Returns the ffmpeg pixel format for a pixel format understood by this class.
		* @param[in] videoSubType: The video pixel format to look up the ffmpeg pixel
		*  format for.
		* @@Returns: The matching ffmpeg video pixel format. Throws if not recognised.
		*/
		static AVPixelFormat GetPixelFormatForVideoSubType(VideoSubTypesEnum videoSubType)
		{
			switch (videoSubType)
			{
				case VideoSubTypesEnum::I420: return AVPixelFormat::AV_PIX_FMT_YUV420P;
				case VideoSubTypesEnum::RGB24: return AVPixelFormat::AV_PIX_FMT_RGB24;
				case VideoSubTypesEnum::RGB32: return AVPixelFormat::AV_PIX_FMT_RGB32;
				case VideoSubTypesEnum::YUY2: return AVPixelFormat::AV_PIX_FMT_YUYV422;
				case VideoSubTypesEnum::BGR24: return AVPixelFormat::AV_PIX_FMT_BGR24;
				default: throw gcnew System::ApplicationException("Video mode not recognised in GetPixelFormatForVideoSubType.");
			}
		}
	};
}
