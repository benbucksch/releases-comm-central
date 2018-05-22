/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsCOMPtr.h"
#include "mimeebod.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "prio.h"
#include "msgCore.h"
#include "nsMimeStringResources.h"
#include "mimemoz2.h"
#include "nsComponentManagerUtils.h"
#include "nsMsgUtils.h"
#include "nsINetUtil.h"
#include <ctype.h>

namespace mozilla::mime {

#define SUPERCLASS Part

#ifdef XP_MACOSX
extern PartClass MultipartAppleDoubleClass;
#endif

ExternalBody::~ExternalBody()
{
  if (this.hdrs)
  {
    delete this.hdrs;
    this.hdrs = nullptr;
  }
  PR_FREEIF(this.body);
}

int
ExternalBody::ParseLine(const char* line, int32_t length)
{
  int status = 0;
  if (!line || !*line) return -1;

  if (!this.output_p) return 0;

  /* If we're supposed to write this object, but aren't supposed to convert
   it to HTML, simply pass it through unaltered. */
  if (this.options &&
    !this.options->write_html_p &&
    this.options->output_fn)
  return this.Write(line, length, true);


  /* If we already have a `body' then we're done parsing headers, and all
   subsequent lines get tacked onto the body. */
  if (this.body)
  {
    int L = strlen(this.body);
    char *new_str = (char *)PR_Realloc(this.body, L + length + 1);
    if (!new_str) return MIME_OUT_OF_MEMORY;
    this.body = new_str;
    memcpy(this.body + L, line, length);
    this.body[L + length] = 0;
    return 0;
  }

  /* Otherwise we don't yet have a body, which means we're not done parsing
   our headers.
   */
  if (!this.hdrs)
  {
    this.hdrs = new Headers();
    if (!this.hdrs) return MIME_OUT_OF_MEMORY;
  }

  status = this.hdrs->ParseLine(line, length);
  if (status < 0) return status;

  /* If this line is blank, we're now done parsing headers, and should
   create a dummy body to show that.  Gag.
   */
  if (*line == '\r' || *line == '\n')
  {
    this.body = strdup("");
    if (!this.body) return MIME_OUT_OF_MEMORY;
  }

  return 0;
}


char *
MimeExternalBody_make_url(const char *ct,
              const char *at, const char *lexp, const char *size,
              const char *perm, const char *dir, const char *mode,
              const char *name, const char *url, const char *site,
              const char *svr, const char *subj, const char *body)
{
  char *s;
  uint32_t slen;
  if (!at)
  {
    return 0;
  }
  else if (!PL_strcasecmp(at, "ftp") || !PL_strcasecmp(at, "anon-ftp"))
  {
    if (!site || !name)
      return 0;
	
    slen = strlen(name) + strlen(site) + (dir ? strlen(dir) : 0) + 20;
    s = (char *) PR_MALLOC(slen);

    if (!s) return 0;
    PL_strncpyz(s, "ftp://", slen);
    PL_strcatn(s, slen, site);
    PL_strcatn(s, slen, "/");
    if (dir) PL_strcatn(s, slen, (dir[0] == '/' ? dir+1 : dir));
    if (s[strlen(s)-1] != '/')
      PL_strcatn(s, slen, "/");
    PL_strcatn(s, slen, name);
    return s;
  }
  else if (!PL_strcasecmp(at, "local-file") || !PL_strcasecmp(at, "afs"))
  {
    if (!name)
      return 0;

#ifdef XP_UNIX
    if (!PL_strcasecmp(at, "afs"))   /* only if there is a /afs/ directory */
    {
      nsCOMPtr <nsIFile> fs = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID);
      bool exists = false;
      if (fs)
      {
        fs->InitWithNativePath(NS_LITERAL_CSTRING("/afs/."));
        fs->Exists(&exists);
      }
      if  (!exists)
        return 0;
    }
#else  /* !XP_UNIX */
    return 0;            /* never, if not Unix. */
#endif /* !XP_UNIX */

    slen = (strlen(name) * 3 + 20);
    s = (char *) PR_MALLOC(slen);
    if (!s) return 0;
    PL_strncpyz(s, "file:", slen);

    nsCString s2;
    MsgEscapeString(nsDependentCString(name), nsINetUtil::ESCAPE_URL_PATH, s2);
    PL_strcatn(s, slen, s2.get());
    return s;
  }
else if (!PL_strcasecmp(at, "mail-server"))
{
  if (!svr)
    return 0;
	
  slen =  (strlen(svr)*4 + (subj ? strlen(subj)*4 : 0) +
                         (body ? strlen(body)*4 : 0) + 25); // dpv xxx: why 4x? %xx escaping should be 3x
  s = (char *) PR_MALLOC(slen);
  if (!s) return 0;
  PL_strncpyz(s, "mailto:", slen);

  nsCString s2;
  MsgEscapeString(nsDependentCString(svr), nsINetUtil::ESCAPE_XALPHAS, s2);
  PL_strcatn(s, slen, s2.get());

  if (subj)
    {
      MsgEscapeString(nsDependentCString(subj), nsINetUtil::ESCAPE_XALPHAS, s2);
      PL_strcatn(s, slen, "?subject=");
      PL_strcatn(s, slen, s2.get());
    }
  if (body)
    {
      MsgEscapeString(nsDependentCString(body), nsINetUtil::ESCAPE_XALPHAS, s2);
      PL_strcatn(s, slen, (subj ? "&body=" : "?body="));
      PL_strcatn(s, slen, s2.get());
    }
  return s;
}
else if (!PL_strcasecmp(at, "url"))      /* RFC 2017 */
                            {
  if (url)
    return strdup(url);       /* it's already quoted and everything */
  else
    return 0;
                            }
                            else
                            return 0;
}

int
ExternalBody::ParseEOF(bool abort_p)
{
  int status = 0;
  if (this.closed_p) return 0;

  /* Run parent method first, to flush out any buffered data. */
  status = SUPERCLASS::ParseEOF(abort_p);
  if (status < 0) return status;

#ifdef XP_MACOSX
  if (this.parent && this.parent->IsType(MultipartAppleDoubleClass))
    goto done;
#endif /* XP_MACOSX */

  if (!abort_p &&
      this.output_p &&
      this.options &&
      this.options->write_html_p)
  {
    bool all_headers_p = this.options->headers == HeadersState::All;
    DisplayOptions* newopt = this.options;  /* copy it */

    char *ct = this.headers->Get(HEADER_CONTENT_TYPE, false, false);
    char *at, *lexp, *size, *perm;
    char *url, *dir, *mode, *name, *site, *svr, *subj;
    char *h = 0, *lname = 0, *lurl = 0, *body = 0;
    Headers* hdrs = 0;

    if (!ct) return MIME_OUT_OF_MEMORY;

    at   = Headers::GetParameter(ct, "access-type", NULL, NULL);
    lexp  = Headers::GetParameter(ct, "expiration", NULL, NULL);
    size = Headers::GetParameter(ct, "size", NULL, NULL);
    perm = Headers::GetParameter(ct, "permission", NULL, NULL);
    dir  = Headers::GetParameter(ct, "directory", NULL, NULL);
    mode = Headers::GetParameter(ct, "mode", NULL, NULL);
    name = Headers::GetParameter(ct, "name", NULL, NULL);
    site = Headers::GetParameter(ct, "site", NULL, NULL);
    svr  = Headers::GetParameter(ct, "server", NULL, NULL);
    subj = Headers::GetParameter(ct, "subject", NULL, NULL);
    url  = Headers::GetParameter(ct, "url", NULL, NULL);
    PR_FREEIF(ct);

    /* the *internal* content-type */
    ct = this.hdrs->Get(HEADER_CONTENT_TYPE, true, false);

    uint32_t hlen = ((at ? strlen(at) : 0) +
                    (lexp ? strlen(lexp) : 0) +
                    (size ? strlen(size) : 0) +
                    (perm ? strlen(perm) : 0) +
                    (dir ? strlen(dir) : 0) +
                    (mode ? strlen(mode) : 0) +
                    (name ? strlen(name) : 0) +
                    (site ? strlen(site) : 0) +
                    (svr ? strlen(svr) : 0) +
                    (subj ? strlen(subj) : 0) +
                    (ct ? strlen(ct) : 0) +
                    (url ? strlen(url) : 0) + 100);
					
	h = (char *) PR_MALLOC(hlen);
    if (!h)
    {
      status = MIME_OUT_OF_MEMORY;
      goto FAIL;
    }

    /* If there's a URL parameter, remove all whitespace from it.
      (The URL parameter to one of these headers is stored with
       lines broken every 40 characters or less; it's assumed that
       all significant whitespace was URL-hex-encoded, and all the
       rest of it was inserted just to keep the lines short.)
      */
    if (url)
    {
      char *in, *out;
      for (in = url, out = url; *in; in++)
        if (!IS_SPACE(*in))
          *out++ = *in;
      *out = 0;
    }

    hdrs = new Headers();
    if (!hdrs)
    {
      status = MIME_OUT_OF_MEMORY;
      goto FAIL;
    }

# define FROB(STR,VAR) \
    if (VAR) \
    { \
      PL_strncpyz(h, STR ": ", hlen); \
        PL_strcatn(h, hlen, VAR); \
          PL_strcatn(h, hlen, MSG_LINEBREAK); \
            status = MimeHeaders_ParseLine(h, strlen(h), hdrs); \
              if (status < 0) goto FAIL; \
    }
    FROB("Access-Type",  at);
    FROB("URL",      url);
    FROB("Site",      site);
    FROB("Server",    svr);
    FROB("Directory",    dir);
    FROB("Name",      name);
    FROB("Type",      ct);
    FROB("Size",      size);
    FROB("Mode",      mode);
    FROB("Permission",  perm);
    FROB("Expiration",  lexp);
    FROB("Subject",    subj);
# undef FROB
    PL_strncpyz(h, MSG_LINEBREAK, hlen);
    status = hdrs->ParseLine(h, strlen(h));
    if (status < 0) goto FAIL;

    lurl = MimeExternalBody_make_url(ct, at, lexp, size, perm, dir, mode,
                                     name, url, site, svr, subj, this.body);
    if (lurl)
    {
      lname = MimeGetStringByID(MIME_MSG_LINK_TO_DOCUMENT);
    }
    else
    {
      lname = MimeGetStringByID(MIME_MSG_DOCUMENT_INFO);
      all_headers_p = true;
    }

    all_headers_p = true;  /* #### just do this all the time? */

    if (this.body && all_headers_p)
    {
      char *s = this.body;
      while (IS_SPACE(*s)) s++;
      if (*s)
      {
        const char *pre = "<P><PRE>";
        const char *suf = "</PRE>";
        int32_t i;
        for(i = strlen(s)-1; i >= 0 && IS_SPACE(s[i]); i--)
          s[i] = 0;
        nsCString s2;
        nsAppendEscapedHTML(nsDependentCString(s), s2);
        body = (char *) PR_MALLOC(strlen(pre) + s2.Length() +
                                  strlen(suf) + 1);
        if (!body)
        {
          goto FAIL;
        }
        PL_strcpy(body, pre);
        PL_strcat(body, s2.get());
        PL_strcat(body, suf);
      }
    }

    newopt->fancy_headers_p = true;
    newopt->headers = (all_headers_p ? HeadersState::All : HeadersState::Some);

FAIL:
    if (hdrs)
      delete hdrs;
    PR_FREEIF(h);
    PR_FREEIF(lname);
    PR_FREEIF(lurl);
    PR_FREEIF(body);
    PR_FREEIF(ct);
    PR_FREEIF(at);
    PR_FREEIF(lexp);
    PR_FREEIF(size);
    PR_FREEIF(perm);
    PR_FREEIF(dir);
    PR_FREEIF(mode);
    PR_FREEIF(name);
    PR_FREEIF(url);
    PR_FREEIF(site);
    PR_FREEIF(svr);
    PR_FREEIF(subj);
  }

#ifdef XP_MACOSX
done:
#endif

    return status;
}

#if 0
#if defined(DEBUG) && defined(XP_UNIX)
static int
ExternalBody::DebugPrint(PRFileDesc* stream, int32_t depth)
{
  int i;
  char *ct, *ct2;
  char *addr = this.PartAddress();

  if (this.headers)
  ct = this.headers->Get(HEADER_CONTENT_TYPE, false, false);
  if (this.hdrs)
  ct2 = this.hdrs->Get(HEADER_CONTENT_TYPE, false, false);

  for (i=0; i < depth; i++)
  PR_Write(stream, "  ", 2);
/***
  fprintf(stream,
      "<%s %s\n"
      "\tcontent-type: %s\n"
      "\tcontent-type: %s\n"
      "\tBody:%s\n\t0x%08X>\n\n",
      this.clazz->class_name,
      addr ? addr : "???",
      ct ? ct : "<none>",
      ct2 ? ct2 : "<none>",
      this.body ? this.body : "<none>",
      (uint32_t) obj);
***/
  PR_FREEIF(addr);
  PR_FREEIF(ct);
  PR_FREEIF(ct2);
  return 0;
}
#endif
#endif // 0

bool
ExternalBodyClass::DisplayableInline(Headers* hdrs)
{
  char *ct = hdrs->Get(HEADER_CONTENT_TYPE, false, false);
  char *at = Headers::GetParameter(ct, "access-type", NULL, NULL);
  bool inline_p = false;

  if (!at)
  ;
  else if (!PL_strcasecmp(at, "ftp") ||
       !PL_strcasecmp(at, "anon-ftp") ||
       !PL_strcasecmp(at, "local-file") ||
       !PL_strcasecmp(at, "mail-server") ||
       !PL_strcasecmp(at, "url"))
  inline_p = true;
#ifdef XP_UNIX
  else if (!PL_strcasecmp(at, "afs"))   /* only if there is a /afs/ directory */
  {
    nsCOMPtr <nsIFile> fs = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID);
    bool exists = false;
    if (fs)
    {
      fs->InitWithNativePath(NS_LITERAL_CSTRING("/afs/."));
      fs->Exists(&exists);
    }
    if  (!exists)
      return 0;

    inline_p = true;
  }
#endif /* XP_UNIX */

  PR_FREEIF(ct);
  PR_FREEIF(at);
  return inline_p;
}


} // namespace
