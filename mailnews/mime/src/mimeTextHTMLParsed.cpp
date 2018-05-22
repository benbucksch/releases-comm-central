/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Most of this code is copied from mimethsa. If you find a bug here, check that class, too. */

/* This runs the entire HTML document through the Mozilla HTML parser, and
   then outputs it as string again. This ensures that the HTML document is
   syntactically correct and complete and all tags and attributes are closed.

   That prevents "MIME in the middle" attacks like efail.de.
   The base problem is that we concatenate different MIME parts in the output
   and render them all together as a single HTML document in the display.

   The better solution would be to put each MIME part into its own <iframe type="content">.
   during rendering. Unfortunately, we'd need <iframe seamless> for that.
   That would remove the need for this workaround, and stop even more attack classes.
*/

#include "mimeTextHTMLParsed.h"
#include "prmem.h"
#include "prlog.h"
#include "msgCore.h"
#include "mozilla/dom/DOMParser.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "mozilla/ErrorResult.h"
#include "nsIPrefBranch.h"

#define MIME_SUPERCLASS mimeInlineTextHTMLClass
MimeDefClass(MimeInlineTextHTMLParsed, MimeInlineTextHTMLParsedClass,
             mimeInlineTextHTMLParsedClass, &MIME_SUPERCLASS);

static int MimeInlineTextHTMLParsed_ParseLine(const char *, int32_t,
                                               Part *);
static int MimeInlineTextHTMLParsed_ParseBegin(Part *obj);
static int MimeInlineTextHTMLParsed_ParseEOF(Part *, bool);
static void MimeInlineTextHTMLParsed_finalize(Part *obj);

static int
MimeInlineTextHTMLParsedClassInitialize(MimeInlineTextHTMLParsedClass *clazz)
{
  PartClass *oclass = (MimeObjectClass *)clazz;
  NS_ASSERTION(!oclass->class_initialized, "problem with superclass");
  oclass->ParseLine  = MimeInlineTextHTMLParsed_parse_line;
  oclass->ParseBegin = MimeInlineTextHTMLParsed_parse_begin;
  oclass->ParseEOF   = MimeInlineTextHTMLParsed_parse_eof;
  oclass->finalize    = MimeInlineTextHTMLParsed_finalize;

  return 0;
}

static int
MimeInlineTextHTMLParsed_ParseBegin(Part *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;
  me->complete_buffer = new nsString();
  int status = ((PartClass*)&MIME_SUPERCLASS)->ParseBegin(obj);
  if (status < 0)
    return status;

  // Dump the charset we get from the mime headers into a HTML <meta http-equiv>.
  char *content_type = obj->headers ?
    MimeHeaders_get(obj->headers, HEADER_CONTENT_TYPE, false, false) : 0;
  if (content_type)
  {
    char* charset = MimeHeaders_get_parameter(content_type,
                                              HEADER_PARM_CHARSET,
                                              NULL, NULL);
    PR_Free(content_type);
    if (charset)
    {
      nsAutoCString charsetline(
        "\n<meta http-equiv=\"content-type\" content=\"text/html; charset=");
      charsetline += charset;
      charsetline += "\">\n";
      int status = Part_write(obj,
                                    charsetline.get(),
                                    charsetline.Length(),
                                    true);
      PR_Free(charset);
      if (status < 0)
        return status;
    }
  }
  return 0;
}

static int
MimeInlineTextHTMLParsed_ParseEOF(Part *obj, bool abort_p)
{

  if (obj->closed_p)
    return 0;
  int status = ((PartClass*)&MIME_SUPERCLASS)->ParseEOF(obj, abort_p);
  if (status < 0)
    return status;
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  // We have to cache all lines and parse the whole document at once.
  // There's a useful sounding function parseFromStream(), but it only allows XML
  // mimetypes, not HTML. Methinks that's because the HTML soup parser
  // needs the entire doc to make sense of the gibberish that people write.
  if (!me || !me->complete_buffer)
    return 0;

  nsString& rawHTML = *(me->complete_buffer);
  nsString parsed;
  nsresult rv;

  // Parse the HTML source.
  mozilla::ErrorResult rv2;
  RefPtr<mozilla::dom::DOMParser> parser =
    mozilla::dom::DOMParser::CreateWithoutGlobal(rv2);
  nsCOMPtr<nsIDocument> document = parser->ParseFromString(
    rawHTML, mozilla::dom::SupportedType::Text_html, rv2);
  if (rv2.Failed())
    return -1;

  // Serialize it back to HTML source again.
  nsCOMPtr<nsIDocumentEncoder> encoder = do_CreateInstance(
    "@mozilla.org/layout/documentEncoder;1?type=text/html");
  uint32_t aFlags = 0;
  rv = encoder->Init(document, NS_LITERAL_STRING("text/html"), aFlags);
  NS_ENSURE_SUCCESS(rv, -1);
  rv = encoder->EncodeToString(parsed);
  NS_ENSURE_SUCCESS(rv, -1);

  // Write it out.
  NS_ConvertUTF16toUTF8 resultCStr(parsed);
  status = ((PartClass*)&MIME_SUPERCLASS)->ParseLine(
    resultCStr.BeginWriting(), resultCStr.Length(), obj);
  rawHTML.Truncate();
  return status;
}

void
MimeInlineTextHTMLParsed_finalize(Part *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  if (me && me->complete_buffer)
  {
    obj->clazz->ParseEOF(obj, false);
    delete me->complete_buffer;
    me->complete_buffer = NULL;
  }

  ((PartClass*)&MIME_SUPERCLASS)->finalize(obj);
}

static int
MimeInlineTextHTMLParsed_ParseLine(const char *line, int32_t length,
                                    Part *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  if (!me || !(me->complete_buffer))
    return -1;

  nsCString linestr(line, length);
  NS_ConvertUTF8toUTF16 line_ucs2(linestr.get());
  if (length && line_ucs2.IsEmpty())
    CopyASCIItoUTF16(linestr, line_ucs2);
  (me->complete_buffer)->Append(line_ucs2);

  return 0;
}
