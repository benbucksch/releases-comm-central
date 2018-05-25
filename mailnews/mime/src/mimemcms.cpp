/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsICMSMessage.h"
#include "nsICMSMessageErrors.h"
#include "nsICMSDecoder.h"
#include "nsICryptoHash.h"
#include "mimemcms.h"
#include "mimecryp.h"
#include "nsMimeTypes.h"
#include "nspr.h"
#include "nsMimeStringResources.h"
#include "mimemsg.h"
#include "mimemoz2.h"
#include "nsIURI.h"
#include "nsIMsgWindow.h"
#include "nsIMsgMailNewsUrl.h"
#include "nsIMimeMiscStatus.h"
#include "nsIMsgSMIMEHeaderSink.h"
#include "nsCOMPtr.h"
#include "nsIX509Cert.h"
#include "plstr.h"
#include "nsComponentManagerUtils.h"

extern int SEC_ERROR_CERT_ADDR_MISMATCH;

/* #### MimeEncryptedCMS and MimeMultipartSignedCMS have a sleazy,
        incestuous, dysfunctional relationship. */
extern void MimeCMSGetFromSender(Part* obj,
                                 nsCString &from_addr,
                                 nsCString &from_name,
                                 nsCString &sender_addr,
                                 nsCString &sender_name);
extern bool MimeCMSHeadersAndCertsMatch(Part* obj,
                                          nsICMSMessage *,
                                          bool *signing_cert_without_email_address);
extern void MimeCMSRequestAsyncSignatureVerification(nsICMSMessage *aCMSMsg,
                                                     const char *aFromAddr, const char *aFromName,
                                                     const char *aSenderAddr, const char *aSenderName,
                                                     nsIMsgSMIMEHeaderSink *aHeaderSink, int32_t aMimeNestingLevel,
                                                     unsigned char* item_data, uint32_t item_len);
extern char *MimeCMS_MakeSAURL(Part* obj);
extern char *IMAP_CreateReloadAllPartsUrl(const char *url);
extern int MIMEGetRelativeCryptoNestLevel(Part* obj);

namespace mozilla::mime {

namespace mozilla::mime {

class MultCMSdata
{
public:
  int16_t hash_type;
  nsCOMPtr<nsICryptoHash> data_hash_context;
  nsCOMPtr<nsICMSDecoder> sig_decoder_context;
  nsCOMPtr<nsICMSMessage> content_info;
  char *sender_addr;
  bool decoding_failed;
  unsigned char* item_data;
  uint32_t item_len;
  Part* self;
  bool parent_is_encrypted_p;
  bool parent_holds_stamp_p;
  nsCOMPtr<nsIMsgSMIMEHeaderSink> smimeHeaderSink;

  MultCMSdata()
  :hash_type(0),
  sender_addr(nullptr),
  decoding_failed(false),
  item_data(nullptr),
  self(nullptr),
  parent_is_encrypted_p(false),
  parent_holds_stamp_p(false)
  {
  }

  ~MultCMSdata()
  {
    PR_FREEIF(sender_addr);

    // Do a graceful shutdown of the nsICMSDecoder and release the nsICMSMessage //
    if (sig_decoder_context)
    {
      nsCOMPtr<nsICMSMessage> cinfo;
      sig_decoder_context->Finish(getter_AddRefs(cinfo));
    }

    delete [] item_data;
  }
};

void*
MultipartSignedCMS::CryptoInit()
{
  Headers *hdrs = this->headers;
  MultCMSdata* data = nullptr;
  char *ct, *micalg;
  int16_t hash_type;
  nsresult rv;

  ct = headers->Get(HEADER_CONTENT_TYPE, false, false);
  if (!ct) return 0; /* #### bogus message?  out of memory? */
  micalg = Headers::GetParameter(ct, PARAM_MICALG, NULL, NULL);
  PR_Free(ct);
  ct = 0;
  if (!micalg) return 0; /* #### bogus message?  out of memory? */

  if (!PL_strcasecmp(micalg, PARAM_MICALG_MD5) ||
      !PL_strcasecmp(micalg, PARAM_MICALG_MD5_2))
    hash_type = nsICryptoHash::MD5;
  else if (!PL_strcasecmp(micalg, PARAM_MICALG_SHA1) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_2) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_3) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_4) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA1_5))
    hash_type = nsICryptoHash::SHA1;
  else if (!PL_strcasecmp(micalg, PARAM_MICALG_SHA256) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA256_2) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA256_3))
    hash_type = nsICryptoHash::SHA256;
  else if (!PL_strcasecmp(micalg, PARAM_MICALG_SHA384) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA384_2) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA384_3))
    hash_type = nsICryptoHash::SHA384;
  else if (!PL_strcasecmp(micalg, PARAM_MICALG_SHA512) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA512_2) ||
       !PL_strcasecmp(micalg, PARAM_MICALG_SHA512_3))
    hash_type = nsICryptoHash::SHA512;
  else if (!PL_strcasecmp(micalg, PARAM_MICALG_MD2))
    hash_type = nsICryptoHash::MD2;
  else
    hash_type = -1;

  PR_Free(micalg);
  micalg = 0;

  if (hash_type == -1) return 0; /* #### bogus message? */

  data = new MultCMSdata;
  if (!data)
    return 0;

  data->self = this;
  data->hash_type = hash_type;

  data->data_hash_context = do_CreateInstance("@mozilla.org/security/hash;1", &rv);
  if (NS_FAILED(rv))
  {
    delete data;
    return 0;
  }

  rv = data->data_hash_context->Init(data->hash_type);
  if (NS_FAILED(rv))
  {
    delete data;
    return 0;
  }

  PR_SetError(0,0);

  data->parent_holds_stamp_p = this->parent && this->parent->IsCryptoStamped();

  data->parent_is_encrypted_p = this->parent && this->parent->IsEncrypted();

  /* If the parent of this object is a crypto-blob, then it's the grandparent
   who would have written out the headers and prepared for a stamp...
   (This s##t s$%#s.)
   */
  if (data->parent_is_encrypted_p &&
    !data->parent_holds_stamp_p &&
    this->parent && this->parent->parent)
  data->parent_holds_stamp_p = this->parent->parent->IsCryptoStamped();

  mime_stream_data *msd = (mime_stream_data *) (this->options->stream_closure);
  if (msd)
  {
    nsIChannel *channel = msd->channel;  // note the lack of ref counting...
    if (channel)
    {
      nsCOMPtr<nsIURI> uri;
      nsCOMPtr<nsIMsgWindow> msgWindow;
      nsCOMPtr<nsIMsgHeaderSink> headerSink;
      nsCOMPtr<nsIMsgMailNewsUrl> msgurl;
      nsCOMPtr<nsISupports> securityInfo;
      channel->GetURI(getter_AddRefs(uri));
      if (uri)
      {
        nsAutoCString urlSpec;
        rv = uri->GetSpec(urlSpec);

        // We only want to update the UI if the current mime transaction
        // is intended for display.
        // If the current transaction is intended for background processing,
        // we can learn that by looking at the additional header=filter
        // string contained in the URI.
        //
        // If we find something, we do not set smimeHeaderSink,
        // which will prevent us from giving UI feedback.
        //
        // If we do not find header=filter, we assume the result of the
        // processing will be shown in the UI.

        if (!strstr(urlSpec.get(), "?header=filter") &&
            !strstr(urlSpec.get(), "&header=filter")&&
            !strstr(urlSpec.get(), "?header=attach") &&
            !strstr(urlSpec.get(), "&header=attach"))
        {
          msgurl = do_QueryInterface(uri);
          if (msgurl)
            msgurl->GetMsgWindow(getter_AddRefs(msgWindow));
          if (msgWindow)
            msgWindow->GetMsgHeaderSink(getter_AddRefs(headerSink));
          if (headerSink)
            headerSink->GetSecurityInfo(getter_AddRefs(securityInfo));
          if (securityInfo)
            data->smimeHeaderSink = do_QueryInterface(securityInfo);
         }
       }
    } // if channel
  } // if msd

  return data;
}

int
MultipartSignedCMS::CryptoDataHash(const char* buf, int32_t size, void* crypto_closure)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  if (!data || !data->data_hash_context) {
    return -1;
  }

  PR_SetError(0, 0);
  nsresult rv = data->data_hash_context->Update((unsigned char *) buf, size);
  data->decoding_failed = NS_FAILED(rv);

  return 0;
}

int
MultipartSignedCMS::CryptoDataEOF(void* crypto_closure, bool abort_p)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  if (!data || !data->data_hash_context) {
    return -1;
  }

  nsAutoCString hashString;
  data->data_hash_context->Finish(false, hashString);
  PR_SetError(0, 0);

  data->item_len  = hashString.Length();
  data->item_data = new unsigned char[data->item_len];
  if (!data->item_data) return MIME_OUT_OF_MEMORY;

  memcpy(data->item_data, hashString.get(), data->item_len);

  // Release our reference to nsICryptoHash //
  data->data_hash_context = nullptr;

  /* At this point, data->item.data contains a digest for the first part.
   When we process the signature, the security library will compare this
   digest to what's in the signature object. */

  return 0;
}

int
MultipartSignedCMS::CryptoSignatureInit(void* crypto_closure,
            Part* multipart_object,
            Headers* signature_hdrs)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  char *ct;
  int status = 0;
  nsresult rv;

  if (!signature_hdrs) {
    return -1;
  }

  ct = signature_hdrs->Get(HEADER_CONTENT_TYPE, true, false);

  /* Verify that the signature object is of the right type. */
  if (!ct || /* is not a signature type */
             (PL_strcasecmp(ct, APPLICATION_XPKCS7_SIGNATURE) != 0
              && PL_strcasecmp(ct, APPLICATION_PKCS7_SIGNATURE) != 0)) {
    status = -1; /* #### error msg about bogus message */
  }
  PR_FREEIF(ct);
  if (status < 0) return status;

  data->sig_decoder_context = do_CreateInstance(NS_CMSDECODER_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return 0;

  rv = data->sig_decoder_context->Start(nullptr, nullptr);
  if (NS_FAILED(rv)) {
    status = PR_GetError();
    if (status >= 0) status = -1;
  }
  return status;
}


int
MultipartSignedCMS::CryptoSignatureHash(const char* buf, int32_t size, void* crypto_closure)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  nsresult rv;

  if (!data || !data->sig_decoder_context) {
    return -1;
  }

  rv = data->sig_decoder_context->Update(buf, size);
  data->decoding_failed = NS_FAILED(rv);

  return 0;
}

int
MultipartSignedCMS::CryptoSignatureEOF(void* crypto_closure, bool abort_p)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;

  if (!data) {
    return -1;
  }

  /* Hand an EOF to the crypto library.

   We save away the value returned and will use it later to emit a
   blurb about whether the signature validation was cool.
   */

  if (data->sig_decoder_context) {
    data->sig_decoder_context->Finish(getter_AddRefs(data->content_info));

    // Release our reference to nsICMSDecoder //
    data->sig_decoder_context = nullptr;
  }

  return 0;
}

int
MultipartSignedCMS::CryptoFree(void* crypto_closure)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  if (!data) return;

  delete data;
}

char*
MultipartSignedCMS::CryptoGenerateHTML(void* crypto_closure)
{
  MultCMSdata* data = (MultCMSdata*)crypto_closure;
  if (!data) return 0;
  nsCOMPtr<nsIX509Cert> signerCert;

  int aRelativeNestLevel = MIMEGetRelativeCryptoNestLevel(this);

  if (aRelativeNestLevel < 0)
    return nullptr;

  int32_t maxNestLevel = 0;
  if (data->smimeHeaderSink && aRelativeNestLevel >= 0)
  {
    data->smimeHeaderSink->MaxWantedNesting(&maxNestLevel);

    if (aRelativeNestLevel > maxNestLevel)
      return nullptr;
  }

  if (this->options->missing_parts)
  {
    // We were not given all parts of the message.
    // We are therefore unable to verify correctness of the signature.

    if (data->smimeHeaderSink)
      data->smimeHeaderSink->SignedStatus(aRelativeNestLevel,
                                          nsICMSMessageErrors::VERIFY_NOT_YET_ATTEMPTED,
                                          nullptr);
    return nullptr;
  }

  if (!data->content_info)
  {
    /* No content_info at all -- since we're inside a multipart/signed,
     that means that we've either gotten a message that was truncated
     before the signature part, or we ran out of memory, or something
     awful has happened.
     */
     return nullptr;
  }

  nsCString from_addr;
  nsCString from_name;
  nsCString sender_addr;
  nsCString sender_name;

  MimeCMSGetFromSender(this,
                       from_addr, from_name,
                       sender_addr, sender_name);

  MimeCMSRequestAsyncSignatureVerification(data->content_info,
                                           from_addr.get(), from_name.get(),
                                           sender_addr.get(), sender_name.get(),
                                           data->smimeHeaderSink, aRelativeNestLevel,
                                           data->item_data, data->item_len);

  if (data->content_info)
  {
#if 0 // XXX Fix this-> What do we do here? //
    if (SEC_CMSContainsCertsOrCrls(data->content_info))
    {
      /* #### call libsec telling it to import the certs */
    }
#endif
  }

  return nullptr;
}

} // namespace mozilla::mime
