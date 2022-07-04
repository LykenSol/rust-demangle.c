#include <stddef.h>

#define RUST_DEMANGLE_FLAG_VERBOSE 1

int rust_demangle_with_callback(
    const char *mangled, int flags,
    void (*callback)(const char *data, size_t len, void *opaque), void *opaque
);
char *rust_demangle(const char *mangled, int flags);
