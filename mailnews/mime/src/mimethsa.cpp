/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimethsa.h"
#include "prmem.h"
#include "prlog.h"
#include "msgCore.h"
#include "mimemoz2.h"
#include "nsString.h"

namespace mozilla::mime {

/* See header file for description */
/* Most of this code is copied from mimethsa. If you find a bug here, check that class, too. */

#define SUPERCLASS HTML

int
HTMLSanitized::ParseBegin()
{
  this->complete_buffer = new nsString();
  int status = SUPERCLASS::ParseBegin();
  if (status < 0)
    return status;

  return 0;
}

int
HTMLSanitized::ParseEOF(bool abort_p)
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

  nsString& cb = *this->complete_buffer;
  nsString sanitized;

  // Sanitize.
  HTMLSanitize(cb, sanitized);

  // Write it out.
  NS_ConvertUTF16toUTF8 resultCStr(sanitized);
  status = SUPERCLASS::ParseLine(resultCStr.BeginWriting(), resultCStr.Length());
  cb.Truncate();
  return status;
}

HTMLSanitized::~HTMLSanitized()
{
  if (this->complete_buffer) {
    this->ParseEOF(false);
    delete this->complete_buffer;
    this->complete_buffer = NULL;
  }
}

int
HTMLSanitized::ParseLine(const char* line, int32_t length)
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
