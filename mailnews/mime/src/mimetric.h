/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMETRIC_H_
#define _MIMETRIC_H_

#include "mimetext.h"

namespace mozilla {
namespace mime {

/**
 * The MimeInlineTextRichtext class implements the (obsolete and deprecated)
 * text/richtext MIME content type, as defined in RFC 1341, and also the
 * text/enriched MIME content type, as defined in RFC 1563.
 */
class InlineTextRichtext : public InlineText {
  typedef InlineText Super;

public:
  InlineTextRichtext()
    : encriched_p(0)
    {}
  virtual ~InlineTextRichtext() {}

  // Part overrides
  virtual int ParseBegin() override;
  virtual int ParseLine(const char* line, int32_t length) override;

protected:
  bool enriched_p;  /* Whether we should act like text/enriched instead. */
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMETRIC_H_ */
