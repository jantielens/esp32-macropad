#include "remote_log.h"
#include <cassert>
#include <cstring>
#include <string>

int main() {
    RemoteLogStore store;
    RemoteLogRecord output{};
    assert(!store.get(1, output));
    store.append("disabled");
    assert(store.range(false, 0, 32).count == 0);
    RemoteLogRecord records[3];
    store.init(records, 3);
    store.append("one");
    store.append("two");
    auto range = store.range(false, 0, 32);
    assert(range.oldest == 1 && range.newest == 2 && range.count == 2 && range.cursor == 0);
    assert(store.get(1, output) && !strcmp(output.line, "one"));
    range = store.range(true, 1, 1);
    assert(range.count == 1 && range.cursor == 1 && !range.has_more);
    store.append("three");
    store.append("four");
    assert(!store.get(1, output));
    range = store.range(true, 0, 2);
    assert(range.missed == 1 && range.oldest == 2 && range.count == 2 && range.has_more);
    assert(store.range(true, 4, 32).count == 0);
    assert(store.range(true, 100, 32).reset);
    store.append(std::string(500, 'x').c_str());
    assert(store.get(5, output) && strlen(output.line) == REMOTE_LOG_LINE_BYTES - 1);
    store.init(records, 3, UINT32_MAX - 1);
    store.append("max");
    store.append("zero");
    store.append("one");
    range = store.range(true, UINT32_MAX, 32);
    assert(range.count == 2 && range.cursor == UINT32_MAX && !range.reset);
    assert(store.get(0, output) && !strcmp(output.line, "zero"));
    store.append("two");
    range = store.range(true, UINT32_MAX - 1, 32);
    assert(range.missed == 1 && range.oldest == 0 && range.count == 3);
    RemoteLogRecord many[40];
    store.init(many, 40);
    for (size_t index = 0; index < 40; ++index) store.append("record");
    assert(store.range(false, 0, 100).count == 32);
    assert(store.range(false, 0, 0).count == 0);
}