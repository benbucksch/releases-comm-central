/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMETEXT_H_
#define _MIMETEXT_H_

#include "mimeleaf.h"

namespace mozilla::mime {

/* The |Text| class is the superclass of all handlers for the MIME
   text/ content types, which convert various text formats
   to HTML, in one form or another.

   It provides two services:

   =  text will be converted from the message's charset to the "target"
        charset before the `parse_line' method is called.

   =  if ROT13 decoding is desired, the text will be rotated before
      the `parse_line' method it called;

   The contract with charset-conversion is that the converted data will
   be such that one may interpret any octets (8-bit bytes) in the data
   which are in the range of the ASCII characters (0-127) as ASCII
   characters.  It is explicitly legal, for example, to scan through
   the string for "<" and replace it with "&lt;", and to search for things
   that look like URLs and to wrap them with interesting HTML tags.

   The charset to which we convert will probably be UTF-8 (an encoding of
   the Unicode character set, with the feature that all octets with the
   high bit off have the same interpretations as ASCII.)

   NOTE: if it turns out that we use JIS (ISO-2022-JP) as the target
    encoding, then this is not quite true; it is safe to search for the
    low ASCII values (under hex 0x40, octal 0100, which is '@') but it
    is NOT safe to search for values higher than that -- they may be
    being used as the subsequent bytes in a multi-byte escape sequence.
    It's a nice coincidence that HTML's critical characters ("<", ">",
    and "&") have values under 0x40...
 */
abstract class Text : public Leaf {
public:
  Text();
  virtual ~Text();

  override int ParseDecodedBuffer(const char* buf, int32_t size);
  override int ParseEOF(bool abort_p);

  int OpenDAM(char* line, int32_t length);
  int ROT13Line(char* line, int32_t length);
  int ConvertLineCharset(char* line, int32_t length);
  int InitializeCharset();

  /**
   * The charset from the content-type of this object,
   * or the caller-specified overrides or defaults.
   */
  char *charset;
  bool charsetOverridable;
  bool needUpdateMsgWinCharset;
  /**
   * Buffer used for charset conversion.
   */
  char *cbuffer;
  int32_t cbuffer_size;

  bool    inputAutodetect;
  bool    initializedCharset;
  int32_t lastLineInDam;
  int32_t curDamOffset;
  char *lineDamBuffer;
  char **lineDamPtrs;
};

class TextClass : LeafClass {
};

#define DAM_MAX_BUFFER_SIZE 8*1024
#define DAM_MAX_LINES  1024

} // namespace
#endif // _MIMETEXT_H_
