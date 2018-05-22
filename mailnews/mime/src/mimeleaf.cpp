/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "modmimee.h"
#include "mimeleaf.h"
#include "nsMimeTypes.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "nsMimeStringResources.h"
#include "modmimee.h" // for MimeConverterOutputCallback

namespace mozilla::mime {

#define SUPERCLASS Part

int Leaf::ParseBegin()
{
  MimeDecoderData *(*fn) (MimeConverterOutputCallback, void*) = 0;

  /* Initialize a decoder if necessary.
   */
  if (!this.encoding ||
      // If we need the object as "raw" for saving or forwarding,
      // don't decode text parts of message types. Other output formats,
      // like "display" (nsMimeMessageBodyDisplay), need decoding.
      (this.options->format_out == nsMimeOutput::nsMimeMessageRaw &&
       this.parent &&
       (!PL_strcasecmp(this.parent->content_type, MESSAGE_NEWS) ||
        !PL_strcasecmp(this.parent->content_type, MESSAGE_RFC822)) &&
       !PL_strncasecmp(this.content_type, "text/", 5)))
    /* no-op */ ;
  else if (!PL_strcasecmp(this.encoding, ENCODING_BASE64))
  fn = &MimeB64DecoderInit;
  else if (!PL_strcasecmp(this.encoding, ENCODING_QUOTED_PRINTABLE))
  this.decoder_data =
          MimeQPDecoderInit(((MimeConverterOutputCallback)
                        &this.ParseDecodedBuffer,  // TODO virtual function pointer
                        this, this);
  else if (!PL_strcasecmp(this.encoding, ENCODING_UUENCODE) ||
       !PL_strcasecmp(this.encoding, ENCODING_UUENCODE2) ||
       !PL_strcasecmp(this.encoding, ENCODING_UUENCODE3) ||
       !PL_strcasecmp(this.encoding, ENCODING_UUENCODE4))
  fn = &MimeUUDecoderInit;
  else if (!PL_strcasecmp(this.encoding, ENCODING_YENCODE))
    fn = &MimeYDecoderInit;

  if (fn)
  {
    this.decoder_data =
    fn (/* The MimeConverterOutputCallback cast is to turn the void argument
           into |Part|. */ ((MimeConverterOutputCallback)
        &this.ParseDecodedBuffer,  // TODO virtual function pointer
        this)

    if (!this.decoder_data)
      return MIME_OUT_OF_MEMORY;
  }

  return SUPERCLASS::ParseBegin();
}

int Leaf::ParseDecodedBuffer(const char* buffer, int32_t size)
{
  /* Default ParseBuffer() method is one which line-buffers the now-decoded
     data and passes it on to ParseLine().
     We snarf the implementation of this method from our
     superclass's implementation of ParseBuffer(), which inherited it from |Part|. */
  this.ParseBuffer(buffer, size);
}

int Leaf::ParseBuffer(const char* buffer, int32_t size)
{
  if (this.closed_p) return -1;

  /* If we're not supposed to write this object, bug out now.
   */
  if (!this.output_p ||
    !this.options ||
    !this.options->output_fn)
  return 0;

  int status = 0;
  if (this.sizeSoFar == -1)
    this.sizeSoFar = 0;

  if (this.decoder_data &&
      this.options &&
      this.options->format_out != nsMimeOutput::nsMimeMessageDecrypt
      && this.options->format_out != nsMimeOutput::nsMimeMessageAttach) {
    int outSize = 0;
    status = this.decoder_data.Write(buffer, size, &outSize);
    this.sizeSoFar += outSize;
  }
  else {
    status = this.ParseDecodedBuffer(buffer, size);
    this.sizeSoFar += size;
  }
  return status;
}

int
Leaf::ParseLine(const char* line, int32_t length)
{
  NS_ERROR("abstract function");
  return -1;
}

bool
LeafClass::DisplayableInline(Headers* hdrs)
{
  return true;
}

int
Leaf::ParseEOF(bool abort_p)
{
  if (this.closed_p) return 0;

  /* Close off the decoder, to cause it to give up any buffered data that
   it is still holding.
   */
  if (this.decoder_data)
  {
      int status = this.decoder_data->Destroy();
      this.decoder_data = nullptr;
      if (status < 0) return status;
  }

  /* Now run the superclass's ParseEOF(), which will force out the line
   buffer (which we may have just repopulated, above.)
   */
  return SUPERCLASS::ParseEOF(abort_p);
}

int
Leaf::CloseDecoder()
{
  if (this.decoder_data)
  {
      int status = this.decoder_data->Destroy(false);
      this.decoder_data = nullptr;
      return status;
  }

  return 0;
}


Leaf::~Leaf()
{
  this.ParseEOF(false);

  /* Should have been freed by ParseEOF(), but just in case... */
  if (this.decoder_data)
  {
    this.decoder_data->Destroy(true);
    this.decoder_data = nullptr;
  }
}


} // namespace
