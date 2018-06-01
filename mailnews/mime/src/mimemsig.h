/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMSIG_H_
#define _MIMEMSIG_H_

#include "mimemult.h"
#include "mimepbuf.h"
#include "modmimee.h"

namespace mozilla {
namespace mime {

/**
 * The MimeMultipartSigned class implements the multipart/signed MIME
 * container, which provides a general method of associating a cryptographic
 * signature to an arbitrary MIME object.
 */
class MultipartSigned : public Multipart {
  typedef Multipart Super;

public:
  // Part overrides
  virtual int ParseLine(const char* line, int32_t length) override;
  virtual int ParseEOF(bool abort_p) override;

  // Multipart overrides
  virtual int CreateChild() override;
  virtual int CloseChild() override;
  virtual int ParseChildLine(const char* line, int32_t length, bool isFirstLine) override;

  // Callbacks used by dexlateion (really, signature verification) module.
  /**
   * This is called with the object, the object->headers of which should be
   * used to initialize the dexlateion engine.  NULL indicates failure;
   * otherwise, an opaque closure object should be returned.
   */
  virtual void* CryptoInit() = 0;
  /**
   * This is called with the raw data, for which a signature has been computed.
   * The crypto module should examine this, and compute a signature for it.
   */
  virtual int CryptoDataHash(const char* data, int32_t data_size, void* crypto_closure) = 0;
  /**
   * This is called when no more data remains.  If `abort_p' is true, then the
   * crypto module may choose to discard any data rather than processing it,
   * as we're terminating abnormally.
   */
  virtual int CryptoDataEof(void*crypto_closure, bool abort_p) = 0;
  /**
   * This is called after CryptoDataEOF() and just before the first call to
   * CryptoSignatureHash().  The crypto module may wish to do some
   * initialization here, or may wish to examine the actual headers of the
   * signature object itself.
   */
  virtual int CryptoSignatureInit(void* crypto_closure, Part* multipart_object, Headers* signature_hdrs) = 0;
  /**
   * This is called with the raw data of the detached signature block.  It will
   * be called after CryptoDataEOF() has been called to signify the end of
   * the data which is signed.  This data is the data of the signature itself.
   */
  virtual int CryptoSignatureHash(const char* data, int32_t data_size, void* crypto_closure) = 0;
  /**
   * This is called when no more signature data remains.  If abort_p is true,
   * then the crypto module may choose to discard any data rather than
   * processing it, as we're terminating abnormally.
   */
  virtual int CryptoSignatureEOF(void* crypto_closure, bool abort_p) = 0;
  /**
   * This is called after CryptoSignatureEOF() but before CryptoFree().
   * The crypto module should return a newly-allocated string of HTML code
   * which explains the status of the dexlateion to the user (whether the
   * signature checks out, etc.)
   */
  virtual char* CryptoGenerateHtml(void* crypto_closure) = 0;
  /**
   * This will be called when we're all done, after CryptoSignatureEOF() and
   * CryptoGenerateHTML().  It is intended to free any data represented by the
   * crypto_closure.
   */
  virtual void CryptoFree(void* crypto_closure) = 0;

  enum ParseState {
    Preamble,
    BodyFirstHeader,
    BodyHeaders,
    BodyFirstLine,
    BodyLine,
    SignatureHeaders,
    SignatureFirstLine,
    SignatureLine,
    Epilogue
  };

protected:
  MultipartSigned();
  virtual ~MultipartSigned();
  /**
   * State of parser.
   */
  ParseState state;
  /**
   * Opaque data used by signature verification module.
   */
  void* crypto_closure;
  /**
   * The headers of the signed object.
   */
  Headers* body_hdrs;
  /**
   * The headers of the signature.
   */
  Headers* sig_hdrs;
  /**
   * The buffered body of the signed object (see mimepbuf.h)
   */
  MimePartBufferData* part_buffer;
  /**
   * The signature is probably base64 encoded;
   * this is the decoder used to get raw bits out of it.
   */
  Decoder* sig_decoder_data;
};

} // namespace mime
} // namespace mozilla

#endif // _MIMEMSIG_H_
