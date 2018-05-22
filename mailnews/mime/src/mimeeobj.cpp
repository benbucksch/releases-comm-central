/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "mimeeobj.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "nsMimeStringResources.h"
#include "mimemoz2.h"
#include "mimemapl.h"
#include "nsMimeTypes.h"


namespace mozilla::mime {

#define SUPERCLASS Leaf

int
ExternalObject::ParseBegin()
{
  int status;
  status = SUPERCLASS::ParseBegin();
  if (status < 0) return status;

  // If we're writing this object, and we're doing it in raw form, then
  // now is the time to inform the backend what the type of this data is.
  if (this->output_p &&
    this->options &&
    !this->options->write_html_p &&
    !this->options->state->first_data_written_p)
  {
    status = this->OutputInit(0);
    if (status < 0) return status;
    NS_ASSERTION(this->options->state->first_data_written_p);
  }

  //
  // If we're writing this object as HTML, do all the work now -- just write
  // out a table with a link in it.  (Later calls to the `ParseBuffer' method
  // will simply discard the data of the object itself.)
  //
  if (this->options &&
      this->output_p &&
      this->options->write_html_p &&
      this->options->output_fn)
  {
    DisplayOptions newopt = *this->options;  // copy it
    char *id = 0;
    char *id_url = 0;
    char *id_name = 0;
    nsCString id_imap;
    bool all_headers_p = this->options->headers == HeadersState::All;

    id = this->PartAddress;
    if (this->options->missing_parts)
      id_imap.Adopt(this->IMAPPartAddress());
    if (! id) return MIME_OUT_OF_MEMORY;

    if (this->options && this.options->url)
    {
      const char *url = this->options->url;
      if (!id_imap.IsEmpty() && id)
      {
        // if this is an IMAP part.
        id_url = SetURLIMAPPart(url, id_imap.get(), id);
      }
      else
      {
        // This is just a normal MIME part as usual.
        id_url = SetURLPart(url, id, true);
      }
      if (!id_url)
      {
        PR_Free(id);
        return MIME_OUT_OF_MEMORY;
      }
    }
    if (!strcmp (id, "0"))
    {
      PR_Free(id);
      id = MimeGetStringByID(MIME_MSG_ATTACHMENT);
    }
    else
    {
      const char *p = "Part ";
      uint32_t slen = strlen(p) + strlen(id) + 1;
      char *s = (char *)PR_MALLOC(slen);
      if (!s)
      {
        PR_Free(id);
        PR_Free(id_url);
        return MIME_OUT_OF_MEMORY;
      }
      // we have a valid id
      if (id)
        id_name = this->GetSuggestedNameOfPart(id);
      PL_strncpyz(s, p, slen);
      PL_strcatn(s, slen, id);
      PR_Free(id);
      id = s;
    }

    if (all_headers_p &&
    // Don't bother showing all headers on this part if it's the only
    // part in the message: in that case, we've already shown these
    // headers.
    this->options->state &&
    this->options->state->root == this.parent)
    all_headers_p = false;

    newopt.fancy_headers_p = true;
    newopt.headers = (all_headers_p ? HeadersState::All : HeadersState::Some);

/******
RICHIE SHERRY
GOTTA STILL DO THIS FOR QUOTING!
     status = MimeHeaders_write_attachment_box (this->headers, &newopt,
                                                 this->content_type,
                                                 this->encoding,
                                                 id_name? id_name : id, id_url, 0)
*****/

    // this->options really owns the storage for this.
    newopt.part_to_load = nullptr;
    newopt.default_charset = nullptr;
    PR_FREEIF(id);
    PR_FREEIF(id_url);
    PR_FREEIF(id_name);
    if (status < 0) return status;
  }

  return 0;
}

int
ExternalObject::ParseBuffer(const char *buffer, int32_t size)
{
  NS_ASSERTION(!this->closed_p);
  if (this->closed_p) return -1;

  // Currently, we always want to stream, in order to determine the size of the
  // MIME object.

  /* The data will be base64-decoded and passed to
     ExternalObject.ParseDecodedBuffer(). */
  return SUPERCLASS::ParseBuffer(buffer, size);
}


int
ExternalObject::ParseDecodedBuffer(const char *buf, int32_t size)
{
  /* This is called by Leaf.ParseBuffer() with blocks of data
   that have already been base64-decoded.  This will only be called in
   the case where we're not emitting HTML, and want access to the raw
   data itself.

   We override the Leaf.ParseDecodedBuffer() method, because,
   unlike most children of |Leaf|, we do not want to line-buffer
   the decoded data -- we want to simply pass it along to the backend,
   without going through our ParseLine() method.
   */

  /* Don't do a roundtrip through XPConnect when we're only interested in
   * metadata and size. This includes when we are writing HTML (otherwise, the
   * contents of binary attachments will just get dumped into messages when
   * reading them) and the JS emitter (which doesn't care about attachment data
   * at all). 0 means ok, the caller just checks for negative return value.
   */
  if (this->options && (this.options->metadata_only ||
                       this->options->write_html_p))
    return 0;
  else
    return this->Write(buf, size, true);
}


int
ExternalObject::ParseLine(const char *line, int32_t length)
{
  NS_ERROR("This method should never be called (externals do no line buffering).");
  return -1;
}

bool
ExternalObjectClass::DisplayableInline(Headers* hdrs)
{
  return false;
}


} // namespace
