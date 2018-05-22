/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimethpl.h"
#include "prlog.h"
#include "msgCore.h"
#include "mimemoz2.h"
#include "nsString.h"
#include "nsIDocumentEncoder.h" // for output flags

/* See header file for description */
/* Most of this code is copied from mimethsa. If you find a bug here, check that class, too. */
/* TODO
  - If you Save As File .html with this mode, you get a total mess.
  - Print is untested (crashes in all modes).
*/

namespace mozilla::mime {

/* I should use the Flowed class as base (because our HTML->TXT converter
   can generate flowed, and we tell it to) - this would get a bit nicer
   rendering. However, that class is more picky about line endings
   and I currently don't feel like splitting up the generated plaintext
   into separate lines again. So, I just throw the whole message at once
   at the TextPlain_ParseLine function - it happens to work... */
#define SUPERCLASS TextPlain

int
HTMLAsPlaintext::ParseBegin()
{
  this->complete_buffer = new nsString();
  return SUPERCLASS::ParseBegin();
}

int
HTMLAsPlaintext::ParseEOF(bool abort_p)
{
  if (this->closed_p)
    return 0;

  // This is a hack. We need to call ParseEOF() of the super class to flush out any buffered data.
  // We can't call it yet for our direct super class, because it would "close" the output
  // (write tags such as </pre> and </div>). We'll do that after parsing the buffer.
  int status = Text::ParseEOF(abort_p);
  if (status < 0)
    return status;

  if (!this->complete_buffer)
    return 0;

  nsString& cb = *this->complete_buffer;

  // could be empty, e.g., if part isn't actually being displayed
  if (cb.Length())
  {
    nsString asPlaintext;
    uint32_t flags = nsIDocumentEncoder::OutputFormatted
      | nsIDocumentEncoder::OutputWrap
      | nsIDocumentEncoder::OutputFormatFlowed
      | nsIDocumentEncoder::OutputLFLineBreak
      | nsIDocumentEncoder::OutputNoScriptContent
      | nsIDocumentEncoder::OutputNoFramesContent
      | nsIDocumentEncoder::OutputBodyOnly;
    HTML2Plaintext(cb, asPlaintext, flags, 80);

    NS_ConvertUTF16toUTF8 resultCStr(asPlaintext);
    // TODO parse each line independently
    status = SUPERCLASS::ParseLine(resultCStr.BeginWriting(), resultCStr.Length());
    cb.Truncate();
  }

  if (status < 0)
    return status;

  // Second part of the flush hack. Pretend obj wasn't closed yet, so that our super class
  // gets a chance to write the closing.
  bool save_closed_p = this->closed_p;
  this->closed_p = false;
  status = SUPERCLASS::ParseEOF(abort_p);
  // Restore closed_p.
  this->closed_p = save_closed_p;
  return status;
}

void
HTMLAsPlaintext::~HTMLAsPlaintext()
{
  if (this->complete_buffer)
  {
    // If there's content in the buffer, make sure that we output it.
    // Don't care about return codes.
    this->ParseEOF(false);

    delete this->complete_buffer;
    this->complete_buffer = NULL;
  }
}

int
HTMLAsPlaintext::ParseLine(const char* line, int32_t length)
{
  if (!this->complete_buffer)
  {
#if DEBUG
printf("Can't output: %s\n", line);
#endif
    return -1;
  }

  /*
    To convert HTML->TXT synchronously, I need the full source at once,
    not line by line (how do you convert "<li>foo\n" to plaintext?).
    ParseDecodedBuffer claims to give me that, but in fact also gives
    me single lines.
    It might be theoretically possible to drive this asynchronously, but
    I don't know, which odd circumstances might arise and how libmime
    will behave then. It's not worth the trouble for me to figure this all out.
   */
  nsCString linestr(line, length);
  NS_ConvertUTF8toUTF16 line_ucs2(linestr.get());
  if (length && line_ucs2.IsEmpty())
    CopyASCIItoUTF16 (linestr, line_ucs2);
  this->complete_buffer->Append(line_ucs2);

  return 0;
}
