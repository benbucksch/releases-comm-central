/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMREL_H_
#define _MIMEMREL_H_

#include "mimemult.h"
#include "plhash.h"
#include "prio.h"
#include "nsNetUtil.h"
#include "modmimee.h" // for MimeConverterOutputCallback

namespace mozilla {
namespace mime {

/**
 * The MultipartRelated class implements the multipart/related MIME
 * container, which allows `sibling' sub-parts to refer to each other.
 */
class MultipartRelated : public Multipart {
  typedef Multipart Super;

public:
  MultipartRelated();
  virtual ~MultipartRelated();

  // Part overrides
  int ParseEOF(bool abort_p);

  // Multipart overrides
  bool OutputChild(Part* child);
  int ParseChildLine(const char* line, int32_t length, bool first_line_p);

private:
  static int NukeHash(PLHashEntry* table, int indx, void* arg);
  bool StartParamExists(Part* child);
  bool ThisIsStartPart(Part* child);
  int RealWrite(const char* buf, int32_t size);
  int PushTag(const char* buf, int32_t size);
  bool AcceptRelatedPart(Part* part_obj);
  int FlushTag();
  int ParseTags(const char* buf, int32_t size);

  /**
   *  Base URL (if any) for the whole multipart/related.
   */
  char* base_url;
  /**
   * Buffer used to remember the text/html 'head' part.
   */
  char* head_buffer;
  /**
   * Active length.
   */
  uint32_t head_buffer_fp;
  /**
   * How big it is.
   */
  uint32_t head_buffer_size;
  /**
   * The nsIFile of a temp file used when we run out of room in the head_buffer.
   */
  nsCOMPtr <nsIFile>          file_buffer;
  /**
   * A stream to it.
   */
  nsCOMPtr <nsIInputStream>   input_file_stream;
  /**
   * A stream to it.
   */
  nsCOMPtr <nsIOutputStream>  output_file_stream;
  /**
   * The headers of the 'head' part.
   */
  Headers* buffered_hdrs;
  /**
   * Whether we've already passed the 'head' part.
   */
  bool head_loaded;
  /**
   * The actual text/html head object.
   */
  Part* headobj;

  PLHashTable    *hash;

  MimeConverterOutputCallback real_output_fn;
  void* real_output_closure;

  char* curtag;
  int32_t curtag_max;
  int32_t curtag_length;
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEMREL_H_ */
