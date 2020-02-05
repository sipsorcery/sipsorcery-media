//-----------------------------------------------------------------------------
// Filename: VpxEncoder.h
//
// Description: A rudimentary VP8 encoder wrapper for libvpx.
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
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>

#include <memory>

using namespace System;
using namespace System::Runtime::InteropServices;

namespace SIPSorceryMedia {

	public ref class VpxEncoder
	{
		public:

			/**
			* Default constructor.
			*/
			VpxEncoder();

			/**
			* Default destructor.
			*/
			~VpxEncoder();

			/**
			* Initialises the VP8 encoder.
			* @param[in] width: the width of the I420 image that will be encoded.
			* @param[in] height: the height of the I420 image that will be encoded.
			* @param[in] stride: the stride (alignment) of the I420 image that will be encoded.
			* @@Returns: 0 if successful or -1 if not.
			*/
			int InitEncoder(unsigned int width, unsigned int height, unsigned int stride);
			
			/**
			* Initialises the VP8 decoder.
			* @@Returns: 0 if successful or -1 if not.
			*/
			int InitDecoder();
			
			/**
			* Attempts to encode an I420 frame as VP8.
			* @param[in] i420: pointer to the buffer with the i420 frame to encode.
			* @param[in] i420Length: the length of the i420 buffer.
			* @param[in] sampleCount: an integer which when multiplied by the stream's timebase gives the 
			* presentation time of the sample.
			* @param[out] buffer: a buffer holding the VP8 encoded sample.
			* @@Returns: 0 if successful or -1 if not.
			*/
			int Encode(unsigned char * i420, int i420Length, int sampleCount, array<Byte> ^% buffer);
			
			/**
			* Attempts to decode an VP8 frame to an I420 image.
			* @param[in] buffer: pointer to the VP8 encoded frame to decode.
			* @param[in] bufferSize: the length of the VP8 encoded frame.
			* @param[out] outBuffer: a buffer holding the decoded I420 image. This buffer will be allocated
			*  and should be null.
			* @param[out] width: the width of the decoded I420 image.
			* @param[out] height: the height of the decoded I420 image.
			* @@Returns: 0 if successful or -1 if not.
			*/
			int Decode(unsigned char* buffer, int bufferSize, array<Byte> ^% outBuffer, unsigned int % width, unsigned int % height);

			/**
			* Returns the current width of the VP8 encoder.
			* @@Returns: the current width of the VP8 encoder.
			*/
      int GetWidth() { return _width; }

			/**
			* Returns the current height of the VP8 encoder.
			* @@Returns: the current height of the VP8 encoder.
			*/
      int GetHeight() { return _height; }

			/**
			* Returns the current stride/alignment of the VP8 encoder.
			* @@Returns: the current width/alignment of the VP8 encoder.
			*/
      int GetStride() { return _stride; }

		private:

			vpx_codec_ctx_t * _vpxCodec;
			vpx_codec_ctx_t * _vpxDecoder;
			vpx_image_t * _rawImage;
			int _width = 0, _height = 0, _stride = 0;
	};
}

