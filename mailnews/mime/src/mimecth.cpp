/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mimecth.h"

/*
 * These calls are necessary to expose the object class hierarchy
 * to externally developed content type handlers.
 */
MimeInlineTextClass*
MIME_GetmimeInlineTextClass(void)
{
  return &TextClass;
}

MimeLeafClass*
MIME_GetmimeLeafClass(void)
{
  return &LeafClass;
}

PartClass*
MIME_GetmimeObjectClass(void)
{
  return &PartClass;
}

ContainerClass*
MIME_GetmimeContainerClass(void)
{
  return &ContainerClass;
}

MultipartClass*
MIME_GetmimeMultipartClass(void)
{
  return &MultipartClass;
}

MultipartSignedClass*
MIME_GetmimeMultipartSignedClass(void)
{
  return &MultipartSignedClass;
}

EncryptedClass*
MIME_GetmimeEncryptedClass(void)
{
  return &EncryptedClass;
}
