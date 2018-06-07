/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHTMLPARSED_H_
#define _MIMEHTMLPARSED_H_

#include "mimethtm.h"

namespace mozilla {
namespace mime {

/**
 * This runs the entire HTML document through the Mozilla HTML parser, and
 * then outputs it as string again. This ensures that the HTML document is
 * syntactically correct and complete and all tags and attributes are closed.
 *
 * That prevents "MIME in the middle" attacks like efail.de.
 * The base problem is that we concatenate different MIME parts in the output
 * and render them all together as a single HTML document in the display.
 *
 * The better solution would be to put each MIME part into its own <iframe type="content">.
 * during rendering. Unfortunately, we'd need <iframe seamless> for that.
 * That would remove the need for this workaround, and stop even more attack classes.
 */
class HTMLParsed : public HTML {
  typedef HTML Super;

public:
  HTMLParsed(Headers* hdrs, const char* overrideContentType)
    : Super(hdrs, overrideContentType)
    , complete_buffer(nullptr)
  {}
  virtual ~HTMLParsed();

  // Part overrides
  virtual int ParseBegin() override;
  virtual int ParseLine(const char* line, int32_t length) override;
  virtual int ParseEOF(bool abort_p) override;

protected:
  /**
   * Buffers the entire HTML document.
   * Gecko parser expects the complete document,
   * as wide string.
   */
  nsString* complete_buffer;
};

class HTMLParsedClass : HTMLClass {
}

} // namespace mime
} // namespace mozilla
#endif // _MIMEHTMLPARSED_H_
