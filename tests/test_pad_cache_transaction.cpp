#include "pad_cache_transaction.h"
#include "pad_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t eligibility_mask = 0;
static unsigned publish_count = 0;
static unsigned primary_allocation_count = 0;
static unsigned fallback_allocation_count = 0;
static unsigned load_count = 0;

static PadConfig* fail_primary_allocation() {
    primary_allocation_count++;
    return nullptr;
}

static PadConfig* fail_fallback_allocation() {
    fallback_allocation_count++;
    return nullptr;
}

static PadConfig* allocate_config() {
    primary_allocation_count++;
    return (PadConfig*)calloc(1, sizeof(PadConfig));
}

static bool load_nonempty(uint8_t, PadConfig* config) {
    load_count++;
    config->button_count = 1;
    return true;
}

static void publish_eligibility(uint8_t page, bool eligible) {
    publish_count++;
    uint32_t bit = (uint32_t)1U << page;
    if (eligible) eligibility_mask |= bit;
    else eligibility_mask &= ~bit;
}

static void test_failed_allocation_retains_existing_state() {
    PadConfig existing = {};
    PadConfig* slot = &existing;
    eligibility_mask = (uint32_t)1U << 3;
    publish_count = 0;
    primary_allocation_count = 0;
    fallback_allocation_count = 0;
    load_count = 0;

    PadCacheRefreshResult result = pad_cache_refresh(
        &slot, 3, fail_primary_allocation, fail_fallback_allocation,
        load_nonempty, publish_eligibility);

    assert(result == PadCacheRefreshResult::AllocationFailed);
    assert(slot == &existing);
    assert(eligibility_mask == ((uint32_t)1U << 3));
    assert(publish_count == 0);
    assert(primary_allocation_count == 1);
    assert(fallback_allocation_count == 1);
    assert(load_count == 0);
}

static void test_successful_replacement_commits_pointer_and_eligibility() {
    PadConfig* slot = (PadConfig*)calloc(1, sizeof(PadConfig));
    assert(slot);
    PadConfig* existing = slot;
    eligibility_mask = 0;
    publish_count = 0;
    primary_allocation_count = 0;
    fallback_allocation_count = 0;
    load_count = 0;

    PadCacheRefreshResult result = pad_cache_refresh(
        &slot, 3, allocate_config, fail_fallback_allocation,
        load_nonempty, publish_eligibility);

    assert(result == PadCacheRefreshResult::Replaced);
    assert(slot != existing);
    assert((eligibility_mask & ((uint32_t)1U << 3)) != 0);
    assert(publish_count == 1);
    assert(primary_allocation_count == 1);
    assert(fallback_allocation_count == 0);
    assert(load_count == 1);
    free(slot);
}

int main() {
    test_failed_allocation_retains_existing_state();
    test_successful_replacement_commits_pointer_and_eligibility();
    puts("pad_cache_transaction: PASS");
    return 0;
}