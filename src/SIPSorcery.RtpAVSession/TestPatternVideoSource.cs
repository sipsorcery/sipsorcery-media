//-----------------------------------------------------------------------------
// Filename: TestPatternVideoSource.cs
//
// Description: An video stream source generated from a static image overlaid
// with a title and temporal text.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
// 
// History:
// 26 Feb 2020	Aaron Clauson	Created, Dublin, Ireland.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using SIPSorceryMedia;

namespace SIPSorcery.Media
{
    public class TestPatternVideoSource : IDisposable
    {
        public const int DEFAULT_FRAMES_PER_SECOND = 30;

        private static string FALLBACK_TEST_PATTERN_IMAGE_PATH = "media/testpattern.jpeg";
        private const float TEXT_SIZE_PERCENTAGE = 0.035f;       // height of text as a percentage of the total image height
        private const float TEXT_OUTLINE_REL_THICKNESS = 0.02f; // Black text outline thickness is set as a percentage of text height in pixels
        private const int TEXT_MARGIN_PIXELS = 5;
        private const int POINTS_PER_INCH = 72;

        private static Microsoft.Extensions.Logging.ILogger logger = SIPSorcery.Sys.Log.Logger;

        private VpxEncoder _vpxEncoder;
        private ImageConvert _colorConverter;
        private Timer _videoStreamTimer;
        private string _testPatternPath;
        private int _framesPerSecond;
        private int _samplePeriod;
        private Bitmap _testPattern;
        private uint _width, _height, _stride;
        private bool _isDisposing = false; // To detect redundant calls

        /// <summary>
        /// The current frame rate being used to generate the test pattern.
        /// </summary>
        public int FramesPerSecond
        {
            get { return _framesPerSecond; }
        }

        public event Action<byte[]> SampleReady;

        public TestPatternVideoSource(string testPatternSource, int framesPerSecond)
        {
            _testPatternPath = testPatternSource;
            _framesPerSecond = (framesPerSecond > 0 && framesPerSecond <= DEFAULT_FRAMES_PER_SECOND) ? framesPerSecond : DEFAULT_FRAMES_PER_SECOND;
            _samplePeriod = 1000 / framesPerSecond;

            if (!String.IsNullOrEmpty(testPatternSource) && !File.Exists(testPatternSource))
            {
                logger.LogWarning($"Requested test pattern file could not be found {testPatternSource}.");
            }

            if (_testPatternPath == null)
            {
                if (!File.Exists(FALLBACK_TEST_PATTERN_IMAGE_PATH))
                {
                    throw new ApplicationException($"The fallback test pattern image file could not be found {FALLBACK_TEST_PATTERN_IMAGE_PATH}.");
                }
                else
                {
                    _testPatternPath = FALLBACK_TEST_PATTERN_IMAGE_PATH;
                }
            }

            logger.LogDebug($"Loading test pattern from {_testPatternPath}.");

            _testPattern = new Bitmap(_testPatternPath);

            // Get the stride.
            Rectangle rect = new Rectangle(0, 0, _testPattern.Width, _testPattern.Height);
            System.Drawing.Imaging.BitmapData bmpData =
                _testPattern.LockBits(rect, System.Drawing.Imaging.ImageLockMode.ReadWrite,
                _testPattern.PixelFormat);

            _width = (uint)_testPattern.Width;
            _height = (uint)_testPattern.Height;

            // Get the address of the first line.
            _stride = (uint)bmpData.Stride;

            _testPattern.UnlockBits(bmpData);

            // Initialise the video codec and color converter.
            _vpxEncoder = new VpxEncoder();
            _vpxEncoder.InitEncoder(_width, _height, _stride);

            _colorConverter = new ImageConvert();
        }

        public void Start()
        {
            _videoStreamTimer = new Timer(SendTestPatternSample, null, 0, _samplePeriod);
        }

        public void Stop()
        {
            _videoStreamTimer?.Dispose();
        }

        public async Task SetSource(string newSource, int framesPerSecond)
        {
            _framesPerSecond = (framesPerSecond > 0 && framesPerSecond <= DEFAULT_FRAMES_PER_SECOND) ? framesPerSecond : DEFAULT_FRAMES_PER_SECOND;
            _samplePeriod = 1000 / framesPerSecond;

            if (newSource != null && File.Exists(newSource) && _testPatternPath != newSource)
            {
                if (_videoStreamTimer != null)
                {
                    _videoStreamTimer?.Dispose();
                    await Task.Delay(_samplePeriod * 2).ConfigureAwait(false);
                }

                // TODO: We're relying on the new source being the same dimensions. Need to add a check for that.
                _testPatternPath = newSource;
                _testPattern?.Dispose();
                _testPattern = new Bitmap(_testPatternPath);
            }
        }

        public void SendTestPatternSample(object state)
        {
            try
            {
                if (SampleReady != null && !_isDisposing)
                {
                    lock (_vpxEncoder)
                    {
                        unsafe
                        {
                            byte[] sampleBuffer = null;
                            byte[] encodedBuffer = null;

                            var stampedTestPattern = _testPattern.Clone() as System.Drawing.Image;
                            AddTimeStampAndLocation(stampedTestPattern, DateTime.UtcNow.ToString("dd MMM yyyy HH:mm:ss:fff"), "Test Pattern");
                            sampleBuffer = BitmapToRGB24(stampedTestPattern as System.Drawing.Bitmap);

                            fixed (byte* p = sampleBuffer)
                            {
                                byte[] convertedFrame = null;
                                _colorConverter.ConvertRGBtoYUV(p, VideoSubTypesEnum.BGR24, (int)_width, (int)_height, (int)_stride, VideoSubTypesEnum.I420, ref convertedFrame);

                                fixed (byte* q = convertedFrame)
                                {
                                    int encodeResult = _vpxEncoder.Encode(q, convertedFrame.Length, 1, ref encodedBuffer);

                                    if (encodeResult != 0)
                                    {
                                        throw new ApplicationException("VPX encode of video sample failed.");
                                    }
                                }
                            }

                            stampedTestPattern.Dispose();

                            SampleReady?.Invoke(encodedBuffer);
                        }
                    }
                }
            }
            catch (Exception excp)
            {
                logger.LogError("Exception SendTestPatternSample. " + excp);
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
                return new byte[] { };
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

        protected virtual void Dispose(bool disposing)
        {
            if (!_isDisposing)
            {
                _isDisposing = true;

                if (disposing)
                {
                    _testPattern.Dispose();
                }

                lock (_vpxEncoder)
                {
                    // Prevent the encoder being disposed of if it's in the middle of a sample.
                    _vpxEncoder.Dispose();
                    _vpxEncoder = null;
                }

                _colorConverter = null;
            }
        }

        public void Dispose()
        {
            Dispose(true);
        }
    }
}
