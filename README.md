[![Build status](https://ci.appveyor.com/api/projects/status/u8nmgpkowce2q4fb/branch/master?svg=true)](https://ci.appveyor.com/project/sipsorcery/sipsorcery-9ql6k/branch/master)

**Update: As of Sep 2020 this library has been replaced by a combination of new C# features in the main [SIPSorcery](https://github.com/sipsorcery/sipsorcery) library and Windows audio and video device access plus VP8 codec hooks in a new [SIPSorceryMedia.Windows](https://github.com/sipsorcery/SIPSorceryMedia.Windows) library. It is not envisaged that this library will continue to be updated or maintained.**

This repository contains a companion .Net Core 3.1 compatible library to the [SIPSorcery SIP and WebRTC library](https://github.com/sipsorcery/sipsorcery). This library provides wrappers and integrations for a number of open source libraries and the functions to facilitate [WebRTC](https://www.w3.org/TR/webrtc/) communications:

 - [OpenSSL](https://www.openssl.org/) - the DTLS handshake to negotiate the SRTP keying material.
 - [libsrtp](https://github.com/cisco/libsrtp) - for the Secure Realtime Transport Protocol.
 - [libvpx](https://www.webmproject.org/code/) - for VPX codecs (currently only VP8 is wired up).
 - [ffmpeg](https://www.ffmpeg.org/) - for some image conversion functions.
 
In addition [Microsoft's Media Foundation Win32 API](https://docs.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk) is used to provide access to Windows audio and video capture devices.

## Building

Use `vcpkg` to install the dependencies.

- Clone `vcpkg` from the [github repository](https://github.com/Microsoft/vcpkg) and install as per the instructions in the main README.md.
- Install the required packages:

```
PS >.\vcpkg install --triplet x64-windows openssl libvpx ffmpeg libsrtp
```

Open `src\SIPSorceryMedia.sln` with Visual Studio and build or use the `Native Tools Command Prompt for Visual Studio`:

````
msbuild /m src\SIPSorceryMedia.sln /p:Configuration=Release /p:Platform=x64 /t:clean,build
````

## Installing

This library can be used by .Net Core 3.1 applications on Windows. The library can either be built from source as described above or it can be installed via nuget using:

````
Install-Package SIPSorceryMedia -pre
````

````
dotnet add package SIPSorceryMedia --version "4.0.58-pre"
````

## Getting Started

There are a number of sample applications available in the main SIPSorcery library repository.

[WebRTCTestPatternServer](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCExamples/WebRTCTestPatternServer): The simplest example. This program serves up a test pattern video stream to a WebRTC peer.

[WebRTCServer](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCExamples/WebRTCServer): This example extends the test pattern example and can act as a media source for a peer. It has two source options:
 - An mp4 file.
 - Capture devices (webcam and microphone). The example includes an html file which runs in a Browser and will connect to a sample program running on the same machine.
 
[WebRTCReceiver](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCExamples/WebRTCReceiver  ): A receive only example. It attempts to connect to a WebRTC peer and display the video stream that it receives.
