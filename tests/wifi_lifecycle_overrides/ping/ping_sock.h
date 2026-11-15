#pragma once
#include <lwip/netif.h>
using esp_ping_handle_t = void*;
using esp_err_t = int;
constexpr int ESP_OK = 0;
struct esp_ping_config_t {
    unsigned count, timeout_ms, interval_ms, data_size, task_stack_size, task_prio;
    ip_addr_t target_addr;
};
struct esp_ping_callbacks_t {
    void (*on_ping_success)(esp_ping_handle_t, void*);
    void (*on_ping_timeout)(esp_ping_handle_t, void*);
    void (*on_ping_end)(esp_ping_handle_t, void*);
};
#define ESP_PING_DEFAULT_CONFIG() esp_ping_config_t{}
inline void esp_ping_delete_session(esp_ping_handle_t) {}
inline int esp_ping_new_session(const esp_ping_config_t*, const esp_ping_callbacks_t*, esp_ping_handle_t*) {
    return -1;
}
inline void esp_ping_start(esp_ping_handle_t) {}
