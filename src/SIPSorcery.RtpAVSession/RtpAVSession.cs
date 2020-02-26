//-----------------------------------------------------------------------------
// Filename: RtpAVSession.cs
//
// Description: An example RTP audio/video session that can capture and render
// media on Windows.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
// 
// History:
// 20 Feb 2020	Aaron Clauson	Created, Dublin, Ireland.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Extensions.Logging;
using NAudio.Wave;
using SIPSorcery.Net;
using SIPSorcery.SIP.App;
using SIPSorcery.Sys;
using SIPSorceryMedia;

namespace SIPSorcery.Media
{
    public enum AudioSourcesEnum
    {
        None = 0,
        Microphone = 1,
        Music = 2,
        Silence = 3
    }

    public class AudioSourceOptions
    {
        public AudioSourcesEnum AudioSource;
        public string SourceFile;
    }

    public enum VideoSourcesEnum
    {
        None = 0,
        Webcam = 1,
        TestPattern = 2
    }

    public class VideoSourceOptions
    {
        public VideoSourcesEnum VideoSource;
        public string SourceFile;
    }

    public class RtpAVSession : RTPSession, IMediaSession
    {
        public const string TELEPHONE_EVENT_ATTRIBUTE = "telephone-event";
        public const int DTMF_EVENT_DURATION = 1200;        // Default duration for a DTMF event.
        public const int DTMF_EVENT_PAYLOAD_ID = 101;
        private const int AUDIO_SAMPLE_PERIOD_MILLISECONDS = 30;
        private const int VP8_TIMESTAMP_SPACING = 3000;

        /// <summary>
        /// PCMU encoding for silence, http://what-when-how.com/voip/g-711-compression-voip/
        /// </summary>
        private static readonly byte PCMU_SILENCE_BYTE_ZERO = 0x7F;
        private static readonly byte PCMU_SILENCE_BYTE_ONE = 0xFF;

        private static Microsoft.Extensions.Logging.ILogger Log = SIPSorcery.Sys.Log.Logger;

        private static readonly WaveFormat _waveFormat = new WaveFormat(8000, 16, 1);

        private AudioSourceOptions _audioSourceOpts;
        private VideoSourceOptions _videoSourceOpts;

        /// <summary>
        /// Audio render device.
        /// </summary>
        private WaveOutEvent _waveOutEvent;

        /// <summary>
        /// Buffer for audio samples to be rendered.
        /// </summary>
        private BufferedWaveProvider _waveProvider;

        /// <summary>
        /// Audio capture device.
        /// </summary>
        private WaveInEvent _waveInEvent;

        private byte[] _currVideoFrame = new byte[65536];
        private int _currVideoFramePosn = 0;

        private VpxEncoder _vpxDecoder;
        private ImageConvert _imgConverter;

        /// <summary>
        /// Dummy video source which supplies a test pattern with a rolling 
        /// timestamp.
        /// </summary>
        private TestPatternVideoSource _testPatternVideoSource;
        private StreamReader _audioStreamReader;
        private Timer _audioStreamTimer;

        private uint _rtpAudioTimestampPeriod = 0;
        private bool _isClosed = false;

        /// <summary>
        /// Fired when a video sample is ready for rendering.
        /// [sample, width, height, stride].
        /// </summary>
        public event Action<byte[], uint, uint, int> OnVideoSampleReady;

        /// <summary>
        /// Creates a new RTP audio visual session where the remote video stream will be rendered to a Windows
        /// Forms control.
        /// </summary>
        /// <param name="addrFamily">The address family to create the underlying socket on (IPv4 or IPv6).</param>
        /// <param name="audioSourceOptions">The options for the audio source.</param>
        /// <param name="videoSourceOptions">The options for the audio source.</param>
        public RtpAVSession(AddressFamily addrFamily, AudioSourceOptions audioSourceOptions, VideoSourceOptions videoSourceOptions)
            : base(addrFamily, false, false, false)
        {
            _audioSourceOpts = audioSourceOptions;
            _videoSourceOpts = videoSourceOptions;

            _vpxDecoder = new VpxEncoder();
            int res = _vpxDecoder.InitDecoder();
            if (res != 0)
            {
                throw new ApplicationException("VPX decoder initialisation failed.");
            }

            _imgConverter = new ImageConvert();

            if (_audioSourceOpts != null && _audioSourceOpts.AudioSource != AudioSourcesEnum.None)
            {
                MediaStreamTrack videoTrack = new MediaStreamTrack(null, SDPMediaTypesEnum.audio, false, new List<SDPMediaFormat> { new SDPMediaFormat(SDPMediaFormatsEnum.PCMU) });
                addTrack(videoTrack);

                InitAudio(_audioSourceOpts);
            }

            if (_videoSourceOpts != null && _videoSourceOpts.VideoSource != VideoSourcesEnum.None)
            {
                MediaStreamTrack videoTrack = new MediaStreamTrack(null, SDPMediaTypesEnum.video, false, new List<SDPMediaFormat> { new SDPMediaFormat(SDPMediaFormatsEnum.VP8) });
                addTrack(videoTrack);

                InitVideo(_videoSourceOpts);
            }

            base.OnRtpPacketReceived += RtpPacketReceived;
        }

        /// <summary>
        /// Starts the media capturing/source devices.
        /// </summary>
        public void Start()
        {
            _waveOutEvent?.Play();

            AudioSourcesEnum audioSource = (_audioSourceOpts != null) ? _audioSourceOpts.AudioSource : AudioSourcesEnum.None;

            if (audioSource == AudioSourcesEnum.Microphone && _waveInEvent != null)
            {
                _waveInEvent?.StartRecording();
            }
            else if (audioSource == AudioSourcesEnum.Silence)
            {
                _audioStreamTimer = new Timer(SendSilenceSample, null, 0, AUDIO_SAMPLE_PERIOD_MILLISECONDS);
            }
            else if (audioSource == AudioSourcesEnum.Music)
            {
                _audioStreamTimer = new Timer(SendMusicSample, null, 0, AUDIO_SAMPLE_PERIOD_MILLISECONDS);
            }

            VideoSourcesEnum videoSource = (_videoSourceOpts != null) ? _videoSourceOpts.VideoSource : VideoSourcesEnum.None;

            if (videoSource == VideoSourcesEnum.TestPattern && _testPatternVideoSource != null)
            {
                _testPatternVideoSource.Start();
            }
        }

        /// <summary>
        /// Sends a DTMF tone as an RTP event to the remote party.
        /// </summary>
        /// <param name="key">The DTMF tone to send.</param>
        /// <param name="ct">RTP events can span multiple RTP packets. This token can
        /// be used to cancel the send.</param>
        public Task SendDtmf(byte key, CancellationToken ct)
        {
            var dtmfEvent = new RTPEvent(key, false, RTPEvent.DEFAULT_VOLUME, DTMF_EVENT_DURATION, DTMF_EVENT_PAYLOAD_ID);
            return SendDtmfEvent(dtmfEvent, ct);
        }

        /// <summary>
        /// Send a media sample via RTP.
        /// </summary>
        /// <param name="mediaType">The type of media to send (audio or video).</param>
        /// <param name="samplePeriod">The period measured in sampling units for the sample.</param>
        /// <param name="sample">The raw sample.</param>
        public void SendMedia(SDPMediaTypesEnum mediaType, uint samplePeriod, byte[] sample)
        {
            throw new NotImplementedException();
        }

        /// <summary>
        /// Sets relevant properties for this session based on the SDP from the remote party.
        /// </summary>
        /// <param name="sessionDescription">The session description from the remote call party.</param>
        public override void setRemoteDescription(RTCSessionDescription sessionDescription)
        {
            base.setRemoteDescription(sessionDescription);

            var connAddr = IPAddress.Parse(sessionDescription.sdp.Connection.ConnectionAddress);

            foreach (var announcement in sessionDescription.sdp.Media)
            {
                var annAddr = connAddr;
                if (announcement.Connection != null)
                {
                    annAddr = IPAddress.Parse(announcement.Connection.ConnectionAddress);
                }

                if (announcement.Media == SDPMediaTypesEnum.audio)
                {
                    var connRtpEndPoint = new IPEndPoint(annAddr, announcement.Port);
                    var connRtcpEndPoint = new IPEndPoint(annAddr, announcement.Port + 1);

                    SetDestination(SDPMediaTypesEnum.audio, connRtpEndPoint, connRtcpEndPoint);

                    foreach (var mediaFormat in announcement.MediaFormats)
                    {
                        if (mediaFormat.FormatAttribute?.StartsWith(TELEPHONE_EVENT_ATTRIBUTE) == true)
                        {
                            if (!int.TryParse(mediaFormat.FormatID, out var remoteRtpEventPayloadID))
                            {
                                //logger.LogWarning("The media format on the telephone event attribute was not a valid integer.");
                            }
                            else
                            {
                                base.RemoteRtpEventPayloadID = remoteRtpEventPayloadID;
                            }
                            break;
                        }
                    }
                }
                else if (announcement.Media == SDPMediaTypesEnum.video)
                {
                    var connRtpEndPoint = new IPEndPoint(annAddr, announcement.Port);
                    var connRtcpEndPoint = new IPEndPoint(annAddr, announcement.Port + 1);

                    SetDestination(SDPMediaTypesEnum.video, connRtpEndPoint, connRtcpEndPoint);
                }
            }
        }

        /// <summary>
        /// Closes the session.
        /// </summary>
        /// <param name="reason">Reason for the closure.</param>
        public void Close(string reason)
        {
            if (!_isClosed)
            {
                _isClosed = true;

                base.OnRtpPacketReceived -= RtpPacketReceived;

                _waveOutEvent?.Stop();
                _waveInEvent?.StopRecording();
                _audioStreamTimer?.Dispose();
                _testPatternVideoSource?.Stop();

                // The VPX encoder is a memory hog. 
                _testPatternVideoSource?.Dispose();

                base.CloseSession(reason);
            }
        }

        /// <summary>
        /// Initialise the audio capture and render device.
        /// </summary>
        private void InitAudio(AudioSourceOptions audioSourceOpts)
        {
            // Render device.
            _waveOutEvent = new WaveOutEvent();
            _waveProvider = new BufferedWaveProvider(_waveFormat);
            _waveProvider.DiscardOnBufferOverflow = true;
            _waveOutEvent.Init(_waveProvider);

            // Audio source.
            if (audioSourceOpts.AudioSource == AudioSourcesEnum.Microphone)
            {
                if (WaveInEvent.DeviceCount > 0)
                {
                    _waveInEvent = new WaveInEvent();
                    _waveInEvent.BufferMilliseconds = AUDIO_SAMPLE_PERIOD_MILLISECONDS;
                    _waveInEvent.NumberOfBuffers = 1;
                    _waveInEvent.DeviceNumber = 0;
                    _waveInEvent.WaveFormat = _waveFormat;
                    _waveInEvent.DataAvailable += LocalAudioSampleAvailable;
                }
                else
                {
                    Log.LogWarning("No audio capture devices are available. No audio stream will be sent.");
                }
            }
            else if (audioSourceOpts.AudioSource == AudioSourcesEnum.Music)
            {
                _audioStreamReader = new StreamReader(_audioSourceOpts.SourceFile);
            }

            _rtpAudioTimestampPeriod = (uint)(SDPMediaFormatInfo.GetClockRate(SDPMediaFormatsEnum.PCMU) / AUDIO_SAMPLE_PERIOD_MILLISECONDS);
        }

        /// <summary>
        /// Initialise the video capture and render device.
        /// </summary>
        private void InitVideo(VideoSourceOptions videoSourceOpts)
        {
            if (videoSourceOpts.VideoSource == VideoSourcesEnum.TestPattern)
            {
                _testPatternVideoSource = new TestPatternVideoSource();
                _testPatternVideoSource.SampleReady += LocalVideoSampleAvailable;
            }
        }

        /// <summary>
        /// Event handler for audio sample being supplied by local capture device.
        /// </summary>
        private void LocalAudioSampleAvailable(object sender, WaveInEventArgs args)
        {
            byte[] sample = new byte[args.Buffer.Length / 2];
            int sampleIndex = 0;

            for (int index = 0; index < args.BytesRecorded; index += 2)
            {
                var ulawByte = NAudio.Codecs.MuLawEncoder.LinearToMuLawSample(BitConverter.ToInt16(args.Buffer, index));
                sample[sampleIndex++] = ulawByte;
            }

            base.SendAudioFrame((uint)sample.Length, (int)SDPMediaFormatsEnum.PCMU, sample);
        }

        /// <summary>
        /// Event handler for video sample being supplied by local capture device.
        /// </summary>
        private void LocalVideoSampleAvailable(byte[] sample)
        {
            base.SendVp8Frame(VP8_TIMESTAMP_SPACING, (int)SDPMediaFormatsEnum.VP8, sample);
        }

        /// <summary>
        /// Event handler for receiving RTP packets from a remote party.
        /// </summary>
        /// <param name="mediaType">The media type of the packets.</param>
        /// <param name="rtpPacket">The RTP packet with the media sample.</param>
        private void RtpPacketReceived(SDPMediaTypesEnum mediaType, RTPPacket rtpPacket)
        {
            //Log.LogDebug($"RTP packet received for {mediaType}.");

            if (mediaType == SDPMediaTypesEnum.audio)
            {
                RenderAudio(rtpPacket);
            }
            else if (mediaType == SDPMediaTypesEnum.video)
            {
                RenderVideo(rtpPacket);
            }
        }

        /// <summary>
        /// Render an audio RTP packet received from a remote party.
        /// </summary>
        /// <param name="rtpPacket">The RTP packet containing the audio payload.</param>
        private void RenderAudio(RTPPacket rtpPacket)
        {
            var sample = rtpPacket.Payload;
            for (int index = 0; index < sample.Length; index++)
            {
                short pcm = NAudio.Codecs.MuLawDecoder.MuLawToLinearSample(sample[index]);
                byte[] pcmSample = new byte[] { (byte)(pcm & 0xFF), (byte)(pcm >> 8) };
                _waveProvider.AddSamples(pcmSample, 0, 2);
            }
        }

        /// <summary>
        /// Render a video RTP packet received from a remote party.
        /// </summary>
        /// <param name="rtpPacket">The RTP packet containing the video payload.</param>
        private void RenderVideo(RTPPacket rtpPacket)
        {
            if (_currVideoFramePosn > 0 || (rtpPacket.Payload[0] & 0x10) > 0)
            {
                RtpVP8Header vp8Header = RtpVP8Header.GetVP8Header(rtpPacket.Payload);
                Buffer.BlockCopy(rtpPacket.Payload, vp8Header.Length, _currVideoFrame, _currVideoFramePosn, rtpPacket.Payload.Length - vp8Header.Length);
                _currVideoFramePosn += rtpPacket.Payload.Length - vp8Header.Length;

                if (rtpPacket.Header.MarkerBit == 1)
                {
                    unsafe
                    {
                        fixed (byte* p = _currVideoFrame)
                        {
                            uint width = 0, height = 0;
                            byte[] i420 = null;

                            //Console.WriteLine($"Attempting vpx decode {_currVideoFramePosn} bytes.");

                            int decodeResult = _vpxDecoder.Decode(p, _currVideoFramePosn, ref i420, ref width, ref height);

                            if (decodeResult != 0)
                            {
                                Console.WriteLine("VPX decode of video sample failed.");
                            }
                            else
                            {
                                if(OnVideoSampleReady != null)
                                {
                                    fixed (byte* r = i420)
                                    {
                                        byte[] bmp = null;
                                        int stride = 0;
                                        int convRes = _imgConverter.ConvertYUVToRGB(r, VideoSubTypesEnum.I420, (int)width, (int)height, VideoSubTypesEnum.BGR24, ref bmp, ref stride);

                                        if (convRes == 0)
                                        {
                                            //fixed (byte* s = bmp)
                                            //{
                                            //    System.Drawing.Bitmap bmpImage = new System.Drawing.Bitmap((int)width, (int)height, stride, System.Drawing.Imaging.PixelFormat.Format24bppRgb, (IntPtr)s);
                                            //}
                                            OnVideoSampleReady(bmp, width, height, stride);
                                        }
                                        else
                                        {
                                            Log.LogWarning("Pixel format conversion of decoded sample failed.");
                                        }
                                    }
                                }
                            }
                        }
                    }

                    _currVideoFramePosn = 0;
                }
            }
            else
            {
                Log.LogWarning("Discarding RTP packet, VP8 header Start bit not set.");
                Log.LogWarning($"rtp video, seqnum {rtpPacket.Header.SequenceNumber}, ts {rtpPacket.Header.Timestamp}, marker {rtpPacket.Header.MarkerBit}, payload {rtpPacket.Payload.Length}, payload[0-5] {rtpPacket.Payload.HexStr(5)}.");
            }
        }

        /// <summary>
        /// Sends the sounds of silence. If the destination is on the other side of a NAT this is useful to open
        /// a pinhole and hopefully get the remote RTP stream through.
        /// </summary>
        private void SendMusicSample(object state)
        {
            int sampleSize = (SDPMediaFormatInfo.GetClockRate(SDPMediaFormatsEnum.PCMU) / 1000) * AUDIO_SAMPLE_PERIOD_MILLISECONDS;
            byte[] sample = new byte[sampleSize];
            int bytesRead = _audioStreamReader.BaseStream.Read(sample, 0, sample.Length);

            if (bytesRead == 0 || _audioStreamReader.EndOfStream)
            {
                _audioStreamReader.BaseStream.Position = 0;
                bytesRead = _audioStreamReader.BaseStream.Read(sample, 0, sample.Length);
            }

            SendAudioFrame((uint)bytesRead, (int)SDPMediaFormatsEnum.PCMU, sample.Take(bytesRead).ToArray());
        }

        /// <summary>
        /// Sends the sounds of silence. If the destination is on the other side of a NAT this is useful to open
        /// a pinhole and hopefully get the remote RTP stream through.
        /// </summary>
        private void SendSilenceSample(object state)
        {
            uint bufferSize = (uint)AUDIO_SAMPLE_PERIOD_MILLISECONDS;

            byte[] sample = new byte[bufferSize / 2];
            int sampleIndex = 0;

            for (int index = 0; index < bufferSize; index += 2)
            {
                sample[sampleIndex] = PCMU_SILENCE_BYTE_ZERO;
                sample[sampleIndex + 1] = PCMU_SILENCE_BYTE_ONE;
            }

            SendAudioFrame(bufferSize, (int)SDPMediaFormatsEnum.PCMU, sample);
        }
    }
}
