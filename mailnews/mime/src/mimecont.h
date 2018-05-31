/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMECONT_H_
#define _MIMECONT_H_

#include "mimeobj.h"

namespace mozilla {
namespace mime {

/**
 * Container is the class for the objects representing all MIME
 * types which can contain other MIME objects within them.
 *
 * The Container destructor will destroy the children as well.
 */
class Container : public Part {
  typedef Part Super;

protected:
  Container()
    : children(nullptr)
    , nchildren(0)
  {}
  virtual ~Container();

public:
  // Part overrides
  virtual int ParseEOF(bool abort_p) override;
  virtual int ParseEnd(bool abort_p) override;
  virtual bool DisplayableInline(Headers* hdrs);
#if defined(DEBUG) && defined(XP_UNIX)
  virtual int DebugPrint(PRFileDesc* stream, int32_t depth) override;
#endif

  /**
   * This method adds the child (any Part) to the parent's list of children.
   */
  virtual int AddChild(Part* child);

private:
  /**
   * An array of contained objects.
   */
  Part **children;
  /**
   * The number of contained objects.
   */
  int32_t nchildren;
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMECONT_H_ */
