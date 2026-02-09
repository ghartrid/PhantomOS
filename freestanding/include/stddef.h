/*
 * PhantomOS Freestanding stddef.h
 * Standard type definitions
 */

#ifndef _STDDEF_H
#define _STDDEF_H

/* NULL pointer constant */
#ifndef NULL
#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL ((void *)0)
#endif
#endif

/* size_t - unsigned integer type for sizes */
typedef unsigned long size_t;

/* ssize_t - signed size type (POSIX, but useful) */
typedef long ssize_t;

/* ptrdiff_t - signed integer type for pointer differences */
typedef long ptrdiff_t;

/* wchar_t - wide character type */
#ifndef __cplusplus
typedef int wchar_t;
#endif

/* max_align_t - type with the maximum alignment */
typedef struct {
    long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
    long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;

/* offsetof - offset of member within structure */
#define offsetof(type, member) __builtin_offsetof(type, member)

/* Boolean type (C99) */
#ifndef __cplusplus
#ifndef bool
#define bool    _Bool
#define true    1
#define false   0
#endif
#endif

#endif /* _STDDEF_H */
