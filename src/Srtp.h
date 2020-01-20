//-----------------------------------------------------------------------------
// Filename: Srtp.h
//
// Description: A rudimentary Secure Real-Time Transport (SRTP) wrapper around 
// Cisco's srtp library.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??	          Aaron Clauson	  Created.
// 24 Aug 2019  Aaron Clauson   Added header comment block.
// 20 Jan 2020	Aaron Clauson		Renamed class from SRTPManaged to Srtp.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//
// libsrtp license:
// See LICENSE_libsrtp file or https://github.com/cisco/libsrtp/blob/master/LICENSE.
//
// Useful Links:
// http://stackoverflow.com/questions/22692109/webrtc-srtp-decryption
// https://tools.ietf.org/html/rfc5764  Datagram Transport Layer Security (DTLS) 
//  Extension to Establish Keys for the Secure Real - time Transport Protocol(SRTP)
// https://tools.ietf.org/html/rfc3711 The Secure Real-time Transport Protocol (SRTP)
//-----------------------------------------------------------------------------

#pragma once

#include "Dtls.h"

#include "srtp2/srtp.h"
#include "openssl/srtp.h"
#include "openssl/err.h"
#include <msclr/marshal_cppstd.h>

#include <iostream>

using namespace System;

namespace SIPSorceryMedia {

	public ref class Srtp {
		public:

      /**
			Initialises the libsrtp library. Only needs to be called once per process.
			While the initialisation will happen automatically this method can be called
			preemptively to save a second or two when the first client connects.
			*/
      static void InitialiseLibSrtp()
      {
        // Only need to call the libsrtp initialisation routines once per process.
        if (!_isLibSrtpInitialised) {
					srtp_init();

					_isLibSrtpInitialised = true;
        }
      }

			/**
			* Constructor.
			* @param[in] key: raw key material to initialise the SRTP context with.
			* @param[in] isClient: set to true if the SRTP session is being used to receive or
			*  false if it being used to send.
			*/
			Srtp(cli::array<System::Byte>^ key, bool isClient);
	
			/**
			* Constructor.
			* @param[in] dtlsContext: the context of a DTLS session that has completed the handshake
			*  and that will be used to derive the SRTP session key material with.
			* @param[in] isClient: set to true if the SRTP session is being used to receive or
			*  false if it being used to send.
			*/
			Srtp(Dtls^ dtlsContext, bool isClient);

			/**
			* Destructor. Cleans up the SRTP session context.
			*/
			~Srtp();

			/**
			* Protects an RTP packet ready for sending.
			* @param[in] buffer: The buffer containing the RTP packet to send. Must include
			*  the additional bytes for the SRTP authentication token.
			* @param[in] length: The length of the RTP payload NOT including the bytes
			*  allocated for the SRTP authentication token.
			*/
			int ProtectRTP(cli::array<System::Byte>^ buffer, int length);

			/**
			* Attempts to decrypt and/or authorise an RTP packet.
			* @param[in] buffer: The buffer containing the RTP packet to unprotect.
			* @param[in] length: The length of the RTP packet.
			*/
			int UnprotectRTP(cli::array<System::Byte>^ buffer, int length);

			/**
			* Protects an RTCP packet ready for sending.
			* @param[in] buffer: The buffer containing the RTCP packet to send. Must include
			*  the additional bytes for the SRTP authentication token.
			* @param[in] length: The length of the RTCP payload NOT including the bytes
			*  allocated for the SRTP authentication token.
			*/
      int ProtectRTCP(cli::array<System::Byte>^ buffer, int length);

			/**
			* Attempts to decrypt and/or authorise an RTCP packet.
			* @param[in] buffer: The buffer containing the RTCP packet to unprotect.
			* @param[in] length: The length of the RTCP packet.
			*/
			int UnprotectRTCP(cli::array<System::Byte>^ buffer, int length);

		private:

			static const int SRTP_ANTI_REPLAY_WINDOW_SIZE = 128;
			static const int SRTP_AES_KEY_KEY_LEN = SRTP_AES_128_KEY_LEN;

			static bool _isLibSrtpInitialised = false;

			srtp_t * _session{ nullptr };
			System::String^ _key;
	};
}