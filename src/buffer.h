#ifndef CMARK_BUFFER_H
#define CMARK_BUFFER_H

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "cmark.h"

#ifdef __cplusplus
extern "C" {
#endif

#define bufsize_t cmark_bufsize_t

struct cmark_strbuf {
  cmark_mem *mem;
  unsigned char *ptr;
  bufsize_t asize, size;
};

extern unsigned char cmark_strbuf__initbuf[];

#define CMARK_BUF_INIT(mem)                                                    \
  { mem, cmark_strbuf__initbuf, 0, 0 }

/** Initialize a cmark_strbuf structure.
 *
 * For the cases where CMARK_BUF_INIT cannot be used to do static
 * initialization.
 */
void cmark_strbuf_init(cmark_mem *mem, cmark_strbuf *buf, cmark_bufsize_t initial_size);

static CMARK_INLINE const char *cmark_strbuf_cstr(const cmark_strbuf *buf) {
  return (char *)buf->ptr;
}

unsigned char *cmark_strbuf_detach(cmark_strbuf *buf);

void cmark_strbuf_release(cmark_strbuf *buf);

#define cmark_strbuf_at(buf, n) ((buf)->ptr[n])

#ifdef __cplusplus
}
#endif

#endif
