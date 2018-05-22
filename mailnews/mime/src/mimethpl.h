/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHTMLASPLAINTEXT_H_
#define _MIMEHTMLASPLAINTEXT_H_

#include "mimetpla.h"
#include "nsString.h"

namespace mozilla::mime {

/**
 * The MimeInlineTextHTMLAsPlaintext class converts HTML->TXT->HTML,
 * i.e. HTML to Plaintext and the result to HTML again.
 * This might sound crazy, and maybe it is, but it is for the "View as Plaintext"
 * option, if the sender didn't supply a plaintext alternative (bah!).
 */
class HTMLAsPlaintext : TextPlain {
public:
  ~HTMLAsPlaintext();
  override int ParseBegin();
  override int ParseLine(const char* line, int32_t length);
  override int ParseEOF(bool abort_p);

protected:
  /**
   * Buffers the entire HTML document.
   * Gecko parser expects the complete document,
   * as wide string.
   */
  nsString* complete_buffer;
};

class HTMLAsPlaintextClass : TextPlainClass {
}

} // namespace
#endif // _MIMEHTMLASPLAINTEXT_H_
