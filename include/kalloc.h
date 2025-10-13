#pragma once

#include <stdint.h>

void kinit(void);
void freerange(void *pa_start, void *pa_end);
void kfree(void *pa);
void *kalloc(void);

