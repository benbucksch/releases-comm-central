/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimecryp.h"
#include "mimemoz2.h"
#include "nsMimeTypes.h"
#include "nsMimeStringResources.h"
#include "mimebuf.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "mimemult.h"
#include "modmimee.h" // for MimeConverterOutputCallback

namespace mozilla {
namespace mime {

int
Encrypted::ParseBegin()
{
  Decoder::Encoding which = Decoder::Encoding::None;

  if (this->crypto_closure)
  return -1;

  this->crypto_closure = CryptoInit(HandleDecryptedOutputClosure, this);
  if (!this->crypto_closure)
  return -1;


  /* (Mostly duplicated from Leaf, see comments in mimecryp.h.)
     Initialize a decoder if necessary.
   */
  if (!this->encoding)
  ;
  else if (!PL_strcasecmp(this->encoding, ENCODING_BASE64))
    which = Decoder::Encoding::Base64;
  else if (!PL_strcasecmp(this->encoding, ENCODING_QUOTED_PRINTABLE))
    which = Decoder::Encoding::QuotedPrintable;
  else if (!PL_strcasecmp(this->encoding, ENCODING_UUENCODE) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE2) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE3) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE4))
    which = Decoder::Encoding::UUEncode;
  else if (!PL_strcasecmp(this->encoding, ENCODING_YENCODE))
    which = Decoder::Encoding::YEncode;
  if (which != Decoder::Encoding::None)
  {
    this->decoder_data = new Decoder(which, ParseDecodedBuffer, this, this);

    if (!this->decoder_data)
    return MIME_OUT_OF_MEMORY;
  }

  return Super::ParseBegin();
}

int
Encrypted::ParseBuffer(const char* buffer, int32_t size)
{
  /* (Duplicated from Leaf, see comments in mimecryp.h.)
   */

  if (this->closed_p) return -1;

  /* Don't consult output_p here, since at this point we're behaving as a
   simple container object -- the output_p decision should be made by
   the child of this object. */

  if (this->decoder_data)
    return this->decoder_data->Write(buffer, size, nullptr);
  else
  return ParseDecodedBuffer(buffer, size, this);
}

int
Encrypted::ParseDecodedBuffer(const char* buffer, int32_t size, void* obj)
{
  Encrypted* enc = (Encrypted*)obj;
  return enc->CryptoWrite(buffer, size, enc->crypto_closure);
}

int
Encrypted::ParseEOF(bool abort_p)
{
  int status = 0;

  if (this->closed_p) return 0;
  NS_ASSERTION(!this->parsed_p, "1.2 <mscott@netscape.com> 01 Nov 2001 17:59");

  /* (Duplicated from Leaf, see comments in mimecryp.h.)
     Close off the decoder, to cause it to give up any buffered data that
   it is still holding.
   */
  if (this->decoder_data)
  {
    int status = this->decoder_data->Flush();
    delete this->decoder_data;
    this->decoder_data = 0;
    if (status < 0) return status;
  }


  /* If there is still data in the ibuffer, that means that the last
   *decrypted* line of this part didn't end in a newline; so push it out
   anyway (this means that the ParseLine method will be called with a
   string with no trailing newline, which isn't the usual case.)  */
  if (!abort_p &&
    this->ibuffer_fp > 0)
  {
    int status = HandleDecryptedOutputLine(this->ibuffer,
                          this->ibuffer_fp);
    this->ibuffer_fp = 0;
    if (status < 0)
    {
      this->closed_p = true;
      return status;
    }
  }


  /* Now run the superclass's ParseEOF, which (because we've already taken
   care of ibuffer in a way appropriate for this class, immediately above)
   will ony set closed_p to true.
   */
  status = Super::ParseEOF(abort_p);
  if (status < 0) return status;


  /* Now close off the underlying crypto module.  At this point, the crypto
   module has all of the input.  (DecoderDestroy called ParseDecodedBuffer
   which called CryptoWrite, with the last of the data.)
   */
  if (this->crypto_closure)
  {
    status = CryptoEOF(this->crypto_closure, abort_p);
    if (status < 0 && !abort_p)
    return status;
  }

  /* Now we have the entire child part in the part buffer.
   We are now able to verify its signature, emit a blurb, and then
   emit the part.
   */
  if (abort_p)
  return 0;
  else
  return EmitBufferedChild();
}

void
Encrypted::Cleanup(bool finalizing_p)
{
  if (this->part_buffer)
  {
    MimePartBufferDestroy(this->part_buffer);
    this->part_buffer = 0;
  }

  if (finalizing_p && this->crypto_closure)
  {
    /* Don't free these until this object is really going away -- keep them
     around for the lifetime of the MIME object, so that we can get at the
     security info of sub-parts of the currently-displayed message. */
    CryptoFree(this->crypto_closure);
    this->crypto_closure = 0;
  }

  /* (Duplicated from Leaf, see comments in mimecryp.h.)
   Free the decoder data, if it's still around. */
  if (this->decoder_data)
  {
    delete this->decoder_data;
    this->decoder_data = 0;
  }

  if (this->hdrs)
  {
    delete this->hdrs;
    this->hdrs = 0;
  }
}


Encrypted::~Encrypted()
{
  Cleanup(true);
}


int
Encrypted::HandleDecryptedOutputClosure(const char* buf, int32_t buf_size,
               void *output_closure)
{
  return ((Encrypted*)output_closure)->HandleDecryptedOutput(buf, buf_size);
}

int
Encrypted::HandleDecryptedOutput(const char* buf, int32_t buf_size)
{
  /* This method is invoked by the underlying decryption module.
   The module is assumed to return a MIME object, and its associated
   headers.  For example, if a text/plain document was encrypted,
   the encryption module would return the following data:

     Content-Type: text/plain

     Decrypted text goes here.

   This function will then extract a header block (up to the first
   blank line, as usual) and will then handle the included data as
   appropriate.
   */

  /* Is it truly safe to use ibuffer here?  I think so... */
  return mime_LineBuffer (buf, buf_size,
             &this->ibuffer, &this->ibuffer_size, &this->ibuffer_fp,
             true, HandleDecryptedOutputLineClosure, this);
}

int
Encrypted::HandleDecryptedOutputLineClosure(char* line, int32_t length, void* obj)
{
  return ((Encrypted*)obj)->HandleDecryptedOutputLine(line, length);
}

int
Encrypted::HandleDecryptedOutputLine(char* line, int32_t length)
{
  /* Largely the same as Message::ParseLine (the other MIME container
   type which contains exactly one child.)
   */
  int status = 0;

  if (!line || !*line) return -1;

  /* If we're supposed to write this object, but aren't supposed to convert
   it to HTML, simply pass it through unaltered. */
  if (this->output_p &&
    this->options &&
    !this->options->write_html_p &&
    this->options->output_fn)
  return this->Write(line, length, true);

  /* If we already have a child object in the buffer, then we're done parsing
   headers, and all subsequent lines get passed to the inferior object
   without further processing by us.  (Our parent will stop feeding us
   lines when this Message part is out of data.)
   */
  if (this->part_buffer)
  return MimePartBufferWrite (this->part_buffer, line, length);

  /* Otherwise we don't yet have a child object in the buffer, which means
   we're not done parsing our headers yet.
   */
  if (!this->hdrs)
  {
    this->hdrs = new Headers();
    if (!this->hdrs) return MIME_OUT_OF_MEMORY;
  }

  status = this->hdrs->ParseLine(line, length);
  if (status < 0) return status;

  /* If this line is blank, we're now done parsing headers, and should
   examine our content-type to create our "body" part.
   */
  if (*line == '\r' || *line == '\n')
  {
    status = this->CloseHeaders();
    if (status < 0) return status;
  }

  return 0;
}

int
Encrypted::CloseHeaders()
{
  // Notify the JS Mime Emitter that this was an encrypted part that it should
  // hopefully not analyze for indexing...
  if (this->options->notify_nested_bodies)
    mimeEmitterAddHeaderField(this->options, "x-jsemitter-encrypted", "1");

  if (this->part_buffer) return -1;
  this->part_buffer = MimePartBufferCreate();
  if (!this->part_buffer)
  return MIME_OUT_OF_MEMORY;

  return 0;
}


int
Encrypted::AddChild(Part* child)
{
  if (!child) return -1;

  /* Encryption containers can only have one child. */
  if (this->nchildren != 0) return -1;

  return Super::AddChild(child);
}


int
Encrypted::EmitBufferedChild()
{
  int status = 0;
  char *ct = 0;
  Part *body;

  NS_ASSERTION(this->crypto_closure, "1.2 <mscott@netscape.com> 01 Nov 2001 17:59");

  /* Emit some HTML saying whether the signature was cool.
   But don't emit anything if in FO_QUOTE_MESSAGE mode.

   Also, don't emit anything if the enclosed object is itself a signed
   object -- in the case of an encrypted object which contains a signed
   object, we only emit the HTML once (since the normal way of encrypting
   and signing is to nest the signature inside the crypto envelope.)
   */
  if (this->crypto_closure &&
    this->options &&
    this->options->headers != HeadersState::Citation &&
    this->options->write_html_p &&
    this->options->output_fn)
    // && !mime_crypto_object_p(this->hdrs, true)) // XXX fix later XXX //
  {
    char *html;
#if 0 // XXX Fix this later XXX //
    char* html = CryptoGenerateHTML(this-crypto_closure);
    if (!html) return -1; /* MK_OUT_OF_MEMORY? */

    status = Part_write(this, html, strlen(html), false);
    PR_FREEIF(html);
    if (status < 0) return status;
#endif

    /* Now that we have written out the crypto stamp, the outermost header
     block is well and truly closed.  If this is in fact the outermost
     message, then run the post_header_html_fn now.
     */
    if (this->options &&
      this->options->state &&
      this->options->generate_post_header_html_fn &&
      !this->options->state->postHeaderHTMLRun)
    {
      Headers* outer_headers = nullptr;
      Part* p;
      for (p = this; p->parent; p = p->parent)
        outer_headers = p->headers;
      NS_ASSERTION(this->options->state->first_data_written_p, "1.2 <mscott@netscape.com> 01 Nov 2001 17:59");
      html = this->options->generate_post_header_html_fn(NULL,
                          this->options->html_closure,
                              outer_headers);
      this->options->state->postHeaderHTMLRun = true;
      if (html)
      {
        status = this->Write(html, strlen(html), false);
        PR_FREEIF(html);
        if (status < 0) return status;
      }
    }
  }
  else if (this->crypto_closure &&
       this->options &&
     this->options->decrypt_p)
  {
    /* Do this just to cause `mime_set_crypto_stamp' to be called, and to
     cause the various `decode_error' and `verify_error' slots to be set:
     we don't actually use the returned HTML, because we're not emitting
     HTML.  It's maybe not such a good thing that the determination of
     whether it was encrypted or not is tied up with generating HTML,
     but oh well. */
    char* html = CryptoGenerateHTML(this->crypto_closure);
    PR_FREEIF(html);
  }

  if (this->hdrs)
  ct = this->hdrs->Get(HEADER_CONTENT_TYPE, true, false);
  body = mime_create((ct ? ct : TEXT_PLAIN), this->hdrs, this->options);

#ifdef MIME_DRAFTS
  if (this->options->decompose_file_p) {
  if (mime_typep (body, (PartClass*) &mimeMultipartClass) )
    this->options->is_multipart_msg = true;
  else if (this->options->decompose_file_init_fn)
    this->options->decompose_file_init_fn(this->options->stream_closure,
                       this->hdrs);
  }
#endif /* MIME_DRAFTS */

  PR_FREEIF(ct);

  if (!body) return MIME_OUT_OF_MEMORY;
  status = AddChild(body);
  if (status < 0)
  {
    delete body;
    return status;
  }

  /* Now that we've added this new object to our list of children,
   start its parser going. */
  status = body->ParseBegin();
  if (status < 0) return status;

  /* If this object (or the parent) is being output, then by definition
   the child is as well.  (This is only necessary because this is such
   a funny sort of container...)
   */
  if (!body->output_p &&
    (this->output_p ||
     (this->parent && this->parent->output_p)))
  body->output_p = true;

  /* If the body is being written raw (not as HTML) then make sure to
   write its headers as well. */
  if (body->output_p && this->output_p && !this->options->write_html_p)
  {
    status = body->Write("", 0, false);  /* initialize */
    if (status < 0) return status;
    status = body->headers->WriteRawHeaders(this->options, false);
    if (status < 0) return status;
  }

  if (this->part_buffer)  /* part_buffer is 0 for 0-length encrypted data. */
  {
#ifdef MIME_DRAFTS
    if (this->options->decompose_file_p && !this->options->is_multipart_msg)
    {
    status = MimePartBufferRead(this->part_buffer,
                /* The (MimeConverterOutputCallback) cast is to turn the `void'
                   argument into `Part'. */
                 ((MimeConverterOutputCallback)
                this->options->decompose_file_output_fn),
                this->options->stream_closure);
    }
    else
    {
#endif /* MIME_DRAFTS */

  status = MimePartBufferRead(this->part_buffer,
                /* The (MimeConverterOutputCallback) cast is to turn the `void'
                   argument into `Part'. */
                 ((MimeConverterOutputCallback) body->clazz->ParseBuffer),
                body);
#ifdef MIME_DRAFTS
    }
#endif /* MIME_DRAFTS */
  }
  if (status < 0) return status;

  /* The child has been fully processed.  Close it off.
   */
  status = body->ParseEOF(false);
  if (status < 0) return status;

  status = body->ParseEnd(false);
  if (status < 0) return status;

#ifdef MIME_DRAFTS
  if (this->options->decompose_file_p && !this->options->is_multipart_msg)
    this->options->decompose_file_close_fn(this->options->stream_closure);
#endif /* MIME_DRAFTS */

  /* Put out a separator after every encrypted object. */
  status = WriteSeparator();
  if (status < 0) return status;

  Cleanup(false);

  return 0;
}

} // namespace mime
} // namsepace mozilla
