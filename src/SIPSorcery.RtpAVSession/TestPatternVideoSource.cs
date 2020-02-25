using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using SIPSorceryMedia;

namespace SIPSorcery.Media
{
    public class TestPatternVideoSource : IDisposable
    {
        private static int VIDEO_SAMPLE_PERIOD_MILLISECONDS = 30;
        private static string TEST_PATTERN_IMAGE_PATH = "media/testpattern.jpeg";
        private const float TEXT_SIZE_PERCENTAGE = 0.035f;       // height of text as a percentage of the total image height
        private const float TEXT_OUTLINE_REL_THICKNESS = 0.02f; // Black text outline thickness is set as a percentage of text height in pixels
        private const int TEXT_MARGIN_PIXELS = 5;
        private const int POINTS_PER_INCH = 72;

        private static Microsoft.Extensions.Logging.ILogger logger = SIPSorcery.Sys.Log.Logger;

        private VpxEncoder _vpxEncoder;
        private SIPSorceryMedia.ImageConvert _colorConverter;
        private Timer _videoStreamTimer;
        private Bitmap _testPattern;
        private uint _width, _height, _stride;
        private bool _exit = false;
        private bool _disposedValue = false; // To detect redundant calls

        public event Action<byte[]> SampleReady;

        public TestPatternVideoSource()
        {
            _testPattern = new Bitmap(TEST_PATTERN_IMAGE_PATH);

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
            _videoStreamTimer = new Timer(SendTestPatternSample, null, 0, VIDEO_SAMPLE_PERIOD_MILLISECONDS);
            //Task.Factory.StartNew(SendTestPatternSample, TaskCreationOptions.LongRunning);
        }

        public void Stop()
        {
            _exit = true;
            _videoStreamTimer?.Dispose();
        }

        public void SendTestPatternSample(object state)
        {
            try
            {
                if (SampleReady != null)
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
            if (!_disposedValue)
            {
                _disposedValue = true;

                if (disposing)
                {
                    _testPattern.Dispose();
                }

                _vpxEncoder.Dispose();
                _vpxEncoder = null;

                _colorConverter = null;
            }
        }

        // This code added to correctly implement the disposable pattern.
        public void Dispose()
        {
            Dispose(true);
        }
    }
}
