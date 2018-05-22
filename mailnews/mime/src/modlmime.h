/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _LIBMIME_H_
#define _LIBMIME_H_

#ifdef XP_UNIX
#undef Bool
#endif

#include "nsString.h"
#include "nsMailHeaders.h"
#include "nsIMimeStreamConverter.h"
#include "mozilla/Encoding.h"
#include "nsIPrefBranch.h"
#include "mozITXTToHTMLConv.h"
#include "nsCOMPtr.h"
#include "modmimee.h" // for MimeConverterOutputCallback

namespace mozilla::mime {

#define MIME_DRAFTS

class DisplayOptions;
class ParseStateObject;
typedef struct MSG_AttachmentData MSG_AttachmentData;

enum class HeadersState {
  /**
   * Show all headers
   */
  All,
  /**
   * Show all "interesting" headers
   */
  Some,
  /**
   * Same, but suppress the `References' header.
   * For when we're printing this message.
   */
  SomeNoRef,
  /**
   * Show a one-line header summary
   */
  Micro,
  /**
   * Same, but show the full recipient list as well (To, CC, etc.)
   */
  MicroPlus,
  /**
   * A one-line summary geared toward use in a reply citation,
   * e.g. "So-and-so wrote:"
   */
  Citation,
  /**
   * Just parse and output headers...nothing else
   */
  Only,
  /**
   * Skip showing any headers
   */
  None
};


/* The signature for various callbacks in the MimeDisplayOptions structure.
 */
typedef char *(*MimeHTMLGeneratorFunction) (const char *data, void *closure,
                      Headers *headers);

class DisplayOptions
{
public:
  MimeDisplayOptions();
  virtual ~DisplayOptions();
  mozITXTToHTMLConv   *conv;        // For text conversion...
  nsCOMPtr<nsIPrefBranch> m_prefBranch; /* prefBranch-service */
  nsMimeOutputType    format_out;   // The format out type

  const char *url;      /* Base URL for the document.  This string should
                 be freed by the caller, after the parser
                 completes (possibly at the same time as the
                 MimeDisplayOptions itself.) */

  HeadersState headers;  /* How headers should be displayed. */
  bool fancy_headers_p;  /* Whether to do clever formatting of headers
                 using tables, instead of spaces. */

  bool output_vcard_buttons_p;  /* Whether to output the buttons */
                  /* on vcards. */

  bool variable_width_plaintext_p;  /* Whether text/plain messages should
                       be in variable width, or fixed. */
  bool wrap_long_lines_p;  /* Whether to wrap long lines in text/plain
                   messages. */

  bool rot13_p;      /* Whether text/plain parts should be rotated
                 Set by "?rot13=true" */
  char *part_to_load;    /* The particular part of the multipart which
                 we are extracting.  Set by "?part=3.2.4" */

  bool no_output_p; /* Will never write output when this is true.
          (When false, output or not may depend on other things.)
          This needs to be in the options, because the method
          Part_ParseBegin is controlling the property "output_p"
          (on the Part) so we need a way for other functions to
          override it and tell that we do not want output. */

  bool write_html_p;    /* Whether the output should be HTML, or raw. */

  bool decrypt_p;    /* Whether all traces of xlateion should be
                 eradicated -- this is only meaningful when
                 write_html_p is false; we set this when
                 attaching a message for forwarding, since
                 forwarding someone else a message that wasn't
                 xlated for them doesn't work.  We have to
                 dexlate it before sending it.
               */

  uint32_t whattodo ;       /* from the prefs, we'll get if the user wants to do glyph or structure substitutions and set this member variable. */

  char *default_charset;  /* If this is non-NULL, then it is the charset to
                 assume when no other one is specified via a
                 `charset' parameter.
               */
  bool override_charset;  /* If this is true, then we will assume that
                 all data is in the default_charset, regardless
                               of what the `charset' parameter of that part
                               says. (This is to cope with the fact that, in
                               the real world, many messages are mislabelled
                               with the wrong charset.)
               */
  bool    force_user_charset; /* this is the new strategy to deal with incorrectly
                                 labeled attachments */

  /* =======================================================================
   Stream-related callbacks; for these functions, the `closure' argument
   is what is found in `options->stream_closure'.  (One possible exception
   is for output_fn; see "output_closure" below.)
   */
  void *stream_closure;

  /* For setting up the display stream, so that the MIME parser can inform
   the caller of the type of the data it will be getting. */
  int (*output_init_fn) (const char *type,
             const char *charset,
             const char *name,
             const char *x_mac_type,
             const char *x_mac_creator,
             void *stream_closure);

  /* How the MIME parser feeds its output (HTML or raw) back to the caller. */
  MimeConverterOutputCallback output_fn;

  /* Closure to pass to the above output_fn.  If NULL, then the
   stream_closure is used. */
  void *output_closure;

  /* A hook for the caller to perform charset-conversion before HTML is
   returned.  Each set of characters which originated in a mail message
   (body or headers) will be run through this filter before being converted
   into HTML.  (This should return bytes which may appear in an HTML file,
   ie, we must be able to scan through the string to search for "<" and
   turn it in to "&lt;", and so on.)

   `input' is a non-NULL-terminated string of a single line from the message.
   `input_length' is how long it is.
   `input_charset' is a string representing the charset of this string (as
     specified by MIME headers.)
   The conversion is always to UTF-8.
   `output_ret' is where a newly-malloced string is returned.  It may be
     NULL if no translation is needed.
   `output_size_ret' is how long the returned string is (it need not be
     NULL-terminated.).
   */
  int (*charset_conversion_fn) (const char *input_line,
                int32_t input_length, const char *input_charset,
                nsACString& output_ret,
                void *stream_closure);

  /* If true, perform both charset-conversion and decoding of
   MIME-2 header fields (using RFC-1522 encoding.)
   */
  bool rfc1522_conversion_p;

  /* A hook for the caller to turn a file name into a content-type. */
  char *(*file_type_fn) (const char *filename, void *stream_closure);

  /* A hook by which the user may be prompted for a password by the security
   library.  (This is really of type `SECKEYGetPasswordKey'; see sec.h.) */
  void *(*passwd_prompt_fn)(void *arg1, void *arg2);

  /* =======================================================================
   Various callbacks; for all of these functions, the `closure' argument
   is what is found in `html_closure'.
   */
  void *html_closure;

  /* For emitting some HTML before the start of the outermost message
   (this is called before any HTML is written to layout.) */
  MimeHTMLGeneratorFunction generate_header_html_fn;

  /* For emitting some HTML after the outermost header block, but before
   the body of the first message. */
  MimeHTMLGeneratorFunction generate_post_header_html_fn;

  /* For emitting some HTML at the very end (this is called after libmime
   has written everything it's going to write.) */
  MimeHTMLGeneratorFunction generate_footer_html_fn;

  /* For turning a message ID into a loadable URL. */
  MimeHTMLGeneratorFunction generate_reference_url_fn;

  /* For turning a mail address into a mailto URL. */
  MimeHTMLGeneratorFunction generate_mailto_url_fn;

  /* For turning a newsgroup name into a news URL. */
  MimeHTMLGeneratorFunction generate_news_url_fn;

  /* =======================================================================
     Callbacks to handle the backend-specific inlined image display
   (internal-external-reconnect junk.)  For `image_begin', the `closure'
   argument is what is found in `stream_closure'; but for all of the
   others, the `closure' argument is the data that `image_begin' returned.
   */

  /* Begins processing an embedded image; the URL and content_type are of the
   image itself. */
  void *(*image_begin) (const char *image_url, const char *content_type,
            void *stream_closure);

  /* Stop processing an image. */
  void (*image_end) (void *image_closure, int status);

  /* Dump some raw image data down the stream. */
  int (*image_write_buffer) (const char *buf, int32_t size, void *image_closure);

  /* What HTML should be dumped out for this image. */
  char *(*make_image_html) (void *image_closure);


  /* =======================================================================
   Other random opaque state.
   */
  ParseStateObject *state;    /* Some state used by libmime internals;
                     initialize this to 0 and leave it alone.
                   */


#ifdef MIME_DRAFTS
  /* =======================================================================
  Mail Draft hooks -- 09-19-1996
   */
  bool decompose_file_p;            /* are we decomposing a mime msg
                     into separate files */
  bool done_parsing_outer_headers;  /* are we done parsing the outer message
                      headers; this is really useful when
                      we have multiple Message/RFC822
                      headers */
  bool is_multipart_msg;            /* are we decomposing a multipart
                      message */

  int decompose_init_count;            /* used for non multipart message only
                    */

  bool signed_p;             /* to tell draft this is a signed
                      message */

  bool caller_need_root_headers;    /* set it to true to receive the message main
                                         headers through the callback
                                         decompose_headers_info_fn */

  /* Callback to gather the outer most headers so we could use the
   information to initialize the addressing/subject/newsgroups fields
   for the composition window. */
  int (*decompose_headers_info_fn) (void *closure,
                  MimeHeaders *headers);

  /* Callbacks to create temporary files for drafts attachments. */
  int (*decompose_file_init_fn) (void *stream_closure,
                 MimeHeaders *headers );

  MimeConverterOutputCallback decompose_file_output_fn;

  int (*decompose_file_close_fn) (void *stream_closure);
#endif /* MIME_DRAFTS */

  int32_t attachment_icon_layer_id; /* Hackhackhack.  This is zero if we have
                   not yet emitted the attachment layer
                   stuff.  If we have, then this is the
                   id number for that layer, which is a
                   unique random number every time, to keep
                   evil people from writing javascript code
                   to hack it. */

  bool missing_parts;  /* Whether or not this message is going to contain
                 missing parts (from IMAP Mime Parts On Demand) */

  bool show_attachment_inline_p; /* Whether or not we should display attachment inline (whatever say
                         the content-disposition) */

  bool quote_attachment_inline_p; /* Whether or not we should include inlined attachments in
                         quotes of replies) */

  int32_t html_as_p; /* How we should display HTML, which allows us to know if we should display all body parts */

  /**
   * Should StartBody/EndBody events be generated for nested MimeMessages.  If
   *  false (the default value), the events are only generated for the outermost
   *  MimeMessage.
   */
  bool notify_nested_bodies;

  /**
   * When true, compels mime parts to only write the actual body
   *  payload and not display-gunk like links to attachments. This was
   *  primarily introduced for the benefit of the javascript emitter.
   */
  bool write_pure_bodies;

  /**
   * When true, only processes metadata (i.e. size) for streamed attachments.
   *  Mime emitters that expect any attachment data (including inline text and
   *  image attachments) should leave this as false (the default value).  At
   *  the moment, only the JS mime emitter uses this.
   */
  bool metadata_only;
};

} // namespace
#endif // _MODLMIME_H_
