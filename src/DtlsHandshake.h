//-----------------------------------------------------------------------------
// Filename: DtlsHandshake.h
//
// Description: Performs either the server or client end of a DTLS handshake.
// The handshake provides the keying material for the SRTP session.
//
// Author(s):
// Aaron Clauson (aaron@sipsorcery.com)
//
// History:
// ??	          Aaron Clauson	  Created, based on https://gist.github.com/roxlu/9835067.
// 24 Aug 2019  Aaron Clauson   Added header comment block.
// 19 Jan 2020  Aaron Clauson   Switched from using memory to datagram BIO.
// 29 Jan 2020  Aaron Clauson   Added method for client handshake.
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
// Memory BIO's also resulted in Microsoft's Edge browser not being able to complete
// the handshake because of a seeming incompatibility with packet fragmentation.
// The memory BIO typically concatenated multiple transmissions for the client 
// into a single output and when that was communicated to Edge it doesn't seem
// able to separate them again.
//
// The alternative to a memory BIO is to use a datagram BIO (BIO_new_dgram) and 
// pass the socket handle from C# directly to this class. The OpenSSL functions
// are able to recognised DTLS packets and process the handshake while leaving
// any other packets (STUN, RTP, RTCP etc.) alone for the application to handle.
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

#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/bio.h>
#include <openssl/dtls1.h>
#include <openssl/err.h>
#include <openssl/srtp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <string>

using namespace System;
using namespace System::Runtime::InteropServices;

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

  public ref class DtlsHandshake
  {
  private:
    static bool _isOpenSSLInitialised = false;

    krx* _k{ nullptr };
    property System::String^ _certFile;
    property System::String^ _keyFile;
    bool _handshakeComplete = false;

  public:

    property System::Boolean Debug;

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
    * Constructor for acting as the client side of the handshake.
    */
    DtlsHandshake();

    /**
    * Constructor for acting as the server side of the handshake.
    * @param[in] certFile: path to the certificate file, must be in PEM format.
    * @param[in] keyFile: path to the private key file, must be in PEM format.
    */
    DtlsHandshake(System::String^ certFile, System::String^ _keyFile);

    /**
    * Destructor. Cleans up the SSL context and associated structures.
    */
    ~DtlsHandshake();

    /**
    * Performs the DTLS handshake as the server. This method blocks waiting for the 
    * client to initiate the connection and then attempts to complete the handshake.
    * @param[in] socket: handle to the socket to perform the DTLS handshake on. Note
    *  the SSL context is configured in "accept" mode which means our end is acting
    *  as the server. The remote socket will need to initiate the DTLS handshake.
    * @param[out] fingerprint: if the handshake completes successfully then the
    *  fingerprint (sha2556 hash of the X509 certificate) of the client's X509 
    *  certificate will be set. It's up to the calling application to verify the 
    *  fingerprint matches what was expected.
    * @@Returns: 0 if the handshake completed successfully or -1 if there was an error.
    */
    int DoHandshakeAsServer(SOCKET socket, /* out */ array<Byte>^% fingerprint);

    /**
    * Performs the DTLS handshake as the client. It will initiate the handshake to
    * the DTLS server.
    * WARNING: The approach of sharing the socket handle is not reliable. If
    * another thread is also calling receive on the socket then this method will 
    * usually fail because it does not get one or more packets involved in the 
    * handshake. Ideally any other thread using the socket should pause until the 
    * handshake completes.
    * @param[in] socket: handle to the socket to perform the DTLS handshake on. 
    *  IMPORTANT: the socket must have had connect called to set the remote destination
    *  end point.
    * @param[in] svrAddrFamily: whether the remote server to perform the handshake
    *  with is IPv4 or IPv6.
    * @param[in] addrByte: IP address of the remote server.
    * @param[in] svrPort: port for the remote server.
    * @param[out] fingerprint: if the handshake completes successfully then the
    *  fingerprint (sha2556 hash of the X509 certificate) of the servers' X509 
    *  certificate will be set. It's up to the calling application to verify the 
    *  fingerprint matches what was expected.
    * @@Returns: 0 if the handshake completed successfully or -1 if there was an error.
    */
    int DoHandshakeAsClient(
      SOCKET socket, 
      short svrAddrFamily, 
      array<Byte>^ addrBytes,
      u_short svrPort, 
      /* out */ array<Byte>^% fingerprint);

    /**
    * Checks whether the DTLS handshake has been completed.
    * @@Returns: true if it has been completed or false if not.
    */
    bool IsHandshakeComplete();

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