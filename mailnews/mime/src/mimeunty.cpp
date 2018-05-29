/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mimeunty.h"
#include "prmem.h"
#include "plstr.h"
#include "prlog.h"
#include "nsMimeTypes.h"
#include "msgCore.h"
#include "nsMimeStringResources.h"
#include <ctype.h>

namespace mozilla {
namespace mime {

UntypedText::UntypedText()
  : open_subpart(nullptr)
  , type(SubpartType::Text)
  , open_hdrs(nullptr)
{
}

UntypedText::~UntypedText()
{
  if (this->open_hdrs)
  {
    /* Oops, those shouldn't still be here... */
    delete this->open_hdrs;
  }

  /* What about the open_subpart?  We're gonna have to assume that it
   is also on the MimeContainer->children list, and will get cleaned
   up by that class. */
}

int
UntypedText::ParseLine(const char* line, int32_t length)
{
  int status = 0;
  char *name = 0, *type = 0;
  bool begin_line_p = false;

  NS_ASSERTION(line && *line, "empty line in mime untyped ParseLine");
  if (!line || !*line) return -1;

  /* If we're supposed to write this object, but aren't supposed to convert
   it to HTML, simply pass it through unaltered. */
  if (this->output_p &&
    this->options &&
    !this->options->write_html_p &&
    this->options->output_fn)
  return this->Write(line, length, true);


  /* Open a new sub-part if this line demands it.
   */
  if (line[0] == 'b' &&
    LineBeginsUUE(line, length, this->options,
                    &type, &name))
  {
    /* Close the old part and open a new one. */
    status = OpenSubpart(SubpartType::UUE,
                       type, ENCODING_UUENCODE,
                       name, NULL);
    PR_FREEIF(name);
    PR_FREEIF(type);
    if (status < 0) return status;
    begin_line_p = true;
  }

  else if (line[0] == '=' &&
    LineBeginsYEnc(line, length, &type, &name))
  {
    /* Close the old part and open a new one. */
    status = OpenSubpart(SubpartType::YEnc,
                       type, ENCODING_YENCODE,
                       name, NULL);
    PR_FREEIF(name);
    PR_FREEIF(type);
    if (status < 0) return status;
    begin_line_p = true;
  }

  else if (line[0] == '(' && line[1] == 'T' &&
       LineBeginsBinhex(line, length))
  {
    /* Close the old part and open a new one. */
    status = OpenSubpart(SubpartType::Binhex,
                       APPLICATION_BINHEX, NULL,
                       NULL, NULL);
    if (status < 0) return status;
    begin_line_p = true;
  }

  /* Open a text/plain sub-part if there is no sub-part open.
   */
  if (!this->open_subpart)
  {
    // rhp: If we get here and we are being fed a line ending, we should
    // just eat it and continue and if we really get more data, we'll open
    // up the subpart then.
    //
    if (line[0] == '\r') return 0;
    if (line[0] == '\n') return 0;

    PR_ASSERT(!begin_line_p);
    status = OpenSubpart(SubpartType::Text,
                       TEXT_PLAIN, NULL, NULL, NULL);
    PR_ASSERT(this->open_subpart);
    if (!this->open_subpart) return -1;
    if (status < 0) return status;
  }

  /* Hand this line to the currently-open sub-part.
   */
  status = this->open_subpart->ParseBuffer(line, length);
  if (status < 0) return status;

  /* Close this sub-part if this line demands it.
   */
  if (begin_line_p)
  ;
  else if (line[0] == 'e' &&
       this->type == SubpartType::UUE &&
       LineEndsUUE(line, length))
  {
    status = CloseSubpart();
    if (status < 0) return status;
    NS_ASSERTION(!this->open_subpart, "no open subpart");
  }
  else if (line[0] == '=' &&
       this->type == SubpartType::YEnc &&
       LineEndsYEnc(line, length))
  {
    status = CloseSubpart();
    if (status < 0) return status;
    NS_ASSERTION(!this->open_subpart, "no open subpart");
  }
  else if (this->type == SubpartType::Binhex &&
       LineEndsBinhex(line, length))
  {
    status = CloseSubpart();
    if (status < 0) return status;
    NS_ASSERTION(!this->open_subpart, "no open subpart");
  }

  return 0;
}


int
UntypedText::CloseSubpart()
{
  int status;

  if (this->open_subpart)
  {
    status = this->open_subpart->ParseEOF(false);
    this->open_subpart = nullptr;

    PR_ASSERT(this->open_hdrs);
    if (this->open_hdrs)
    {
      delete this->open_hdrs;
      this->open_hdrs = nullptr;
    }
    this->type = SubpartType::Text;
    if (status < 0) return status;

    /* Never put out a separator between sub-parts of UntypedText.
     (This bypasses the rule that text/plain subparts always
     have separators before and after them.)
     */
    if (this->options && this->options->state)
      this->options->state->separator_suppressed_p = true;
  }

  PR_ASSERT(!this->open_hdrs);
  return 0;
}

int
UntypedText::OpenSubpart(SubpartType ttype,
                const char *type,
                const char *enc,
                const char *name,
                const char *desc)
{
  int status = 0;
  char *h = 0;

  if (!type || !*type || !PL_strcasecmp(type, UNKNOWN_CONTENT_TYPE))
  type = APPLICATION_OCTET_STREAM;
  if (enc && !*enc)
  enc = 0;
  if (desc && !*desc)
  desc = 0;
  if (name && !*name)
  name = 0;

  if (this->open_subpart)
  {
    status = CloseSubpart();
    if (status < 0) return status;
  }
  NS_ASSERTION(!this->open_subpart, "no open subpart");
  NS_ASSERTION(!this->open_hdrs, "no open headers");

  /* To make one of these implicitly-typed sub-objects, we make up a fake
   header block, containing only the minimum number of MIME headers needed.
   We could do most of this (Type and Encoding) by making a null header
   block, and simply setting this->content_type and this->encoding; but making
   a fake header block is better for two reasons: first, it means that
   something will actually be displayed when in `Show All Headers' mode;
   and second, it's the only way to communicate the filename parameter,
   aside from adding a new slot to Part (which is something to be
   avoided when possible.)
   */

  this->open_hdrs = new Headers();
  if (!this->open_hdrs) return MIME_OUT_OF_MEMORY;

  uint32_t hlen = strlen(type) +
                (enc ? strlen(enc) : 0) +
                (desc ? strlen(desc) : 0) +
                (name ? strlen(name) : 0) +
                100;
  h = (char *) PR_MALLOC(hlen);
  if (!h) return MIME_OUT_OF_MEMORY;

  PL_strncpyz(h, HEADER_CONTENT_TYPE ": ", hlen);
  PL_strcatn(h, hlen, type);
  PL_strcatn(h, hlen, MSG_LINEBREAK);
  status = this->open_hdrs->ParseLine(h, strlen(h));
  if (status < 0) goto FAIL;

  if (enc)
  {
    PL_strncpyz(h, HEADER_CONTENT_TRANSFER_ENCODING ": ", hlen);
    PL_strcatn(h, hlen, enc);
    PL_strcatn(h, hlen, MSG_LINEBREAK);
    status = this->open_hdrs->ParseLine(h, strlen(h));
    if (status < 0) goto FAIL;
  }

  if (desc)
  {
    PL_strncpyz(h, HEADER_CONTENT_DESCRIPTION ": ", hlen);
    PL_strcatn(h, hlen, desc);
    PL_strcatn(h, hlen, MSG_LINEBREAK);
    status = this->open_hdrs->ParseLine(h, strlen(h));
    if (status < 0) goto FAIL;
  }
  if (name)
  {
    PL_strncpyz(h, HEADER_CONTENT_DISPOSITION ": inline; filename=\"", hlen);
    PL_strcatn(h, hlen, name);
    PL_strcatn(h, hlen, "\"" MSG_LINEBREAK);
    status = this->open_hdrs->ParseLine(h, strlen(h));
    if (status < 0) goto FAIL;
  }

  /* push out a blank line. */
  PL_strncpyz(h, MSG_LINEBREAK, hlen);
  status = this->open_hdrs->ParseLine(h, strlen(h));
  if (status < 0) goto FAIL;


  /* Create a child... */
  {
  bool horrid_kludge = (this->options && this->options->state &&
               this->options->state->first_part_written_p);
  if (horrid_kludge)
    this->options->state->first_part_written_p = false;

  this->open_subpart = mime_create(type, this->open_hdrs, this->options);

  if (horrid_kludge)
    this->options->state->first_part_written_p = true;

  if (!this->open_subpart)
    {
    status = MIME_OUT_OF_MEMORY;
    goto FAIL;
    }
  }

  /* Add it to the list... */
  status = AddChild(this->open_subpart);
  if (status < 0)
  {
    delete this->open_subpart;
    this->open_subpart = nullptr;
    goto FAIL;
  }

  /* And start its parser going. */
  status = this->open_subpart->ParseBegin();
  if (status < 0)
  {
    /* MimeContainer->finalize will take care of shutting it down now. */
    this->open_subpart = nullptr;
    goto FAIL;
  }

  this->type = ttype;

 FAIL:
  PR_FREEIF(h);

  if (status < 0 && this->open_hdrs)
  {
    delete this->open_hdrs;
    this->open_hdrs = nullptr;
  }

  return status;
}

bool
LineBeginsUUE(const char* line, int32_t length, char** type_ret, char** name_ret)
{
  const char *s;
  char *name = 0;
  char *type = 0;

  if (type_ret) *type_ret = 0;
  if (name_ret) *name_ret = 0;

  if (strncmp (line, "begin ", 6)) return false;
  /* ...then three or four octal digits. */
  s = line + 6;
  if (*s < '0' || *s > '7') return false;
  s++;
  if (*s < '0' || *s > '7') return false;
  s++;
  if (*s < '0' || *s > '7') return false;
  s++;
  if (*s == ' ')
  s++;
  else
  {
    if (*s < '0' || *s > '7') return false;
    s++;
    if (*s != ' ') return false;
  }

  while (IS_SPACE(*s))
  s++;

  name = (char *) PR_MALLOC(((line+length)-s) + 1);
  if (!name) return false; /* grr... */
  memcpy(name, s, (line+length)-s);
  name[(line+length)-s] = 0;

  /* take off newline. */
  if (name[strlen(name)-1] == '\n') name[strlen(name)-1] = 0;
  if (name[strlen(name)-1] == '\r') name[strlen(name)-1] = 0;

  /* Now try and figure out a type.
   */
  if (this->options && this->options->file_type_fn)
  type = this->options->file_type_fn(name, this->options->stream_closure);
  else
  type = 0;

  if (name_ret)
  *name_ret = name;
  else
  PR_FREEIF(name);

  if (type_ret)
  *type_ret = type;
  else
  PR_FREEIF(type);

  return true;
}

bool
LineEndsUUE(const char *line, int32_t length)
{
#if 0
  /* A strictly conforming uuencode end line. */
  return (line[0] == 'e' &&
      line[1] == 'n' &&
      line[2] == 'd' &&
      (line[3] == 0 || IS_SPACE(line[3])));
#else
  /* ...but, why don't we accept any line that begins with the three
   letters "END" in any case: I've seen lots of partial messages
   that look like

    BEGIN----- Cut Here-----
    begin 644 foo.gif
    Mxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    Mxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    Mxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    END------- Cut Here-----

   so let's be lenient here.  (This is only for the untyped-text-plain
   case -- the uudecode parser itself is strict.)
   */
  return (line[0] == ' ' ||
      line[0] == '\t' ||
      ((line[0] == 'e' || line[0] == 'E') &&
       (line[1] == 'n' || line[1] == 'N') &&
       (line[2] == 'd' || line[2] == 'D')));
#endif
}

bool
LineBeginsYEnc(const char* line, int32_t length, char** type_ret, char** name_ret)
{
  const char *s;
  const char *endofline = line + length;
  char *name = 0;
  char *type = 0;

  if (type_ret) *type_ret = 0;
  if (name_ret) *name_ret = 0;

  /* we don't support yenc V2 neither multipart yencode,
     therefore the second parameter should always be "line="*/
  if (length < 13 || strncmp (line, "=ybegin line=", 13)) return false;

  /* ...then couple digits. */
  for (s = line + 13; s < endofline; s ++)
    if (*s < '0' || *s > '9')
      break;

  /* ...next, look for <space>size= */
  if ((endofline - s) < 6 || strncmp (s, " size=", 6)) return false;

  /* ...then couple digits. */
  for (s += 6; s < endofline; s ++)
    if (*s < '0' || *s > '9')
      break;

   /* ...next, look for <space>name= */
  if ((endofline - s) < 6 || strncmp (s, " name=", 6)) return false;

  /* anything left is the file name */
  s += 6;
  name = (char *) PR_MALLOC((endofline-s) + 1);
  if (!name) return false; /* grr... */
  memcpy(name, s, endofline-s);
  name[endofline-s] = 0;

  /* take off newline. */
  if (name[strlen(name)-1] == '\n') name[strlen(name)-1] = 0;
  if (name[strlen(name)-1] == '\r') name[strlen(name)-1] = 0;

  /* Now try and figure out a type.
   */
  if (this->options && this->options->file_type_fn)
  type = this->options->file_type_fn(name, this->options->stream_closure);
  else
  type = 0;

  if (name_ret)
  *name_ret = name;
  else
  PR_FREEIF(name);

  if (type_ret)
  *type_ret = type;
  else
  PR_FREEIF(type);

  return true;
}

bool
LineEndsYEnc(const char *line, int32_t length)
{
  if (length < 11 || strncmp (line, "=yend size=", 11)) return false;

  return true;
}


#define BINHEX_MAGIC "(This file must be converted with BinHex 4.0)"
#define BINHEX_MAGIC_LEN 45

bool
LineBeginsBinhex(const char *line, int32_t length)
{
  if (length <= BINHEX_MAGIC_LEN)
  return false;

  while(length > 0 && IS_SPACE(line[length-1]))
  length--;

  if (length != BINHEX_MAGIC_LEN)
  return false;

  if (!strncmp(line, BINHEX_MAGIC, BINHEX_MAGIC_LEN))
  return true;
  else
  return false;
}

bool
LineEndsBinhex(const char *line, int32_t length)
{
  if (length > 0 && line[length-1] == '\n') length--;
  if (length > 0 && line[length-1] == '\r') length--;

  if (length != 0 && length != 64)
  return true;
  else
  return false;
}

} // namespace mime
} // namespace mozilla
