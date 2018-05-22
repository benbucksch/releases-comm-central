/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MIMEI_H_
#define _MIMEI_H_

/*
  This module, libmime, implements a general-purpose MIME parser.
  One of the methods provided by this parser is the ability to emit
  an HTML representation of it.

  All Mozilla-specific code is (and should remain) isolated in the
  file mimemoz.c.  Generally, if the code involves images, netlib
  streams it should be in mimemoz.c instead of in the main body of
  the MIME parser.

  The parser is object-oriented and fully buzzword-compliant.
  There is a class for each MIME type, and each class is responsible
  for parsing itself, and/or handing the input data off to one of its
  child objects.

  The class hierarchy is:

     MimeObject (abstract)
      |
      +--- MimeContainer (abstract)
      |     |
      |     +--- MimeMultipart (abstract)
      |     |     |
      |     |     +--- MimeMultipartMixed
      |     |     |
      |     |     +--- MimeMultipartDigest
      |     |     |
      |     |     +--- MimeMultipartParallel
      |     |     |
      |     |     +--- MimeMultipartAlternative
      |     |     |
      |     |     +--- MimeMultipartRelated
      |     |     |
      |     |     +--- MimeMultipartAppleDouble
      |     |     |
      |     |     +--- MimeSunAttachment
      |     |     |
      |     |     \--- MimeMultipartSigned (abstract)
      |     |          |
      |     |          \--- MimeMultipartSignedCMS
      |     |
      |     +--- MimeEncrypted (abstract)
      |     |     |
      |     |     \--- MimeEncryptedPKCS7
      |     |
      |     +--- MimeXlateed (abstract)
      |     |     |
      |     |     \--- MimeXlateed
      |     |
      |     +--- MimeMessage
      |     |
      |     \--- MimeUntypedText
      |
      +--- MimeLeaf (abstract)
      |     |
      |     +--- MimeInlineText (abstract)
      |     |     |
      |     |     +--- MimeInlineTextPlain
      |     |     |     |
      |     |     |     \--- MimeInlineTextHTMLAsPlaintext
      |     |     |
      |     |     +--- MimeInlineTextPlainFlowed
      |     |     |
      |     |     +--- MimeInlineTextHTML
      |     |     |     |
      |     |     |     +--- MimeInlineTextHTMLParsed
      |     |     |     |
      |     |     |     \--- MimeInlineTextHTMLSanitized
      |     |     |
      |     |     +--- MimeInlineTextRichtext
      |     |     |     |
      |     |     |     \--- MimeInlineTextEnriched
      |     |    |
      |     |    +--- MimeInlineTextVCard
      |     |
      |     +--- MimeInlineImage
      |     |
      |     \--- MimeExternalObject
      |
      \--- MimeExternalBody


  =========================================================================
  There is one header file and one source file for each class (for example,
  the InlineText class is defined in "text.h" and "text.c".)

  Code style follows the Mozilla Style Guide
  <https://developer.mozilla.org/en-US/docs/Mozilla/Developer_guide/Coding_Style>
  Blocks will be indented 2 space.
  Adhere to about 80-100 lines per line. Longer lines will be wrapped,
  and indented at least 4 spaces.


  Each header file follows the following boiler-plate form:

  TYPEDEFS: these come first to avoid circular dependencies.

      typedef struct FoobarClass FoobarClass;
      typedef struct Foobar      Foobar;

  CLASS DECLARATION:
  Theis structure defines the callback routines and other per-class data
  of the class defined in this module.

      struct FoobarClass {
        ParentClass superclass;
        ...any callbacks or class-variables...
      };

  CLASS DEFINITION:
  This variable holds an instance of the one-and-only class record; the
  various instances of this class point to this object.  (One interrogates
  the type of an instance by comparing the value of its class pointer with
  the address of this variable.)

      extern FoobarClass foobarClass;

  INSTANCE DECLARATION:
  Theis structure defines the per-instance data of an object, and a pointer
  to the corresponding class record.

      struct Foobar {
        Parent parent;
        ...any instance variables...
      };

  Then, in the corresponding .c file, the following structure is used:

  CLASS DEFINITION:
  First we pull in the appropriate include file (which includes all necessary
  include files for the parent classes) and then we define the class object
  using the MimeDefClass macro:

      #include "foobar.h"
      #define MIME_SUPERCLASS parentlClass
      MimeDefClass(Foobar, FoobarClass, foobarClass, &MIME_SUPERCLASS);

  The definition of MIME_SUPERCLASS is just to move most of the knowledge of the
  exact class hierarchy up to the file's header, instead of it being scattered
  through the various methods; see below.

  METHOD DECLARATIONS:
  We will be putting function pointers into the class object, so we declare
  them here.  They can generally all be static, since nobody outside of this
  file needs to reference them by name; all references to these routines should
  be through the class object.

      extern int FoobarMethod(Foobar *);
      ...etc...

  CLASS INITIALIZATION FUNCTION:
  The MimeDefClass macro expects us to define a function which will finish up
  any initialization of the class object that needs to happen before the first
  time it is instantiated.  Its name must be of the form "<class>Initialize",
  and it should initialize the various method slots in the class as
  appropriate.  Any methods or class variables which this class does not wish
  to override will be automatically inherited from the parent class (by virtue
  of its class-initialization function having been run first.)  Each class
  object will only be initialized once.

      static int
      FoobarClassInitialize(FoobarClass *class)
      {
        clazz->method = FoobarMethod.
        ...etc...
      }

  METHOD DEFINITIONS:
  Next come the definitions of the methods we referred to in the class-init
  function.  The way to access earlier methods (methods defined on the
  superclass) is to simply extract them from the superclass's object.
  But note that you CANNOT get at methods by indirecting through
  object->clazz->superclass: that will only work to one level, and will
  go into a loop if some subclass tries to continue on this method.

  The easiest way to do this is to make use of the MIME_SUPERCLASS macro that
  was defined at the top of the file, as shown below.  The alternative to that
  involves typing the literal name of the direct superclass of the class
  defined in this file, which will be a maintenance headache if the class
  hierarchy changes.  If you use the MIME_SUPERCLASS idiom, then a textual
  change is required in only one place if this class's superclass changes.

      static void
      Foobar_finalize (MimeObject *object)
      {
        ((MimeObjectClass*)&MIME_SUPERCLASS)->finalize(object);  //  RIGHT
        parentClass.whatnot.object.finalize(object);             //  (works...)
        object->clazz->superclass->finalize(object);             //  WRONG!!
      }

  If you write a libmime content type handler, libmime might create several
  instances of your class at once and call e.g. the same finalize code for
  3 different objects in a row.
 */

#include "mimehdrs.h"
#include "nsTArray.h"
#include "mimeobj.h"

namespace MIME {

#ifdef ENABLE_SMIME
class nsICMSMessage;
#endif // ENABLE_SMIME

#define cpp_stringify_noop_helper(x)#x
#define cpp_stringify(x) cpp_stringify_noop_helper(x)

/**
 * Given a MIME content-type string, finds and returns
 * an appropriate subclass of |Part|.
 *
 * This is the main switchboard of libmime.
 * It decides which implementation class responds to which
 * MIME content type.
 *
 * @param exactMatch If |exactMatch| is true, then
 * only fully-known types will be returned.
 * For example:
 * - if true, "text/x-unknown" will return MimeInlineTextPlainType
 * - if false, "text/x-unknown" will return NULL
 * @returns  A class object
 */
extern PartClass* findClass(
    const char* content_type,
    Headers* hdrs,
    DisplayOptions* opts,
    bool exactMatch);

/**
 * Puts a part-number into a URL.  If append_p is true, then the part number
 * is appended to any existing part-number already in that URL; otherwise,
 *  it replaces it.
 */
extern char* SetURLPart(const char* url, const char* part, bool append);

/**
 * cut the part of url for display a attachment as a email.
 */
extern char* GetBaseURL(const char* url);

/**
 * Puts an *IMAP* part-number into a URL.
 */
extern char* SetURLIMAPPart(const char* url, const char* part, const char* libmimepart);


/**
 * Parse the various "?" options off the URL and into the options struct.
 * @param DisplayOptions {out}
 */
extern int ParseURLOptions(const char* url, DisplayOptions*);

class ParseStateObject {
public:

  ParseStateObject()
  {
    root = 0;
    separatorQueued = false;
    separatorSuppressed = false;
    firstPartWritten = false;
    postHeaderHTMLRun = false;
    firstDataWritten = false;
    decrypted = false;
    strippingPart = false;
  }

  /**
   * The outermost parser object
   */
  Part* root;

 /**
  * Whether a separator should be written out
  * before the next text is written.
  * This lets us write separators lazily, so that one doesn't appear
  * at the end, and so that more than one don't appear in a row.
  */
  bool separatorQueued;

  /**
   * Whether the currently-queued separator should not be printed.
   * This is a kludge to prevent separators from being printed just after
   * a header block.
   */
  bool separatorSuppressed;

  /**
   * State used for the "Show Attachments As Links" kludge.
   */
  bool firstPartWritten;

  /**
   * Whether we've run options->generatePostHeaderHTML().
   */
  bool postHeaderHTMLRun;

  /**
   * State used for Mozilla lazy-stream-creation evilness.
   */
  bool firstDataWritten;

  /**
   * If options->dexlate is true, then this will be set to indicate whether any
   * dexlateion did in fact occur.
   */
  bool decrypted;

  /**
   * if we're stripping parts, what parts to strip
   */
  nsTArray<nsCString> partsToStrip;

  /**
   * if we're detaching parts, where each part was detached to
   */
  nsTArray<nsCString> detachToFiles;

  bool strippingPart;

  /**
   * if we've detached this part, filepath of detached par
   */
  nsCString detachedFilePath;
};

extern int Options::Write(Headers*, DisplayOptions*,
                         const char* data, int32_t length,
                         bool userVisible);

/**
 * This struct is what we hang off of (context)->mime_data,
 * to remember info about the last MIME object we've parsed and displayed.
 * @see GuessURLContentName(). */
class DisplayData {
  Part* lastParsedPbject;
  char* lastParsedURL;
};


} // namespace
#endif // _MIMEI_H_
