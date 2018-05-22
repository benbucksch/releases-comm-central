/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMULT_H_
#define _MIMEMULT_H_

#include "mimecont.h"

namespace mozilla::mime {

enum class MultipartParseState{
  Preamble,
  Headers,
  PartFirstLine,
  PartLine,
  Epilogue
};

enum class MultipartBoundaryType {
  None,
  Separator,
  Terminator
};

/**
 * The MimeMultipart class class implements the objects representing all of
 * the "multipart/" MIME types.
 */
class Multipart : Container {
public:
  override int ParseLine(const char* line, int32_t length);
  override int ParseEOF(bool abort_p);

  /**
   * When it has been determined that a new sub-part should be created,
   * this method is called to do that.
   *
   * The default value for this method does it in the usual multipart/mixed way.
   *
   * The headers of the object-to-be-created may be found
   * in the |hdrs| variable of the |Multipart| object.
   */
  virtual int CreateChild();
  /**
   * Whether this child should be output.
   *
   * Default method always says yes.
   */
  virtual bool CanOutputChild(Part* child);

  /**
   * When we reach the end of a sub-part (a separator line) this method is
   * called to shut down the currently-active child.
   *
   * The default method simply calls `ParseEOF' on the most-recently-added
   * child object.
   */
  virtual int CloseChild();

  /**
   * When we have a line which should be handed off to the currently-active
   * child object, this method is called to do that.
   *
   * The default method simply passes the line to the most-recently-added
   * child object.
   *
   * @param isFirstLine will be true only for the very first line
   *     handed off to this sub-part.
   */
  virtual int ParseChildLine(const char* line, int32_t length, bool isFirstLine);

  /**
   * This method is used to examine a line and determine whether it is a
   * part boundary, and if so, what kind.
   *
   * @returns a MultipartBoundaryType describing the line.
   */
  virtual MultipartBoundaryType CheckBoundary(const char* line, int32_t length);

  /**
   * Utility function that calles this.Write() with an nsCString
   */
  int WriteCString(const nsACString &string);

  NotifyEmitter();

  /**
   * This is the type which should be assumed for sub-parts which have
   * no explicit type specified.
   *
   * The default is "text/plain",
   * but the "multipart/digest" subclass overrides this to "message/rfc822".
   */
  const char* default_part_type;

protected:
  char *boundary; // Inter-part delimiter string
  Headers *hdrs; // headers of the part currently being parsed, if any
  MultipartParseState state; // State of parser

protected:
  void NotifyEmitter();
};

class MultipartClass : ContainerClass {
}

extern void MimeMultipart_notify_emitter(Part *);

} // namespace
#endif // _MIMEMULT_H_
