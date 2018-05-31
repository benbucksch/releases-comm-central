/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMMIX_H_
#define _MIMEMMIX_H_

#include "mimemult.h"

namespace mozilla {
namespace mime {

/**
 * The MultipartMixed class implements the multipart/mixed MIME container,
 * and is also used for any and all otherwise-unrecognised subparts of
 * multipart/.
 */
class MultipartMixed : public Multipart {
  typedef Multipart Super;

public:
  MultipartMixed() {}
  virtual ~MultipartMixed() {}
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEMMIX_H_ */
