/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMELEAF_H_
#define _MIMELEAF_H_

#include "mimeobj.h"
#include "modmimee.h"

namespace MIME {

/**
 * MimeLeaf is the class for the objects representing all MIME types which
 * are not containers for other MIME objects.  The implication of this is
 * that they are MIME types which can have Content-Transfer-Encodings
 * applied to their data.
 *
 * This class provides that service in its ParseBuffer() method.
 */
public class Leaf {
public:
  Leaf();
  virtual ~Leaf();

  /**
   * This is the callback that is handed to the decoder.
   *
   * The ParseBuffer() method of MimeLeaf passes each block of data through
   * the appropriate decoder (if any) and then calls ParseDecodedBuffer()
   * on each block (not line) of output.
   *
   * The default ParseDecodedBuffer() method of MimeLeaf line-buffers the
   * now-decoded data, handing each line to the ParseLine() method in turn.
   * If different behavior is desired (for example, if a class wants access
   * to the decoded data before it is line-buffered), the ParseDecodedBuffer()
   * method should be overridden. ExternalObject does this.
   */
  int ParseDecodedBuffer(const char* buf, int32_t size);
  int CloseDecoder();

  /**
   * If we're doing Base64, Quoted-Printable, or UU decoding,
   * this is the state object for the decoder. */
  DecoderData* decoder_data;

  /**
   * We want to count the size of the |Part| to offer consumers the
   * opportunity to display the sizes of attachments.
   */
  int sizeSoFar;
};

class LeafClass : PartClass {
}

} // namespace
#endif // _MIMELEAF_H_
