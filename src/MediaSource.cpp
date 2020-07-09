//-----------------------------------------------------------------------------
// Filename: MediaSource.cpp
//
// Description: See header.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#include "MediaSource.h"

namespace SIPSorceryMedia {

  /*
  * Default constructor.
  */
  MediaSource::MediaSource()
  {
    if (!_isInitialised)
    {
      _isInitialised = true;

      CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

      CHECKHR_THROW(MFStartup(MF_VERSION),
        "Media Foundation initialisation failed.");

      // Register the color converter DSP for this process, in the video 
      // processor category. This will enable the sink writer to enumerate
      // the color converter when the sink writer attempts to match the
      // media types.
      CHECKHR_THROW(MFTRegisterLocalByCLSID(
        __uuidof(CColorConvertDMO),
        MFT_CATEGORY_VIDEO_PROCESSOR,
        L"",
        MFT_ENUM_FLAG_SYNCMFT,
        0,
        NULL,
        0,
        NULL),
        "Registration of colour converter failed.");
    }
  }

  /*
  * Default destructor.
  */
  MediaSource::~MediaSource()
  {
    if (_sourceReader != NULL) {
      _sourceReader->Release();
    }
  }

  /*
  * Shuts down and cleans up the source reader. Ends the sampling session.
  */
  void MediaSource::Shutdown()
  {
    if (_sourceReader != NULL) {
      _sourceReader->Release();
      _sourceReader = NULL;
    }
  }

  /*
  * Initialises the media source using system capture devices.
  */
  HRESULT MediaSource::Init(int audioDeviceIndex, int videoDeviceIndex, VideoSubTypesEnum videoSubType, UInt32 width, UInt32 height)
  {
    const GUID MF_INPUT_FORMAT = VideoSubTypes::GetGuidForVideoSubType(videoSubType);
    IMFMediaSource* pVideoSource = NULL, * pAudioSource = NULL;
    IMFMediaSource* pAggSource = NULL;
    IMFCollection* pCollection = NULL;
    IMFMediaType* pVideoType = NULL, * pInputVideoType = NULL, * pAudioOutType = NULL;

    try {
      _width = width;
      _height = height;
      _isLiveSource = true;

      // Get the sources for the video and audio capture devices.
      CHECKHR_THROW(GetSourceFromCaptureDevice(DeviceType::Video, videoDeviceIndex, &pVideoSource, nullptr),
        "Failed to get video source and reader.");

      CHECKHR_THROW(GetSourceFromCaptureDevice(DeviceType::Audio, audioDeviceIndex, &pAudioSource, nullptr),
        "Failed to get video source and reader.");

      // Combine the two into an aggregate source and create a reader.
      CHECKHR_THROW(MFCreateCollection(&pCollection), "Failed to create source collection.");
      CHECKHR_THROW(pCollection->AddElement(pVideoSource), "Failed to add video source to collection.");
      CHECKHR_THROW(pCollection->AddElement(pAudioSource), "Failed to add audio source to collection.");

      CHECKHR_THROW(MFCreateAggregateSource(pCollection, &pAggSource),
        "Failed to create aggregate source.");

      // Create the source readers. Need to pin the video reader as it's a managed resource being access by native code.
      cli::pin_ptr<IMFSourceReader*> pinnedVideoReader = &_sourceReader;

      CHECKHR_THROW(MFCreateSourceReaderFromMediaSource(
        pAggSource,
        NULL,
        reinterpret_cast<IMFSourceReader**>(pinnedVideoReader)), "Error creating video source reader.");

      FindVideoMode(_sourceReader, MF_INPUT_FORMAT, width, height, &pInputVideoType);

      if (pInputVideoType == NULL) {
        throw gcnew System::ApplicationException("The specified media type could not be found for the MF video reader.");
      }
      else {
        CHECKHR_THROW(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pInputVideoType),
          "Error setting video reader media type.");

        CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
          (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
          &pVideoType),
          "Error retrieving current media type from first video stream.");

        long stride = -1;
        CHECKHR_THROW(GetDefaultStride(pVideoType, &stride), "There was an error retrieving the stride for the media type.");
        _stride = (int)stride;

        Console::WriteLine("Webcam Video Description:");
        std::cout << GetMediaTypeDescription(pVideoType) << std::endl;
      }

      CHECKHR_THROW(MFCreateMediaType(&pAudioOutType), "Failed to create media type.");
      CHECKHR_THROW(pAudioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Failed to set output media major type.");
      CHECKHR_THROW(pAudioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Failed to set output audio sub type (PCM).");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "Failed to set audio output to mono.");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Failed to set audio bits per sample.");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 8000), "Failed to set audio samples per second.");

      CHECKHR_THROW(_sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pAudioOutType),
        "Failed to set audio media type on source reader.");

      // Iterate through the source reader streams to identify the audio and video stream indexes.
      CHECKHR_THROW(SetStreamIndexes(), "Failed to set stream indexes.");

      return S_OK;
    }
    finally {
      SAFE_RELEASE(pVideoSource);
      SAFE_RELEASE(pAudioSource);
      SAFE_RELEASE(pAggSource);
      SAFE_RELEASE(pCollection);
      SAFE_RELEASE(pVideoType);
      SAFE_RELEASE(pInputVideoType);
      SAFE_RELEASE(pAudioOutType);
    }
  }

  /*
  * Initialises the media source using an MP4 file.
  */
  HRESULT MediaSource::Init(String^ path, bool loop)
  {
    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

    IMFSourceResolver* pSourceResolver = nullptr;
    IUnknown* uSource = nullptr;
    IMFMediaSource* mediaFileSource = nullptr;
    IMFAttributes* mediaFileConfig = nullptr;
    IMFMediaType* pVideoOutType = nullptr, * pAudioOutType = nullptr;
    IMFMediaType* videoType = nullptr, * audioType = nullptr;

    try {

      std::wstring pathNative = msclr::interop::marshal_as<std::wstring>(path);
      _loop = loop;

      // Create the source resolver.
      CHECKHR_THROW(MFCreateSourceResolver(&pSourceResolver), "MFCreateSourceResolver failed.");

      // Use the source resolver to create the media source.
      CHECKHR_THROW(pSourceResolver->CreateObjectFromURL(
        pathNative.c_str(),
        MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
        NULL,                       // Optional property store.
        &ObjectType,        // Receives the created object type. 
        &uSource            // Receives a pointer to the media source.
      ), "CreateObjectFromURL failed.");

      // Get the IMFMediaSource interface from the media source.
      CHECKHR_THROW(uSource->QueryInterface(IID_PPV_ARGS(&mediaFileSource)),
        "Failed to get IMFMediaSource.");

      CHECKHR_THROW(MFCreateAttributes(&mediaFileConfig, 2),
        "Failed to create MF attributes.");

      CHECKHR_THROW(mediaFileConfig->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
        "Failed to set the source attribute type for reader configuration.");

      CHECKHR_THROW(mediaFileConfig->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1),
        "Failed to set enable video processing attribute type for reader configuration.");

      // Create the source readers. Need to pin the video reader as it's a managed resource being access by native code.
      cli::pin_ptr<IMFSourceReader*> pinnedVideoReader = &_sourceReader;

      CHECKHR_THROW(MFCreateSourceReaderFromMediaSource(
        mediaFileSource,
        mediaFileConfig,
        reinterpret_cast<IMFSourceReader**>(pinnedVideoReader)),
        "Error creating video source reader.");

      CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &videoType), "Error retrieving current media type from first video stream.");

      Console::WriteLine("Source File Video Description:");
      std::cout << GetMediaTypeDescription(videoType) << std::endl;

      CHECKHR_THROW(MFCreateMediaType(&pVideoOutType), "Failed to create output media type.");
      CHECKHR_THROW(pVideoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set output media major type.");
      CHECKHR_THROW(pVideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420), "Failed to set output media sub type (I420).");

      CHECKHR_THROW(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoOutType),
        "Error setting video reader media type.");

      CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &videoType),
        "Error retrieving current media type from first video stream.");

      std::cout << "Output Video Description:" << std::endl;
      std::cout << GetMediaTypeDescription(videoType) << std::endl;

      // Get the frame dimensions and stride
      UINT32 nWidth, nHeight;
      MFGetAttributeSize(videoType, MF_MT_FRAME_SIZE, &nWidth, &nHeight);
      _width = nWidth;
      _height = nHeight;

      long stride = -1;
      CHECKHR_THROW(GetDefaultStride(videoType, &stride),
        "There was an error retrieving the stride for the media type.");
      _stride = (int)stride;

      // Set audio type.
      CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        &audioType),
        "Error retrieving current type from first audio stream.");

      std::cout << GetMediaTypeDescription(audioType) << std::endl;

      CHECKHR_THROW(MFCreateMediaType(&pAudioOutType), "Failed to create output media type.");
      CHECKHR_THROW(pAudioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Failed to set output media major type.");
      CHECKHR_THROW(pAudioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Failed to set output audio sub type (PCM).");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "Failed to set audio output to mono.");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Failed to set audio bits per sample.");
      CHECKHR_THROW(pAudioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 8000), "Failed to set audio samples per second.");

      CHECKHR_THROW(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pAudioOutType),
        "Error setting reader audio type.");

      CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        &audioType), "Error retrieving current type from first audio stream.");

      std::cout << "Output Audio Description:" << std::endl;
      std::cout << GetMediaTypeDescription(audioType) << std::endl;

      // Iterate through the source reader streams to identify the audio and video stream indexes.
      CHECKHR_THROW(SetStreamIndexes(), "Failed to set stream indexes.");

      return S_OK;
    }
    finally {
      SAFE_RELEASE(pSourceResolver);
      SAFE_RELEASE(uSource);
      SAFE_RELEASE(mediaFileSource);
      SAFE_RELEASE(mediaFileConfig);
      SAFE_RELEASE(pVideoOutType);
      SAFE_RELEASE(pAudioOutType);
      SAFE_RELEASE(videoType);
      SAFE_RELEASE(audioType);
    }
  }

  /*
  * Requests a media sample from the source reader.
  */
  MediaSampleProperties^ MediaSource::GetSample(/* out */ array<Byte>^% buffer)
  {
    MediaSampleProperties^ sampleProps = gcnew MediaSampleProperties();

    if (_sourceReader == nullptr) {
      sampleProps->Success = false;
      sampleProps->Error = "Source reader is not initialised.";
      return sampleProps;
    }
    else {

      IMFSample* pSample = nullptr;
      IMFMediaBuffer* pVideoBuffer;
      IMFMediaType* pVideoType = nullptr;

      try {
        DWORD streamIndex, flags;
        LONGLONG sampleTimestamp;

        CHECKHR_THROW(_sourceReader->ReadSample(
          MF_SOURCE_READER_ANY_STREAM,
          0,                              // Flags.
          &streamIndex,                   // Receives the actual stream index. 
          &flags,                         // Receives status flags.
          &sampleTimestamp,               // Receives the time stamp.
          &pSample                         // Receives the sample or NULL.
        ), L"Error reading media sample.");

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
          std::cout << "End of stream." << std::endl;
          sampleProps->EndOfStream = true;

          if (_loop) {
            std::cout << "resetting media source position to start." << std::endl;

            PROPVARIANT var = { 0 };
            var.vt = VT_I8;
            CHECKHR_THROW(_sourceReader->SetCurrentPosition(GUID_NULL, var),
              "Failed to set source reader position.");

            CHECKHR_THROW(_sourceReader->Flush(_audioStreamIndex),
              "Failed to flush the audio stream.");

            CHECKHR_THROW(_sourceReader->Flush(_videoStreamIndex),
              "Failed to flush the video stream.");

            _prevSampleTs = 0;
          }
        }

        if (flags & MF_SOURCE_READERF_NEWSTREAM)
        {
          std::cout << "New stream." << std::endl;
        }

        if (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
        {
          std::cout << "Native type changed." << std::endl;
        }

        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
          std::cout << "Current type changed for stream index " << streamIndex << "." << std::endl;

          if (streamIndex == _videoStreamIndex) {
            CHECKHR_THROW(_sourceReader->GetCurrentMediaType(
              (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
              &pVideoType), "Error retrieving current media type from first video stream.");

            std::cout << GetMediaTypeDescription(pVideoType) << std::endl;

            // Get the frame dimensions and stride
            UINT32 nWidth, nHeight;
            MFGetAttributeSize(pVideoType, MF_MT_FRAME_SIZE, &nWidth, &nHeight);
            _width = nWidth;
            _height = nHeight;

            LONG lFrameStride;
            pVideoType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lFrameStride);
            _stride = lFrameStride;

            sampleProps->Width = nWidth;
            sampleProps->Height = nHeight;
            sampleProps->Stride = lFrameStride;

            SAFE_RELEASE(pVideoType);
          }
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
          std::cout << "Stream tick." << std::endl;
        }

        if (pSample == nullptr)
        {
          std::cout << "Failed to get media sample in from source reader." << std::endl;
        }
        else
        {
          //std::cout << "Stream index " << streamIndex << ", timestamp " <<  sampleTimestamp << ", flags " << flags << "." << std::endl;

          sampleProps->Timestamp = sampleTimestamp;
          sampleProps->NowMilliseconds = std::chrono::milliseconds(std::time(NULL)).count();

          DWORD nCurrBufferCount = 0;
          CHECKHR_THROW(pSample->GetBufferCount(&nCurrBufferCount), "Failed to get the buffer count from the media sample.");
          sampleProps->FrameCount = nCurrBufferCount;

          CHECKHR_THROW(pSample->ConvertToContiguousBuffer(&pVideoBuffer), "Failed to extract the media sample into a raw buffer.");

          DWORD nCurrLen = 0;
          CHECKHR_THROW(pVideoBuffer->GetCurrentLength(&nCurrLen), "Failed to get the length of the raw buffer holding the media sample.");

          byte* imgBuff;
          DWORD buffCurrLen = 0;
          DWORD buffMaxLen = 0;
          pVideoBuffer->Lock(&imgBuff, &buffMaxLen, &buffCurrLen);

          buffer = gcnew array<Byte>(buffCurrLen);
          Marshal::Copy((IntPtr)imgBuff, buffer, 0, buffCurrLen);

          pVideoBuffer->Unlock();

          if (streamIndex == _videoStreamIndex) {
            sampleProps->Width = _width;
            sampleProps->Height = _height;
            sampleProps->Stride = _stride;
            sampleProps->HasVideoSample = true;
          }
          else if (streamIndex == _audioStreamIndex) {
            sampleProps->HasAudioSample = true;
          }

          if (!_isLiveSource && (sampleProps->HasAudioSample || sampleProps->HasVideoSample)) {

            LONGLONG samplePeriodMs = 0;
            LONGLONG wallclockPeriodMs = 0;

            if (_previousSampleAt == nullptr) {
              _previousSampleAt = new std::chrono::time_point<std::chrono::steady_clock>;
            }
            else {
              samplePeriodMs = (sampleTimestamp - _prevSampleTs) / TIMESTAMP_MILLISECOND_DIVISOR;
              wallclockPeriodMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *_previousSampleAt).count();
            }

            //printf("Samples timestamp %I64d, current timestamp %I64d.\n", sampleTimestamp / 10000, samplePeriodMs);

            if (samplePeriodMs > 0 && samplePeriodMs > wallclockPeriodMs)
            {
              LONGLONG sleepMs = samplePeriodMs - wallclockPeriodMs;
              //printf("Sleeping for %I64dms.\n", sleepMs);
              Sleep(sleepMs);
            }

            _prevSampleTs = sampleTimestamp;
            *_previousSampleAt = std::chrono::steady_clock::now();
          }
        }

        return sampleProps;
      }
      finally {
        SAFE_RELEASE(pVideoType);
        SAFE_RELEASE(pVideoBuffer);
        SAFE_RELEASE(pSample);
      }
    }
  }

  /*
  * Set the audio and video stream indexes based on how the source reader has assigned them.
  */
  HRESULT MediaSource::SetStreamIndexes()
  {
    DWORD stmIndex = 0;
    BOOL isSelected = false;

    while (_sourceReader->GetStreamSelection(stmIndex, &isSelected) == S_OK && stmIndex < MAX_STREAM_INDEX) {
      if (isSelected) {
        IMFMediaType* pTestType = NULL;
        CHECKHR_THROW(_sourceReader->GetCurrentMediaType(stmIndex, &pTestType), "Failed to get media type for selected stream.");
        GUID majorMediaType;
        pTestType->GetGUID(MF_MT_MAJOR_TYPE, &majorMediaType);
        if (majorMediaType == MFMediaType_Audio) {
          std::cout << "Audio stream index is " << stmIndex << "." << std::endl;
          _audioStreamIndex = stmIndex;
        }
        else if (majorMediaType == MFMediaType_Video) {
          std::cout << "Video stream index is " << stmIndex << "." << std::endl;
          _videoStreamIndex = stmIndex;
        }
        SAFE_RELEASE(pTestType);
      }
      stmIndex++;
    }

    return S_OK;
  }

  /*
  * Gets a list of the system's video capture devices.
  */
  HRESULT MediaSource::GetVideoDevices(/* out */ List<VideoMode^>^% devices)
  {
    devices = gcnew List<VideoMode^>();

    IMFMediaSource* videoSource = NULL;
    IMFAttributes* videoConfig = NULL;
    IMFActivate** videoDevices = NULL;
    IMFSourceReader* videoReader = NULL;

    try {

      UINT32 videoDeviceCount = 0;

      // Create an attribute store to hold the search criteria.
      CHECKHR_THROW(MFCreateAttributes(&videoConfig, 1), "Error creating video configuration.");

      // Request video capture devices.
      CHECKHR_THROW(videoConfig->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "Error initialising video configuration object.");

      // Enumerate the devices,
      CHECKHR_THROW(MFEnumDeviceSources(videoConfig, &videoDevices, &videoDeviceCount), "Error enumerating video devices.");

      for (unsigned int index = 0; index < videoDeviceCount; index++)
      {
        WCHAR* deviceFriendlyName;

        CHECKHR_THROW(videoDevices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceFriendlyName, NULL),
          "Error getting device friendly name.");

        //Console::WriteLine("Video device[{0}] name: {1}.", index, Marshal::PtrToStringUni((IntPtr)deviceFriendlyName));

        // Request video capture device.
        CHECKHR_THROW(videoDevices[index]->ActivateObject(IID_PPV_ARGS(&videoSource)), "Error activating video device.");

        // Create a source reader.
        CHECKHR_THROW(MFCreateSourceReaderFromMediaSource(
          videoSource,
          videoConfig,
          &videoReader), "Error creating video source reader.");

        DWORD dwMediaTypeIndex = 0;
        HRESULT hr = S_OK;

        while (SUCCEEDED(hr))
        {
          IMFMediaType* pType = NULL;
          hr = videoReader->GetNativeMediaType(0, dwMediaTypeIndex, &pType);
          if (hr == MF_E_NO_MORE_TYPES)
          {
            hr = S_OK;
            break;
          }
          else if (SUCCEEDED(hr))
          {
            GUID videoSubType;
            UINT32 pWidth = 0, pHeight = 0;

            hr = pType->GetGUID(MF_MT_SUBTYPE, &videoSubType);
            MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &pWidth, &pHeight);

            auto videoMode = gcnew VideoMode();
            videoMode->DeviceFriendlyName = Marshal::PtrToStringUni((IntPtr)deviceFriendlyName);
            videoMode->DeviceIndex = index;
            videoMode->Width = pWidth;
            videoMode->Height = pHeight;
            videoMode->VideoSubType = VideoSubTypes::GetVideoSubTypeForGuid(videoSubType);
            devices->Add(videoMode);
          }

          ++dwMediaTypeIndex;
          SAFE_RELEASE(pType);
        }
      }

      return S_OK;
    }
    finally {
      SAFE_RELEASE(videoSource);
      SAFE_RELEASE(videoConfig);
      SAFE_RELEASE(videoDevices);
      SAFE_RELEASE(videoReader);
    }
  }

  /*
  * Attempts to find a media type on a video source reader matching the specified parameters.
  */
  HRESULT MediaSource::FindVideoMode(IMFSourceReader* pReader, const GUID mediaSubType, UInt32 width, UInt32 height, /* out */ IMFMediaType** ppFoundType)
  {
    HRESULT hr = NULL;
    DWORD dwMediaTypeIndex = 0;

    while (hr == S_OK)
    {
      IMFMediaType* pType = NULL;
      hr = pReader->GetNativeMediaType(0, dwMediaTypeIndex, &pType);
      if (hr == MF_E_NO_MORE_TYPES) {
        break;
      }
      else if (hr == S_OK)
      {
        GUID videoSubType;
        UINT32 pWidth = 0, pHeight = 0;

        hr = pType->GetGUID(MF_MT_SUBTYPE, &videoSubType);
        CHECKHR_THROW(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &pWidth, &pHeight), "Failed to get attribute size.");

        if (hr == S_OK)
        {
          //printf("Video subtype %s, width=%i, height=%i.\n", STRING_FROM_GUID(videoSubType), pWidth, pHeight);

          if (videoSubType == mediaSubType && pWidth == width && pHeight == height)
          {
            *ppFoundType = pType;
            printf("Media type successfully located.\n");
            break;
          }
        }
      }

      ++dwMediaTypeIndex;
      SAFE_RELEASE(pType);
    }

    return S_OK;
  }
}
