/** @file test_boot.c @brief Reset-cause and retained-marker adversarial tests.
 * Major function: main rejects cold, torn, stale and unrelated reset requests. */
#include "atlas_boot.h"
#include <assert.h>
int main(void)
{
    assert(AtlasBoot_MarkerValid(true, false, ATLAS_BOOT_MAGIC, ~ATLAS_BOOT_MAGIC));
    assert(!AtlasBoot_MarkerValid(false, false, ATLAS_BOOT_MAGIC, ~ATLAS_BOOT_MAGIC));
    assert(!AtlasBoot_MarkerValid(true, true, ATLAS_BOOT_MAGIC, ~ATLAS_BOOT_MAGIC));
    assert(!AtlasBoot_MarkerValid(true, false, 0U, ~ATLAS_BOOT_MAGIC));
    assert(!AtlasBoot_MarkerValid(true, false, ATLAS_BOOT_MAGIC, 0U));
    for (unsigned i = 0; i < 32; ++i)
        assert(!AtlasBoot_MarkerValid(true, false, ATLAS_BOOT_MAGIC ^ (1U << i), ~ATLAS_BOOT_MAGIC));
    return 0;
}
