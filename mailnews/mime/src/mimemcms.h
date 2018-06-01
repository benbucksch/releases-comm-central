/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMPKC_H_
#define _MIMEMPKC_H_

#include "mimemsig.h"

class nsICMSMessage;

namespace mozilla {
namespace mime {

/*
 * The MultipartSignedCMS class implements a multipart/signed MIME
 * container with protocol=application/x-CMS-signature, which passes the
 * signed object through CMS code to verify the signature.  See mimemsig.h
 * for details of the general mechanism on which this is built.
 */
class MultipartSignedCMS : public MultipartSigned
{
  typedef MultipartSigned Super;

public:
  MultipartSignedCMS() {}
  virtual ~MultipartSignedCMS() {}

  virtual void* CryptoInit() override;
  virtual int CryptoDataHash(const char* data, int32_t dataSize, void* crypto_closure) override;
  virtual int CryptoSignatureHash(const char* data, int32_t dataSize, void* crypto_closure) override;
  virtual int CryptoDataEOF(void* crypto_closure, bool abort_p) override;
  virtual int CryptoSignatureEOF(void* crypto_closure, bool abort_p) override;
  virtual int CryptoSignatureInit(void* crypto_closure, Part* multipart_object, Headers* signature_hdrs);
  virtual char* CryptoGenerateHTML(void* crypto_closure);
  virtual void CryptoFree(void* crypto_closure);
};

} // namespace mime
} // namespace mozilla

#endif // _MIMEMPKC_H_
