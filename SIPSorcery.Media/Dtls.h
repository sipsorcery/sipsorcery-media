//-----------------------------------------------------------------------------
// Filename: Dtls.h
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
// 19 Jan 2020  Aaron Clauson   Switched from using memory to datagram BIO.
//
// License: 
// BSD 3-Clause "New" or "Revised" License, see included LICENSE.md file.
//
// OpenSSL License:
// This application includes software developed by the OpenSSL Project and 
// cryptographic software written by Eric Young (eay@cryptsoft.com)
// See the accompanying LICENSE file for conditions.
//
// Remarks about OpenSSL BIO's:
// ----------------------------
// BIO stands for Basic I/O Abstraction and is what OpenSSL uses to communicate
// between the SSL Context and the SSL instance. 
// See https://www.openssl.org/docs/manmaster/man7/bio.html.
//
// The original implementation in this class used memory BIO's (BIO_s_mem) which
// involved two separate BIO's one for writing DTLS handshake data received from 
// the client and the other for reading data handshake data to send to the client.
// While this approach mostly worked it involved about 150-200 lines of code to 
// wire the two BIO's together and pass the handshake data to and from the UDP
// socket (which was only accessible on the C# side).
//
// Memory BIO's also resulted in Microsoft's Edge browser note being able to complete
// the handshake because of a seeming incompatibility with packet fragmentation.
// The memory BIO typically concatenated multiple transmissions for the client 
// into a single output and when that was communicated to Edge it doesn't seem
// able to separate them again.
//
// The alternative to a memory BIO is to use a datagram BIO (BIO_new_dgram) and 
// pass the socket handle from C# directly to this class. The OpenSSL functions
// are able to recognised DTLS packets and process the handshake while leaving
// any other packets (STUN,RTP, RTCP etc.) alone or the application to handle.
// This is a much nicer approach. Instead of having to do the C++/.Net interop 
// to pass buffers backwards and forwards a single socket handle is all that gets
// passed.
//
// This approach works a lot better and Edge (Legacy) now completes the DTLS handshake
// roughly 3/4 of the time (Chrome and Firefox complete every time but they do
// with the memory BIO as well). Coincidentally Edge based on Chromium was released
// 5 days again on 15 Jan 2020 and upon testing with it the DTLS handshake completes
// 100% reliably. It's likely Edge Chromium is now also using OpenSSL's DTLS
// implementation.
//-----------------------------------------------------------------------------

#pragma once

#include "openssl/srtp.h"
#include "openssl/err.h"
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <string>

#define SRTP_ALGORITHM "SRTP_AES128_CM_SHA1_80"
#define DTLS_COOKIE "sipsorcery"
#define HANDSHAKE_ERROR_STATUS -1

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
    BIO* bio;
  } krx;

  /**
  * Convenience enum for SSL states defined in ssl.h.
  */
  public enum class DtlsState
  {
    CONNECT = 0x1000,
    ACCEPT = 0x2000,
    MASK = 0x0FFF,
    SSLSTINIT = (SSL_ST_CONNECT|SSL_ST_ACCEPT),
    BEFORE = 0x4000,
    OK = 0x03,
    SSLSTRENEGOTIATE = (0x04|SSL_ST_INIT),
    SSLSTERR = (0x05|SSL_ST_INIT),
  };

  public ref class Dtls
  {
  private:
    static bool _isOpenSSLInitialised = false;

    krx* _k{ nullptr };
    property System::String^ _certFile;
    property System::String^ _keyFile;
    bool _handshakeComplete = false;

  public:

    /**
    Initialises the OpenSSL library. Only needs to be called once per process.
    While the initialisation will happen automatically this method can be called 
    preemptively to save a second or two when the first client connects.
    */
    static void InitialiseOpenSSL()
    {
      // Only need to call the OpenSSL initialisation routines once per process.
      if (!_isOpenSSLInitialised) {
        SSL_library_init();
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();

        _isOpenSSLInitialised = true;
      }
    }

    /**
    * Constructor.
    * @param[in] certFile: path to the certificate file, must be in PEM format.
    * @param[in] keyFile: path to the private key file, must be in PEM format.
    */
    Dtls(System::String^ certFile, System::String^ _keyFile);

    /**
    * Destructor. Cleans up the SSL context and associated structures.
    */
    ~Dtls();

    /**
    * Initialises the SSL context, API and other bits and pieces required to 
    * accept DTLS clients or connect to a DTLS server. It then waits for the 
    * DTLS handshake to complete on the provided socket handle.
    * @param[in] socket: handle to the socket to perform the DTLS handshake on. Note
    *  the SSL context is configured in "accept" mode which means our end is acting
    *  as the server. The remote socket will need to initiate the DTLS handshake.
    * @@Returns: 0 if the handshake completed successfully or -1 if there was an error.
    */
    int DoHandshake(SOCKET socket);

    /**
    * Checks whether the DTLS handshake has been completed.
    * @@Returns: true if it has been completed or false if not.
    */
    bool IsHandshakeComplete();

    /**
    * Gets the state of the SSL connection. It should match one of the options
    * in the DtlsState enum.
    */
    int GetState();

    /**
    * Shutsdown the SSL context and the instance and cleans up.
    */
    void Shutdown();

    /**
    * Provides access to the SSL connection. Access is needed by the SRTP connection to
    * initialise its keying material.
    */
    SSL* GetSSL()
    {
      if (_k != nullptr) {
        return _k->ssl;
      }
      else {
        return nullptr;
      }
    }
  };
}