/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMAPL_H_
#define _MIMEMAPL_H_

#include "mimemult.h"

namespace mozilla::mime {

/**
 * The MimeMultipartAppleDouble class implements the multipart/appledouble
 * MIME container, which provides a method of encapsulating and reconstructing
 * a two-forked Macintosh file.
 */
class MultipartAppleDouble : public Multipart {
public:
  override int ParseBegin();
  override bool OutputChild(Part *child);
};

class MultipartAppleDoubleClass : MultipartClass {
}

} // namespace
#endif // _MIMEMAPL_H_
