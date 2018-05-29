/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHTML_H_
#define _MIMEHTML_H_

#include "mimetext.h"

namespace mozilla {
namespace mime {

/**
 * The HTML class implements the text/html MIME content type.
 */
class HTML : public Text{
public:
  override int ParseBegin();
  override int ParseLine(const char* line, int32_t length);
  override int ParseEOF(bool abort_p);

protected:
  /**
   * If we found a charset, do some converting
   */
  char* charset;
};

class HTMLClass : TextClass {
}

} // namespace mime
} // namespace mozilla
#endif // _MIMEHTML_H_
