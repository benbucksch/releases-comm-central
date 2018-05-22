/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEEXTOBJ_H_
#define _MIMEEXTOBJ_H_

#include "mimeleaf.h"

namespace mozilla::mime {

/**
 * The ExternalObject class represents MIME parts which contain data
 * which cannot be displayed inline -- e.g. application/octet-stream,
 * and any other type that is not otherwise specially handled.
 *
 * This is not to be confused with ExternalBody, which is the handler for the
 * message/external-object MIME type only.
 */
public class ExternalObject : public Leaf {
public:
  override int ParseBegin();
  override int ParseBuffer(const char* buf, int32_t size);
  override int ParseDecodedBuffer(const char* buf, int32_t size);
  override int ParseLine(const char* line, int32_t length);
};

class ExternalObjectClass : LeafClass {
  override bool DisplayableInline(Headers* hdrs);
}

} // namespace
#endif // _MIMEEXTOBJ_H_
