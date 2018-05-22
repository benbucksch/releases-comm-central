/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimetpla.h"
#include "mimebuf.h"
#include "prmem.h"
#include "plstr.h"
#include "mozITXTToHTMLConv.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsMimeStringResources.h"
#include "mimemoz2.h"
#include "nsIPrefBranch.h"
#include "prprf.h"
#include "nsMsgI18N.h"

namespace mozilla::mime {

#define SUPERCLASS Text

extern "C"
void
MimeTextBuildPrefixCSS(int32_t    quotedSizeSetting,   // mail.quoted_size
                       int32_t    quotedStyleSetting,  // mail.quoted_style
                       nsACString &citationColor,      // mail.citation_color
                       nsACString &style)
{
  switch (quotedStyleSetting)
  {
  case 0:     // regular
    break;
  case 1:     // bold
    style.AppendLiteral("font-weight: bold; ");
    break;
  case 2:     // italic
    style.AppendLiteral("font-style: italic; ");
    break;
  case 3:     // bold-italic
    style.AppendLiteral("font-weight: bold; font-style: italic; ");
    break;
  }

  switch (quotedSizeSetting)
  {
  case 0:     // regular
    break;
  case 1:     // large
    style.AppendLiteral("font-size: large; ");
    break;
  case 2:     // small
    style.AppendLiteral("font-size: small; ");
    break;
  }

  if (!citationColor.IsEmpty())
  {
    style += "color: ";
    style += citationColor;
    style += ';';
  }
}

int
TextPlain::ParseBegin()
{
  int status = 0;
  bool quoting = ( this.options
    && ( this.options->format_out == nsMimeOutput::nsMimeMessageQuoting ||
         this.options->format_out == nsMimeOutput::nsMimeMessageBodyQuoting
       )       );  // The output will be inserted in the composer as quotation
  bool plainHTML = quoting || (this.options &&
       (this.options->format_out == nsMimeOutput::nsMimeMessageSaveAs));
       // Just good(tm) HTML. No reliance on CSS.
  bool rawPlainText = this.options &&
       (this.options->format_out == nsMimeOutput::nsMimeMessageFilterSniffer
         || this.options->format_out == nsMimeOutput::nsMimeMessageAttach);

  status = SUPERCLASS::ParseBegin();
  if (status < 0) return status;

  if (!this.output_p) return 0;

  if (this.options &&
    this.options->write_html_p &&
    this.options->output_fn)
  {
      this.mCiteLevel = 0;

      // Get the prefs

      // Quoting
      this.mBlockquoting = true; // mail.quoteasblock

      // Viewing
      this.mQuotedSizeSetting = 0;   // mail.quoted_size
      this.mQuotedStyleSetting = 0;  // mail.quoted_style
      this.mCitationColor.Truncate();  // mail.citation_color
      this.mStripSig = true; // mail.strip_sig_on_reply
      bool graphicalQuote = true; // mail.quoted_graphical

      nsIPrefBranch *prefBranch = GetPrefBranch(this.options);
      if (prefBranch)
      {
        prefBranch->GetIntPref("mail.quoted_size", &(this.mQuotedSizeSetting));
        prefBranch->GetIntPref("mail.quoted_style", &(this.mQuotedStyleSetting));
        prefBranch->GetCharPref("mail.citation_color", this.mCitationColor);
        prefBranch->GetBoolPref("mail.strip_sig_on_reply", &(this.mStripSig));
        prefBranch->GetBoolPref("mail.quoted_graphical", &graphicalQuote);
        prefBranch->GetBoolPref("mail.quoteasblock", &(this.mBlockquoting));
      }

      if (!rawPlainText)
      {
        // Get font
        // only used for viewing (!plainHTML)
        nsAutoCString fontstyle;
        nsAutoCString fontLang;  // langgroup of the font

        // generic font-family name ( -moz-fixed for fixed font and NULL for
        // variable font ) is sufficient now that bug 105199 has been fixed.

        if (!this.options->variable_width_plaintext_p)
          fontstyle = "font-family: -moz-fixed";

        if (nsMimeOutput::nsMimeMessageBodyDisplay == this.options->format_out ||
            nsMimeOutput::nsMimeMessagePrintOutput == this.options->format_out)
        {
          int32_t fontSize;       // default font size
          int32_t fontSizePercentage;   // size percentage
          nsresult rv = GetMailNewsFont(this,
                             !this.options->variable_width_plaintext_p,
                             &fontSize, &fontSizePercentage, fontLang);
          if (NS_SUCCEEDED(rv))
          {
            if ( ! fontstyle.IsEmpty() ) {
              fontstyle += "; ";
            }
            fontstyle += "font-size: ";
            fontstyle.AppendInt(fontSize);
            fontstyle += "px;";
          }
        }

        // Opening <div>. We currently have to add formatting here. :-(
        nsAutoCString openingDiv;
        if (!quoting)
             /* 4.x' editor can't break <div>s (e.g. to interleave comments).
                We'll add the class to the <blockquote type=cite> later. */
        {
          openingDiv = "<div class=\"moz-text-plain\"";
          if (!plainHTML)
          {
            if (this.options->wrap_long_lines_p)
              openingDiv += " wrap=true";
            else
              openingDiv += " wrap=false";

            if (graphicalQuote)
              openingDiv += " graphical-quote=true";
            else
              openingDiv += " graphical-quote=false";

            if (!fontstyle.IsEmpty())
            {
              openingDiv += " style=\"";
              openingDiv += fontstyle;
              openingDiv += '\"';
            }
            if (!fontLang.IsEmpty())
            {
              openingDiv += " lang=\"";
              openingDiv += fontLang;
              openingDiv += '\"';
            }
          }
          openingDiv += "><pre wrap class=\"moz-quote-pre\">\n";
        }
        else
          openingDiv = "<pre wrap class=\"moz-quote-pre\">\n";

      // text/plain objects always have separators before and after them
      status = this.WriteSeparator();
      if (status < 0) return status;

      status = this.Write(openingDiv.get(), openingDiv.Length(), true);
      if (status < 0) return status;
    }
  }

  return 0;
}

int
TextPlain::ParseEOF(bool abort_p)
{
  int status;

  // Has this method already been called for this object?
  // In that case return.
  if (this.closed_p) return 0;

  nsCString citationColor;
  if (text && !this.mCitationColor.IsEmpty())
    citationColor = this.mCitationColor;

  bool quoting = ( this.options
    && ( this.options->format_out == nsMimeOutput::nsMimeMessageQuoting ||
         this.options->format_out == nsMimeOutput::nsMimeMessageBodyQuoting
       )           );  // see above

  bool rawPlainText = this.options &&
       (this.options->format_out == nsMimeOutput::nsMimeMessageFilterSniffer
        || this.options->format_out == nsMimeOutput::nsMimeMessageAttach);

  /* Run parent method first, to flush out any buffered data. */
  status = SUPERCLASS::ParseEOF(abort_p);
  if (status < 0) return status;

  if (!this.output_p) return 0;

  if (this.options &&
    this.options->write_html_p &&
    this.options->output_fn &&
    !abort_p && !rawPlainText)
  {
      if (this.mIsSig && !quoting)
      {
        status = this.Write("</div>", 6, false);  // .moz-txt-sig
        if (status < 0) return status;
      }
      status = this.Write( "</pre>", 6, false);
      if (status < 0) return status;
      if (!quoting)
      {
        status = this.Write("</div>", 6, false); // .moz-text-plain
        if (status < 0) return status;
      }

      /* text/plain objects always have separators before and after them.
     Note that this is not the case for text/enriched objects.
     */
    status = this.WriteSeparator();
    if (status < 0) return status;
  }

  return 0;
}


int
TextPlain::ParseLine(const char* line, int32_t length)
{
  bool quoting = ( this.options
    && ( this.options->format_out == nsMimeOutput::nsMimeMessageQuoting ||
         this.options->format_out == nsMimeOutput::nsMimeMessageBodyQuoting
       )           );  // see above
  bool plainHTML = quoting || (this.options &&
       this.options->format_out == nsMimeOutput::nsMimeMessageSaveAs);
       // see above

  bool rawPlainText = this.options &&
       (this.options->format_out == nsMimeOutput::nsMimeMessageFilterSniffer
       || this.options->format_out == nsMimeOutput::nsMimeMessageAttach);

  // this routine gets called for every line of data that comes through the
  // mime converter. It's important to make sure we are efficient with
  // how we allocate memory in this routine. be careful if you go to add
  // more to this routine.

  NS_ASSERTION(length > 0, "zero length");
  if (length <= 0) return 0;

  mozITXTToHTMLConv* conv = GetTextConverter(this.options);

  bool skipConversion = !conv || rawPlainText ||
                          (this.options && this.options->force_user_charset);

  char* mailCharset = NULL;
  nsresult rv;
  int status;

  if (!skipConversion)
  {
    nsDependentCString inputStr(line, length);
    nsAutoString lineSourceStr;

    // For 'SaveAs', |line| is in |mailCharset|.
    // convert |line| to UTF-16 before 'html'izing (calling ScanTXT())
    if (this.options->format_out == nsMimeOutput::nsMimeMessageSaveAs)
    { // Get the mail charset of this message.
      if (!this.initializedCharset)
         this.InitializeCharset(this);
      mailCharset = this.charset;
      if (mailCharset && *mailCharset) {
        rv = nsMsgI18NConvertToUnicode(nsDependentCString(mailCharset),
                                       inputStr,
                                       lineSourceStr);
        NS_ENSURE_SUCCESS(rv, -1);
      }
      else // this probably never happens ...
        CopyUTF8toUTF16(inputStr, lineSourceStr);
    }
    else  // line is in UTF-8
      CopyUTF8toUTF16(inputStr, lineSourceStr);

    nsAutoCString prefaceResultStr;  // Quoting stuff before the real text

    // Recognize quotes
    uint32_t oldCiteLevel = this.mCiteLevel;
    uint32_t logicalLineStart = 0;
    rv = conv->CiteLevelTXT(lineSourceStr.get(),
                            &logicalLineStart, &(this.mCiteLevel));
    NS_ENSURE_SUCCESS(rv, -1);

    // Find out, which recognitions to do
    uint32_t whattodo = this.options->whattodo;
    if (plainHTML)
    {
      if (quoting)
        whattodo = 0;  // This is done on Send. Don't do it twice.
      else
        whattodo = whattodo & ~mozITXTToHTMLConv::kGlyphSubstitution;
                   /* Do recognition for the case, the result is viewed in
                      Mozilla, but not GlyphSubstitution, because other UAs
                      might not be able to display the glyphs. */
      if (!this.mBlockquoting)
        this.mCiteLevel = 0;
    }

    // Write blockquote
    if (this.mCiteLevel > oldCiteLevel)
    {
      prefaceResultStr += "</pre>";
      for (uint32_t i = 0; i < this.mCiteLevel - oldCiteLevel; i++)
      {
        nsAutoCString style;
        MimeTextBuildPrefixCSS(this.mQuotedSizeSetting, this.mQuotedStyleSetting,
                               this.mCitationColor, style);
        if (!plainHTML && !style.IsEmpty())
        {
          prefaceResultStr += "<blockquote type=cite style=\"";
          prefaceResultStr += style;
          prefaceResultStr += "\">";
        }
        else
          prefaceResultStr += "<blockquote type=cite>";
      }
      prefaceResultStr += "<pre wrap class=\"moz-quote-pre\">\n";
    }
    else if (this.mCiteLevel < oldCiteLevel)
    {
      prefaceResultStr += "</pre>";
      for (uint32_t i = 0; i < oldCiteLevel - this.mCiteLevel; i++)
        prefaceResultStr += "</blockquote>";
      prefaceResultStr += "<pre wrap class=\"moz-quote-pre\">\n";
    }

    // Write plain text quoting tags
    if (logicalLineStart != 0 && !(plainHTML && this.mBlockquoting))
    {
      if (!plainHTML)
        prefaceResultStr += "<span class=\"moz-txt-citetags\">";

      nsString citeTagsSource(StringHead(lineSourceStr, logicalLineStart));

      // Convert to HTML
      nsString citeTagsResultUnichar;
      rv = conv->ScanTXT(citeTagsSource.get(), 0 /* no recognition */,
                         getter_Copies(citeTagsResultUnichar));
      if (NS_FAILED(rv)) return -1;

      prefaceResultStr.Append(NS_ConvertUTF16toUTF8(citeTagsResultUnichar));
      if (!plainHTML)
        prefaceResultStr += "</span>";
    }


    // recognize signature
    if ((lineSourceStr.Length() >= 4)
        && lineSourceStr.First() == '-'
        && Substring(lineSourceStr, 0, 3).EqualsLiteral("-- ")
        && (lineSourceStr[3] == '\r' || lineSourceStr[3] == '\n') )
    {
      this.mIsSig = true;
      if (!quoting)
        prefaceResultStr += "<div class=\"moz-txt-sig\">";
    }


    /* This is the main TXT to HTML conversion:
       escaping (very important), eventually recognizing etc. */
    nsString lineResultUnichar;

    rv = conv->ScanTXT(lineSourceStr.get() + logicalLineStart,
                       whattodo, getter_Copies(lineResultUnichar));
    NS_ENSURE_SUCCESS(rv, -1);

    if (!(this.mIsSig && quoting && this.mStripSig))
    {
      status = this.Write(prefaceResultStr.get(), prefaceResultStr.Length(), true);
      if (status < 0) return status;
      nsAutoCString outString;
      if (this.options->format_out != nsMimeOutput::nsMimeMessageSaveAs ||
          !mailCharset || !*mailCharset)
        CopyUTF16toUTF8(lineResultUnichar, outString);
      else
      { // convert back to mailCharset before writing.
        rv = nsMsgI18NConvertFromUnicode(nsDependentCString(mailCharset),
                                         lineResultUnichar,
                                         outString);
        NS_ENSURE_SUCCESS(rv, -1);
      }

      status = this.Write(outString.get(), outString.Length(), true);
    }
    else
    {
      status = 0;
    }
  }
  else
  {
    status = this.Write(line, length, true);
  }

  return status;
}

