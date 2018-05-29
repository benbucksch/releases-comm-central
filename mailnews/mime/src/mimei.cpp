/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date         Modified by      Description of modification
 * 2000-04-20   IBM Corp.        OS/2 VisualAge build
 * 2018-05-22   Ben Bucksch      Convert to C++ classes
 */

#include "nsCOMPtr.h"
                       // If you add classes here, also add them to mimei.h
#include "mimeobj.h"   //  Part (abstract)
#include "mimecont.h"  //   |--- Container (abstract)
#include "mimemult.h"  //   |     |--- Multipart (abstract)
#include "mimemmix.h"  //   |     |     |--- MultipartMixed
#include "mimemdig.h"  //   |     |     |--- MultipartDigest
#include "mimempar.h"  //   |     |     |--- MultipartParallel
#include "mimemalt.h"  //   |     |     |--- MultipartAlternative
#include "mimemrel.h"  //   |     |     |--- MultipartRelated
#include "mimemapl.h"  //   |     |     |--- MultipartAppleDouble
#include "mimesun.h"   //   |     |     |--- SunAttachment
#include "mimemsig.h"  //   |     |     |--- MultipartSigned (abstract)
#ifdef ENABLE_SMIME    //   |     |           |
#include "mimemcms.h"  //   |     |           |---MultipartSignedCMS
#endif                 //   |     |
#include "mimecryp.h"  //   |     |--- Encrypted (abstract)
#ifdef ENABLE_SMIME    //   |     |     |
#include "mimecms.h"   //   |     |     |--- EncryptedPKCS7
#endif                 //   |     |
#include "mimemsg.h"   //   |     |--- Message
#include "mimeunty.h"  //   |     |--- UntypedText
#include "mimeleaf.h"  //   |--- Leaf (abstract)
#include "mimetext.h"  //   |     |--- Text (abstract)
#include "mimetpla.h"  //   |     |     |--- TextPlain
#include "mimethpl.h"  //   |     |     |     |--- TextHTMLAsPlaintext
#include "mimetpfl.h"  //   |     |     |--- TextPlainFlowed
#include "mimethtm.h"  //   |     |     |--- TextHTML
#include "mimethsa.h"  //   |     |     |     |--- TextHTMLSanitized
#include "mimeTextHTMLParsed.h" //|     |     |--- TextHTMLParsed
#include "mimetric.h"  //   |     |     |--- TextRichtext
#include "mimetenr.h"  //   |     |     |     |--- TextEnriched
// SUPPORTED VIA PLUGIN     |     |     |--- TextVCard
#include "mimeiimg.h"  //   |     |--- InlineImage
#include "mimeeobj.h"  //   |     |--- ExternalObject
#include "mimeebod.h"  //   |--- ExternalBody
#include "prlog.h"
#include "prmem.h"
#include "prenv.h"
#include "plstr.h"
#include "prlink.h"
#include "prprf.h"
#include "mimecth.h"
#include "mimebuf.h"
#include "mimemoz2.h"
#include "nsIMimeContentTypeHandler.h"
#include "nsCategoryManagerUtils.h"
#include "nsXPCOMCID.h"
#include "nsISimpleMimeConverter.h"
#include "nsSimpleMimeConverterStub.h"
#include "nsTArray.h"
#include "nsMimeStringResources.h"
#include "nsMimeTypes.h"
#include "nsMsgUtils.h"
#include "nsIPrefBranch.h"
#include "imgLoader.h"

#include "nsIMsgMailNewsUrl.h"
#include "nsIMsgHdr.h"

namespace mozilla {
namespace mime {

// forward declaration
void getMsgHdrForCurrentURL(DisplayOptions* opts, nsIMsgDBHdr** aMsgHdr);

#define  IMAP_EXTERNAL_CONTENT_HEADER "X-Mozilla-IMAP-Part"
#define  EXTERNAL_ATTACHMENT_URL_HEADER "X-Mozilla-External-Attachment-URL"

/*
 * These are the necessary defines/variables for doing
 * content type handlers in external plugins.
 */
class ContentTypeHandler {
  char        contentType[128];
  bool        forceInlineDisplay;
};

nsTArray<ContentTypeHandler*>* gContentTypeHandlerList = NULL;

/*
 * This will return true, if the contentType is found in the
 * list. returns false, if not found.
 *
 * @param forceInlineDisplay {out} Of that content type should be
 * forced to display inline
 */
bool GetContentTypeAttribs(const char* contentType,
                            bool* forceInlineDisplay)
{
  *forceInlineDisplay = false;
  if (!gContentTypeHandlerList)
    return false;

  for (size_t i = 0; i < gContentTypeHandlerList->Length(); i++)
  {
    ContentTypeHandler* ptr = gContentTypeHandlerList->ElementAt(i);
    if (PL_strcasecmp(contentType, ptr->contentType) == 0)
    {
      *forceInlineDisplay = ptr->forceInlineDisplay;
      return true;
    }
  }

  return false;
}

void
AddContentTypeAttribs(const char* contentType,
                         contentTypeHandlerInitStruct* ctHandlerInfo)
{
  ContentTypeHandler* ptr = NULL;
  bool forceInlineDisplay;

  if (GetContentTypeAttribs(contentType, &forceInlineDisplay))
    return;

  if ( (!contentType) || (!ctHandlerInfo) )
    return;

  if (!gContentTypeHandlerList)
    gContentTypeHandlerList = new nsTArray<ContentTypeHandler*>();

  if (!gContentTypeHandlerList)
    return;

  ptr = new ContentTypeHandler();
  if (!ptr)
    return;

  PL_strncpy(ptr->contentType, contentType, sizeof(ptr->contentType));
  ptr->forceInlineDisplay = ctHandlerInfo->forceInlineDisplay;
  gContentTypeHandlerList->AppendElement(ptr);
}

bool IsForceInlineDisplay(const char* contentType)
{
  bool forceInlineDisp;
  GetContentTypeAttribs(contentType, &forceInlineDisp);
  return forceInlineDisp;
}

/*
 * This will find all content type handler for a specific content
 * type (if it exists) and is defined to the nsRegistry.
 */
PartClass* locateExternalContentHandler(const char* contentType,
                                               contentTypeHandlerInitStruct* ctHandlerInfo)
{
  if (!contentType || !*(contentType)) // null or empty content type
    return nullptr;

  PartClass* newObj = NULL;
  nsresult rv;

  nsAutoCString lookupID("@mozilla.org/mimecth;1?type=");
  nsAutoCString contentType;
  ToLowerCase(nsDependentCString(contentType), contentType);
  lookupID += contentType;

  nsCOMPtr<nsIMimeContentTypeHandler> ctHandler = do_CreateInstance(lookupID.get(), &rv);
  if (NS_FAILED(rv) || !ctHandler) {
    nsCOMPtr<nsICategoryManager> catman =
      do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
    if (NS_FAILED(rv))
      return nullptr;

    nsCString value;
    rv = catman->GetCategoryEntry(NS_SIMPLEMIMECONVERTERS_CATEGORY,
                                  contentType.get(), getter_Copies(value));
    if (NS_FAILED(rv) || value.IsEmpty())
      return nullptr;
    rv = MIME_NewSimpleMimeConverterStub(contentType.get(),
                                         getter_AddRefs(ctHandler));
    if (NS_FAILED(rv) || !ctHandler)
      return nullptr;
  }

  rv = ctHandler->CreateContentTypeHandlerClass(contentType.get(), ctHandlerInfo, &newObj);
  if (NS_FAILED(rv))
    return nullptr;

  AddContentTypeAttribs(contentType.get(), ctHandlerInfo);
  return newObj;
}

/* This is necessary to expose the |Part.Write()| method outside of this DLL */
int
MIME_Part::Write(Part* part, const char* output, int32_t length, bool userVisible)
{
  part->write(output, length, userVisible);
}

bool IsAllowedClass(const PartClass* clazz,
                             int32_t typesOfClassesToDisallow)
{
  if (typesOfClassesToDisallow == 0)
    return true;
  bool avoid_html = (typesOfClassesToDisallow >= 1);
  bool avoid_images = (typesOfClassesToDisallow >= 2);
  bool avoid_strange_content = (typesOfClassesToDisallow >= 3);
  bool allow_only_vanilla_classes = (typesOfClassesToDisallow == 100);

  if (allow_only_vanilla_classes)
    /* A "safe" class is one that is unlikely to have security bugs or to
       allow security exploits or one that is essential for the usefulness
       of the application, even for paranoid users.
       What's included here is more personal judgement than following
       strict rules, though, unfortunately.
       The function returns true only for known good classes, i.e. is a
       "whitelist" in this case.
       This idea comes from Georgi Guninski.
    */
    return
      (
        clazz == TextPlainClass ||
        clazz == TextPlainFlowedClass ||
        clazz == TextHTMLSanitizedClass ||
        clazz == TextHTMLAsPlaintextClass ||
           /* The latter 2 classes bear some risk, because they use the Gecko
              HTML parser, but the user has the option to make an explicit
              choice in this case, via html_as. */
        clazz == MultipartMixedClass ||
        clazz == MultipartAlternativeClass ||
        clazz == MultipartDigestClass ||
        clazz == MultipartAppleDoubleClass ||
        clazz == MessageClass ||
        clazz == ExternalObjectClass ||
                               /*    UntypedTextClass? -- does uuencode */
#ifdef ENABLE_SMIME
        clazz == MultipartSignedCMSClass ||
        clazz == EncryptedCMSClass ||
#endif
        clazz == 0
      );

  /* Contrairy to above, the below code is a "blacklist", i.e. it
     *excludes* some "bad" classes. */
  return
     !(
        (avoid_html
         && (
              clazz == TextHTMLParsedClass
                         /* Should not happen - we protect against that in
                            findClass(). Still for safety... */
            )) ||
        (avoid_images
         && (
              clazz == InlineImageClass
            )) ||
        (avoid_strange_content
         && (
              clazz == TextEnrichedClass ||
              clazz == TextRichtextClass ||
              clazz == SunAttachmentClass ||
              clazz == ExternalBodyClass
            ))
      );
}

void getMsgHdrForCurrentURL(DisplayOptions* opts, nsIMsgDBHdr** aMsgHdr)
{
  *aMsgHdr = nullptr;

  if (!opts)
    return;

  mime_stream_data *msd = (mime_stream_data *) (opts->stream_closure);
  if (!msd)
    return;

  nsCOMPtr<nsIChannel> channel = msd->channel;  // note the lack of ref counting...
  if (channel)
  {
    nsCOMPtr<nsIURI> uri;
    nsCOMPtr<nsIMsgMessageUrl> msgURI;
    channel->GetURI(getter_AddRefs(uri));
    if (uri)
    {
      msgURI = do_QueryInterface(uri);
      if (msgURI)
      {
        msgURI->GetMessageHeader(aMsgHdr);
        if (*aMsgHdr)
          return;
        nsCString rdfURI;
        msgURI->GetUri(getter_Copies(rdfURI));
        if (!rdfURI.IsEmpty())
        {
          nsCOMPtr<nsIMsgDBHdr> msgHdr;
          GetMsgDBHdrFromURI(rdfURI.get(), getter_AddRefs(msgHdr));
          NS_IF_ADDREF(*aMsgHdr = msgHdr);
        }
      }
    }
  }

  return;
}

PartClass* findClass(const char* contentType,
                            Headers* hdrs, DisplayOptions* opts, bool exact_match_p)
{
  PartClass* clazz = 0;
  PartClass* tempClass = 0;
  contentTypeHandlerInitStruct  ctHandlerInfo;

  // Read some prefs
  nsIPrefBranch *prefBranch = GetPrefBranch(opts);
  int32_t html_as = 0;  // def. see below
  int32_t typesOfClassesToDisallow = 0;  /* Let only a few libmime classes
       process incoming data. This protects from bugs (e.g. buffer overflows)
       and from security loopholes (e.g. allowing unchecked HTML in some
       obscure classes, although the user has html_as > 0).
       This option is mainly for the UI of html_as.
       0 = allow all available classes
       1 = Use hardcoded blacklist to avoid rendering (incoming) HTML
       2 = ... and images
       3 = ... and some other uncommon content types
       4 = show all body parts
       100 = Use hardcoded whitelist to avoid even more bugs(buffer overflows).
           This mode will limit the features available (e.g. uncommon
           attachment types and inline images) and is for paranoid users.
       */
  if (opts && opts->format_out != nsMimeOutput::nsMimeMessageFilterSniffer &&
               opts->format_out != nsMimeOutput::nsMimeMessageDecrypt
               && opts->format_out != nsMimeOutput::nsMimeMessageAttach)
    if (prefBranch)
    {
      prefBranch->GetIntPref("mailnews.display.html_as", &html_as);
      prefBranch->GetIntPref("mailnews.display.disallow_mime_handlers",
                            &typesOfClassesToDisallow);
      if (typesOfClassesToDisallow > 0 && html_as == 0)
           // We have non-sensical prefs. Do some fixup.
        html_as = 1;
    }

  // First, check to see if the message has been marked as JUNK. If it has,
  // then force the message to be rendered as simple, unless this has been
  // called by a filtering routine.
  bool sanitizeJunkMail = false;

  // it is faster to read the pref first then figure out the msg hdr for the current url only if we have to
  // XXX instead of reading this pref every time, part of mime should be an observer listening to this pref change
  // and updating internal state accordingly. But none of the other prefs in this file seem to be doing that...=(
  if (prefBranch)
    prefBranch->GetBoolPref("mail.spam.display.sanitize", &sanitizeJunkMail);

  if (sanitizeJunkMail &&
      !(opts && opts->format_out == nsMimeOutput::nsMimeMessageFilterSniffer))
  {
    nsCOMPtr<nsIMsgDBHdr> msgHdr;
    getMsgHdrForCurrentURL(opts, getter_AddRefs(msgHdr));
    if (msgHdr)
    {
      nsCString junkScoreStr;
      (void) msgHdr->GetStringProperty("junkscore", getter_Copies(junkScoreStr));
      if (html_as == 0 && junkScoreStr.get() && atoi(junkScoreStr.get()) > 50)
        html_as = 3; // 3 == Simple HTML
    } // if msgHdr
  } // if we are supposed to sanitize junk mail

  /*
  * What we do first is check for an external content handler plugin.
  * This will actually extend the mime handling by calling a routine
  * which will allow us to load an external content type handler
  * for specific content types. If one is not found, we will drop back
  * to the default handler.
  */
  if ((tempClass = locateExternalContentHandler(contentType, &ctHandlerInfo)) != NULL)
  {
#ifdef MOZ_THUNDERBIRD
      // This is a case where we only want to add this property if we are a thunderbird build AND
      // we have found an external mime content handler for text/calendar
      // This will enable iMIP support in Lightning
      if ( hdrs && (!PL_strncasecmp(contentType, "text/calendar", 13)))
      {
          char* fullContentType = hdrs->Get(HEADER_CONTENT_TYPE, false, false);
          if (fullContentType)
          {
              char *imip_method = Headers::GetParameter(fullContentType, "method", NULL, NULL);
              nsCOMPtr<nsIMsgDBHdr> msgHdr;
              getMsgHdrForCurrentURL(opts, getter_AddRefs(msgHdr));
              if (msgHdr)
                msgHdr->SetStringProperty("imip_method", (imip_method) ? imip_method : "nomethod");
              // PR_Free checks for null
              PR_Free(imip_method);
              PR_Free(fullContentType);
          }
      }
#endif

    if (typesOfClassesToDisallow > 0
        && (!PL_strncasecmp(contentType, "text/x-vcard", 12))
       )
      /* Use a little hack to prevent some dangerous plugins, which ship
         with Mozilla, to run.
         For the truly user-installed plugins, we rely on the judgement
         of the user. */
    {
      if (!exact_match_p)
        clazz = ExternalObjectClass; // As attachment
    }
    else
      clazz = (PartClass*)tempClass;
  }
  else
  {
    if (!contentType || !*contentType ||
        !PL_strcasecmp(contentType, "text"))  /* with no / in the type */
      clazz = UntypedTextClass;

    /* Subtypes of text...
    */
    else if (!PL_strncasecmp(contentType,      "text/", 5))
    {
      if      (!PL_strcasecmp(contentType+5,    "html"))
      {
        if (opts
            && opts->format_out == nsMimeOutput::nsMimeMessageSaveAs)
          // SaveAs in new modes doesn't work yet.
        {
          clazz = TextHTMLParsedClass;
          typesOfClassesToDisallow = 0;
        }
        else if (html_as == 0 || html_as == 4) // Render sender's HTML
          clazz = TextHTMLParsedClass;
        else if (html_as == 1) // convert HTML to plaintext
          // Do a HTML->TXT->HTML conversion, see mimethpl.h.
          clazz = TextHTMLAsPlaintextClass;
        else if (html_as == 2) // display HTML source
          /* This is for the freaks. Treat HTML as plaintext,
             which will cause the HTML source to be displayed.
             Not very user-friendly, but some seem to want this-> */
          clazz = TextPlainClass;
        else if (html_as == 3) // Sanitize
          // Strip all but allowed HTML
          clazz = TextHTMLSanitizedClass;
        else // Goofy pref
          /* User has an unknown pref value. Maybe he used a newer Mozilla
             with a new alternative to avoid HTML. Defaulting to option 1,
             which is less dangerous than defaulting to the raw HTML. */
          clazz = TextHTMLAsPlaintextClass;
      }
      else if (!PL_strcasecmp(contentType+5,    "enriched"))
        clazz = TextEnrichedClass;
      else if (!PL_strcasecmp(contentType+5,    "richtext"))
        clazz = TextRichtextClass;
      else if (!PL_strcasecmp(contentType+5,    "rtf"))
        clazz = ExternalObjectClass;
      else if (!PL_strcasecmp(contentType+5,    "plain"))
      {
        // Preliminary use the normal plain text
        clazz = TextPlainClass;

        if (opts && opts->format_out != nsMimeOutput::nsMimeMessageFilterSniffer
          && opts->format_out != nsMimeOutput::nsMimeMessageAttach
          && opts->format_out != nsMimeOutput::nsMimeMessageRaw)
        {
          bool disable_format_flowed = false;
          if (prefBranch)
            prefBranch->GetBoolPref("mailnews.display.disable_format_flowed_support",
                                    &disable_format_flowed);

          if(!disable_format_flowed)
          {
            // Check for format=flowed, damn, it is already stripped away from
            // the contenttype!
            // Look in headers instead even though it's expensive and clumsy
            // First find Content-Type:
            char *contentType_row =
              (hdrs
               ? hdrs->Get(HEADER_CONTENT_TYPE, false, false)
               : 0);
            // Then the format parameter if there is one.
            // I would rather use a PARAM_FORMAT but I can't find the right
            // place to put the define. The others seems to be in net.h
            // but is that really really the right place? There is also
            // a nsMimeTypes.h but that one isn't included. Bug?
            char *contentType_format =
              (contentType_row
               ? Headers::GetParameter(contentType_row, "format", NULL,NULL)
               : 0);

            if (contentType_format && !PL_strcasecmp(contentType_format,
                                                          "flowed"))
              clazz = TextPlainFlowedClass;
            PR_FREEIF(contentType_format);
            PR_FREEIF(contentType_row);
          }
        }
      }
      else if (!exact_match_p)
        clazz = TextPlainClass;
    }

    /* Subtypes of multipart...
    */
    else if (!PL_strncasecmp(contentType,      "multipart/", 10))
    {
      // When html_as is 4, we want all MIME parts of the message to
      // show up in the displayed message body, if they are MIME types
      // that we know how to display, and also in the attachment pane
      // if it's appropriate to put them there. Both
      // multipart/alternative and multipart/related play games with
      // hiding various MIME parts, and we don't want that to happen,
      // so we prevent that by parsing those MIME types as
      // multipart/mixed, which won't mess with anything.
      //
      // When our output format is nsMimeOutput::nsMimeMessageAttach,
      // i.e., we are reformatting the message to remove attachments,
      // we are in a similar boat. The code for deleting
      // attachments properly in that mode is in mimemult.cpp
      // functions which are inherited by MultipartMixedClass but
      // not by MultipartAlternativeClass or
      // MultipartRelatedClass. Therefore, to ensure that
      // everything is handled properly, in this context too we parse
      // those MIME types as multipart/mixed.
      bool basic_formatting = (html_as == 4) ||
        (opts && opts->format_out == nsMimeOutput::nsMimeMessageAttach);
      if      (!PL_strcasecmp(contentType+10,  "alternative"))
        clazz = basic_formatting ? MultipartMixedClass :
          MultipartAlternativeClass;
      else if (!PL_strcasecmp(contentType+10,  "related"))
        clazz = basic_formatting ? MultipartMixedClass :
          MultipartRelatedClass;
      else if (!PL_strcasecmp(contentType+10,  "digest"))
        clazz = MultipartDigestClass;
      else if (!PL_strcasecmp(contentType+10,  "appledouble") ||
               !PL_strcasecmp(contentType+10,  "header-set"))
        clazz = MultipartAppleDoubleClass;
      else if (!PL_strcasecmp(contentType+10,  "parallel"))
        clazz = MultipartParallelClass;
      else if (!PL_strcasecmp(contentType+10,  "mixed"))
        clazz = MultipartMixedClass;
#ifdef ENABLE_SMIME
      else if (!PL_strcasecmp(contentType+10,  "signed"))
      {
      /* Check that the "protocol" and "micalg" parameters are ones we
        know about. */
        char *ct = (hdrs
                    ? hdrs->Get(HEADER_CONTENT_TYPE, false, false)
                    : 0);
        char *proto = (ct
          ? Headers::GetParameter(ct, PARAM_PROTOCOL, NULL, NULL)
          : 0);
        char *micalg = (ct
          ? Headers::GetParameter(ct, PARAM_MICALG, NULL, NULL)
          : 0);

          if (proto
              && (
                  (/* is a signature */
                   !PL_strcasecmp(proto, APPLICATION_XPKCS7_SIGNATURE)
                   ||
                   !PL_strcasecmp(proto, APPLICATION_PKCS7_SIGNATURE))
                  && micalg
                  && (!PL_strcasecmp(micalg, PARAM_MICALG_MD5) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_MD5_2) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA1) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_2) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_3) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_4) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_5) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA256) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA256_2) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA256_3) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA384) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA384_2) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA384_3) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA512) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA512_2) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_SHA512_3) ||
                      !PL_strcasecmp(micalg, PARAM_MICALG_MD2))))
            clazz = MultipartSignedCMSClass;
          else
            clazz = 0;
        PR_FREEIF(proto);
        PR_FREEIF(micalg);
        PR_FREEIF(ct);
      }
#endif

      if (!clazz && !exact_match_p)
        /* Treat all unknown multipart subtypes as "multipart/mixed" */
        clazz = MultipartMixedClass;

      /* If we are sniffing a message, let's treat alternative parts as mixed */
      if (opts && opts->format_out == nsMimeOutput::nsMimeMessageFilterSniffer)
        if (clazz == MultipartAlternativeClass)
          clazz = MultipartMixedClass;
    }

    /* Subtypes of message...
    */
    else if (!PL_strncasecmp(contentType,      "message/", 8))
    {
      if      (!PL_strcasecmp(contentType+8,    "rfc822") ||
        !PL_strcasecmp(contentType+8,    "news"))
        clazz = MessageClass;
      else if (!PL_strcasecmp(contentType+8,    "external-body"))
        clazz = ExternalBodyClass;
      else if (!PL_strcasecmp(contentType+8,    "partial"))
        /* I guess these are most useful as externals, for now... */
        clazz = ExternalObjectClass;
      else if (!exact_match_p)
        /* Treat all unknown message subtypes as "text/plain" */
        clazz = TextPlainClass;
    }

    /* The magic image types which we are able to display internally...
    */
    else if (!PL_strncasecmp(contentType,    "image/", 6)) {
        if (imgLoader::SupportImageWithMimeType(contentType, AcceptedMimeTypes::IMAGES_AND_DOCUMENTS))
          clazz = InlineImageClass;
        else
          clazz = ExternalObjectClass;
    }

#ifdef ENABLE_SMIME
    else if (!PL_strcasecmp(contentType, APPLICATION_XPKCS7_MIME)
             || !PL_strcasecmp(contentType, APPLICATION_PKCS7_MIME)) {
        char *ct = (hdrs ? hdrs->Get(HEADER_CONTENT_TYPE, false, false)
                           : nullptr);
        char *st = (ct ? Headers::GetParameter(ct, "smime-type", NULL, NULL)
                         : nullptr);

        /* by default, assume that it is an encrypted message */
        clazz = EncryptedCMSClass;

        /* if the smime-type parameter says that it's a certs-only or
           compressed file, then show it as an attachment, however
           (MimeEncryptedCMS doesn't handle these correctly) */
        if (st &&
            (!PL_strcasecmp(st, "certs-only") ||
             !PL_strcasecmp(st, "compressed-data")))
          clazz = ExternalObjectClass;
        else {
          /* look at the file extension... less reliable, but still covered
             by the S/MIME specification (RFC 3851, section 3.2.1)  */
          char *name = (hdrs ? hdrs->GetFilename(opts) : nullptr);
          if (name) {
            char *suf = PL_strrchr(name, '.');
            bool p7mExternal = false;

            if (prefBranch)
              prefBranch->GetBoolPref("mailnews.p7m_external", &p7mExternal);
            if (suf &&
                ((!PL_strcasecmp(suf, ".p7m") && p7mExternal) ||
                 !PL_strcasecmp(suf, ".p7c") ||
                 !PL_strcasecmp(suf, ".p7z")))
              clazz = ExternalObjectClass;
          }
          PR_Free(name);
        }
        PR_Free(st);
        PR_Free(ct);
    }
#endif
    /* A few types which occur in the real world and which we would otherwise
    treat as non-text types (which would be bad) without this special-case...
    */
    else if (!PL_strcasecmp(contentType,      APPLICATION_PGP) ||
             !PL_strcasecmp(contentType,      APPLICATION_PGP2))
      clazz = TextPlainClass;

    else if (!PL_strcasecmp(contentType,      SUN_ATTACHMENT))
      clazz = SunAttachmentClass;

    /* Everything else gets represented as a clickable link.
    */
    else if (!exact_match_p)
      clazz = ExternalObjectClass;

    if (!IsAllowedClass(clazz, typesOfClassesToDisallow))
    {
      /* Do that check here (not after the if block), because we want to allow
         user-installed plugins. */
      if(!exact_match_p)
        clazz = ExternalObjectClass;
      else
        clazz = 0;
    }
  }

#ifdef ENABLE_SMIME
  // see bug #189988
  if (opts && opts->format_out == nsMimeOutput::nsMimeMessageDecrypt &&
       (clazz != EncryptedCMSClass)) {
    clazz = ExternalObjectClass;
  }
#endif

  NS_ASSERTION(!exact_match_p && clazz);
  if (!clazz) return 0; // TODO pointless
  return clazz;
}


Part* CreatePart(const char* contentType, Headers* hdrs,
       DisplayOptions* opts, bool forceInline /* = false */)
{
  /* If there is no Content-Disposition header, or if the Content-Disposition
   is "inline", then we display the part inline, and let findClass() decide how.

   If there is any other Content-Disposition (either ``attachment'' or some
   disposition that we don't recognise) then we always display the part as
   an external link, by using ExternalObject to display it.

   But Content-Disposition is ignored for all containers except `message'.
   (including multipart/mixed, and multipart/digest.)  It's not clear if
   this is to spec, but from a usability standpoint, I think it's necessary.
   */

  PartClass* clazz = nullptr;
  char *content_disposition = nullptr;
  Part* obj = nullptr;
  char *contentTypeOverride = nullptr;

  /* We've had issues where the incoming contentType is invalid, of a format:
     contentType="=?windows-1252?q?application/pdf" (bug 659355)
     We decided to fix that by simply trimming the stuff before the ?
  */
  if (contentType)
  {
    const char *lastQuestion = strrchr(contentType, '?');
    if (lastQuestion)
      contentType = lastQuestion + 1;  // the substring after the last '?'
  }

  /* There are some clients send out all attachments with a content-type
   of application/octet-stream.  So, if we have an octet-stream attachment,
   try to guess what type it really is based on the file extension.  I HATE
   that we have to do this->..
  */
  if (hdrs && opts && opts->file_type_fn &&

    /* ### mwelch - don't override AppleSingle */
    (contentType ? PL_strcasecmp(contentType, APPLICATION_APPLEFILE) : true) &&
    /* ## davidm Apple double shouldn't use this #$%& either. */
    (contentType ? PL_strcasecmp(contentType, MULTIPART_APPLEDOUBLE) : true) &&
    (!contentType ||
     !PL_strcasecmp(contentType, APPLICATION_OCTET_STREAM) ||
     !PL_strcasecmp(contentType, UNKNOWN_CONTENT_TYPE)))
  {
    char *name = hdrs->GetFilename(opts);
    if (name)
    {
      contentTypeOverride = opts->file_type_fn (name, opts->stream_closure);
      // appledouble isn't a valid override content type, and makes
      // attachments invisible.
      if (!PL_strcasecmp(contentTypeOverride, MULTIPART_APPLEDOUBLE))
        contentTypeOverride = nullptr;
      PR_FREEIF(name);

      // Workaround for saving '.eml" file encoded with base64.
      // Do not override with message/rfc822 whenever Transfer-Encoding is
      // base64 since base64 encoding of message/rfc822 is invalid.
      // Our |Message| class has no capability to decode it.
      if (!PL_strcasecmp(contentTypeOverride, MESSAGE_RFC822)) {
        nsCString encoding;
        encoding.Adopt(hdrs->Get(HEADER_CONTENT_TRANSFER_ENCODING,
                                       true, false));
        if (encoding.LowerCaseEqualsLiteral(ENCODING_BASE64))
          contentTypeOverride = nullptr;
      }

      // If we get here and it is not the unknown content type from the
      // file name, let's do some better checking not to inline something bad
      if (contentTypeOverride &&
          *contentTypeOverride &&
          (PL_strcasecmp(contentTypeOverride, UNKNOWN_CONTENT_TYPE)))
        contentType = contentTypeOverride;
    }
  }

  clazz = findClass(contentType, hdrs, opts, false);

  NS_ASSERTION(clazz, "1.1 <rhp@netscape.com> 19 Mar 1999 12:00");
  if (!clazz) goto FAIL;

  if (opts && opts->part_to_load)
  /* Always ignore Content-Disposition when we're loading some specific
     sub-part (which may be within some container that we wouldn't otherwise
     descend into, if the container itself had a Content-Disposition of
     `attachment'. */
  content_disposition = 0;

  else if (clazz->IsSubclassOf(ContainerClass) &&
       !clazz->IsSubclassOf(MessageClass))
  /* Ignore Content-Disposition on all containers except `message'.
     That is, Content-Disposition is ignored for multipart/mixed objects,
     but is obeyed for message/rfc822 objects. */
  content_disposition = 0;

  else
  {
    /* Check to see if the plugin should override the content disposition
       to make it appear inline. One example is a vcard which has a content
       disposition of an "attachment;" */
    if (IsForceInlineDisplay(contentType))
      NS_MsgSACopy(&content_disposition, "inline");
    else
      content_disposition = (hdrs
                 ? hdrs->Get(HEADER_CONTENT_DISPOSITION, true, false)
                 : 0);
  }

  if (!content_disposition || !PL_strcasecmp(content_disposition, "inline"))
    ;  /* Use the class we've got. */
  else
  {
    // override messages that have content disposition set to "attachment"
    // even though we probably should show them inline.
    if (  (clazz != TextClass) &&
          (clazz != TextPlainClass) &&
          (clazz != TextPlainFlowedClass) &&
          (clazz != TextHTMLClass) &&
          (clazz != TextHTMLParsedClass) &&
          (clazz != TextHTMLSanitizedClass) &&
          (clazz != TextHTMLAsPlaintextClass) &&
          (clazz != TextRichtextClass) &&
          (clazz != TextEnrichedClass) &&
          (clazz != MessageClass) &&
          (clazz != InlineImageClass) )
      // not a special inline type, so show as attachment
      clazz = ExternalObjectClass;
  }

  /* If the option `Show Attachments Inline' is off, now would be the time to change our mind... */
  /* Also, if we're doing a reply (i.e. quoting the body), then treat that according to preference. */
  if (opts && ((!opts->show_attachment_inline_p && !forceInline) ||
               (!opts->quote_attachment_inline_p &&
                (opts->format_out == nsMimeOutput::nsMimeMessageQuoting ||
                 opts->format_out == nsMimeOutput::nsMimeMessageBodyQuoting))))
  {
    if (clazz->IsSubclassOf(TextClass))
    {
      /* It's a text type.  Write it only if it's the *first* part
         that we're writing, and then only if it has no "filename"
         specified (the assumption here being, if it has a filename,
         it wasn't simply typed into the text field -- it was actually
         an attached document.) */
      if (opts->state && opts->state->first_part_written_p)
        clazz = ExternalObjectClass;
      else
      {
        /* If there's a name, then write this as an attachment. */
        char *name = (hdrs ? hdrs->GetFilename(opts) : nullptr);
        if (name)
        {
          clazz = ExternalObjectClass;
          PR_Free(name);
        }
      }
    }
    else
      if (clazz->IsSubclassOf(ContainerClass) &&
           !clazz->IsSubclassOf(MessageClass))
        /* Multipart subtypes are ok, except for messages; descend into
           multiparts, and defer judgement.

           Encrypted blobs are just like other containers (make the crypto
           layer invisible, and treat them as simple containers.  So there's
           no easy way to save encrypted data directly to disk; it will tend
           to always be wrapped inside a message/rfc822.  That's ok.) */
          ;
        else if (opts && opts->part_to_load &&
                  clazz->IsSubclassOf(MessageClass))
          /* Descend into messages only if we're looking for a specific sub-part. */
            ;
          else
          {
            /* Anything else, and display it as a link (and cause subsequent
               text parts to also be displayed as links.) */
            clazz = ExternalObjectClass;
          }
  }

  PR_FREEIF(content_disposition);
  obj = mime_new (clazz, hdrs, contentType);

 FAIL:

  /* If we decided to ignore the content-type in the headers of this object
   (see above) then make sure that our new content-type is stored in the
   object itself.  (Or free it, if we're in an out-of-memory situation.)
   */
  if (contentTypeOverride)
  {
    if (obj)
    {
      PR_FREEIF(this->contentType);
      this->contentType = contentTypeOverride;
    }
    else
    {
      PR_Free(contentTypeOverride);
    }
  }

  return obj;
}



// <move to="mimeobj.cpp" TODO>

bool PartClass::IsSubclassOf(PartClass* parent)
{
  if (this == parent)
    return true;
  else if (!this->superclass)
    return false;
  else
    return this->superclass->IsSubclass(parent);
}

bool Part::IsType(PartClass* clazz)
{
  return this->clazz->IsSubclassOf(clazz);
}

char* Part::PartAddress()
{
  if (!this->parent)
    return strdup("0");
  else
  {
    /* Find this object in its parent. */
    int32_t i, j = -1;
    char buf [20];
    char *higher = 0;
    Container *cont = (Container *) this->parent;
    NS_ASSERTION(this->parent->IsType(ContainerClass));
    for (i = 0; i < cont->nchildren; i++)
    if (cont->children[i] == this)
    {
      j = i+1;
      break;
    }
    if (j == -1)
    {
      NS_ERROR("No children under MeimContainer");
      return 0;
    }

    PR_snprintf(buf, sizeof(buf), "%ld", j);
    if (this->parent->parent)
    {
      higher = this->parent->PartAddress();
      if (!higher) return 0;  /* MIME_OUT_OF_MEMORY */
    }

    if (!higher)
      return strdup(buf);
    else
    {
      uint32_t slen = strlen(higher) + strlen(buf) + 3;
      char *s = (char *)PR_MALLOC(slen);
      if (!s)
      {
        PR_Free(higher);
        return 0;  /* MIME_OUT_OF_MEMORY */
      }
      PL_strncpyz(s, higher, slen);
      PL_strcatn(s, slen, ".");
      PL_strcatn(s, slen, buf);
      PR_Free(higher);
      return s;
    }
  }
}


char* Part::IMAPPartAddress()
{
  if (!this->headers)
    return 0;
  else
    return this->headers->Get(IMAP_EXTERNAL_CONTENT_HEADER, false, false);
}

/* Returns a full URL if the current Part has a EXTERNAL_ATTACHMENT_URL_HEADER
   header.
   Return value must be freed by the caller.
*/
char* Part::ExternalAttachmentURL()
{
  if (!this->headers)
    return 0;
  else
    return this->headers->Get(EXTERNAL_ATTACHMENT_URL_HEADER, false, false);
}

/* Whether the given object has written out the HTML version of its headers
   in such a way that it will have a "crypto stamp" next to the headers.  If
   this is true, then the child must write out its HTML slightly differently
   to take this into account...
 */
bool Part::IsCryptoStamped()
{
  return false;
}

// </move>


#ifdef ENABLE_SMIME
/* Asks whether the given object is one of the cryptographically signed
   or encrypted objects that we know about.  (MimeMessageClass uses this
   to decide if the headers need to be presented differently.)
 */
bool IsCryptoObject(Headers* hdrs, bool clearsigned_counts)
{
  char *ct;
  PartClass* clazz;

  if (!hdrs) return false;

  ct = hdrs->Get(HEADER_CONTENT_TYPE, true, false);
  if (!ct) return false;

  /* Rough cut -- look at the string before doing a more complex comparison. */
  if (PL_strcasecmp(ct, MULTIPART_SIGNED) &&
    PL_strncasecmp(ct, "application/", 12))
  {
    PR_Free(ct);
    return false;
  }

  /* It's a candidate for being a crypto object.  Let's find out for sure... */
  clazz = findClass(ct, hdrs, 0, true);
  PR_Free(ct);

  if (clazz == (EncryptedCMSClass))
    return true;
  else if (clearsigned_counts && clazz == (MultipartSignedCMSClass))
    return true;
  else
    return false;
}

#endif // ENABLE_SMIME

/* Puts a part-number into a URL.  If append_p is true, then the part number
   is appended to any existing part-number already in that URL; otherwise,
   it replaces it.
 */
char* SetURLPart(const char* url, const char* part, bool append_p)
{
  const char *part_begin = 0;
  const char *part_end = 0;
  bool got_q = false;
  const char *s;
  char *result;

  if (!url || !part) return 0;

  nsAutoCString urlString(url);
  int32_t typeIndex = urlString.Find("?type=application/x-message-display");
  if (typeIndex != -1)
  {
    urlString.Cut(typeIndex, sizeof("?type=application/x-message-display") - 1);
    if (urlString.CharAt(typeIndex) == '&')
      urlString.Replace(typeIndex, 1, '?');
    url = urlString.get();
  }

  for (s = url; *s; s++)
  {
    if (*s == '?')
    {
      got_q = true;
      if (!PL_strncasecmp(s, "?part=", 6))
      part_begin = (s += 6);
    }
    else if (got_q && *s == '&' && !PL_strncasecmp(s, "&part=", 6))
    part_begin = (s += 6);

    if (part_begin)
    {
      for (; (*s && *s != '?' && *s != '&'); s++)
      ;
      part_end = s;
      break;
          }
  }

  uint32_t resultlen = strlen(url) + strlen(part) + 10;
  result = (char *) PR_MALLOC(resultlen);
  if (!result) return 0;

  if (part_begin)
  {
    if (append_p)
    {
      memcpy(result, url, part_end - url);
      result [part_end - url]     = '.';
      result [part_end - url + 1] = 0;
    }
    else
    {
      memcpy(result, url, part_begin - url);
      result [part_begin - url] = 0;
    }
  }
  else
  {
    PL_strncpyz(result, url, resultlen);
    if (got_q)
    PL_strcatn(result, resultlen, "&part=");
    else
    PL_strcatn(result, resultlen, "?part=");
  }

  PL_strcatn(result, resultlen, part);

  if (part_end && *part_end)
  PL_strcatn(result, resultlen, part_end);

  /* Semi-broken kludge to omit a trailing "?part=0". */
  {
  int L = strlen(result);
  if (L > 6 &&
    (result[L-7] == '?' || result[L-7] == '&') &&
    !strcmp("part=0", result + L - 6))
    result[L-7] = 0;
  }

  return result;
}



/* Puts an *IMAP* part-number into a URL.
   Strips off any previous *IMAP* part numbers, since they are absolute, not relative.
 */
char* SetURLIMAPPart(const char *url, const char *imappart, const char *libmimepart)
{
  char *result = 0;
  char *whereCurrent = PL_strstr(url, "/;section=");
  if (whereCurrent)
  {
    *whereCurrent = 0;
  }

  uint32_t resultLen = strlen(url) + strlen(imappart) + strlen(libmimepart) + 17;
  result = (char *) PR_MALLOC(resultLen);
  if (!result) return 0;

  PL_strncpyz(result, url, resultLen);
  PL_strcatn(result, resultLen, "/;section=");
  PL_strcatn(result, resultLen, imappart);
  PL_strcatn(result, resultLen, "?part=");
  PL_strcatn(result, resultLen, libmimepart);

  if (whereCurrent)
    *whereCurrent = '/';

  return result;
}


// <move to="mimeobj.cpp" TODO>

Part* Part::GetObjectForPartAddress(const char *part)
{
  /* Note: this is an N^2 operation, but the number of parts in a message
   shouldn't ever be large enough that this really matters... */

  bool isMyself;

  if (!part || !*part)
  {
    isMyself = !this->parent;
  }
  else
  {
    char* part2 = this->PartAddress();
    if (!part2) return 0;  /* MIME_OUT_OF_MEMORY */
    isMyself = !strcmp(part, part2);
    PR_Free(part2);
  }

  if (isMyself)
  {
    /* These are the droids we're looking for. */
    return this;
  }
  else if (!this->IsType(ContainerClass))
  {
    /* Not a container, pull up, pull up! */
    return 0;
  }
  else
  {
    int32_t i;
    Container cont = (Container) *this;
    for (i = 0; i < cont.nchildren; i++)
    {
      Part* o2 = cont.children[i]->GetObjectForPartAddress(part);
      if (o2) return o2;
    }
    return 0;
  }
}

/* Given a part ID, looks through the |Part| tree for a sub-part whose ID
   number matches; if one is found, returns the Content-Name of that part.
   Else returns NULL.  (part is not a URL -- it's of the form "1.3.5".)
 */
char* Part::GetContentTypeOfPart(const char *part)
{
  Part* obj = this->GetObjectForPartAddress(part);
  if (!obj) return 0;

  return (this->headers ? this.headers->Get(HEADER_CONTENT_TYPE, true, false) : 0);
}

/* Given a part ID, looks through the |Part| tree for a sub-part whose ID
   number matches; if one is found, returns the Content-Name of that part.
   Else returns NULL.  (part is not a URL -- it's of the form "1.3.5".)
 */
char* Part::GetSuggestedNameOfPart(const char *part)
{
  char *result = 0;

  Part* obj = this->GetObjectForPartAddress(part);
  if (!obj) return 0;

  result = (this->headers ? this.headers->GetFilename(this.options) : 0);

  /* If this part doesn't have a name, but this part is one fork of an
   AppleDouble, and the AppleDouble itself has a name, then use that. */
  if (!result &&
    this->parent &&
    this->parent->headers &&
    this->parent->IsType(MultipartAppleDoubleClass))
  result = this->parent->headers->GetFilename(this.options);

  /* Else, if this part is itself an AppleDouble, and one of its children
   has a name, then use that (check data fork first, then resource.) */
  if (!result && this->IsType(MultipartAppleDoubleClass))
  {
    Container cont = (Container) *this;
    if (cont.nchildren > 1 &&
      cont.children[1] &&
      cont.children[1]->headers)
    result = cont.children[1]->headers->GetFilename(this->options);

    if (!result &&
      cont.nchildren > 0 &&
      cont.children[0] &&
      cont.children[0]->headers)
    result = cont.children[0]->headers->GetFilename(this->options);
  }

  /* Ok, now we have the suggested name, if any.
   Now we remove any extensions that correspond to the
   Content-Transfer-Encoding.  For example, if we see the headers

    Content-Type: text/plain
    Content-Disposition: inline; filename=foo.text.uue
    Content-Transfer-Encoding: x-uuencode

   then we would look up (in file /etc/mime.types) the file extensions which are
   associated with the x-uuencode encoding, find that "uue" is one of
   them, and remove that from the end of the file name, thus returning
   "foo.text" as the name.  This is because, by the time this file ends
   up on disk, its content-transfer-encoding will have been removed;
   therefore, we should suggest a file name that indicates that.
   */
  if (result && this->encoding && *this.encoding)
  {
    int32_t L = strlen(result);
    const char **exts = 0;

    /*
     I'd like to ask the /etc/mime.types file, "what extensions correspond
     to this->encoding (which happens to be "x-uuencode") but doing that
     in a non-sphagetti way would require brain surgery.  So, since
     currently uuencode is the only content-transfer-encoding which we
     understand which traditionally has an extension, we just special-
     case it here!  Icepicks in my forehead!

     Note that it's special-cased in a similar way in libmsg/compose.c.
     */
    if (!PL_strcasecmp(this->encoding, ENCODING_UUENCODE))
    {
      static const char *uue_exts[] = { "uu", "uue", 0 };
      exts = uue_exts;
    }

    while (exts && *exts)
    {
      const char *ext = *exts;
      int32_t L2 = strlen(ext);
      if (L > L2 + 1 &&              /* long enough */
        result[L - L2 - 1] == '.' &&      /* '.' in right place*/
        !PL_strcasecmp(ext, result + (L - L2)))  /* ext matches */
      {
        result[L - L2 - 1] = 0;    /* truncate at '.' and stop. */
        break;
      }
      exts++;
    }
  }

  return result;
}

// </move>

/* Parse the various "?" options off the URL and into the options struct.
 */
int ParseURLOptions(const char* url, DisplayOptions* options)
{
  const char *q;

  if (!url || !*url) return 0;
  if (!options) return 0;

  HeadersState default_headers = options->headers;

  q = PL_strrchr (url, '?');
  if (! q) return 0;
  q++;
  while (*q)
  {
    const char *end, *value, *name_end;
    for (end = q; *end && *end != '&'; end++)
      ;
    for (value = q; *value != '=' && value < end; value++)
      ;
    name_end = value;
    if (value < end) value++;
    if (name_end <= q)
      ;
    else if (!PL_strncasecmp ("headers", q, name_end - q))
    {
      if (end > value && !PL_strncasecmp ("only", value, end-value))
        options->headers = HeadersState::Only;
      else if (end > value && !PL_strncasecmp ("none", value, end-value))
        options->headers = HeadersState::None;
      else if (end > value && !PL_strncasecmp ("all", value, end - value))
        options->headers = HeadersState::All;
      else if (end > value && !PL_strncasecmp ("some", value, end - value))
        options->headers = HeadersState::Some;
      else if (end > value && !PL_strncasecmp ("micro", value, end - value))
        options->headers = HeadersState::Micro;
      else if (end > value && !PL_strncasecmp ("cite", value, end - value))
        options->headers = HeadersState::Citation;
      else if (end > value && !PL_strncasecmp ("citation", value, end-value))
        options->headers = HeadersState::Citation;
      else
        options->headers = default_headers;
    }
    else if (!PL_strncasecmp ("part", q, name_end - q) &&
      options->format_out != nsMimeOutput::nsMimeMessageBodyQuoting)
    {
      PR_FREEIF (options->part_to_load);
      if (end > value)
      {
        options->part_to_load = (char *) PR_MALLOC(end - value + 1);
        if (!options->part_to_load)
          return MIME_OUT_OF_MEMORY;
        memcpy(options->part_to_load, value, end-value);
        options->part_to_load[end-value] = 0;
      }
    }
    else if (!PL_strncasecmp ("rot13", q, name_end - q))
    {
      options->rot13_p = end <= value || !PL_strncasecmp ("true", value, end - value);
    }
    else if (!PL_strncasecmp ("emitter", q, name_end - q))
    {
      if ((end > value) && !PL_strncasecmp ("js", value, end - value))
      {
        // the js emitter needs to hear about nested message bodies
        //  in order to build a proper representation.
        options->notify_nested_bodies = true;
        // show_attachment_inline_p has the side-effect of letting the
        //  emitter see all parts of a multipart/alternative, which it
        //  really appreciates.
        options->show_attachment_inline_p = true;
        // however, show_attachment_inline_p also results in a few
        // subclasses writing junk into the body for display purposes.
        // Put a stop to these shenanigans by enabling write_pure_bodies.
        // Current offenders are: |InlineImage|
        options->write_pure_bodies = true;
        // we don't actually care about the data in the attachments, just the
        // metadata (i.e. size)
        options->metadata_only = true;
      }
    }

    q = end;
    if (*q)
      q++;
  }


/* Compatibility with the "?part=" syntax used in the old (Mozilla 2.0)
   MIME parser.

   Basically, the problem is that the old part-numbering code was totally
   busted: here's a comparison of the old and new numberings with a pair
   of hypothetical messages (one with a single part, and one with nested
   containers.)
                      NEW:      OLD:  OR:
   message/rfc822
   image/jpeg         1         0     0

   message/rfc822
   multipart/mixed    1         0     0
   text/plain         1.1       1     1
   image/jpeg         1.2       2     2
   message/rfc822     1.3       -     3
   text/plain         1.3.1     3     -
   message/rfc822     1.4       -     4
   multipart/mixed    1.4.1     4     -
   text/plain         1.4.1.1   4.1   -
   image/jpeg         1.4.1.2   4.2   -
   text/plain         1.5       5     5

 The "NEW" column is how the current code counts.  The "OLD" column is
 what "?part=" references would do in 3.0b4 and earlier; you'll see that
 you couldn't directly refer to the child message/rfc822 objects at all!
 But that's when it got really weird, because if you turned on
 "Attachments As Links" (or used a URL like "?inline=false&part=...")
 then you got a totally different numbering system (seen in the "OR"
 column.)  Gag!

 So, the problem is, ClariNet had been using these part numbers in their
 HTML news feeds, as a sleazy way of transmitting both complex HTML layouts
 and images using NNTP as transport, without invoking HTTP.

 The following clause is to provide some small amount of backward
 compatibility.  By looking at that table, one can see that in the new
 model, "part=0" has no meaning, and neither does "part=2" or "part=3"
 and so on.

 "part=1" is ambiguous between the old and new way, as is any part
 specification that has a "." in it.

 So, the compatibility hack we do here is: if the part is "0", then map
 that to "1".  And if the part is >= "2", then prepend "1." to it (so that
 we map "2" to "1.2", and "3" to "1.3".)

 This leaves the URLs compatible in the cases of:

 = single part messages
 = references to elements of a top-level multipart except the first

 and leaves them incompatible for:

 = the first part of a top-level multipart
 = all elements deeper than the outermost part

 Life s#$%s when you don't properly think out things that end up turning
 into de-facto standards...
 */

 if (options->part_to_load &&
   !PL_strchr(options->part_to_load, '.'))    /* doesn't contain a dot */
 {
   if (!strcmp(options->part_to_load, "0"))    /* 0 */
   {
     PR_Free(options->part_to_load);
     options->part_to_load = strdup("1");
     if (!options->part_to_load)
       return MIME_OUT_OF_MEMORY;
   }
   else if (strcmp(options->part_to_load, "1"))  /* not 1 */
   {
     const char *prefix = "1.";
     uint32_t slen = strlen(options->part_to_load) + strlen(prefix) + 1;
     char *s = (char *) PR_MALLOC(slen);
     if (!s) return MIME_OUT_OF_MEMORY;
     PL_strncpyz(s, prefix, slen);
     PL_strcatn(s, slen, options->part_to_load);
     PR_Free(options->part_to_load);
     options->part_to_load = s;
   }
 }

  return 0;
}


/* Some output-generation utility functions...
 */

int Options::Write(Headers* hdrs, DisplayOptions* opt, const char* data,
                  int32_t length, bool userVisible)
{
  int status = 0;
  void* closure = 0;
  if (!opt || !opt->output_fn || !opt->state)
  return 0;

  closure = opt->output_closure;
  if (!closure) closure = opt->stream_closure;

//  PR_ASSERT(opt->state->first_data_written_p);

  if (opt->state->separator_queued_p && userVisible)
  {
    opt->state->separator_queued_p = false;
    if (opt->state->separator_suppressed_p)
      opt->state->separator_suppressed_p = false;
    else {
      const char *sep = "<BR><FIELDSET CLASS=\"mimeAttachmentHeader\">";
      int lstatus = opt->output_fn(sep, strlen(sep), closure);
      opt->state->separator_suppressed_p = false;
      if (lstatus < 0) return lstatus;

      nsCString name;
      name.Adopt(hdrs->GetFilename(opt));
      Headers::ConvertHeaderValue(opt, name, false);

      if (!name.IsEmpty()) {
          sep = "<LEGEND CLASS=\"mimeAttachmentHeaderName\">";
          lstatus = opt->output_fn(sep, strlen(sep), closure);
          opt->state->separator_suppressed_p = false;
          if (lstatus < 0) return lstatus;

          nsCString escapedName;
          nsAppendEscapedHTML(name, escapedName);

          lstatus = opt->output_fn(escapedName.get(),
                                   escapedName.Length(), closure);
          opt->state->separator_suppressed_p = false;
          if (lstatus < 0) return lstatus;

          sep = "</LEGEND>";
          lstatus = opt->output_fn(sep, strlen(sep), closure);
          opt->state->separator_suppressed_p = false;
          if (lstatus < 0) return lstatus;
      }

      sep = "</FIELDSET>";
      lstatus = opt->output_fn(sep, strlen(sep), closure);
      opt->state->separator_suppressed_p = false;
      if (lstatus < 0) return lstatus;
    }
  }
  if (userVisible)
  opt->state->separator_suppressed_p = false;

  if (length > 0)
  {
    status = opt->output_fn(data, length, closure);
    if (status < 0) return status;
  }

  return 0;
}

// <move to="mimeobj.cpp" TODO>

int Part::Write(const char* output, int32_t length, bool userVisible)
{
  if (!this->output_p) return 0;

  // if we're stripping attachments, check if any parent is not being output
  if (this->options->format_out == nsMimeOutput::nsMimeMessageAttach)
  {
    // if true, mime generates a separator in html - we don't want that.
    userVisible = false;

    for (Part* parent = this->parent; parent; parent = parent->parent)
    {
      if (!parent->output_p)
        return 0;
    }
  }
  if (!this->options->state->first_data_written_p)
  {
    int status = this->OutputInit(0);
    if (status < 0) return status;
    NS_ASSERTION(this->options->state->first_data_written_p);
  }

  return Options::Write(this->headers, this.options, output, length, userVisible);
}

int Part::WriteSeparator()
{
  if (this->options && this.options->state &&
      // we never want separators if we are asking for pure bodies
      !this->options->write_pure_bodies)
    this->options->state->separator_queued_p = true;
  return 0;
}

int Part::OutputInit(const char* contentType)
{
  if (this->options &&
    this->options->state &&
    !thisoptions->state->firstDataWritten)
  {
    int status;
    const char *charset = 0;
    char *name = 0, *x_mac_type = 0, *x_mac_creator = 0;

    if (!this->options->output_init_fn)
    {
      this->options->state->first_data_written_p = true;
      return 0;
    }

    if (this->headers)
    {
      char *ct;
      name = this->headers->GetFilename(this.options);

      ct = this->headers->Get(HEADER_CONTENT_TYPE,
                 false, false);
      if (ct)
      {
        x_mac_type   = Headers::GetParameter(ct, PARAM_X_MAC_TYPE, NULL, NULL);
        x_mac_creator= Headers::GetParameter(ct, PARAM_X_MAC_CREATOR, NULL, NULL);
        /* if don't have a x_mac_type and x_mac_creator, we need to try to get it from its parent */
        if (!x_mac_type && !x_mac_creator && this->parent && this.parent->headers)
        {
          char * ctp = this->parent->headers->Get(HEADER_CONTENT_TYPE, false, false);
          if (ctp)
          {
            x_mac_type   = Headers::GetParameter(ctp, PARAM_X_MAC_TYPE, NULL, NULL);
            x_mac_creator= Headers::GetParameter(ctp, PARAM_X_MAC_CREATOR, NULL, NULL);
            PR_Free(ctp);
          }
        }

        if (!(this->options->override_charset)) {
          char *charset = Headers::GetParameter(ct, "charset", nullptr, nullptr);
          if (charset)
          {
            PR_FREEIF(this->options->default_charset);
            this->options->default_charset = charset;
          }
        }
        PR_Free(ct);
      }
    }

    if (this->IsType(TextClass))
      charset = ((Text) this).charset;

    if (!contentType)
      contentType = this->contentType;
    if (!contentType)
      contentType = TEXT_PLAIN;

    // Set the charset on the channel we are dealing with so people know
    // what the charset is set to. Do this for quoting/Printing ONLY!
    extern void ResetChannelCharset(Part* obj); // declare
    if ( (this->options) &&
         ( (this->options->format_out == nsMimeOutput::nsMimeMessageQuoting) ||
           (this->options->format_out == nsMimeOutput::nsMimeMessageBodyQuoting) ||
           (this->options->format_out == nsMimeOutput::nsMimeMessageSaveAs) ||
           (this->options->format_out == nsMimeOutput::nsMimeMessagePrintOutput) ) )
      ResetChannelCharset(obj);

    status = this->options->output_init_fn (contentType, charset, name,
                       x_mac_type, x_mac_creator,
                       this->options->stream_closure);
    PR_FREEIF(name);
    PR_FREEIF(x_mac_type);
    PR_FREEIF(x_mac_creator);
    this->options->state->first_data_written_p = true;
    return status;
  }
  return 0;
}
// </move>

char* getBaseURL(const char *url)
{
  if (!url)
    return nullptr;

  const char *s = strrchr(url, '?');
  if (s && !strncmp(s, "?type=application/x-message-display", sizeof("?type=application/x-message-display") - 1))
  {
    const char *nextTerm = strchr(s, '&');
    s = (nextTerm) ? nextTerm : s + strlen(s) - 1;
  }
  // we need to keep the ?number part of the url, or we won't know
  // which local message the part belongs to.
  if (s && *s && *(s+1) && !strncmp(s + 1, "number=", sizeof("number=") - 1))
  {
    const char *nextTerm = strchr(++s, '&');
    s = (nextTerm) ? nextTerm : s + strlen(s) - 1;
  }
  char *result = (char *) PR_MALLOC(strlen(url) + 1);
  NS_ASSERTION(result, "out of memory");
  if (!result)
    return nullptr;

  memcpy(result, url, s - url);
  result[s - url] = 0;
  return result;
}


} // namespace mime
} // namespace mozilla
