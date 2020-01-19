//-----------------------------------------------------------------------------
// Filename: DtlsManaged.h
//
// Description: A rudimentary Data Transport Layer Security (DTLS) wrapper 
// around OpenSSL DTLS functions.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??	          Aaron Clauson	  Created, based on https://gist.github.com/roxlu/9835067.
// 24 Aug 2019  Aaron Clauson   Added header comment block.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//
// OpenSSL License:
// This application includes software developed by the OpenSSL Project and 
// cryptographic software written by Eric Young (eay@cryptsoft.com)
// See the accompanying LICENSE file for conditions.
//-----------------------------------------------------------------------------

#pragma once

#include "srtp2/srtp.h"
#include "openssl/srtp.h"
#include "openssl/err.h"
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <string>

#define SSL_WHERE_INFO(ssl, w, flag, msg) {                \
    if(w & flag) {                                         \
      printf("%20.20s", msg);                              \
      printf(" - %30.30s ", SSL_state_string_long(ssl));   \
      printf(" - %5.10s ", SSL_state_string(ssl));         \
      printf("\n");                                        \
	    }                                                    \
    } 

namespace SIPSorceryMedia {

  typedef struct {
    SSL_CTX* ctx;		/* main ssl context */
    SSL* ssl;       /* the SSL* which represents a "connection" */
    BIO* in_bio;    /* we use memory read bios */
    BIO* out_bio;   /* we use memory write bios */
    char name[512];
  } krx;

  /**
  * Convenience enum for SSL states defined in ssl.h.
  */
  public enum class DtlsState
  {
    CONNECT = 0x1000,
    ACCEPT = 0x2000,
    MASK = 0x0FFF,
    //SSL_ST_INIT                     (SSL_ST_CONNECT|SSL_ST_ACCEPT)
    BEFORE = 0x4000,
    OK = 0x03,
    //SSL_ST_RENEGOTIATE              (0x04|SSL_ST_INIT)
    //SSL_ST_ERR                      (0x05|SSL_ST_INIT)
  };

  public ref class DtlsManaged
  {
  private:
    static bool _isOpenSSLInitialised;

    krx* _k{ nullptr };
    property System::String^ _certFile;
    property System::String^ _keyFile;
    bool _handshakeComplete = false;

    static int VerifyPeer(int ok, X509_STORE_CTX* ctx);

  public:

    /**
    * Constructor.
    * @param[in] certFile: path to the certificate file, must be in PEM format.
    * @param[in] keyFile: path to the private key file, must be in PEM format.
    */
    DtlsManaged(System::String^ certFile, System::String^ _keyFile);

    /**
    * Destructor. Cleans up the SSL context and associated structures.
    */
    ~DtlsManaged();

    /**
    * Initialises the SSL context, API and other bits and pieces required to 
    * accept DTLS clients or connect to a DTLS server.
    */
    int Init();

    //int DoHandshake(cli::array<System::Byte>^ buffer, int bufferLength); 
    int Write(cli::array<System::Byte>^ buffer, int bufferLength);
    int Read(cli::array<System::Byte>^ buffer, int bufferLength);
    bool IsHandshakeComplete();
    int GetState();

    void Shutdown();

    property SSL* Ssl {
      SSL* get() {
        return _k->ssl;
      }
    }
  };
}