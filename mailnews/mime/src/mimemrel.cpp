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

/* Thoughts on how to implement this:

   = if the type of this multipart/related is not text/html, then treat
     it the same as multipart/mixed.

   = For each part in this multipart/related
     = if this part is not the "top" part
       = then save this part to a tmp file or a memory object,
         kind-of like what we do for multipart/alternative sub-parts.
         If this is an object we're blocked on (see below) send its data along.
     = else
       = emit this part (remember, it's of type text/html)
       = at some point, layout may load a URL for <IMG SRC="cid:xxxx">.
         we intercept that.
         = if one of our cached parts has that cid, return the data for it.
         = else, "block", the same way the image library blocks layout when it
           doesn't yet have the size of the image.
       = at some point, layout may load a URL for <IMG SRC="relative/yyy">.
         we need to intercept that too.
         = expand the URL, and compare it to our cached objects.
           if it matches, return it.
         = else block on it.

   = once we get to the end, if we have any sub-part references that we're
     still blocked on, map over them:
     = if they're cid: references, close them ("broken image" results.)
     = if they're URLs, then load them in the normal way.

  --------------------------------------------------

  Ok, that's fairly complicated.  How about an approach where we go through
  all the parts first, and don't emit until the end?

   = if the type of this multipart/related is not text/html, then treat
     it the same as multipart/mixed.

   = For each part in this multipart/related
       = save this part to a tmp file or a memory object,
         like what we do for multipart/alternative sub-parts.

   = Emit the "top" part (the text/html one)
     = intercept all calls to NET_GetURL, to allow us to rewrite the URL.
       (hook into netlib, or only into imglib's calls to GetURL?)
       (make sure we're behaving in a context-local way.)

       = when a URL is loaded, look through our cached parts for a match.
         = if we find one, map that URL to a "cid:" URL
         = else, let it load normally

     = at some point, layout may load a URL for <IMG SRC="cid:xxxx">.
       it will do this either because that's what was in the HTML, or because
       that's how we "rewrote" the URLs when we intercepted NET_GetURL.

       = if one of our cached parts has the requested cid, return the data
         for it.
       = else, generate a "broken image"

   = free all the cached data

  --------------------------------------------------

  How hard would be an approach where we rewrite the HTML?
  (Looks like it's not much easier, and might be more error-prone.)

   = if the type of this multipart/related is not text/html, then treat
     it the same as multipart/mixed.

   = For each part in this multipart/related
       = save this part to a tmp file or a memory object,
         like what we do for multipart/alternative sub-parts.

   = Parse the "top" part, and emit slightly different HTML:
     = for each <IMG SRC>, <IMG LOWSRC>, <A HREF>?  Any others?
       = look through our cached parts for a matching URL
         = if we find one, map that URL to a "cid:" URL
         = else, let it load normally

     = at some point, layout may load a URL for <IMG SRC="cid:xxxx">.
       = if one of our cached parts has the requested cid, return the data
         for it.
       = else, generate a "broken image"

   = free all the cached data
 */
#include "nsCOMPtr.h"
#include "mimemrel.h"
#include "mimemapl.h"
#include "prmem.h"
#include "prprf.h"
#include "prlog.h"
#include "plstr.h"
#include "mimemoz2.h"
#include "nsString.h"
#include "msgCore.h"
#include "nsMimeStringResources.h"
#include "nsMimeTypes.h"
#include "mimebuf.h"
#include "nsMsgUtils.h"
#include <ctype.h>

//
// External Defines...
//

extern nsresult
nsMsgCreateTempFile(const char *tFileName, nsIFile **tFile);

namespace mozilla {
namespace mime {

class MimeHashValue
{
public:
  MimeHashValue(Part *obj, char *url) {
    m_obj = obj;
    m_url = strdup(url);
  }
  virtual ~MimeHashValue() {
    if (m_url)
      PR_Free((void *)m_url);
  }

  Part  *m_obj;
  char        *m_url;
};

MultipartRelated::MultipartRelated()
  : base_url(nullptr)
  , head_buffer(nullptr)
  , head_buffer_fp(0)
  , head_buffer_size(0)
  , buffered_hdrs(nullptr)
  , head_loaded(false)
  , headobj(nullptr)
  , hash(nullptr)
  , real_output_fn(nullptr)
  , real_output_closure(nullptr)
  , curtag(nullptr)
  , curtag_max(0)
  , curtag_length(0)
{
  this->base_url = this->headers->Get(HEADER_CONTENT_BASE,
                     false, false);
  /* rhp: need this for supporting Content-Location */
  if (!this->base_url)
  {
    this->base_url = this->headers->Get(HEADER_CONTENT_LOCATION,
      false, false);
  }
  /* rhp: need this for supporting Content-Location */

  /* I used to have code here to test if the type was text/html.  Then I
     added multipart/alternative as being OK, too.  Then I found that the
     VCard spec seems to talk about having the first part of a
     multipart/related be an application/directory.  At that point, I decided
     to punt.  We handle anything as the first part, and stomp on the HTML it
     generates to adjust tags to point into the other parts.  This probably
     works out to something reasonable in most cases. */

  this->hash = PL_NewHashTable(20, PL_HashString, PL_CompareStrings, PL_CompareValues,
                                (PLHashAllocOps *)NULL, NULL);
}

int
MultipartRelated::NukeHash(PLHashEntry* table, int indx, void* arg)
{
  if (table->key)
    PR_Free((char*) table->key);

  if (table->value)
    delete (MimeHashValue *)table->value;

  return HT_ENUMERATE_NEXT;   /* XP_Maphash will continue traversing the hash */
}

MultipartRelated::~MultipartRelated()
{
  PR_FREEIF(this->base_url);
  PR_FREEIF(this->curtag);
  if (this->buffered_hdrs) {
    delete this->buffered_hdrs;
  }
  PR_FREEIF(this->head_buffer);
  this->head_buffer_fp = 0;
  this->head_buffer_size = 0;
  if (this->hash) {
    PL_HashTableEnumerateEntries(this->hash, NukeHash, NULL);
    PL_HashTableDestroy(this->hash);
    this->hash = NULL;
  }

  if (this->input_file_stream)
  {
    this->input_file_stream->Close();
    this->input_file_stream = nullptr;
  }

  if (this->output_file_stream)
  {
    this->output_file_stream->Close();
    this->output_file_stream = nullptr;
  }

  if (this->file_buffer)
  {
    this->file_buffer->Remove(false);
    this->file_buffer = nullptr;
  }

  if (this->headobj) {
    // In some error conditions when MimeMultipartRelated_ParseEOF() isn't run
    // (for example, no temp disk space available to extract message parts),
    // the head object is also referenced as a child.
    // If we free it, we remove the child reference first ... or crash later :-(
    for (int i = 0; i < this->nchildren; i++) {
      if (this->children[i] == this->headobj) {
        // Shift remaining children down.
        for (int j = i + 1; j < this->nchildren; j++) {
          this->children[j - 1] = this->children[j];
        }
        this->children[--this->nchildren] = nullptr;
        break;
      }
    }

    delete this->headobj;
    this->headobj = nullptr;
  }
}

#define ISHEX(c) ( ((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F') )
#define NONHEX(c) (!ISHEX(c))

extern "C" char *
escape_unescaped_percents(const char *incomingURL)
{
  const char *inC;
  char *outC;
  char *result = (char *) PR_Malloc(strlen(incomingURL)*3+1);

  if (result)
  {
    for(inC = incomingURL, outC = result; *inC != '\0'; inC++)
    {
      if (*inC == '%')
      {
        /* Check if either of the next two characters are non-hex. */
        if ( !*(inC+1) || NONHEX(*(inC+1)) || !*(inC+2) || NONHEX(*(inC+2)) )
        {
          /* Hex characters don't follow, escape the
             percent char */
          *outC++ = '%'; *outC++ = '2'; *outC++ = '5';
        }
        else
        {
          /* Hex characters follow, so assume the percent
             is escaping something else */
          *outC++ = *inC;
        }
      }
      else
        *outC++ = *inC;
    }
    *outC = '\0';
  }

  return result;
}

/* This routine is only necessary because the mailbox URL fed to us
   by the winfe can contain spaces and '>'s in it. It's a hack. */
static char *
escape_for_mrel_subst(char *inURL)
{
  char *output, *inC, *outC, *temp;

  int size = strlen(inURL) + 1;

  for(inC = inURL; *inC; inC++)
    if ((*inC == ' ') || (*inC == '>'))
      size += 2; /* space -> '%20', '>' -> '%3E', etc. */

  output = (char *)PR_MALLOC(size);
  if (output)
  {
    /* Walk through the source string, copying all chars
      except for spaces, which get escaped. */
    inC = inURL;
    outC = output;
    while(*inC)
    {
      if (*inC == ' ')
      {
        *outC++ = '%'; *outC++ = '2'; *outC++ = '0';
      }
      else if (*inC == '>')
      {
        *outC++ = '%'; *outC++ = '3'; *outC++ = 'E';
      }
      else
        *outC++ = *inC;

      inC++;
    }
    *outC = '\0';

    temp = escape_unescaped_percents(output);
    if (temp)
    {
      PR_FREEIF(output);
      output = temp;
    }
  }
  return output;
}

bool
MultipartRelated::StartParamExists(Part* child)
{
  char *ct = this->headers->Get(HEADER_CONTENT_TYPE, false, false);
  char *st = (ct
              ? MimeHeaders_get_parameter(ct, HEADER_PARM_START, NULL, NULL)
              : 0);

  PR_FREEIF(ct);
  if (!st)
    return false;

  PR_FREEIF(st);
  return true;
}

bool
MultipartRelated::ThisIsStartPart(Part* child)
{
  bool rval = false;
  char *ct, *st, *cst;

  ct = this->headers->Get(HEADER_CONTENT_TYPE, false, false);
  st = (ct
        ? MimeHeaders_get_parameter(ct, HEADER_PARM_START, NULL, NULL)
        : 0);

  PR_FREEIF(ct);
  if (!st)
    return false;

  cst = child->headers->Get(HEADER_CONTENT_ID, false, false);
  if (!cst)
    rval = false;
  else
  {
    char *tmp = cst;
    if (*tmp == '<')
    {
      int length;
      tmp++;
      length = strlen(tmp);
      if (length > 0 && tmp[length - 1] == '>')
      {
        tmp[length - 1] = '\0';
      }
    }

    rval = (!strcmp(st, tmp));
  }

  PR_FREEIF(st);
  PR_FREEIF(cst);
  return rval;
}
/* rhp - gotta support the "start" parameter */

char *
MakeAbsoluteURL(char *base_url, char *relative_url)
{
  char            *retString = nullptr;
  nsIURI          *base = nullptr;

  // if either is NULL, just return the relative if safe...
  if (!base_url || !relative_url)
  {
    if (!relative_url)
      return nullptr;

    NS_MsgSACopy(&retString, relative_url);
    return retString;
  }

  nsresult err = nsMimeNewURI(&base, base_url, nullptr);
  if (NS_FAILED(err))
    return nullptr;

  nsAutoCString spec;

  nsIURI    *url = nullptr;
  err = nsMimeNewURI(&url, relative_url, base);
  if (NS_FAILED(err))
    goto done;

  err = url->GetSpec(spec);
  if (NS_FAILED(err))
  {
    retString = nullptr;
    goto done;
  }
  retString = ToNewCString(spec);

done:
  NS_IF_RELEASE(url);
  NS_IF_RELEASE(base);
  return retString;
}

bool
MultipartRelated::OutputChild(Part* child)
{
  /* rhp - Changed from "if (this->head_loaded)" alone to support the
           start parameter
   */
  if (
       (this->head_loaded) ||
       (StartParamExists(child) && !ThisIsStartPart(child))
     )
  {
    /* This is a child part.  Just remember the mapping between the URL
       it represents and the part-URL to get it back. */

    char* location = child->headers->Get(HEADER_CONTENT_LOCATION,
                     false, false);
    if (!location) {
      char* tmp = child->headers->Get(HEADER_CONTENT_ID,
                    false, false);
      if (tmp) {
        char* tmp2 = tmp;
        if (*tmp2 == '<') {
          int length;
          tmp2++;
          length = strlen(tmp2);
          if (length > 0 && tmp2[length - 1] == '>') {
            tmp2[length - 1] = '\0';
          }
        }
        location = PR_smprintf("cid:%s", tmp2);
        PR_Free(tmp);
      }
    }

    if (location) {
      char *absolute;
      char *base_url = child->headers->Get(HEADER_CONTENT_BASE,
                       false, false);
      absolute = MakeAbsoluteURL(base_url ? base_url : this->base_url, location);

      PR_FREEIF(base_url);
      PR_Free(location);
      if (absolute) {
        nsAutoCString partnum;
        nsAutoCString imappartnum;
        partnum.Adopt(mime_part_address(child));
        if (!partnum.IsEmpty()) {
          if (this->options->missing_parts)
          {
            char * imappart = mime_imap_part_address(child);
            if (imappart)
              imappartnum.Adopt(imappart);
          }

          /*
            AppleDouble part need special care: we need to output only the data fork part of it.
            The problem at this point is that we haven't yet decoded the children of the AppleDouble
            part therefore we will have to hope the datafork is the second one!
          */
          if (mime_typep(child, (PartClass *) &mimeMultipartAppleDoubleClass))
            partnum.AppendLiteral(".2");

          char* part;
          if (!imappartnum.IsEmpty())
            part = mime_set_url_imap_part(this->options->url, imappartnum.get(), partnum.get());
          else
          {
            char *no_part_url = nullptr;
            if (this->options->part_to_load && this->options->format_out == nsMimeOutput::nsMimeMessageBodyDisplay)
              no_part_url = mime_get_base_url(this->options->url);
            if (no_part_url)
            {
              part = mime_set_url_part(no_part_url, partnum.get(), false);
              PR_Free(no_part_url);
            }
            else
              part = mime_set_url_part(this->options->url, partnum.get(), false);
          }
          if (part)
          {
            char *name = child->headers->GetName(child->options);
            // let's stick the filename in the part so save as will work.
            if (name)
            {
              char *savePart = part;
              part = PR_smprintf("%s&filename=%s", savePart, name);
              PR_Free(savePart);
              PR_Free(name);
            }
            char *temp = part;
            /* If there's a space in the url, escape the url.
               (This happens primarily on Windows and Unix.) */
            if (PL_strchr(part, ' ') || PL_strchr(part, '>') || PL_strchr(part, '%'))
              temp = escape_for_mrel_subst(part);
            MimeHashValue * value = new MimeHashValue(child, temp);
            PL_HashTableAdd(this->hash, absolute, value);

            /* rhp - If this part ALSO has a Content-ID we need to put that into
                     the hash table and this is what this code does
             */
            {
              char *tloc;
              char *tmp = child->headers->Get(HEADER_CONTENT_ID, false, false);
              if (tmp)
              {
                char* tmp2 = tmp;
                if (*tmp2 == '<')
                {
                  int length;
                  tmp2++;
                  length = strlen(tmp2);
                  if (length > 0 && tmp2[length - 1] == '>')
                  {
                    tmp2[length - 1] = '\0';
                  }
                }

                tloc = PR_smprintf("cid:%s", tmp2);
                PR_Free(tmp);
                if (tloc)
                {
                  MimeHashValue *value;
                  value = (MimeHashValue*)PL_HashTableLookup(this->hash, tloc);

                  if (!value)
                  {
                    value = new MimeHashValue(child, temp);
                    PL_HashTableAdd(this->hash, tloc, value);
                  }
                  else
                    PR_smprintf_free(tloc);
                }
              }
            }
            /*  rhp - End of putting more stuff into the hash table */

              /* it's possible that temp pointer is the same than the part pointer,
                 therefore be careful to not freeing twice the same pointer */
              if (temp && temp != part)
                PR_Free(temp);
              PR_Free(part);
            }
          }
        }
      }
  } else {
    /* Ah-hah!  We're the head object.  */
    char* base_url;
    this->head_loaded = true;
    this->headobj = child;
    this->buffered_hdrs = new Headers(*child->headers);
    base_url = child->headers->Get(HEADER_CONTENT_BASE,
                   false, false);
    /* rhp: need this for supporting Content-Location */
    if (!base_url)
    {
      base_url = child->headers->Get(HEADER_CONTENT_LOCATION, false, false);
    }
    /* rhp: need this for supporting Content-Location */

    if (base_url) {
      /* If the head object has a base_url associated with it, use
         that instead of any base_url that may have been associated
         with the multipart/related. */
      PR_FREEIF(this->base_url);
      this->base_url = base_url;
    }
  }
  if (this->options && !this->options->write_html_p
#ifdef MIME_DRAFTS
    && !this->options->decompose_file_p
#endif /* MIME_DRAFTS */
    )
    {
    return true;
    }

  return false;      /* Don't actually parse this child; we'll handle
                 all that at eof time. */
}

int
MultipartRelated::ParseChildLine(const char* line, int32_t length, bool first_line_p)
{
  Part *kid;

  if (this->options && !this->options->write_html_p
#ifdef MIME_DRAFTS
    && !this->options->decompose_file_p
#endif /* MIME_DRAFTS */
    )
    {
    /* Oh, just go do the normal thing... */
    return Super::ParseChildLine(line, length, firsT_line_p);
    }

  /* Throw it away if this isn't the head object.  (Someday, maybe we'll
     cache it instead.) */
  PR_ASSERT(this->nchildren > 0);
  if (this->nchildren <= 0)
    return -1;
  kid = this->children[this->nchildren-1];
  PR_ASSERT(kid);
  if (!kid) return -1;
  if (kid != this->headobj) return 0;

  /* Buffer this up (###tw much code duplication from mimemalt.c) */
  /* If we don't yet have a buffer (either memory or file) try and make a
     memory buffer. */
  if (!this->head_buffer && !this->file_buffer) {
    int target_size = 1024 * 50;       /* try for 50k */
    while (target_size > 0) {
      this->head_buffer = (char*)PR_MALLOC(target_size);
      if (this->head_buffer) break;  /* got it! */
      target_size -= (1024 * 5);     /* decrease it and try again */
    }

    if (this->head_buffer) {
      this->head_buffer_size = target_size;
    } else {
      this->head_buffer_size = 0;
    }

    this->head_buffer_fp = 0;
  }

  nsresult rv;
  /* Ok, if at this point we still don't have either kind of buffer, try and
     make a file buffer. */
  if (!this->head_buffer && !this->file_buffer)
  {
    nsCOMPtr <nsIFile> file;
    rv = nsMsgCreateTempFile("nsma", getter_AddRefs(file));
    NS_ENSURE_SUCCESS(rv, -1);
    this->file_buffer = do_QueryInterface(file);

    rv = MsgNewBufferedFileOutputStream(getter_AddRefs(this->output_file_stream), this->file_buffer, PR_WRONLY | PR_CREATE_FILE, 00600);
    NS_ENSURE_SUCCESS(rv, -1);
  }

  PR_ASSERT(this->head_buffer || this->output_file_stream);


  /* If this line will fit in the memory buffer, put it there.
   */
  if (this->head_buffer &&
      this->head_buffer_fp + length < this->head_buffer_size) {
    memcpy(this->head_buffer + this->head_buffer_fp, line, length);
    this->head_buffer_fp += length;
  } else {
    /* Otherwise it won't fit; write it to the file instead. */

    /* If the file isn't open yet, open it, and dump the memory buffer
       to it. */
    if (!this->output_file_stream)
    {
      if (!this->file_buffer)
      {
        nsCOMPtr <nsIFile> file;
        rv = nsMsgCreateTempFile("nsma", getter_AddRefs(file));
        NS_ENSURE_SUCCESS(rv, -1);
        this->file_buffer = do_QueryInterface(file);
      }

      nsresult rv = MsgNewBufferedFileOutputStream(getter_AddRefs(this->output_file_stream), this->file_buffer, PR_WRONLY | PR_CREATE_FILE, 00600);
      NS_ENSURE_SUCCESS(rv, -1);

      if (this->head_buffer && this->head_buffer_fp)
      {
        uint32_t bytesWritten;
        rv = this->output_file_stream->Write(this->head_buffer, this->head_buffer_fp, &bytesWritten);
        if (NS_FAILED(rv) || (bytesWritten < this->head_buffer_fp))
          return MIME_UNABLE_TO_OPEN_TMP_FILE;
      }

      PR_FREEIF(this->head_buffer);
      this->head_buffer_fp = 0;
      this->head_buffer_size = 0;
    }

    /* Dump this line to the file. */
    uint32_t bytesWritten;
    rv = this->output_file_stream->Write(line, length, &bytesWritten);
    if ((int32_t) bytesWritten < length || NS_FAILED(rv))
      return MIME_UNABLE_TO_OPEN_TMP_FILE;
  }

  return 0;
}




int
MultpartRelated::RealWrite(const char* buf, int32_t size)
{
  void* closure = this->real_output_closure;

#ifdef MIME_DRAFTS
  if (this->options &&
      this->options->decompose_file_p &&
      this->options->decompose_file_output_fn)
  {

    // the buf here has already been decoded, but we want to use general output
    // functions here that permit decoded or encoded input, using the closure
    // to tell the difference. We'll temporarily disable the closure's decoder,
    // then restore it when we are done. Not sure if we shouldn't just turn it off
    // permanently though.

    mime_draft_data* mdd = (mime_draft_data*)this->options->stream_closure;
    Decoder* old_decoder_data = mdd->decoder_data;
    mdd->decoder_data = nullptr;
    int status = this->options->decompose_file_output_fn(buf, size, (void*)mdd);
    mdd->decoder_data = old_decoder_data;
    return status;
  }
  else
#endif /* MIME_DRAFTS */
  {
    if (!closure) {
      closure = this->options->stream_closure;
    }
    return this->real_output_fn(buf, size, closure);
  }
}


int
MultipartRelated::PushTag(const char* buf, int32_t size)
{
  if (size + this->curtag_length > this->curtag_max) {
    this->curtag_max += 2 * size;
    if (this->curtag_max < 1024)
      this->curtag_max = 1024;

    char* newBuf = (char*)PR_Realloc(this->curtag, this->curtag_max);
    NS_ENSURE_TRUE(newBuf, MIME_OUT_OF_MEMORY);
    this->curtag = newBuf;
  }
  memcpy(this->curtag + this->curtag_length, buf, size);
  this->curtag_length += size;
  return 0;
}

bool
MultipartRelated::AcceptRelatedPart(Part* part_obj)
{
  if (!part_obj)
    return false;

  /* before accepting it as a valid related part, make sure we
     are able to display it inline as an embedded object. Else just ignore
     it, that will prevent any bad surprise... */
  PartClass *clazz = mime_find_class (part_obj->content_type, part_obj->headers, part_obj->options, false);
  if (clazz ? clazz->displayable_inline_p(clazz, part_obj->headers) : false)
    return true;

  /* ...but we always accept it if it's referenced by an anchor */
  return (this->curtag && this->curtag_length >= 3 &&
    (this->curtag[1] == 'A' || this->curtag[1] == 'a') && IS_SPACE(this->curtag[2]));
}

int
MultipartRelated::FlushTag()
{
  int length = this->curtag_length;
  char* buf;
  int status;

  if (this->curtag == nullptr || length == 0) return 0;

  status = PushTag("", 1); /* Push on a trailing NULL. */
  if (status < 0) return status;
  buf = this->curtag;
  PR_ASSERT(*buf == '<' && buf[length - 1] == '>');
  while (*buf) {
    char c;
    char* absolute;
    char* part_url;
    char* ptr = buf;
    char *ptr2;
    char quoteDelimiter = '\0';
    while (*ptr && *ptr != '=') ptr++;
    if (*ptr == '=') {
      /* Ignore = and leading space. */
      /* Safe, because there's a '>' at the end! */
      do {ptr++;} while (IS_SPACE(*ptr));
      if (*ptr == '"' || *ptr == '\'') {
        quoteDelimiter = *ptr;
        /* Take up the quote and leading space here as well. */
        /* Safe because there's a '>' at the end */
        do {ptr++;} while (IS_SPACE(*ptr));
      }
    }
    status = RealWrite(buf, ptr - buf);
    if (status < 0) return status;
    buf = ptr;
    if (!*buf) break;
    if (quoteDelimiter)
    {
      ptr = PL_strnchr(buf, quoteDelimiter, length - (buf - this->curtag));
    } else {
      for (ptr = buf; *ptr ; ptr++) {
        if (*ptr == '>' || IS_SPACE(*ptr)) break;
      }
      PR_ASSERT(*ptr);
    }
    if (!ptr || !*ptr) break;

    while(buf < ptr)
    {
      /* ### mwelch For each word in the value string, see if
                      the word is a cid: URL. If so, attempt to
              substitute the appropriate mailbox part URL in
              its place. */
      ptr2=buf; /* walk from the left end rightward */
      while((ptr2<ptr) && (!IS_SPACE(*ptr2)))
        ptr2++;
      /* Compare the beginning of the word with "cid:". Yuck. */
      if (((ptr2 - buf) > 4) &&
        ((buf[0]=='c' || buf[0]=='C') &&
         (buf[1]=='i' || buf[1]=='I') &&
         (buf[2]=='d' || buf[2]=='D') &&
          buf[3]==':'))
      {
        // Make sure it's lowercase, otherwise it won't be found in the hash table
        buf[0] = 'c'; buf[1] = 'i'; buf[2] = 'd';

        /* Null terminate the word so we can... */
        c = *ptr2;
        *ptr2 = '\0';

        /* Construct a URL out of the word. */
        absolute = MakeAbsoluteURL(this->base_url, buf);

        /* See if we have a mailbox part URL
           corresponding to this cid. */
        part_url = nullptr;
        MimeHashValue * value = nullptr;
        if (absolute)
        {
          value = (MimeHashValue *)PL_HashTableLookup(this->hash, buf);
          part_url = value ? value->m_url : nullptr;
          PR_FREEIF(absolute);
        }

        /*If we found a mailbox part URL, write that out instead.*/
        if (part_url && AcceptRelatedPart(value->m_obj))
        {
          status = RealWrite(part_url, strlen(part_url));
          if (status < 0) return status;
          buf = ptr2; /* skip over the cid: URL we substituted */

          /* don't show that object as attachment */
          if (value->m_obj)
            value->m_obj->dontShowAsAttachment = true;
        }

        /* Restore the character that we nulled. */
        *ptr2 = c;
      }
      /* rhp - if we get here, we should still check against the hash table! */
      else
      {
        char holder = *ptr2;
        char *realout;

        *ptr2 = '\0';

        /* Construct a URL out of the word. */
        absolute = MakeAbsoluteURL(this->base_url, buf);

        /* See if we have a mailbox part URL
           corresponding to this cid. */
        MimeHashValue * value;
        if (absolute)
          value = (MimeHashValue *)PL_HashTableLookup(this->hash, absolute);
        else
          value = (MimeHashValue *)PL_HashTableLookup(this->hash, buf);
        realout = value ? value->m_url : nullptr;

        *ptr2 = holder;
        PR_FREEIF(absolute);

        if (realout && AcceptRelatedPart(value->m_obj))
        {
          status = RealWrite(realout, strlen(realout));
          if (status < 0) return status;
          buf = ptr2; /* skip over the cid: URL we substituted */

          /* don't show that object as attachment */
          if (value->m_obj)
            value->m_obj->dontShowAsAttachment = true;
        }
      }
      /* rhp - if we get here, we should still check against the hash table! */

      /* Advance to the beginning of the next word, or to
         the end of the value string. */
      while((ptr2<ptr) && (IS_SPACE(*ptr2)))
        ptr2++;

      /* Write whatever original text remains after
         cid: URL substitution. */
      status = RealWrite(buf, ptr2-buf);
      if (status < 0) return status;
      buf = ptr2;
    }
  }
  if (buf && *buf) {
    status = RealWrite(buf, strlen(buf));
    if (status < 0) return status;
  }
  this->curtag_length = 0;
  return 0;
}



static int
mime_multipart_related_output_fn(const char* buf, int32_t size, void *stream_closure)
{
  return ((MultipartRelated*)stream_closure)->ParseTags(buf, size);
}

int
MultipartRelated::ParseTags(const char* buf, int32_t size)
{
  char* ptr;
  int32_t delta;
  int status;
  while (size > 0) {
    if (this->curtag_length > 0) {
      ptr = PL_strnchr(buf, '>', size);
      if (!ptr) {
        return PushTag(buf, size);
      }
      delta = ptr - buf + 1;
      status = PushTag(buf, delta);
      if (status < 0) return status;
      status = FlushTag()
      if (status < 0) return status;
      buf += delta;
      size -= delta;
    }
    ptr = PL_strnchr(buf, '<', size);
    if (ptr && ptr - buf >= size) ptr = 0;
    if (!ptr) {
      return RealWrite(buf, size);
    }
    delta = ptr - buf;
    status = RealWrite(buf, delta);
    if (status < 0) return status;
    buf += delta;
    size -= delta;
    PR_ASSERT(this->curtag_length == 0);
    status = PushTag(buf, 1);
    if (status < 0) return status;
    PR_ASSERT(this->curtag_length == 1);
    buf++;
    size--;
  }
  return 0;
}


int
MultipartRelated::ParseEOF(bool abort_p)
{
  /* OK, all the necessary data has been collected.  We now have to spew out
     the HTML.  We let it go through all the normal mechanisms (which
     includes content-encoding handling), and intercept the output data to do
     translation of the tags.  Whee. */
  int status = 0;
  Part *body;
  char* ct;
  const char* dct;

  status = Super::ParseEOF(abort_p);
  if (status < 0) goto FAIL;

  if (!this->headobj) return 0;

  ct = (this->buffered_hdrs
      ? this->buffered_hdrs->Get(HEADER_CONTENT_TYPE,
               true, false)
      : 0);
  dct = this->default_part_type;

  this->real_output_fn = this->options->output_fn;
  this->real_output_closure = this->options->output_closure;

  this->options->output_fn = mime_multipart_related_output_fn;
  this->options->output_closure = this;

  body = mime_create(((ct && *ct) ? ct : (dct ? dct : TEXT_HTML)),
             this->buffered_hdrs, this->options);

  PR_FREEIF(ct);
  if (!body) {
    status = MIME_OUT_OF_MEMORY;
    goto FAIL;
  }
  // replace the existing head object with the new object
  for (int iChild = 0; iChild < this->nchildren; iChild++) {
    if (this->children[iChild] == this->headobj) {
      // cleanup of the headobj is performed explicitly in our finalizer now
      //  that it does not get cleaned up as a child.
      this->children[iChild] = body;
      body->parent = this;
      body->options = this->options;
    }
  }

  if (!body->parent) {
    NS_WARNING("unexpected mime multipart related structure");
    goto FAIL;
  }

  body->dontShowAsAttachment = body->clazz->displayable_inline_p(body->clazz, body->headers);

#ifdef MIME_DRAFTS
  if (this->options &&
      this->options->decompose_file_p &&
      this->options->decompose_file_init_fn &&
      (this->file_buffer || this->head_buffer))
  {
    status = this->options->decompose_file_init_fn(this->options->stream_closure, this->buffered_hdrs);
    if (status < 0) return status;
  }
#endif /* MIME_DRAFTS */

  /* if the emitter wants to know about nested bodies, then it needs
     to know that we jumped back to this body part. */
  if (this->options->notify_nested_bodies)
  {
    char *part_path = mime_part_address(body);
    if (part_path)
    {
      mimeEmitterAddHeaderField(this->options,
                                "x-jsemitter-part-path",
                                part_path);
      PR_Free(part_path);
    }
  }

  /* Now that we've added this new object to our list of children,
     start its parser going. */
  status = body->ParseBegin(body);
  if (status < 0) goto FAIL;

  if (this->head_buffer)
  {
    /* Read it out of memory. */
    PR_ASSERT(!this->file_buffer && !this->input_file_stream);

    status = body->ParseBuffer(this->head_buffer, this->head_buffer_fp);
  }
  else if (this->file_buffer)
  {
    /* Read it off disk. */
    char *buf;

    PR_ASSERT(this->head_buffer_size == 0 &&
          this->head_buffer_fp == 0);
    PR_ASSERT(this->file_buffer);
    if (!this->file_buffer)
    {
      status = -1;
      goto FAIL;
    }

    buf = (char *) PR_MALLOC(FILE_IO_BUFFER_SIZE);
    if (!buf)
    {
      status = MIME_OUT_OF_MEMORY;
      goto FAIL;
    }

    // First, close the output file to open the input file!
    if (this->output_file_stream)
      this->output_file_stream->Close();

    nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(this->input_file_stream), this->file_buffer);
    if (NS_FAILED(rv))
    {
      PR_Free(buf);
      status = MIME_UNABLE_TO_OPEN_TMP_FILE;
      goto FAIL;
    }

    while(1)
    {
      uint32_t bytesRead = 0;
      rv = this->input_file_stream->Read(buf, FILE_IO_BUFFER_SIZE - 1, &bytesRead);
      if (NS_FAILED(rv) || !bytesRead)
      {
        status = NS_FAILED(rv) ? -1 : 0;
        break;
      }
      else
      {
        /* It would be really nice to be able to yield here, and let
           some user events and other input sources get processed.
           Oh well. */

        status = body->ParseBuffer(buf, bytesRead, body);
        if (status < 0) break;
      }
    }
    PR_Free(buf);
  }

  if (status < 0) goto FAIL;

  /* Done parsing. */
  status = body->ParseEOF(body, false);
  if (status < 0) goto FAIL;
  status = body->ParseEnd(body, false);
  if (status < 0) goto FAIL;

FAIL:

#ifdef MIME_DRAFTS
  if (this->options &&
      this->options->decompose_file_p &&
      this->options->decompose_file_close_fn &&
      (this->file_buffer || this->head_buffer)) {
  status = this->options->decompose_file_close_fn(this->options->stream_closure);
  if (status < 0) return status;
  }
#endif /* MIME_DRAFTS */

  this->options->output_fn = this->real_output_fn;
  this->options->output_closure = this->real_output_closure;

  return status;
}

} // namespace mime
} // namespace mozilla
