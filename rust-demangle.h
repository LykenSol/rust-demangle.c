#include <stddef.h>

/* FIXME(eddyb) clean up the naming. */

#define DMGL_VERBOSE (1 << 3)

int rust_demangle_callback (const char *mangled, int options,
                            void (*callback) (const char *data, size_t len,
                                              void *opaque),
                            void *opaque);
char *rust_demangle (const char *mangled, int options);
