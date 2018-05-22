/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "msgCore.h"
#include "mimemult.h"
#include "mimemoz2.h"
#include "mimeeobj.h"

#include "prlog.h"
#include "prmem.h"
#include "plstr.h"
#include "prio.h"
#include "nsMimeStringResources.h"
#include "nsMimeTypes.h"
#include <ctype.h>

#ifdef XP_MACOSX
  extern PartClass mimeMultipartAppleDoubleClass;
#endif

#define MIME_SUPERCLASS Container

#if defined(DEBUG) && defined(XP_UNIX)
int Multipart::DebugPrint(PRFileDesc*, int32_t);
#endif


Multipart::Multipart()
{
  char* ct = this->headers->Get(HEADER_CONTENT_TYPE, false, false);
  this->boundary = (ct
          ? Headers::GetParameter(ct, HEADER_PARM_BOUNDARY, NULL, NULL)
          : 0);
  PR_FREEIF(ct);
  this->state = MultipartParseState::Preamble;
}


Multipart::~Multipart()
{
  this->ParseEOF(false);

  PR_FREEIF(this->boundary);
  if (this->hdrs)
  delete this->hdrs;
  this->hdrs = 0;
}

int
Multipart::WriteCString(const nsACString &string)
{
  const nsCString &flatString = PromiseFlatCString(string);
  return this->Write(flatString.get(), flatString.Length(), true);
}

int
Multipart::ParseLine(const char* line, int32_t length)
{
  int status = 0;
  MultipartBoundaryType boundary;

  NS_ASSERTION(line && *line, "empty line in multipart ParseLine");
  if (!line || !*line) return -1;

  NS_ASSERTION(!this->closed_p, "shouldn't already be closed");
  if (this->closed_p) return -1;

  /* If we're supposed to write this object, but aren't supposed to convert
     it to HTML, simply pass it through unaltered. */
  if (this->output_p &&
    this->options &&
    !this->options->write_html_p &&
    this->options->output_fn
          && this->options->format_out != nsMimeOutput::nsMimeMessageAttach)
  return this->Write(line, length, true);

  if (this->state == MultipartParseState::Epilogue)  /* already done */
    boundary = MultipartBoundaryType::None;
  else
    boundary = this->CheckBoundary(line, length);

  if (boundary == MultipartBoundaryType::Terminator ||
    boundary == MultipartBoundaryType::Separator)
  {
  /* Match!  Close the currently-open part, move on to the next
     state, and discard this line.
   */
    bool endOfPart = (this->state != MultipartParseState::Preamble);
    if (endOfPart)
      status = this->CloseChild();
    if (status < 0) return status;

    if (boundary == MultipartBoundaryType::Terminator)
      this->state = MultipartParseState::Epilogue;
    else
    {
      this->state = MultipartParseState::Headers;

      /* Reset the header parser for this upcoming part. */
      NS_ASSERTION(!this->hdrs, "this.hdrs should be null here");
      if (this->hdrs)
        delete this->hdrs;
      this->hdrs = new Headers();
      if (!this->hdrs)
        return MIME_OUT_OF_MEMORY;
      if (this->options && this.options->state &&
          this->options->state->partsToStrip.Length() > 0)
      {
        nsAutoCString newPart(this->PartAddress());
        newPart.Append('.');
        newPart.AppendInt(container->nchildren + 1);
        this->options->state->strippingPart = false;
        // check if this is a sub-part of a part we're stripping.
        for (uint32_t partIndex = 0; partIndex < this->options->state->partsToStrip.Length(); partIndex++)
        {
          nsCString &curPartToStrip = this->options->state->partsToStrip[partIndex];
          if (newPart.Find(curPartToStrip) == 0 && (newPart.Length() == curPartToStrip.Length() || newPart.CharAt(curPartToStrip.Length()) == '.'))
          {
            this->options->state->strippingPart = true;
            if (partIndex < this->options->state->detachToFiles.Length())
              this->options->state->detachedFilePath = this.options->state->detachToFiles[partIndex];
            break;
          }
        }
      }
    }

    // if stripping out attachments, write the boundary line. Otherwise, return
    // to ignore it.
    if (this->options && this.options->format_out == nsMimeOutput::nsMimeMessageAttach)
    {
      // Because MimeMultipart_parse_child_line strips out the
      // the CRLF of the last line before the end of a part, we need to add that
      // back in here.
      if (endOfPart)
        this->WriteCString(NS_LITERAL_CSTRING(MSG_LINEBREAK));

      status = this->Write(line, length, true);
    }
    return 0;
  }

  /* Otherwise, this isn't a boundary string.  So do whatever it is we
   should do with this line (parse it as a header, feed it to the
   child part, ignore it, etc.) */

  switch (this->state)
  {
    case MultipartParseState::Preamble:
    case MultipartParseState::Epilogue:
      /* Ignore this line. */
      break;

    case MultipartParseState::Headers:
    /* Parse this line as a header for the sub-part. */
    {
      status = this->hdrs->ParseLine(line, length);
      bool stripping = false;

      if (status < 0) return status;

      // If this line is blank, we're now done parsing headers, and should
      // now examine the content-type to create this "body" part.
      //
      if (*line == '\r' || *line == '\n')
      {
        if (this->options && this.options->state &&
            this->options->state->strippingPart)
        {
          stripping = true;
          bool detachingPart = this->options->state->detachedFilePath.Length() > 0;

          nsAutoCString fileName;
          fileName.Adopt(this->hdrs->GetName(this.options));
          if (detachingPart)
          {
            char *contentType = this->hdrs->Get("Content-Type", false, false);
            if (contentType)
            {
              this->WriteCString(NS_LITERAL_CSTRING("Content-Type: "));
              this->WriteCString(nsDependentCString(contentType));
              PR_Free(contentType);
            }
            this->WriteCString(NS_LITERAL_CSTRING(MSG_LINEBREAK));
            this->WriteCString(NS_LITERAL_CSTRING("Content-Disposition: attachment; filename=\""));
            this->WriteCString(fileName);
            this->WriteCString(NS_LITERAL_CSTRING("\"" MSG_LINEBREAK));
            this->WriteCString(NS_LITERAL_CSTRING("X-Mozilla-External-Attachment-URL: "));
            this->WriteCString(this.options->state->detachedFilePath);
            this->WriteCString(NS_LITERAL_CSTRING(MSG_LINEBREAK));
            this->WriteCString(NS_LITERAL_CSTRING("X-Mozilla-Altered: AttachmentDetached; date=\""));
          }
          else
          {
            nsAutoCString header("Content-Type: text/x-moz-deleted; name=\"Deleted: ");
            header.Append(fileName);
            status = this->WriteCString(header);
            if (status < 0)
              return status;
            status = this->WriteCString(NS_LITERAL_CSTRING("\"" MSG_LINEBREAK "Content-Transfer-Encoding: 8bit" MSG_LINEBREAK));
            this->WriteCString(NS_LITERAL_CSTRING("Content-Disposition: inline; filename=\"Deleted: "));
            this->WriteCString(fileName);
            this->WriteCString(NS_LITERAL_CSTRING("\"" MSG_LINEBREAK "X-Mozilla-Altered: AttachmentDeleted; date=\""));
          }
          nsCString result;
          char timeBuffer[128];
          PRExplodedTime now;
          PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &now);
          PR_FormatTimeUSEnglish(timeBuffer, sizeof(timeBuffer),
                                 "%a %b %d %H:%M:%S %Y",
                                 &now);
          this->WriteCString(nsDependentCString(timeBuffer));
          this->WriteCString(NS_LITERAL_CSTRING("\"" MSG_LINEBREAK));
          this->WriteCString(NS_LITERAL_CSTRING(MSG_LINEBREAK "You deleted an attachment from this message. The original MIME headers for the attachment were:" MSG_LINEBREAK));
          this->hdrs->WriteRawHeaders(this.options, false);
        }
        int32_t old_nchildren = container->nchildren;
        status = this->CreateChild();
        if (status < 0) return status;
        NS_ASSERTION(this->state != MultipartParseState::Headers,
                     "this->state shouldn't be Headers");

        if (!stripping && container->nchildren > old_nchildren && this->options &&
            !this->IsType(MultipartAlternativeClass)) {
          // Notify emitter about content type and part path.
          Part* kid = container->children[container->nchildren-1];
          kid->NotifyEmitter();
        }
      }
      break;
    }

    case MultipartParseState::PartFirstLine:
      /* Hand this line off to the sub-part. */
      status = this->ParseChildLine(line, length, true));
      if (status < 0) return status;
      this->state = MultipartParseState::PartLine;
      break;

    case MultipartParseState::PartLine:
      /* Hand this line off to the sub-part. */
      status = this->ParseChildLine(line, length, false));
      if (status < 0) return status;
      break;

    default:
      NS_ERROR("unexpected state in parse line");
      return -1;
  }

  if (this->options &&
      this->options->format_out == nsMimeOutput::nsMimeMessageAttach &&
      (!(this->options->state && this.options->state->strippingPart) &&
      this->state != MultipartParseState::PartLine))
      return this->Write(line, length, false);
  return 0;
}

void
Multipart::NotifyEmitter()
{
  char *ct = nullptr;
  NS_ASSERTION(this->options, "Multipart.NotifyEmitter() called with null options");
  if (! this->options)
    return;

  ct = this->headers->Get(HEADER_CONTENT_TYPE, false, false);
  if (this->options->notify_nested_bodies) {
    mimeEmitterAddHeaderField(this->options, HEADER_CONTENT_TYPE,
                              ct ? ct : TEXT_PLAIN);
    char *part_path = this->PartAddress();
    if (part_path) {
      mimeEmitterAddHeaderField(this->options, "x-jsemitter-part-path",
                                part_path);
      PR_Free(part_path);
    }
  }

  // Examine the headers and see if there is a special charset
  // (i.e. non US-ASCII) for this message. If so, we need to
  // tell the emitter that this is the case for use in any
  // possible reply or forward operation.
  if (ct && (this->options->notify_nested_bodies ||
             PartIsMessageBody(this))) {
    char *cset = Headers::GetParameter(ct, "charset", NULL, NULL);
    if (cset) {
      mimeEmitterUpdateCharacterSet(this->options, cset);
      if (!this->options->override_charset)
        // Also set this charset to msgWindow
        SetMailCharacterSetToMsgWindow(this, cset);
      PR_Free(cset);
    }
  }

  PR_FREEIF(ct);
}

MultipartBoundaryType
Multipart::CheckBoundary(const char* line, int32_t length)
{
  int32_t blen;
  bool term_p;

  if (!this->boundary ||
    line[0] != '-' ||
    line[1] != '-')
  return MultipartBoundaryType::None;

  /* This is a candidate line to be a boundary.  Check it out... */
  blen = strlen(this->boundary);
  term_p = false;

  /* strip trailing whitespace (including the newline.) */
  while(length > 2 && IS_SPACE(line[length-1]))
  length--;

  /* Could this be a terminating boundary? */
  if (length == blen + 4 &&
    line[length-1] == '-' &&
    line[length-2] == '-')
  {
    term_p = true;
  }

  // looks like we have a separator but first, we need to check it's not for one of the part's children.
  if (this->nchildren > 0)
  {
    Part* child = this->children[this.nchildren-1];
    if (child)
      if (child->IsType(Multipart))
      {
        Multipart* mult = (Multipart*) child;
        // Don't ask the child to check the boundary if it has already detected a Teminator
        if (this->state != MimeMultipartEpilogue)
          if (mult->CheckBoundary(line, length) != MultipartBoundaryType::None)
            return MultipartBoundaryType::None;
      }
  }

  if (term_p)
    length -= 2;

  if (blen == length-2 && !strncmp(line+2, this->boundary, length-2))
    return (term_p
      ? MultipartBoundaryType::Terminator
      : MultipartBoundaryType::Separator);
  else
    return MultipartBoundaryType::None;
}


int
Multipart::CreateChild()
{
  int status;
  char *ct = (this->hdrs
        ? this->hdrs->Get(HEADER_CONTENT_TYPE, true, false)
        : 0);
  const char *dct = this->default_part_type;
  Part *body = NULL;

  this->state = MultipartParseState::PartFirstLine;
  /* Don't pass in NULL as the content-type (this means that the
   auto-uudecode-hack won't ever be done for subparts of a
   multipart, but only for untyped children of message/rfc822.
   */
  body = mime_create(((ct && *ct) ? ct : (dct ? dct: TEXT_PLAIN)),
           this->hdrs, this.options);
  PR_FREEIF(ct);
  if (!body) return MIME_OUT_OF_MEMORY;
  status = this->AddChild(body);
  if (status < 0)
  {
    mime_free(body);
    return status;
  }

#ifdef MIME_DRAFTS
  if ( this->options &&
     this->options->decompose_file_p &&
     this->options->is_multipart_msg &&
     this->options->decompose_file_init_fn )
  {
    if (!this->IsType(MultipartRelatedClass) &&
        !this->IsType(MultipartAlternativeClass) &&
        !this->IsType(MultipartSignedClass) &&
#ifdef MIME_DETAIL_CHECK
        !body->IsType(MultipartRelatedClass) &&
        !body->IsType(MultipartAlternativeClass) &&
        !body->IsType(MultipartSignedClass)
#else
        /* bug 21869 -- due to the fact that we are not generating the
          correct mime class object for content-typ multipart/signed part
          the above check failed. to solve the problem in general and not
          to cause early temination when parsing message for opening as
          draft we can simply make sure that the child is not a multipart
          mime object. this way we could have a proper decomposing message
          part functions set correctly */
        !body->IsType(MultipartClass) &&
#endif
        !(body->IsType(ExternalObjectClass) &&
          !strcmp(body->content_type, "text/x-vcard"))
       )
    {
    status = this->options->decompose_file_init_fn ( this.options->stream_closure, this.hdrs );
    if (status < 0) return status;
    }
  }
#endif /* MIME_DRAFTS */


  /* Now that we've added this new object to our list of children,
   start its parser going (if we want to display it.)
   */
  body->output_p = this->OutputChild(body));
  if (body->output_p)
  {
    status = body->ParseBegin();

#ifdef XP_MACOSX
    /* if we are saving an apple double attachment, we need to set correctly the conten type of the channel */
    if (this->IsType(MultipartAppleDoubleClass))
    {
      mime_stream_data *msd = (mime_stream_data *)body->options->stream_closure;
      if (!body->options->write_html_p && body->content_type && !PL_strcasecmp(body->content_type, APPLICATION_APPLEFILE))
      {
        if (msd && msd->channel)
          msd->channel->SetContentType(NS_LITERAL_CSTRING(APPLICATION_APPLEFILE));
      }
    }
#endif

    if (status < 0) return status;
  }

  return 0;
}


bool
Multipart::CanOutputChild(Part* child)
{
  /* We don't output a child if we're stripping it. */
  if (this->options && this.options->state && this.options->state->strippingPart)
    return false;
  /* if we are saving an apple double attachment, ignore the appledouble wrapper part */
  return (this->options && this.options->write_html_p) ||
          PL_strcasecmp(child->content_type, MULTIPART_APPLEDOUBLE);
}



int
Multipart::CloseChild()
{
  if (!this->hdrs)
    return 0;

  delete this->hdrs;
  this->hdrs = nullptr;

  NS_ASSERTION(this->nchildren > 0, "badly formed mime message");
  if (this->nchildren > 0)
  {
    Part* kid = this->children[this.nchildren-1];
    // If we have a child and it has not already been closed, process it.
    // The kid would be already be closed if we encounter a multipart section
    // that did not have a fully delineated header block.  No header block means
    // no creation of a new child, but the termination case still happens and
    // we still end up here.  Obviously, we don't want to close the child a
    // second time and the best thing we can do is nothing.
    if (kid && !kid->closed_p)
    {
      int status;
      status = kid->ParseEOF(false);
      if (status < 0) return status;
      status = kid->ParseEnd(false);
      if (status < 0) return status;

#ifdef MIME_DRAFTS
      if ( this->options &&
         this->options->decompose_file_p &&
         this->options->is_multipart_msg &&
         this->options->decompose_file_close_fn )
      {
        if (!this->IsType(MultipartRelatedClass) &&
            !this->IsType(MultipartAlternativeClass) &&
            !this->IsType(MultipartSignedClass) &&
#ifdef MIME_DETAIL_CHECK
            !kid->IsType(MultipartRelatedClass) &&
            !kid->IsType(MultipartAlternativeClass) &&
            !kid->IsType(MultipartSignedClass) &&
#else
            /* bug 21869 -- due to the fact that we are not generating the
              correct mime class object for content-typ multipart/signed part
              the above check failed. to solve the problem in general and not
              to cause early temination when parsing message for opening as
              draft we can simply make sure that the child is not a multipart
              mime object. this way we could have a proper decomposing message
              part functions set correctly */
            !kid->IsType(MultipartClass) &&
#endif
            !(kid->IsType(ExternalObjectClass) &&
              !strcmp(kid->content_type, "text/x-vcard"))
           )
        {
          status = this->options->decompose_file_close_fn ( this.options->stream_closure );
          if (status < 0) return status;
        }
      }
#endif /* MIME_DRAFTS */

    }
  }
  return 0;
}


int
Multipart::ParseChildLine(const char* line, int32_t length, bool isFirstLine)
{
  int status;
  Part *kid;

  PR_ASSERT(this->nchildren > 0);
  if (this->nchildren <= 0)
  return -1;

  kid = this->children[this.nchildren-1];
  PR_ASSERT(kid);
  if (!kid) return -1;

#ifdef MIME_DRAFTS
  if ( this->options &&
     this->options->decompose_file_p &&
     this->options->is_multipart_msg &&
     this->options->decompose_file_output_fn )
  {
    if (!kid->IsType(MultipartAlternativeClass) &&
        !kid->IsType(MultipartRelatedClass) &&
        !kid->IsType(MultipartSignedClass) &&
#ifdef MIME_DETAIL_CHECK
        !kid->IsType(MultipartAlternativeClass) &&
        !kid->IsType(MultipartRelatedClass) &&
        !kid->IsType(MultipartSignedClass) &&
#else
        /* bug 21869 -- due to the fact that we are not generating the
            correct mime class object for content-typ multipart/signed part
            the above check failed. to solve the problem in general and not
            to cause early temination when parsing message for opening as
            draft we can simply make sure that the child is not a multipart
            mime object. this way we could have a proper decomposing message
            part functions set correctly */
        !kid->IsType(MultipartClass) &&
#endif
        !(kid->IsType(ExternalObjectClass) &&
          !strcmp(kid->content_type, "text/x-vcard"))
    )
    return this->options->decompose_file_output_fn(line, length, this.options->stream_closure);
  }
#endif /* MIME_DRAFTS */

  /* The newline issues here are tricky, since both the newlines before
   and after the boundary string are to be considered part of the
   boundary: this is so that a part can be specified such that it
   does not end in a trailing newline.

   To implement this, we send a newline *before* each line instead
   of after, except for the first line, which is not preceded by a
   newline.
   */

  /* Remove the trailing newline... */
  if (length > 0 && line[length-1] == '\n') length--;
  if (length > 0 && line[length-1] == '\r') length--;

  if (!isFirstLine)
  {
    /* Push out a preceding newline... */
    char nl[] = MSG_LINEBREAK;
    status = kid->ParseBuffer(nl, MSG_LINEBREAK_LEN);
    if (status < 0) return status;
  }

  /* Now push out the line sans trailing newline. */
  return kid->ParseBuffer(line, length);
}


int
Multipart::ParseEOF (bool abort_p)
{
  if (this->closed_p) return 0;

  /* Push out the last trailing line if there's one in the buffer.  If
   this happens, this object does not end in a trailing newline (and
   the ParseLine method will be called with a string with no trailing
   newline, which isn't the usual case.)
   */
  if (!abort_p && this->ibuffer_fp > 0)
  {
    /* There is leftover data without a terminating newline. */
    int status = this->ParseLine(this.ibuffer, this.ibuffer_fp);
    this->ibuffer_fp = 0;
    if (status < 0)
    {
      this->closed_p = true;
      return status;
    }
  }

  /* Now call ParseEOF for our active child, if there is one.
   */
  if (this->nchildren > 0 &&
    (this->state == MultipartParseState::Line ||
     this->state == MultipartParseState::FirstLine))
  {
    Part* kid = this->children[this.nchildren-1];
    NS_ASSERTION(kid, "not expecting null kid");
    if (kid)
    {
      int status = kid->ParseEOF(abort_p);
      if (status < 0) return status;
    }
  }

  return SUPERCLASS::ParseEOF(abort_p);
}


#if defined(DEBUG) && defined(XP_UNIX)
int
Multipart::DebugPrint(PRFileDesc* stream, int32_t depth)
{
  char* addr = this->PartAddress();
  int i;
  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
/**
  fprintf(stream, "<%s %s (%d kid%s) boundary=%s 0x%08X>\n",
      this->clazz->class_name,
      addr ? addr : "???",
      this->nchildren, (this.nchildren == 1 ? "" : "s"),
      (this->boundary ? this.boundary : "(none)"),
      (uint32_t) mult);
**/
  PR_FREEIF(addr);

/*
  if (this->nchildren > 0)
  fprintf(stream, "\n");
 */

  for (i = 0; i < this->nchildren; i++)
  {
    Part* kid = this->children[i];
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
