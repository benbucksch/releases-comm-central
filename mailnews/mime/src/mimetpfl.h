/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMETPFL_H_
#define _MIMETPFL_H_

#include "mimetext.h"

namespace mozilla::mime {

/**
 * The TextPlainFlowed class implements the text/plain MIME content type
 * for the special case of the subtype format=flowed.
 * @see RFC 2646
 * @see <https://joeclark.org/ffaq.html>
 * This implementation was based on the earlier draft
 * <ftp://ftp.ietf.org/internet-drafts/draft-gellens-format-06.txt>
 */
class TextPlainFlowed : Text {
public:
  TextPlainFlowed();
  virtual ~TextPlainFlowed();

  bool            delSp;                // DelSp=yes (RFC 3676)
  int32_t         mQuotedSizeSetting;   // mail.quoted_size
  int32_t         mQuotedStyleSetting;  // mail.quoted_style
  nsCString       mCitationColor;       // mail.citation_color
  bool            mStripSig;            // mail.strip_sig_on_reply
};

class TextPlainFlowedClass : TextClass {
}

/*
 * Made to contain information to be kept during the whole message parsing.
 */
class TextPlainFlowedExData {
  struct Part* ownerobj; /* The owner of this struct */
  bool inflow; /* If we currently are in flow */
  bool fixedwidthfont; /* If we output text for fixed width font */
  uint32_t quotelevel; /* How deep is your love, uhr, quotelevel I meen. */
  bool isSig;  // we're currently in a signature
  TextPlainFlowedExData* next;
};

} // namespace
#endif // _MIMETPFL_H_
