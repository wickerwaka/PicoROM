#if !defined(STR_UTIL_H)
#define STR_UTIL_H 1

#include <stddef.h>
#include <stdint.h>

char *strcpyz(char *dest, size_t dest_size, const char *src);
bool streq(const char *a, const char *b);
uint32_t strtoul(const char *nptr);

#endif // STR_UTIL_H
