/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* The MimeInlineTextPlain class implements the text/plain MIME content type,
   and is also used for all otherwise-unknown text/ subtypes.
 */

#ifndef _MIMETEXTPLAIN_H_
#define _MIMETEXTPLAIN_H_

#include "mimetext.h"

namespace mozilla::mime {

class TextPlain : public Text {
public:
  override int ParseBegin();
  override int ParseLine(const char *line, int32_t length);
  override int ParseEOF(bool abort_p);

protected:
  uint32_t mCiteLevel;
  bool            mBlockquoting;
  //bool            mInsideQuote;
  int32_t         mQuotedSizeSetting;   // mail.quoted_size
  int32_t         mQuotedStyleSetting;  // mail.quoted_style
  nsCString       mCitationColor;       // mail.citation_color
  bool            mStripSig;            // mail.strip_sig_on_reply
  bool            mIsSig;
};

class TextPlainClass : TextClass {
}

} // namespace
#endif // _MIMETEXTPLAIN_H_
