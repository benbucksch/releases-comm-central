/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This is the definitions for the Content Type Handler plugins for
 * libmime. This will allow developers the dynamically add the ability
 * for libmime to render new content types in the MHTML rendering of
 * HTML messages.
 */

#ifndef _MIMECTH_H_
#define _MIMECTH_H_

#include "mimei.h"
#include "mimeobj.h"   //  Part (abstract)
#include "mimecont.h"  //   |--- MimeContainer (abstract)
#include "mimemult.h"  //   |     |--- MimeMultipart (abstract)
#include "mimemsig.h"  //   |     |     |--- MimeMultipartSigned (abstract)
#include "mimetext.h"  //   |     |--- MimeInlineText (abstract)
#include "mimecryp.h"  //   |     |--- Encrypted (abstract)

#include "nsIMimeContentTypeHandler.h"

/**
 * This file exposes functions that are necessary to access the
 * object hierarchy for the mime chart.
 * @see mimei.cpp for the hierarchy.
 */

/*
 * These functions are exposed by libmime to be used by content type
 * handler plugins for processing stream data.
 */
/*
 * This is the write call for outputting processed stream data.
 */
extern int MIME_Part_write(Part*, const char *data,
                           int32_t length, bool user_visible_p);
/*
 * The following group of calls expose the pointers for the object
 * system within libmime.
 */
extern TextClass             *MIME_GetmimeInlineTextClass(void);
extern LeafClass             *MIME_GetmimeLeafClass(void);
extern PartClass             *MIME_GetmimeObjectClass(void);
extern ContainerClass        *MIME_GetmimeContainerClass(void);
extern MultipartClass        *MIME_GetmimeMultipartClass(void);
extern MultipartSignedClass  *MIME_GetmimeMultipartSignedClass(void);
extern EncryptedClass        *MIME_GetmimeEncryptedClass(void);

/*
 * These are the functions that need to be implemented by the
 * content type handler plugin. They will be called by by libmime
 * when the module is loaded at runtime.
 */

/*
 * MIME_GetContentType() is called by libmime to identify the content
 * type handled by this plugin.
 */
extern "C"
char* MIME_GetContentType(void);

/*
 * This will create the PartClass object to be used by the libmime
 * object system.
 */
extern "C"
PartClass* MIME_CreateContentTypeHandlerClass(const char *content_type,
                                   contentTypeHandlerInitStruct *initStruct);

/*
 * Typedefs for libmime to use when locating and calling the above
 * defined functions.
 */
typedef char * (*mime_get_ct_fn_type)(void);
typedef PartClass* (*mime_create_class_fn_type)
                              (const char *, contentTypeHandlerInitStruct *);

#endif /* _MIMECTH_H_ */
