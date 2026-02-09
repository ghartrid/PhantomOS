/*
 * PhantomOS Freestanding stdarg.h
 * Variable argument list handling
 *
 * Uses GCC built-ins for portability
 */

#ifndef _STDARG_H
#define _STDARG_H

/* Type to hold information about variable arguments */
typedef __builtin_va_list va_list;

/* Initialize a va_list to point to the first variadic argument */
#define va_start(ap, last)  __builtin_va_start(ap, last)

/* Retrieve the next argument from the va_list */
#define va_arg(ap, type)    __builtin_va_arg(ap, type)

/* Clean up the va_list */
#define va_end(ap)          __builtin_va_end(ap)

/* Copy a va_list */
#define va_copy(dest, src)  __builtin_va_copy(dest, src)

#endif /* _STDARG_H */
