/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEI_H_
#define _MIMEI_H_

/*
  This module, libmime, implements a general-purpose MIME parser.
  One of the methods provided by this parser is the ability to emit
  an HTML representation of it.

  All Mozilla-specific code is (and should remain) isolated in the
  file mimemoz.c.  Generally, if the code involves images, netlib
  streams it should be in mimemoz.c instead of in the main body of
  the MIME parser.

  The parser is object-oriented and fully buzzword-compliant.
  There is a class for each MIME type, and each class is responsible
  for parsing itself, and/or handing the input data off to one of its
  child objects.

  The class hierarchy is:

     Object (abstract)
      |
      +--- Container (abstract)
      |     |
      |     +--- Multipart (abstract)
      |     |     |
      |     |     +--- MultipartMixed
      |     |     |
      |     |     +--- MultipartDigest
      |     |     |
      |     |     +--- MultipartParallel
      |     |     |
      |     |     +--- MultipartAlternative
      |     |     |
      |     |     +--- MultipartRelated
      |     |     |
      |     |     +--- MultipartAppleDouble
      |     |     |
      |     |     +--- SunAttachment
      |     |     |
      |     |     \--- MultipartSigned (abstract)
      |     |          |
      |     |          \--- MultipartSignedCMS
      |     |
      |     +--- Encrypted (abstract)
      |     |     |
      |     |     \--- EncryptedPKCS7
      |     |
      |     +--- Xlateed (abstract)
      |     |     |
      |     |     \--- Xlateed
      |     |
      |     +--- Message
      |     |
      |     \--- UntypedText
      |
      +--- Leaf (abstract)
      |     |
      |     +--- Text (abstract)
      |     |     |
      |     |     +--- TextPlain
      |     |     |     |
      |     |     |     \--- TextHTMLAsPlaintext
      |     |     |
      |     |     +--- TextPlainFlowed
      |     |     |
      |     |     +--- TextHTML
      |     |     |     |
      |     |     |     +--- TextHTMLParsed
      |     |     |     |
      |     |     |     \--- TextHTMLSanitized
      |     |     |
      |     |     +--- TextRichtext
      |     |     |     |
      |     |     |     \--- TextEnriched
      |     |    |
      |     |    +--- TextVCard
      |     |
      |     +--- InlineImage
      |     |
      |     \--- ExternalObject
      |
      \--- ExternalBody


  =========================================================================

  Code style follows the Mozilla Style Guide
  <https://developer.mozilla.org/en-US/docs/Mozilla/Developer_guide/Coding_Style>
  Blocks will be indented 2 space.
  About 80-100 characters per line.
  Longer lines will be wrapped, and indented at least 4 spaces.
 */

#include "mimehdrs.h"
#include "nsTArray.h"
#include "mimeobj.h"

namespace mozilla::mime {

#ifdef ENABLE_SMIME
class nsICMSMessage;
#endif // ENABLE_SMIME

#define cpp_stringify_noop_helper(x)#x
#define cpp_stringify(x) cpp_stringify_noop_helper(x)

/**
 * Given a MIME content-type string, finds and returns
 * an appropriate subclass of |Part|.
 *
 * This is the main switchboard of libmime.
 * It decides which implementation class responds to which
 * MIME content type.
 *
 * @param exactMatch If |exactMatch| is true, then
 * only fully-known types will be returned.
 * For example:
 * - if true, "text/x-unknown" will return TextPlainType
 * - if false, "text/x-unknown" will return NULL
 * @returns  A class object
 */
extern PartClass* findClass(
    const char* content_type,
    Headers* hdrs,
    DisplayOptions* opts,
    bool exactMatch);

/**
 * Puts a part-number into a URL.  If append_p is true, then the part number
 * is appended to any existing part-number already in that URL; otherwise,
 *  it replaces it.
 */
extern char* SetURLPart(const char* url, const char* part, bool append);

/**
 * cut the part of url for display a attachment as a email.
 */
extern char* GetBaseURL(const char* url);

/**
 * Puts an *IMAP* part-number into a URL.
 */
extern char* SetURLIMAPPart(const char* url, const char* part, const char* libmimepart);


/**
 * Parse the various "?" options off the URL and into the options struct.
 * @param DisplayOptions {out}
 */
extern int ParseURLOptions(const char* url, DisplayOptions*);

class ParseStateObject {
public:

  ParseStateObject()
  {
    root = 0;
    separatorQueued = false;
    separatorSuppressed = false;
    firstPartWritten = false;
    postHeaderHTMLRun = false;
    firstDataWritten = false;
    decrypted = false;
    strippingPart = false;
  }

  /**
   * The outermost parser object
   */
  Part* root;

 /**
  * Whether a separator should be written out
  * before the next text is written.
  * This lets us write separators lazily, so that one doesn't appear
  * at the end, and so that more than one don't appear in a row.
  */
  bool separatorQueued;

  /**
   * Whether the currently-queued separator should not be printed.
   * This is a kludge to prevent separators from being printed just after
   * a header block.
   */
  bool separatorSuppressed;

  /**
   * State used for the "Show Attachments As Links" kludge.
   */
  bool firstPartWritten;

  /**
   * Whether we've run options->generatePostHeaderHTML().
   */
  bool postHeaderHTMLRun;

  /**
   * State used for Mozilla lazy-stream-creation evilness.
   */
  bool firstDataWritten;

  /**
   * If options->dexlate is true, then this will be set to indicate whether any
   * dexlateion did in fact occur.
   */
  bool decrypted;

  /**
   * if we're stripping parts, what parts to strip
   */
  nsTArray<nsCString> partsToStrip;

  /**
   * if we're detaching parts, where each part was detached to
   */
  nsTArray<nsCString> detachToFiles;

  bool strippingPart;

  /**
   * if we've detached this part, filepath of detached par
   */
  nsCString detachedFilePath;
};

extern int Options::Write(Headers*, DisplayOptions*,
                         const char* data, int32_t length,
                         bool userVisible);

/**
 * This struct is what we hang off of (context)->mime_data,
 * to remember info about the last MIME object we've parsed and displayed.
 * @see GuessURLContentName(). */
class DisplayData {
public:
  Part* lastParsedPbject;
  char* lastParsedURL;
};


} // namespace
#endif // _MIMEI_H_
