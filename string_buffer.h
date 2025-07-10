#ifndef STRING_BUFFER_H
#define STRING_BUFFER_H

#include <stdlib.h>

typedef struct {
    char *ptr;
    size_t length;
    size_t capacity;
} StringBuffer;

StringBuffer sb_new_buffer(void);
void sb_free(StringBuffer sb);
void sb_putc(int c, StringBuffer *sb);
void sb_puts(const char *str, StringBuffer *sb);

#endif
