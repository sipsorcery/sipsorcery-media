//-----------------------------------------------------------------------------
// Filename: ImageConvert.h
//
// Description: Uses ffmpeg routines to convert between pixel formats.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??	          Aaron Clauson	  Created.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#pragma once

#include <stdio.h>

#include "VideoSubTypes.h"

extern "C"
{
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

using namespace System;
using namespace System::Runtime::InteropServices;

namespace SIPSorceryMedia {

  public ref class ImageConvert
  {
  public:

    /**
    * Default constructor.
    */
    ImageConvert();
    
    /**
    * Default destructor.
    */
    ~ImageConvert();

    /**
    * Converts an RGB pixel formatted image to a YUV image.
    * @param[in] bmp: the RGB source image to convert.
    * @param[in] rgbInputFormat: the RGB type of source image (e.g. RGB32. BGR32 etc.).
    * @param[in] width: the width of the source image.
    * @param[in] height: the height of the source image.
    * @param[in] stride: the stride of the source image.
    * @param[in] yuvOutputFormat: the YUV format for the destination image (e.g. I420, YUV2 etc.).
    * @param[in] buffer: the buffer to hold the destination YUV image.
    */
    int ConvertRGBtoYUV(
      unsigned char* bmp,
      VideoSubTypesEnum rgbInputFormat,
      int width,
      int height,
      int stride,
      VideoSubTypesEnum yuvOutputFormat,
      /* out */ array<Byte>^% buffer);

    /**
    * Converts a YUV pixel formatted image to an RGB image.
    * @param[in] yuv: the source image to convert.
    * @param[in] yuvInputFormat: the YUV type of source image (e.g. I420, YUV2 etc.).
    * @param[in] width: the width of the source image.
    * @param[in] height: the height of the source image.
    * @param[in] rgbOutputFormat: the RGB type format for the destination image (e.g. I420, YUV2 etc.).
    * @param[in] buffer: the buffer to hold the destination RGB image.
    */
    int ConvertYUVToRGB(
      unsigned char* yuv,
      VideoSubTypesEnum yuvInputFormat,
      int width,
      int height,
      VideoSubTypesEnum rgbOutputFormat,
      /* out */ array<Byte>^% buffer);

  private:
    SwsContext* _swsContextRGBToYUV;
    SwsContext* _swsContextYUVToRGB;
  };
}