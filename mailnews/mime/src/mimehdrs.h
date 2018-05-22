/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEHDRS_H_
#define _MIMEHDRS_H_

#include "modlmime.h"

/* This file defines the interface to message-header parsing and formatting
   code, including conversion to HTML. */

/* Other structs defined later in this file.
 */

/* Some crypto-related HTML-generated utility routines.
 * XXX This may not be needed. XXX
 */
extern char *MimeHeaders_open_crypto_stamp(void);
extern char *MimeHeaders_finish_open_crypto_stamp(void);
extern char *MimeHeaders_close_crypto_stamp(void);
extern char *MimeHeaders_make_crypto_stamp(bool encrypted_p,
   bool signed_p,
   bool good_p,
   bool unverified_p,
   bool close_parent_stamp_p,
   const char *stamp_url);

extern char *mime_decode_filename(const char *name, const char* charset,
                                  MimeDisplayOptions *opt);

extern "C"  char * MIME_StripContinuations(char *original);

/**
 * Convert this value to a unicode string, based on the charset.
 */
extern void MimeHeaders_convert_header_value(MimeDisplayOptions *opt,
                                             nsCString &value,
                                             bool convert_charset_only);
#endif /* _MIMEHDRS_H_ */
