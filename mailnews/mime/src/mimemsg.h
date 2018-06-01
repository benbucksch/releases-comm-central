/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEMSG_H_
#define _MIMEMSG_H_

#include "mimecont.h"

namespace mozilla {
namespace mime {

/**
 * The Message class implements the message/rfc822 and message/news
 * MIME containers, which is to say, mail and news messages.
 */
class Message : public Container {
  typedef Container Super;

protected:
  Message();
  virtual ~Message();

public:
  // Part overrides
  virtual int ParseBegin() override;
  virtual int ParseLine(const char* aLine, int32_t aLength) override;
  virtual int ParseEOF(bool abort_p) override;
#if defined(DEBUG) && defined(XP_UNIX)
  virtual int DebugPrint(PRFileDesc* stream, int32_t depth);
#endif

  // Container overrides
  virtual int AddChild(Part* child) override;

private:
  int CloseHeaders();
  char* DetermindMailCharset();
  int WriteHeadersHTML();

  /**
   * Whether this |Part| has written out the HTML version of its headers
   * in such a way that it will have a "crypto stamp" next to the headers.  If
   * this is true, then the child must write out its HTML slightly differently
   * to take this into account...
   */
  bool IsCryptoStamped();

  Headers *hdrs;      /* headers of this message */
  bool newline_p;      /* whether the last line ended in a newline */
  bool crypto_stamped_p;    /* whether the header of this message has been
                   emitted expecting its child to emit HTML
                   which says that it is xlated. */

  bool crypto_msg_signed_p;  /* What the emitted xlation-stamp *says*. */
  bool crypto_msg_encrypted_p;
  bool grabSubject;  /* Should we try to grab the subject of this message */
  int32_t bodyLength; /* Used for determining if the body has been truncated */
  int32_t sizeSoFar; /* The total size of the MIME message, once parsing is
                        finished. */
};

} // namespace mime
} // namespace mozilla

#endif /* _MIMEMSG_H_ */
