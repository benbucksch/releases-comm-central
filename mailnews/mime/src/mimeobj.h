/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEOBJ_H_
#define _MIMEOBJ_H_

#include "mimei.h"
#include "prio.h"

namespace mozilla::mime {

/**
 * This is the base-class for the objects representing all other MIME types.
 * was: MimeObject
 */
abstract class Part {
public:
  Part(Headers* hdrs, const char* contentTypeOverride);

  /**
   * Given a content-type string, creates and returns an appropriate subclass
   * of MimeObject.  The headers (from which the content-type was presumably
   * extracted) are copied. forceInline is set to true when the caller wants
   * the function to ignore opts->show_attachment_inline_p and force inline
   * display, e.g., mimemalt wants the body part to be shown inline.
   */
  Part(const char* contentType, Headers* hdrs,
       DisplayOptions* opts, bool forceInline = false);

  virtual ~Part();

  ///////////////////////////////
  // The methods shared by all MIME objects.

  /**
   * Called after constructor but before ParseLine() or ParseBuffer().
   * Can be used to initialize various parsing machinery.
   */
  virtual int ParseBegin();

  /**
   * This is the method by which you feed arbitrary data into the parser
   * for this object.  Most subclasses will probably inherit this method
   * from the MimeObject base-class, which line-buffers the data and then
   * hands it off to the ParseLine() method.
  * If this object uses a Content-Transfer-Encoding (base64, qp, uue)
  * then the data may be decoded by ParseBuffer() before ParseLine()
  * is called.  (The MimeLeaf class provides this functionality.)
  */
  virtual int ParseBuffer(const char *buf, int32_t size);

  /**
   * This method is called (by ParseBuffer()) for each complete line of
   * data handed to the parser, and is the method which most subclasses
   * will override to implement their parsers.
   *
   * When handing data off to a MIME object for parsing, one should always
   * call the ParseBuffer() method, and not call the ParseLine() method
   * directly, since the ParseBuffer() method may do other transformations
   * on the data (like base64 decoding.)
   *
   * You should generally not call ParseLine() directly, since that could
   * bypass decoding. You should call ParseBuffer() instead.
   */
  virtual int ParseLine(const char *line, int32_t length);

  /**
   * This is called when there is no more data to be handed to the object:
   * when the parent object is done feeding data to an object being parsed.
   * Implementors of this method should be sure to also call the ParseEOF()
   * methods of any sub-objects to which they have pointers.
   *
   * This is also called by the destructor, if it has not already been called.
   *
   * The closed_p instance variable is used to prevent multiple calls to ParseEOF().
   */
  virtual int ParseEOF(bool abort_p);

  /**
   * Called after ParseEOF() but before the destructor.
   * This can be used to free up any memory no longer needed now that parsing
   * is done (to avoid surprises due to unexpected method combination, it's
   * best to free things in this method in preference to ParseEOF().)
   * Implementors of this method should be sure to also call the ParseEnd()
   * methods of any sub-objects to which they have pointers.
   *
   * This is also called by the destructior, if it has not already been called.
   *
   * The parsed_p instance variable is used to prevent multiple calls to
   * ParseEnd().
   */
  virtual int ParseEnd(bool abort_p);

  /**
   * This method should return true if this class of object will be displayed
   * directly, as opposed to being displayed as a link.  This information is
   * used by the "multipart/alternative" parser to decide which of its children
   * is the ``best'' one to display.   Note that this is a class method, not
   * an object method -- there is not yet an instance of this class at the time
   * that it is called.  The `hdrs' provided are the headers of the object that
   * might be instantiated -- from this, the method may extract additional
   * information that it might need to make its decision.
   */
  virtual bool DisplayableInline(Headers *hdrs);


  //////////////////////////////
  // Properties

  bool IsType(PartClass* clazz);

  /**
   * Returns a string describing the location of the part, like "2.5.3".
   * @returns part number of the form "1.3.5". Not a URL.
   */
  char* PartAddress();

  /**
   * Returns a string describing the location of the IMAP part, like "2.5.3".
   * This part is explicitly passed in the X-Mozilla-IMAP-Part header.
   * @returns part number of the form "1.3.5". Not a URL.
   * Return value must be freed by the caller.
   */
  char* IMAPPartAddress();

  char* ExternalAttachmentURL();

  /**
   * Given a part ID, looks through the MimeObject tree for a sub-part whose ID
   * number matches, and returns the MimeObject (else NULL.)
   * @param part is of the form "1.3.5". Not a URL.
   */
  Part* GetObjectForPartAddress(const char* part);

  /**
   * Given a part ID, looks through the MimeObject tree for a sub-part whose ID
   * number matches; if one is found, returns the Content-Name of that part.
   * Else returns NULL.
   * @param part is of the form "1.3.5". Not a URL.
   */
  char* GetSuggestedNameOfPart(const char* part);

  /**
   * Given a part ID, looks through the MimeObject tree for a sub-part whose ID
    * number matches; if one is found, returns the Content-Name of that part.
    * Else returns NULL.
   * @param part is of the form "1.3.5". Not a URL.
    */
  char* GetContentTypeOfPart(const char* part);

  bool IsMessageBody();


  ////////////////////////////////////
  // Public member variables
public:

  PartClass* clazz;

  /**
   * The header data associated with this object;
   * this is where the content-type, disposition, description,
   * and other meta-data live.
  *
  * For example, the outermost message/rfc822 object
  * would have NULL here (since it has no parent,
  * thus no headers to describe it.)  However, a multipart/mixed object,
  * which was the sole child of that message/rfc822 object, would have
  * here a copy of the headers which began the parent object
  * (the headers which describe the child.)
  */
  Headers* headers;

  /**
   * The MIME content-type and encoding.
   */
  char* content_type;

  /**
   * In most cases, these will be the same as the values to be found
   * in the `headers' object, but in some cases, the values in these slots
   * will be more correct than the headers.
   */
  char* encoding;

  /**
    * Backpointer to a MimeContainer object.
    */
  Part* parent;

  /**
   * Display preferences set by caller.
   */
  DisplayOptions* options;

  /**
   * Whether it's done being written to.
   */
  bool closed_p;

  /**
   * Whether the parser has been shut down.
   */
  bool parsed_p;

  /**
   * Whether it should be written.
   */
  bool output_p;

  /**
   * Force an object to not be shown as attachment,
   * but when is false, it doesn't mean it will be shown as attachment;
   * specifically, body parts are never shown as attachments.
   */
  bool dontShowAsAttachment = false;

protected:
  /**
   * Read-buffer and write-buffer (on input, ParseBuffer() uses ibuffer to
   * compose calls to ParseLine(); on output, `obuffer' is used in various
   * ways by various routines.)  These buffers are created and grow as needed.
   * `ibuffer' should be generally be considered hands-off, and `obuffer'
   * should generally be considered fair game.
   */
  char* ibuffer;
  char* obuffer;
  int32_t ibuffer_size;
  int32_t obuffer_size;
  int32_t ibuffer_fp
  int32_t obuffer_fp;


  //////////////////////////////
  // Some output-generation utility functions
public:

  int OutputInit(const char* content_type);

  /**
   * Write content to the output stream.
   *
   *@param userVisible whether the output that has just been
   * written will cause characters or images to show up on the screen, that
   * is, it should be false if the stuff being written is merely structural
   * HTML or whitespace ("<p>", "</table>", etc.)  This information is used
   * when making the decision of whether a separating <hr> is needed.
   */
  int Write(const char* data, int32_t length, bool userVisible);

  /**
   * Writes out the right kind of <hr> (or rather, queues it for writing).
   */
  int WriteSeparator();

#if defined(DEBUG) && defined(XP_UNIX)
  int DebugPrint(PRFileDesc *stream, int32_t depth);
#endif

#ifdef ENABLE_SMIME

  /**
   * Asks whether the given object is one of the cryptographically signed
   * or encrypted objects that we know about.
   * |Message| uses this to decide if the headers need to be presented differently.
   */
  bool IsCryptoObject(Headers&, bool clearsignedCounts);

  /**
   * Tells whether this |Part| is a message which has been encrypted
   * or signed.
   * Helper for GetMessageCryptoState().
   */
  void GetCryptoState(bool* signed, bool* encrypted,
                   bool* signedOK, bool* encryptedOK);

  /**
   * Whether this |Part| has written out the HTML version of its headers
   * in such a way that it will have a "crypto stamp" next to the headers.  If
   * this is true, then the child must write out its HTML slightly differently
   * to take this into account...
   */
  bool IsCryptoStamped();

  /**
   * How the crypto code tells the MimeMessage object what the crypto stamp
   * on it says.
   */
  void SetCryptoStamp(bool signed, bool encrypted);

#endif // ENABLE_SMIME
};


/**
 * RTTI forbidden
 * <https://developer.mozilla.org/en-US/docs/Mozilla/Using_CXX_in_Mozilla_code>
 *
 * This allows a dynamic class system and inspection, that is,
 * 1. to query your own subtype in runtime, and
 * 2. to allow instantiation of a passed-in class in mimei.cpp.
 *
 * For this to work, each PartClass needs to be a singleton
 * for its respective type.
 */
class PartClass {
  bool IsSubclassOf(PartClass* parent);
  PartClass* superclass = nullptr:
};


} // namespace
#endif // _MIMEOBJ_H_
