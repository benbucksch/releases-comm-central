/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 /* -*- Mode: C; tab-width: 4 -*-
   mimeenc.c --- MIME encoders and decoders, version 2 (see mimei.h)
   Copyright (c) 1996 Netscape Communications Corporation, all rights reserved.
   Created: Jamie Zawinski <jwz@netscape.com>, 15-May-96.
 */

#ifndef _MIMEENC_H_
#define _MIMEENC_H_

#include "nsError.h"
#include "nscore.h" // for nullptr

namespace mozilla {
namespace mime {

typedef int (*MimeConverterOutputCallback)
  (const char *buf, int32_t size, void *closure);

class Part;

/* This file defines interfaces to generic implementations of Base64,
   Quoted-Printable, and UU decoders; and of Base64 and Quoted-Printable
   encoders.
 */

class Decoder {
public:
  enum class Encoding {
    None,
    Base64,
    QuotedPrintable,
    UUEncode,
    YEncode
  };

  Decoder(Encoding which, MimeConverterOutputCallback output_fn, void* closure, Part* object = nullptr);
  /**
   * Push data through the encoder/decoder, causing the above-provided output_fn
   * to be called with encoded/decoded data.
   */
  int Write(const char* buffer, int32_t size, int32_t* outSize);
  /*
   * When you're done encoding/decoding, call this to free the data.
   * If you're aborting, then just delete without calling Flush() first.
   */
  int Flush();
  ~Decoder();

private:
  int DecodeQPBuffer(const char* buffer, int32_t length, int32_t* outSize);
  int DecodeBase64Token(const char* in, char* out);
  int DecodeBase64Buffer(const char* buffer, int32_t length, int32_t* outSize);
  int DecodeUUEBuffer(const char* buffer, int32_t length, int32_t* outSize);
  int DecodeYEncBuffer(const char* buffer, int32_t length, int32_t* outSize);

  enum class State {
    DS_BEGIN, DS_BODY, DS_END
  };

  /**
   * Which encoding to use.
   */
  Encoding encoding;

  /* A read-buffer used for QP and B64. */
  char token[4];
  int token_size;

  /* State and read-buffer used for uudecode and yencode. */
  State ds_state;
  char* line_buffer;
  int line_buffer_size;

  Part* objectToDecode; // might be null, only used for QP currently
  /* Where to write the decoded data */
  MimeConverterOutputCallback write_buffer;
  void* closure;
};

} // namespace mime
} // namespace mozilla

#endif /* _MODMIMEE_H_ */
