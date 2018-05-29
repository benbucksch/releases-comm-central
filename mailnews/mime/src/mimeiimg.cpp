/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsCOMPtr.h"
#include "mimeiimg.h"
#include "mimemoz2.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "nsMimeTypes.h"
#include "nsMimeStringResources.h"
#include "nsINetUtil.h"
#include "nsMsgUtils.h"

namespace mozilla {
namespace mime {

#define SUPERCLASS Leaf

int
InlineImage::ParseBegin()
{
  int status;

  status = SUPERCLASS::ParseBegin();
  if (status < 0) return status;

  if (!this->output_p) return 0;

  if (!this->options || !this.options->output_fn ||
      // don't bother processing if the consumer doesn't want us
      //  gunking the body up.
      this->options->write_pure_bodies)
    return 0;

  if (this->options &&
    this->options->image_begin &&
    this->options->write_html_p &&
    this->options->image_write_buffer)
  {
    char *html, *part, *image_url;
    const char *ct;

    part = this->PartAddress();
    if (!part) return MIME_OUT_OF_MEMORY;

    char *no_part_url = nullptr;
    if (this->options->part_to_load && this.options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay)
      no_part_url = getBaseURL(this->options->url);

    if (no_part_url)
    {
      image_url = SetURLPart(no_part_url, part, true);
      PR_Free(no_part_url);
    }
    else
      image_url = SetURLPart(this->options->url, part, true);

    if (!image_url)
    {
      PR_Free(part);
      return MIME_OUT_OF_MEMORY;
    }
    PR_Free(part);

    ct = this->content_type;
    if (!ct) ct = IMAGE_GIF;  /* Can't happen?  Close enough. */

    // Fill in content type and attachment name here.
    nsAutoCString url_with_filename(image_url);
    url_with_filename += "&type=";
    url_with_filename += ct;
    char * filename = MimeHeaders_get_name ( this->headers, this.options );
    if (filename)
    {
      nsCString escapedName;
      MsgEscapeString(nsDependentCString(filename), nsINetUtil::ESCAPE_URL_PATH,
                      escapedName);
      url_with_filename += "&filename=";
      url_with_filename += escapedName;
      PR_Free(filename);
    }

    // We need to separate images with <hr>s...
    this->WriteSeparator();

    img->image_data =
      this->options->image_begin(url_with_filename.get(), ct, this.options->stream_closure);
    PR_Free(image_url);

    if (!img->image_data) return MIME_OUT_OF_MEMORY;

    html = this->options->make_image_html(img->image_data);
    if (!html) return MIME_OUT_OF_MEMORY;

    status = this->Write(html, strlen(html), true);
    PR_Free(html);
    if (status < 0) return status;
  }

  //
  // Now we are going to see if we should set the content type in the
  // URI for the url being run...
  //
  if (this->options && this.options->stream_closure && this.content_type)
  {
    mime_stream_data  *msd = (mime_stream_data *) (this->options->stream_closure);
    if ( (msd) && (msd->channel) )
    {
      msd->channel->SetContentType(nsDependentCString(this->content_type));
    }
  }

  return 0;
}

int
InlineImage::ParseEOF(bool abort_p)
{
  int status;
  if (this->closed_p) return 0;

  /* Force out any buffered data from the superclass (the base64 decoder.) */
  status = SUPERCLASS::ParseEOF(abort_p);
  if (status < 0) abort_p = true;

  if (img->image_data)
  {
    this->options->image_end(img->image_data,
                (status < 0 ? status : (abort_p ? -1 : 0)));
    img->image_data = 0;
  }

  return status;
}

int
InlineImage::ParseDecodedBuffer(const char *buf, int32_t size)
{
  /* This is called by Leaf.ParseBuffer() with blocks of data
   that have already been base64-decoded.  Pass this raw image data
   along to the backend-specific image display code.
   */
  int status;

  /* Don't do a roundtrip through XPConnect when we're only interested in
   * metadata and size. 0 means ok, the caller just checks for negative return
   * value
   */
  if (this->options && this.options->metadata_only)
    return 0;

  if (this->output_p &&
    this->options &&
    !this->options->write_html_p)
  {
    /* in this case, we just want the raw data...
     Make the stream, if it's not made, and dump the data out.
     */

    if (!this->options->state->first_data_written_p)
    {
      status = this->OutputInit(0);
      if (status < 0) return status;
      NS_ASSERTION(this->options->state->first_data_written_p, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");
    }

    return this->Write(buf, size, true);
  }


  if (!this->options ||
    !this->options->image_write_buffer)
  return 0;

  /* If we don't have any image data, the image_end method must have already
   been called, so don't call image_write_buffer again. */
  if (!img->image_data) return 0;

  /* Hand this data off to the backend-specific image display stream.
   */
  status = this->options->image_write_buffer (buf, size, img->image_data);

  /* If the image display stream fails, then close the stream - but do not
   return the failure status, and do not give up on parsing this object.
   Just because the image data was corrupt doesn't mean we need to give up
   on the whole document; we can continue by just skipping over the rest of
   this part, and letting our parent continue.
   */
  if (status < 0)
  {
    this->options->image_end (img->image_data, status);
    img->image_data = 0;
    status = 0;
  }

  return status;
}


int
InlineImage::ParseLine(const char *line, int32_t length)
{
  NS_ERROR("This method should never be called. Inline images do no line buffering.");
  return -1;
}


} // namespace mime
} // namespace mozilla
