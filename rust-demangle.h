#include <stddef.h>

/* FIXME(eddyb) clean up the naming. */

#define DMGL_VERBOSE (1 << 3)

typedef void (*demangle_callbackref) (const char *, size_t, void *);

int rust_demangle_callback (const char *mangled, int options,
                            demangle_callbackref callback, void *opaque);
char *rust_demangle (const char *mangled, int options);
