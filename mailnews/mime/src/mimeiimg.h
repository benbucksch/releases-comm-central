/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEIIMG_H_
#define _MIMEIIMG_H_

#include "mimeleaf.h"

namespace MIME {

/** The MimeInlineImage class implements those MIME image types which can be
 * displayed inline.
 */
public class InlineImage : Leaf {
public:
  /**
   * Opaque data object for the backend-specific inline-image-display code
   * (internal-external-reconnect nastiness).
   */
  void *image_data;
};

class InlineImageClass : LeafClass {
}

} // namespace
#endif // _MIMEIIMG_H_
