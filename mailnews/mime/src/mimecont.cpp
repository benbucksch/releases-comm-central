/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "prio.h"
#include "mimecont.h"
#include "nsMimeStringResources.h"

namespace mozilla {
namespace mime {

Container::~Container()
{
  /* Do this first so that children have their ParseEOF methods called
   in forward order (0-N) but are destroyed in backward order (N-0)
   */
  if (!this->closed_p)
    this->ParseEOF(false);
  if (!this->parsed_p)
    this->ParseEnd(false);

  if (this->children)
  {
    for (int i = this->nchildren - 1; i >= 0; i--)
    {
      Part *kid = this->children[i];
      if (kid)
        delete kid;
      this->children[i] = nullptr;
    }
    PR_FREEIF(this->children);
    this->nchildren = 0;
  }
}

int
Container::ParseEOF(bool abort_p)
{
  int status;

  /* We must run all of this object's parent methods first, to get all the
   data flushed down its stream, so that the children's ParseEOF methods
   can access it.  We do not access *this* object again after doing this,
   only its children.
   */
  status = Super::ParseEOF(abort_p);
  if (status < 0) return status;

  if (this->children)
  {
    for (int i = 0; i < this->nchildren; i++)
    {
      Part *kid = this->children[i];
      if (kid && !kid->closed_p)
      {
        int lstatus = kid->ParseEOF(abort_p);
        if (lstatus < 0) return lstatus;
      }
    }
  }
  return 0;
}

int
Container::ParseEnd(bool abort_p)
{
  int status;

  /* We must run all of this object's parent methods first, to get all the
   data flushed down its stream, so that the children's ParseEOF methods
   can access it.  We do not access *this* object again after doing this,
   only its children.
   */
  status = Super::ParseEnd(abort_p);
  if (status < 0) return status;

  if (this->children)
  {
    for (int i = 0; i < this->nchildren; i++)
    {
      Part *kid = this->children[i];
      if (kid && !kid->parsed_p)
      {
        int lstatus = kid->ParseEnd(abort_p);
        if (lstatus < 0) return lstatus;
      }
    }
  }
  return 0;
}

int
Container::AddChild(Part *child)
{
  Part **old_kids, **new_kids;

  NS_ASSERTION(child, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");
  if (!child) return -1;

  old_kids = this->children;
  new_kids = (Part **)PR_MALLOC(sizeof(Part*) * (this->nchildren + 1));
  if (!new_kids) return MIME_OUT_OF_MEMORY;

  if (this->nchildren > 0)
    memcpy(new_kids, old_kids, sizeof(Part*) * this->nchildren);
  new_kids[this->nchildren] = child;
  PR_Free(old_kids);
  this->children = new_kids;
  this->nchildren++;

  child->parent = this;

  /* Copy this object's options into the child. */
  child->options = this->options;

  return 0;
}

bool
Container::DisplayableInline(Headers* hdrs)
{
  return true;
}


#if defined(DEBUG) && defined(XP_UNIX)
int
Container::DebugPrint(PRFileDesc *stream, int32_t depth)
{
  int i;
  char *addr = PartAddress();
  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
  /*
  PR_Write(stream, "<%s %s (%d kid%s) 0x%08X>\n",
      this->clazz->class_name,
      addr ? addr : "???",
      this->nchildren, (this->nchildren == 1 ? "" : "s"),
      (uint32_t) this);
  */
  PR_FREEIF(addr);

/*
  if (this->nchildren > 0)
  fprintf(stream, "\n");
 */

  for (i = 0; i < this->nchildren; i++)
  {
    Part *kid = this->children[i];
    int status = kid->DebugPrint(stream, depth + 1);
    if (status < 0) return status;
  }

/*
  if (this->nchildren > 0)
  fprintf(stream, "\n");
 */

  return 0;
}
#endif

} // namespace mime
} // namespace mozilla
