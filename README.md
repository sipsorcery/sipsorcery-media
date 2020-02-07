[![Build status](https://ci.appveyor.com/api/projects/status/u8nmgpkowce2q4fb/branch/master?svg=true)](https://ci.appveyor.com/project/sipsorcery/sipsorcery-9ql6k/branch/master)

This repository contains a companion .Net Core 3.1 compatible library to the [SIPSorcery SIP and WebRTC library](https://github.com/sipsorcery/sipsorcery). This library provides wrappers and integrations for a number of open source libraries provide the functions to facilitate [WebRTC](https://www.w3.org/TR/webrtc/) communications:

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
dotnet add package SIPSorceryMedia --version "4.0.13-pre"
````

## Getting Started

There are a number of sample applications available in the main SIPSorcery library repository.

[WebRTCTestPatternServer](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCTestPatternServer): The simplest example. This program serves up a test pattern video stream to a WebRTC peer.

[WebRTCServer](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCServer): This example extends the test pattern example and can act as a media source for a peer. It has two source options:
 - An mp4 file.
 - Capture devices (webcam and microphone). The example includes an html file which runs in a Browser and will connect to a sample program running on the same machine.
 
[WebRTCReceiver](https://github.com/sipsorcery/sipsorcery/tree/master/examples/WebRTCReceiver): A receive only example. It attempts to connect to a WebRTC peer and display the video stream that it receives.

A primary use case of this library, and one that's demonstrated in all the sample applications, is the DTLS handshake to initialise SRTP keying material. The method shown below demonstrates the approach.

Due to the way OpenSSL works the DTLS handshake can be achieved by simply handing over the socket handle to the `dtls.DoHandshakeAsServer` method. There's no need to do any UDP sends or receives. OpenSSL takes care of the handshake and setting the key material from the Diffie-Hellman exchange. In a similar manner the SRTP contexts can be initialised by passing a reference to the `DtlsHandshake` class.

````csharp
using SIPSorcery.Net; // This namespace is from the main SIPSorcery library.
using SIPSorceryMedia;

private const string DTLS_CERTIFICATE_PATH = "certs/localhost.pem";
private const string DTLS_KEY_PATH = "certs/localhost_key.pem";

/// <summary>
/// Hands the socket handle to the DTLS context and waits for the handshake to complete.
/// </summary>
/// <param name="webRtcSession">The WebRTC session to perform the DTLS handshake on.</param>
private static bool DoDtlsHandshake(WebRtcSession webRtcSession)
{
	logger.LogDebug("DoDtlsHandshake started.");

	if (!File.Exists(DTLS_CERTIFICATE_PATH))
	{
		throw new ApplicationException($"The DTLS certificate file could not be found at {DTLS_CERTIFICATE_PATH}.");
	}
	else if (!File.Exists(DTLS_KEY_PATH))
	{
		throw new ApplicationException($"The DTLS key file could not be found at {DTLS_KEY_PATH}.");
	}

	var dtls = new DtlsHandshake(DTLS_CERTIFICATE_PATH, DTLS_KEY_PATH);
	webRtcSession.OnClose += (reason) => dtls.Shutdown();
	
	int res = dtls.DoHandshakeAsServer((ulong)webRtcSession.RtpSession.RtpChannel.RtpSocket.Handle);

	logger.LogDebug("DtlsContext initialisation result=" + res);

	if (dtls.IsHandshakeComplete())
	{
		logger.LogDebug("DTLS negotiation complete.");

		var srtpSendContext = new Srtp(dtls, false);
		var srtpReceiveContext = new Srtp(dtls, true);

		webRtcSession.RtpSession.SetSecurityContext(
			srtpSendContext.ProtectRTP,
			srtpReceiveContext.UnprotectRTP,
			srtpSendContext.ProtectRTCP,
			srtpReceiveContext.UnprotectRTCP);

		webRtcSession.IsDtlsNegotiationComplete = true;

		return true;
	}
	else
	{
		return false;
	}
}
````
