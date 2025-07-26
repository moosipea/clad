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

void sb_free(StringBuffer sb) { free(sb.ptr); }

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

void sb_putsn(StringBuffer *sb, const char *str, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        sb_putc(str[i], sb);
    }
}
