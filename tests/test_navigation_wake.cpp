#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

#undef HAS_DISPLAY
#define HAS_DISPLAY 1
#define DISPLAY_MANAGER_H
#define SCREEN_SAVER_MANAGER_H

using portMUX_TYPE = int;
portMUX_TYPE g_splash_status_mux = 0;
#define portENTER_CRITICAL(mutex) ((void)(mutex))
#define portEXIT_CRITICAL(mutex) ((void)(mutex))
static unsigned wake_requests = 0;
void screen_saver_manager_notify_activity(bool wake) {
    assert(wake);
    ++wake_requests;
}

static uint32_t eligible_mask = 7;
uint32_t pad_config_get_eligible_mask() { return eligible_mask; }

struct Screen {
    void show() {}
    void hide() {}
    void setStatus(const char*) {}
    const char* wakeScreenId() { return nullptr; }
};
struct ScreenInfo { const char* id; Screen* instance; };

using TaskHandle_t = uintptr_t;
static TaskHandle_t current_task = 1;
struct TestMutex { TaskHandle_t owner = 0; };
static TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
static TaskHandle_t xSemaphoreGetMutexHolder(TestMutex* mutex) { return mutex->owner; }

class DisplayManager {
public:
    Screen splashScreen, infoScreen, testScreen, firstPad, secondPad, idlePad;
    Screen* padScreens[MAX_PADS] = {&firstPad, &secondPad, &idlePad};
    ScreenInfo availableScreens[3] = {{"pad_0", &firstPad}, {"pad_1", &secondPad}, {"pad_2", &idlePad}};
    size_t screenCount = 3;
    Screen* currentScreen = &firstPad;
    Screen* pendingScreen = nullptr;
    Screen* screenHistory[8] = {};
    size_t screenHistoryCount = 0;
    bool skipHistoryPush = false;
    Screen* transientResumeScreen = nullptr;
    bool transientScreenActive = false;
    Screen* pendingHistoryScreen = nullptr;
    TaskHandle_t lvglTaskHandle = 0;
    TestMutex mutex;
    TestMutex* lvglMutex = &mutex;
    char pendingSplashStatus[96] = {};
    bool pendingSplashStatusSet = false;
    unsigned locks = 0;
    unsigned acquisitions = 0;
    bool release_other_owner_on_wait = false;
    void lock() {
        if (!lvglMutex) return;
        assert(!mutex.owner || (release_other_owner_on_wait && mutex.owner != current_task));
        mutex.owner = current_task;
        ++locks;
        ++acquisitions;
    }
    void unlock() {
        if (!lvglMutex) return;
        assert(locks && mutex.owner == current_task);
        mutex.owner = 0;
        --locks;
    }
    bool isInLvglTask() const { return lvglTaskHandle && xTaskGetCurrentTaskHandle() == lvglTaskHandle; }
    void lockIfNeeded(bool& didLock);
    void unlockIfNeeded(bool didLock);
    void showSplash();
    void showInfo();
    void showTest();
    void setSplashStatus(const char* text);
    void prepareNavigationWake();
    bool showScreen(const char* id, bool wake = false);
    bool showTransientScreen(const char* id);
    bool restoreTransientScreen();
    bool goBack(bool wake = false);
    bool cyclePad(int8_t direction, bool wrap, uint32_t excludedMask, bool wake = false);
    void handleSleepScreenRedirect();
    const char* getCurrentScreenId();
    const ScreenInfo* getAvailableScreens(size_t* count);
    void enterIdle() {
        assert(showTransientScreen("pad_2"));
        currentScreen = pendingScreen;
        pendingScreen = nullptr;
        skipHistoryPush = false;
    }
};

#include "navigation_lock_helpers.inc"
#include "../src/app/display_screen_nav.cpp"

int main() {
    {
        DisplayManager manager;
        manager.lock();
        assert(xSemaphoreGetMutexHolder(manager.lvglMutex) == xTaskGetCurrentTaskHandle());
        bool nested_lock = true;
        manager.lockIfNeeded(nested_lock);
        assert(!nested_lock);
        manager.unlockIfNeeded(nested_lock);
        assert(manager.mutex.owner == current_task && manager.locks == 1);
        assert(manager.showScreen("pad_1", true));
        manager.screenHistory[manager.screenHistoryCount++] = &manager.firstPad;
        assert(manager.goBack(true));
        assert(manager.cyclePad(1, true, 0, true));
        assert(!manager.showScreen("missing", true));
        assert(!manager.restoreTransientScreen());
        assert(manager.acquisitions == 1 && manager.locks == 1);
        manager.unlock();
        assert(manager.mutex.owner == 0 && manager.locks == 0);
    }
    {
        DisplayManager manager;
        manager.enterIdle();
        manager.lock();
        assert(manager.showScreen("pad_1", true));
        assert(!manager.restoreTransientScreen());
        assert(manager.pendingHistoryScreen == &manager.firstPad);
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.locks == 1);
        manager.unlock();
    }
    {
        DisplayManager manager;
        manager.mutex.owner = 2;
        manager.release_other_owner_on_wait = true;
        bool did_lock = false;
        manager.lockIfNeeded(did_lock);
        assert(did_lock && manager.acquisitions == 1);
        assert(manager.mutex.owner == current_task);
        manager.unlockIfNeeded(did_lock);
        assert(manager.mutex.owner == 0);
    }
    {
        DisplayManager manager;
        manager.lvglTaskHandle = current_task;
        manager.lock();
        bool did_lock = true;
        manager.lockIfNeeded(did_lock);
        assert(!did_lock && manager.acquisitions == 1);
        manager.unlockIfNeeded(did_lock);
        assert(manager.locks == 1);
        manager.unlock();
        manager.lvglMutex = nullptr;
        manager.lvglTaskHandle = 0;
        did_lock = false;
        manager.lockIfNeeded(did_lock);
        manager.unlockIfNeeded(did_lock);
        assert(manager.locks == 0);
    }
    wake_requests = 0;
    {
        DisplayManager manager;
        assert(manager.showScreen("pad_1"));
        assert(wake_requests == 0);
        assert(!manager.showScreen("missing", true));
        assert(!manager.showScreen(nullptr, true));
        assert(!manager.goBack(true));
        eligible_mask = 0;
        assert(!manager.cyclePad(1, true, 0, true));
        eligible_mask = 7;
        assert(wake_requests == 0);
        assert(manager.showScreen("pad_1", true));
        assert(wake_requests == 1);
        assert(manager.pendingScreen == &manager.secondPad);
        assert(!manager.showTransientScreen("pad_2"));
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.locks == 0);
    }
    {
        DisplayManager manager;
        manager.enterIdle();
        assert(manager.showScreen("pad_1", true));
        assert(!manager.restoreTransientScreen());
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.pendingHistoryScreen == &manager.firstPad);
        assert(!manager.skipHistoryPush);
        assert(manager.locks == 0);
    }
    {
        DisplayManager manager;
        manager.enterIdle();
        assert(manager.restoreTransientScreen());
        assert(manager.showScreen("pad_1", true));
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.pendingHistoryScreen == &manager.firstPad);
        assert(!manager.skipHistoryPush);
    }
    {
        DisplayManager manager;
        manager.enterIdle();
        assert(manager.cyclePad(1, true, 0, true));
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.pendingHistoryScreen == &manager.firstPad);
        assert(!manager.restoreTransientScreen());
        assert(!manager.skipHistoryPush);
    }
    {
        DisplayManager manager;
        manager.screenHistory[manager.screenHistoryCount++] = &manager.secondPad;
        manager.enterIdle();
        assert(manager.goBack(true));
        assert(manager.pendingScreen == &manager.secondPad);
        assert(manager.screenHistoryCount == 0);
        assert(manager.skipHistoryPush);
        assert(!manager.restoreTransientScreen());
    }
    assert(wake_requests == 5);
    puts("Navigation wake tests passed");
}