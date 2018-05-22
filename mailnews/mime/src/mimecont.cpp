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

#define MIME_SUPERCLASS mimeObjectClass
MimeDefClass(MimeContainer, MimeContainerClass,
       mimeContainerClass, &MIME_SUPERCLASS);

static int MimeContainer_initialize (Part *);
static void MimeContainer_finalize (Part *);
static int MimeContainer_add_child (Part *, MimeObject *);
static int MimeContainer_ParseEOF (Part *, bool);
static int MimeContainer_ParseEnd (Part *, bool);
static bool MimeContainer_displayable_inline_p (PartClass *clazz,
                           MimeHeaders *hdrs);

#if defined(DEBUG) && defined(XP_UNIX)
static int MimeContainer_debug_print (Part *, PRFileDesc *, int32_t depth);
#endif

static int
MimeContainerClassInitialize(MimeContainerClass *clazz)
{
  PartClass *oclass = (MimeObjectClass *) &clazz->object;

  NS_ASSERTION(!oclass->class_initialized, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");
  oclass->initialize  = MimeContainer_initialize;
  oclass->finalize    = MimeContainer_finalize;
  oclass->ParseEOF   = MimeContainer_parse_eof;
  oclass->ParseEnd   = MimeContainer_parse_end;
  oclass->displayable_inline_p = MimeContainer_displayable_inline_p;
  clazz->add_child    = MimeContainer_add_child;

#if defined(DEBUG) && defined(XP_UNIX)
  oclass->debug_print = MimeContainer_debug_print;
#endif
  return 0;
}


static int
MimeContainer_initialize (Part *object)
{
  /* This is an abstract class; it shouldn't be directly instantiated. */
  NS_ASSERTION(object->clazz != (PartClass *) &mimeContainerClass, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");

  return ((PartClass*)&MIME_SUPERCLASS)->initialize(object);
}

static void
MimeContainer_finalize (Part *object)
{
  MimeContainer *cont = (MimeContainer *) object;

  /* Do this first so that children have their ParseEOF methods called
   in forward order (0-N) but are destroyed in backward order (N-0)
   */
  if (!object->closed_p)
  object->clazz->ParseEOF (object, false);
  if (!object->parsed_p)
  object->clazz->ParseEnd (object, false);

  if (cont->children)
  {
    int i;
    for (i = cont->nchildren-1; i >= 0; i--)
    {
      Part *kid = cont->children[i];
      if (kid)
      mime_free(kid);
      cont->children[i] = 0;
    }
    PR_FREEIF(cont->children);
    cont->nchildren = 0;
  }
  ((PartClass*)&MIME_SUPERCLASS)->finalize(object);
}

static int
MimeContainer_ParseEOF (Part *object, bool abort_p)
{
  MimeContainer *cont = (MimeContainer *) object;
  int status;

  /* We must run all of this object's parent methods first, to get all the
   data flushed down its stream, so that the children's ParseEOF methods
   can access it.  We do not access *this* object again after doing this,
   only its children.
   */
  status = ((PartClass*)&MIME_SUPERCLASS)->ParseEOF(object, abort_p);
  if (status < 0) return status;

  if (cont->children)
  {
    int i;
    for (i = 0; i < cont->nchildren; i++)
    {
      Part *kid = cont->children[i];
      if (kid && !kid->closed_p)
      {
        int lstatus = kid->clazz->ParseEOF(kid, abort_p);
        if (lstatus < 0) return lstatus;
      }
    }
  }
  return 0;
}

static int
MimeContainer_ParseEnd (Part *object, bool abort_p)
{
  MimeContainer *cont = (MimeContainer *) object;
  int status;

  /* We must run all of this object's parent methods first, to get all the
   data flushed down its stream, so that the children's ParseEOF methods
   can access it.  We do not access *this* object again after doing this,
   only its children.
   */
  status = ((PartClass*)&MIME_SUPERCLASS)->ParseEnd(object, abort_p);
  if (status < 0) return status;

  if (cont->children)
  {
    int i;
    for (i = 0; i < cont->nchildren; i++)
    {
      Part *kid = cont->children[i];
      if (kid && !kid->parsed_p)
      {
        int lstatus = kid->clazz->ParseEnd(kid, abort_p);
        if (lstatus < 0) return lstatus;
      }
    }
  }
  return 0;
}

static int
MimeContainer_add_child (Part *parent, MimeObject *child)
{
  MimeContainer *cont = (MimeContainer *) parent;
  Part **old_kids, **new_kids;

  NS_ASSERTION(parent && child, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");
  if (!parent || !child) return -1;

  old_kids = cont->children;
  new_kids = (Part **)PR_MALLOC(sizeof(MimeObject *) * (cont->nchildren + 1));
  if (!new_kids) return MIME_OUT_OF_MEMORY;

  if (cont->nchildren > 0)
    memcpy(new_kids, old_kids, sizeof(Part *) * cont->nchildren);
  new_kids[cont->nchildren] = child;
  PR_Free(old_kids);
  cont->children = new_kids;
  cont->nchildren++;

  child->parent = parent;

  /* Copy this object's options into the child. */
  child->options = parent->options;

  return 0;
}

static bool
MimeContainer_displayable_inline_p (PartClass *clazz, MimeHeaders *hdrs)
{
  return true;
}


#if defined(DEBUG) && defined(XP_UNIX)
static int
MimeContainer_debug_print (Part *obj, PRFileDesc *stream, int32_t depth)
{
  MimeContainer *cont = (MimeContainer *) obj;
  int i;
  char *addr = mime_part_address(obj);
  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
  /*
  PR_Write(stream, "<%s %s (%d kid%s) 0x%08X>\n",
      obj->clazz->class_name,
      addr ? addr : "???",
      cont->nchildren, (cont->nchildren == 1 ? "" : "s"),
      (uint32_t) cont);
  */
  PR_FREEIF(addr);

/*
  if (cont->nchildren > 0)
  fprintf(stream, "\n");
 */

  for (i = 0; i < cont->nchildren; i++)
  {
    Part *kid = cont->children[i];
    int status = kid->clazz->debug_print (kid, stream, depth+1);
    if (status < 0) return status;
  }

/*
  if (cont->nchildren > 0)
  fprintf(stream, "\n");
 */

  return 0;
}
#endif
