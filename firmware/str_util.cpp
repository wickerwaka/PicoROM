#include <stdint.h>
#include <stddef.h>
#include <string.h>

char *strcpyz(char *dest, size_t dest_size, const char *src)
{
	size_t n = dest_size - 1;
	strncpy(dest, src, n);
	dest[n] = '\0';

	return dest;
}

bool streq(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

// libc stroul uses errno which uses a few 100 bytes for reentrant support
// in newlib which can't afford.
uint32_t strtoul(const char *nptr)
{
    const char *p = nptr;
    uint32_t n = 0;
    int base = 0;

    if (*p == '0')
    {
        p++;
        if (base == 16 && (*p == 'X' || *p == 'x'))
        {
            p++;
        }
        else if (base == 2 && (*p == 'B' || *p == 'b'))
        {
            p++;
        }
        else if (base == 0)
        {
            if (*p == 'X' || *p == 'x')
            {
                base = 16;
                p++;
            }
            else if (*p == 'B' || *p == 'b')
            {
                base = 2;
                p++;
            }
            else
            {
                base = 8;
            }
        }
    }
    else if (base == 0)
    {
        base = 10;
    }
    while (1)
    {
        int c;
        if (*p >= 'A')
            c = ((*p - 'A') & (~('a' ^ 'A'))) + 10;
        else if (*p <= '9')
            c = *p - '0';
        else
            break;
        if (c < 0 || c >= base) break;
        p++;
        n = n * base + c;
    }
    return n;
}


