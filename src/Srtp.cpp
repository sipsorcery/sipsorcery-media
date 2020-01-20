//-----------------------------------------------------------------------------
// Filename: Srtp.cpp
//
// Description: See header.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#include "Srtp.h"

using namespace System;
using namespace System::Runtime::InteropServices;

namespace SIPSorceryMedia {

	Srtp::Srtp(cli::array<System::Byte>^ key, bool isClient)
	{
		InitialiseLibSrtp();

		// Need pre-processor directive of ENABLE_DEBUGGING for libsrtp debugging.
		//debug_on(mod_srtp);
		//debug_on(srtp_mod_auth);

		pin_ptr<System::Byte> p = &key[0];

		srtp_policy_t policy;

		// set policy to describe a policy for an SRTP stream
		srtp_crypto_policy_set_rtp_default(&policy.rtp);
		srtp_crypto_policy_set_rtcp_default(&policy.rtcp);

		policy.key = reinterpret_cast<unsigned char*>(p);
		policy.window_size = SRTP_ANTI_REPLAY_WINDOW_SIZE;
		policy.allow_repeat_tx = 0;
		policy.ssrc.type = (isClient) ? ssrc_any_outbound : ssrc_any_inbound;
		policy.enc_xtn_hdr_count = 0;
		policy.next = NULL;
		
		_session = new srtp_t();
		auto err = srtp_create(_session, &policy);

		std::cout << "Create srtp session result " << err << "." << std::endl;
	}

	Srtp::Srtp(Dtls^ dtlsContext, bool isClient)
	{
		InitialiseLibSrtp();

		// Need pre-processor directive of ENABLE_DEBUGGING for libsrtp debugging.
		//debug_on(mod_srtp);
		//debug_on(srtp_mod_auth);

		unsigned char dtls_buffer[SRTP_AES_KEY_KEY_LEN * 2 + SRTP_SALT_LEN * 2];
		unsigned char client_write_key[SRTP_AES_KEY_KEY_LEN + SRTP_SALT_LEN];
		unsigned char server_write_key[SRTP_AES_KEY_KEY_LEN + SRTP_SALT_LEN];
		size_t offset = 0;

		const char * label = "EXTRACTOR-dtls_srtp";

		SRTP_PROTECTION_PROFILE * srtp_profile = SSL_get_selected_srtp_profile(dtlsContext->GetSSL());

		int res = SSL_export_keying_material(dtlsContext->GetSSL(),
			dtls_buffer,
			sizeof(dtls_buffer),
			label,
			strlen(label),
			NULL,
			0,
			0);

		if (res != 1)
		{
			printf("Export of SSL key information failed.\n");
		}
		else
		{
			memcpy(&client_write_key[0], &dtls_buffer[offset], SRTP_AES_KEY_KEY_LEN);
			offset += SRTP_AES_KEY_KEY_LEN;
			memcpy(&server_write_key[0], &dtls_buffer[offset], SRTP_AES_KEY_KEY_LEN);
			offset += SRTP_AES_KEY_KEY_LEN;
			memcpy(&client_write_key[SRTP_AES_KEY_KEY_LEN], &dtls_buffer[offset], SRTP_SALT_LEN);
			offset += SRTP_SALT_LEN;
			memcpy(&server_write_key[SRTP_AES_KEY_KEY_LEN], &dtls_buffer[offset], SRTP_SALT_LEN);

			srtp_policy_t policy;

			srtp_crypto_policy_set_rtp_default(&policy.rtp);
			srtp_crypto_policy_set_rtcp_default(&policy.rtcp);

			/* Init transmit direction */
			policy.key = (isClient) ? client_write_key : server_write_key;

			policy.ssrc.value = 0;
			policy.window_size = SRTP_ANTI_REPLAY_WINDOW_SIZE;
			policy.allow_repeat_tx = 0;
			policy.ssrc.type = (isClient) ? ssrc_any_inbound : ssrc_any_outbound;
			policy.next = NULL;
			policy.enc_xtn_hdr_count = 0;
			_session = new srtp_t();

			auto err = srtp_create(_session, &policy);
			if (err != srtp_err_status_ok) {
				printf("Unable to create SRTP session.\n");
			}

			if (isClient)
			{
				std::cout << "Create srtp client session result " << err << "." << std::endl;
			}
			else
			{
				std::cout << "Create srtp server session result " << err << "." << std::endl;
			}
		}
	}

	int Srtp::UnprotectRTP(cli::array<System::Byte>^ buffer, int length)
	{
		pin_ptr<System::Byte> p = &buffer[0];
		return srtp_unprotect(*_session, reinterpret_cast<char*>(p), &length);
	}

	int Srtp::ProtectRTP(cli::array<System::Byte>^ buffer, int length)
	{
		pin_ptr<System::Byte> p = &buffer[0];
		return srtp_protect(*_session, reinterpret_cast<char*>(p), &length);
	}

  int Srtp::ProtectRTCP(cli::array<System::Byte>^ buffer, int length)
  {
		pin_ptr<System::Byte> p = &buffer[0];
    return srtp_protect_rtcp(*_session, reinterpret_cast<char*>(p), &length);
  }

	int Srtp::UnprotectRTCP(cli::array<System::Byte>^ buffer, int length)
	{
		pin_ptr<System::Byte> p = &buffer[0];
		return srtp_unprotect_rtcp(*_session, reinterpret_cast<char*>(p), &length);
	}

	Srtp::~Srtp()
	{
		if (_session != nullptr)
		{
			srtp_dealloc(*_session);
			_session = nullptr;
		}
	}
}
