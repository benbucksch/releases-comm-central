/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date         Modified by      Description of modification
 * 2000-04-20   IBM Corp.        OS/2 VisualAge build
 * 2018-05-22   Ben Bucksch      Convert to C++ classes
 */
#include "mimetext.h"
#include "mimebuf.h"
#include "mimethtm.h"
#include "comi18n.h"
#include "mimemoz2.h"

#include "prlog.h"
#include "prmem.h"
#include "plstr.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIPrefLocalizedString.h"
#include "nsMsgUtils.h"
#include "nsMimeTypes.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::mime {

#define SUPERCLASS Leaf

int Text::Text()
{
  this.initializedCharset = false;
  this.needUpdateMsgWinCharset = false;
}

int Text::InitializeCharset()
{
  this.inputAutodetect = false;
  this.charsetOverridable = false;

  /* Figure out an appropriate charset for this object.
  */
  if (!this.charset && this.headers)
  {
    if (this.options && this.options->override_charset)
    {
      this.charset = strdup(this.options->default_charset);
    }
    else
    {
      char *ct = this.headers->Get(HEADER_CONTENT_TYPE, false, false);
      if (ct)
      {
        this.charset = Headers::GetParameter(ct, "charset", NULL, NULL);
        PR_Free(ct);
      }

      if (!this.charset)
      {
        /* If we didn't find "Content-Type: ...; charset=XX" then look
           for "X-Sun-Charset: XX" instead.  (Maybe this should be done
           in SunAttachmentClass, but it's harder there than here.)
         */
        this.charset = this.headers->Get(HEADER_X_SUN_CHARSET, false, false);
      }

      /* iMIP entities without an explicit charset parameter default to
       US-ASCII (RFC 2447, section 2.4). However, Microsoft Outlook generates
       UTF-8 but omits the charset parameter.
       When no charset is defined by the container (e.g. iMIP), iCalendar
       files default to UTF-8 (RFC 2445, section 4.1.4).
       */
      if (!this.charset &&
          this.content_type &&
          !PL_strcasecmp(this.content_type, TEXT_CALENDAR))
        this.charset = strdup("UTF-8");

      if (!this.charset)
      {
        nsresult res;

        this.charsetOverridable = true;

        nsCOMPtr<nsIPrefBranch> prefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID, &res));
        if (NS_SUCCEEDED(res))
        {
          nsCOMPtr<nsIPrefLocalizedString> str;
          if (NS_SUCCEEDED(prefBranch->GetComplexValue("intl.charset.detector", NS_GET_IID(nsIPrefLocalizedString), getter_AddRefs(str)))) {
            //only if we can get autodetector name correctly, do we set this to true
            this.inputAutodetect = true;
          }
        }

        if (this.options && this.options->default_charset)
          this.charset = strdup(this.options->default_charset);
        else
        {
          if (NS_SUCCEEDED(res))
          {
            nsString value;
            NS_GetLocalizedUnicharPreferenceWithDefault(prefBranch, "mailnews.view_default_charset", EmptyString(), value);
            this.charset = ToNewUTF8String(value);
          }
          else
            this.charset = strdup("");
        }
      }
    }
  }

  if (this.inputAutodetect)
  {
    //we need to prepare lineDam for charset detection
    this.lineDamBuffer = (char*)PR_Malloc(DAM_MAX_BUFFER_SIZE);
    this.lineDamPtrs = (char**)PR_Malloc(DAM_MAX_LINES*sizeof(char*));
    this.curDamOffset = 0;
    this.lastLineInDam = 0;
    if (!this.lineDamBuffer || !this.lineDamPtrs)
    {
      this.inputAutodetect = false;
      PR_FREEIF(this.lineDamBuffer);
      PR_FREEIF(this.lineDamPtrs);
    }
  }

  this.initializedCharset = true;

  return 0;
}

Text::~Text()
{
  this.ParseEOF(false);
  this.ParseEnd(false);

  PR_FREEIF(this.charset);

  /* Should have been freed by ParseEOF, but just in case... */
  PR_ASSERT(!this.cbuffer);
  PR_FREEIF (this.cbuffer);

  if (this.inputAutodetect) {
    PR_FREEIF(this.lineDamBuffer);
    PR_FREEIF(this.lineDamPtrs);
    this.inputAutodetect = false;
  }
}


int Text::ParseEOF(bool abort_p)
{
  int status;

  if (this.closed_p) return 0;
  NS_ASSERTION(!this.parsed_p, "obj already parsed");

  /* Flush any buffered data from Leaf's decoder */
  status = SUPERCLASS::CloseDecoder();
  if (status < 0) return status;

  /* If there is still data in the ibuffer, that means that the last
   line of this part didn't end in a newline; so push it out anyway
   (this means that the ParseLine() method will be called with a string
   with no trailing newline, which isn't the usual case).  We do this
   here, rather than in Part.ParseEOF(), because |Part| isn't
   aware of the rotating-and-converting / charset detection we need to
   do first.
  */
  if (!abort_p && this.ibuffer_fp > 0)
  {
    status = this.RotateConvertAndParseLine(this.ibuffer, this.ibuffer_fp);
    this.ibuffer_fp = 0;
    if (status < 0)
    {
      //we haven't find charset yet? Do it before return
      if (this.inputAutodetect)
        status = this.OpenDAM(nullptr, 0);

      this.closed_p = true;
      return status;
    }
  }

  //we haven't find charset yet? now its the time
  if (this.inputAutodetect) {
     status = this.OpenDAM(nullptr, 0);
     // TODO check status
  }

  return SUPERCLASS::ParseOEF(abort_p);
}

int Text:ParseEnd(bool abort_p)
{
  if (this.parsed_p)
  {
    PR_ASSERT(this.closed_p);
    return 0;
  }

  /* We won't be needing this buffer any more; nuke it. */
  PR_FREEIF(this.cbuffer);
  this.cbuffer_size = 0;

  return SUPERCLASS::ParseEnd(abort_p);
}


/* This maps A-M to N-Z and N-Z to A-M.  All other characters are left alone.
   (Comments in GNUS imply that for Japanese, one should rotate by 47?)
 */
const unsigned char rot13_table[256] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
  59, 60, 61, 62, 63, 64, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
  65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 91, 92, 93, 94, 95, 96,
  110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 97, 98,
  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 123, 124, 125, 126,
  127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141,
  142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156,
  157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171,
  172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186,
  187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201,
  202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216,
  217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
  232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246,
  247, 248, 249, 250, 251, 252, 253, 254, 255 };

int Text:ROT13Line(char *line, int32_t length)
{
  unsigned char *s, *end;
  PR_ASSERT(line);
  if (!line) return -1;
  s = (unsigned char *) line;
  end = s + length;
  while (s < end)
  {
    *s = rot13_table[*s];
    s++;
  }
  return 0;
}


int Text::ParseDecodedBuffer(const char *buf, int32_t size)
{
  PR_ASSERT(!this.closed_p);
  if (this.closed_p) return -1;

  /* |Leaf| takes care of this. */
  PR_ASSERT(this.output_p && this.options && this.options->output_fn);
  if (!this.options) return -1;

  /* If we're supposed to write this object, but aren't supposed to convert
   it to HTML, simply pass it through unaltered. */
  if (!this.options->write_html_p && this.options->format_out != nsMimeOutput::nsMimeMessageAttach)
  return this.Write(buf, size, true);

  /* This is just like the ParseDecodedBuffer() method we inherit from the
   Leaf class, except that we line-buffer to our own wrapper on the
   `ParseLine' method instead of calling the `parse_line' method directly.
   */
  return mime_LineBuffer (buf, size,
             &this.ibuffer, &this.ibuffer_size, &this.ibuffer_fp,
             true,
             &Text::RotateConvertAndParseLine, // TODO virtual function pointer
             this);
}

int Text::ConvertAndParseLine(char* line, int32_t length)
{
  int status;
  nsAutoCString converted;

  //in case of charset autodetection, charset can be override by meta charset
  if (this.charsetOverridable) {
    if (this.IsType(TextHTMLClass))
    {
      if (this.needUpdateMsgWinCharset &&
          this.charset && *this.charset)
      {
        //if meta tag specified charset is different from our detected result, use meta charset.
        //update MsgWindow charset if we are instructed to do so
        SetMailCharacterSetToMsgWindow(this, this.charset);
      }
    }
  }

  status = this.options->charset_conversion_fn(line, length,
                       this.charset,
                       converted,
                       this.options->stream_closure);

  if (status == 0)
  {
    line = (char *) converted.get();
    length = converted.Length();
  }

  /* Now that the line has been converted, call the subclass's ParseLine
   method with the decoded data. */
  //status = this.clazz->ParseLine(line, length);
  status = this.ParseLine(line, length);

  return status;
}

// In this function, all buffered lines in lineDam will be sent to charset detector
// and a charset will be used to parse all those line and following lines in this mime obj.
int Text::OpenDAM(char* line, int32_t length)
{
  const char* detectedCharset = nullptr;
  nsresult res = NS_OK;
  int status = 0;
  int32_t i;

  if (this.curDamOffset <= 0) {
    //there is nothing in dam, use current line for detection
    if (length > 0) {
      res = MIME_detect_charset(line, length, &detectedCharset);
    }
  } else {
    //we have stuff in dam, use the one
    res = MIME_detect_charset(this.lineDamBuffer, this.curDamOffset, &detectedCharset);
  }

  //set the charset
  if (NS_SUCCEEDED(res) && detectedCharset && *detectedCharset)  {
    PR_FREEIF(this.charset);
    this.charset = strdup(detectedCharset);

    //update MsgWindow charset if we are instructed to do so
    if (this.needUpdateMsgWinCharset && *this.charset)
      SetMailCharacterSetToMsgWindow(this, this.charset);
  }

  //process dam and line using the charset
  if (this.curDamOffset) {
    for (i = 0; i < this.lastLineInDam-1; i++)
    {
      status = this.ConvertAndParseLine(
                this.lineDamPtrs[i],
                this.lineDamPtrs[i+1] - this.lineDamPtrs[i]);
    }
    status = this.ConvertAndParseLine(
                this.lineDamPtrs[i],
                this.lineDamBuffer + this.curDamOffset - this.lineDamPtrs[i]);
  }

  if (length)
    status = this.ConvertAndParseLine(line, length);

  PR_Free(this.lineDamPtrs);
  PR_Free(this.lineDamBuffer);
  this.lineDamPtrs = nullptr;
  this.lineDamBuffer = nullptr;
  this.inputAutodetect = false;

  return status;
}


int Text::RotateConvertAndParseLine(char* line, int32_t length)
{
  int status = 0;

  PR_ASSERT(!this.closed_p);
  if (this.closed_p) return -1;

  /* Rotate the line, if desired (this happens on the raw data, before any
   charset conversion.) */
  if (this.options && this.options->rot13_p)
  {
    status = this.ROT13Line(line, length);
    if (status < 0) return status;
  }

  // Now convert to the canonical charset, if desired.
  bool    doConvert = true;
  // Don't convert vCard data
  if ( ( (this.content_type) && (!PL_strcasecmp(this.content_type, TEXT_VCARD)) ) ||
       (this.options->format_out == nsMimeOutput::nsMimeMessageSaveAs)
       || this.options->format_out == nsMimeOutput::nsMimeMessageAttach)
    doConvert = false;

  // Only convert if the user prefs is false
  if ( (this.options && this.options->charset_conversion_fn) &&
       (!this.options->force_user_charset) &&
       (doConvert)
     )
  {
    if (!this.initializedCharset)
    {
      this.initializeCharset();
      //update MsgWindow charset if we are instructed to do so
      if (this.needUpdateMsgWinCharset && *this.charset)
        SetMailCharacterSetToMsgWindow(this, this.charset);
    }

    //if autodetect is on, push line to dam
    if (this.inputAutodetect)
    {
      //see if we reach the lineDam buffer limit, if so, there is no need to keep buffering
      if (this.lastLineInDam >= DAM_MAX_LINES ||
          DAM_MAX_BUFFER_SIZE - this.curDamOffset <= length) {
        //we let open dam process this line as well as thing that already in Dam
        //In case there is nothing in dam because this line is too big, we need to
        //perform autodetect on this line
        status = this.OpenDAM(line, length);
      }
      else {
        //buffering current line
        this.lineDamPtrs[this.lastLineInDam] = this.lineDamBuffer + this.curDamOffset;
        memcpy(this.lineDamPtrs[this.lastLineInDam], line, length);
        this.lastLineInDam++;
        this.curDamOffset += length;
      }
    }
    else
      status = this.ConvertAndParseLine(line, length);
  }
  else
    status = this.ParseLine(line, length);

  return status;
}


} // namespace
