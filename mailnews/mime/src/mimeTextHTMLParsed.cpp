/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimeTextHTMLParsed.h"
#include "prmem.h"
#include "prlog.h"
#include "msgCore.h"
#include "mozilla/dom/DOMParser.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "mozilla/ErrorResult.h"
#include "nsIPrefBranch.h"

/* See header file for description */
/* Most of this code is copied from mimethsa. If you find a bug here, check that class, too. */

namespace mozilla {
namespace mime {

#define SUPERCLASS HTML

int
HTMLParsed::ParseBegin()
{
  this->complete_buffer = new nsString();
  int status = SUPERCLASS::ParseBegin();
  if (status < 0)
    return status;

  return 0;
}

int
HTMLParsed::ParseEOF(bool abort_p)
{
  if (this->closed_p)
    return 0;
  int status = SUPERCLASS::ParseEOF(abort_p);
  if (status < 0)
    return status;

  // We have to cache all lines and parse the whole document at once.
  // There's a useful sounding function parseFromStream(), but it only allows XML
  // mimetypes, not HTML. Methinks that's because the HTML soup parser
  // needs the entire doc to make sense of the gibberish that people write.
  if (!this->complete_buffer)
    return 0;

  nsString& rawHTML = *this->complete_buffer;
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
  status = SUPERCLASS::ParseLine(
    resultCStr.BeginWriting(), resultCStr.Length());
  rawHTML.Truncate();
  return status;
}

void
HTMLParsed::~HTMLParsed()
{
  if (this->complete_buffer) {
    this->ParseEOF(false);
    delete this->complete_buffer;
    this->complete_buffer = NULL;
  }
}

int
HTMLParsed::ParseLine(const char *line, int32_t length)
{
  if (!this->complete_buffer)
    return -1;

  nsCString linestr(line, length);
  NS_ConvertUTF8toUTF16 line_ucs2(linestr.get());
  if (length && line_ucs2.IsEmpty())
    CopyASCIItoUTF16(linestr, line_ucs2);
  this->complete_buffer->Append(line_ucs2);

  return 0;
}


} // namespace mime
} // namespace mozilla
