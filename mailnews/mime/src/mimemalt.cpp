/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
  BACKGROUND
  ----------

  At the simplest level, multipart/alternative means "pick one of these and
  display it." However, it's actually a lot more complicated than that.

  The alternatives are in preference order, and counterintuitively, they go
  from *least* to *most* preferred rather than the reverse. Therefore, when
  we're parsing, we can't just take the first one we like and throw the rest
  away -- we have to parse through the whole thing, discarding the n'th part if
  we are capable of displaying the n+1'th.

  Adding a wrinkle to that is the fact that we give the user the option of
  demanding the plain-text alternative even though we are perfectly capable of
  displaying the HTML, and it is almost always the preferred format, i.e., it
  almost always comes after the plain-text alternative.

  Speaking of which, you can't assume that each of the alternatives is just a
  basic text/[whatever]. There may be, for example, a text/plain followed by a
  multipart/related which contains text/html and associated embedded
  images. Yikes!

  You also can't assume that there will be just two parts. There can be an
  arbitrary number, and the ones we are capable of displaying and the ones we
  aren't could be interspersed in any order by the producer of the MIME.

  We can't just throw away the parts we're not displaying when we're processing
  the MIME for display. If we were to do that, then the MIME parts that
  remained wouldn't get numbered properly, and that would mean, for example,
  that deleting attachments wouldn't work in some messages. Indeed, that very
  problem is what prompted a rewrite of this file into its current
  architecture.

  ARCHITECTURE
  ------------

  Parts are read and queued until we know whether we're going to display
  them. If the first pending part is one we don't know how to display, then we
  can add it to the MIME structure immediately, with output_p disabled. If the
  first pending part is one we know how to display, then we can't add it to the
  in-memory MIME structure until either (a) we encounter a later, more
  preferred part we know how to display, or (b) we reach the end of the
  parts. A display-capable part of the queue may be followed by one or more
  display-incapable parts. We can't add them to the in-memory structure until
  we figure out what to do with the first, display-capable pending part,
  because otherwise the order and numbering will be wrong. All of the logic in
  this paragraph is implemented in the flush_children function.

  The display_cached_part function is what actually adds a MIME part to the
  in-memory MIME structure. There is one complication there which forces us to
  violate abstrations... Even if we set output_p on a child before adding it to
  the parent, the ParseBegin function resets it. The kluge I came up with to
  prevent that was to give the child a separate options object and set
  output_fn to nullptr in it, because that causes ParseBegin to set output_p to
  false. This seemed like the least onerous way to accomplish this, although I
  can't say it's a solution I'm particularly fond of.

  Another complication in display_cached_part is that if we were just a normal
  multipart type, we could rely on MimeMultipart_ParseLine to notify emitters
  about content types, character sets, part numbers, etc. as our new children
  get created. However, since we defer creation of some children, the
  notification doesn't happen there, so we have to handle it
  ourselves. Unfortunately, this requires a small abstraction violation in
  MimeMultipart_ParseLine -- we have to check there if the entity is
  multipart/alternative and if so not notify emitters there because
  MimeMultipartAlternative_create_child handles it.

  - Jonathan Kamens, 2010-07-23

  When the option prefer_plaintext is on, the last text/plain part
  should be preferred over any other part that can be displayed. But
  if no text/plain part is found, then the algorithm should go as
  normal and convert any html part found to text. To achieve this I
  found that the simplest way was to change the function display_part_p
  into returning priority as an integer instead of boolean can/can't
  display. Then I also changed the function flush_children so it selects
  the last part with the highest priority. (Priority 0 means it cannot
  be displayed and the part is never chosen.)

  - Terje Bråten, 2013-02-16
*/

#include "mimemalt.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "nsMimeTypes.h"
#include "nsMimeStringResources.h"
#include "nsIPrefBranch.h"
#include "mimemoz2.h" // for prefs
#include "modmimee.h" // for MimeConverterOutputCallback

namespace mozilla {
namespace mime {

MultipartAlternative::MultipartAlternative()
  : buffered_hdrs(nullptr)
  , part_buffers(nullptr)
  , pending_parts(0)
  , max_parts(0)
  , buffered_priority(Priority::Undisplayable)
{
}

void
MultipartAlternative::Cleanup()
{
  int32_t i;

  for (i = 0; i < this->pending_parts; i++) {
    MimeHeaders_free(this->buffered_hdrs[i]);
    MimePartBufferDestroy(this->part_buffers[i]);
  }
  PR_FREEIF(this->buffered_hdrs);
  PR_FREEIF(this->part_buffers);
  this->pending_parts = 0;
  this->max_parts = 0;
}

MultipartAlternative::~MultipartAlternative()
{
  Cleanup();
}


int
MultipartAlternative::FlushChildren(bool finished, MultipartAlternative::Priority next_priority)
{
  /*
    The cache should always have at the head the part with highest priority.

    Possible states:

    1. Cache contains nothing: do nothing.

    2. Finished, and the cache contains one displayable body followed
       by zero or more bodies with lower priority:

    3. Finished, and the cache contains one non-displayable body:
       create it with output off.

    4. Not finished, and the cache contains one displayable body
       followed by zero or more bodies with lower priority, and the new
       body we're about to create is higher or equal priority:
       create all cached bodies with output off.

    5. Not finished, and the cache contains one displayable body
       followed by zero or more bodies with lower priority, and the new
       body we're about to create has lower priority: do nothing.

    6. Not finished, and the cache contains one non-displayable body:
       create it with output off.
  */
  bool have_displayable, do_flush, do_display;

  /* Case 1 */
  if (! this->pending_parts)
    return 0;

  have_displayable = (this->buffered_priority > next_priority);

  if (finished && have_displayable) {
    /* Case 2 */
    do_flush = true;
    do_display = true;
  }
  else if (finished && ! have_displayable) {
    /* Case 3 */
    do_flush = true;
    do_display = false;
  }
  else if (! finished && have_displayable) {
    /* Case 5 */
    do_flush = false;
    do_display = false;
  }
  else if (! finished && ! have_displayable) {
    /* Case 4 */
    /* Case 6 */
    do_flush = true;
    do_display = false;
  }
  else {
    NS_ERROR("mimemalt.cpp: logic error in flush_children");
    return -1;
  }

  if (do_flush) {
    int32_t i;
    for (i = 0; i < this->pending_parts; i++) {
      DisplayCachedPart(this->buffered_hdrs[i], this->part_buffers[i], do_display && (i == 0));
      delete this->buffered_hdrs[i];
      MimePartBufferDestroy(this->part_buffers[i]);
    }
    this->pending_parts = 0;
  }
  return 0;
}

int
MultipartAlternative::ParseEOF(bool abort_p)
{
  int status = 0;

  if (this->closed_p) return 0;

  status = Super::ParseEOF(abort_p);
  if (status < 0) return status;


  status = FlushChildren(true, PRIORITY_UNDISPLAYABLE);
  if (status < 0)
    return status;

  Cleanup();

  return status;
}


int
MultipartAlternative::CreateChild()
{
  Priority priority = DisplayPart(this->hdrs);

  FlushChildren(false, priority);

  this->state = MimeMultipartPartFirstLine;
  int32_t i = this->pending_parts++;

  if (i==0) {
    this->buffered_priority = priority;
  }

  if (this->pending_parts > this->max_parts) {
    this->max_parts = this->pending_parts;
    MimeHeaders **newBuf = (MimeHeaders **)
      PR_REALLOC(malt->buffered_hdrs,
                 malt->max_parts * sizeof(*malt->buffered_hdrs));
    NS_ENSURE_TRUE(newBuf, MIME_OUT_OF_MEMORY);
    this->buffered_hdrs = newBuf;

    MimePartBufferData **newBuf2 = (MimePartBufferData **)
      PR_REALLOC(malt->part_buffers,
                 malt->max_parts * sizeof(*malt->part_buffers));
    NS_ENSURE_TRUE(newBuf2, MIME_OUT_OF_MEMORY);
    this->part_buffers = newBuf2;
  }

  this->buffered_hdrs[i] = new MimeHeaders(*this->hdrs);
  NS_ENSURE_TRUE(malt->buffered_hdrs[i], MIME_OUT_OF_MEMORY);

  this->part_buffers[i] = MimePartBufferCreate();
  NS_ENSURE_TRUE(this->part_buffers[i], MIME_OUT_OF_MEMORY);

  return 0;
}


int
MultipartAlternative::ParseChildLine(const char *line, int32_t length, bool first_line_p)
{
  NS_ASSERTION(this->pending_parts, "should be pending parts, but there aren't");
  if (!this->pending_parts)
    return -1;
  int32_t i = this->pending_parts - 1;

  /* Push this line into the buffer for later retrieval. */
  return MimePartBufferWrite(this->part_buffers[i], line, length);
}


int
MultipartAlternative::CloseChild()
{
  /* PR_ASSERT(this->part_buffer);      Some Mac brokenness trips this->..
  if (!this->part_buffer) return -1; */

  if (this->pending_parts)
    MimePartBufferClose(this->part_buffers[this->pending_parts-1]);

  /* PR_ASSERT(this->hdrs);         I expect the Mac trips this too */

  if (this->hdrs) {
    delete this->hdrs;
    this->hdrs = nullptr;
  }

  return 0;
}


MultipartAlternative::Priority
MultipartAlternative::DisplayPart(Headers* sub_hdrs)
{
  Priority priority = PRIORITY_UNDISPLAYABLE;
  char *ct = sub_hdrs->Get(HEADER_CONTENT_TYPE, true, false);
  if (!ct)
    return priority;

  /* RFC 1521 says:
     Receiving user agents should pick and display the last format
     they are capable of displaying.  In the case where one of the
     alternatives is itself of type "multipart" and contains unrecognized
     sub-parts, the user agent may choose either to show that alternative,
     an earlier alternative, or both.
   */

  // We must pass 'true' as last parameter so that text/calendar is
  // only displayable when Lightning is installed.
  PartClass *clazz = mime_find_class(ct, sub_hdrs, this->options, true);
  if (clazz && clazz->displayable_inline_p(clazz, sub_hdrs)) {
    // prefer_plaintext pref
    bool prefer_plaintext = false;
    nsIPrefBranch *prefBranch = GetPrefBranch(this->options);
    if (prefBranch) {
      prefBranch->GetBoolPref("mailnews.display.prefer_plaintext",
                              &prefer_plaintext);
    }
    prefer_plaintext = prefer_plaintext &&
           (this->options->format_out != nsMimeOutput::nsMimeMessageSaveAs) &&
           (this->options->format_out != nsMimeOutput::nsMimeMessageRaw);

    priority = PrioritizePart(ct, prefer_plaintext);
  }

  PR_FREEIF(ct);
  return priority;
}

/**
* RFC 1521 says we should display the last format we are capable of displaying.
* But for various reasons (mainly to improve the user experience) we choose
* to ignore that in some cases, and rather pick one that we prioritize.
*/
MultipartAlternative::Priority
MultipartAlternative::PrioritizePart(char* content_type, bool prefer_plaintext)
{
  /*
   * PRIORITY_NORMAL is the priority of text/html, multipart/..., etc. that
   * we normally display. We should try to have as few exceptions from
   * PRIORITY_NORMAL as possible
   */

  /* (with no / in the type) */
  if (!PL_strcasecmp(content_type, "text")) {
    if (prefer_plaintext) {
       /* When in plain text view, a plain text part is what we want. */
       return PRIORITY_HIGH;
    }
    /* We normally prefer other parts over the unspecified text type. */
    return  PRIORITY_TEXT_UNKNOWN;
  }

  if (!PL_strncasecmp(content_type, "text/", 5)) {
    char *text_type = content_type + 5;

    if (!PL_strncasecmp(text_type, "plain", 5)) {
      if (prefer_plaintext) {
        /* When in plain text view,
           the text/plain part is exactly what we want */
        return PRIORITY_HIGHEST;
      }
      /*
       * Because the html and the text part may be switched,
       * or we have an extra text/plain added by f.ex. a buggy virus checker,
       * we prioritize text/plain lower than normal.
       */
      return PRIORITY_TEXT_PLAIN;
    }

    if (!PL_strncasecmp(text_type, "calendar", 8) && prefer_plaintext) {
      /*
       * text/calendar receives an equally high priority so an invitation
       * shows even in plaintext mode.
       */
      return PRIORITY_HIGHEST;
    }

    /* Need to white-list all text/... types that are or could be implemented. */
    if (!PL_strncasecmp(text_type, "html", 4) ||
        !PL_strncasecmp(text_type, "enriched", 8) ||
        !PL_strncasecmp(text_type, "richtext", 8) ||
        !PL_strncasecmp(text_type, "calendar", 8) ||
        !PL_strncasecmp(text_type, "rtf", 3)) {
      return PRIORITY_NORMAL;
    }

    /* We prefer other parts over unknown text types. */
    return PRIORITY_TEXT_UNKNOWN;
  }

  // Guard against rogue messages with incorrect MIME structure and
  // don't show images when plain text is requested.
  if (!PL_strncasecmp(content_type, "image", 5)) {
    if (prefer_plaintext)
      return PRIORITY_UNDISPLAYABLE;
    else
      return PRIORITY_LOW;
  }

  return PRIORITY_NORMAL;
}

int
MultipartAlternative::DisplayCachedPart(Headers* hdrs, MimePartBufferData *buffer, bool do_display)
{
  int status;
  bool old_options_no_output_p;

  char *ct = (hdrs
        ? hdrs->Get(HEADER_CONTENT_TYPE, true, false)
        : 0);
  const char *dct = this->default_part_type;
  Part *body;
  /** Don't pass in NULL as the content-type (this means that the
   * auto-uudecode-hack won't ever be done for subparts of a
   * multipart, but only for untyped children of message/rfc822.
   */
  const char *uct = (ct && *ct) ? ct : (dct ? dct: TEXT_PLAIN);

  // We always want to display the cached part inline.
  body = mime_create(uct, hdrs, this->options, true);
  PR_FREEIF(ct);
  if (!body) return MIME_OUT_OF_MEMORY;
  body->output_p = do_display;

  status = AddChild(body);
  if (status < 0)
  {
    delete body;
    return status;
  }
  /* add_child assigns body->options from obj->options, but that's
     just a pointer so if we muck with it in the child it'll modify
     the parent as well, which we definitely don't want. Therefore we
     need to make a copy of the old value and restore it later. */
  old_options_no_output_p = this->options->no_output_p;
  if (! do_display)
    body->options->no_output_p = true;

#ifdef MIME_DRAFTS
  /* if this object is a child of a multipart/related object, the parent is
     taking care of decomposing the whole part, don't need to do it at this level.
     However, we still have to call decompose_file_init_fn and decompose_file_close_fn
     in order to set the correct content-type. But don't call MimePartBufferRead
  */
  bool multipartRelatedChild = mime_typep(obj->parent,(PartClass*)&mimeMultipartRelatedClass);
  bool decomposeFile = do_display && this->options &&
                  this->options->decompose_file_p &&
                  this->options->decompose_file_init_fn &&
                  !mime_typep(body, (PartClass *) &mimeMultipartClass);

  if (decomposeFile)
  {
    status = this->options->decompose_file_init_fn(
                        this->options->stream_closure, hdrs);
    if (status < 0) return status;
  }
#endif /* MIME_DRAFTS */

  /* Now that we've added this new object to our list of children,
   notify emitters and start its parser going. */
  MimeMultipart_notify_emitter(body);

  status = body->ParseBegin(body);
  if (status < 0) return status;

#ifdef MIME_DRAFTS
  if (decomposeFile && !multipartRelatedChild)
    status = MimePartBufferRead (buffer,
                  this->options->decompose_file_output_fn,
                  this->options->stream_closure);
  else
#endif /* MIME_DRAFTS */

  status = MimePartBufferRead (buffer,
                  /* The MimeConverterOutputCallback cast is to turn the
                   `void' argument into `Part'. */
                  ((MimeConverterOutputCallback) body->ParseBuffer),
                  body);

  if (status < 0) return status;

  /* Done parsing. */
  status = body->ParseEOF(body, false);
  if (status < 0) return status;
  status = body->ParseEnd(body, false);
  if (status < 0) return status;

#ifdef MIME_DRAFTS
  if (decomposeFile)
  {
    status = this->options->decompose_file_close_fn(this->options->stream_closure);
    if (status < 0) return status;
  }
#endif /* MIME_DRAFTS */

  /* Restore options to what parent classes expects. */
  obj->options->no_output_p = old_options_no_output_p;

  return 0;
}

} // namespace mime
} // namespace mozilla
