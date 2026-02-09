/*
 * PhantomOS Freestanding String Library
 * "To Create, Not To Destroy"
 *
 * Basic string and memory manipulation functions
 * for use in a freestanding (no libc) environment.
 */

#include <stddef.h>
#include <stdint.h>

/*============================================================================
 * Memory Functions
 *============================================================================*/

/*
 * Fill memory with a constant byte
 */
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;

    while (n--) {
        *p++ = val;
    }

    return s;
}

/*
 * Copy memory area (non-overlapping)
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

/*
 * Copy memory area (handles overlapping regions)
 */
void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s) {
        /* Copy forward */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Copy backward to handle overlap */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }

    return dest;
}

/*
 * Compare memory areas
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

/*
 * Search for a byte in memory
 */
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char val = (unsigned char)c;

    while (n--) {
        if (*p == val) {
            return (void *)p;
        }
        p++;
    }

    return NULL;
}

/*============================================================================
 * String Functions
 *============================================================================*/

/*
 * Calculate length of a string
 */
size_t strlen(const char *s)
{
    size_t len = 0;

    while (*s++) {
        len++;
    }

    return len;
}

/*
 * Calculate length of a string with maximum limit
 */
size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;

    while (len < maxlen && *s++) {
        len++;
    }

    return len;
}

/*
 * Copy a string
 */
char *strcpy(char *dest, const char *src)
{
    char *d = dest;

    while ((*d++ = *src++))
        ;

    return dest;
}

/*
 * Copy a string with length limit
 */
char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;

    while (n && (*d++ = *src++)) {
        n--;
    }

    /* Pad with zeros if src was shorter */
    while (n--) {
        *d++ = '\0';
    }

    return dest;
}

/*
 * Concatenate strings
 */
char *strcat(char *dest, const char *src)
{
    char *d = dest;

    /* Find end of dest */
    while (*d) {
        d++;
    }

    /* Copy src */
    while ((*d++ = *src++))
        ;

    return dest;
}

/*
 * Concatenate strings with length limit
 */
char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;

    /* Find end of dest */
    while (*d) {
        d++;
    }

    /* Copy up to n characters from src */
    while (n && *src) {
        *d++ = *src++;
        n--;
    }

    *d = '\0';

    return dest;
}

/*
 * Compare two strings
 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * Compare two strings with length limit
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) {
        return 0;
    }

    while (--n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * Find first occurrence of character in string
 */
char *strchr(const char *s, int c)
{
    char ch = (char)c;

    while (*s) {
        if (*s == ch) {
            return (char *)s;
        }
        s++;
    }

    /* Check for null terminator match */
    return (ch == '\0') ? (char *)s : NULL;
}

/*
 * Find last occurrence of character in string
 */
char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    char ch = (char)c;

    while (*s) {
        if (*s == ch) {
            last = s;
        }
        s++;
    }

    /* Check for null terminator match */
    return (ch == '\0') ? (char *)s : (char *)last;
}

/*
 * Find substring in string
 */
char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) {
        return (char *)haystack;
    }

    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n && *h == *n) {
            h++;
            n++;
        }

        if (!*n) {
            return (char *)haystack;
        }

        haystack++;
    }

    return NULL;
}

/*============================================================================
 * String to Number Conversion
 *============================================================================*/

/*
 * Helper: Check if character is whitespace
 */
static int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

/*
 * Helper: Check if character is a digit
 */
static int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

/*
 * Helper: Check if character is alphabetic
 * (Currently unused but kept for future use)
 */
__attribute__((unused))
static int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/*
 * Helper: Convert character to digit value
 */
static int char_to_digit(char c, int base)
{
    int val;

    if (isdigit(c)) {
        val = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        val = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        val = c - 'A' + 10;
    } else {
        return -1;
    }

    return (val < base) ? val : -1;
}

/*
 * Convert string to unsigned long
 */
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long result = 0;
    int digit;

    /* Skip whitespace */
    while (isspace(*s)) {
        s++;
    }

    /* Handle optional sign (ignore for unsigned) */
    if (*s == '+') {
        s++;
    }

    /* Detect base from prefix if base is 0 */
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        /* Skip optional 0x prefix for hex */
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }
    }

    /* Convert digits */
    while ((digit = char_to_digit(*s, base)) >= 0) {
        result = result * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char *)s;
    }

    return result;
}

/*
 * Convert string to long
 */
long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    int negative = 0;

    /* Skip whitespace */
    while (isspace(*s)) {
        s++;
    }

    /* Handle sign */
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    unsigned long result = strtoul(s, endptr, base);

    return negative ? -(long)result : (long)result;
}
