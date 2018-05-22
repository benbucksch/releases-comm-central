/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "mimemapl.h"
#include "prmem.h"
#include "plstr.h"
#include "nsMimeStringResources.h"
#include "mimemoz2.h"
#include "nsMimeTypes.h"

namespace mozilla::mime {

#define SUPERCLASS MultipartClass

int
MultipartAppleDouble::ParseBegin()
{
  // This method is identical to ExternalObject.ParseBegin(), which kinda s#$%s...
  int status;
  status = SUPERCLASS::ParseBegin();
  if (status < 0) return status;

  /* If we're writing this object, and we're doing it in raw form, then
   now is the time to inform the backend what the type of this data is.
   */
  if (this.output_p &&
    this.options &&
    !this.options->write_html_p &&
    !this.options->state->first_data_written_p)
  {
    status = this.OutputInit(0);
    if (status < 0) return status;
    NS_ASSERTION(this.options->state->first_data_written_p, "first data not written");
  }

#ifdef XP_MACOSX
  if (this.options && this.options->state)
  {
//  this.options->state->separator_suppressed_p = true;
  goto done;
  }
  /*
   * It would be nice to not showing the resource fork links
   * if we are displaying inline. But, there is no way we could
   * know ahead of time that we could display the data fork and
   * the data fork is always hidden on MAC platform.
   */
#endif
  /* If we're writing this object as HTML, then emit a link for the
   multipart/appledouble part (both links) that looks just like the
   links that MimeExternalObject emits for external leaf parts.
   */
  if (this.options &&
    this.output_p &&
    this.options->write_html_p &&
    this.options->output_fn)
  {
    char *id = 0;
    char *id_url = 0;
    char *id_imap = 0;

    id = this.PartAddress();
    if (! id) return MIME_OUT_OF_MEMORY;
    if (this.options->missing_parts)
    id_imap = this.IMAPPartAddress();

      if (this.options && this.options->url)
    {
      const char *url = this.options->url;
      if (id_imap && id)
      {
      /* if this is an IMAP part. */
      id_url = SetURLIMAPart(url, id_imap, id);
      }
      else
      {
      /* This is just a normal MIME part as usual. */
      id_url = SetURLPart(url, id, true);
      }
      if (!id_url)
      {
        PR_Free(id);
        return MIME_OUT_OF_MEMORY;
      }
    }

/**********************
    if (!strcmp (id, "0"))
    {
      PR_Free(id);
      id = MimeGetStringByID(MIME_MSG_ATTACHMENT);
    }
    else
    {
      const char *p = "Part ";
      char *s = (char *)PR_MALLOC(strlen(p) + strlen(id) + 1);
      if (!s)
      {
        PR_Free(id);
        PR_Free(id_url);
        return MIME_OUT_OF_MEMORY;
      }
      PL_strcpy(s, p);
      PL_strcat(s, id);
      PR_Free(id);
      id = s;
    }

    if (all_headers_p &&
      // Don't bother showing all headers on this part if it's the only
      // part in the message: in that case, we've already shown these
      // headers.
      this.options->state &&
      this.options->state->root == this.parent)
    all_headers_p = false;

    newopt.fancy_headers_p = true;
    newopt.headers = (all_headers_p ? HeadersState::All : HeadersState::Some);

//
RICHIE SHERRY
GOTTA STILL DO THIS FOR QUOTING!
     status = MimeHeaders_write_attachment_box (this.headers, &newopt,
                                                 this.content_type,
                                                 this.encoding,
                                                 id_name? id_name : id, id_url, 0
//
*********************************************************************************/

//  FAIL:
    PR_FREEIF(id);
    PR_FREEIF(id_url);
    PR_FREEIF(id_imap);
    if (status < 0) return status;
  }

#ifdef XP_MACOSX
done:
#endif

  return 0;
}

bool
MultipartAppleDouble::OutputChild(Part* child)
{
  /* If this is the first child, and it's an application/applefile, then
   don't emit a link for it.  (There *should* be only two children, and
   the first one should always be an application/applefile.)
   */

  if (this.nchildren >= 1 && this.children[0] == child && child->content_type &&
      !PL_strcasecmp(child->content_type, APPLICATION_APPLEFILE))
  {
#ifdef XP_MACOSX
    if (this.output_p && this.options && this.options->write_html_p) //output HTML
      return false;
#else
    /* if we are not on a Macintosh, don't emitte the resources fork at all. */
    return false;
#endif
  }

  return true;
}


} // namespace
