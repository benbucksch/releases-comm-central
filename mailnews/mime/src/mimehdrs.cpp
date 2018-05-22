/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsCOMPtr.h"
#include "msgCore.h"
#include "mimei.h"
#include "prmem.h"
#include "prlog.h"
#include "plstr.h"
#include "mimebuf.h"
#include "mimemoz2.h"
#include "nsMsgMessageFlags.h"
#include "comi18n.h"
#include "nsMailHeaders.h"
#include "msgCore.h"
#include "nsMimeStringResources.h"
#include "mimemoz2.h"
#include "nsMsgI18N.h"
#include "mimehdrs.h"
#include "nsIMIMEHeaderParam.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsMemory.h"
#include <ctype.h>
#include "nsMsgUtils.h"
#include "mozilla/Unused.h"

static void
Headers::ConvertHeaderValue(DisplayOptions* opt, nsCString &value,
                                 bool convert_charset_only)
{
  if (value.IsEmpty())
    return;

  if (convert_charset_only)
  {
    nsAutoCString output;
    nsMsgI18NConvertRawBytesToUTF8(value,
                                   nsDependentCString(opt->default_charset),
                                   output);
    value.Assign(output);
    return;
  }

  if (opt && opt->rfc1522_conversion_p)
  {
    nsAutoCString temporary;
    MIME_DecodeMimeHeader(value.get(), opt->default_charset,
                          opt->override_charset, true, temporary);

    if (!temporary.IsEmpty())
    {
      value = temporary;
    }
  }
  else
  {
    // This behavior, though highly unusual, was carefully preserved
    // from the previous implementation.  It may be that this is dead
    // code, in which case opt->rfc1522_conversion_p is no longer
    // needed.
    value.Truncate();
  }
}

Headers::Headers()
{
  this.done_p = false;
}

Headers::~Headers()
{
  PR_FREEIF(this.all_headers);
  PR_FREEIF(this.heads);
  PR_FREEIF(this.obuffer);
  PR_FREEIF(this.munged_subject);
  this.obuffer_fp = 0;
  this.obuffer_size = 0;
}

int
Headers::ParseLine(const char* buffer, int32_t size)
{
  int status = 0;
  int desired_size;

  /* Don't try and feed me more data after having fed me a blank line... */
  NS_ASSERTION(!this.done_p);
  if (this.done_p) return -1;

  if (!buffer || size == 0 || *buffer == '\r' || *buffer == '\n')
  {
    /* If this is a blank line, we're done.
     */
    this.done_p = true;
    return this.BuildHeadsList();
  }

  /* Tack this data on to the end of our copy.
   */
  desired_size = this.all_headers_fp + size + 1;
  if (desired_size >= this.all_headers_size)
  {
    status = mime_GrowBuffer(desired_size, sizeof(char), 255,
                 &this.all_headers, &this.all_headers_size);
    if (status < 0) return status;
  }
  memcpy(this.all_headers + this.all_headers_fp, buffer, size);
  this.all_headers_fp += size;

  return 0;
}

Headers* Headers::Copy()
{
  Headers *copy = (Headers*) PR_MALLOC(sizeof(this));
  if (!copy) return 0;
  memset(copy, 0, sizeof(*copy));

  if (this.all_headers)
  {
    copy->all_headers = (char *) PR_MALLOC(this.all_headers_fp);
    if (!copy->all_headers)
    {
      PR_Free(copy);
      return 0;
    }
    memcpy(copy->all_headers, this.all_headers, this.all_headers_fp);

    copy->all_headers_fp   = this.all_headers_fp;
    copy->all_headers_size = this.all_headers_fp;
  }

  copy->done_p = this.done_p;

  if (this.heads)
  {
    int i;
    copy->heads = (char **) PR_MALLOC(this.heads_size
                    * sizeof(*this.heads));
    if (!copy->heads)
    {
      PR_FREEIF(copy->all_headers);
      PR_Free(copy);
      return 0;
    }
    copy->heads_size = this.heads_size;
    for (i = 0; i < this.heads_size; i++)
    {
      copy->heads[i] = (copy->all_headers +
               (this.heads[i] - this.all_headers));
    }
  }
  return copy;
}

int
Headers::BuildHeadsList()
{
  char *s;
  char *end;
  int i;

  NS_ASSERTION(this.done_p && !this.heads);
  if (!this.done_p || this.heads)
  return -1;

  if (this.all_headers_fp == 0)
  {
    /* Must not have been any headers (we got the blank line right away.) */
    PR_FREEIF (this.all_headers);
    this.all_headers_size = 0;
    return 0;
  }

  /* At this point, we might as well realloc all_headers back down to the
   minimum size it must be (it could be up to 1k bigger.)  But don't
   bother if we're only off by a tiny bit. */
  NS_ASSERTION(this.all_headers_fp <= this.all_headers_size);
  if (this.all_headers_fp + 60 <= this.all_headers_size)
  {
    char *ls = (char *)PR_Realloc(this.all_headers, this.all_headers_fp);
    if (ls) /* can this ever fail?  we're making it smaller... */
    {
      this.all_headers = ls;  /* in case it got relocated */
      this.all_headers_size = this.all_headers_fp;
    }
  }

  /* First go through and count up the number of headers in the block.
   */
  end = this.all_headers + this.all_headers_fp;
  for (s = this.all_headers; s < end; s++)
  {
    if (s < (end-1) && s[0] == '\r' && s[1] == '\n') /* CRLF -> LF */
      s++;

    if ((s[0] == '\r' || s[0] == '\n') &&      /* we're at a newline, and */
      (s >= (end-1) ||            /* we're at EOF, or */
       !(s[1] == ' ' || s[1] == '\t')))    /* next char is nonwhite */
    this.heads_size++;
  }

  /* Now allocate storage for the pointers to each of those headers.
   */
  this.heads = (char **) PR_MALLOC((this.heads_size + 1) * sizeof(char *));
  if (!this.heads)
    return MIME_OUT_OF_MEMORY;
  memset(this.heads, 0, (this.heads_size + 1) * sizeof(char *));

  /* Now make another pass through the headers, and this time, record the
   starting position of each header.
   */

  i = 0;
  this.heads[i++] = this.all_headers;
  s = this.all_headers;

  while (s < end)
  {
  SEARCH_NEWLINE:
    while (s < end && *s != '\r' && *s != '\n')
      s++;

    if (s >= end)
      break;

    /* If "\r\n " or "\r\n\t" is next, that doesn't terminate the header. */
    else if (s+2 < end &&
         (s[0] == '\r'  && s[1] == '\n') &&
         (s[2] == ' ' || s[2] == '\t'))
    {
      s += 3;
      goto SEARCH_NEWLINE;
    }
    /* If "\r " or "\r\t" or "\n " or "\n\t" is next, that doesn't terminate
     the header either. */
    else if (s+1 < end &&
         (s[0] == '\r'  || s[0] == '\n') &&
         (s[1] == ' ' || s[1] == '\t'))
    {
      s += 2;
      goto SEARCH_NEWLINE;
    }

    /* At this point, `s' points before a header-terminating newline.
     Move past that newline, and store that new position in `heads'.
     */
    if (*s == '\r')
      s++;

    if (s >= end)
      break;

    if (*s == '\n')
      s++;

    if (s < end)
    {
      NS_ASSERTION(! (i > this.heads_size));
      if (i > this.heads_size)
        return -1;
      this.heads[i++] = s;
    }
  }

  return 0;
}

char*
Headers::Get(const char* header_name, bool strip_p, bool all_p)
{
  int i;
  int name_length;
  char *result = 0;

  NS_ASSERTION(header_name);
  if (!header_name) return 0;

  /* Specifying strip_p and all_p at the same time doesn't make sense... */
  NS_ASSERTION(!(strip_p && all_p));

  /* One shouldn't be trying to read headers when one hasn't finished
   parsing them yet... but this can happen if the message ended
   prematurely, and has no body at all (as opposed to a null body,
   which is more normal.)   So, if we try to read from the headers,
   let's assume that the headers are now finished.  If they aren't
   in fact finished, then a later attempt to write to them will assert.
   */
  if (!this.done_p)
  {
    int status;
    this.done_p = true;
    status = this.BuildHeadsList();
    if (status < 0) return 0;
  }

  if (!this.heads)    /* Must not have been any headers. */
  {
    NS_ASSERTION(this.all_headers_fp == 0);
    return 0;
  }

  name_length = strlen(header_name);

  for (i = 0; i < this.heads_size; i++)
  {
    char *head = this.heads[i];
    char *end = (i == this.heads_size-1
           ? this.all_headers + this.all_headers_fp
           : this.heads[i+1]);
    char *colon, *ocolon;

    NS_ASSERTION(head);
    if (!head) continue;

    /* Quick hack to skip over BSD Mailbox delimiter. */
    if (i == 0 && head[0] == 'F' && !strncmp(head, "From ", 5))
    continue;

    /* Find the colon. */
    for (colon = head; colon < end; colon++)
    if (*colon == ':') break;

    if (colon >= end) continue;

    /* Back up over whitespace before the colon. */
    ocolon = colon;
    for (; colon > head && IS_SPACE(colon[-1]); colon--)
    ;

    /* If the strings aren't the same length, it doesn't match. */
    if (name_length != colon - head )
    continue;

    /* If the strings differ, it doesn't match. */
    if (PL_strncasecmp(header_name, head, name_length))
    continue;

    /* Otherwise, we've got a match. */
    {
    char *contents = ocolon + 1;
    char *s;

    /* Skip over whitespace after colon. */
    while (contents < end && IS_SPACE(contents[0])) {
      /* Mac or Unix style line break, followed by space or tab. */
      if (contents < (end - 1) &&
         (contents[0] == '\r' || contents[0] == '\n') &&
         (contents[1] == ' ' || contents[1] == '\t'))
        contents += 2;
      /* Windows style line break, followed by space or tab. */
      else if (contents < (end - 2) &&
               contents[0] == '\r' && contents[1] == '\n' &&
              (contents[2] == ' ' || contents[2] == '\t'))
        contents += 3;
      /* Any space or tab. */
      else if (contents[0] == ' ' || contents[0] == '\t')
        contents++;
      /* If we get here, it's because this character is a line break
         followed by non-whitespace, or a line break followed by
         another line break
       */
      else {
        end = contents;
        break;
      }
    }

    /* If we're supposed to strip at the first token, pull `end' back to
       the first whitespace or ';' after the first token.
     */
    if (strip_p)
      {
      for (s = contents;
         s < end && *s != ';' && *s != ',' && !IS_SPACE(*s);
         s++)
        ;
      end = s;
      }

    /* Now allocate some storage.
       If `result' already has a value, enlarge it.
       Otherwise, just allocate a block.
       `s' gets set to the place where the new data goes.
     */
    if (!result)
      {
      result = (char *) PR_MALLOC(end - contents + 1);
      if (!result)
        return 0;
      s = result;
      }
    else
      {
      int32_t L = strlen(result);
      s = (char *) PR_Realloc(result, (L + (end - contents + 10)));
      if (!s)
        {
        PR_Free(result);
        return 0;
        }
      result = s;
      s = result + L;

      /* Since we are tacking more data onto the end of the header
         field, we must make it be a well-formed continuation line,
                          by separating the old and new data with CR-LF-TAB.
       */
      *s++ = ',';        /* #### only do this for addr headers? */
      *s++ = MSG_LINEBREAK[0];
# if (MSG_LINEBREAK_LEN == 2)
      *s++ = MSG_LINEBREAK[1];
# endif
      *s++ = '\t';
      }


    /* Take off trailing whitespace... */
    while (end > contents && IS_SPACE(end[-1]))
      end--;

    if (end > contents)
      {
        /* Now copy the header's contents in...
         */
        memcpy(s, contents, end - contents);
        s[end - contents] = 0;
      }
    else
      {
        s[0] = 0;
      }

    /* If we only wanted the first occurrence of this header, we're done. */
    if (!all_p) break;
    }
  }

  if (result && !*result)  /* empty string */
  {
    PR_Free(result);
    return 0;
  }

  return result;
}

char *
MimeHeaders_get_parameter (const char *header_value, const char *parm_name,
                           char **charset, char **language)
{
  if (!header_value || !parm_name || !*header_value || !*parm_name)
    return nullptr;

  nsresult rv;
  nsCOMPtr <nsIMIMEHeaderParam> mimehdrpar =
    do_GetService(NS_MIMEHEADERPARAM_CONTRACTID, &rv);

  if (NS_FAILED(rv))
    return nullptr;

  nsCString result;
  rv = mimehdrpar->GetParameterInternal(header_value, parm_name, charset,
                                        language, getter_Copies(result));
  return NS_SUCCEEDED(rv) ? PL_strdup(result.get()) : nullptr;
}

#define MimeHeaders_write(HDRS,OPT,DATA,LENGTH) \
    MimeOptions_write((HDRS), (OPT), (DATA), (LENGTH), true);


#define MimeHeaders_grow_obuffer(hdrs, desired_size) \
  ((((long) (desired_size)) >= ((long) (hdrs)->obuffer_size)) ? \
   mime_GrowBuffer ((desired_size), sizeof(char), 255, \
           &(hdrs)->obuffer, &(hdrs)->obuffer_size) \
   : 0)

int
Headers::WriteAllHeaders(DisplayOptions* opt, bool attachment)
{
  int status = 0;
  int i;
  bool wrote_any_p = false;

  /* One shouldn't be trying to read headers when one hasn't finished
     parsing them yet... but this can happen if the message ended
     prematurely, and has no body at all (as opposed to a null body,
     which is more normal.)   So, if we try to read from the headers,
     let's assume that the headers are now finished.  If they aren't
     in fact finished, then a later attempt to write to them will assert.
   */
  if (!this.done_p)
  {
    this.done_p = true;
    status = this.BuildHeadsList();
    if (status < 0) return 0;
  }

  char *charset = nullptr;
  if (opt->format_out == nsMimeOutput::nsMimeMessageSaveAs)
  {
    if (opt->override_charset)
      charset = PL_strdup(opt->default_charset);
    else
    {
      char *contentType = this.Get(HEADER_CONTENT_TYPE, false, false);
      if (contentType)
        charset = GetParameter(contentType, HEADER_PARM_CHARSET, nullptr, nullptr);
      PR_FREEIF(contentType);
    }
  }

  for (i = 0; i < this.heads_size; i++)
  {
    char *head = this.heads[i];
    char *end = (i == this.heads_size-1
                      ? this.all_headers + this.all_headers_fp
                      : this.heads[i+1]);
    char *colon, *ocolon;
    char *contents = end;

    /* Hack for BSD Mailbox delimiter. */
    if (i == 0 && head[0] == 'F' && !strncmp(head, "From ", 5))
    {
      /* For now, we don't really want this header to be output so
         we are going to just continue */
      continue;
      /* colon = head + 4; contents = colon + 1; */
    }
    else
    {
      /* Find the colon. */
      for (colon = head; colon < end && *colon != ':'; colon++)
        ;

        /* Back up over whitespace before the colon. */
        ocolon = colon;
        for (; colon > head && IS_SPACE(colon[-1]); colon--)
          ;

        contents = ocolon + 1;
    }

    /* Skip over whitespace after colon. */
    while (contents < end && IS_SPACE(*contents))
      contents++;

    /* Take off trailing whitespace... */
    while (end > contents && IS_SPACE(end[-1]))
      end--;

    nsAutoCString name(Substring(head, colon));
    nsAutoCString hdr_value;

    if ( (end - contents) > 0 )
    {
      hdr_value = Substring(contents, end);
    }

    // MW Fixme: more?
    bool convert_charset_only =
          MsgLowerCaseEqualsLiteral(name, "to") || MsgLowerCaseEqualsLiteral(name, "from") ||
          MsgLowerCaseEqualsLiteral(name, "cc") || MsgLowerCaseEqualsLiteral(name, "bcc") ||
          MsgLowerCaseEqualsLiteral(name, "reply-to") || MsgLowerCaseEqualsLiteral(name, "sender");
    Headers::ConvertHeaderValue(hdr_value, opt, convert_charset_only);
    // if we're saving as html, we need to convert headers from utf8 to message charset, if any
    if (opt->format_out == nsMimeOutput::nsMimeMessageSaveAs && charset)
    {
      nsAutoCString convertedStr;
      if (NS_SUCCEEDED(nsMsgI18NConvertFromUnicode(nsDependentCString(charset),
                       NS_ConvertUTF8toUTF16(hdr_value),
                       convertedStr)))
      {
        hdr_value = convertedStr;
      }
    }

    if (attachment) {
      if (NS_FAILED(mimeEmitterAddAttachmentField(opt, name.get(), hdr_value.get())))
        status = -1;
    }
    else {
      if (NS_FAILED(mimeEmitterAddHeaderField(opt, name.get(), hdr_value.get())))
        status = -1;
    }

    if (status < 0) return status;
    if (!wrote_any_p)
      wrote_any_p = (status > 0);
  }
  mimeEmitterAddAllHeaders(opt, this.all_headers, this.all_headers_fp);
  PR_FREEIF(charset);

  return 1;
}

/* Strip CR+LF runs within (original).
   Since the string at (original) can only shrink,
   this conversion is done in place. (original)
   is returned. */
extern "C" char *
MIME_StripContinuations(char *original)
{
  char *p1, *p2;

  /* If we were given a null string, return it as is */
  if (!original) return NULL;

  /* Start source and dest pointers at the beginning */
  p1 = p2 = original;

  while (*p2) {
    /* p2 runs ahead at (CR and/or LF) */
    if ((p2[0] == '\r') || (p2[0] == '\n'))
      p2++;
    else if (p2 > p1)
      *p1++ = *p2++;
    else {
      p1++;
      p2++;
    }
  }
  *p1 = '\0';

  return original;
}

extern int16_t INTL_DefaultMailToWinCharSetID(int16_t csid);

/* Given text purporting to be a qtext header value, strip backslashes that
  may be escaping other chars in the string. */
char *
Headers::DecodeFilename(const char* name, const char* charset, DisplayOptions* opt)
{
  nsresult rv;
  nsCOMPtr <nsIMIMEHeaderParam> mimehdrpar =
    do_GetService(NS_MIMEHEADERPARAM_CONTRACTID, &rv);

  if (NS_FAILED(rv))
    return nullptr;
  nsAutoCString result;
  rv = mimehdrpar->DecodeParameter(nsDependentCString(name), charset,
                                   opt ? opt->default_charset : nullptr,
                                   opt ? opt->override_charset : false,
                                   result);
  return NS_SUCCEEDED(rv) ? PL_strdup(result.get()) : nullptr;
}

/* Pull the name out of some header or another.  Order is:
   Content-Disposition: XXX; filename=NAME (RFC 1521/1806)
   Content-Type: XXX; name=NAME (RFC 1341)
   Content-Name: NAME (no RFC, but seen to occur)
   X-Sun-Data-Name: NAME (no RFC, but used by MailTool)
 */
char*
Headers::GetFilename(DisplayOptions* opt)
{
  char *s = 0, *name = 0, *cvt = 0;
  char *charset = nullptr; // for RFC2231 support

  s = this.Get(HEADER_CONTENT_DISPOSITION, false, false);
  if (s)
  {
    name = Header::GetParameter(s, HEADER_PARM_FILENAME, &charset, NULL);
    PR_Free(s);
  }

  if (! name)
  {
    s = this.Get(HEADER_CONTENT_TYPE, false, false);
    if (s)
    {
      free(charset);

      name = Header::GetParameter(s, HEADER_PARM_NAME, &charset, NULL);
      PR_Free(s);
    }
  }

  if (! name)
    name = this.Get(HEADER_CONTENT_NAME, false, false);

  if (! name)
    name = this.Get(HEADER_X_SUN_DATA_NAME, false, false);

  if (name)
  {
    /* First remove continuation delimiters (CR+LF+space), then
       remove escape ('\\') characters, then attempt to decode
       mime-2 encoded-words. The latter two are done in
       mime_decode_filename.
    */
    MIME_StripContinuations(name);

    /* Argh. What we should do if we want to be robust is to decode qtext
       in all appropriate headers. Unfortunately, that would be too scary
       at this juncture. So just decode qtext/mime2 here. */
    cvt = Headers::DecodeFilename(name, charset, opt);

    free(charset);

    if (cvt && cvt != name)
    {
      PR_Free(name);
      name = cvt;
    }
  }

  return name;
}

#ifdef XP_UNIX
/* This piece of junk is so that I can use BBDB with Mozilla.
   = Put bbdb-srv.perl on your path.
   = Put bbdb-srv.el on your lisp path.
   = Make sure gnudoit (comes with xemacs) is on your path.
   = Put (gnuserv-start) in ~/.emacs
   = setenv NS_MSG_DISPLAY_HOOK bbdb-srv.perl
 */
void
Headers::DoUnixDisplayHookHack()
{
  static const char *cmd = 0;
  if (!cmd)
  {
  /* The first time we're invoked, look up the command in the
    environment.  Use "" as the `no command' tag. */
    cmd = getenv("NS_MSG_DISPLAY_HOOK");
    if (!cmd)
      cmd = "";
  }

  /* Invoke "cmd" at the end of a pipe, and give it the headers on stdin.
   The command is expected to be safe from hostile input!!
  */
  if (cmd && *cmd)
  {
    FILE *fp = popen(cmd, "w");
    if (fp)
    {
      mozilla::Unused << fwrite(this.all_headers, 1, this.all_headers_fp, fp);
      pclose(fp);
    }
  }
}
#endif /* XP_UNIX */

void
Headers::Compact()
{
  PR_FREEIF(this.obuffer);
  this.obuffer_fp = 0;
  this.obuffer_size = 0;

  /* These really shouldn't have gotten out of whack again. */
  NS_ASSERTION(this.all_headers_fp <= this.all_headers_size &&
            this.all_headers_fp + 100 > this.all_headers_size);
}

/* Writes the headers as text/plain.
   This writes out a blank line after the headers, unless
   dont_write_content_type is true, in which case the header-block
   is not closed off, and none of the Content- headers are written.
 */
int
Headers::WriteRawHeaders(DisplayOptions* opt, bool dont_write_content_type)
{
  int status;

  if (!this.done_p)
  {
    this.done_p = true;
    status = this.BuildHeadsList();
    if (status < 0) return 0;
  }

  if (!dont_write_content_type)
  {
    char nl[] = MSG_LINEBREAK;
    status = this.Write(opt, this.all_headers, this.all_headers_fp);
    if (status < 0) return status;
    status = this.Write(opt, nl, strlen(nl));
    if (status < 0) return status;
  }
  else
  {
    int32_t i;
    for (i = 0; i < this.heads_size; i++)
    {
      char *head = this.heads[i];
      char *end = (i == this.heads_size-1
             ? this.all_headers + this.all_headers_fp
             : this.heads[i+1]);

      NS_ASSERTION(head);
      if (!head) continue;

      /* Don't write out any Content- header. */
      if (!PL_strncasecmp(head, "Content-", 8))
      continue;

      /* Write out this (possibly multi-line) header. */
      status = this.Write(opt, head, end - head);
      if (status < 0) return status;
    }
  }

  this.Compact();

  return 0;
}
