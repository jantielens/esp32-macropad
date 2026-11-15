#include "voice_note_range.h"
#include <cassert>
#include <initializer_list>

int main() {
    uint64_t start = 0, end = 0;
    assert(voice_note_parse_range("bytes=2-5", 10, &start, &end) && start == 2 && end == 5);
    assert(voice_note_parse_range("bytes=2-", 10, &start, &end) && start == 2 && end == 9);
    assert(voice_note_parse_range("bytes=-3", 10, &start, &end) && start == 7 && end == 9);
    assert(voice_note_parse_range("bytes=-30", 10, &start, &end) && start == 0 && end == 9);
    assert(voice_note_parse_range("bytes=8-99", 10, &start, &end) && start == 8 && end == 9);
    for (const char* invalid : {"bytes=10-", "bytes=3-2", "bytes=-0", "bytes=0-1,4-5",
                                "bytes=18446744073709551616-", "bytes=-", "items=0-1"})
        assert(!voice_note_parse_range(invalid, 10, &start, &end));
    assert(!voice_note_parse_range("bytes=0-", 0, &start, &end));
}
