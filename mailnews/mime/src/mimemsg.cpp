/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "mimemsg.h"
#include "mimemoz2.h"
#include "prmem.h"
#include "prio.h"
#include "plstr.h"
#include "msgCore.h"
#include "prlog.h"
#include "prprf.h"
#include "nsMimeStringResources.h"
#include "nsMimeTypes.h"
#include "nsMsgMessageFlags.h"
#include "nsString.h"
#include "mimetext.h"
#include "mimecryp.h"
#include "mimetpfl.h"
#include "nsINetUtil.h"
#include "nsMsgUtils.h"
#include "nsMsgI18N.h"

namespace mozilla {
namespace mime {

Message::Message()
  : hdrs(nullptr)
  , newline_p(false)
  , crypto_stamped_p(false)
  , crypto_msg_signed_p(false)
  , crypto_msg_encrypted_p(false)
  , grabSubject(false)
  , bodyLength(0)
  , sizeSoFor(0)
{
}

Message::~Message()
{
  if (this->hdrs)
    delete this->hdrs;
  msg->hdrs = 0;
}

int
Message::ParseBegin()
{
  int status = Super::ParseBegin();
  if (status < 0) return status;

  if (this->parent)
  {
    this->grabSubject = true;
  }

  /* Messages have separators before the headers, except for the outermost
   message. */
  return WriteSeparator();
}


int
MimeMessage::ParseLine(const char* aLine, int32_t aLength)
{
  const char * line = aLine;
  int32_t length = aLength;

  int status = 0;

  NS_ASSERTION(line && *line, "empty line in mime msg ParseLine");
  if (!line || !*line) return -1;

  this->sizeSoFar += length;

  if (this->grabSubject)
  {
    if ( (!PL_strncasecmp(line, "Subject: ", 9)) && (this->parent) )
    {
      if ( (this->headers) && (!this->headers->munged_subject) )
      {
        this->headers->munged_subject = (char*)PL_strndup(line + 9, length - 9);
        char *tPtr = this->headers->munged_subject;
        while (*tPtr)
        {
          if ( (*tPtr == '\r') || (*tPtr == '\n') )
          {
            *tPtr = '\0';
            break;
          }
          tPtr++;
        }
      }
    }
  }

  /* If we already have a child object, then we're done parsing headers,
   and all subsequent lines get passed to the inferior object without
   further processing by us.  (Our parent will stop feeding us lines
   when this MimeMessage part is out of data.)
   */
  if (this->nchildren)
  {
    Part *kid = this->children[0];
    bool nl;
    PR_ASSERT(kid);
    if (!kid) return -1;

    this->bodyLength += length;

    /* Don't allow MimeMessage objects to not end in a newline, since it
     would be inappropriate for any following part to appear on the same
     line as the last line of the message.

     #### This assumes that the only time the `ParseLine' method is
     called with a line that doesn't end in a newline is when that line
     is the last line.
     */
    nl = (length > 0 && (line[length-1] == '\r' || line[length-1] == '\n'));

#ifdef MIME_DRAFTS
    if (!mime_typep (kid, (PartClass*) &mimeMessageClass) &&
        this->options &&
        this->options->decompose_file_p &&
        !this->options->is_multipart_msg &&
        this->options->decompose_file_output_fn &&
        !this->options->decrypt_p)
    {
      // If we are processing a flowed plain text line, we need to parse the
      // line in mimeInlineTextPlainFlowedClass.
      if (mime_typep(kid, (PartClass *)&mimeInlineTextPlainFlowedClass))
      {
        // Remove any stuffed space.
        if (length > 0 && ' ' == *line)
        {
          line++;
          length--;
        }
        return kid->clazz->ParseLine (line, length, kid);
      }
      else
      {
        status = this->options->decompose_file_output_fn(line,
                                 length,
                                 this->options->stream_closure);
        if (status < 0) return status;
        if (!nl) {
        status = this->options->decompose_file_output_fn(MSG_LINEBREAK,
                                 MSG_LINEBREAK_LEN,
                                 this->options->stream_closure);
        if (status < 0) return status;
        }
        return status;
      }
    }
#endif /* MIME_DRAFTS */


    if (nl)
    return kid->clazz->ParseBuffer (line, length, kid);
    else
    {
      /* Hack a newline onto the end. */
      char *s = (char *)PR_MALLOC(length + MSG_LINEBREAK_LEN + 1);
      if (!s) return MIME_OUT_OF_MEMORY;
      memcpy(s, line, length);
      PL_strncpyz(s + length, MSG_LINEBREAK, MSG_LINEBREAK_LEN + 1);
      status = kid->clazz->ParseBuffer (s, length + MSG_LINEBREAK_LEN, kid);
      PR_Free(s);
      return status;
    }
  }

  /* Otherwise we don't yet have a child object, which means we're not
   done parsing our headers yet.
   */
  if (!this->hdrs)
  {
    this->hdrs = new Headers()
    if (!this->hdrs) return MIME_OUT_OF_MEMORY;
  }

#ifdef MIME_DRAFTS
  if (this->options &&
      this->options->decompose_file_p &&
      ! this->options->is_multipart_msg &&
      this->options->done_parsing_outer_headers &&
      this->options->decompose_file_output_fn)
  {
    status = this->options->decompose_file_output_fn(line, length, this->options->stream_closure);
    if (status < 0)
      return status;
  }
#endif /* MIME_DRAFTS */

  status = this->hdrs->ParseLine(line, length);
  if (status < 0) return status;

  /* If this line is blank, we're now done parsing headers, and should
   examine our content-type to create our "body" part.
   */
  if (*line == '\r' || *line == '\n')
  {
    status = CloseHeaders();
    if (status < 0) return status;
  }

  return 0;
}

int
MimeMessage::CloseHeaders()
{
  int status = 0;
  char *ct = 0;      /* Content-Type header */
  Part *body;

  // Do a proper decoding of the munged subject.
  if (this->headers && this->hdrs && this->grabSubject && this->headers->munged_subject) {
    // nsMsgI18NConvertToUnicode wants nsAStrings...
    nsDependentCString orig(this->headers->munged_subject);
    nsAutoString dest;
    // First, get the Content-Type, then extract the charset="whatever" part of
    // it.
    nsCString charset;
    nsCString contentType;
    contentType.Adopt(this->hdrs->Get(HEADER_CONTENT_TYPE, false, false));
    if (!contentType.IsEmpty())
      charset.Adopt(MimeHeaders_get_parameter(contentType.get(), "charset", nullptr, nullptr));

    // If we've got a charset, use nsMsgI18NConvertToUnicode to magically decode
    // the munged subject.
    if (!charset.IsEmpty()) {
      nsresult rv = nsMsgI18NConvertToUnicode(charset, orig, dest);
      // If we managed to convert the string, replace munged_subject with the
      // UTF8 version of it, otherwise, just forget about it (maybe there was an
      // improperly encoded string in there).
      PR_Free(this->headers->munged_subject);
      if (NS_SUCCEEDED(rv))
        this->headers->munged_subject = ToNewUTF8String(dest);
      else
        this->headers->munged_subject = nullptr;
    } else {
      PR_Free(this->headers->munged_subject);
      this->headers->munged_subject = nullptr;
    }
  }

  if (this->hdrs)
  {
    bool outer_p = !this->headers; /* is this the outermost message? */


#ifdef MIME_DRAFTS
    if (outer_p &&
        this->options &&
        (this->options->decompose_file_p || this->options->caller_need_root_headers) &&
        this->options->decompose_headers_info_fn)
    {
#ifdef ENABLE_SMIME
      if (this->options->decrypt_p && !mime_crypto_object_p(this->hdrs, false))
        this->options->decrypt_p = false;
#endif /* ENABLE_SMIME */
      if (!this->options->caller_need_root_headers || (this == this->options->state->root))
        status = this->options->decompose_headers_info_fn( this->options->stream_closure, this->hdrs);
    }
#endif /* MIME_DRAFTS */


    /* If this is the outermost message, we need to run the
     `generate_header' callback.  This happens here instead of
     in `ParseBegin', because it's only now that we've parsed
     our headers.  However, since this is the outermost message,
     we have yet to write any HTML, so that's fine.
     */
    if (outer_p &&
        this->output_p &&
        this->options &&
        this->options->write_html_p &&
        this->options->generate_header_html_fn)
    {
      int lstatus = 0;
      char *html = 0;

      /* The generate_header_html_fn might return HTML, so it's important
       that the output stream be set up with the proper type before we
       make the Part_write() call below. */
      if (!this->options->state->first_data_written_p)
      {
        lstatus = Part_output_init (this, TEXT_HTML);
        if (lstatus < 0) return lstatus;
        PR_ASSERT(this->options->state->first_data_written_p);
      }

      html = this->options->generate_header_html_fn(NULL, this->options->html_closure, this->hdrs);
      if (html)
      {
        lstatus = Write(html, strlen(html), false);
        PR_Free(html);
        if (lstatus < 0) return lstatus;
      }
    }


    /* Find the content-type of the body of this message.
     */
    {
    bool ok = true;
    char *mv = this->hdrs->Get(HEADER_MIME_VERSION,
                  true, false);

#ifdef REQUIRE_MIME_VERSION_HEADER
    /* If this is the outermost message, it must have a MIME-Version
       header with the value 1.0 for us to believe what might be in
       the Content-Type header.  If the MIME-Version header is not
       present, we must treat this message as untyped.
     */
    ok = (mv && !strcmp(mv, "1.0"));
#else
    /* #### actually, we didn't check this in Mozilla 2.0, and checking
       it now could cause some compatibility nonsense, so for now, let's
       just believe any Content-Type header we see.
     */
    ok = true;
#endif

    if (ok)
      {
      ct = this->hdrs->Get(HEADER_CONTENT_TYPE, true, false);

      /* If there is no Content-Type header, but there is a MIME-Version
         header, then assume that this *is* in fact a MIME message.
         (I've seen messages with

          MIME-Version: 1.0
          Content-Transfer-Encoding: quoted-printable

         and no Content-Type, and we should treat those as being of type
         MimeInlineTextPlain rather than MimeUntypedText.)
       */
      if (mv && !ct)
        ct = strdup(TEXT_PLAIN);
      }

    PR_FREEIF(mv);  /* done with this now. */
    }

    /* If this message has a body which is encrypted and we're going to
       decrypt it (without converting it to HTML, since decrypt_p and
       write_html_p are never true at the same time)
    */
    if (this->output_p &&
        this->options &&
        this->options->decrypt_p
#ifdef ENABLE_SMIME
        && !mime_crypto_object_p(this->hdrs, false)
#endif /* ENABLE_SMIME */
        )
    {
      /* The body of this message is not an encrypted object, so we need
         to turn off the decrypt_p flag (to prevent us from s#$%ing the
         body of the internal object up into one.) In this case,
         our output will end up being identical to our input.
      */
      this->options->decrypt_p = false;
    }

    /* Emit the HTML for this message's headers.  Do this before
     creating the object representing the body.
     */
    if (this->output_p &&
        this->options &&
        this->options->write_html_p)
    {
      /* If citation headers are on, and this is not the outermost message,
       turn them off. */
      if (this->options->headers == MimeHeadersCitation && !outer_p)
      this->options->headers = MimeHeadersSome;

      /* Emit a normal header block. */
      status = MimeMessage_write_headers_html(this);
      if (status < 0)
      {
        PR_FREEIF(ct);
        return status;
      }
    }
    else if (this->output_p)
    {
      /* Dump the headers, raw. */
      status = Write("", 0, false);  /* initialize */
      if (status < 0)
      {
        PR_FREEIF(ct);
        return status;
      }
      status = this->hdrs->WriteRawHeaders(this->options, this->options->decrypt_p);
      if (status < 0)
      {
        PR_FREEIF(ct);
        return status;
      }
    }

#ifdef XP_UNIX
    if (outer_p && this->output_p)
    /* Kludge from mimehdrs.c */
      this->hdrs->DoUnixDisplayHookHack();
#endif /* XP_UNIX */
  }

  /* Never put out a separator after a message header block. */
  if (this->options && this->options->state)
    this->options->state->separator_suppressed_p = true;

#ifdef MIME_DRAFTS
  if (!this->headers &&    /* outer most message header */
      this->options &&
      this->options->decompose_file_p &&
     ct )
  this->options->is_multipart_msg = PL_strcasestr(ct, "multipart/") != NULL;
#endif /* MIME_DRAFTS */


  body = mime_create(ct, this->hdrs, this->options);

  PR_FREEIF(ct);
  if (!body) return MIME_OUT_OF_MEMORY;
  status = Super::AddChild(body);
  if (status < 0)
  {
    mime_free(body);
    return status;
  }

  // Only do this if this is a Text Object!
  if ( mime_typep(body, (PartClass *) &mimeInlineTextClass) )
  {
    ((MimeInlineText *) body)->needUpdateMsgWinCharset = true;
  }

  /* Now that we've added this new object to our list of children,
   start its parser going. */
  status = body->clazz->ParseBegin(body);
  if (status < 0) return status;

  // Now notify the emitter if this is the outer most message, unless
  // it is a part that is not the head of the message. If it's a part,
  // we need to figure out the content type/charset of the part
  //
  bool outer_p = !this->headers;  /* is this the outermost message? */

  if ( (outer_p || this->options->notify_nested_bodies) &&
       (!this->options->part_to_load || this->options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay))
  {
    // call SetMailCharacterSetToMsgWindow() to set a menu charset
    if (mime_typep(body, (PartClass *) &mimeInlineTextClass))
    {
      MimeInlineText  *text = (MimeInlineText *) body;
      if (text && text->charset && *text->charset)
        SetMailCharacterSetToMsgWindow(body, text->charset);
    }

    char  *msgID = msg->hdrs->Get(HEADER_MESSAGE_ID,
                                    false, false);

    const char  *outCharset = NULL;
    if (!this->options->force_user_charset)  /* Only convert if the user prefs is false */
      outCharset = "UTF-8";

    mimeEmitterStartBody(this->options, (this->options->headers == MimeHeadersNone), msgID, outCharset);
    PR_FREEIF(msgID);

  // setting up truncated message html fotter function
  char *xmoz = this->hdrs->Get(HEADER_X_MOZILLA_STATUS, false,
                 false);
  if (xmoz)
  {
    uint32_t flags = 0;
    char dummy = 0;
    if (sscanf(xmoz, " %x %c", &flags, &dummy) == 1 &&
      flags & nsMsgMessageFlags::Partial)
    {
      this->options->html_closure = this;
      this->options->generate_footer_html_fn =
        MimeMessage_partial_message_html;
    }
    PR_FREEIF(xmoz);
  }
  }

  return 0;
}



int
MimeMessage::ParseEOF(abort_p)
{
  int status;
  bool outer_p;
  if (this->closed_p) return 0;

  /* Run parent method first, to flush out any buffered data. */
  status = Super::ParseEOF(abort_p);
  if (status < 0) return status;

  outer_p = !this->headers;  /* is this the outermost message? */

  // Hack for messages with truncated headers (bug 244722)
  // If there is no empty line in a message, the parser can't figure out where
  // the headers end, causing parsing to hang. So we insert an extra newline
  // to keep it happy. This is OK, since a message without any empty lines is
  // broken anyway...
  if(outer_p && this->hdrs && ! this->hdrs->done_p) {
    ParseLine("\n", 1);
  }

  // Once we get to the end of parsing the message, we will notify
  // the emitter that we are done the the body.

  // Mark the end of the mail body if we are actually emitting the
  // body of the message (i.e. not Header ONLY)
  if ((outer_p || this->options->notify_nested_bodies) && this->options &&
      this->options->write_html_p)
  {
    if (this->options->generate_footer_html_fn)
    {
      mime_stream_data *msd =
        (mime_stream_data*)this->options->stream_closure;
      if (msd)
      {
        char *html = this->options->generate_footer_html_fn
          (msd->orig_url_name, this->options->html_closure, this->hdrs);
        if (html)
        {
          int lstatus = Write(html, strlen(html), false);
          PR_Free(html);
          if (lstatus < 0) return lstatus;
        }
      }
    }
    if ((!this->options->part_to_load  || this->options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay) &&
      this->options->headers != MimeHeadersOnly)
      mimeEmitterEndBody(this->options);
  }

#ifdef MIME_DRAFTS
  if (this->options &&
      this->options->decompose_file_p &&
      this->options->done_parsing_outer_headers &&
      ! this->options->is_multipart_msg &&
      ! mime_typep(this, (PartClass*) &mimeEncryptedClass) &&
      this->options->decompose_file_close_fn) {
  status = this->options->decompose_file_close_fn(this->options->stream_closure);

  if ( status < 0 ) return status;
  }
#endif /* MIME_DRAFTS */


  /* Put out a separator after every message/rfc822 object. */
  if (!abort_p && !outer_p)
  {
    status = WriteSeparator();
    if (status < 0) return status;
  }

  return 0;
}


int
Message::AddChild(Part* child)
{
  PR_ASSERT(parent && child);
  if (!parent || !child) return -1;

  /* message/rfc822 containers can only have one child. */
  PR_ASSERT(cont->nchildren == 0);
  if (cont->nchildren != 0) return -1;

#ifdef MIME_DRAFTS
  if ( parent->options &&
     parent->options->decompose_file_p &&
     ! parent->options->is_multipart_msg &&
     ! mime_typep(child, (PartClass*) &mimeEncryptedClass) &&
     parent->options->decompose_file_init_fn ) {
  int status = 0;
  status = parent->options->decompose_file_init_fn (
                        parent->options->stream_closure,
                        ((MimeMessage*)parent)->hdrs );
  if ( status < 0 ) return status;
  }
#endif /* MIME_DRAFTS */

  return ((MimeContainerClass*)&MIME_SUPERCLASS)->add_child (parent, child);
}

// This is necessary to determine which charset to use for a reply/forward
char *
MimeMessage::DetermineMailCharset()
{
  char          *retCharset = nullptr;

  if (this->hdrs)
  {
    char *ct = this->hdrs->Get(HEADER_CONTENT_TYPE,
                                false, false);
    if (ct)
    {
      retCharset = MimeHeaders_get_parameter (ct, "charset", NULL, NULL);
      PR_Free(ct);
    }

    if (!retCharset)
    {
      // If we didn't find "Content-Type: ...; charset=XX" then look
      // for "X-Sun-Charset: XX" instead.  (Maybe this should be done
      // in MimeSunAttachmentClass, but it's harder there than here.)
      retCharset = this->hdrs->Get(HEADER_X_SUN_CHARSET,
                                    false, false);
    }
  }

  if (!retCharset)
    return strdup("ISO-8859-1");
  else
    return retCharset;
}

int
MimeMessage::WriteHeadersHTML()
{
  int             status;

  if (!this->options || !this->options->output_fn)
    return 0;

  PR_ASSERT(this->output_p && this->options->write_html_p);

  // To support the no header option! Make sure we are not
  // suppressing headers on included email messages...
  if ( (this->options->headers == MimeHeadersNone) &&
       (this == this->options->state->root) )
  {
    // Ok, we are going to kick the Emitter for a StartHeader
    // operation ONLY WHEN THE CHARSET OF THE ORIGINAL MESSAGE IS
    // NOT US-ASCII ("ISO-8859-1")
    //
    // This is only to notify the emitter of the charset of the
    // original message
    char    *mailCharset = DetermineMailCharset();

    if ( (mailCharset) && (PL_strcasecmp(mailCharset, "US-ASCII")) &&
         (PL_strcasecmp(mailCharset, "ISO-8859-1")) )
      mimeEmitterUpdateCharacterSet(this->options, mailCharset);
    PR_FREEIF(mailCharset);
    return 0;
  }

  if (!this->options->state->first_data_written_p)
  {
    status = OutputInit(TEXT_HTML);
    if (status < 0)
    {
      mimeEmitterEndHeader(this->options, this);
      return status;
    }
    PR_ASSERT(this->options->state->first_data_written_p);
  }

  // Start the header parsing by the emitter
  char *msgID = this->hdrs->Get(HEADER_MESSAGE_ID,
                                    false, false);
  bool outer_p = !this->headers; /* is this the outermost message? */
  if (!outer_p && this->options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay &&
      this->options->part_to_load)
  {
    //Maybe we are displaying a embedded message as outer part!
    char *id = PartAddress();
    if (id)
    {
      outer_p = !strcmp(id, this->options->part_to_load);
      PR_Free(id);
    }
  }

  // Ok, we should really find out the charset of this part. We always
  // output UTF-8 for display, but the original charset is necessary for
  // reply and forward operations.
  //
  char    *mailCharset = DetermineMailCharset();
  mimeEmitterStartHeader(this->options,
                            outer_p,
                            (this->options->headers == MimeHeadersOnly),
                            msgID,
                            mailCharset);

  // Change the default_charset by the charset of the original message
  // ONLY WHEN THE CHARSET OF THE ORIGINAL MESSAGE IS NOT US-ASCII
  // ("ISO-8859-1") and default_charset and mailCharset are different,
  // or when there is no default_charset (this can happen with saved messages).
  if ( (!this->options->default_charset ||
        ((mailCharset) && (PL_strcasecmp(mailCharset, "US-ASCII")) &&
         (PL_strcasecmp(mailCharset, "ISO-8859-1")) &&
         (PL_strcasecmp(this->options->default_charset, mailCharset)))) &&
       !this->options->override_charset )
  {
    PR_FREEIF(this->options->default_charset);
    this->options->default_charset = strdup(mailCharset);
  }

  PR_FREEIF(msgID);
  PR_FREEIF(mailCharset);

  status = this->hdrs->WriteAllHeaders(this->options, false);
  if (status < 0)
  {
    mimeEmitterEndHeader(this->options, this);
    return status;
  }

  if (!this->crypto_stamped_p)
  {
  /* If we're not writing a xlation stamp, and this is the outermost
  message, then now is the time to run the post_header_html_fn.
  (Otherwise, it will be run when the xlation-stamp is finally
  closed off, in MimeXlateed_emit_buffered_child() or
  MimeMultipartSigned_emit_child().)
     */
    if (this->options &&
        this->options->state &&
        this->options->generate_post_header_html_fn &&
        !this->options->state->post_header_html_run_p)
    {
      char *html = 0;
      PR_ASSERT(this->options->state->first_data_written_p);
      html = this->options->generate_post_header_html_fn(nullptr,
                                          this->options->html_closure,
                                          this->hdrs);
      this->options->state->post_header_html_run_p = true;
      if (html)
      {
        status = Write(html, strlen(html), false);
        PR_Free(html);
        if (status < 0)
        {
          mimeEmitterEndHeader(this->options, this);
          return status;
        }
      }
    }
  }

  mimeEmitterEndHeader(this->options, this);

  // rhp:
  // For now, we are going to parse the entire message, even if we are
  // only interested in headers...why? Well, because this is the only
  // way to build the attachment list. Now we will have the attachment
  // list in the output being created by the XML emitter. If we ever
  // want to go back to where we were before, just uncomment the conditional
  // and it will stop at header parsing.
  //
  // if (this->options->headers == MimeHeadersOnly)
  //   return -1;
  // else

  return 0;
}

static char *
MimeMessage_partial_message_html(const char *data, void *closure,
                                 MimeHeaders *headers)
{
  Message *msg = (Message *)closure;
  nsAutoCString orig_url(data);
  char *uidl = MimeHeaders_get(headers, HEADER_X_UIDL, false, false);
  char *msgId = MimeHeaders_get(headers, HEADER_MESSAGE_ID, false,
                  false);
  char *msgIdPtr = PL_strchr(msgId, '<');

  int32_t pos = orig_url.Find("mailbox-message");
  if (pos != -1)
    orig_url.Cut(pos + 7, 8);

  pos = orig_url.FindChar('#');
  if (pos != -1)
    orig_url.Replace(pos, 1, "?number=", 8);

  if (msgIdPtr)
    msgIdPtr++;
  else
    msgIdPtr = msgId;
  char *gtPtr = PL_strchr(msgIdPtr, '>');
  if (gtPtr)
    *gtPtr = 0;

  bool msgBaseTruncated = (msg->bodyLength > MSG_LINEBREAK_LEN);

  nsCString partialMsgHtml;
  nsCString item;

  partialMsgHtml.AppendLiteral("<div style=\"margin: 1em auto; border: 1px solid black; width: 80%\">");
  partialMsgHtml.AppendLiteral("<div style=\"margin: 5px; padding: 10px; border: 1px solid gray; font-weight: bold; text-align: center;\">");

  partialMsgHtml.AppendLiteral("<span style=\"font-size: 120%;\">");
  if (msgBaseTruncated)
    item.Adopt(MimeGetStringByName(u"MIME_MSG_PARTIAL_TRUNCATED"));
  else
    item.Adopt(MimeGetStringByName(u"MIME_MSG_PARTIAL_NOT_DOWNLOADED"));
  partialMsgHtml += item;
  partialMsgHtml.AppendLiteral("</span><hr>");

  if (msgBaseTruncated)
    item.Adopt(MimeGetStringByName(u"MIME_MSG_PARTIAL_TRUNCATED_EXPLANATION"));
  else
    item.Adopt(MimeGetStringByName(u"MIME_MSG_PARTIAL_NOT_DOWNLOADED_EXPLANATION"));
  partialMsgHtml += item;
  partialMsgHtml.AppendLiteral("<br><br>");

  partialMsgHtml.AppendLiteral("<a href=\"");
  partialMsgHtml.Append(orig_url);

  if (msgIdPtr) {
    partialMsgHtml.AppendLiteral("&messageid=");

    MsgEscapeString(nsDependentCString(msgIdPtr), nsINetUtil::ESCAPE_URL_PATH,
                    item);

    partialMsgHtml.Append(item);
  }

  if (uidl) {
    partialMsgHtml.AppendLiteral("&uidl=");

    MsgEscapeString(nsDependentCString(uidl), nsINetUtil::ESCAPE_XALPHAS,
                    item);

    partialMsgHtml.Append(item);
  }

  partialMsgHtml.AppendLiteral("\">");
  item.Adopt(MimeGetStringByName(u"MIME_MSG_PARTIAL_CLICK_FOR_REST"));
  partialMsgHtml += item;
  partialMsgHtml.AppendLiteral("</a>");

  partialMsgHtml.AppendLiteral("</div></div>");

  return ToNewCString(partialMsgHtml);
}

#if defined(DEBUG) && defined(XP_UNIX)
int
MimeMessage::DebugPrint(PRFileDesc* stream, int32_t depth)
{
  char *addr = mime_part_address(this);
  int i;
  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
/*
  fprintf(stream, "<%s %s%s 0x%08X>\n",
      this->clazz->class_name,
      addr ? addr : "???",
      (this->container.nchildren == 0 ? " (no body)" : ""),
      (uint32_t) this);
*/
  PR_FREEIF(addr);

#if 0
  if (this->hdrs)
  {
    char *s;

    depth++;

# define DUMP(HEADER) \
    for (i=0; i < depth; i++)                        \
        PR_Write(stream, "  ", 2);                        \
    s = MimeHeaders_get (this->hdrs, HEADER, false, true);
/**
    \
    PR_Write(stream, HEADER ": %s\n", s ? s : "");              \
**/

    PR_FREEIF(s)

      DUMP(HEADER_SUBJECT);
      DUMP(HEADER_DATE);
      DUMP(HEADER_FROM);
      DUMP(HEADER_TO);
      /* DUMP(HEADER_CC); */
      DUMP(HEADER_NEWSGROUPS);
      DUMP(HEADER_MESSAGE_ID);
# undef DUMP

    PR_Write(stream, "\n", 1);
  }
#endif

  PR_ASSERT(this->nchildren <= 1);
  if (this->nchildren == 1)
  {
    Part *kid = this->children[0];
    int status = kid->clazz->debug_print (kid, stream, depth+1);
    if (status < 0) return status;
  }
  return 0;
}
#endif

} // namespace mime
} // namespace mozilla
