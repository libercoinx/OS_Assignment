#include "string.h"

void *memset(void *dst, int c, unsigned n) {
  unsigned char *d = (unsigned char *)dst;
  while(n--) {
    *d++ = (unsigned char)c;
  }
  return dst;
}

void *memmove(void *dst, const void *src, unsigned n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;

  if(d == s || n == 0) {
    return dst;
  }

  if(d < s) {
    while(n--) {
      *d++ = *s++;
    }
  } else {
    d += n;
    s += n;
    while(n--) {
      *--d = *--s;
    }
  }
  return dst;
}

void *memcpy(void *dst, const void *src, unsigned n) {
  return memmove(dst, src, n);
}

unsigned strlcpy(char *dst, const char *src, int n) {
  unsigned i;
  if(n <= 0)
    return 0;
  for(i = 0; i + 1 < (unsigned)n && src[i]; i++)
    dst[i] = src[i];
  dst[i] = '\0';
  while(src[i])
    i++;
  return i;
}

int strncmp(const char *s, const char *t, unsigned n) {
  while(n > 0 && *s && *s == *t) {
    s++;
    t++;
    n--;
  }
  if(n == 0)
    return 0;
  return (unsigned char)*s - (unsigned char)*t;
}

int strcmp(const char *s, const char *t) {
  while(*s && *s == *t) {
    s++;
    t++;
  }
  return (unsigned char)*s - (unsigned char)*t;
}
