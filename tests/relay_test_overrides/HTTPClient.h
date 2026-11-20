#pragma once

#include <freertos/task.h>
#include <string>
#include <vector>

#define tskNO_AFFINITY (-1)
constexpr int HTTPC_ERROR_READ_TIMEOUT = -11;

inline std::vector<std::string> relay_test_requests;
inline void vTaskDelay(TickType_t) {}

class WiFiClient {
public:
    void setTimeout(unsigned) {}
    void stop() {}
};

class HTTPClient {
public:
    void setReuse(bool) {}
    void setConnectTimeout(unsigned) {}
    void setTimeout(unsigned) {}
    bool begin(WiFiClient&, const char* url) { url_ = url; return true; }
    int GET() { relay_test_requests.push_back(url_); return 200; }
    void end() {}
    std::string errorToString(int) { return "error"; }
private:
    std::string url_;
};
