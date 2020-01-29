//-----------------------------------------------------------------------------
// Filename: DtlsHandshake.cpp
//
// Description: See header.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//-----------------------------------------------------------------------------

#include "DtlsHandshake.h"

namespace SIPSorceryMedia {

  int krx_ssl_verify_peer(int ok, X509_STORE_CTX* ctx) {
    return 1;
  }

  int verify_cookie(SSL* ssl, const unsigned char* cookie, unsigned int cookie_len)
  {
    // Accept any cookie.
    return 1;
  }

  int generate_cookie(SSL* ssl, unsigned char* cookie, unsigned int* cookie_len)
  {
    int cookieLength = sizeof(DTLS_COOKIE);
    *cookie_len = cookieLength;
    memcpy(cookie, (unsigned char*)DTLS_COOKIE, cookieLength);
    return 1;
  }

  void krx_ssl_info_callback(const SSL* ssl, int where, int ret)
  {
    if (ret == 0) {
      printf("-- krx_ssl_info_callback: error occurred.\n");
      return;
    }

    SSL_WHERE_INFO(ssl, where, SSL_CB_LOOP, "LOOP");
    SSL_WHERE_INFO(ssl, where, SSL_CB_HANDSHAKE_START, "HANDSHAKE START");
    SSL_WHERE_INFO(ssl, where, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
  }

  DtlsHandshake::DtlsHandshake()
  {
    if (!_isOpenSSLInitialised) {
      DtlsHandshake::InitialiseOpenSSL();
    }

    _k = new krx();
    _k->ctx = nullptr;
    _k->ssl = nullptr;
    _k->bio = nullptr;
  }

  DtlsHandshake::DtlsHandshake(System::String^ certFile, System::String^ keyFile) :
    DtlsHandshake()
  {
    _certFile::set(certFile);
    _keyFile::set(keyFile);
  }

  DtlsHandshake::~DtlsHandshake()
  {
    Shutdown();
  }

  /*
  * Performs the server side of a DTLS handshake.
  */
  int DtlsHandshake::DoHandshakeAsServer(SOCKET rtpSocket)
  {
    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    int r = 0;

    /* create a new context using DTLS */
    _k->ctx = SSL_CTX_new(DTLS_server_method());
    if (!_k->ctx) {
      printf("Error: cannot create SSL_CTX.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    /* set our supported ciphers */
    r = SSL_CTX_set_cipher_list(_k->ctx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    if (r != 1) {
      printf("Error: cannot set the cipher list.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    /* enable srtp */
    r = SSL_CTX_set_tlsext_use_srtp(_k->ctx, SRTP_ALGORITHM);
    if (r != 0) {
      printf("Error: cannot setup srtp.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    std::string certFilePath = msclr::interop::marshal_as<std::string>(_certFile);

    /* certificate file; contains also the public key */
    r = SSL_CTX_use_certificate_file(_k->ctx, certFilePath.c_str(), SSL_FILETYPE_PEM);
    if (r != 1) {
      printf("Error: cannot load certificate file.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    std::string keyFilePath = msclr::interop::marshal_as<std::string>(_keyFile);

    /* load private key */
    r = SSL_CTX_use_PrivateKey_file(_k->ctx, keyFilePath.c_str(), SSL_FILETYPE_PEM);
    if (r != 1) {
      printf("Error: cannot load private key file.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    /* check if the private key is valid */
    r = SSL_CTX_check_private_key(_k->ctx);
    if (r != 1) {
      printf("Error: checking the private key failed. \n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    // Don't need to use a handshake cookie at this point. The DTLS 
    // handshake does not get initiated until the ICE connection is done
    // and that serves as the DoS protection.
    //SSL_CTX_set_cookie_generate_cb(_k->ctx, generate_cookie);
    //SSL_CTX_set_cookie_verify_cb(_k->ctx, verify_cookie);
    SSL_CTX_set_ecdh_auto(_k->ctx, 1);                        // Needed for FireFox DTLS negotiation.
    SSL_CTX_set_verify(_k->ctx, SSL_VERIFY_NONE, nullptr);    // The client doesn't have to send it's certificate.
    //SSL_CTX_set_verify(_k->ctx, SSL_VERIFY_PEER, krx_ssl_verify_peer);

    /* create SSL* */
    _k->ssl = SSL_new(_k->ctx);
    if (!_k->ssl) {
      printf("Error: cannot create new SSL*.\n");
      return HANDSHAKE_ERROR_STATUS;
    }

    _k->bio = BIO_new_dgram((int)rtpSocket, BIO_NOCLOSE);
    if (!_k->bio) {
      printf("Error: cannot create new BIO*.\n");
      return HANDSHAKE_ERROR_STATUS;
    }

    SSL_set_bio(_k->ssl, _k->bio, _k->bio);
    SSL_set_info_callback(_k->ssl, krx_ssl_info_callback);

    // Wait for a client to initiate the DTLS handshake.
    SSL_set_accept_state(_k->ssl);

    // No cookie required.
    //BIO_ADDR* clientAddr = BIO_ADDR_new();
    //DTLSv1_listen(_k->ssl, clientAddr);
    //BIO_ADDR_free(clientAddr);

    //printf("New DTLS client connection.\n");

    // Attempt to complete the DTLS handshake
    // If successful, the DTLS link state is initialized internally
    if (SSL_accept(_k->ssl) <= 0) {
      printf("Failed to complete SSL handshake.\n");
      return HANDSHAKE_ERROR_STATUS;
    }
    else {
      printf("DTLS Handshake completed.\n");
    }

    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    return 0;
  }
 
  /*
  * Performs the client side of a DTLS handshake.
  */
  int DtlsHandshake::DoHandshakeAsClient(SOCKET rtpSocket, short svrAddrFamily, u_long svrIPAddr, u_short svrPort)
  {
    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    int r = 0;
    sockaddr_in svrAddr;
    svrAddr.sin_family = svrAddrFamily;
    svrAddr.sin_addr.s_addr = htonl(svrIPAddr);
    svrAddr.sin_port = htons(svrPort);

    /* create a new context using DTLS */
    _k->ctx = SSL_CTX_new(DTLS_client_method());
    if (!_k->ctx) {
      printf("Error: cannot create SSL_CTX.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    /* set our supported ciphers */
    r = SSL_CTX_set_cipher_list(_k->ctx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    if (r != 1) {
      printf("Error: cannot set the cipher list.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    /* enable srtp */
    r = SSL_CTX_set_tlsext_use_srtp(_k->ctx, SRTP_ALGORITHM);
    if (r != 0) {
      printf("Error: cannot setup srtp.\n");
      ERR_print_errors_fp(stderr);
      return HANDSHAKE_ERROR_STATUS;
    }

    SSL_CTX_set_ecdh_auto(_k->ctx, 1);                        // Needed for FireFox DTLS negotiation.
    SSL_CTX_set_verify(_k->ctx, SSL_VERIFY_NONE, nullptr);    // The client doesn't have to send it's certificate.

    /* create SSL* */
    _k->ssl = SSL_new(_k->ctx);
    if (!_k->ssl) {
      printf("Error: cannot create new SSL*.\n");
      return HANDSHAKE_ERROR_STATUS;
    }

    _k->bio = BIO_new_dgram((int)rtpSocket, BIO_NOCLOSE);
    if (!_k->bio) {
      printf("Error: cannot create new BIO*.\n");
      return HANDSHAKE_ERROR_STATUS;
    }

    SSL_set_bio(_k->ssl, _k->bio, _k->bio);
    SSL_set_info_callback(_k->ssl, krx_ssl_info_callback);

    // We will be initiating the handshake.
    SSL_set_connect_state(_k->ssl);

    if (BIO_ctrl(_k->bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &svrAddr) <= 0) {
      printf("Error: BIO_CTL to set BIO_CTRL_DGRAM_SET_CONNECTED failed.\n");
    }

    // Attempt to complete the DTLS handshake
    // If successful, the DTLS link state is initialized internally
    if (SSL_connect(_k->ssl) <= 0) {
      printf("Failed to complete SSL handshake.\n");
      return HANDSHAKE_ERROR_STATUS;
    }
    else {
      printf("DTLS Handshake completed.\n");
    }

    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    return 0;
  }

  bool DtlsHandshake::IsHandshakeComplete()
  {
    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    return SSL_get_state(_k->ssl) == TLS_ST_OK;
  }

  void DtlsHandshake::Shutdown()
  {
    // Dump any openssl errors.
    ERR_print_errors_fp(stderr);

    if (_k != nullptr) {

      if (_k->ctx != nullptr) {
        SSL_CTX_free(_k->ctx);
        _k->ctx = nullptr;
      }

      if (_k->ssl != nullptr) {
        SSL_shutdown(_k->ssl);
        SSL_free(_k->ssl);
        _k->ssl = nullptr;
      }

      _k = nullptr;
    }
  }
}