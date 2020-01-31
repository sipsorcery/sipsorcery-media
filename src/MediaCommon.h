//-----------------------------------------------------------------------------
// Filename: MediaCommon.h
//
// Description: Header file to contain common Media Foundation functions.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// 31 Jan 2020  Aaron Clauson	  Created, Dublin, Ireland.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#pragma once

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

#define CHECKHR_THROW(hr, msg) if (hr != S_OK) { throw gcnew System::ApplicationException(msg); }

#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return #val
#endif

template <class T> void SAFE_RELEASE(T** ppT)
{
  if (*ppT)
  {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

template <class T> inline void SAFE_RELEASE(T*& pT)
{
  if (pT != NULL)
  {
    pT->Release();
    pT = NULL;
  }
}

namespace SIPSorceryMedia {

  enum class DeviceType
  {
    Audio,
    Video
  };

  /*
  * Gets a human recognisable name for common Media Foundation GUIDs.
  */
  LPCSTR GetGUIDNameConst(const GUID& guid)
  {
    IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_SUBTYPE);
    IF_EQUAL_RETURN(guid, MF_MT_ALL_SAMPLES_INDEPENDENT);
    IF_EQUAL_RETURN(guid, MF_MT_FIXED_SIZE_SAMPLES);
    IF_EQUAL_RETURN(guid, MF_MT_COMPRESSED);
    IF_EQUAL_RETURN(guid, MF_MT_SAMPLE_SIZE);
    IF_EQUAL_RETURN(guid, MF_MT_WRAPPED_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_NUM_CHANNELS);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BLOCK_ALIGNMENT);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BITS_PER_SAMPLE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_VALID_BITS_PER_SAMPLE);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_BLOCK);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_CHANNEL_MASK);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FOLDDOWN_MATRIX);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKREF);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKTARGET);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGREF);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGTARGET);
    IF_EQUAL_RETURN(guid, MF_MT_AUDIO_PREFER_WAVEFORMATEX);
    IF_EQUAL_RETURN(guid, MF_MT_AAC_PAYLOAD_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_SIZE);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MAX);
    IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MIN);
    IF_EQUAL_RETURN(guid, MF_MT_PIXEL_ASPECT_RATIO);
    IF_EQUAL_RETURN(guid, MF_MT_DRM_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_PAD_CONTROL_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_SOURCE_CONTENT_HINT);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_CHROMA_SITING);
    IF_EQUAL_RETURN(guid, MF_MT_INTERLACE_MODE);
    IF_EQUAL_RETURN(guid, MF_MT_TRANSFER_FUNCTION);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_PRIMARIES);
    IF_EQUAL_RETURN(guid, MF_MT_CUSTOM_VIDEO_PRIMARIES);
    IF_EQUAL_RETURN(guid, MF_MT_YUV_MATRIX);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_LIGHTING);
    IF_EQUAL_RETURN(guid, MF_MT_VIDEO_NOMINAL_RANGE);
    IF_EQUAL_RETURN(guid, MF_MT_GEOMETRIC_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_MINIMUM_DISPLAY_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_APERTURE);
    IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_ENABLED);
    IF_EQUAL_RETURN(guid, MF_MT_AVG_BITRATE);
    IF_EQUAL_RETURN(guid, MF_MT_AVG_BIT_ERROR_RATE);
    IF_EQUAL_RETURN(guid, MF_MT_MAX_KEYFRAME_SPACING);
    IF_EQUAL_RETURN(guid, MF_MT_DEFAULT_STRIDE);
    IF_EQUAL_RETURN(guid, MF_MT_PALETTE);
    IF_EQUAL_RETURN(guid, MF_MT_USER_DATA);
    IF_EQUAL_RETURN(guid, MF_MT_AM_FORMAT_TYPE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG_START_TIME_CODE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_PROFILE);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_LEVEL);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG2_FLAGS);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG_SEQUENCE_HEADER);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_0);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_0);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_1);
    IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_1);
    IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_SRC_PACK);
    IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_CTRL_PACK);
    IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_HEADER);
    IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_FORMAT);
    IF_EQUAL_RETURN(guid, MF_MT_IMAGE_LOSS_TOLERANT);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG4_SAMPLE_DESCRIPTION);
    IF_EQUAL_RETURN(guid, MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY);
    IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_4CC);
    IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_WAVE_FORMAT_TAG);

    // Media types

    IF_EQUAL_RETURN(guid, MFMediaType_Audio);
    IF_EQUAL_RETURN(guid, MFMediaType_Video);
    IF_EQUAL_RETURN(guid, MFMediaType_Protected);
    IF_EQUAL_RETURN(guid, MFMediaType_SAMI);
    IF_EQUAL_RETURN(guid, MFMediaType_Script);
    IF_EQUAL_RETURN(guid, MFMediaType_Image);
    IF_EQUAL_RETURN(guid, MFMediaType_HTML);
    IF_EQUAL_RETURN(guid, MFMediaType_Binary);
    IF_EQUAL_RETURN(guid, MFMediaType_FileTransfer);

    IF_EQUAL_RETURN(guid, MFVideoFormat_AI44); //     FCC('AI44')
    IF_EQUAL_RETURN(guid, MFVideoFormat_ARGB32); //   D3DFMT_A8R8G8B8 
    IF_EQUAL_RETURN(guid, MFVideoFormat_AYUV); //     FCC('AYUV')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DV25); //     FCC('dv25')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DV50); //     FCC('dv50')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVH1); //     FCC('dvh1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVSD); //     FCC('dvsd')
    IF_EQUAL_RETURN(guid, MFVideoFormat_DVSL); //     FCC('dvsl')
    IF_EQUAL_RETURN(guid, MFVideoFormat_H264); //     FCC('H264')
    IF_EQUAL_RETURN(guid, MFVideoFormat_I420); //     FCC('I420')
    IF_EQUAL_RETURN(guid, MFVideoFormat_IYUV); //     FCC('IYUV')
    IF_EQUAL_RETURN(guid, MFVideoFormat_M4S2); //     FCC('M4S2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MJPG);
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP43); //     FCC('MP43')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP4S); //     FCC('MP4S')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MP4V); //     FCC('MP4V')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MPG1); //     FCC('MPG1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MSS1); //     FCC('MSS1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_MSS2); //     FCC('MSS2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_NV11); //     FCC('NV11')
    IF_EQUAL_RETURN(guid, MFVideoFormat_NV12); //     FCC('NV12')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P010); //     FCC('P010')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P016); //     FCC('P016')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P210); //     FCC('P210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_P216); //     FCC('P216')
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB24); //    D3DFMT_R8G8B8 
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB32); //    D3DFMT_X8R8G8B8 
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB555); //   D3DFMT_X1R5G5B5 
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB565); //   D3DFMT_R5G6B5 
    IF_EQUAL_RETURN(guid, MFVideoFormat_RGB8);
    IF_EQUAL_RETURN(guid, MFVideoFormat_UYVY); //     FCC('UYVY')
    IF_EQUAL_RETURN(guid, MFVideoFormat_v210); //     FCC('v210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_v410); //     FCC('v410')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV1); //     FCC('WMV1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV2); //     FCC('WMV2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WMV3); //     FCC('WMV3')
    IF_EQUAL_RETURN(guid, MFVideoFormat_WVC1); //     FCC('WVC1')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y210); //     FCC('Y210')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y216); //     FCC('Y216')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y410); //     FCC('Y410')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y416); //     FCC('Y416')
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y41P);
    IF_EQUAL_RETURN(guid, MFVideoFormat_Y41T);
    IF_EQUAL_RETURN(guid, MFVideoFormat_YUY2); //     FCC('YUY2')
    IF_EQUAL_RETURN(guid, MFVideoFormat_YV12); //     FCC('YV12')
    IF_EQUAL_RETURN(guid, MFVideoFormat_YVYU);

    IF_EQUAL_RETURN(guid, MFAudioFormat_PCM); //              WAVE_FORMAT_PCM 
    IF_EQUAL_RETURN(guid, MFAudioFormat_Float); //            WAVE_FORMAT_IEEE_FLOAT 
    IF_EQUAL_RETURN(guid, MFAudioFormat_DTS); //              WAVE_FORMAT_DTS 
    IF_EQUAL_RETURN(guid, MFAudioFormat_Dolby_AC3_SPDIF); //  WAVE_FORMAT_DOLBY_AC3_SPDIF 
    IF_EQUAL_RETURN(guid, MFAudioFormat_DRM); //              WAVE_FORMAT_DRM 
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV8); //        WAVE_FORMAT_WMAUDIO2 
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV9); //        WAVE_FORMAT_WMAUDIO3 
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudio_Lossless); // WAVE_FORMAT_WMAUDIO_LOSSLESS 
    IF_EQUAL_RETURN(guid, MFAudioFormat_WMASPDIF); //         WAVE_FORMAT_WMASPDIF 
    IF_EQUAL_RETURN(guid, MFAudioFormat_MSP1); //             WAVE_FORMAT_WMAVOICE9 
    IF_EQUAL_RETURN(guid, MFAudioFormat_MP3); //              WAVE_FORMAT_MPEGLAYER3 
    IF_EQUAL_RETURN(guid, MFAudioFormat_MPEG); //             WAVE_FORMAT_MPEG 
    IF_EQUAL_RETURN(guid, MFAudioFormat_AAC); //              WAVE_FORMAT_MPEG_HEAAC 
    IF_EQUAL_RETURN(guid, MFAudioFormat_ADTS); //             WAVE_FORMAT_MPEG_ADTS_AAC 

    return NULL;
  }

  /**
  * Helper function to get a user friendly description for a media type.
  * Note that there may be properties missing or incorrectly described.
  * @param[in] pMediaType: pointer to the media type to get a description for.
  * @@Returns A string describing the media type.
  *
  * Potential improvements https://docs.microsoft.com/en-us/windows/win32/medfound/media-type-debugging-code.
  */
  std::string GetMediaTypeDescription(IMFMediaType* pMediaType)
  {
    HRESULT hr = S_OK;
    GUID MajorType;
    UINT32 cAttrCount;
    LPCSTR pszGuidStr;
    std::string description;
    WCHAR TempBuf[200];

    if (pMediaType == NULL) {
      description = "<NULL>";
    }
    else {
      CHECKHR_THROW(pMediaType->GetMajorType(&MajorType),
        "Failed to get major media type.");

      //pszGuidStr = STRING_FROM_GUID(MajorType);
      pszGuidStr = GetGUIDNameConst(MajorType);
      if (pszGuidStr != NULL)
      {
        description += pszGuidStr;
        description += ": ";
      }
      else
      {
        description += "Other: ";
      }

      CHECKHR_THROW(pMediaType->GetCount(&cAttrCount),
        "Failed to get attribute count.");

      for (UINT32 i = 0; i < cAttrCount; i++)
      {
        GUID guidId;
        MF_ATTRIBUTE_TYPE attrType;

        CHECKHR_THROW(pMediaType->GetItemByIndex(i, &guidId, NULL),
          "Failed to get item bu index.");

        CHECKHR_THROW(pMediaType->GetItemType(guidId, &attrType),
          "Failed to get item type.");

        //pszGuidStr = STRING_FROM_GUID(guidId);
        pszGuidStr = GetGUIDNameConst(guidId);
        if (pszGuidStr != NULL)
        {
          description += pszGuidStr;
        }
        else
        {
          LPOLESTR guidStr = NULL;

          CHECKHR_THROW(StringFromCLSID(guidId, &guidStr),
            "Failed to get string from class ID.");
          auto wGuidStr = std::wstring(guidStr);
          description += std::string(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.

          CoTaskMemFree(guidStr);
        }

        description += "=";

        switch (attrType)
        {
        case MF_ATTRIBUTE_UINT32:
        {
          UINT32 Val;
          CHECKHR_THROW(pMediaType->GetUINT32(guidId, &Val),
            "Failed to get uint32 from media type.");

          description += std::to_string(Val);
          break;
        }
        case MF_ATTRIBUTE_UINT64:
        {
          UINT64 Val;
          CHECKHR_THROW(pMediaType->GetUINT64(guidId, &Val),
            "Failed to get uint32 from media type.");

          if (guidId == MF_MT_FRAME_SIZE)
          {
            description += "W:" + std::to_string(HI32(Val)) + " H: " + std::to_string(LO32(Val));
          }
          else if (guidId == MF_MT_FRAME_RATE)
          {
            // Frame rate is numerator/denominator.
            description += std::to_string(HI32(Val)) + "/" + std::to_string(LO32(Val));
          }
          else if (guidId == MF_MT_PIXEL_ASPECT_RATIO)
          {
            description += std::to_string(HI32(Val)) + ":" + std::to_string(LO32(Val));
          }
          else
          {
            //tempStr.Format("%ld", Val);
            description += std::to_string(Val);
          }

          //description += tempStr;

          break;
        }
        case MF_ATTRIBUTE_DOUBLE:
        {
          DOUBLE Val;
          CHECKHR_THROW(pMediaType->GetDouble(guidId, &Val),
            "Failed to get double from media type.");

          //tempStr.Format("%f", Val);
          description += std::to_string(Val);
          break;
        }
        case MF_ATTRIBUTE_GUID:
        {
          GUID Val;
          const char* pValStr;

          CHECKHR_THROW(pMediaType->GetGUID(guidId, &Val),
            "Failed to get GUID from media type.");

          //pValStr = STRING_FROM_GUID(Val);
          pValStr = GetGUIDNameConst(Val);
          if (pValStr != NULL)
          {
            description += pValStr;
          }
          else
          {
            LPOLESTR guidStr = NULL;
            CHECKHR_THROW(StringFromCLSID(Val, &guidStr),
              "Failed to get string from class ID.");
            auto wGuidStr = std::wstring(guidStr);
            description += std::string(wGuidStr.begin(), wGuidStr.end()); // GUID's won't have wide chars.

            CoTaskMemFree(guidStr);
          }

          break;
        }
        case MF_ATTRIBUTE_STRING:
        {
          hr = pMediaType->GetString(guidId, TempBuf, sizeof(TempBuf) / sizeof(TempBuf[0]), NULL);
          if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
          {
            description += "<Too Long>";
            break;
          }
          CHECKHR_THROW(hr, "Failed to gt string from media type.");
          auto wstr = std::wstring(TempBuf);
          description += std::string(wstr.begin(), wstr.end()); // It's unlikely the attribute descriptions will contain multi byte chars.

          break;
        }
        case MF_ATTRIBUTE_BLOB:
        {
          description += "<BLOB>";
          break;
        }
        case MF_ATTRIBUTE_IUNKNOWN:
        {
          description += "<UNK>";
          break;
        }
        }

        description += ", ";
      }
    }

    return description;
  }

  /**
  * Gets an audio or video source reader from a capture device such as a webcam or microphone.
  * @param[in] deviceType: the type of capture device to get a source reader for.
  * @param[in] nDevice: the capture device index to attempt to get the source reader for.
  * @param[out] ppMediaSource: will be set with the source for the reader if successful.
  * @param[out] ppVMediaReader: will be set with the reader if successful. Set this parameter
  *  to nullptr if no reader is required and only the source is needed.
  * @@Returns S_OK if successful or an error code if not.
  */
  HRESULT GetSourceFromCaptureDevice(DeviceType deviceType, UINT nDevice, IMFMediaSource** ppMediaSource, IMFSourceReader** ppMediaReader)
  {
    IMFAttributes* pDeviceConfig = NULL;
    IMFActivate** ppCaptureDevices = NULL;
    IMFAttributes* pAttributes = NULL;

    try {
      UINT32 captureDeviceCount = 0;
      UINT nameLength = 0;
      //WCHAR* deviceFriendlyName;
      HRESULT hr = S_OK;

      CHECKHR_THROW(MFCreateAttributes(&pDeviceConfig, 1),
        "Error creating capture device configuration.");

      GUID captureType = (deviceType == DeviceType::Audio) ?
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID :
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID;

      // Request video capture devices.
      CHECKHR_THROW(pDeviceConfig->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        captureType),
        "Error initialising capture device configuration object.");

      CHECKHR_THROW(MFEnumDeviceSources(pDeviceConfig, &ppCaptureDevices, &captureDeviceCount),
        "Error enumerating capture devices.");

      if (nDevice >= captureDeviceCount) {
        //printf("The device index of %d was invalid for available device count of %d.\n", nDevice, captureDeviceCount);
        //hr = E_INVALIDARG;
        throw gcnew System::ApplicationException("The device index was invalid for available device count.");
      }
      else {
        //CHECKHR_THROW(ppCaptureDevices[nDevice]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceFriendlyName, &nameLength),
        //   "Error retrieving video device friendly name.");

        //wprintf(L"Capture device friendly name: %s\n", deviceFriendlyName);

        CHECKHR_THROW(ppCaptureDevices[nDevice]->ActivateObject(IID_PPV_ARGS(ppMediaSource)),
          "Error activating capture device.");

        // Is a reader required or does the caller only want the source?
        if (ppMediaReader != nullptr) {
          CHECKHR_THROW(MFCreateAttributes(&pAttributes, 1),
            "Failed to create attributes.");

          if (deviceType == DeviceType::Video) {
            // Adding this attribute creates a video source reader that will handle
            // colour conversion and avoid the need to manually convert between RGB24 and RGB32 etc.
            CHECKHR_THROW(pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1),
              "Failed to set enable video processing attribute.");
          }

          // Create a source reader.
          CHECKHR_THROW(MFCreateSourceReaderFromMediaSource(
            *ppMediaSource,
            pAttributes,
            ppMediaReader),
          "Error creating media source reader.");
        }
      }

      return hr;
    }
    finally {

      SAFE_RELEASE(pDeviceConfig);
      SAFE_RELEASE(ppCaptureDevices);
      SAFE_RELEASE(pAttributes);
    }
  }

  /**
  * Gets the default stride for a video media type.
  * @param[in] pType: pointer to the video media type to get the stride for.
  * @param[out] plStride: if successful the pointer value will be set to the stride.
  * @@Returns: S_OK if successful or throws if a failure occurs.
  */
  HRESULT GetDefaultStride(IMFMediaType* pType, /* out */ LONG* plStride)
  {
    LONG lStride = 0;

    // Try to get the default stride from the media type.
    HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (FAILED(hr)) {
      // Attribute not set. Try to calculate the default stride.

      GUID subtype = GUID_NULL;

      UINT32 width = 0;
      UINT32 height = 0;

      // Get the subtype and the image size.
      CHECKHR_THROW(pType->GetGUID(MF_MT_SUBTYPE, &subtype),
        "Failed to get video sub type from video media type.");

      CHECKHR_THROW(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height),
        "Failed to get attribute size from media type.");

      CHECKHR_THROW(MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride),
        "Failed to get string for bitmap header info.");

      hr = S_OK;
    }

    if (SUCCEEDED(hr)) {
      *plStride = lStride;
    }

    return hr;
  }

  /* Used to describe the modes of the attached video devices. */
  public ref class VideoMode
  {
  public:
    String^ DeviceFriendlyName;
    int DeviceIndex;
    UInt32 Width;
    UInt32 Height;
    VideoSubTypesEnum VideoSubType;
  };

  public ref class MediaSampleProperties
  {
  public:
    bool Success;
    bool HasVideoSample;
    bool HasAudioSample;
    bool EndOfStream;
    String^ Error;
    int Width;
    int Height;
    int Stride;
    Guid VideoSubType;
    String^ VideoSubTypeFriendlyName;
    UInt64 Timestamp;
    UInt32 FrameCount;								// Number of audio of video frames contained in the raw sample.
    UInt64 NowMilliseconds;						// The current time the sample was received in millisecond resolution.

    MediaSampleProperties() :
      Success(true),
      HasVideoSample(false),
      HasAudioSample(false),
      EndOfStream(false),
      Error(),
      Width(0),
      Height(0),
      Stride(0),
      VideoSubType(Guid::Empty),
      VideoSubTypeFriendlyName(),
      Timestamp(0),
      FrameCount(0)
    {}

    MediaSampleProperties(const MediaSampleProperties% copy)
    {
      Success = copy.Success;
      HasVideoSample = copy.HasVideoSample;
      HasAudioSample = copy.HasAudioSample;
      Error = copy.Error;
      Width = copy.Width;
      Height = copy.Height;
      VideoSubType = copy.VideoSubType;
      VideoSubTypeFriendlyName = copy.VideoSubTypeFriendlyName;
      Timestamp = copy.Timestamp;
    }
  };
}

