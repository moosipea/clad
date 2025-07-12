#include "string_buffer.h"
#include <stdarg.h>
#include <stdio.h>

StringBuffer sb_new_buffer(void) 
{
    StringBuffer sb = {0};
    sb.capacity = 32;
    sb.ptr = calloc(sb.capacity, sizeof(char));
    return sb;
}

void sb_free(StringBuffer sb) 
{
    free(sb.ptr);
}

static void sb_grow_buffer(StringBuffer *sb) 
{
    sb->capacity *= 2;
    sb->ptr = realloc(sb->ptr, sb->capacity);
}

void sb_putc(int c, StringBuffer *sb) 
{
    if (sb->length + 1 >= sb->capacity) 
    {
        sb_grow_buffer(sb);
    }

    sb->ptr[sb->length + 0] = c;
    sb->ptr[sb->length + 1] = '\0';
    sb->length++;
}

void sb_puts(const char *str, StringBuffer *sb) 
{
    // TODO: optimize this to potentially realloc once and memcpy the string! 
    for (size_t i = 0; str[i] != '\0'; i++) 
    {
        sb_putc(str[i], sb);
    }
}

void sb_printf(StringBuffer *sb, const char *fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);

    // TODO: This should operate on the string buffer directly as that would avoid an
    // extra allocation here.
    int size = vsnprintf(NULL, 0, fmt, ap);
    char *buffer = malloc(size + 1);
    vsnprintf(buffer, size + 1, fmt, ap);
    
    sb_puts(buffer, sb);

    free(buffer);
    va_end(ap);
}

void sb_putsn(StringBuffer *sb, const char *str, size_t length) 
{
    for (size_t i = 0; i < length; i++) 
    {
        sb_putc(str[i], sb);
    }
}
