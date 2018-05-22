/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHDRS_H_
#define _MIMEHDRS_H_

#include "modlmime.h"

/**
 * The interface to message-header parsing and formatting code,
 * including conversion to HTML.
 *
 * This is an opaque object describing a block of message headers,
 * and a couple of routines for extracting data from one.
 */
class Headers
{
public:
  Headers();
  ~Headers();

  /**
   * Given the name of a header, returns the contents of that header as
   * a newly-allocated string (which the caller must free.)  If the header
   * is not present, or has no contents, NULL is returned.
   *
   * If `strip_p' is true, then the data returned will be the first token
   * of the header; else it will be the full text of the header.  (This is
   * useful for getting just "text/plain" from "text/plain; name=foo".)
   *
   * If `all_p' is false, then the first header encountered is used, and
   * any subsequent headers of the same name are ignored.  If true, then
   * all headers of the same name are appended together (this is useful
   * for gathering up all CC headers into one, for example.)
   */
  char* Get(const char* header_name,
            bool strip_p, bool all_p);

/**
 * Given a header string of the form of the MIME "Content-" headers,
 * extracts a named parameter from it, if it exists.
 * For example, GetParameter("text/plain; charset=us-ascii", "charset")
 * would return "us-ascii".
 *
 * Returns NULL if there is no match, or if there is an allocation failure.
 *
 * RFC2231 - MIME Parameter Value and Encoded Word Extensions: Character Sets,
 * Languages, and Continuations
 *
 * RFC2231 has added the character sets, languages, and continuations mechanism.
 * charset, and language information may also be returned to the caller.
 * Note that charset and language should be free()'d while the return value
 * (parameter) has to be PR_FREE'd.
 *
 * For example,
 * GetParameter("text/plain; name*=us-ascii'en-us'This%20is%20%2A%2A%2Afun%2A%2A%2A", "name")
 * GetParameter("text/plain; name*0*=us-ascii'en-us'This%20is%20; CRLFLWSPname*1*=%2A%2A%2Afun%2A%2A%2A", "name")
 * would return "This is ***fun***" and *charset = "us-ascii", *language = "en-us"
 */
  static char* GetParameter(
                              const char* header_value, const char* parm_name,
                              char **charset, char **language);

  /**
   * Does all the heuristic silliness to find the filename in the given headers.
   */
  char* GetFilename(MimeDisplayOptions* opt);

  /**
    * Convert this value to a unicode string, based on the charset.
    */
  static void ConvertHeaderValue(nsCString &value,
                                 DisplayOptions* opt, bool convert_charset_only);

  /**
   * Feed this method the raw data from which you would like a header
   * block to be parsed, one line at a time.  Feed it a blank line when
   * you're done.  Returns negative on allocation-related failure.
   */
  int ParseLine(const char *buffer, int32_t size);

  /**
   * Converts a MimeHeaders object into HTML,
   * by writing to the provided output function.
   */
  int WriteHeadersHTML(DisplayOptions* opt, bool attachment);

  /**
   * Writes all headers to the mime emitter.
   */
  int WriteAllHeaders(DisplayOptions* opt, bool attachment);

  /**
   * Writes the headers as text/plain.
   *
   * This writes out a blank line after the headers, unless
   * dont_write_content_type is true, in which case the header-block
   * is not closed off, and none of the Content- headers are written.
   */
  int WriteRawHeaders(DisplayOptions *opt, bool dont_write_content_type);

  /**
   * Returns a copy of this
   */
  Headers* Copy();

protected:
  void DoUnixDisplayHookHack();
  void Compact();
  static char* DecodeFilename(const char* name, const char* charset, DisplayOptions *opt);

protected:
  /**
   * The entire header section
   *
   * Not NULL-terminated, but the length is in |all_headers_fp|.
   */
  char* all_headers;

  /**
   * The length of all_headers
   */
  int32_t all_headers_fp;

  /**
   * The size of the allocated block.
   */
  int32_t all_headers_size;

  /**
   * Whether we've read the end-of-headers marker
   * (the terminating blank line).
   */
  bool done_p;

  /**
   * An array of length n_headers which points to the beginning of
   * each distinct header: just after the newline which terminated
   * the previous one.  This is to speed search.
   *
   *  This is not initialized until all the headers have been read.
   *
   * The length is in |heads_size|.
   */
  char** heads;

  /**
   * The length of |heads|, and consequently,
   * how many distinct headers are in here.
   */
  int32_t heads_size;

  char* obuffer;
  int32_t obuffer_size;
  int32_t obuffer_fp;

  /**
   * What a hack.  This is a place to write down the subject header,
   * after it's been charset-ified and stuff.  Remembered so that
   * we can later use it to generate the <title> tag.
   * Also works for giving names to RFC822 attachments.
   */
  char* munged_subject;
};

extern "C"  char * MIME_StripContinuations(char *original);

#endif /* _MIMEHDRS_H_ */
