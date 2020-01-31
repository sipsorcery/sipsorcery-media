#include "MFVideoSampler.h"

namespace SIPSorceryMedia {

  MFVideoSampler::MFVideoSampler()
  {
    if (!_isInitialised)
    {
      _isInitialised = true;
      CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
      MFStartup(MF_VERSION);

      // Register the color converter DSP for this process, in the video 
      // processor category. This will enable the sink writer to enumerate
      // the color converter when the sink writer attempts to match the
      // media types.
      MFTRegisterLocalByCLSID(
        __uuidof(CColorConvertDMO),
        MFT_CATEGORY_VIDEO_PROCESSOR,
        L"",
        MFT_ENUM_FLAG_SYNCMFT,
        0,
        NULL,
        0,
        NULL);
    }
  }

  MFVideoSampler::~MFVideoSampler()
  {
    if (_sourceReader != NULL) {
      _sourceReader->Release();
    }
  }

  void MFVideoSampler::Stop()
  {
    if (_sourceReader != NULL) {
      _sourceReader->Release();
      _sourceReader = NULL;
    }
  }

  HRESULT MFVideoSampler::GetVideoDevices(/* out */ List<VideoMode^>^% devices)
  {
    devices = gcnew List<VideoMode^>();

    IMFMediaSource* videoSource = NULL;
    UINT32 videoDeviceCount = 0;
    IMFAttributes* videoConfig = NULL;
    IMFActivate** videoDevices = NULL;
    IMFSourceReader* videoReader = NULL;

    // Create an attribute store to hold the search criteria.
    CHECK_HR(MFCreateAttributes(&videoConfig, 1), L"Error creating video configuation.");

    // Request video capture devices.
    CHECK_HR(videoConfig->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), L"Error initialising video configuration object.");

    // Enumerate the devices,
    CHECK_HR(MFEnumDeviceSources(videoConfig, &videoDevices, &videoDeviceCount), L"Error enumerating video devices.");

    for (int index = 0; index < videoDeviceCount; index++)
    {
      WCHAR* deviceFriendlyName;

      videoDevices[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceFriendlyName, NULL);

      //Console::WriteLine("Video device[{0}] name: {1}.", index, Marshal::PtrToStringUni((IntPtr)deviceFriendlyName));

      // Request video capture device.
      CHECK_HR(videoDevices[index]->ActivateObject(IID_PPV_ARGS(&videoSource)), L"Error activating video device.");

      // Create a source reader.
      CHECK_HR(MFCreateSourceReaderFromMediaSource(
        videoSource,
        videoConfig,
        &videoReader), L"Error creating video source reader.");

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
          videoMode->VideoSubType = FromGUID(videoSubType);
          videoMode->VideoSubTypeFriendlyName = gcnew System::String(STRING_FROM_GUID(videoSubType));
          devices->Add(videoMode);

          //devices->Add(Marshal::PtrToStringUni((IntPtr)deviceFriendlyName));

          pType->Release();
        }
        ++dwMediaTypeIndex;
      }
    }

    return S_OK;
  }

  HRESULT MFVideoSampler::Init(int videoDeviceIndex, VideoSubTypesEnum videoSubType, UInt32 width, UInt32 height)
  {
    const GUID MF_INPUT_FORMAT = VideoSubTypesHelper::GetGuidForVideoSubType(videoSubType);
    IMFMediaSource* pVideoSource = NULL, * pAudioSource = NULL;
    IMFMediaSource* pAggSource = NULL;
    IMFCollection* pCollection = NULL;
    IMFAttributes* videoConfig = NULL;
    IMFMediaType* videoType = NULL;
    IMFMediaType* desiredInputVideoType = NULL, * pAudioOutType = NULL;

    _width = width;
    _height = height;
    _isLiveSource = true;

    // Get the sources for the video and audio capture devices.

    CHECK_HR(GetSourceFromCaptureDevice(DeviceType::Video, videoDeviceIndex, &pVideoSource, nullptr),
      L"Failed to get video source and reader.");

    CHECK_HR(GetSourceFromCaptureDevice(DeviceType::Audio, 0, &pAudioSource, nullptr),
      L"Failed to get video source and reader.");

    // Combine the two into an aggregate source and create a reader.

    CHECK_HR(MFCreateCollection(&pCollection), L"Failed to create source collection.");
    CHECK_HR(pCollection->AddElement(pVideoSource), L"Failed to add video source to collection.");
    CHECK_HR(pCollection->AddElement(pAudioSource), L"Failed to add audio source to collection.");

    CHECK_HR(MFCreateAggregateSource(pCollection, &pAggSource),
      L"Failed to create aggregate source.");

    // Create the source readers. Need to pin the video reader as it's a managed resource being access by native code.
    cli::pin_ptr<IMFSourceReader*> pinnedVideoReader = &_sourceReader;

    CHECK_HR(MFCreateSourceReaderFromMediaSource(
      pAggSource,
      videoConfig,
      reinterpret_cast<IMFSourceReader**>(pinnedVideoReader)), L"Error creating video source reader.");

    FindVideoMode(_sourceReader, MF_INPUT_FORMAT, width, height, desiredInputVideoType);

    if (desiredInputVideoType == NULL) {
      printf("The specified media type could not be found for the MF video reader.\n");
    }
    else {
      CHECK_HR(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, desiredInputVideoType),
        L"Error setting video reader media type.\n");

      CHECK_HR(_sourceReader->GetCurrentMediaType(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &videoType), L"Error retrieving current media type from first video stream.");

      long stride = -1;
      CHECK_HR(GetDefaultStride(videoType, &stride), L"There was an error retrieving the stride for the media type.");
      _stride = (int)stride;

      Console::WriteLine("Webcam Video Description:");
      std::cout << GetMediaTypeDescription(videoType) << std::endl;
    }

    CHECK_HR(MFCreateMediaType(&pAudioOutType), "Failed to create media type.");
    CHECK_HR(pAudioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Failed to set output media major type.");
    CHECK_HR(pAudioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Failed to set output audio sub type (PCM).");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "Failed to set audio output to mono.");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Failed to set audio bits per sample.");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 8000), "Failed to set audio samples per second.");

    CHECK_HR(_sourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pAudioOutType),
      "Failed to set audio media type on source reader.");

    //videoConfig->Release();
    pAggSource->Release();
    videoType->Release();
    desiredInputVideoType->Release();

    // Iterate through the source reader streams to identify the audio and video stream indexes.
    CHECK_HR(SetStreamIndexes(), "Failed to set stream indexes.");

    return S_OK;
  }

  HRESULT MFVideoSampler::InitFromFile(String^ path)
  {
    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

    IMFSourceResolver* pSourceResolver = nullptr;
    IUnknown* uSource = nullptr;
    IMFMediaSource* mediaFileSource = nullptr;
    IMFAttributes* mediaFileConfig = nullptr;
    IMFMediaType* pVideoOutType = nullptr, * pAudioOutType = nullptr;
    IMFMediaType* videoType = nullptr, * audioType = nullptr;

    std::wstring pathNative = msclr::interop::marshal_as<std::wstring>(path);

    // Create the source resolver.
    CHECK_HR(MFCreateSourceResolver(&pSourceResolver), "MFCreateSourceResolver failed.");

    // Use the source resolver to create the media source.
    CHECK_HR(pSourceResolver->CreateObjectFromURL(
      pathNative.c_str(),
      MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
      NULL,                       // Optional property store.
      &ObjectType,        // Receives the created object type. 
      &uSource            // Receives a pointer to the media source.
    ), "CreateObjectFromURL failed.");

    // Get the IMFMediaSource interface from the media source.
    CHECK_HR(uSource->QueryInterface(IID_PPV_ARGS(&mediaFileSource)),
      "Failed to get IMFMediaSource.");

    CHECK_HR(MFCreateAttributes(&mediaFileConfig, 2),
      "Failed to create MF attributes.");

    CHECK_HR(mediaFileConfig->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
      "Failed to set the source attribute type for reader configuration.");

    CHECK_HR(mediaFileConfig->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1),
      "Failed to set enable video processing attribute type for reader configuration.");

    // Create the source readers. Need to pin the video reader as it's a managed resource being access by native code.
    cli::pin_ptr<IMFSourceReader*> pinnedVideoReader = &_sourceReader;

    CHECK_HR(MFCreateSourceReaderFromMediaSource(
      mediaFileSource,
      mediaFileConfig,
      reinterpret_cast<IMFSourceReader**>(pinnedVideoReader)),
      "Error creating video source reader.");

    CHECK_HR(_sourceReader->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
      &videoType), "Error retrieving current media type from first video stream.");

    Console::WriteLine("Source File Video Description:");
    std::cout << GetMediaTypeDescription(videoType) << std::endl;

    CHECK_HR(MFCreateMediaType(&pVideoOutType), "Failed to create output media type.");
    CHECK_HR(pVideoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set output media major type.");
    CHECK_HR(pVideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420), "Failed to set output media sub type (I420).");

    CHECK_HR(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoOutType),
      "Error setting video reader media type.");

    CHECK_HR(_sourceReader->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
      &videoType),
      "Error retrieving current media type from first video stream.");

    std::cout << "Output Video Description:" << std::endl;
    std::cout << GetMediaTypeDescription(videoType) << std::endl;

    GUID majorVidType;
    videoType->GetMajorType(&majorVidType);
    VideoMajorType = FromGUID(majorVidType);

    // Get the frame dimensions and stride
    UINT32 nWidth, nHeight;
    MFGetAttributeSize(videoType, MF_MT_FRAME_SIZE, &nWidth, &nHeight);
    _width = nWidth;
    _height = nHeight;

    long stride = -1;
    CHECK_HR(GetDefaultStride(videoType, &stride),
      "There was an error retrieving the stride for the media type.");
    _stride = (int)stride;

    // Set audio type.
    CHECK_HR(_sourceReader->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
      &audioType),
      "Error retrieving current type from first audio stream.");

    std::cout << GetMediaTypeDescription(audioType) << std::endl;

    CHECK_HR(MFCreateMediaType(&pAudioOutType), "Failed to create output media type.");
    CHECK_HR(pAudioOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Failed to set output media major type.");
    CHECK_HR(pAudioOutType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Failed to set output audio sub type (PCM).");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "Failed to set audio output to mono.");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Failed to set audio bits per sample.");
    CHECK_HR(pAudioOutType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 8000), "Failed to set audio samples per second.");

    CHECK_HR(_sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pAudioOutType),
      "Error setting reader audio type.");

    CHECK_HR(_sourceReader->GetCurrentMediaType(
      (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
      &audioType), "Error retrieving current type from first audio stream.");

    std::cout << "Output Audio Description:" << std::endl;
    std::cout << GetMediaTypeDescription(audioType) << std::endl;

    videoType->Release();
    audioType->Release();

    // Iterate through the source reader streams to identify the audio and video stream indexes.
    CHECK_HR(SetStreamIndexes(), "Failed to set stream indexes.");

    return S_OK;
  }

  /*
  * Set the audio and video stream indexes based on how the source reader has assigned them.
  */
  HRESULT MFVideoSampler::SetStreamIndexes()
  {
    DWORD stmIndex = 0;
    BOOL isSelected = false;

    while (_sourceReader->GetStreamSelection(stmIndex, &isSelected) == S_OK && stmIndex < MAX_STREAM_INDEX) {
      if (isSelected) {
        IMFMediaType* pTestType = NULL;
        CHECK_HR(_sourceReader->GetCurrentMediaType(stmIndex, &pTestType), "Failed to get media type for selected stream.");
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
        pTestType->Release();
      }
      stmIndex++;
    }

    return S_OK;
  }

  HRESULT MFVideoSampler::FindVideoMode(IMFSourceReader* pReader, const GUID mediaSubType, UInt32 width, UInt32 height, /* out */ IMFMediaType*& foundpType)
  {
    HRESULT hr = NULL;
    DWORD dwMediaTypeIndex = 0;

    while (SUCCEEDED(hr))
    {
      IMFMediaType* pType = NULL;
      hr = pReader->GetNativeMediaType(0, dwMediaTypeIndex, &pType);
      if (hr == MF_E_NO_MORE_TYPES)
      {
        hr = S_OK;
        break;
      }
      else if (SUCCEEDED(hr))
      {
        // Examine the media type. (Not shown.)
        //CMediaTypeTrace *nativeTypeMediaTrace = new CMediaTypeTrace(pType);
        //printf("Native media type: %s.\n", nativeTypeMediaTrace->GetString());

        GUID videoSubType;
        UINT32 pWidth = 0, pHeight = 0;

        hr = pType->GetGUID(MF_MT_SUBTYPE, &videoSubType);
        MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &pWidth, &pHeight);

        if (SUCCEEDED(hr))
        {
          //printf("Video subtype %s, width=%i, height=%i.\n", STRING_FROM_GUID(videoSubType), pWidth, pHeight);

          if (videoSubType == mediaSubType && pWidth == width && pHeight == height)
          {
            foundpType = pType;
            printf("Media type successfully located.\n");
            break;
          }
        }

        pType->Release();
      }
      ++dwMediaTypeIndex;
    }

    return S_OK;
  }

  // Gets the next available sample from the source reader.
  MediaSampleProperties^ MFVideoSampler::GetSample(/* out */ array<Byte>^% buffer)
  {
    MediaSampleProperties^ sampleProps = gcnew MediaSampleProperties();

    if (_sourceReader == nullptr) {
      sampleProps->Success = false;
      return sampleProps;
    }
    else {
      IMFSample* sample = nullptr;
      DWORD streamIndex, flags;
      LONGLONG sampleTimestamp;

      if (_playbackStart == System::DateTime::MinValue) {
        _playbackStart = System::DateTime::Now;
      }

      CHECK_HR_EXTENDED(_sourceReader->ReadSample(
        MF_SOURCE_READER_ANY_STREAM,
        0,                              // Flags.
        &streamIndex,                   // Receives the actual stream index. 
        &flags,                         // Receives status flags.
        &sampleTimestamp,               // Receives the time stamp.
        &sample                         // Receives the sample or NULL.
      ), L"Error reading media sample.");

      if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      {
        std::cout << "End of stream." << std::endl;
        sampleProps->EndOfStream = true;
      }
      else
      {
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

          IMFMediaType* videoType = nullptr;
          CHECK_HR_EXTENDED(_sourceReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            &videoType), L"Error retrieving current media type from first video stream.");

          std::cout << GetMediaTypeDescription(videoType) << std::endl;

          // Get the frame dimensions and stride
          UINT32 nWidth, nHeight;
          MFGetAttributeSize(videoType, MF_MT_FRAME_SIZE, &nWidth, &nHeight);
          _width = nWidth;
          _height = nHeight;

          LONG lFrameStride;
          videoType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lFrameStride);
          _stride = lFrameStride;

          sampleProps->Width = nWidth;
          sampleProps->Height = nHeight;
          sampleProps->Stride = lFrameStride;

          videoType->Release();
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
          std::cout << "Stream tick." << std::endl;
        }

        if (sample == nullptr)
        {
          std::cout << "Failed to get media sample in from source reader." << std::endl;
        }
        else
        {
          //std::cout << "Stream index " << streamIndex << ", timestamp " <<  sampleTimestamp << ", flags " << flags << "." << std::endl;

          sampleProps->Timestamp = sampleTimestamp;
          sampleProps->NowMilliseconds = std::chrono::milliseconds(std::time(NULL)).count();

          DWORD nCurrBufferCount = 0;
          CHECK_HR_EXTENDED(sample->GetBufferCount(&nCurrBufferCount), L"Failed to get the buffer count from the video sample.\n");
          sampleProps->FrameCount = nCurrBufferCount;

          IMFMediaBuffer* pVideoBuffer;
          CHECK_HR_EXTENDED(sample->ConvertToContiguousBuffer(&pVideoBuffer), L"Failed to extract the video sample into a raw buffer.\n");

          DWORD nCurrLen = 0;
          CHECK_HR_EXTENDED(pVideoBuffer->GetCurrentLength(&nCurrLen), L"Failed to get the length of the raw buffer holding the video sample.\n");

          byte* imgBuff;
          DWORD buffCurrLen = 0;
          DWORD buffMaxLen = 0;
          pVideoBuffer->Lock(&imgBuff, &buffMaxLen, &buffCurrLen);

          buffer = gcnew array<Byte>(buffCurrLen);
          Marshal::Copy((IntPtr)imgBuff, buffer, 0, buffCurrLen);

          pVideoBuffer->Unlock();
          pVideoBuffer->Release();
          sample->Release();

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

            auto currentPlaybackTimestamp = (Int64)(System::DateTime::Now - _playbackStart).TotalMilliseconds;

            //printf("Samples timestamp %I64d, current timestamp %I64d.\n", sampleTimestamp / 10000, currentPlaybackTimestamp);

            if (sampleTimestamp / TIMESTAMP_MILLISECOND_DIVISOR > currentPlaybackTimestamp)
            {
              //printf("Sleeping for %I64d.\n", sampleTimestamp / 10000 - currentPlaybackTimestamp);
              Sleep(sampleTimestamp / TIMESTAMP_MILLISECOND_DIVISOR - currentPlaybackTimestamp);
            }
          }
        }
      } // End of sample.

      return sampleProps;
    }
  }

  HRESULT MFVideoSampler::GetDefaultStride(IMFMediaType* pType, /* out */ LONG* plStride)
  {
    LONG lStride = 0;

    // Try to get the default stride from the media type.
    HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (FAILED(hr))
    {
      // Attribute not set. Try to calculate the default stride.

      GUID subtype = GUID_NULL;

      UINT32 width = 0;
      UINT32 height = 0;

      // Get the subtype and the image size.
      hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
      if (FAILED(hr))
      {
        goto done;
      }

      hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
      if (FAILED(hr))
      {
        goto done;
      }

      hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
      if (FAILED(hr))
      {
        goto done;
      }

      // Set the attribute for later reference.
      (void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
    }

    if (SUCCEEDED(hr))
    {
      *plStride = lStride;
    }

  done:
    return hr;
  }

  LPCSTR STRING_FROM_GUID(GUID Attr)
  {
    LPCSTR pAttrStr = NULL;

    // Generics
    INTERNAL_GUID_TO_STRING(MF_MT_MAJOR_TYPE, 6);                     // MAJOR_TYPE
    INTERNAL_GUID_TO_STRING(MF_MT_SUBTYPE, 6);                        // SUBTYPE
    INTERNAL_GUID_TO_STRING(MF_MT_ALL_SAMPLES_INDEPENDENT, 6);        // ALL_SAMPLES_INDEPENDENT   
    INTERNAL_GUID_TO_STRING(MF_MT_FIXED_SIZE_SAMPLES, 6);             // FIXED_SIZE_SAMPLES
    INTERNAL_GUID_TO_STRING(MF_MT_COMPRESSED, 6);                     // COMPRESSED
    INTERNAL_GUID_TO_STRING(MF_MT_SAMPLE_SIZE, 6);                    // SAMPLE_SIZE
    INTERNAL_GUID_TO_STRING(MF_MT_USER_DATA, 6);                      // MF_MT_USER_DATA

    // Audio
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_NUM_CHANNELS, 12);            // NUM_CHANNELS
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_SAMPLES_PER_SECOND, 12);      // SAMPLES_PER_SECOND
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12);    // AVG_BYTES_PER_SECOND
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_BLOCK_ALIGNMENT, 12);         // BLOCK_ALIGNMENT
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_BITS_PER_SAMPLE, 12);         // BITS_PER_SAMPLE
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, 12);   // VALID_BITS_PER_SAMPLE
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_SAMPLES_PER_BLOCK, 12);       // SAMPLES_PER_BLOCK
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_CHANNEL_MASK, 12);            // CHANNEL_MASK
    INTERNAL_GUID_TO_STRING(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 12);     // PREFER_WAVEFORMATEX

    // Video
    INTERNAL_GUID_TO_STRING(MF_MT_FRAME_SIZE, 6);                     // FRAME_SIZE
    INTERNAL_GUID_TO_STRING(MF_MT_FRAME_RATE, 6);                     // FRAME_RATE

    INTERNAL_GUID_TO_STRING(MF_MT_PIXEL_ASPECT_RATIO, 6);             // PIXEL_ASPECT_RATIO
    INTERNAL_GUID_TO_STRING(MF_MT_INTERLACE_MODE, 6);                 // INTERLACE_MODE
    INTERNAL_GUID_TO_STRING(MF_MT_AVG_BITRATE, 6);                    // AVG_BITRATE
    INTERNAL_GUID_TO_STRING(MF_MT_DEFAULT_STRIDE, 6);				          // STRIDE
    INTERNAL_GUID_TO_STRING(MF_MT_AVG_BIT_ERROR_RATE, 6);
    INTERNAL_GUID_TO_STRING(MF_MT_GEOMETRIC_APERTURE, 6);
    INTERNAL_GUID_TO_STRING(MF_MT_MINIMUM_DISPLAY_APERTURE, 6);
    INTERNAL_GUID_TO_STRING(MF_MT_PAN_SCAN_APERTURE, 6);
    INTERNAL_GUID_TO_STRING(MF_MT_VIDEO_NOMINAL_RANGE, 6);

    // Major type values
    INTERNAL_GUID_TO_STRING(MFMediaType_Default, 12);                 // Default
    INTERNAL_GUID_TO_STRING(MFMediaType_Audio, 12);                   // Audio
    INTERNAL_GUID_TO_STRING(MFMediaType_Video, 12);                   // Video
    INTERNAL_GUID_TO_STRING(MFMediaType_Script, 12);                  // Script
    INTERNAL_GUID_TO_STRING(MFMediaType_Image, 12);                   // Image
    INTERNAL_GUID_TO_STRING(MFMediaType_HTML, 12);                    // HTML
    INTERNAL_GUID_TO_STRING(MFMediaType_Binary, 12);                  // Binary
    INTERNAL_GUID_TO_STRING(MFMediaType_SAMI, 12);                    // SAMI
    INTERNAL_GUID_TO_STRING(MFMediaType_Protected, 12);               // Protected

    // Minor video type values
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa370819(v=vs.85).aspx
    INTERNAL_GUID_TO_STRING(MFVideoFormat_Base, 14);                  // Base
    INTERNAL_GUID_TO_STRING(MFVideoFormat_MP43, 14);                  // MP43
    INTERNAL_GUID_TO_STRING(MFVideoFormat_WMV1, 14);                  // WMV1
    INTERNAL_GUID_TO_STRING(MFVideoFormat_WMV2, 14);                  // WMV2
    INTERNAL_GUID_TO_STRING(MFVideoFormat_WMV3, 14);                  // WMV3
    INTERNAL_GUID_TO_STRING(MFVideoFormat_MPG1, 14);                  // MPG1
    INTERNAL_GUID_TO_STRING(MFVideoFormat_MPG2, 14);                  // MPG2
    INTERNAL_GUID_TO_STRING(MFVideoFormat_RGB24, 14);				          // RGB24
    INTERNAL_GUID_TO_STRING(MFVideoFormat_YUY2, 14);				          // YUY2
    INTERNAL_GUID_TO_STRING(MFVideoFormat_YV12, 14);                   // YV12
    INTERNAL_GUID_TO_STRING(MFVideoFormat_I420, 14);				          // I420

    // Minor audio type values
    INTERNAL_GUID_TO_STRING(MFAudioFormat_Base, 14);                  // Base
    INTERNAL_GUID_TO_STRING(MFAudioFormat_PCM, 14);                   // PCM
    INTERNAL_GUID_TO_STRING(MFAudioFormat_DTS, 14);                   // DTS
    INTERNAL_GUID_TO_STRING(MFAudioFormat_Dolby_AC3_SPDIF, 14);       // Dolby_AC3_SPDIF
    INTERNAL_GUID_TO_STRING(MFAudioFormat_Float, 14);                 // IEEEFloat
    INTERNAL_GUID_TO_STRING(MFAudioFormat_WMAudioV8, 14);             // WMAudioV8
    INTERNAL_GUID_TO_STRING(MFAudioFormat_WMAudioV9, 14);             // WMAudioV9
    INTERNAL_GUID_TO_STRING(MFAudioFormat_WMAudio_Lossless, 14);      // WMAudio_Lossless
    INTERNAL_GUID_TO_STRING(MFAudioFormat_WMASPDIF, 14);              // WMASPDIF
    INTERNAL_GUID_TO_STRING(MFAudioFormat_MP3, 14);                   // MP3
    INTERNAL_GUID_TO_STRING(MFAudioFormat_MPEG, 14);                  // MPEG
    INTERNAL_GUID_TO_STRING(MFAudioFormat_AAC, 14);                   // AAC

    // Media sub types
    INTERNAL_GUID_TO_STRING(WMMEDIASUBTYPE_I420, 15);                  // I420
    INTERNAL_GUID_TO_STRING(WMMEDIASUBTYPE_WVC1, 0);
    INTERNAL_GUID_TO_STRING(WMMEDIASUBTYPE_WMAudioV8, 0);
    INTERNAL_GUID_TO_STRING(MFImageFormat_RGB32, 0);

    // MP4 Media Subtypes.
    INTERNAL_GUID_TO_STRING(MF_MT_MPEG4_SAMPLE_DESCRIPTION, 6);
    INTERNAL_GUID_TO_STRING(MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY, 6);
    //INTERNAL_GUID_TO_STRING(MFMPEG4Format_MP4A, 0);

  done:
    return pAttrStr;
  }

#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return #val
#endif

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

    if (pMediaType == NULL)
    {
      description = "<NULL>";
      goto done;
    }

    hr = pMediaType->GetMajorType(&MajorType);
    CHECKHR_GOTO(hr, done);

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

    hr = pMediaType->GetCount(&cAttrCount);
    CHECKHR_GOTO(hr, done);

    for (UINT32 i = 0; i < cAttrCount; i++)
    {
      GUID guidId;
      MF_ATTRIBUTE_TYPE attrType;

      hr = pMediaType->GetItemByIndex(i, &guidId, NULL);
      CHECKHR_GOTO(hr, done);

      hr = pMediaType->GetItemType(guidId, &attrType);
      CHECKHR_GOTO(hr, done);

      //pszGuidStr = STRING_FROM_GUID(guidId);
      pszGuidStr = GetGUIDNameConst(guidId);
      if (pszGuidStr != NULL)
      {
        description += pszGuidStr;
      }
      else
      {
        LPOLESTR guidStr = NULL;

        CHECKHR_GOTO(StringFromCLSID(guidId, &guidStr), done);
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
        hr = pMediaType->GetUINT32(guidId, &Val);
        CHECKHR_GOTO(hr, done);

        description += std::to_string(Val);
        break;
      }
      case MF_ATTRIBUTE_UINT64:
      {
        UINT64 Val;
        hr = pMediaType->GetUINT64(guidId, &Val);
        CHECKHR_GOTO(hr, done);

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
        hr = pMediaType->GetDouble(guidId, &Val);
        CHECKHR_GOTO(hr, done);

        //tempStr.Format("%f", Val);
        description += std::to_string(Val);
        break;
      }
      case MF_ATTRIBUTE_GUID:
      {
        GUID Val;
        const char* pValStr;

        hr = pMediaType->GetGUID(guidId, &Val);
        CHECKHR_GOTO(hr, done);

        //pValStr = STRING_FROM_GUID(Val);
        pValStr = GetGUIDNameConst(Val);
        if (pValStr != NULL)
        {
          description += pValStr;
        }
        else
        {
          LPOLESTR guidStr = NULL;
          CHECKHR_GOTO(StringFromCLSID(Val, &guidStr), done);
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
        CHECKHR_GOTO(hr, done);
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

  done:

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
    UINT32 captureDeviceCount = 0;
    IMFAttributes* pDeviceConfig = NULL;
    IMFActivate** ppCaptureDevices = NULL;
    WCHAR* deviceFriendlyName;
    UINT nameLength = 0;
    IMFAttributes* pAttributes = NULL;

    HRESULT hr = S_OK;

    hr = MFCreateAttributes(&pDeviceConfig, 1);
    CHECK_HR(hr, "Error creating capture device configuration.");

    GUID captureType = (deviceType == DeviceType::Audio) ?
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID :
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID;

    // Request video capture devices.
    hr = pDeviceConfig->SetGUID(
      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
      captureType);
    CHECK_HR(hr, "Error initialising capture device configuration object.");

    hr = MFEnumDeviceSources(pDeviceConfig, &ppCaptureDevices, &captureDeviceCount);
    CHECK_HR(hr, "Error enumerating capture devices.");

    if (nDevice >= captureDeviceCount) {
      printf("The device index of %d was invalid for available device count of %d.\n", nDevice, captureDeviceCount);
      hr = E_INVALIDARG;
    }
    else {
      hr = ppCaptureDevices[nDevice]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceFriendlyName, &nameLength);
      CHECK_HR(hr, "Error retrieving video device friendly name.\n");

      wprintf(L"Capture device friendly name: %s\n", deviceFriendlyName);

      hr = ppCaptureDevices[nDevice]->ActivateObject(IID_PPV_ARGS(ppMediaSource));
      CHECK_HR(hr, "Error activating capture device.");

      // Is a reader required or does the caller only want the source?
      if (ppMediaReader != nullptr) {
        CHECK_HR(MFCreateAttributes(&pAttributes, 1),
          "Failed to create attributes.");

        if (deviceType == DeviceType::Video) {
          // Adding this attribute creates a video source reader that will handle
          // colour conversion and avoid the need to manually convert between RGB24 and RGB32 etc.
          CHECK_HR(pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1),
            "Failed to set enable video processing attribute.");
        }

        // Create a source reader.
        hr = MFCreateSourceReaderFromMediaSource(
          *ppMediaSource,
          pAttributes,
          ppMediaReader);
        CHECK_HR(hr, "Error creating media source reader.");
      }
    }

  done:

    SAFE_RELEASE(pDeviceConfig);
    SAFE_RELEASE(ppCaptureDevices);
    SAFE_RELEASE(pAttributes);

    return hr;
  }
}
