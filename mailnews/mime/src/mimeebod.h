/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEEXTBOD_H_
#define _MIMEEXTBOD_H_

#include "mimeobj.h"

namespace mozilla::mime {

/**
 * The ExternalBody class implements the message/external-body MIME type
 * only.
 *
 * This is not to be confused with ExternalObject, which implements the
 * handler for application/octet-stream and other types with no more specific
 * handlers.
 */
class ExternalBody : public Part {
public:
  override int ParseBegin();
  override int ParseBuffer(const char* buf, int32_t size);
  override int ParseDecodedBuffer(const char* buf, int32_t size);
  override int ParseLine(const char* line, int32_t length);
  override int ParseEOF(bool abort_p);
  override int ParseEnd(bool abort_p);

public:
  /**
   * Headers within this external-body, which describe the
   * network data which this body is a pointer to.
   */
  Headers* hdrs;

  /**
   * The "phantom body" of this link.
   */
  char* body;
};

class ExternalBodyClass : PartClass {
  override bool DisplayableInline(Headers* hdrs);
}

} // namespace
#endif // _MIMEEXTBOD_H_
