/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEUNTY_H_
#define _MIMEUNTY_H_

#include "mimecont.h"

namespace mozilla {
namespace mime {

/**
 * The MimeUntypedText class is used for untyped message contents, that is,
 * it is the class used for the body of a message/rfc822 object which had
 * *no* Content-Type header, as opposed to an unrecognized content-type.
 * Such a message, technically, does not contain MIME data (it follows only
 * RFC 822, not RFC 1521.)
 *
 * This is a container class, and the reason for that is that it loosely
 * parses the body of the message looking for ``sub-parts'' and then
 * creates appropriate containers for them.
 *
 * More specifically, it looks for uuencoded data.  It may do more than that
 * some day.
 *
 * Basically, the algorithm followed is:
 *
 *   if line is "begin 644 foo.gif"
 *     if there is an open sub-part, close it
 *   add a sub-part with type: image/gif; encoding: x-uue
 *   hand this line to it
 *   and hand subsequent lines to that subpart
 * else if there is an open uuencoded sub-part, and line is "end"
 *   hand this line to it
 *   close off the uuencoded sub-part
 *   else if there is an open sub-part
 *   hand this line to it
 *   else
 *     open a text/plain subpart
 *   hand this line to it
 *
 * Adding other types than uuencode to this (for example, PGP) would be
 * pretty straightforward.
 */
class UntypedText : public Container {
  typedef Container Super;

public:
  UntypedText();
  virtual ~UntypedText();

  // Part overrides
  virtual int ParseLine(const char* line, int32_t length) override;

private:
  enum class SubpartType {
    /**
     * Plain text
     */
    Text,
    /**
     * UUEncoded data
     */
    UUE,
    /**
     * YEncoded data
     */
    YEnc,
    /**
     * Mac Binhex data
     */
    Binhex
  };
  int OpenSubpart(SubpartType type, const char* type, const char* enc, const char* name, const char* desc);
  int CloseSubpart();

  bool LineBeginsUUE(const char* line, int32_t length, char **type_ret, char **name_ret);
  bool LineEndsUUE(const char* line, int32_t length);
  bool LineBeginsYEnc(const char* line, int32_t length, char **type_ret, char **name_ret);
  bool LineEndsYEnc(const char* line, int32_t length);
  bool LineBeginsBinhex(const char* line, int32_t length);
  bool LineEndsBinhex(const char* line, int32_t length);

  /**
   * The part still being parsed
   */
  Part *open_subpart;
  /**
   * What type it is
   */
  SubpartType type;
  /**
   * Fake headers describing it
   */
  Headers *open_hdrs;
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEUNTY_H_ */
