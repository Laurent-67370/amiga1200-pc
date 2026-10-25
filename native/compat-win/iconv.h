/* iconv de MinGW (win-iconv) : son entrée est « const char ** », Amiberry passe un « char ** » (forme POSIX) */
#pragma once
#include_next <iconv.h>
#ifdef __cplusplus
static inline size_t amb_iconv(iconv_t cd, char **in, size_t *inl, char **out, size_t *outl) {
  return iconv(cd, (const char **)in, inl, out, outl);
}
#define iconv amb_iconv
#endif
