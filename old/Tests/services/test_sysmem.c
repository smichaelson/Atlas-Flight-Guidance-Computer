/**
 * @file test_sysmem.c
 * @brief Verify the production zero-heap boundary without linking an MCU image.
 * Major functions: main rejects positive, zero, negative and extreme heap changes.
 */
#include "../../Core/Src/sysmem.c"
#include <assert.h>
#include <stdio.h>

/** @brief Test every heap-change direction; no memory may be handed out.
 * @return Zero after all assertions pass. */
int main(void)
{
    const ptrdiff_t changes[] = {0, 1, -1, 4096, PTRDIFF_MAX, PTRDIFF_MIN};
    for (size_t i = 0U; i < sizeof(changes) / sizeof(changes[0]); ++i)
    {
        errno = 0;
        assert(_sbrk(changes[i]) == (void *)-1);
        assert(errno == ENOMEM);
    }
    puts("C-library heap: all growth/query/shrink requests rejected PASS");
    return 0;
}
