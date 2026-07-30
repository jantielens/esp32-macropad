#pragma once

#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>

#include <string.h>

typedef void (*DeferredDispatchExec)(const void* ctx, bool* ok, char* msg, size_t msg_len);
typedef void (*DeferredDispatchCleanup)(const void* ctx);

enum DeferredDispatchResult {
    DEFERRED_DISPATCH_OK = 0,
    DEFERRED_DISPATCH_BUSY,
    DEFERRED_DISPATCH_TIMEOUT,
    DEFERRED_DISPATCH_INVALID,
    DEFERRED_DISPATCH_UNAVAILABLE,
    DEFERRED_DISPATCH_TOO_LARGE,
};

// One fixed-size job slot shared by the main-loop and display dispatchers.
// The caller owns copied contexts unless it explicitly transfers payload
// ownership through cleanup, which runs only after an abandoned job completes.
template <size_t ContextBytes, size_t MessageBytes = 160>
class DeferredDispatchSlot {
public:
    DeferredDispatchSlot()
        : doneSem(nullptr), busy(false), pending(false), done(false), waiter(false),
          abandoned(false), exec(nullptr), cleanup(nullptr), ctxLen(0), ok(false) {
        msg[0] = '\0';
    }

    bool init() {
        portENTER_CRITICAL(&mux);
        if (!doneSem) doneSem = xSemaphoreCreateBinaryStatic(&doneSemStorage);
        const bool ready = doneSem != nullptr;
        initialized = ready;
        accepting = ready;
        portEXIT_CRITICAL(&mux);
        return ready;
    }

    // Reject new producers while leaving an existing consumer able to drain.
    void beginShutdown() {
        portENTER_CRITICAL(&mux);
        accepting = false;
        portEXIT_CRITICAL(&mux);
    }

    // A busy slot cannot be safely torn down because its consumer owns the
    // completion and any abandoned cleanup. Call beginShutdown() first.
    bool shutdown() {
        portENTER_CRITICAL(&mux);
        if (accepting || busy) {
            portEXIT_CRITICAL(&mux);
            return false;
        }
        initialized = false;
        SemaphoreHandle_t sem = doneSem;
        portEXIT_CRITICAL(&mux);
        if (sem) xSemaphoreTake(sem, 0);
        return true;
    }

    DeferredDispatchResult enqueue(DeferredDispatchExec job,
                                   const void* context, size_t contextLen,
                                   bool fromIsr) {
        return publish(job, nullptr, context, contextLen, false, fromIsr);
    }

    DeferredDispatchResult dispatch(DeferredDispatchExec job,
                                    DeferredDispatchCleanup abandonedCleanup,
                                    const void* context, size_t contextLen,
                                    uint32_t timeoutMs,
                                    bool callerIsConsumer, bool fromIsr,
                                    bool* outOk, char* outMsg, size_t outMsgLen) {
        if (callerIsConsumer || fromIsr) return DEFERRED_DISPATCH_INVALID;

        const DeferredDispatchResult published = publish(
            job, abandonedCleanup, context, contextLen, true, false);
        if (published != DEFERRED_DISPATCH_OK) return published;

        if (xSemaphoreTake(doneSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
            return collectCompleted(outOk, outMsg, outMsgLen);
        }

        // Completion can race with wait expiry. Keep the slot reserved until
        // the matching give has been consumed before allowing another job.
        portENTER_CRITICAL(&mux);
        const bool completed = done;
        if (!completed) {
            waiter = false;
            abandoned = true;
        }
        portEXIT_CRITICAL(&mux);
        if (completed) {
            xSemaphoreTake(doneSem, portMAX_DELAY);
            return collectCompleted(outOk, outMsg, outMsgLen);
        }
        return DEFERRED_DISPATCH_TIMEOUT;
    }

    // Called exclusively by the owning consumer task.
    void drain() {
        DeferredDispatchExec job = nullptr;
        portENTER_CRITICAL(&mux);
        if (initialized && pending) {
            pending = false;
            job = exec;
        }
        portEXIT_CRITICAL(&mux);
        if (!job) return;

        bool jobOk = false;
        char jobMsg[MessageBytes] = {0};
        job(ctx, &jobOk, jobMsg, sizeof(jobMsg));

        DeferredDispatchCleanup abandonedCleanup = nullptr;
        bool notify = false;
        portENTER_CRITICAL(&mux);
        ok = jobOk;
        copyMessage(msg, sizeof(msg), jobMsg);
        done = true;
        notify = waiter;
        if (!notify && abandoned) abandonedCleanup = cleanup;
        portEXIT_CRITICAL(&mux);

        if (notify) {
            xSemaphoreGive(doneSem);
            return;
        }

        // Cleanup is intentionally on the consumer task and outside the lock.
        if (abandonedCleanup) abandonedCleanup(ctx);
        releaseCompleted();
    }

private:
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    StaticSemaphore_t doneSemStorage;
    SemaphoreHandle_t doneSem;
    bool initialized = false;
    bool accepting = false;
    bool busy;
    bool pending;
    bool done;
    bool waiter;
    bool abandoned;
    DeferredDispatchExec exec;
    DeferredDispatchCleanup cleanup;
    uint8_t ctx[ContextBytes];
    size_t ctxLen;
    bool ok;
    char msg[MessageBytes];

    static void copyMessage(char* destination, size_t destinationLen,
                            const char* source) {
        if (!destinationLen) return;
        const size_t sourceLen = source ? strlen(source) : 0;
        const size_t count = sourceLen < destinationLen - 1 ? sourceLen : destinationLen - 1;
        if (count) memcpy(destination, source, count);
        destination[count] = '\0';
    }

    DeferredDispatchResult publish(DeferredDispatchExec job,
                                   DeferredDispatchCleanup abandonedCleanup,
                                   const void* context, size_t contextLen,
                                   bool needsWaiter, bool fromIsr) {
        if (fromIsr || !job || (contextLen && !context)) return DEFERRED_DISPATCH_INVALID;
        if (contextLen > ContextBytes) return DEFERRED_DISPATCH_TOO_LARGE;

        portENTER_CRITICAL(&mux);
        if (!initialized || !accepting || !doneSem) {
            portEXIT_CRITICAL(&mux);
            return DEFERRED_DISPATCH_UNAVAILABLE;
        }
        if (busy) {
            portEXIT_CRITICAL(&mux);
            return DEFERRED_DISPATCH_BUSY;
        }
        busy = true;
        pending = false;
        done = false;
        waiter = needsWaiter;
        abandoned = false;
        exec = job;
        cleanup = abandonedCleanup;
        ctxLen = contextLen;
        if (contextLen) memcpy(ctx, context, contextLen);
        ok = false;
        msg[0] = '\0';
        portEXIT_CRITICAL(&mux);

        // No consumer can complete while pending is false, so this drains only
        // the previous request's signal before making the new request visible.
        xSemaphoreTake(doneSem, 0);

        portENTER_CRITICAL(&mux);
        pending = true;
        portEXIT_CRITICAL(&mux);
        return DEFERRED_DISPATCH_OK;
    }

    DeferredDispatchResult collectCompleted(bool* outOk, char* outMsg,
                                            size_t outMsgLen) {
        portENTER_CRITICAL(&mux);
        if (outOk) *outOk = ok;
        if (outMsg && outMsgLen) copyMessage(outMsg, outMsgLen, msg);
        busy = false;
        waiter = false;
        abandoned = false;
        cleanup = nullptr;
        exec = nullptr;
        portEXIT_CRITICAL(&mux);
        return DEFERRED_DISPATCH_OK;
    }

    void releaseCompleted() {
        portENTER_CRITICAL(&mux);
        busy = false;
        waiter = false;
        abandoned = false;
        cleanup = nullptr;
        exec = nullptr;
        portEXIT_CRITICAL(&mux);
    }
};