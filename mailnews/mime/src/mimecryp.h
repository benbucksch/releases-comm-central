/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMECRYP_H_
#define _MIMECRYP_H_

#include "mimecont.h"
// #include "mimeenc.h"
#include "modmimee.h"
#include "mimepbuf.h"

namespace mozilla {
namespace mime {

/*
 * The Encrypted class implements a type of MIME object where the object
 * is passed to some other routine, which then returns a new MIME object.
 * This is the basis of a decryption module.
 *
 * Oddly, this class behaves both as a container and as a leaf: it acts as a
 * container in that it parses out data in order to eventually present a
 * contained object; however, it acts as a leaf in that this container may
 * itself have a Content-Transfer-Encoding applied to its body.  This violates
 * the cardinal rule of MIME containers, which is that encodings don't nest,
 * and therefore containers can't have encodings.  But, the fact that the
 * S/MIME spec doesn't follow the groundwork laid down by previous MIME specs
 * isn't something we can do anything about at this point...
 *
 * Therefore, this class duplicates some of the work done by the Leaf
 * class, to meet its dual goals of container-hood and leaf-hood.  (We could
 * alternately have made this class be a subclass of leaf, and had it duplicate
 * the effort of Container, but that seemed like the harder approach.)
 */
class Encrypted : public Container {
  typedef Container Super;

public:
  // Part overrides
  virtual int ParseBegin() override;
  virtual int ParseBuffer(const char* buf, int32_t size) override;
  virtual int ParseLine(const char* line, int32_t length) override = 0;
  virtual int ParseEOF(bool abort_p) override;

  // Container overrides
  virtual int AddChild(Part*) override;

  // Callbacks overriden by decryption module.
  /**
   * This is called with the Part representing the encrypted data.
   * The obj->headers should be used to initialize the decryption engine.
   * NULL indicates failure; otherwise, an opaque closure object should
   * be returned.
   *
   * output_fn is what the decryption module should use to write a new MIME
   * object (the decrypted data.)  output_closure should be passed along to
   * every call to the output routine.
   *
   * The data sent to output_fn should begin with valid MIME headers indicating
   * the type of the data.  For example, if decryption resulted in a text
   * document, the data fed through to the output_fn might minimally look like
   *
   *      Content-Type: text/plain
   *
   *      This is the decrypted data.
   *      It is only two lines long.
   *
   * Of course, the data may be of any MIME type, including multipart/mixed.
   * Any returned MIME object will be recursively processed and presented
   * appropriately.  (This also imples that encrypted objects may nest, and
   * thus that the underlying decryption module must be reentrant.)
   */
  virtual void* CryptoInit(int (*output_fn) (const char* data, int32_t data_size, void* output_closure), void* output_closure) = 0;
  /**
   * This is called with the raw encrypted data.  This data might not come
   * in line-based chunks: if there was a Content-Transfer-Encoding applied
   * to the data (base64 or quoted-printable) then it will have been decoded
   * first (handing binary data to the filter_fn.)  crypto_closure is the
   * object that CryptoInit() returned.  This may return negative on error.
   */
  virtual int CryptoWrite(const char* data, int32_t data_size, void* crypto_closure) = 0;
  /**
   * This is called when no more data remains.  It may call output_fn() again
   * to flush out any buffered data.  If abort_p is true, then it may choose
   * to discard any data rather than processing it, as we're terminating
   * abnormally.
   */
  virtual int CryptoEOF(void* crypto_closure, bool abort_p) = 0;
  /**
   * This is called after CryptoEOF() but before CryptoFree().  The crypto
   * module should return a newly-allocated string of HTML code which
   * explains the status of the decryption to the user (whether the signature
   * checked out, etc.)
   */
  virtual char* CryptoGenerateHTML(void* crypto_closure) = 0;
  /**
   * This will be called when we're all done, after CryptoEOF() and
   * CryptoGenerateHTML().  It is intended to free any data represented
   * by the crypto_closure.  output_fn may not be called.
   */
  virtual void CryptoFree(void* crypto_closure) = 0;
  /**
   * This method, of the same name as one in Leaf, is a part of the
   * afforementioned leaf/container hybridization.  This method is invoked
   * with the content-transfer-decoded body of this part (without line
   * buffering.)  The default behavior of this method is to simply invoke
   * CryptoWrite() on the data with which it is called.  It's unlikely that
   * a subclass will need to specialize this.
   */
  int ParseDecodedBuffer(const char* buf, int32_t size); // XXX TODO this is unusable as the callback that it's needed for
  static int ParseDecodedBuffer(const char* buffer, int32_t size, void* obj);
  static int HandleDecryptedOutputClosure(const char*, int32_t, void*);
  int HandleDecryptedOutput(const char*, int32_t);
  static int HandleDecryptedOutputLineClosure(char*, int32_t, void*);
  int HandleDecryptedOutputLine(char*, int32_t);

protected:
  Encrypted(Headers* hdrs, const char* contentTypeOverride)
    : Super(hdrs, contentTypeOverride)
    , crypto_closure(nullptr)
    , decoder_data(nullptr)
    , hdrs(nullptr)
    , part_buffer(nullptr)
  {}
  virtual ~Encrypted();
  void Cleanup(bool finalizing_p);
  int CloseHeaders();
  int EmitBufferedChild();

  void* crypto_closure;       /* Opaque data used by decryption module. */
  Decoder* decoder_data; /* Opaque data for the Transfer-Encoding
                  decoder. */
  Headers* hdrs;       /* Headers of the enclosed object (including
                  the type of the *decrypted* data.) */
  MimePartBufferData* part_buffer;  /* The data of the decrypted enclosed
                     object (see mimepbuf.h) */
};

} // namespace mime
} // namespace mozilla

#endif // _MIMECRYP_H_
