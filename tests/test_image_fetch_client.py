#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/image_fetch.cpp").read_text()
start = source.index("template <typename ClientType>")
end = source.index("struct SlotConn", start)
client = source[start:end]
for client_type in ("WiFiClient", "WiFiClientSecure"):
    assert f"new (std::nothrow) ImageFetchClient<{client_type}>()" in source

harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
static unsigned delay_calls;
void vTaskDelay(unsigned ticks) {
    assert(ticks == 1);
    ++delay_calls;
}
class PlainClient {
public:
    int next = -1;
    virtual ~PlainClient() = default;
    virtual int read() { return next; }
    virtual int read(uint8_t*, size_t size) { return static_cast<int>(size); }
};
class SecureClient : public PlainClient {
public:
    using PlainClient::read;
    int read() override { return next; }
};
'''
checks = r'''
template <typename ClientType>
void check_client() {
    ImageFetchClient<ClientType> client;
    PlainClient& stream = client;
    unsigned before = delay_calls;
    assert(stream.read() == -1);
    assert(delay_calls == before + 1);
    for (int byte = 0; byte <= 255; ++byte) {
        client.next = byte;
        assert(stream.read() == byte);
    }
    assert(delay_calls == before + 1);
    uint8_t buffer[4] = {};
    assert(client.read(buffer, sizeof(buffer)) == 4);
    assert(stream.read(buffer, sizeof(buffer)) == 4);
    assert(delay_calls == before + 1);
    client.next = -1;
    for (unsigned retry = 0; retry < 10; ++retry) {
        assert(stream.read() == -1);
    }
    assert(delay_calls == before + 11);
}
int main() {
    check_client<PlainClient>();
    check_client<SecureClient>();
}
'''

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    test_source = temporary / "test.cpp"
    test_source.write_text(harness + client + checks)
    executable = temporary / "test"
    subprocess.run([
        "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        str(test_source), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: plain and TLS empty reads yield; byte and bulk reads are preserved")