#pragma once

#include <stdint.h>

void *memset(void *dst, int c, unsigned n);
void *memmove(void *dst, const void *src, unsigned n);
void *memcpy(void *dst, const void *src, unsigned n);
unsigned strlcpy(char *dst, const char *src, int n);
int strncmp(const char *s, const char *t, unsigned n);
int strcmp(const char *s, const char *t);
