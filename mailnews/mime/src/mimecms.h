/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMECMS_H_
#define _MIMECMS_H_

#include "mimecryp.h"

class nsICMSMessage;

namespace mozilla {
namespace mime {

/**
 * The EncryptedCMS class implements a type of MIME object where the
 * object is passed through a CMS decryption engine to decrypt or verify
 * signatures.  That module returns a new MIME object, which is then presented
 * to the user.  See mimecryp.h for details of the general mechanism on which
 * this is built.
 */
class EncryptedCMS : public Encrypted {
  typedef Encrypted Super;

protected:
  EncryptedCMS();
  virtual ~EncryptedCMS();

public:
  // Part overrides
  virtual bool IsEncrypted() override;

  // Encrypted overrides
  virtual void* CryptoInit(int (*output_fn)(const char* buf, int32_t bufSize, void* output_closure), void* output_closure) override;
  virtual int CryptoWrite(const char* buf, int32_t bufSize, void* closure) override;
  virtual int CryptoEOF(void* crypto_closure, bool abort_p) override;
  virtual char* GenerateHTML(void* crypto_closure) override;
  virtual int CryptoFree(void* crypto_closure) override;
};

} // namespace mime
} // namespace mozilla

#endif // _MIMEPKCS_H_
