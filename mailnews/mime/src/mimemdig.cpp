/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimemdig.h"
#include "prlog.h"
#include "nsMimeTypes.h"

namespace mozilla {
namespace mime {

MultipartDigest::MultipartDigest()
{
  this->default_part_type = MESSAGE_RFC822;
}

} // namespace mime
} // namespace mozilla
