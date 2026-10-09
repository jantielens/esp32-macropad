#include "../src/app/web_portal_path.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <regex>
#include <string>
#include <functional>
#include <vector>

class String : public std::string {
public:
    using std::string::string;
    String(const std::string& value) : std::string(value) {}
    String substring(size_t start) const { return substr(start); }
    String substring(size_t start, size_t end) const { return substr(start, end - start); }
    bool isEmpty() const { return empty(); }
    int indexOf(char value, size_t start) const {
        const size_t found = find(value, start);
        return found == npos ? -1 : static_cast<int>(found);
    }
};

using WebRequestMethodComposite = int;
enum { HTTP_GET = 1, HTTP_POST = 2, HTTP_DELETE = 4, HTTP_PUT = 8, HTTP_OPTIONS = 16, HTTP_PATCH = 32 };

struct AsyncWebServerRequest {
    String path;
    int verb;
    int status = 0;
    bool authenticated = true;
    void* _tempObject = nullptr;
    const String& url() const { return path; }
    int method() const { return verb; }
    void send(int code, const char*, const char*) { status = code; }
    void send(int code, const char*, const String&) { status = code; }
};

using RequestHandler = std::function<void(AsyncWebServerRequest*)>;
using BodyHandler = std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)>;

struct ComponentAction {
    const char* name;
    int method;
    RequestHandler handler;
    BodyHandler body_handler;
};

struct ComponentDef {
    RequestHandler get_config, save_config, delete_config;
    BodyHandler save_config_body;
    std::vector<ComponentAction> actions;
};

static ComponentDef test_component;
static unsigned config_calls = 0, action_calls = 0, body_calls = 0, cors_calls = 0;
static std::string body_received;

bool portal_auth_gate(AsyncWebServerRequest* request) {
    if (!request->authenticated) request->status = 401;
    return request->authenticated;
}
ComponentDef* component_registry_find(const char* name) {
    return strcmp(name, "wifi") == 0 ? &test_component : nullptr;
}
const ComponentAction* find_action(ComponentDef* component, const char* name, int method) {
    for (const auto& action : component->actions) {
        if (strcmp(action.name, name) == 0 && action.method == method) return &action;
    }
    return nullptr;
}
void web_portal_send_cors_preflight(AsyncWebServerRequest*) { ++cors_calls; }
void handlePortalNav(AsyncWebServerRequest*) {}
#define LOGI(...) ((void)0)

struct AsyncURIMatcher {
    String path;
    bool prefix_match = false;
    AsyncURIMatcher(const char* value) : path(value) {}
    static AsyncURIMatcher prefix(const char* value) {
        AsyncURIMatcher matcher(value);
        matcher.prefix_match = true;
        return matcher;
    }
    static AsyncURIMatcher exact(const char* value) { return AsyncURIMatcher(value); }
    bool matches(const String& url) const {
        if (prefix_match) return url.compare(0, path.size(), path) == 0;
        if (!path.empty() && path.back() == '*') return url.compare(0, path.size() - 1, path, 0, path.size() - 1) == 0;
        return url == path;
    }
};

struct TestRoute {
    AsyncURIMatcher matcher;
    int method;
    RequestHandler request_handler;
    BodyHandler body_handler;
    std::function<bool(AsyncWebServerRequest*)> filter;
    TestRoute& setFilter(std::function<bool(AsyncWebServerRequest*)> value) {
        filter = value;
        return *this;
    }
};

class AsyncWebServer {
public:
    std::vector<TestRoute> routes;
    TestRoute& on(AsyncURIMatcher matcher, int method, RequestHandler handler,
                 void* = nullptr, BodyHandler body = nullptr) {
        routes.push_back({matcher, method, handler, body, nullptr});
        return routes.back();
    }
    bool dispatch(AsyncWebServerRequest& request, const std::string& body = "") {
        for (auto& route : routes) {
            if (route.method != request.method() || !route.matcher.matches(request.url()) ||
                (route.filter && !route.filter(&request))) continue;
            if (route.body_handler && !body.empty()) {
                for (size_t index = 0; index < body.size(); ++index) {
                    uint8_t value = static_cast<uint8_t>(body[index]);
                    route.body_handler(&request, &value, 1, index, body.size());
                }
            }
            route.request_handler(&request);
            return true;
        }
        return false;
    }
};

#include "portal_route_handlers.inc"

static constexpr size_t FS_STORE_PATCH_MAX_BYTES = 1024;
struct FsIndexedStore {
    String last_id, last_patch;
    unsigned list_calls = 0, item_calls = 0, remove_calls = 0, patch_calls = 0;
    String list() { ++list_calls; return "{}"; }
    bool exists(const char* id) { ++item_calls; last_id = id; return last_id == "known"; }
    String data_path(const char* id) { last_id = id; return "/known.json"; }
    bool remove(const char* id) { ++remove_calls; last_id = id; return last_id == "known"; }
    bool patch_meta(const char* id, const String& patch) {
        ++patch_calls;
        last_id = id;
        last_patch = patch;
        return last_id == "known";
    }
};
struct TestStorage {
    bool exists(const char*) { return true; }
} Storage;
void sendFileThrottled(AsyncWebServerRequest* request, const char*, const char*) { request->status = 200; }

#include "portal_store_handlers.inc"

static void test_store_dispatch() {
    FsIndexedStore store;
    AsyncWebServer server;
    fs_indexed_store_register_routes(server, store, "/api/store");
    AsyncWebServerRequest request;
    request.path = "/api/store";
    request.verb = HTTP_GET;
    assert(server.dispatch(request) && store.list_calls == 1);
    request.path = "/api/store/known";
    assert(server.dispatch(request) && store.item_calls == 1 && store.list_calls == 1);
    request.verb = HTTP_DELETE;
    assert(server.dispatch(request) && store.remove_calls == 1);
    request.verb = HTTP_PATCH;
    assert(server.dispatch(request, "{\"notes\":\"test\"}"));
    assert(request.status == 200 && store.patch_calls == 1 && store.last_id == "known");
    assert(store.last_patch == "{\"notes\":\"test\"}" && !request._tempObject);
    request.authenticated = false;
    assert(server.dispatch(request, "{}"));
    assert(request.status == 401 && store.patch_calls == 1 && !request._tempObject);
    request.authenticated = true;
    request.path = "/api/store/missing";
    request.verb = HTTP_GET;
    assert(server.dispatch(request) && request.status == 404);
    for (const char* path : {"/api/store/", "/api/store/known/", "/api/store/known/extra", "/api/storeX/known"}) {
        for (int method : {HTTP_GET, HTTP_DELETE, HTTP_PATCH}) {
            request.path = path;
            request.verb = method;
            assert(!server.dispatch(request, "{}"));
        }
    }
    request.path = String("/api/store/") + std::string(128, 'a');
    request.verb = HTTP_PATCH;
    assert(server.dispatch(request, "{}") && request.status == 400 && !request._tempObject);
    request.path = "/api/store/known";
    assert(server.dispatch(request, std::string(1025, 'a')) && request.status == 413 && !request._tempObject);
}

static void test_component_dispatch() {
    auto config_handler = [](AsyncWebServerRequest*) { ++config_calls; };
    auto action_handler = [](AsyncWebServerRequest*) { ++action_calls; };
    auto body_handler = [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t, size_t) {
        ++body_calls;
        body_received.append(reinterpret_cast<char*>(data), len);
    };
    test_component.get_config = config_handler;
    test_component.save_config = config_handler;
    test_component.delete_config = config_handler;
    test_component.actions = {{"run", HTTP_POST, action_handler, nullptr},
                              {"stream", HTTP_PUT, action_handler, body_handler},
                              {"config", HTTP_PUT, action_handler, nullptr}};
    AsyncWebServer server;
    web_portal_register_component_routes(&server);
    for (int method : {HTTP_GET, HTTP_POST, HTTP_DELETE}) {
        AsyncWebServerRequest request;
        request.path = "/api/component/wifi/config";
        request.verb = method;
        assert(server.dispatch(request));
    }
    assert(config_calls == 3 && action_calls == 0);
    for (const char* path : {"/api/component/wifi/config/", "/api/component/wifi/config/extra",
                             "/api/component/Wifi/config", "/api/component//config"}) {
        AsyncWebServerRequest request;
        request.path = path;
        request.verb = HTTP_POST;
        assert(!server.dispatch(request, "payload"));
    }
    AsyncWebServerRequest request;
    request.path = "/api/component/wifi/config";
    request.verb = HTTP_POST;
    test_component.save_config_body = body_handler;
    assert(server.dispatch(request, "config-body"));
    assert(body_received == "config-body" && body_calls == 11 && config_calls == 3);
    request.authenticated = false;
    assert(server.dispatch(request, "rejected"));
    assert(request.status == 401 && body_calls == 11);
    request.verb = HTTP_GET;
    assert(server.dispatch(request));
    assert(config_calls == 3);
    request.authenticated = true;
    request.path = "/api/component/wifi/run";
    request.verb = HTTP_POST;
    assert(server.dispatch(request) && action_calls == 1);
    request.path = "/api/component/wifi/stream";
    request.verb = HTTP_PUT;
    assert(server.dispatch(request, "action-body"));
    assert(body_received == "config-bodyaction-body" && action_calls == 1);
    request.path = "/api/component/wifi/config";
    assert(server.dispatch(request) && action_calls == 2);
    request.path = "/api/component/missing/config";
    request.verb = HTTP_GET;
    assert(server.dispatch(request) && request.status == 404);
    request.verb = HTTP_OPTIONS;
    assert(server.dispatch(request) && cors_calls == 1);
    assert(portal_path_arg(String("/api/component/wifi/config"), "/api/component/", 0) == "wifi");
    assert(portal_path_arg(String("/api/sessions/id.json"), "/api/sessions/", 0) == "id.json");
}

int main() {
    const std::regex component("^/api/component/([a-z0-9-]+)/([a-z0-9-]+)$");
    const std::regex item("^/api/sessions/([^/]+)$");
    const char* paths[] = {
        "", "/api/component/wifi/config", "/api/component/foo-bar/action2",
        "/api/component/-/-", "/api/component/Wifi/config", "/api/component/wifi/",
        "/api/component//config", "/api/component/wifi/config/", "/api/component/wifi/config/extra",
        "/api/component/wifi/config?x=1", "/api/component/wifi/%63onfig",
        "/api/component/wifi_config/config", "/api/components/wifi/config",
        "/api/sessions", "/api/sessions/", "/api/sessions/abc-123",
        "/api/sessions/ABC_123.json", "/api/sessions/..", "/api/sessions/%2F",
        "/api/sessions/abc/def", "/api/sessions/abc/", "/api/sessionsX/abc",
        "/api/sessions/hello world", "/api/sessions/abc\nxyz"
    };
    for (const char* path : paths) {
        assert(portal_path_matches(path, "/api/component/", 2, true) ==
               std::regex_match(path, component));
        assert(portal_path_matches(path, "/api/sessions/", 1) ==
               std::regex_match(path, item));
    }
    assert(!portal_path_matches("/api/sessions/abc", "/api/sessions/", 0));
    test_component_dispatch();
    test_store_dispatch();
    std::puts("web_portal_path: PASS");
}