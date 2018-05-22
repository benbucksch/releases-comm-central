/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHTMLSANITIZED_H_
#define _MIMEHTMLSANITIZED_H_

#include "mimethtm.h"
#include "nsString.h"

namespace mozilla::mime {

/**
 * The MimeInlineTextHTMLSanitized class cleans up HTML
 *
 * This removes offending HTML features that have no business in mail.
 * It is a low-level stop gap for many classes of attacks,
 * and intended for security conscious users.
 * Paranoia is a feature here, and has served very well in practice.
 *
 * It has already prevented countless serious exploits.
 *
 * It pushes the HTML that we get from the sender of the message
 * through a sanitizer (nsTreeSanitizer), which lets only allowed tags through.
 * With the appropriate configuration, this protects from most of the
 * security and visual-formatting problems that otherwise usually come with HTML
 * (and which partly gave HTML in email the bad reputation that it has).
 *
 * However, due to the parsing and serializing (and later parsing again)
 * required, there is an inherent, significant performance hit, when doing the
 * santinizing here at the MIME / HTML source level. But users of this class
 * will most likely find it worth the cost.
 */
class HTMLSanitized : public HTML {
public:
  ~HTMLSanitized();
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

class HTMLSanitizedClass : HTMLClass {
}

#endif // _MIMEHTMLSANITIZED_H_
