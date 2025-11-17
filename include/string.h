#pragma once

#include <stdint.h>

void *memset(void *dst, int c, unsigned n);
void *memmove(void *dst, const void *src, unsigned n);
void *memcpy(void *dst, const void *src, unsigned n);
unsigned strlcpy(char *dst, const char *src, int n);
int strncmp(const char *s1, const char *s2, unsigned n);
char *strncpy(char *dst, const char *src, int n);
unsigned strlen(const char *s);
