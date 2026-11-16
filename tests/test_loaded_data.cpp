#include "loaded_data.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

static bool fail_allocation = false;
void* heap_caps_malloc(size_t size, uint32_t) {
    return fail_allocation ? nullptr : std::malloc(size);
}
void heap_caps_free(void* value) { std::free(value); }
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}

int main() {
    LoadedDataSlot slot;
    assert(slot.state() == LoadedDataState::Idle && !slot.acquire());
    fail_allocation = true;
    assert(!LoadedDataSnapshot::create());
    fail_allocation = false;
    auto* first = LoadedDataSnapshot::create();
    assert(first);
    first->document()["id"] = "one";
    const uint64_t request1 = slot.begin();
    assert(slot.publish(request1, first));
    auto* retained = slot.acquire();
    assert(retained && !strcmp(retained->data()["id"], "one"));
    const uint64_t request2 = slot.begin();
    assert(slot.state() == LoadedDataState::Loading && !slot.acquire());
    assert(!slot.take_unused());
    const uint64_t request3 = slot.begin();
    assert(request2 != request3);
    assert(!slot.fail(request2, "obsolete failure"));
    auto* obsolete = LoadedDataSnapshot::create();
    assert(obsolete && !slot.publish(request2, obsolete));
    obsolete->release();
    retained->release();
    auto* retired = slot.take_unused();
    assert(retired == first);
    retired->release();
    assert(!slot.take_unused());
    assert(slot.fail(request3, "current failure"));
    assert(slot.state() == LoadedDataState::Error && !strcmp(slot.error(), "current failure"));
    const uint64_t request4 = slot.begin();
    assert(slot.state() == LoadedDataState::Loading && !slot.error());
    auto* latest = LoadedDataSnapshot::create();
    assert(latest && slot.publish(request4, latest));
    const uint64_t request5 = slot.begin();
    assert(slot.current(request5) && !slot.fail(request4, "late cancellation"));
    retired = slot.take_unused();
    assert(retired == latest);
    retired->release();
}
