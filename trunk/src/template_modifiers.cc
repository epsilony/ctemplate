// Copyright (c) 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Craig Silverstein
//
// template_modifiers.h has a description of what each escape-routine does.
//
// When creating a new modifier, you must subclass TemplateModifier
// and define your own Modify() method.  This method takes the string
// to be modified as a char*/int pair.  It then emits the modified
// version of the string to outbuf.  Outbuf is an ExpandEmitter, as
// defined in template_modifiers.h.  It's a very simple type that
// supports appending to a data stream.
//
// Be very careful editing an existing modifier.  Subtle changes can
// introduce the possibility for cross-site scripting attacks.  If you
// do change a modifier, be careful that it does not affect
// the list of Safe XSS Alternatives.

#include "config.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <string>
#include <vector>
#include "htmlparser/htmlparser_cpp.h"
#include "template_modifiers_internal.h"
#include <google/template_modifiers.h>
#include <google/per_expand_data.h>

using std::string;
using std::vector;

using HTMLPARSER_NAMESPACE::HtmlParser;

// Really we should be using uint_16_t or something, but this is good
// enough, and more portable...
typedef unsigned int uint16;

// A most-efficient way to append a string literal to the var named 'out'.
// The ""s ensure literal is actually a string literal
#define APPEND(literal)  out->Emit("" literal "", sizeof(literal)-1)

// Check whether the string of length len is identical to the literal.
// The ""s ensure literal is actually a string literal
#define STR_IS(str, len, literal) \
  ((len) == sizeof(""literal"")-1 && \
   memcmp(str, literal, sizeof(""literal"")-1) == 0)

_START_GOOGLE_NAMESPACE_

using ctemplate::PerExpandData;

namespace template_modifiers {

TemplateModifier::~TemplateModifier() {}

void NullModifier::Modify(const char* in, size_t inlen,
                          const PerExpandData*,
                          ExpandEmitter* out, const string& arg) const {
  out->Emit(in, inlen);
}
NullModifier null_modifier;

void HtmlEscape::Modify(const char* in, size_t inlen,
                        const PerExpandData*,
                        ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '&': APPEND("&amp;"); break;
      case '"': APPEND("&quot;"); break;
      case '\'': APPEND("&#39;"); break;
      case '<': APPEND("&lt;"); break;
      case '>': APPEND("&gt;"); break;
      case '\r': case '\n': case '\v': case '\f':
      case '\t': APPEND(" "); break;     // non-space whitespace
      default: out->Emit(in[i]);
    }
  }
}
HtmlEscape html_escape;

void PreEscape::Modify(const char* in, size_t inlen,
                       const PerExpandData*,
                       ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '&': APPEND("&amp;"); break;
      case '"': APPEND("&quot;"); break;
      case '\'': APPEND("&#39;"); break;
      case '<': APPEND("&lt;"); break;
      case '>': APPEND("&gt;"); break;
      // All other whitespace we leave alone!
      default: out->Emit(in[i]);
    }
  }
}
PreEscape pre_escape;

void SnippetEscape::Modify(const char* in, size_t inlen,
                           const PerExpandData*,
                           ExpandEmitter* out, const string& arg) const {
  enum { NONE, B, I, B_THEN_I, I_THEN_B } state = NONE;
  static const char* kCloser[] =
      { "", "</b>", "</i>", "</i></b>", "</b></i>" };
  const char * const end = in + inlen;
  for (const char *c = in; c < end; ++c) {
    switch (*c) {
      case '"': {
        APPEND("&quot;");
        break;
      }
      case '\'': {
        APPEND("&#39;");
        break;
      }
      case '>': {
        APPEND("&gt;");
        break;
      }
      case '\r': case '\n': case '\v': case '\f':
      case '\t': {
        APPEND(" ");
        break;      // non-space whitespace
      }
      case '&': {
        if (c + 1 < end && c[1] == '{') {
          // Could be a javascript entity, so we need to escape.
          // (Javascript entities are an xss risk in Netscape 4.)
          APPEND("&amp;");
        } else {
          APPEND("&");
        }
        break;
      }
      case '<': {
        const char* valid_tag = NULL;
        const char* const next_char = c + 1;
        const int chars_left = end - next_char;
        if ((chars_left >= 2) && !memcmp(next_char, "b>", 2)
            && (state == NONE || state == I)) {
          state = (state == I) ? I_THEN_B : B;
          valid_tag = "<b>";
        } else if ((chars_left >= 2) && !memcmp(next_char, "i>", 2)
                   && (state == NONE || state == B)) {
          state = (state == B) ? B_THEN_I : I;
          valid_tag = "<i>";
        } else if ((chars_left >= 3) && !memcmp(next_char, "/b>", 3)
                   && (state != NONE && state != I)) {
          state = (state == B) ? NONE : I;
          valid_tag = "</b>";
        } else if ((chars_left >= 3) && !memcmp(next_char, "/i>", 3)
                   && (state != NONE && state != B)) {
          state = (state == I) ? NONE : B;
          valid_tag = "</i>";
        } else if ((chars_left >= 3) && !memcmp(next_char, "br>", 3)) {
          valid_tag = "<br>";
        } else if ((chars_left >= 4) && !memcmp(next_char, "wbr>", 4)) {
          valid_tag = "<wbr>";
        }
        if (valid_tag) {
          out->Emit(valid_tag);
          c += strlen(valid_tag) - 1;
        } else {
          APPEND("&lt;");
        }
        break;
      }
      default: {
        out->Emit(*c);
        break;
      }
    }
  }
  if (state != NONE) {
    out->Emit(kCloser[state]);
  }
}
SnippetEscape snippet_escape;

void CleanseAttribute::Modify(const char* in, size_t inlen,
                              const PerExpandData*,
                              ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    char c = in[i];
    switch (c) {
      case '=': {
        if (i == 0 || i == (inlen - 1))
          out->Emit('_');
        else
          out->Emit(c);
        break;
      }
      case '-':
      case '.':
      case '_':
      case ':': {
        out->Emit(c);
        break;
      }
      default: {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
          out->Emit(c);
        } else {
          APPEND("_");
        }
        break;
      }
    }
  }
}
CleanseAttribute cleanse_attribute;

void CleanseCss::Modify(const char* in, size_t inlen,
                              const PerExpandData*,
                              ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    char c = in[i];
    switch (c) {
      case ' ':
      case '_':
      case '.':
      case ',':
      case '!':
      case '#':
      case '%':
      case '-': {
        out->Emit(c);
        break;
      }
      default: {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
          out->Emit(c);
        }
        break;
      }
    }
  }
}
CleanseCss cleanse_css;

void ValidateUrl::Modify(const char* in, size_t inlen,
                         const PerExpandData* per_expand_data,
                         ExpandEmitter* out, const string& arg) const {
  const char* slashpos = (char*)memchr(in, '/', inlen);
  if (slashpos == NULL)
    slashpos = in + inlen;
  const void* colonpos = memchr(in, ':', slashpos - in);
  if (colonpos != NULL) {   // colon before first slash, could be a protocol
    if (inlen > sizeof("http://")-1 &&
        strncasecmp(in, "http://", sizeof("http://")-1) == 0) {
      // We're ok, it's an http protocol
    } else if (inlen > sizeof("https://")-1 &&
               strncasecmp(in, "https://", sizeof("https://")-1) == 0) {
      // https is ok as well
    } else {
      // It's a bad protocol, so return something safe
      chained_modifier_.Modify("#", 1, per_expand_data, out, "");
      return;
    }
  }
  // If we get here, it's a valid url, so just escape it
  chained_modifier_.Modify(in, inlen, per_expand_data, out, "");
}
ValidateUrl validate_url_and_html_escape(html_escape);
ValidateUrl validate_url_and_javascript_escape(javascript_escape);

void XmlEscape::Modify(const char* in, size_t inlen,
                       const PerExpandData*,
                       ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '&': APPEND("&amp;"); break;
      case '"': APPEND("&quot;"); break;
      case '\'': APPEND("&#39;"); break;
      case '<': APPEND("&lt;"); break;
      case '>': APPEND("&gt;"); break;
      default: out->Emit(in[i]);
    }
  }
}
XmlEscape xml_escape;

// Returns the UTF-8 code-unit starting at start, or the special codepoint
// 0xFFFD if the input ends abruptly or is not well-formed UTF-8.
// start -- address of the start of the code unit which also receives the
//          address past the end of the code unit returned.
// end -- exclusive end of the string
static inline uint16 UTF8CodeUnit(const char** start, const char *end) {
  size_t code_unit_len = 1;
  switch (**start & 0xF0) {
    case 0xC0: case 0xD0:  // 110x xxxx  10xx xxxx
      code_unit_len = 2;
      break;
    case 0xE0:             // 1110 xxxx  10xx xxxx  10xx xxxx
      code_unit_len = 3;
      break;
    default:
      // Return the current byte as a codepoint.
      // Either it is a valid single byte codepoint, or it's not part of a valid
      // UTF-8 sequence, and so has to be handled individually.
      char first_char = **start;
      ++*start;
      return static_cast<unsigned char>(first_char);
  }
  const char *code_unit_end = *start + code_unit_len;
  if (code_unit_end < *start || code_unit_end > end) {  // Truncated code unit.
    ++*start;
    return 0xFFFDU;
  }
  const char* pos = *start;
  uint16 code_unit = *pos & (0xFFU >> code_unit_len);
  while (--code_unit_len) {
    uint16 tail_byte = *(++pos);
    if ((tail_byte & 0xC0U) != 0x80U) {  // Malformed code unit.
      ++*start;
      return 0xFFFDU;
    }
    code_unit = (code_unit << 6) | (tail_byte & 0x3FU);
  }
  *start = code_unit_end;
  return code_unit;
}

// A good reference is the ECMA standard (3rd ed), section 7.8.4:
// http://www.ecma-international.org/publications/files/ECMA-ST/Ecma-262.pdf
void JavascriptEscape::Modify(const char* in, size_t inlen,
                              const PerExpandData*,
                              ExpandEmitter* out, const string& arg) const {
  const char* end = in + inlen;
  if (end < in) { return; }
  for (const char* p = in, *pnext = in; p != end; p = pnext) {
    uint16 code_unit = UTF8CodeUnit(&pnext, end);
    switch (code_unit) {
      case '\0': APPEND("\\x00"); break;
      case '"': APPEND("\\x22"); break;
      case '\'': APPEND("\\x27"); break;
      case '\\': APPEND("\\\\"); break;
      case '\t': APPEND("\\t"); break;
      case '\r': APPEND("\\r"); break;
      case '\n': APPEND("\\n"); break;
      case '\b': APPEND("\\b"); break;
      case '\v':
        // Do not escape vertical tabs to "\\v" since it is interpreted as 'v'
        // by JScript according to section 2.1 of
        // http://wiki.ecmascript.org/lib/exe/fetch.php?id=resources%3Aresources
        // &cache=cache&media=resources:jscriptdeviationsfromes3.pdf
        APPEND("\\x0b");
        break;
      case '\f': APPEND("\\f"); break;
      case '&': APPEND("\\x26"); break;
      case '<': APPEND("\\x3c"); break;
      case '>': APPEND("\\x3e"); break;
      case '=': APPEND("\\x3d"); break;
      // Linebreaks according to EcmaScript 262 which cannot appear in strings.
      case 0x2028: APPEND("\\u2028"); break;  // Line separator
      case 0x2029: APPEND("\\u2029"); break;  // Paragraph separator
      default:
        out->Emit(p, pnext - p);
        break;
    }
  }
}
JavascriptEscape javascript_escape;


void JavascriptNumber::Modify(const char* in, size_t inlen,
                              const PerExpandData*,
                              ExpandEmitter* out, const string& arg) const {
  if (inlen == 0)
    return;

  if (STR_IS(in, inlen, "true") || STR_IS(in, inlen, "false")) {
    out->Emit(in, inlen);
    return;
  }

  bool valid = true;
  if (in[0] == '0' && inlen > 2 && (in[1] == 'x' || in[1] == 'X')) {
    // There must be at least one hex digit after the 0x for it to be valid.
    // Hex number. Check that it is of the form 0(x|X)[0-9A-Fa-f]+
    for (size_t i = 2; i < inlen; i++) {
      char c = in[i];
      if (!((c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F') ||
            (c >= '0' && c <= '9'))) {
        valid = false;
        break;
      }
    }
  } else {
    // Must be a base-10 (or octal) number.
    // Check that it has the form [0-9+-.eE]+
    for (size_t i = 0; i < inlen; i++) {
      char c = in[i];
      if (!((c >= '0' && c <= '9') ||
            c == '+' || c == '-' || c == '.' ||
            c == 'e' || c == 'E')) {
        valid = false;
        break;
      }
    }
  }
  if (valid) {
    out->Emit(in, inlen);   // Number was valid, output it.
  } else {
    APPEND("null");         // Number was not valid, output null instead.
  }
}
JavascriptNumber javascript_number;

void UrlQueryEscape::Modify(const char* in, size_t inlen,
                            const PerExpandData*,
                            ExpandEmitter* out, const string& arg) const {
  // Everything not matching [0-9a-zA-Z.,_*/~!()-] is escaped.
  static unsigned long _safe_characters[8] = {
    0x00000000L, 0x03fff702L, 0x87fffffeL, 0x47fffffeL,
    0x00000000L, 0x00000000L, 0x00000000L, 0x00000000L
  };

  for (size_t i = 0; i < inlen; ++i) {
    unsigned char c = in[i];
    if (c == ' ') {
      out->Emit('+');
    } else if ((_safe_characters[(c)>>5] & (1 << ((c) & 31)))) {
      out->Emit(c);
    } else {
      out->Emit('%');
      out->Emit(((c>>4) < 10 ? ((c>>4) + '0') : (((c>>4) - 10) + 'A')));
      out->Emit(((c&0xf) < 10 ? ((c&0xf) + '0') : (((c&0xf) - 10) + 'A')));
    }
  }
}
UrlQueryEscape url_query_escape;

// For more information on escaping JSON, see section 2.5 in
// http://www.ietf.org/rfc/rfc4627.txt.
// Escaping '&', '<', '>' is optional in the JSON proposed RFC
// but alleviates concerns with content sniffing if JSON is used
// in a context where the browser may attempt to interpret HTML.
void JsonEscape::Modify(const char* in, size_t inlen,
                        const PerExpandData*,
                        ExpandEmitter* out, const string& arg) const {
  for (size_t i = 0; i < inlen; ++i) {
    switch (in[i]) {
      case '"': APPEND("\\\""); break;
      case '\\': APPEND("\\\\"); break;
      case '/': APPEND("\\/"); break;
      case '\b': APPEND("\\b"); break;
      case '\f': APPEND("\\f"); break;
      case '\n': APPEND("\\n"); break;
      case '\r': APPEND("\\r"); break;
      case '\t': APPEND("\\t"); break;
      case '&': APPEND("\\u0026"); break;
      case '<': APPEND("\\u003C"); break;
      case '>': APPEND("\\u003E"); break;
      default: out->Emit(in[i]);
    }
  }
}
JsonEscape json_escape;

void PrefixLine::Modify(const char* in, size_t inlen,
                        const PerExpandData*,
                        ExpandEmitter* out, const string& arg) const {
  while (inlen > 0) {
    const char* nl = (const char*)memchr(in, '\n', inlen);
    const char* cr = (const char*)memchr(in, '\r', nl ? nl - in : inlen);
    size_t linelen;
    if (nl == NULL && cr == NULL) {
      // We're at the last line
      out->Emit(in, inlen);
      break;
    } else {
      // One or both of \r and \n is set; point to the first char past
      // the newline.  Note for \r\n, that's the char after the \n,
      // otherwise, it's the char past the \r or the \n we see.
      if ((nl == NULL) != (cr == NULL))     // one is set, the other is NULL
        linelen = (nl ? nl : cr) + 1 - in;
      else if (nl == cr + 1 || nl < cr)     // \r\n, or \n comes first
        linelen = nl + 1 - in;
      else
        linelen = cr + 1 - in;
    }
    out->Emit(in, linelen);
    out->Emit(arg);               // a new line, so emit the prefix
    in += linelen;
    inlen -= linelen;
    assert(inlen >= 0);
  }
}
PrefixLine prefix_line;


// Must be at least one more than the maximum number of alternative modifiers
// specified in any given element of g_modifiers.
# define MAX_SAFE_ALTERNATIVES 10  // If the compiler complains, increase it.

// Use the empty string if you want a modifier not to have a long-name.
// Use '\0' if you want a modifier not to have a short-name.
// Note: not all modifiers are in this array:
// 1) SnippetEscape: use html_escape_with_arg=snippet to get this
// 2) CleanseAttribute: use html_escape_with_arg=attribute to get this
// 3) ValidateUrl: use html_escape_with_arg=url to get this
//
// Some modifiers define other modifiers that are safe replacements
// from an XSS perspective. Replacements are not commutative so for
// example H=pre considers H=attribute a safe replacement to it
// but H=attribute has no safe replacements.
// This struct is not pretty but allows the definitions to be
// done without the need for a global initialization method.
// Be very careful making a change to g_modifiers as modifiers
// point to other ones within that same array so elements
// may not be re-ordered easily. Also you need to change
// the global g_am_dirs correspondingly.
static struct ModifierWithAlternatives {
  ModifierInfo modifier_info;
  ModifierInfo* safe_alt_mods[MAX_SAFE_ALTERNATIVES];
} g_modifiers[] = {
  /* 0 */ { ModifierInfo("cleanse_css", 'c',
                         XSS_WEB_STANDARD, &cleanse_css), {} },
  /* 1 */ { ModifierInfo("html_escape", 'h',
                         XSS_WEB_STANDARD, &html_escape),
            {&g_modifiers[2].modifier_info,   // html_escape_with_arg=snippet
             &g_modifiers[3].modifier_info,   // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info,   // pre_escape
             &g_modifiers[9].modifier_info,   // url_query_escape
             &g_modifiers[12].modifier_info} },  // url_escape_with_arg=query

  /* 2 */ { ModifierInfo("html_escape_with_arg=snippet", 'H',
                         XSS_WEB_STANDARD, &snippet_escape),
            {&g_modifiers[1].modifier_info,   // html_escape
             &g_modifiers[3].modifier_info,   // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info,   // pre_escape
             &g_modifiers[9].modifier_info,   // url_query_escape
             &g_modifiers[12].modifier_info} },  // url_escape_with_arg=query
  /* 3 */ { ModifierInfo("html_escape_with_arg=pre", 'H',
                         XSS_WEB_STANDARD, &pre_escape),
            {&g_modifiers[1].modifier_info,   // html_escape
             &g_modifiers[2].modifier_info,   // html_escape_with_arg=snippet
             &g_modifiers[4].modifier_info,   // html_escape_with_arg=attribute
             &g_modifiers[8].modifier_info,   // pre_escape
             &g_modifiers[9].modifier_info,   // url_query_escape
             &g_modifiers[12].modifier_info} },  // url_escape_with_arg=query
  /* 4 */ { ModifierInfo("html_escape_with_arg=attribute", 'H',
                         XSS_WEB_STANDARD, &cleanse_attribute), {} },
  /* 5 */ { ModifierInfo("html_escape_with_arg=url", 'H',
                         XSS_WEB_STANDARD, &validate_url_and_html_escape), {} },
  /* 6 */ { ModifierInfo("javascript_escape", 'j',
                         XSS_WEB_STANDARD, &javascript_escape),
            {&g_modifiers[7].modifier_info} },  // json_escape
  /* 7 */ { ModifierInfo("json_escape", 'o', XSS_WEB_STANDARD, &json_escape),
            {&g_modifiers[6].modifier_info} },  // javascript_escape
  /* 8 */ { ModifierInfo("pre_escape", 'p', XSS_WEB_STANDARD, &pre_escape),
            {&g_modifiers[1].modifier_info,     // html_escape
             &g_modifiers[2].modifier_info,     // html_escape_with_arg=snippet
             &g_modifiers[3].modifier_info,     // html_escape_with_arg=pre
             &g_modifiers[4].modifier_info,     // html_escape_with_arg=attr...
             &g_modifiers[9].modifier_info,     // url_query_escape
             &g_modifiers[12].modifier_info} },   // url_escape_with_arg=query
  /* 9 */ { ModifierInfo("url_query_escape", 'u',
                         XSS_WEB_STANDARD, &url_query_escape), {} },
  /* 10 */ { ModifierInfo("url_escape_with_arg=javascript", 'U',
                          XSS_WEB_STANDARD,
                          &validate_url_and_javascript_escape), {} },
  /* 11 */ { ModifierInfo("url_escape_with_arg=html", 'U',
                          XSS_WEB_STANDARD, &validate_url_and_html_escape), {} },
  /* 12 */ { ModifierInfo("url_escape_with_arg=query", 'U',
                          XSS_WEB_STANDARD, &url_query_escape), {} },
  /* 13 */ { ModifierInfo("none", '\0', XSS_UNIQUE, &null_modifier), {} },
  /* 14 */ { ModifierInfo("xml_escape", '\0', XSS_WEB_STANDARD, &xml_escape),
             {&g_modifiers[1].modifier_info,      // html_escape
              &g_modifiers[4].modifier_info,} },  // H=attribute
  /* 15 */ { ModifierInfo("javascript_escape_with_arg=number", 'J',
                          XSS_WEB_STANDARD, &javascript_number), {} },
};

static vector<ModifierInfo> g_extension_modifiers;
static vector<ModifierInfo> g_unknown_modifiers;

// Returns whether or not candidate can be safely (w.r.t XSS)
// used in lieu of our ModifierInfo. This is true iff:
//   1. Both have the same modifier function OR
//   2. Candidate's modifier function is in our ModifierInfo's
//      list (vector) of safe alternative modifier functions.
//
// This is used with the auto-escaping code, which automatically
// figures out which modifier to apply to a variable based on the
// variable's context (in an html "<A HREF", for instance).  Some
// built-in modifiers are considered safe alternatives from the perspective
// of preventing XSS (cross-site-scripting) attacks, in which case
// the auto-escaper should allow the choice of which to use in the
// template. This is intended only for internal use as it is dangerous
// and complicated to figure out which modifier is an XSS-safe
// replacement for a given one. Custom modifiers currently may not
// indicate safe replacements, only built-in ones may do so.
//
// Note that this function is not commutative therefore
// IsSafeXSSAlternative(a, b) may not be equal to IsSafeXSSAlternative(b, a).
bool IsSafeXSSAlternative(const ModifierInfo& our,
                          const ModifierInfo& candidate) {
  // Succeeds even for non built-in modifiers but no harm.
  if (our.modifier == candidate.modifier)
    return true;

  for (const ModifierWithAlternatives* mod_with_alts = g_modifiers;
       mod_with_alts < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
       ++mod_with_alts) {
    if (mod_with_alts->modifier_info.long_name == our.long_name)
      // We found our Modifier in the built-in array g_modifiers.
      for (int i = 0; mod_with_alts->safe_alt_mods[i] != NULL &&
               i < MAX_SAFE_ALTERNATIVES; ++i)
        if (mod_with_alts->safe_alt_mods[i]->long_name == candidate.long_name)
          // We found candidate in our Modifier's list of safe alternatives.
          return true;
  }
  // our is not built-in or candidate is not a safe replacement to our.
  return false;
}

static inline bool IsExtensionModifier(const char* long_name) {
  return memcmp(long_name, "x-", 2) == 0;
}

bool AddModifier(const char* long_name,
                 const TemplateModifier* modifier) {
  if (!IsExtensionModifier(long_name))
    return false;

  for (vector<ModifierInfo>::const_iterator mod = g_extension_modifiers.begin();
       mod != g_extension_modifiers.end();
       ++mod) {
    // Check if mod has the same name as us.  For modifiers that also take
    // values, this is everything before the =.  The only time it's ok to
    // have the same name is when we have different modval specializations:
    // "foo=bar" and "foo=baz" are both valid names.  Note "foo" and
    // "foo=bar" is not valid: foo has no modval, but "foo=bar" does.
    const size_t new_modifier_namelen = strcspn(long_name, "=");
    const size_t existing_modifier_namelen = strcspn(mod->long_name.c_str(), "=");
    if (new_modifier_namelen == existing_modifier_namelen &&
        memcmp(long_name, mod->long_name.c_str(), new_modifier_namelen) == 0) {
      if (long_name[new_modifier_namelen] == '=' &&
          mod->long_name[existing_modifier_namelen] == '=' &&
          mod->long_name != long_name) {
        // It's ok, we're different specializations!
      } else {
        // It's not ok: we have the same name and no good excuse.
        return false;
      }
    }
  }

  g_extension_modifiers.push_back(ModifierInfo(long_name, '\0',
                                               XSS_UNIQUE, modifier));
  return true;
}

// If candidate_match is a better match for modname/modval than bestmatch,
// update bestmatch.  To be a better match, two conditions must be met:
//  1) The candidate's name must match modname
//  2) If the candidate is a specialization (that is, name is of the form
//     "foo=bar", then modval matches the specialization value).
//  3) If the candidate is not a specialization, bestmatch isn't a
//     specialization either.
// Condition (3) makes sure that if we match the ModifierInfo with name
// "foo=bar", we don't claim the ModifierInfo "foo=" is a better match.
// Recall that by definition, modval will always start with a '=' if present.
static void UpdateBestMatch(const char* modname, size_t modname_len,
                            const char* modval, size_t modval_len,
                            const ModifierInfo* candidate_match,
                            const ModifierInfo** best_match) {
  // It's easiest to handle the two case differently: (1) candidate_match
  // refers to a modifier that expects a modifier-value; (2) it doesn't.
  if (candidate_match->modval_required) {
    // To be a match, we have to fulfill three requirements: we have a
    // modval, our modname matches candidate_match's modname (either
    // shortname or longname), and our modval is consistent with the
    // value specified in the longname (whatever might follow the =).
    const char* const longname_start = candidate_match->long_name.c_str();
    const char* const equals = strchr(longname_start, '=');
    assert(equals != NULL);
    if (modval_len > 0 &&
        ((modname_len == 1 && *modname == candidate_match->short_name) ||
         (modname_len == equals - longname_start &&
          memcmp(modname, longname_start, modname_len) == 0)) &&
        ((equals[1] == '\0') ||  // name is "foo=" (not a specialization)
         (modval_len
          == longname_start + candidate_match->long_name.size() - equals &&
          memcmp(modval, equals, modval_len) == 0))) {
      // Condition (3) above is satisfied iff our longname is longer than
      // best-match's longname (so we prefer "foo=bar" to "foo=").
      if (*best_match == NULL ||
          candidate_match->long_name.size() > (*best_match)->long_name.size())
        *best_match = candidate_match;
    }
  } else {
    // In this case, to be a match: we must *not* have a modval.  Our
    // modname still must match modifno's modname (either short or long).
    if (modval_len == 0 &&
        ((modname_len == 1 && *modname == candidate_match->short_name) ||
         (modname_len == candidate_match->long_name.size() &&
          !memcmp(modname, candidate_match->long_name.data(), modname_len)))) {
      // In the no-modval case, only one match should exist.
      assert(*best_match == NULL);
      *best_match = candidate_match;
    }
  }
}

const ModifierInfo* FindModifier(const char* modname, size_t modname_len,
                                 const char* modval, size_t modval_len) {
  // More than one modifier can match, in the case of modval specializations
  // (e.g., the modifier "foo=" and "foo=bar" will both match on input of
  // modname="foo", modval="bar").  In that case, we take the ModifierInfo
  // with the longest longname, since that's the most specialized match.
  const ModifierInfo* best_match = NULL;
  if (modname_len >= 2 && IsExtensionModifier(modname)) {
    for (vector<ModifierInfo>::const_iterator mod = g_extension_modifiers.begin();
         mod != g_extension_modifiers.end();
         ++mod) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &*mod, &best_match);
    }
    if (best_match != NULL)
      return best_match;

    for (vector<ModifierInfo>::const_iterator mod = g_unknown_modifiers.begin();
         mod != g_unknown_modifiers.end();
         ++mod) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &*mod, &best_match);
    }
    if (best_match != NULL)
      return best_match;
    // This is the only situation where we can pass in a modifier of NULL.
    // It means "we don't know about this modifier-name."
    string fullname(modname, modname_len);
    if (modval_len) {
      fullname.append("=");
      fullname.append(modval, modval_len);
    }
    g_unknown_modifiers.push_back(ModifierInfo(fullname, '\0',
                                               XSS_UNIQUE, NULL));
    return &g_unknown_modifiers.back();
  } else {
    for (const ModifierWithAlternatives* mod_with_alts = g_modifiers;
         mod_with_alts < g_modifiers + sizeof(g_modifiers)/sizeof(*g_modifiers);
         ++mod_with_alts) {
      UpdateBestMatch(modname, modname_len, modval, modval_len,
                      &mod_with_alts->modifier_info, &best_match);
    }
    return best_match;
  }
}

// For escaping variables under the auto-escape mode:
// Each directive below maps to a distinct sequence of
// escaping directives (i.e a vector<ModifierAndValue>) applied
// to a variable during run-time substitution.
// The directives are stored in a global array (g_mods_ae)
// initialized under lock in InitializeGlobalModifiers.
enum AutoModifyDirective {
  AM_EMPTY,                         // Unused, kept as marker.
  AM_HTML,
  AM_HTML_UNQUOTED,
  AM_JS,
  AM_JS_NUMBER,
  AM_URL_HTML,
  AM_URL_QUERY,
  AM_STYLE,
  AM_XML,
  NUM_ENTRIES_AM,
};

// Populates the global vector of hard-coded modifiers that
// Auto-Escape may pick. We point to the appropriate modifier in
// the global g_modifiers.
// Reference these globals via the global array g_am_dirs[] for consistency.
// Note: We allow for more than one ModifierAndValue in the array hence
// the need to terminate with a Null marker. However currently all the
// escaping directives have exactly one ModifierAndValue.
static const ModifierAndValue g_am_empty[] = {
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_html[] = {
  ModifierAndValue(&g_modifiers[1].modifier_info, "", 0),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_html_unquoted[] = {
  ModifierAndValue(&g_modifiers[4].modifier_info, "=attribute", 10),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_js[] = {
  ModifierAndValue(&g_modifiers[6].modifier_info, "", 0),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_js_number[] = {
  ModifierAndValue(&g_modifiers[15].modifier_info, "=number", 7),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_url_html[] = {
  ModifierAndValue(&g_modifiers[11].modifier_info, "=html", 5),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_url_query[] = {
  ModifierAndValue(&g_modifiers[9].modifier_info, "", 0),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_style[] = {
  ModifierAndValue(&g_modifiers[0].modifier_info, "", 0),
  ModifierAndValue(NULL, "", 0)
};
static const ModifierAndValue g_am_xml[] = {
  ModifierAndValue(&g_modifiers[14].modifier_info, "", 0),
  ModifierAndValue(NULL, "", 0)
};

static const ModifierAndValue* g_am_dirs[NUM_ENTRIES_AM] = {
  g_am_empty,                  /* AM_EMPTY */
  g_am_html,                   /* AM_HTML */
  g_am_html_unquoted,          /* AM_HTML_UNQUOTED */
  g_am_js,                     /* AM_JS */
  g_am_js_number,              /* AM_JS_NUMBER */
  g_am_url_html,               /* AM_URL_HTML */
  g_am_url_query,              /* AM_URL_QUERY */
  g_am_style,                  /* AM_STYLE */
  g_am_xml,                    /* AM_XML */
};

string PrettyPrintOneModifier(const ModifierAndValue& modval) {
  string out;
  out.append(":");
  if (modval.modifier_info->short_name)      // short_name is a char.
    out.append(1, modval.modifier_info->short_name);
  else
    out.append(modval.modifier_info->long_name);
  if (modval.value_len != 0)
    out.append(modval.value, modval.value_len);
  return out;
}

string PrettyPrintModifiers(const vector<const ModifierAndValue*>& modvals,
                            const string& separator) {
  string out;
  for (vector<const ModifierAndValue*>::const_iterator it =
           modvals.begin(); it != modvals.end();  ++it) {
    if (it != modvals.begin())
      out.append(separator);
    out.append(PrettyPrintOneModifier(**it));
  }
  return out;
}

// Return the sequence of escaping directives to apply for the given context.
// An empty vector indicates an error occurred. Currently we never need
// to chain escaping directives hence on success, the vector is always of
// size 1. This may change in the future.
vector<const ModifierAndValue*> GetModifierForHtmlJs(
    HtmlParser* htmlparser, string* error_msg) {
  assert(htmlparser);
  assert(error_msg);
  vector<const ModifierAndValue*> modvals;

  // Two cases of being inside javascript:
  // 1. Inside raw javascript (within a <script> tag). If the value
  //    is quoted we apply javascript_escape, if not we have to coerce
  //    it to a safe value due to the risk of javascript code execution
  //    hence apply :J=number. If arbitrary code needs to be inserted
  //    at run-time, the developer must use :none.
  // 2. In the value of an attribute that takes javascript such
  //    as onmouseevent in '<a href="someUrl" onmousevent="{{EVENT}}">'.
  //    That will be covered in the STATE_VALUE state logic below.
  if (htmlparser->InJavascript() &&
      htmlparser->state() != HtmlParser::STATE_VALUE) {
    if (htmlparser->IsJavascriptQuoted()) {
      modvals.push_back(g_am_dirs[AM_JS]);
      assert(modvals.size() == 1);
      return modvals;
    } else {
      modvals.push_back(g_am_dirs[AM_JS_NUMBER]);
      assert(modvals.size() == 1);
      return modvals;
    }
  }
  switch (htmlparser->state()) {
    case HtmlParser::STATE_VALUE:{
      string attribute_name = htmlparser->attribute();
      switch (htmlparser->AttributeType()) {
        case HtmlParser::ATTR_URI:
          // Case 1: The URL is quoted:
          // . Apply :U=html if it is a complete URL or :h if it is a fragment.
          // Case 2: The URL is not quoted:
          // .  If it is a complete URL, we have no safe modifiers that
          //   won't break it so we have to fail.
          // .  If it is a URL fragment, then :u is safe and not likely to
          //   break the URL.
          if (!htmlparser->IsAttributeQuoted()) {
            if (htmlparser->ValueIndex() == 0) {   // Complete URL.
              error_msg->append("Value of URL attribute \"" + attribute_name +
                                "\" must be enclosed in quotes.");
              assert(modvals.empty());
              return modvals;  // Empty
            } else {                                // URL fragment.
              modvals.push_back(g_am_dirs[AM_URL_QUERY]);
            }
          } else {
            // Only validate the URL if we have a complete URL,
            // otherwise simply html_escape.
            if (htmlparser->ValueIndex() == 0)
              modvals.push_back(g_am_dirs[AM_URL_HTML]);
            else
              modvals.push_back(g_am_dirs[AM_HTML]);
          }
          break;
        case HtmlParser::ATTR_REGULAR:
          // If the value is quoted, simply HTML escape, otherwise
          // apply stricter escaping using H=attribute.
          if (htmlparser->IsAttributeQuoted())
            modvals.push_back(g_am_dirs[AM_HTML]);
          else
            modvals.push_back(g_am_dirs[AM_HTML_UNQUOTED]);
          break;
        case HtmlParser::ATTR_STYLE:
          // If the value is quoted apply :c, otherwise fail.
          if (htmlparser->IsAttributeQuoted()) {
            modvals.push_back(g_am_dirs[AM_STYLE]);
          } else {
            error_msg->append("Value of style attribute \"" + attribute_name +
                              "\" must be enclosed in quotes.");
            assert(modvals.empty());
            return modvals;   // Empty
          }
          break;
        case HtmlParser::ATTR_JS:
          // We require javascript accepting attributes (such as onclick)
          // to be HTML quoted, otherwise they are vulnerable to
          // HTML attribute insertion via the use of whitespace.
          if (!htmlparser->IsAttributeQuoted()) {
            error_msg->append("Value of javascript attribute \"" +
                              attribute_name +
                              "\" must be enclosed in quotes.");
            assert(modvals.empty());
            return modvals;   // Empty
          }
          // If the variable is quoted apply javascript_escape otherwise
          // apply javascript_number which will ensure it is safe against
          // code injection.
          // Note: We normally need to HTML escape after javascript escape
          // but the javascript escape implementation provided makes the
          // HTML escape redundant so simply javascript escape.
          if (htmlparser->IsJavascriptQuoted())
            modvals.push_back(g_am_dirs[AM_JS]);
          else
            modvals.push_back(g_am_dirs[AM_JS_NUMBER]);
          break;
        case HtmlParser::ATTR_NONE:
          assert("We should be in attribute!" == NULL);
        default:
          assert("Should not be able to get here." == NULL);
          return modvals;  // Empty
      }
      // In STATE_VALUE particularly, the parser may get out of sync with
      // the correct state - that the browser sees - due to the fact that
      // it does not get to parse run-time content (variables). So we tell
      // the parser there is content that will be expanded here.
      // A good example is:
      //   <a href={{URL}} alt={{NAME}}>
      // The parser sees <a href= alt=> and interprets 'alt=' to be
      // the value of href.
      htmlparser->InsertText();  // Ignore return value.
      assert(modvals.size() == 1);
      return modvals;
    }
    case HtmlParser::STATE_TAG:{
      // Apply H=attribute to tag names since they are alphabetic.
      // Examples of tag names: TITLE, BODY, A and BR.
      modvals.push_back(g_am_dirs[AM_HTML_UNQUOTED]);
      assert(modvals.size() == 1);
      return modvals;
    }
    case HtmlParser::STATE_ATTR:{
      // Apply H=attribute to attribute names since they are alphabetic.
      // Examples of attribute names: HREF, SRC and WIDTH.
      modvals.push_back(g_am_dirs[AM_HTML_UNQUOTED]);
      assert(modvals.size() == 1);
      return modvals;
    }
    case HtmlParser::STATE_COMMENT:
    case HtmlParser::STATE_TEXT:{
      // Apply :h to regular HTML text and :c if within a style tag.
      if (htmlparser->InCss())
        modvals.push_back(g_am_dirs[AM_STYLE]);
      else
        modvals.push_back(g_am_dirs[AM_HTML]);
      assert(modvals.size() == 1);
      return modvals;
    }
    default:{
      assert("Should not be able to get here." == NULL);
      return modvals;   // Empty
    }
  }
  assert("Should not be able to get here." == NULL);
  return modvals;   // Empty
}

// TODO(jad): Memoize all GetModifierForXXX functions below.
//            They don't depend on parser context (from csilvers).
vector<const ModifierAndValue*> GetModifierForCss(HtmlParser* htmlparser,
                                                  string* error_msg) {
  vector<const ModifierAndValue*> modvals;
  modvals.push_back(g_am_dirs[AM_STYLE]);
  return modvals;
}

vector<const ModifierAndValue*> GetModifierForXml(HtmlParser* htmlparser,
                                                        string* error_msg) {
  vector<const ModifierAndValue*> modvals;
  modvals.push_back(g_am_dirs[AM_XML]);
  return modvals;
}

vector<const ModifierAndValue*> GetModifierForJson(HtmlParser* htmlparser,
                                                         string* error_msg) {
  vector<const ModifierAndValue*> modvals;
  modvals.push_back(g_am_dirs[AM_JS]);
  return modvals;
}

vector<const ModifierAndValue*> GetDefaultModifierForHtml() {
  vector<const ModifierAndValue*> modvals;
  modvals.push_back(g_am_dirs[AM_HTML]);
  return modvals;
}

vector<const ModifierAndValue*> GetDefaultModifierForJs() {
  vector<const ModifierAndValue*> modvals;
  modvals.push_back(g_am_dirs[AM_JS]);
  return modvals;
}

vector<const ModifierAndValue*> GetDefaultModifierForCss() {
  return GetModifierForCss(NULL, NULL);
}

vector<const ModifierAndValue*> GetDefaultModifierForXml() {
  return GetModifierForXml(NULL, NULL);
}

vector<const ModifierAndValue*> GetDefaultModifierForJson() {
  return GetModifierForJson(NULL, NULL);
}

}  // namespace template_modifiers

_END_GOOGLE_NAMESPACE_
