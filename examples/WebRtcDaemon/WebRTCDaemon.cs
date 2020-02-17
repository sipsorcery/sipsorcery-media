//-----------------------------------------------------------------------------
// Filename: WebRtcDaemon.cs
//
// Description: This class manages both the web socket and WebRTC connections from external peers.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// 04 Mar 2016	Aaron Clauson	Created, Hobart, Australia.
// 11 Aug 2019	Aaron Clauson	New attempt to get to work with audio and video rather than static test pattern.
// 16 Feb 2020  Aaron Clauson   Updated from .NET Framework 4.7.2 to .Net Core 3.1.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Create a self signed localhost certificate for use with the web socket server and that is accepted by Chrome
// NOTE: See section below about various flags that may need to be set for different browsers including trusting self 
// signed certificate importing.
//
// openssl req -config req.conf -x509 -newkey rsa:4096 -keyout private/localhost.pem -out localhost.pem -nodes -days 3650
// openssl pkcs12 -export -in localhost.pem -inkey private/localhost.pem -out localhost.pfx -nodes
//
// openssl req -config req.conf -x509 -newkey rsa:2048 -keyout winsvr19-test-key.pem -out winsvr19-test.pem -nodes -days 3650
// openssl pkcs12 -export -in winsvr19-test.pem -inkey winsvr19-test-key.pem -out winsvr19-test.pfx -nodes
//
// cat req.conf
//[ req ]
//default_bits = 2048
//default_md = sha256
//prompt = no
//encrypt_key = no
//distinguished_name = dn
//x509_extensions = x509_ext
//string_mask = utf8only
//[dn]
//CN = localhost
//[x509_ext]
//subjectAltName = localhost, IP:127.0.0.1, IP:::1 
//keyUsage = Digital Signature, Key Encipherment, Data Encipherment
//extendedKeyUsage = TLS Web Server Authentication
//
// Get thumbrpint for certificate used for DTLS:
// openssl x509 -fingerprint -sha256 -in localhost.pem
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// ffmpeg command for an audio and video container that "should" work well with this sample is:
// ToDo: Determine good output codec/format parameters.
// ffmpeg -i max.mp4 -ss 00:00:06 max_even_better.mp4
// ffmpeg -i max4.mp4 -ss 00:00:06 -vf scale=320x240 max4small.mp4
//
// To receive raw RTP samples set the RawRtpBaseEndPoint so the port matches the audio port in
// SDP below and then use ffplay as below:
//
// ffplay -i ffplay_av.sdp -protocol_whitelist "file,rtp,udp"
//
// cat ffplay_av.sdp
//v=0
//o=- 1129870806 2 IN IP4 127.0.0.1
//s=-
//c=IN IP4 192.168.11.50
//t=0 0
//m=audio 4040 RTP/AVP 0
//a=rtpmap:0 PCMU/8000
//m=video 4042 RTP/AVP 100
//a=rtpmap:100 VP8/90000
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Browser flags for webrtc testing and certificate management:
//
// Chrome: prevent hostnames using the <addr>.local format being set in ICE candidates (TODO: handle the .local hostnames)
// chrome://flags/#enable-webrtc-hide-local-ips-with-mdns
//
// Chrome: Allow a non-trusted localhost certificate for the web socket connection (this didn't seem to work)
// chrome://flags/#allow-insecure-localhost
//
// Chrome: To trust a self signed certificate:
// Add certificate to the appropriate Windows store to be trusted for web socket connection:
// Note that the steps below correspond to importing into the Windows Current User\Personal\Certificates store.
// This store can be managed directly by typing "certmgr" in the Windows search bar.
// 1. chrome://settings/?search=cert,
// 2. Click the manage certificated popup icon (next to "Manage certificates"),
// 3. Browse to the localhost.pem file and import.
//
// Chrome Canary: allow WebRtc with DTLS encryption disabled (so RTP packets can be captured and checked):
// "C:\Users\aaron\AppData\Local\Google\Chrome SxS\Application\chrome.exe" -disable-webrtc-encryption
//
// Firefox: To trust a self signed certificate:
// 1. about:preferences#privacy
// 2. Scroll down to certificates and click "View Certificates" to bring up the Certificate Manager,
// 3. Click Servers->Add Exception and in the Location type https://localhost:8081 or the address of the web socket server,
// 4. Click Get Certificate, verify the certificate using View and if happy then check the "Permanently store this exception" and
//    click the "Confirm Security Exception" button.
// 
// Firefox: to allow secure web socket (wss) connection to localhost, enter about:config in address bar.
// Search for network.stricttransportsecurity.preloadlist and set to false. (TODO: this is insecure keep looking for a better way)
// Open https://localhost:8081/ and accept risk which seems to add an exception
//
// Edge: NOTE as of 8 Sep 2019 Edge is not working with this program due to the OpenSSL/DTLS issue below.
//
// Edge: allow web socket connections with localhost (**see note above about Edge not working with openssl)
// C:\WINDOWS\system32>CheckNetIsolation LoopbackExempt -a -n=Microsoft.MicrosoftEdge_8wekyb3d8bbwe
//
// Edge:
// Does not support OpenSSL's RSA (2048 or 4096) bit certificates for DTLS which is required for the WebRTC connection,
// https://developer.microsoft.com/en-us/microsoft-edge/platform/issues/14561214/
//
// Edge: To trust a self signed certificate:
// The only approach found was to add the certificate to the Current User\Trusted Root Certificate Authorities.
// This is not ideal and is incorrect because it's a self signed certificate not a certificate authority.
// This certificate store can be access by Windows Search bar->certmgr select "Trusted Root Certificate Authority".
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// To install as a Windows Service:
// c:\Apps\WebRtcDaemon>sc create "SIPSorcery WebRTC Daemon" binpath="C:\Apps\WebRTCDaemon\WebRTCDaemon.exe" start=auto
//
// To uninstall Windows Service:
// c:\Apps\WebRtcDaemon>sc delete "SIPSorcery WebRTC Daemon" 
//-----------------------------------------------------------------------------

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Configuration;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using NAudio.Codecs;
using Serilog;
using SIPSorceryMedia;
using WebSocketSharp;
using WebSocketSharp.Server;

namespace SIPSorcery.Net.WebRtc
{
    public enum MediaSourceEnum
    {
        Max = 0,
        TestPattern = 1
    }

    public class SDPExchange : WebSocketBehavior
    {
        public MediaSourceEnum MediaSource { get; set; }

        public event Action<WebSocketSharp.Net.WebSockets.WebSocketContext, string, MediaSourceEnum> WebSocketOpened;
        public event Action<WebSocketSharp.Net.WebSockets.WebSocketContext, string, string> SDPAnswerReceived;

        public SDPExchange()
        { }

        protected override void OnMessage(MessageEventArgs e)
        {
            SDPAnswerReceived(this.Context, this.ID, e.Data);
        }

        protected override void OnOpen()
        {
            base.OnOpen();
            WebSocketOpened(this.Context, this.ID, MediaSource);
        }
    }

    public class WebRtcDaemon
    {
        private static Microsoft.Extensions.Logging.ILogger logger = SIPSorcery.Sys.Log.Logger;

        private const float TEXT_SIZE_PERCENTAGE = 0.035f;       // height of text as a percentage of the total image height
        private const float TEXT_OUTLINE_REL_THICKNESS = 0.02f; // Black text outline thickness is set as a percentage of text height in pixels
        private const int TEXT_MARGIN_PIXELS = 5;
        private const int POINTS_PER_INCH = 72;

        private const int VP8_TIMESTAMP_SPACING = 3000;
        private const int VP8_PAYLOAD_TYPE_ID = 100;
        private const int TEST_DTLS_HANDSHAKE_TIMEOUT = 10000;
        private const int DEFAULT_WEB_SOCKET_PORT = 8081;

        // Application configuration settings.
        private readonly string _webSocketCertificatePath = ConfigurationManager.AppSettings["WebSocketCertificatePath"];
        private readonly string _webSocketCertificatePassword = ConfigurationManager.AppSettings["WebSocketCertificatePassword"];
        private readonly string _dtlsCertificatePath = ConfigurationManager.AppSettings["DtlsCertificatePath"];
        private readonly string _dtlsKeyPath = ConfigurationManager.AppSettings["DtlsKeyPath"];
        private readonly string _dtlsCertificateThumbprint = ConfigurationManager.AppSettings["DtlsCertificateThumbprint"];
        private readonly string _mediaFilePath = ConfigurationManager.AppSettings["MediaFilePath"];
        private readonly string _testPattermImagePath = ConfigurationManager.AppSettings["TestPatternFilePath"];
        private readonly string _webSocketPort = ConfigurationManager.AppSettings["WebSocketPort"];

        private bool _exit = false;
        private WebSocketServer _webSocketServer;
        private SIPSorceryMedia.MediaSource _mediaSource;
        private ConcurrentDictionary<string, WebRtcSession> _webRtcSessions = new ConcurrentDictionary<string, WebRtcSession>();
        private bool _isMp4Sampling = false;
        private bool _isTestPatternSampling = false;

        private delegate void MediaSampleReadyDelegate(SDPMediaTypesEnum sampleType, uint timestamp, byte[] sample);
        private event MediaSampleReadyDelegate OnMp4MediaSampleReady;
        private event MediaSampleReadyDelegate OnTestPatternSampleReady;

        public async Task Start(CancellationToken ct)
        {
            try
            {
                AddConsoleLogger();

                logger.LogDebug("WebRTCDaemon starting.");

                var wssCertificate = new System.Security.Cryptography.X509Certificates.X509Certificate2(_webSocketCertificatePath, _webSocketCertificatePassword);
                logger.LogDebug("Web Socket Server Certificate: " + wssCertificate.Subject + ", have key " + wssCertificate.HasPrivateKey + ", Expires " + wssCertificate.GetExpirationDateString() + ".");
                logger.LogDebug($"DTLS certificate thumbprint {_dtlsCertificateThumbprint}.");
                logger.LogDebug($"Web socket port {_webSocketPort}.");

                if (!File.Exists(_mediaFilePath))
                {
                    throw new ApplicationException($"The media file at does not exist at {_mediaFilePath}.");
                }

                // Initialise OpenSSL & libsrtp, saves a couple of seconds for the first client connection.
                Console.WriteLine("Initialising OpenSSL and libsrtp...");
                DtlsHandshake.InitialiseOpenSSL();
                Srtp.InitialiseLibSrtp();

                Task.Run(DoDtlsHandshakeLoopbackTest).Wait();

                Console.WriteLine("Test DTLS handshake complete.");

                // Configure the web socket and the different end point handlers.
                int webSocketPort = (!String.IsNullOrEmpty(_webSocketPort)) ? Int32.Parse(_webSocketPort) : DEFAULT_WEB_SOCKET_PORT;
                _webSocketServer = new WebSocketServer(IPAddress.Any, webSocketPort, true);
                _webSocketServer.SslConfiguration.ServerCertificate = wssCertificate;
                _webSocketServer.SslConfiguration.CheckCertificateRevocation = false;

                // Standard encrypted WebRtc stream.
                _webSocketServer.AddWebSocketService<SDPExchange>("/max", (sdpReceiver) =>
                {
                    sdpReceiver.MediaSource = MediaSourceEnum.Max;
                    sdpReceiver.WebSocketOpened += WebRtcStartCall;
                    sdpReceiver.SDPAnswerReceived += WebRtcAnswerReceived;
                });

                if (!String.IsNullOrEmpty(_testPattermImagePath) && File.Exists(_testPattermImagePath))
                {
                    _webSocketServer.AddWebSocketService<SDPExchange>("/testpattern", (sdpReceiver) =>
                    {
                        sdpReceiver.MediaSource = MediaSourceEnum.TestPattern;
                        sdpReceiver.WebSocketOpened += WebRtcStartCall;
                        sdpReceiver.SDPAnswerReceived += WebRtcAnswerReceived;
                    });
                }

                _webSocketServer.Start();

                Console.WriteLine($"Waiting for browser web socket connection to {_webSocketServer.Address}:{_webSocketServer.Port}...");

                // Initialise the Media Foundation library that will pull the samples from the mp4 file.
                _mediaSource = new MediaSource();
                _mediaSource.Init(_mediaFilePath, true);

                while(!ct.IsCancellationRequested)
                {
                    await Task.Delay(1000);
                }
            }
            catch (Exception excp)
            {
                logger.LogError("Exception WebRTCDaemon.Start. " + excp);
            }
            finally
            {
                Stop();
            }
        }

        public void Stop()
        {
            try
            {
                logger.LogDebug("Stopping WebRtcDaemon.");

                _exit = true;

                _mediaSource.Shutdown();
                _webSocketServer.Stop();

                foreach (var session in _webRtcSessions.Values)
                {
                    session.Close("normal closure");
                }
            }
            catch (Exception excp)
            {
                logger.LogError("Exception WebRTCDaemon.Stop. " + excp);
            }
        }

        private async void WebRtcStartCall(WebSocketSharp.Net.WebSockets.WebSocketContext context, string webSocketID, MediaSourceEnum mediaSource)
        {
            logger.LogDebug($"New WebRTC client added for web socket connection {webSocketID}.");

            if (!_webRtcSessions.Any(x => x.Key == webSocketID))
            {
                var webRtcSession = new WebRtcSession(AddressFamily.InterNetwork, _dtlsCertificateThumbprint, null, null);

                webRtcSession.addTrack(SDPMediaTypesEnum.video, new List<SDPMediaFormat> { new SDPMediaFormat(SDPMediaFormatsEnum.VP8) });

                // Don't need an audio track for the test pattern feed.
                if (mediaSource == MediaSourceEnum.Max)
                {
                    webRtcSession.addTrack(SDPMediaTypesEnum.audio, new List<SDPMediaFormat> { new SDPMediaFormat(SDPMediaFormatsEnum.PCMU) });
                }

                if (_webRtcSessions.TryAdd(webSocketID, webRtcSession))
                {
                    webRtcSession.OnClose += (reason) => PeerClosed(webSocketID, reason);
                    webRtcSession.RtpSession.OnRtcpBye += (reason) => PeerClosed(webSocketID, reason);

                    var offerSdp = await webRtcSession.createOffer();
                    webRtcSession.setLocalDescription(offerSdp);

                    logger.LogDebug($"Sending SDP offer to client {context.UserEndPoint}.");

                    context.WebSocket.Send(webRtcSession.SDP.ToString());

                    if (DoDtlsHandshake(webRtcSession))
                    {
                        if (mediaSource == MediaSourceEnum.Max)
                        {
                            OnMp4MediaSampleReady += webRtcSession.SendMedia;
                            if (!_isMp4Sampling)
                            {
                                _ = Task.Run(SampleMp4Media);
                            }
                        }
                        else if (mediaSource == MediaSourceEnum.TestPattern)
                        {
                            OnTestPatternSampleReady += webRtcSession.SendMedia;
                            if (!_isTestPatternSampling)
                            {
                                _ = Task.Run(SampleTestPattern);
                            }
                        }
                    }
                    else
                    {
                        PeerClosed(webSocketID, "dtls handshake failed");
                    }
                }
                else
                {
                    logger.LogError("Failed to add new WebRTC client.");
                }
            }
        }

        /// <summary>
        /// Hands the socket handle to the DTLS context and waits for the handshake to complete.
        /// </summary>
        /// <param name="webRtcSession">The WebRTC session to perform the DTLS handshake on.</param>
        /// <returns>True if the handshake completed successfully or false otherwise.</returns>
        private bool DoDtlsHandshake(WebRtcSession webRtcSession)
        {
            logger.LogDebug("DoDtlsHandshake started.");

            if (!File.Exists(_dtlsCertificatePath))
            {
                throw new ApplicationException($"The DTLS certificate file could not be found at {_dtlsCertificatePath}.");
            }
            else if (!File.Exists(_dtlsKeyPath))
            {
                throw new ApplicationException($"The DTLS key file could not be found at {_dtlsKeyPath}.");
            }

            var dtls = new DtlsHandshake(_dtlsCertificatePath, _dtlsKeyPath);
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

        private void PeerClosed(string callID, string reason)
        {
            try
            {
                logger.LogDebug($"WebRTC session closed for call ID {callID} with reason {reason}.");

                WebRtcSession closedSession = null;

                if (!_webRtcSessions.TryRemove(callID, out closedSession))
                {
                    logger.LogError("Failed to remove closed WebRTC session from dictionary.");
                }

                if (closedSession != null)
                {
                    OnMp4MediaSampleReady -= closedSession.SendMedia;
                    OnTestPatternSampleReady -= closedSession.SendMedia;

                    if (!closedSession.IsClosed)
                    {
                        closedSession.Close(reason);
                    }
                }
            }
            catch (Exception excp)
            {
                logger.LogError("Exception PeerClosed. " + excp);
            }
        }

        private void WebRtcAnswerReceived(WebSocketSharp.Net.WebSockets.WebSocketContext context, string webSocketID, string sdpAnswer)
        {
            try
            {
                logger.LogDebug("Answer SDP: " + sdpAnswer);

                var answerSDP = SDP.ParseSDPDescription(sdpAnswer);

                var session = _webRtcSessions.Where(x => x.Key == webSocketID).Select(x => x.Value).SingleOrDefault();

                if (session == null)
                {
                    logger.LogWarning("No WebRTC client entry exists for web socket ID " + webSocketID + ", ignoring.");
                }
                else
                {
                    logger.LogDebug("New WebRTC client SDP answer for web socket ID " + webSocketID + ".");
                    session.setRemoteDescription(SdpType.answer, answerSDP);
                }

                context.WebSocket.CloseAsync();
            }
            catch (Exception excp)
            {
                logger.LogError("Exception WebRtcAnswerReceived. " + excp.Message);
            }
        }

        /// <summary>
        /// Video resolution changed event handler.
        /// </summary>
        /// <param name="width">The new video frame width.</param>
        /// <param name="height">The new video frame height.</param>
        /// <param name="stride">The new video frame stride.</param>
        private VpxEncoder InitialiseVpxEncoder(uint width, uint height, uint stride)
        {
            try
            {
                var vpxEncoder = new VpxEncoder();
                vpxEncoder.InitEncoder(width, height, stride);

                logger.LogInformation($"VPX encoder initialised with width {width}, height {height} and stride {stride}.");

                return vpxEncoder;
            }
            catch (Exception excp)
            {
                logger.LogWarning("Exception InitialiseVpxEncoder. " + excp.Message);
                throw;
            }
        }

        /// <summary>
        /// Starts the Media Foundation sampling.
        /// </summary>
        unsafe private void SampleMp4Media()
        {
            try
            {
                logger.LogDebug("Starting mp4 media sampling thread.");

                _isMp4Sampling = true;

                VpxEncoder vpxEncoder = null;
                uint vp8Timestamp = 0;
                uint mulawTimestamp = 0;

                while (!_exit)
                {
                    if (OnMp4MediaSampleReady == null)
                    {
                        logger.LogDebug("No active clients, media sampling paused.");
                        break;
                    }
                    else
                    {
                        byte[] sampleBuffer = null;
                        var sample = _mediaSource.GetSample(ref sampleBuffer);

                        if (sample != null && sample.HasVideoSample)
                        {
                            if (vpxEncoder == null ||
                                (vpxEncoder.GetWidth() != sample.Width || vpxEncoder.GetHeight() != sample.Height || vpxEncoder.GetStride() != sample.Stride))
                            {
                                if(vpxEncoder != null)
                                {
                                    vpxEncoder.Dispose();
                                }

                                vpxEncoder = InitialiseVpxEncoder((uint)sample.Width, (uint)sample.Height, (uint)sample.Stride);
                            }

                            byte[] vpxEncodedBuffer = null;

                            unsafe
                            {
                                fixed (byte* p = sampleBuffer)
                                {
                                    int encodeResult = vpxEncoder.Encode(p, sampleBuffer.Length, 1, ref vpxEncodedBuffer);

                                    if (encodeResult != 0)
                                    {
                                        logger.LogWarning("VPX encode of video sample failed.");
                                    }
                                }
                            }

                            OnMp4MediaSampleReady?.Invoke(SDPMediaTypesEnum.video, vp8Timestamp, vpxEncodedBuffer);

                            //Console.WriteLine($"Video SeqNum {videoSeqNum}, timestamp {videoTimestamp}, buffer length {vpxEncodedBuffer.Length}, frame count {sampleProps.FrameCount}.");

                            vp8Timestamp += VP8_TIMESTAMP_SPACING;
                        }
                        else if (sample != null && sample.HasAudioSample)
                        {
                            uint sampleDuration = (uint)(sampleBuffer.Length / 2);

                            byte[] mulawSample = new byte[sampleDuration];
                            int sampleIndex = 0;

                            for (int index = 0; index < sampleBuffer.Length; index += 2)
                            {
                                var ulawByte = MuLawEncoder.LinearToMuLawSample(BitConverter.ToInt16(sampleBuffer, index));
                                mulawSample[sampleIndex++] = ulawByte;
                            }

                            OnMp4MediaSampleReady?.Invoke(SDPMediaTypesEnum.audio, mulawTimestamp, mulawSample);

                            //Console.WriteLine($"Audio SeqNum {audioSeqNum}, timestamp {audioTimestamp}, buffer length {mulawSample.Length}.");

                            mulawTimestamp += sampleDuration;
                        }
                    }
                }

                vpxEncoder.Dispose();
            }
            catch (Exception excp)
            {
                logger.LogWarning("Exception SampleMp4Media. " + excp.Message);
            }
            finally
            {
                logger.LogDebug("mp4 sampling thread stopped.");
                _isMp4Sampling = false;
            }
        }

        private async void SampleTestPattern()
        {
            try
            {
                logger.LogDebug("Starting test pattern sampling thread.");

                _isTestPatternSampling = true;

                Bitmap testPattern = new Bitmap(_testPattermImagePath);

                // Get the stride.
                Rectangle rect = new Rectangle(0, 0, testPattern.Width, testPattern.Height);
                System.Drawing.Imaging.BitmapData bmpData =
                    testPattern.LockBits(rect, System.Drawing.Imaging.ImageLockMode.ReadWrite,
                    testPattern.PixelFormat);

                // Get the address of the first line.
                int stride = bmpData.Stride;

                testPattern.UnlockBits(bmpData);

                // Initialise the video codec and color converter.
                SIPSorceryMedia.VpxEncoder vpxEncoder = new VpxEncoder();
                vpxEncoder.InitEncoder((uint)testPattern.Width, (uint)testPattern.Height, (uint)stride);

                SIPSorceryMedia.ImageConvert colorConverter = new ImageConvert();

                byte[] sampleBuffer = null;
                byte[] encodedBuffer = null;
                int sampleCount = 0;
                uint rtpTimestamp = 0;

                while (!_exit)
                {
                    if (OnTestPatternSampleReady == null)
                    {
                        logger.LogDebug("No active clients, test pattern sampling paused.");
                        break;
                    }
                    else
                    {
                        var stampedTestPattern = testPattern.Clone() as System.Drawing.Image;
                        AddTimeStampAndLocation(stampedTestPattern, DateTime.UtcNow.ToString("dd MMM yyyy HH:mm:ss:fff"), "Test Pattern");
                        sampleBuffer = BitmapToRGB24(stampedTestPattern as System.Drawing.Bitmap);

                        unsafe
                        {

                            fixed (byte* p = sampleBuffer)
                            {
                                byte[] convertedFrame = null;
                                colorConverter.ConvertRGBtoYUV(p, VideoSubTypesEnum.BGR24, testPattern.Width, testPattern.Height, stride, VideoSubTypesEnum.I420, ref convertedFrame);

                                fixed (byte* q = convertedFrame)
                                {
                                    int encodeResult = vpxEncoder.Encode(q, convertedFrame.Length, 1, ref encodedBuffer);

                                    if (encodeResult != 0)
                                    {
                                        logger.LogWarning("VPX encode of video sample failed.");
                                        continue;
                                    }
                                }
                            }

                            stampedTestPattern.Dispose();

                            OnTestPatternSampleReady?.Invoke(SDPMediaTypesEnum.video, rtpTimestamp, encodedBuffer);

                            sampleCount++;
                            rtpTimestamp += VP8_TIMESTAMP_SPACING;
                        }

                        await Task.Delay(30);
                    }
                }

                testPattern.Dispose();
                vpxEncoder.Dispose();
            }
            catch (Exception excp)
            {
                logger.LogError("Exception SampleTestPattern. " + excp);
            }
            finally
            {
                logger.LogDebug("test pattern sampling thread stopped.");
                _isTestPatternSampling = false;
            }
        }

        private static byte[] BitmapToRGB24(Bitmap bitmap)
        {
            try
            {
                BitmapData bitmapData = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height), ImageLockMode.ReadWrite, System.Drawing.Imaging.PixelFormat.Format24bppRgb);
                var length = bitmapData.Stride * bitmapData.Height;

                byte[] bytes = new byte[length];

                // Copy bitmap to byte[]
                Marshal.Copy(bitmapData.Scan0, bytes, 0, length);
                bitmap.UnlockBits(bitmapData);

                return bytes;
            }
            catch (Exception)
            {
                return new byte[0];
            }
        }

        private static void AddTimeStampAndLocation(System.Drawing.Image image, string timeStamp, string locationText)
        {
            int pixelHeight = (int)(image.Height * TEXT_SIZE_PERCENTAGE);

            Graphics g = Graphics.FromImage(image);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;

            using (StringFormat format = new StringFormat())
            {
                format.LineAlignment = StringAlignment.Center;
                format.Alignment = StringAlignment.Center;

                using (Font f = new Font("Tahoma", pixelHeight, GraphicsUnit.Pixel))
                {
                    using (var gPath = new GraphicsPath())
                    {
                        float emSize = g.DpiY * f.Size / POINTS_PER_INCH;
                        if (locationText != null)
                        {
                            gPath.AddString(locationText, f.FontFamily, (int)FontStyle.Bold, emSize, new Rectangle(0, TEXT_MARGIN_PIXELS, image.Width, pixelHeight), format);
                        }

                        gPath.AddString(timeStamp /* + " -- " + fps.ToString("0.00") + " fps" */, f.FontFamily, (int)FontStyle.Bold, emSize, new Rectangle(0, image.Height - (pixelHeight + TEXT_MARGIN_PIXELS), image.Width, pixelHeight), format);
                        g.FillPath(Brushes.White, gPath);
                        g.DrawPath(new Pen(Brushes.Black, pixelHeight * TEXT_OUTLINE_REL_THICKNESS), gPath);
                    }
                }
            }
        }

        /// <summary>
        /// Runs a DTLS handshake test between two threads on a loopback address. The main motivation for
        /// this test was that the first DTLS handshake between this application and a client browser
        /// was often substantially slower and occasionally failed. By doing a loopback test the idea 
        /// is that the internal OpenSSL state is initialised.
        /// </summary>
        private void DoDtlsHandshakeLoopbackTest()
        {
            IPAddress testAddr = IPAddress.Loopback;

            Socket svrSock = new Socket(testAddr.AddressFamily, SocketType.Dgram, ProtocolType.Udp);
            svrSock.Bind(new IPEndPoint(testAddr, 9000));
            int svrPort = ((IPEndPoint)svrSock.LocalEndPoint).Port;
            DtlsHandshake svrHandshake = new DtlsHandshake(_dtlsCertificatePath, _dtlsKeyPath);
            //svrHandshake.Debug = true;
            var svrTask = Task.Run(() => svrHandshake.DoHandshakeAsServer((ulong)svrSock.Handle));

            Socket cliSock = new Socket(testAddr.AddressFamily, SocketType.Dgram, ProtocolType.Udp);
            cliSock.Bind(new IPEndPoint(testAddr, 0));
            cliSock.Connect(testAddr, svrPort);
            DtlsHandshake cliHandshake = new DtlsHandshake();
            //cliHandshake.Debug = true;
            var cliTask = Task.Run(() => cliHandshake.DoHandshakeAsClient((ulong)cliSock.Handle, (short)testAddr.AddressFamily, testAddr.GetAddressBytes(), (ushort)svrPort));

            bool result = Task.WaitAll(new Task[] { svrTask, cliTask }, TEST_DTLS_HANDSHAKE_TIMEOUT);

            cliHandshake.Shutdown();
            svrHandshake.Shutdown();
            cliSock.Close();
            svrSock.Close();
        }

        /// <summary>
        ///  Adds a console logger. Can be omitted if internal SIPSorcery debug and warning messages are not required.
        /// </summary>
        private static void AddConsoleLogger()
        {
            var loggerFactory = new Microsoft.Extensions.Logging.LoggerFactory();
            var loggerConfig = new LoggerConfiguration()
                .Enrich.FromLogContext()
                .MinimumLevel.Is(Serilog.Events.LogEventLevel.Debug)
                .WriteTo.Console()
                .CreateLogger();
            loggerFactory.AddSerilog(loggerConfig);
            SIPSorcery.Sys.Log.LoggerFactory = loggerFactory;
        }
    }
}
