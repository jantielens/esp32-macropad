#include "list_source.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "display_manager_lock.h"
#include "log_manager.h"
#include "ota_activity.h"

#include <esp_heap_caps.h>
#include <new>

struct ListSourceHandle {
    const ListSourceDescriptor* source;
    ListSourceSnapshot* current;
    ListSourceSnapshot* retired;
    uint32_t generation;
    uint32_t attempted_revision;
    uint32_t version;
    uint16_t demand;
    bool attempted;
    ListSourceState state;
    const char* error;
};

namespace {
ListSourceHandle* handles[LIST_SOURCE_MAX_SOURCES] = {};
uint8_t next_source = 0;

struct Job {
    ListSourceHandle* handle;
    ListSourceSnapshot* snapshot;
    void* data;
    uint32_t generation;
    uint32_t revision;
};
Job job = {};

class DisplayLock {
public:
    DisplayLock() { display_manager_lock_if_needed(&locked_); }
    ~DisplayLock() { display_manager_unlock_if_needed(locked_); }
private:
    bool locked_ = false;
};

void collect_unused(ListSourceHandle* handle) {
    if (handle->retired && handle->retired->references() == 1) {
        handle->retired->release();
        handle->retired = nullptr;
    }
    if (!handle->demand && handle->current && handle->current->references() == 1) {
        handle->current->release();
        handle->current = nullptr;
    }
}

bool job_is_current() {
    return job.handle && job.handle->demand && job.handle->generation == job.generation &&
        job.handle->source->revision() == job.revision;
}

void finish_job() {
    if (job.handle && job.handle->source->finish) job.handle->source->finish(job.data);
    if (job.snapshot) job.snapshot->release();
    DisplayLock lock;
    job = {};
}

void publish_failure(const char* error) {
    if (!error) error = "source load failed";
    bool initial_load = false;
    {
        DisplayLock lock;
        if (!job_is_current()) return;
        ListSourceHandle* handle = job.handle;
        initial_load = handle->state == ListSourceState::Loading;
        if (handle->current) {
            handle->retired = handle->current;
            handle->current = nullptr;
        }
        handle->state = ListSourceState::Error;
        handle->error = error;
        ++handle->version;
    }
    LOGE("ListSource", "%s: %s", job.handle->source->token, error);
    if (initial_load) display_manager_request_binding_refresh();
}
}

ListSourceHandle* list_source_acquire(const ListSourceDescriptor* source) {
    if (!source || !source->revision || !source->step || !source->finish) {
        LOGE("ListSource", "Source has no load service");
        return nullptr;
    }
    DisplayLock lock;
    for (uint8_t index = 0; index < list_source_count(); ++index) {
        if (list_source_at(index) != source) continue;
        ListSourceHandle*& handle = handles[index];
        if (!handle) {
            void* memory = heap_caps_malloc(sizeof(ListSourceHandle), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!memory) {
                LOGE("ListSource", "Cannot allocate source state in PSRAM");
                return nullptr;
            }
            handle = new (memory) ListSourceHandle{source, nullptr, nullptr, 0, 0, 0, 0,
                                                   false, ListSourceState::Loading, nullptr};
        }
        if (handle->demand == UINT16_MAX) {
            LOGE("ListSource", "Source demand limit exceeded");
            return nullptr;
        }
        if (!handle->demand) {
            ++handle->generation;
            handle->attempted = false;
            handle->state = ListSourceState::Loading;
            handle->error = nullptr;
            ++handle->version;
        }
        ++handle->demand;
        return handle;
    }
    LOGE("ListSource", "Source is not registered");
    return nullptr;
}

void list_source_release(ListSourceHandle* handle) {
    if (!handle) return;
    DisplayLock lock;
    if (!handle->demand) {
        LOGE("ListSource", "Unbalanced source release");
        return;
    }
    if (!--handle->demand) {
        ++handle->generation;
        handle->state = ListSourceState::Loading;
        handle->error = nullptr;
        collect_unused(handle);
    }
}

ListSourceState list_source_read(ListSourceHandle* handle, ListSourceSnapshot** snapshot,
                                 uint32_t* revision, const char** error) {
    DisplayLock lock;
    if (snapshot) *snapshot = nullptr;
    if (revision) *revision = 0;
    if (error) *error = nullptr;
    if (!handle || !handle->demand) {
        if (error) *error = "source is not acquired";
        return ListSourceState::Error;
    }
    if (revision) *revision = handle->version;
    if (error) *error = handle->error;
    if (snapshot && handle->state == ListSourceState::Ready && handle->current) {
        handle->current->retain();
        *snapshot = handle->current;
    }
    return handle->state;
}

void list_source_loop() {
    if (job.handle) {
        bool valid;
        {
            DisplayLock lock;
            valid = job_is_current();
        }
        if (!valid) finish_job();
    }
    {
        DisplayLock lock;
        for (uint8_t index = 0; index < list_source_count(); ++index) {
            ListSourceHandle*& handle = handles[index];
            if (!handle) continue;
            collect_unused(handle);
            if (!handle->demand && !handle->current && !handle->retired && job.handle != handle) {
                handle->~ListSourceHandle();
                heap_caps_free(handle);
                handle = nullptr;
            }
        }
    }
    if (ota_activity_is_active()) return;

    if (!job.handle) {
        DisplayLock lock;
        const uint8_t count = list_source_count();
        for (uint8_t offset = 0; offset < count; ++offset) {
            const uint8_t index = (next_source + offset) % count;
            ListSourceHandle* handle = handles[index];
            if (!handle) continue;
            collect_unused(handle);
            if (!handle->demand) continue;
            if (handle->state == ListSourceState::Loading && handle->current) {
                if (handle->current->references() != 1) continue;
                handle->current->release();
                handle->current = nullptr;
            }
            if (handle->retired) continue;
            const uint32_t revision = handle->source->revision();
            if (handle->attempted && handle->attempted_revision == revision) continue;
            handle->attempted = true;
            handle->attempted_revision = revision;
            if (!handle->current && handle->state != ListSourceState::Loading) {
                handle->state = ListSourceState::Loading;
                handle->error = nullptr;
                ++handle->version;
            }
            job = {handle, nullptr, nullptr, handle->generation, revision};
            next_source = (index + 1) % count;
            break;
        }
    }
    if (!job.handle) return;
    if (!job.snapshot) {
        job.snapshot = ListSourceSnapshot::create(job.handle->source);
        if (!job.snapshot) {
            publish_failure("snapshot allocation failed");
            finish_job();
            return;
        }
    }
    const char* error = nullptr;
    const ListSourceLoadStep result = job.handle->source->step(&job.data, job.snapshot, &error);
    if (result == ListSourceLoadStep::Pending) return;
    if (result == ListSourceLoadStep::Deferred) {
        {
            DisplayLock lock;
            if (job_is_current()) job.handle->attempted = false;
        }
        finish_job();
        return;
    }
    bool initial_load = false;
    if (result == ListSourceLoadStep::Complete) {
        error = job.snapshot->seal();
        if (!error) {
            DisplayLock lock;
            if (job_is_current()) {
                ListSourceHandle* handle = job.handle;
                initial_load = handle->state == ListSourceState::Loading;
                handle->retired = handle->current;
                handle->current = job.snapshot;
                job.snapshot = nullptr;
                handle->state = ListSourceState::Ready;
                handle->error = nullptr;
                ++handle->version;
            }
        }
    } else if (!error) {
        error = "source load failed";
    }
    if (error) publish_failure(error);
    finish_job();
    if (initial_load) display_manager_request_binding_refresh();
}

#endif
