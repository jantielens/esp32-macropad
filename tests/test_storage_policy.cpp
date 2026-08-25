#include <assert.h>
#include <string.h>
#include <vector>

#include "storage.h"

int main() {
#if USE_SD_STORAGE
    assert(storage_boot_should_halt(false));
    assert(!storage_boot_should_halt(true));
#else
    assert(!storage_boot_should_halt(false));
    assert(!storage_boot_should_halt(true));
#endif

    std::vector<const char*> roots;
    assert(storage_remove_sd_owned_roots([&roots](const char* root) {
        roots.push_back(root);
        return true;
    }));
    const char* const expected[] = {
        "/camera", "/config", "/icons", "/sounds", "/storage", "/prints", "/brews",
    };
    assert(roots.size() == sizeof(expected) / sizeof(expected[0]));
    for (size_t index = 0; index < roots.size(); ++index) {
        assert(strcmp(roots[index], expected[index]) == 0);
    }

    roots.clear();
    assert(!storage_remove_sd_owned_roots([&roots](const char* root) {
        roots.push_back(root);
        return strcmp(root, "/sounds") != 0;
    }));
    assert(roots.size() == sizeof(expected) / sizeof(expected[0]));
    return 0;
}