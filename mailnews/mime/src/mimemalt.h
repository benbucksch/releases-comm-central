/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMALT_H_
#define _MIMEMALT_H_

#include "mimemult.h"
#include "mimepbuf.h"

namespace mozilla {
namespace mime {

/**
 * The MultipartAlternative class implements the multipart/alternative
 * MIME container, which displays only one (the `best') of a set of enclosed
 * documents.
 */
class MultipartAlternative : public Multipart {
  typedef Multipart Super;

public:
  MultipartAlternative();
  ~MultipartAlternative();

  // Part overrides
  virtual int ParseEOF(bool abort_p) override;

  // Multipart overrides
  virtual int CreateChild() override;
  virtual int ParseChildLine(const char* line, int32_t length, bool first_line_p) override;
  virtual int CloseChild() override;

private:
  enum class Priority {
    Undisplayable,
    Low,
    TextUnknown,
    TextPlain,
    Normal,
    High,
    Highest
  };

  void Cleanup();
  int FlushChildren(bool finished, Priority next_priority);
  Priority DisplayPart(Headers *sub_hdrs);
  Priority PrioritizePart(char* content_type, bool prefer_plaintext);
  int DisplayCachedPart(Headers* hdrs, MimePartBufferData* buffer, bool do_display);

  /**
   * The headers of pending parts.
   */
  Headers **buffered_hdrs;
  /**
   * The data of pending parts (see mimepbuf.h)
   */
  MimePartBufferData **part_buffers;
  int32_t pending_parts;
  int32_t max_parts;
  /**
   * Priority of head of pending parts.
   */
  Priority buffered_priority;
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEMALT_H_ */
