/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMETEXTFLOWED_H_
#define _MIMETEXTFLOWED_H_

#include "mimetext.h"

namespace mozilla {
namespace mime {

/**
 * The TextPlainFlowed class implements the text/plain MIME content type
 * for the special case of the subtype format=flowed.
 * @see RFC 2646
 * @see <https://joeclark.org/ffaq.html>
 * This implementation was based on the earlier draft
 * <ftp://ftp.ietf.org/internet-drafts/draft-gellens-format-06.txt>
 */
class TextFlowed : public Text {
public:
  override int ParseBegin();
  override int ParseLine(const char* line, int32_t length);
  override int ParseEOF(bool abort_p);

protected:
  bool            delSp;                // DelSp=yes (RFC 3676)
  int32_t         mQuotedSizeSetting;   // mail.quoted_size
  int32_t         mQuotedStyleSetting;  // mail.quoted_style
  nsCString       mCitationColor;       // mail.citation_color
  bool            mStripSig;            // mail.strip_sig_on_reply
};

class TextFlowedClass : TextClass {
}

/*
 * Made to contain information to be kept during the whole message parsing.
 */
class TextFlowedExData {
  Part* ownerobj; /* The owner of this struct */
  bool inflow; /* If we currently are in flow */
  bool fixedwidthfont; /* If we output text for fixed width font */
  uint32_t quotelevel; /* How deep is your love, uhr, quotelevel I meen. */
  bool isSig;  // we're currently in a signature
  TextFlowedExData* next;
};

} // namespace mime
} // namespace mozilla
#endif // _MIMETEXTFLOWED_H_
