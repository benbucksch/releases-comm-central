/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMPAR_H_
#define _MIMEMPAR_H_

#include "mimemult.h"

namespace mozilla {
namespace mime {

/**
 * The MultipartParallel class implements the multipart/parallel MIME
 * container, which is currently no different from multipart/mixed, since
 * it's not clear that there's anything useful it could do differently.
 */
class MultipartParallel : public Multipart {
  typedef Multipart Super;

public:
  MultipartParallel() {}
  virtual ~MultipartParallel() {}
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEMPAR_H_ */
