#include <stdlib.h>

static inline void *memalign32(size_t size)
{
    return aligned_alloc(0x20, (size + 0x1F) & ~0x1F);
}

static inline void *memalign64(size_t size)
{
    return aligned_alloc(0x40, (size + 0x3F) & ~0x3F);
}