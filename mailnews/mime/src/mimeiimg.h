/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEIIMG_H_
#define _MIMEIIMG_H_

#include "mimeleaf.h"

namespace mozilla {
namespace mime {

/** The InlineImage class implements those MIME image types which can be
 * displayed inline.
 */
class InlineImage : public Leaf {
public:
  override int ParseBegin();
  override int ParseDecodedBuffer(const char* buf, int32_t size);
  override int ParseLine(const char *line, int32_t length);
  override int ParseEOF(bool abort_p);

protected:
  /**
   * Opaque data object for the backend-specific inline-image-display code
   * (internal-external-reconnect nastiness).
   */
  void* image_data;
};

class InlineImageClass : LeafClass {
}

} // namespace mime
} // namespace mozilla
#endif // _MIMEIIMG_H_
