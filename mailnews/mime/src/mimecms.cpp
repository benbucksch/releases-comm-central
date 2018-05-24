/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsICMSMessage.h"
#include "nsICMSMessage2.h"
#include "nsICMSMessageErrors.h"
#include "nsICMSDecoder.h"
#include "mimecms.h"
#include "mimemsig.h"
#include "nspr.h"
#include "mimemsg.h"
#include "mimemoz2.h"
#include "nsIURI.h"
#include "nsIMsgWindow.h"
#include "nsIMsgMailNewsUrl.h"
#include "nsIMimeMiscStatus.h"
#include "nsIMsgSMIMEHeaderSink.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsIX509Cert.h"
#include "nsServiceManagerUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsProxyRelease.h"
#include "mozilla/mailnews/MimeHeaderParser.h"

using namespace mozilla::mailnews;

extern int SEC_ERROR_CERT_ADDR_MISMATCH;

namespace mozilla::mime {

typedef struct MimeCMSdata
{
  int (*output_fn) (const char *buf, int32_t buf_size, void *output_closure);
  void *output_closure;
  nsCOMPtr<nsICMSDecoder> decoder_context;
  nsCOMPtr<nsICMSMessage> content_info;
  bool ci_is_encrypted;
  char *sender_addr;
  bool decoding_failed;
  uint32_t decoded_bytes;
  Part *self;
  bool parent_is_encrypted_p;
  bool parent_holds_stamp_p;
  nsCOMPtr<nsIMsgSMIMEHeaderSink> smimeHeaderSink;

  MimeCMSdata()
  :output_fn(nullptr),
  output_closure(nullptr),
  ci_is_encrypted(false),
  sender_addr(nullptr),
  decoding_failed(false),
  decoded_bytes(0),
  self(nullptr),
  parent_is_encrypted_p(false),
  parent_holds_stamp_p(false)
  {
  }

  ~MimeCMSdata()
  {
    if(sender_addr)
      PR_Free(sender_addr);

    // Do an orderly release of nsICMSDecoder and nsICMSMessage //
    if (decoder_context)
    {
      nsCOMPtr<nsICMSMessage> cinfo;
      decoder_context->Finish(getter_AddRefs(cinfo));
    }
  }
} MimeCMSdata;

class EncryptedCMS : public Encrypted {
  typedef Encrypted Super;

protected:
  EncryptedCMS();
  virtual ~EncryptedCMS();

public:
  // Part
  virtual bool IsEncrypted() override;

  // Encrypted
  virtual void* CryptoInit(int (*output_fn) (const char *buf, int32_t bufSize, void *output_closure), void* output_closure) override;
  virtual int CryptoWrite(const char* buf, int32_t bufSize, void *closure) override;
  virtual int CryptoEOF(void *crypto_closure, bool abort_p) override;
  virtual char* GenerateHTML(void *crypto_closure) override;
  virtual int CryptoFree(void *crypto_closure) override;
};

/*   SEC_PKCS7DecoderContentCallback for SEC_PKCS7DecoderStart() */
static void MimeCMS_content_callback (void *arg, const char *buf, unsigned long length)
{
  int status;
  MimeCMSdata *data = (MimeCMSdata *) arg;
  if (!data) return;

  if (!data->output_fn)
    return;

  PR_SetError(0,0);
  status = data->output_fn (buf, length, data->output_closure);
  if (status < 0)
  {
    PR_SetError(status, 0);
    data->output_fn = 0;
    return;
  }

  data->decoded_bytes += length;
}

bool EncryptedCMS::IsEncrypted()
{
  MimeCMSdata *data = (MimeCMSdata*)this->crypto_closure;
  if (!data || !data->content_info) return false;
  bool encrypted;
  data->content_info->ContentIsEncrypted(&encrypted);
  return encrypted;
}

} // namespace mozilla::mime

bool MimeCMSHeadersAndCertsMatch(nsICMSMessage *content_info,
                                   nsIX509Cert *signerCert,
                                   const char *from_addr,
                                   const char *from_name,
                                   const char *sender_addr,
                                   const char *sender_name,
                                   bool *signing_cert_without_email_address)
{
  nsCString cert_addr;
  bool match = true;
  bool foundFrom = false;
  bool foundSender = false;

  /* Find the name and address in the cert.
   */
  if (content_info)
  {
    // Extract any address contained in the cert.
    // This will be used for testing, whether the cert contains no addresses at all.
    content_info->GetSignerEmailAddress (getter_Copies(cert_addr));
  }

  if (signing_cert_without_email_address)
    *signing_cert_without_email_address = cert_addr.IsEmpty();

  /* Now compare them --
   consider it a match if the address in the cert matches the
   address in the From field (or as a fallback, the Sender field)
   */

  /* If there is no addr in the cert at all, it can not match and we fail. */
  if (cert_addr.IsEmpty())
  {
    match = false;
  }
  else
  {
    if (signerCert)
    {
      if (from_addr && *from_addr)
      {
        NS_ConvertASCIItoUTF16 ucs2From(from_addr);
        if (NS_FAILED(signerCert->ContainsEmailAddress(ucs2From, &foundFrom)))
        {
          foundFrom = false;
        }
      }
      else if (sender_addr && *sender_addr)
      {
        NS_ConvertASCIItoUTF16 ucs2Sender(sender_addr);
        if (NS_FAILED(signerCert->ContainsEmailAddress(ucs2Sender, &foundSender)))
        {
          foundSender = false;
        }
      }
    }

    if (!foundSender && !foundFrom)
    {
      match = false;
    }
  }

  return match;
}

class nsSMimeVerificationListener : public nsISMimeVerificationListener
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISMIMEVERIFICATIONLISTENER

  nsSMimeVerificationListener(const char *aFromAddr, const char *aFromName,
                              const char *aSenderAddr, const char *aSenderName,
                              nsIMsgSMIMEHeaderSink *aHeaderSink, int32_t aMimeNestingLevel);

protected:
  virtual ~nsSMimeVerificationListener() {}

  /**
   * It is safe to declare this implementation as thread safe,
   * despite not using a lock to protect the members.
   * Because of the way the object will be used, we don't expect a race.
   * After construction, the object is passed to another thread,
   * but will no longer be accessed on the original thread.
   * The other thread is unable to access/modify self's data members.
   * When the other thread is finished, it will call into the "Notify"
   * callback. Self's members will be accessed on the other thread,
   * but this is fine, because there is no race with the original thread.
   * Race-protection for XPCOM reference counting is sufficient.
   */
  bool mSinkIsNull;
  nsMainThreadPtrHandle<nsIMsgSMIMEHeaderSink> mHeaderSink;
  int32_t mMimeNestingLevel;

  nsCString mFromAddr;
  nsCString mFromName;
  nsCString mSenderAddr;
  nsCString mSenderName;
};

class SignedStatusRunnable : public mozilla::Runnable
{
public:
  SignedStatusRunnable(const nsMainThreadPtrHandle<nsIMsgSMIMEHeaderSink> &aSink, int32_t aNestingLevel,
                       int32_t aSignatureStatus, nsIX509Cert *aSignerCert);
  NS_DECL_NSIRUNNABLE
  nsresult mResult;

protected:
  nsMainThreadPtrHandle<nsIMsgSMIMEHeaderSink> m_sink;
  int32_t m_nestingLevel;
  int32_t m_signatureStatus;
  nsCOMPtr<nsIX509Cert> m_signerCert;
};

SignedStatusRunnable::SignedStatusRunnable(const nsMainThreadPtrHandle<nsIMsgSMIMEHeaderSink> &aSink,
                                           int32_t aNestingLevel,
                                           int32_t aSignatureStatus,
                                           nsIX509Cert *aSignerCert) :
  mozilla::Runnable("SignedStatusRunnable"),
  m_sink(aSink), m_nestingLevel(aNestingLevel),
  m_signatureStatus(aSignatureStatus), m_signerCert(aSignerCert)
{
}

NS_IMETHODIMP SignedStatusRunnable::Run()
{
  mResult = m_sink->SignedStatus(m_nestingLevel, m_signatureStatus, m_signerCert);
  return NS_OK;
}


nsresult ProxySignedStatus(const nsMainThreadPtrHandle<nsIMsgSMIMEHeaderSink> &aSink,
                           int32_t aNestingLevel,
                           int32_t aSignatureStatus,
                           nsIX509Cert *aSignerCert)
{
  RefPtr<SignedStatusRunnable> signedStatus =
    new SignedStatusRunnable(aSink, aNestingLevel, aSignatureStatus, aSignerCert);
  nsresult rv = NS_DispatchToMainThread(signedStatus, NS_DISPATCH_SYNC);
  NS_ENSURE_SUCCESS(rv, rv);
  return signedStatus->mResult;
}

NS_IMPL_ISUPPORTS(nsSMimeVerificationListener, nsISMimeVerificationListener)

nsSMimeVerificationListener::nsSMimeVerificationListener(const char *aFromAddr, const char *aFromName,
                                                         const char *aSenderAddr, const char *aSenderName,
                                                         nsIMsgSMIMEHeaderSink *aHeaderSink, int32_t aMimeNestingLevel)
{
  mHeaderSink = new nsMainThreadPtrHolder<nsIMsgSMIMEHeaderSink>("nsSMimeVerificationListener::mHeaderSink", aHeaderSink);
  mSinkIsNull = !aHeaderSink;
  mMimeNestingLevel = aMimeNestingLevel;

  mFromAddr = aFromAddr;
  mFromName = aFromName;
  mSenderAddr = aSenderAddr;
  mSenderName = aSenderName;
}

NS_IMETHODIMP nsSMimeVerificationListener::Notify(nsICMSMessage2 *aVerifiedMessage,
                                                  nsresult aVerificationResultCode)
{
  // Only continue if we have a valid pointer to the UI
  NS_ENSURE_FALSE(mSinkIsNull, NS_OK);

  NS_ENSURE_TRUE(aVerifiedMessage, NS_ERROR_FAILURE);

  nsCOMPtr<nsICMSMessage> msg = do_QueryInterface(aVerifiedMessage);
  NS_ENSURE_TRUE(msg, NS_ERROR_FAILURE);

  nsCOMPtr<nsIX509Cert> signerCert;
  msg->GetSignerCert(getter_AddRefs(signerCert));

  int32_t signature_status = nsICMSMessageErrors::GENERAL_ERROR;

  if (NS_FAILED(aVerificationResultCode))
  {
    if (NS_ERROR_MODULE_SECURITY == NS_ERROR_GET_MODULE(aVerificationResultCode))
      signature_status = NS_ERROR_GET_CODE(aVerificationResultCode);
    else if (NS_ERROR_NOT_IMPLEMENTED == aVerificationResultCode)
      signature_status = nsICMSMessageErrors::VERIFY_ERROR_PROCESSING;
  }
  else
  {
    bool signing_cert_without_email_address;

    bool good_p = MimeCMSHeadersAndCertsMatch(msg, signerCert,
                                                mFromAddr.get(), mFromName.get(),
                                                mSenderAddr.get(), mSenderName.get(),
                                                &signing_cert_without_email_address);
    if (!good_p)
    {
      if (signing_cert_without_email_address)
        signature_status = nsICMSMessageErrors::VERIFY_CERT_WITHOUT_ADDRESS;
      else
        signature_status = nsICMSMessageErrors::VERIFY_HEADER_MISMATCH;
    }
    else
      signature_status = nsICMSMessageErrors::SUCCESS;
  }

  ProxySignedStatus(mHeaderSink, mMimeNestingLevel, signature_status, signerCert);

  return NS_OK;
}

namespace mozilla::mime {

int MIMEGetRelativeCryptoNestLevel(Part *obj)
{
  /*
    the part id of any mimeobj is mime_part_address(obj)
    our currently displayed crypto part is obj
    the part shown as the toplevel object in the current window is
        obj->options->part_to_load
        possibly stored in the toplevel object only ???
        but hopefully all nested mimeobject point to the same displayooptions

    we need to find out the nesting level of our currently displayed crypto object
    wrt the shown part in the toplevel window
  */

  // if we are showing the toplevel message, aTopMessageNestLevel == 0
  int aTopMessageNestLevel = 0;
  Part *aTopShownObject = nullptr;
  if (obj && obj->options->part_to_load) {
    bool aAlreadyFoundTop = false;
    for (Part *walker = obj; walker; walker = walker->parent) {
      if (aAlreadyFoundTop) {
        if (!mime_typep(walker, (PartClass *) &mimeEncryptedClass)
            && !mime_typep(walker, (PartClass *) &mimeMultipartSignedClass)) {
          ++aTopMessageNestLevel;
        }
      }
      if (!aAlreadyFoundTop && !strcmp(mime_part_address(walker), walker->options->part_to_load)) {
        aAlreadyFoundTop = true;
        aTopShownObject = walker;
      }
      if (!aAlreadyFoundTop && !walker->parent) {
        // The mime part part_to_load is not a parent of the
        // the crypto mime part passed in to this function as parameter obj.
        // That means the crypto part belongs to another branch of the mime tree.
        return -1;
      }
    }
  }

  bool CryptoObjectIsChildOfTopShownObject = false;
  if (!aTopShownObject) {
    // no sub part specified, top message is displayed, and
    // our crypto object is definitively a child of it
    CryptoObjectIsChildOfTopShownObject = true;
  }

  // if we are the child of the topmost message, aCryptoPartNestLevel == 1
  int aCryptoPartNestLevel = 0;
  if (obj) {
    for (Part *walker = obj; walker; walker = walker->parent) {
      // Crypto mime objects are transparent wrt nesting.
      if (!mime_typep(walker, (PartClass *) &mimeEncryptedClass)
          && !mime_typep(walker, (PartClass *) &mimeMultipartSignedClass)) {
        ++aCryptoPartNestLevel;
      }
      if (aTopShownObject && walker->parent == aTopShownObject) {
        CryptoObjectIsChildOfTopShownObject = true;
      }
    }
  }

  if (!CryptoObjectIsChildOfTopShownObject) {
    return -1;
  }

  return aCryptoPartNestLevel - aTopMessageNestLevel;
}

void* EncryptedCMS::CryptoInit(int (*output_fn) (const char *buf, int32_t buf_size, void *output_closure), void *output_closure)
{
  MimeCMSdata *data;
  nsresult rv;

  if (!(this->options && output_fn)) return nullptr;

  data = new MimeCMSdata;
  if (!data) return 0;

  data->self = this;
  data->output_fn = output_fn;
  date->output_closure = this;
  PR_SetError(0, 0);
  data->decoder_context = do_CreateInstance(NS_CMSDECODER_CONTRACTID, &rv);
  if (NS_FAILED(rv))
  {
    delete data;
    return 0;
  }

  rv = data->decoder_context->Start(MimeCMS_content_callback, data);
  if (NS_FAILED(rv))
  {
    delete data;
    return 0;
  }

  // XXX Fix later XXX //
  data->parent_holds_stamp_p =
  (this->parent &&
   (this->parent->IsCryptoStamped() ||
    this->parent->IsType(EncryptedClass)));

  data->parent_is_encrypted_p = this->parent && this->parent->IsEncrypted();

  /* If the parent of this object is a crypto-blob, then it's the grandparent
   who would have written out the headers and prepared for a stamp...
   (This shit sucks.)
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
            !strstr(urlSpec.get(), "&header=filter") &&
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

static int
EncryptedCMS::CryptoWrite(const char *buf, int32_t buf_size, void *closure);
{
  MimeCMSdata *data = (MimeCMSdata *) closure;
  nsresult rv;

  if (!data || !data->output_fn || !data->decoder_context) return -1;

  PR_SetError(0, 0);
  rv = data->decoder_context->Update(buf, buf_size);
  data->decoding_failed = NS_FAILED(rv);

  return 0;
}

void MimeCMSGetFromSender(Part *obj,
                          nsCString &from_addr,
                          nsCString &from_name,
                          nsCString &sender_addr,
                          nsCString &sender_name)
{
  MimeHeaders *msg_headers = 0;

  /* Find the headers of the MimeMessage which is the parent (or grandparent)
   of this object (remember, crypto objects nest.) */
  Part *o2 = obj;
  msg_headers = o2->headers;
  while (o2 &&
       o2->parent &&
       !mime_typep(o2->parent, (PartClass *) &mimeMessageClass))
    {
    o2 = o2->parent;
    msg_headers = o2->headers;
    }

  if (!msg_headers)
    return;

  /* Find the names and addresses in the From and/or Sender fields.
   */
  nsCString s;

  /* Extract the name and address of the "From:" field. */
  s.Adopt(MimeHeaders_get(msg_headers, HEADER_FROM, false, false));
  if (!s.IsEmpty())
    ExtractFirstAddress(EncodedHeader(s), from_name, from_addr);

  /* Extract the name and address of the "Sender:" field. */
  s.Adopt(MimeHeaders_get(msg_headers, HEADER_SENDER, false, false));
  if (!s.IsEmpty())
    ExtractFirstAddress(EncodedHeader(s), sender_name, sender_addr);
}

void MimeCMSRequestAsyncSignatureVerification(nsICMSMessage *aCMSMsg,
                                              const char *aFromAddr, const char *aFromName,
                                              const char *aSenderAddr, const char *aSenderName,
                                              nsIMsgSMIMEHeaderSink *aHeaderSink, int32_t aMimeNestingLevel,
                                              unsigned char* item_data, uint32_t item_len)
{
  nsCOMPtr<nsICMSMessage2> msg2 = do_QueryInterface(aCMSMsg);
  if (!msg2)
    return;

  RefPtr<nsSMimeVerificationListener> listener =
    new nsSMimeVerificationListener(aFromAddr, aFromName, aSenderAddr, aSenderName,
                                    aHeaderSink, aMimeNestingLevel);
  if (!listener)
    return;

  if (item_data)
    msg2->AsyncVerifyDetachedSignature(listener, item_data, item_len);
  else
    msg2->AsyncVerifySignature(listener);
}

int EncryptedCMS::CryptoEOF(void *crypto_closure, bool abort_p)
{
  MimeCMSdata *data = (MimeCMSdata *) crypto_closure;
  nsresult rv;
  int32_t status = nsICMSMessageErrors::SUCCESS;

  if (!data || !data->output_fn || !data->decoder_context) {
    return -1;
  }

  int aRelativeNestLevel = MIMEGetRelativeCryptoNestLevel(this);

  /* Hand an EOF to the crypto library.  It may call data->output_fn.
   (Today, the crypto library has no flushing to do, but maybe there
   will be someday.)

   We save away the value returned and will use it later to emit a
   blurb about whether the signature validation was cool.
   */

  PR_SetError(0, 0);
  rv = data->decoder_context->Finish(getter_AddRefs(data->content_info));
  if (NS_FAILED(rv))
    status = nsICMSMessageErrors::GENERAL_ERROR;

  data->decoder_context = nullptr;

  nsCOMPtr<nsIX509Cert> certOfInterest;

  if (!data->smimeHeaderSink)
    return 0;

  if (aRelativeNestLevel < 0)
    return 0;

  int32_t maxNestLevel = 0;
  data->smimeHeaderSink->MaxWantedNesting(&maxNestLevel);

  if (aRelativeNestLevel > maxNestLevel)
    return 0;

  if (data->decoding_failed)
    status = nsICMSMessageErrors::GENERAL_ERROR;

  if (!data->content_info)
  {
    if (!data->decoded_bytes)
    {
      // We were unable to decode any data.
      status = nsICMSMessageErrors::GENERAL_ERROR;
    }
    else
    {
      // Some content got decoded, but we failed to decode
      // the final summary, probably we got truncated data.
      status = nsICMSMessageErrors::ENCRYPT_INCOMPLETE;
    }

    // Although a CMS message could be either encrypted or opaquely signed,
    // what we see is most likely encrypted, because if it were
    // signed only, we probably would have been able to decode it.

    data->ci_is_encrypted = true;
  }
  else
  {
    rv = data->content_info->ContentIsEncrypted(&data->ci_is_encrypted);

    if (NS_SUCCEEDED(rv) && data->ci_is_encrypted) {
      data->content_info->GetEncryptionCert(getter_AddRefs(certOfInterest));
    }
    else {
      // Existing logic in mimei assumes, if !ci_is_encrypted, then it is signed.
      // Make sure it indeed is signed.

      bool testIsSigned;
      rv = data->content_info->ContentIsSigned(&testIsSigned);

      if (NS_FAILED(rv) || !testIsSigned) {
        // Neither signed nor encrypted?
        // We are unable to understand what we got, do not try to indicate S/Mime status.
        return 0;
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
                                               nullptr, 0);
    }
  }

  if (data->ci_is_encrypted)
  {
    data->smimeHeaderSink->EncryptionStatus(
      aRelativeNestLevel,
      status,
      certOfInterest
    );
  }

  return 0;
}

void EncryptedCMS::CryptoFree(void *crypto_closure)
{
  MimeCMSdata *data = (MimeCMSdata *) crypto_closure;
  if (!data) return;

  delete data;
}

static char *
EncryptedCMS::GenerateHTML(void *crypto_closure)
{
  return nullptr;
}

} // namespace mozilla::mime
