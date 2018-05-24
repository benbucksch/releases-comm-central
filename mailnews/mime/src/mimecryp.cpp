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

static int MimeHandleDecryptedOutput (const char *, int32_t, void *);
static int MimeHandleDecryptedOutputLine (char *, int32_t, Part *);

int MimeEncrypted::ParseBegin()
{
  MimeDecoderData *(*fn) (MimeConverterOutputCallback, void*) = 0;

  if (this->crypto_closure)
  return -1;

  this->crypto_closure = CryptoInit(MimeHandleDecryptedOutput, this);
  if (!this->crypto_closure)
  return -1;


  /* (Mostly duplicated from MimeLeaf, see comments in mimecryp.h.)
     Initialize a decoder if necessary.
   */
  if (!this->encoding)
  ;
  else if (!PL_strcasecmp(this->encoding, ENCODING_BASE64))
  fn = &MimeB64DecoderInit;
  else if (!PL_strcasecmp(this->encoding, ENCODING_QUOTED_PRINTABLE))
  {
    this->decoder_data = MimeQPDecoderInit(ParseDecodedBuffer, this);

    if (!this->decoder_data)
    return MIME_OUT_OF_MEMORY;
  }
  else if (!PL_strcasecmp(this->encoding, ENCODING_UUENCODE) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE2) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE3) ||
       !PL_strcasecmp(this->encoding, ENCODING_UUENCODE4))
  fn = &MimeUUDecoderInit;
  else if (!PL_strcasecmp(this->encoding, ENCODING_YENCODE))
    fn = &MimeYDecoderInit;
  if (fn)
  {
    this->decoder_data = fn(ParseDecodedBuffer, this);

    if (!this->decoder_data)
    return MIME_OUT_OF_MEMORY;
  }

  return Super::ParseBegin();
}

int MimeEncrypted::ParseBuffer(const char *buffer, int32_t size)
{
  /* (Duplicated from MimeLeaf, see comments in mimecryp.h.)
   */

  if (this->closed_p) return -1;

  /* Don't consult output_p here, since at this point we're behaving as a
   simple container object -- the output_p decision should be made by
   the child of this object. */

  if (this->decoder_data)
  return MimeDecoderWrite(this->decoder_data, buffer, size, nullptr);
  else
  return ParseDecodedBuffer(buffer, size, this);
}

int MimeEncrypted::ParseDecodedBuffer(const char *buffer, int32_t size, void *obj)
{
  MimeEncrypted *enc = (MimeEncrypted *) obj;
  return obj->CryptoWrite(buffer, size, obj->crypto_closure);
}

int MimeEncrypted::ParseEOF(bool abort_p)
{
  int status = 0;

  if (this->closed_p) return 0;
  NS_ASSERTION(!this->parsed_p, "1.2 <mscott@netscape.com> 01 Nov 2001 17:59");

  /* (Duplicated from MimeLeaf, see comments in mimecryp.h.)
     Close off the decoder, to cause it to give up any buffered data that
   it is still holding.
   */
  if (this->decoder_data)
  {
    int status = MimeDecoderDestroy(this->decoder_data, false);
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
    int status = MimeHandleDecryptedOutputLine(this->ibuffer,
                          this->ibuffer_fp, this);
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

void MimeEncrypted::Cleanup(bool finalizing_p)
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

  /* (Duplicated from MimeLeaf, see comments in mimecryp.h.)
   Free the decoder data, if it's still around. */
  if (this->decoder_data)
  {
    MimeDecoderDestroy(this->decoder_data, true);
    this->decoder_data = 0;
  }

  if (this->hdrs)
  {
    MimeHeaders_free(this->hdrs);
    this->hdrs = 0;
  }
}


~MimeEncrypted()
{
  Cleanup(true);
}


static int
MimeHandleDecryptedOutput (const char *buf, int32_t buf_size,
               void *output_closure)
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
  Part *obj = (MimeObject *) output_closure;

  /* Is it truly safe to use ibuffer here?  I think so... */
  return mime_LineBuffer (buf, buf_size,
             &obj->ibuffer, &obj->ibuffer_size, &obj->ibuffer_fp,
             true,
             ((int (*) (char *, int32_t, void *))
              /* This cast is to turn void into Part */
              MimeHandleDecryptedOutputLine),
             obj);
}

static int
MimeHandleDecryptedOutputLine (char *line, int32_t length, Part *obj)
{
  /* Largely the same as MimeMessage_ParseLine (the other MIME container
   type which contains exactly one child.)
   */
  MimeEncrypted *enc = (MimeEncrypted *) obj;
  int status = 0;

  if (!line || !*line) return -1;

  /* If we're supposed to write this object, but aren't supposed to convert
   it to HTML, simply pass it through unaltered. */
  if (obj->output_p &&
    obj->options &&
    !obj->options->write_html_p &&
    obj->options->output_fn)
  return Part_write(obj, line, length, true);

  /* If we already have a child object in the buffer, then we're done parsing
   headers, and all subsequent lines get passed to the inferior object
   without further processing by us.  (Our parent will stop feeding us
   lines when this MimeMessage part is out of data.)
   */
  if (enc->part_buffer)
  return MimePartBufferWrite (enc->part_buffer, line, length);

  /* Otherwise we don't yet have a child object in the buffer, which means
   we're not done parsing our headers yet.
   */
  if (!enc->hdrs)
  {
    enc->hdrs = MimeHeaders_new();
    if (!enc->hdrs) return MIME_OUT_OF_MEMORY;
  }

  status = MimeHeaders_ParseLine(line, length, enc->hdrs);
  if (status < 0) return status;

  /* If this line is blank, we're now done parsing headers, and should
   examine our content-type to create our "body" part.
   */
  if (*line == '\r' || *line == '\n')
  {
    status = enc->CloseHeaders();
    if (status < 0) return status;
  }

  return 0;
}

int MimeEncrypted::CloseHeaders()
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


int MimeEncrypted::AddChild(Part *child)
{
  if (!child) return -1;

  /* Encryption containers can only have one child. */
  if (this->nchildren != 0) return -1;

  return Super::AddChild(child);
}


int MimeEncrypted::EmitBufferedChild()
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
    this->options->headers != MimeHeadersCitation &&
    this->options->write_html_p &&
    this->options->output_fn)
    // && !mime_crypto_object_p(this->hdrs, true)) // XXX fix later XXX //
  {
    char *html;
#if 0 // XXX Fix this later XXX //
    char *html = CryptoGenerateHTML(this-crypto_closure);
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
      !this->options->state->post_header_html_run_p)
    {
      MimeHeaders *outer_headers = nullptr;
      Part *p;
      for (p = this; p->parent; p = p->parent)
        outer_headers = p->headers;
      NS_ASSERTION(this->options->state->first_data_written_p, "1.2 <mscott@netscape.com> 01 Nov 2001 17:59");
      html = this->options->generate_post_header_html_fn(NULL,
                          this->options->html_closure,
                              outer_headers);
      this->options->state->post_header_html_run_p = true;
      if (html)
      {
        status = Part_write(this, html, strlen(html), false);
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
    char *html = CryptoGenerateHTML(this->crypto_closure);
    PR_FREEIF(html);
  }

  if (this->hdrs)
  ct = MimeHeaders_get (this->hdrs, HEADER_CONTENT_TYPE, true, false);
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
    mime_free(body);
    return status;
  }

  /* Now that we've added this new object to our list of children,
   start its parser going. */
  status = body->clazz->ParseBegin(body);
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
    status = Part_write(body, "", 0, false);  /* initialize */
    if (status < 0) return status;
    status = MimeHeaders_write_raw_headers(body->headers, this->options,
                       false);
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
  status = body->clazz->ParseEOF(body, false);
  if (status < 0) return status;

  status = body->clazz->ParseEnd(body, false);
  if (status < 0) return status;

#ifdef MIME_DRAFTS
  if (this->options->decompose_file_p && !this->options->is_multipart_msg)
    this->options->decompose_file_close_fn(this->options->stream_closure);
#endif /* MIME_DRAFTS */

  /* Put out a separator after every encrypted object. */
  status = Part_write_separator(this);
  if (status < 0) return status;

  Cleanup(false);

  return 0;
}
