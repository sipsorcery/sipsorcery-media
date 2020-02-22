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
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
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

    public class RtpAVSession : RTPMediaSession
    {
        private const int AUDIO_SAMPLE_PERIOD_MILLISECONDS = 30;
        private const int VP8_TIMESTAMP_SPACING = 3000;

        /// <summary>
        /// PCMU encoding for silence, http://what-when-how.com/voip/g-711-compression-voip/
        /// </summary>
        private static readonly byte PCMU_SILENCE_BYTE_ZERO = 0x7F;
        private static readonly byte PCMU_SILENCE_BYTE_ONE = 0xFF;

        private static Microsoft.Extensions.Logging.ILogger Log = SIPSorcery.Sys.Log.Logger;

        private static readonly WaveFormat _waveFormat = new WaveFormat(8000, 16, 1);

        private CancellationTokenSource _cts = new CancellationTokenSource();
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

        private static byte[] _currVideoFrame = new byte[65536];
        private static int _currVideoFramePosn = 0;

        private VpxEncoder _vpxDecoder;
        private ImageConvert _imgConverter;
        private PictureBox _videoBox;

        /// <summary>
        /// Dummy video source which supplies a test pattern with a rolling 
        /// timestamp.
        /// </summary>
        private TestPatternVideoSource _testPatternVideoSource;

        private uint _rtpAudioTimestamp = 0;
        private uint _rtpAudioTimestampPeriod = 0;
        private uint _rtpVideoTimestamp = 0;
        private bool _isClosed = false;

        public RtpAVSession(AddressFamily addrFamily, AudioSourceOptions audioSourceOptions, VideoSourceOptions videoSourceOptions, PictureBox videoBox)
            : base(addrFamily)
        {
            _audioSourceOpts = audioSourceOptions;
            _videoSourceOpts = videoSourceOptions;
            _videoBox = videoBox;

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

                InitAudio(_audioSourceOpts.AudioSource);
            }

            if (_videoSourceOpts != null && _videoSourceOpts.VideoSource != VideoSourcesEnum.None)
            {
                MediaStreamTrack videoTrack = new MediaStreamTrack(null, SDPMediaTypesEnum.video, false, new List<SDPMediaFormat> { new SDPMediaFormat(SDPMediaFormatsEnum.VP8) });
                addTrack(videoTrack);

                InitVideo(_videoSourceOpts.VideoSource);
            }

            base.OnRtpPacketReceived += RtpPacketReceived;
        }

        /// <summary>
        /// Starts the media capturing devices.
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
                Task.Run(() => SendSilence(_cts.Token));
            }
            else if (audioSource == AudioSourcesEnum.Music)
            {
                Task.Run(() => SendMusic(_cts.Token));
            }

            VideoSourcesEnum videoSource = (_videoSourceOpts != null) ? _videoSourceOpts.VideoSource : VideoSourcesEnum.None;

            if (videoSource == VideoSourcesEnum.TestPattern && _testPatternVideoSource != null)
            {
                Task.Run(() => _testPatternVideoSource.Start(_cts.Token));
            }
        }

        public override void Close()
        {
            if (!_isClosed)
            {
                _isClosed = true;

                base.OnRtpPacketReceived -= RtpPacketReceived;

                _waveOutEvent?.Stop();
                _waveInEvent?.StopRecording();
                _cts.Cancel();

                base.Close();
            }
        }

        /// <summary>
        /// Initialise the audio capture and render device.
        /// </summary>
        private void InitAudio(AudioSourcesEnum audioSource)
        {
            // Render device.
            _waveOutEvent = new WaveOutEvent();
            _waveProvider = new BufferedWaveProvider(_waveFormat);
            _waveProvider.DiscardOnBufferOverflow = true;
            _waveOutEvent.Init(_waveProvider);

            // Audio source.
            if (audioSource == AudioSourcesEnum.Microphone)
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

            _rtpAudioTimestampPeriod = (uint)(SDPMediaFormatInfo.GetClockRate(SDPMediaFormatsEnum.PCMU) / AUDIO_SAMPLE_PERIOD_MILLISECONDS);
        }

        /// <summary>
        /// Initialise the video capture and render device.
        /// </summary>
        private void InitVideo(VideoSourcesEnum videoSource)
        {
            if (videoSource == VideoSourcesEnum.TestPattern)
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

            base.SendAudioFrame(_rtpAudioTimestamp, (int)SDPMediaFormatsEnum.PCMU, sample);
            _rtpAudioTimestamp += _rtpAudioTimestampPeriod;
        }

        /// <summary>
        /// Event handler for video sample being supplied by local capture device.
        /// </summary>
        private void LocalVideoSampleAvailable(byte[] sample)
        {
            base.SendVp8Frame(_rtpVideoTimestamp, (int)SDPMediaFormatsEnum.VP8, sample);
            _rtpVideoTimestamp += VP8_TIMESTAMP_SPACING;
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
                // TODO: use the VP8 Payload descriptor to properly determine the VP8 header length (currently hard coded to 4).
                Buffer.BlockCopy(rtpPacket.Payload, 4, _currVideoFrame, _currVideoFramePosn, rtpPacket.Payload.Length - 4);
                _currVideoFramePosn += rtpPacket.Payload.Length - 4;

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
                                //Console.WriteLine($"Video frame ready {width}x{height}.");

                                fixed (byte* r = i420)
                                {
                                    byte[] bmp = null;
                                    int stride = 0;
                                    int convRes = _imgConverter.ConvertYUVToRGB(r, VideoSubTypesEnum.I420, (int)width, (int)height, VideoSubTypesEnum.BGR24, ref bmp, ref stride);

                                    if (convRes == 0)
                                    {
                                        _videoBox.BeginInvoke(new Action(() =>
                                        {
                                            fixed (byte* s = bmp)
                                            {
                                                System.Drawing.Bitmap bmpImage = new System.Drawing.Bitmap((int)width, (int)height, stride, System.Drawing.Imaging.PixelFormat.Format24bppRgb, (IntPtr)s);
                                                _videoBox.Image = bmpImage;
                                            }
                                        }));
                                    }
                                    else
                                    {
                                        Log.LogWarning("Pixel format conversion of decoded sample failed.");
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
        private async void SendMusic(CancellationToken ct)
        {
            uint rtpSampleTimestamp = 0;

            using (StreamReader sr = new StreamReader(_audioSourceOpts.SourceFile))
            {
                int sampleSize = (SDPMediaFormatInfo.GetClockRate(SDPMediaFormatsEnum.PCMU) / 1000) * AUDIO_SAMPLE_PERIOD_MILLISECONDS;
                byte[] sample = new byte[sampleSize];
                int bytesRead = sr.BaseStream.Read(sample, 0, sample.Length);

                while (bytesRead > 0 && !ct.IsCancellationRequested)
                {
                    SendAudioFrame(rtpSampleTimestamp, (int)SDPMediaFormatsEnum.PCMU, sample);
                    rtpSampleTimestamp += _rtpAudioTimestampPeriod;

                    bytesRead = sr.BaseStream.Read(sample, 0, sample.Length);

                    await Task.Delay(AUDIO_SAMPLE_PERIOD_MILLISECONDS);
                }
            }
        }

        /// <summary>
        /// Sends the sounds of silence. If the destination is on the other side of a NAT this is useful to open
        /// a pinhole and hopefully get the remote RTP stream through.
        /// </summary>
        private async void SendSilence(CancellationToken ct)
        {
            uint bufferSize = (uint)AUDIO_SAMPLE_PERIOD_MILLISECONDS;
            uint rtpSampleTimestamp = 0;

            while (!ct.IsCancellationRequested)
            {
                byte[] sample = new byte[bufferSize / 2];
                int sampleIndex = 0;

                for (int index = 0; index < bufferSize; index += 2)
                {
                    sample[sampleIndex] = PCMU_SILENCE_BYTE_ZERO;
                    sample[sampleIndex + 1] = PCMU_SILENCE_BYTE_ONE;
                }

                SendAudioFrame(rtpSampleTimestamp, (int)SDPMediaFormatsEnum.PCMU, sample);
                rtpSampleTimestamp += _rtpAudioTimestampPeriod;

                await Task.Delay(AUDIO_SAMPLE_PERIOD_MILLISECONDS);
            }
        }
    }
}
