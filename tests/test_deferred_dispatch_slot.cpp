#include "deferred_dispatch_slot.h"

#include <assert.h>
#include <condition_variable>
#include <mutex>
#include <stdio.h>
#include <thread>

using Slot = DeferredDispatchSlot<4>;

static Slot* g_slot = nullptr;
static int g_runs = 0;
static int g_cleanups = 0;
static std::mutex g_run_mutex;
static std::condition_variable g_run_cv;
static bool g_running = false;
static bool g_release = false;
static std::thread g_consumer;

class DisplayDispatchWrapper {
public:
    bool init() { return slot.init(); }

    DeferredDispatchResult dispatch(DeferredDispatchExec exec, DeferredDispatchCleanup cleanup,
                                   const void* ctx, size_t ctx_len) {
        return slot.dispatch(exec, cleanup, ctx, ctx_len, 1, false, false,
                             nullptr, nullptr, 0);
    }

    void processDisplayJob() { slot.drain(); }

    bool quiesce() {
        slot.beginShutdown();
        return slot.shutdown();
    }

private:
    Slot slot;
};

static void reset_test_state() {
    test_freertos_semaphore_take_hook = nullptr;
    test_freertos_force_wait_timeout = false;
    test_freertos_successful_takes = 0;
    g_slot = nullptr;
    g_runs = 0;
    g_cleanups = 0;
    g_running = false;
    g_release = false;
}

static void run_ok(const void*, bool* ok, char* msg, size_t msg_len) {
    g_runs++;
    *ok = true;
    snprintf(msg, msg_len, "run-%d", g_runs);
}

static void run_blocking(const void*, bool* ok, char* msg, size_t msg_len) {
    {
        std::lock_guard<std::mutex> lock(g_run_mutex);
        g_running = true;
    }
    g_run_cv.notify_all();
    std::unique_lock<std::mutex> lock(g_run_mutex);
    g_run_cv.wait(lock, [] { return g_release; });
    *ok = true;
    snprintf(msg, msg_len, "released");
}

static void cleanup_count(const void*) {
    g_cleanups++;
}

static void drain_on_wait(SemaphoreHandle_t, TickType_t ticks) {
    if (ticks && g_slot) g_slot->drain();
}

static void start_blocking_consumer(SemaphoreHandle_t, TickType_t ticks) {
    if (!ticks || !g_slot || g_consumer.joinable()) return;
    g_consumer = std::thread([] { g_slot->drain(); });
    std::unique_lock<std::mutex> lock(g_run_mutex);
    g_run_cv.wait(lock, [] { return g_running; });
}

static void test_success_busy_and_limits() {
    reset_test_state();
    Slot slot;
    assert(slot.init());
    g_slot = &slot;

    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_OK);
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_BUSY);
    slot.drain();
    assert(g_cleanups == 0);

    uint8_t too_large[5] = {};
    assert(slot.enqueue(run_ok, too_large, sizeof(too_large), false) ==
           DEFERRED_DISPATCH_TOO_LARGE);
    assert(slot.dispatch(nullptr, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_INVALID);
    assert(slot.dispatch(run_ok, cleanup_count, too_large, sizeof(too_large), 1,
                         false, false, nullptr, nullptr, 0) ==
           DEFERRED_DISPATCH_TOO_LARGE);
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, true, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_INVALID);
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, true,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_INVALID);
    assert(slot.enqueue(run_ok, nullptr, 0, true) == DEFERRED_DISPATCH_INVALID);
    assert(g_cleanups == 0);

    bool ok = false;
    char msg[16] = {};
    test_freertos_semaphore_take_hook = drain_on_wait;
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         &ok, msg, sizeof(msg)) == DEFERRED_DISPATCH_OK);
    assert(ok && g_runs == 2 && g_cleanups == 0);
}

static void test_timeout_before_drain_and_cleanup() {
    reset_test_state();
    Slot slot;
    assert(slot.init());
    g_slot = &slot;

    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_TIMEOUT);
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_BUSY);
    slot.drain();
    assert(g_runs == 1 && g_cleanups == 1);

    test_freertos_semaphore_take_hook = drain_on_wait;
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_OK);
    assert(g_cleanups == 1);
}

static void test_timeout_while_running() {
    reset_test_state();
    Slot slot;
    assert(slot.init());
    g_slot = &slot;
    test_freertos_semaphore_take_hook = start_blocking_consumer;

    assert(slot.dispatch(run_blocking, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_TIMEOUT);
    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_BUSY);
    {
        std::lock_guard<std::mutex> lock(g_run_mutex);
        g_release = true;
    }
    g_run_cv.notify_all();
    g_consumer.join();
    assert(g_cleanups == 1);
}

static void test_completion_before_timed_wait_returns() {
    reset_test_state();
    Slot slot;
    assert(slot.init());
    g_slot = &slot;
    test_freertos_semaphore_take_hook = drain_on_wait;

    bool ok = false;
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         &ok, nullptr, 0) == DEFERRED_DISPATCH_OK);
    assert(ok && g_cleanups == 0);
    assert(test_freertos_successful_takes == 1);
}

static void test_completion_after_timed_expiry_consumes_token() {
    reset_test_state();
    Slot slot;
    assert(slot.init());
    g_slot = &slot;
    test_freertos_semaphore_take_hook = drain_on_wait;
    test_freertos_force_wait_timeout = true;

    bool ok = false;
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         &ok, nullptr, 0) == DEFERRED_DISPATCH_OK);
    assert(ok && g_cleanups == 0);
    // One take observes the timed expiry; this second, blocking take consumes
    // the matching completion signal before the slot is reused.
    assert(test_freertos_successful_takes == 1);

    test_freertos_semaphore_take_hook = nullptr;
    test_freertos_force_wait_timeout = false;
    // A stale completion token would make this return OK without a drain.
    assert(slot.dispatch(run_ok, cleanup_count, nullptr, 0, 1, false, false,
                         nullptr, nullptr, 0) == DEFERRED_DISPATCH_TIMEOUT);
    slot.drain();
    assert(g_cleanups == 1);
}

static void test_fire_and_forget_and_lifecycle() {
    reset_test_state();
    Slot slot;
    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_UNAVAILABLE);
    assert(slot.init());
    g_slot = &slot;
    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_OK);
    assert(!slot.shutdown());
    slot.drain();
    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_OK);
    slot.drain();
    assert(g_runs == 2 && g_cleanups == 0);
    slot.beginShutdown();
    assert(slot.shutdown());
    assert(slot.enqueue(run_ok, nullptr, 0, false) == DEFERRED_DISPATCH_UNAVAILABLE);
}

static void test_display_wrapper_quiesces_through_consumer() {
    reset_test_state();
    DisplayDispatchWrapper wrapper;
    assert(wrapper.init());
    assert(wrapper.dispatch(run_ok, cleanup_count, nullptr, 0) == DEFERRED_DISPATCH_TIMEOUT);
    assert(!wrapper.quiesce());
    wrapper.processDisplayJob();
    assert(g_runs == 1 && g_cleanups == 1);
    assert(wrapper.quiesce());
    assert(wrapper.dispatch(run_ok, cleanup_count, nullptr, 0) ==
           DEFERRED_DISPATCH_UNAVAILABLE);
}

int main() {
    test_success_busy_and_limits();
    test_timeout_before_drain_and_cleanup();
    test_timeout_while_running();
    test_completion_before_timed_wait_returns();
    test_completion_after_timed_expiry_consumes_token();
    test_fire_and_forget_and_lifecycle();
    test_display_wrapper_quiesces_through_consumer();
    puts("deferred_dispatch_slot tests passed");
    return 0;
}