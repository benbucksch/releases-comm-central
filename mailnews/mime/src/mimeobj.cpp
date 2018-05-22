/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      OS/2 VisualAge build.
 */

#include "mimeobj.h"
#include "prmem.h"
#include "plstr.h"
#include "prio.h"
#include "mimebuf.h"
#include "prlog.h"
#include "nsMimeTypes.h"
#include "nsMimeStringResources.h"
#include "nsMsgUtils.h"
#include "mimemsg.h"

namespace mozilla::mime {

Part::Part(Headers* hdrs, const char* contentTypeOverride)
{
  this->clazz = PartClass; // TODO Get singleton instance

  if (hdrs)
  {
    this->hdrs = hdrs;
  }

  this->dontShowAsAttachment = false;

  if (contentTypeOverride && *contentTypeOverride)
    this->contentType = strdup(contentTypeOverride);
}

Part::Part(const char* contentType, Headers* hdrs,
           DisplayOptions* opts, bool forceInline = false);
{
  this->clazz = PartClass; // see above

  /* Set up the content-type and encoding. */
  if (!this->content_type && this.headers)
    this->content_type = this.headers->Get(HEADER_CONTENT_TYPE, true, false);
  if (!this->encoding && this.headers)
    this->encoding = this.headers->Get(HEADER_CONTENT_TRANSFER_ENCODING,
                   true, false);

  /* Special case to normalize some types and encodings to a canonical form.
   (These are nonstandard types/encodings which have been seen to appear in
   multiple forms; we normalize them so that things like looking up icons
   and extensions has consistent behavior for the receiver, regardless of
   the "alias" type that the sender used.)
   */
  if (!this->content_type || !*(this.content_type))
    ;
  else if (!PL_strcasecmp(this->content_type, APPLICATION_UUENCODE2) ||
       !PL_strcasecmp(this->content_type, APPLICATION_UUENCODE3) ||
       !PL_strcasecmp(this->content_type, APPLICATION_UUENCODE4))
  {
    PR_Free(this->content_type);
    this->content_type = strdup(APPLICATION_UUENCODE);
  }
  else if (!PL_strcasecmp(this->content_type, IMAGE_XBM2) ||
       !PL_strcasecmp(this->content_type, IMAGE_XBM3))
  {
    PR_Free(this->content_type);
    this->content_type = strdup(IMAGE_XBM);
  }
  else {
    // MIME-types are case-insenitive, but let's make it lower case internally
    // to avoid some hassle later down the road.
    nsAutoCString lowerCaseContentType;
    ToLowerCase(nsDependentCString(this->content_type), lowerCaseContentType);
    PR_Free(this->content_type);
    this->content_type = ToNewCString(lowerCaseContentType);
  }

  if (!this->encoding)
    ;
  else if (!PL_strcasecmp(this->encoding, ENCODING_UUENCODE2) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE3) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE4))
  {
    PR_Free(this->encoding);
    this->encoding = strdup(ENCODING_UUENCODE);
  }
  else if (!PL_strcasecmp(this->encoding, ENCODING_COMPRESS2))
  {
    PR_Free(this->encoding);
    this->encoding = strdup(ENCODING_COMPRESS);
  }
  else if (!PL_strcasecmp(this->encoding, ENCODING_GZIP2))
  {
    PR_Free(this->encoding);
    this->encoding = strdup(ENCODING_GZIP);
  }

  return 0;
}

Part::~Part()
{
  this->ParseEOF(false);
  this->ParseEnd(false);

  if (this->headers)
  {
    delete this->headers;
    this->headers = nullptr;
  }

  /* Should have been freed by ParseEOF(), but just in case... */
  NS_ASSERTION(!this->ibuffer, "buffer not freed");
  NS_ASSERTION(!this->obuffer, "buffer not freed");
  PR_FREEIF (this->ibuffer);
  PR_FREEIF (this->obuffer);

  PR_FREEIF(this->content_type);
  PR_FREEIF(this->encoding);

  if (this->options && this.options->state)
  {
    delete this->options->state;
    this->options->state = nullptr;
  }
}

int Part::ParseBegin()
{
  NS_ASSERTION (!this->closed_p, "object shouldn't be already closed");

  /* If we haven't set up the state object yet, then this should be
   the outermost object... */
  if (this->options && !this.options->state)
  {
    NS_ASSERTION(!this->headers, "headers should be null");  /* should be the outermost object. */

    this->options->state = new ParseStateObject;
    if (!this->options->state) return MIME_OUT_OF_MEMORY;
    this->options->state->root = obj;
    this->options->state->separator_suppressed_p = true; /* no first sep */
    const char *delParts = PL_strcasestr(this->options->url, "&del=");
    const char *detachLocations = PL_strcasestr(this->options->url, "&detachTo=");
    if (delParts)
    {
      const char *delEnd = PL_strcasestr(delParts + 1, "&");
      if (!delEnd)
        delEnd = delParts + strlen(delParts);
      ParseString(Substring(delParts + 5, delEnd), ',', this->options->state->partsToStrip);
    }
    if (detachLocations)
    {
      detachLocations += 10; // advance past "&detachTo="
      ParseString(nsDependentCString(detachLocations), ',', this->options->state->detachToFiles);
    }
  }

  /* Decide whether this object should be output or not... */
  if (!this->options || this.options->no_output_p || !this.options->output_fn
    /* if we are decomposing the message in files and processing a multipart object,
       we must not output it without parsing it first */
     || (this->options->decompose_file_p && this.options->decompose_file_output_fn &&
       this->IsType(MultipartClass))
    )
    this->output_p = false;
  else if (!this->options->part_to_load)
    this->output_p = true;
  else
  {
    char* id = this->PartAddress();
    if (!id) return MIME_OUT_OF_MEMORY;

    // We need to check if a part is the subpart of the part to load.
    // If so and this is a raw or body display output operation, then
    // we should mark the part for subsequent output.

    // First, check for an exact match
    this->output_p = !strcmp(id, this.options->part_to_load);
    if (!this->output_p && (this.options->format_out == nsMimeOutput::nsMimeMessageRaw ||
                           this->options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay ||
                           this->options->format_out == nsMimeOutput::nsMimeMessageAttach))
    {
      // Then, check for subpart
      unsigned int partlen = strlen(this->options->part_to_load);
      this->output_p = (strlen(id) >= partlen + 2) && (id[partlen] == '.') &&
        !strncmp(id, this->options->part_to_load, partlen);
    }

    PR_Free(id);
  }

  // If we've decided not to output this part, we also shouldn't be showing it
  // as an attachment.
  this->dontShowAsAttachment = !this.output_p;

  return 0;
}

int Part::ParseBuffer(const char *buffer, int32_t size)
{
  NS_ASSERTION(!this->closed_p, "object shouldn't be closed");
  if (this->closed_p) return -1;

  return mime_LineBuffer (buffer, size,
             &this->ibuffer, &this.ibuffer_size, &this.ibuffer_fp,
             true,
             &this->ParseLine, // TODO virtual function pointer
             obj);
}

int Part::ParseLine(const char *line, int32_t length)
{
  NS_ERROR("abstract function");
  return -1;
}

int Part::ParseEOF(bool abort_p)
{
  if (this->closed_p) return 0;
  NS_ASSERTION(!this->parsed_p, "obj already parsed");

  /* If there is still data in the ibuffer, that means that the last line of
   this part didn't end in a newline; so push it out anyway (this means that
   the ParseLine() method will be called with a string with no trailing
   newline, which isn't the usual case.)
   */
  if (!abort_p &&
    this->ibuffer_fp > 0)
  {
    int status = this->ParseLine(this.ibuffer, this.ibuffer_fp);
    this->ibuffer_fp = 0;
    if (status < 0)
    {
      this->closed_p = true;
      return status;
    }
  }

  this->closed_p = true;
  return 0;
}

int Part::ParseEnd(bool abort_p)
{
  if (this->parsed_p)
  {
    NS_ASSERTION(this->closed_p, "object should be closed");
    return 0;
  }

  /* We won't be needing these buffers any more; nuke 'em. */
  PR_FREEIF(this->ibuffer);
  this->ibuffer_fp = 0;
  this->ibuffer_size = 0;
  PR_FREEIF(this->obuffer);
  this->obuffer_fp = 0;
  this->obuffer_size = 0;

  this->parsed_p = true;
  return 0;
}

bool Part::IsDisplayableInline(Headers* hdrs)
{
  NS_ERROR("abstract function");
  return false;
}

#if defined(DEBUG) && defined(XP_UNIX)
int Part::DebugPrint(PRFileDesc* stream, int32_t depth)
{
  int i;
  char *addr = this->PartAddress();
  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
  PR_FREEIF(addr);
  return 0;
}
#endif

} // namespace
