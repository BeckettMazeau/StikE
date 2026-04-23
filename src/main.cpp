#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_log.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "pins.h"
#include "state_types.h"
#include "display_mgr.h"
#include "keyboard_mgr.h"

// TEST_START: Systems Test
#ifdef STike_SYSTEM_TEST
#include "systems_test.h"
#endif
// TEST_END: Systems Test

DisplayManager displayMgr;
KeyboardManager keyboardMgr;

// TEST_START: Systems Test
#ifdef STike_SYSTEM_TEST
SystemsTest systemsTest;
#endif
// TEST_END: Systems Test

// Event queue for system events
QueueHandle_t systemEventQueue = nullptr;

SystemState currentState = SystemState::STATE_UI_LIST;
TaskItem tasks[MAX_TASKS];
uint32_t taskCount = 0;
int selectedTaskIndex = -1;

// Add-task view input buffer
char inputBuffer[INPUT_BUFFER_SIZE];
uint32_t inputBufferLen = 0;

// Redraw only after an event mutates state
bool uiDirty = true;

uint32_t sleepCycleCount = 0;
int currentEpaperView = 0;

// Volatile flag for wake-up from light sleep
volatile bool wakeRequested = false;

// Preferences for NVS storage
Preferences prefs;

// Sleep-safe logging macros
#define LOG_PRINT(...)   do { if (Serial) Serial.print(__VA_ARGS__); } while(0)
#define LOG_PRINTLN(...) do { if (Serial) Serial.println(__VA_ARGS__); } while(0)
#define LOG_PRINTF(...)  do { if (Serial) { char _buf[256]; snprintf(_buf, sizeof(_buf), __VA_ARGS__); Serial.print(_buf); } } while(0)

void IRAM_ATTR wakeButtonISR() {
    wakeRequested = true;
}

// Send system event to queue
void sendSystemEvent(SystemEventType type, int param = 0) {
    if (systemEventQueue) {
        SystemEvent event = {type, param};
        xQueueSendFromISR(systemEventQueue, &event, nullptr);
    }
}

// Keyboard reader task - runs on core 0
void keyboardTask(void* parameter) {
    for (;;) {
        // --- CONCURRENCY FIX START ---
        #ifdef STike_SYSTEM_TEST
        if (SystemsTest::isTestMode()) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Yield Core 0 while Test Mode handles I2C
            continue;
        }
        #endif
        // --- CONCURRENCY FIX END ---

        if (keyboardMgr.isAvailable()) {
            char key = keyboardMgr.getKeyPress();
            if (key != 0) {
                uint8_t k = static_cast<uint8_t>(key);
                switch (k) {
                    case 0xB5: // Up arrow
                        sendSystemEvent(SystemEventType::EVENT_NAV_UP);
                        break;
                    case 0xB6: // Down arrow
                        sendSystemEvent(SystemEventType::EVENT_NAV_DOWN);
                        break;
                    case 0x0D: // Enter
                        sendSystemEvent(SystemEventType::EVENT_SELECT);
                        break;
                    case 0x08: // Backspace / Del
                        sendSystemEvent(SystemEventType::EVENT_BACKSPACE);
                        break;
                    case 0x1B: // ESC — sleep in LIST, cancel in ADD (handled by state)
                        sendSystemEvent(SystemEventType::SLEEP_REQ);
                        break;
                    default:
                        if (k >= 0x20 && k <= 0x7E) {
                            // Printable ASCII — carries the char as param
                            sendSystemEvent(SystemEventType::EVENT_TYPE_CHAR, k);
                        }
                        break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms polling interval
    }
}

// Save tasks to NVS
void saveTasks() {
    prefs.begin("stike", false);
    prefs.putUInt("taskCount", taskCount);
    
    for (uint32_t i = 0; i < taskCount && i < MAX_TASKS; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "task_%lu_title", i);
        prefs.putString(key, tasks[i].title);
        
        snprintf(key, sizeof(key), "task_%lu_completed", i);
        prefs.putBool(key, tasks[i].isCompleted);
        
        snprintf(key, sizeof(key), "task_%lu_timestamp", i);
        prefs.putUInt(key, tasks[i].timestamp);
    }
    
    prefs.end();
    LOG_PRINTLN("[NVS] Tasks saved");
}

// Load tasks from NVS
void loadTasks() {
    prefs.begin("stike", true);
    taskCount = prefs.getUInt("taskCount", 0);
    if (taskCount > MAX_TASKS) taskCount = MAX_TASKS;
    
    for (uint32_t i = 0; i < taskCount; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "task_%lu_title", i);
        String titleStr = prefs.getString(key, "");
        strncpy(tasks[i].title, titleStr.c_str(), 31);
        tasks[i].title[31] = '\0';
        
        snprintf(key, sizeof(key), "task_%lu_completed", i);
        tasks[i].isCompleted = prefs.getBool(key, false);
        
        snprintf(key, sizeof(key), "task_%lu_timestamp", i);
        tasks[i].timestamp = prefs.getUInt(key, 0);
    }
    
    prefs.end();
    LOG_PRINTF("[NVS] Loaded %u tasks\n", taskCount);
}

// Add demo tasks (used when no saved tasks exist)
void addDemoTasks() {
    tasks[0] = TaskItem("Review PR #42", false, 0);
    tasks[1] = TaskItem("Update docs", true, 0);
    tasks[2] = TaskItem("Fix bug in display", false, 0);
    tasks[3] = TaskItem("Team standup", true, 0);
    taskCount = 4;
}

void enterSleepMode() {
    LOG_PRINTLN("[State] Entering SLEEP mode");

    displayMgr.turnOffTFT();
    displayMgr.prepareEpaperViews(tasks, taskCount);

    sleepCycleCount = 0;
    currentEpaperView = 0;
    currentState = SystemState::STATE_SLEEP;
    
    // Save tasks before sleeping
    saveTasks();
}

void wakeToActive() {
    LOG_PRINTLN("[State] Waking to ACTIVE mode");

    displayMgr.turnOnTFT();
    currentState = SystemState::STATE_UI_LIST;
    selectedTaskIndex = (taskCount > 0) ? 0 : -1;
    inputBuffer[0] = '\0';
    inputBufferLen = 0;
    uiDirty = true;
}

static void handleUIListEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            LOG_PRINTLN("[Input] ESC in LIST - entering sleep");
            enterSleepMode();
            return;

        case SystemEventType::EVENT_NAV_UP:
            if (selectedTaskIndex > 0) {
                selectedTaskIndex--;
                uiDirty = true;
            } else if (selectedTaskIndex < 0 && taskCount > 0) {
                selectedTaskIndex = 0;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (selectedTaskIndex < static_cast<int>(taskCount) - 1) {
                selectedTaskIndex++;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_SELECT:
            if (selectedTaskIndex >= 0 &&
                selectedTaskIndex < static_cast<int>(taskCount)) {
                tasks[selectedTaskIndex].isCompleted =
                    !tasks[selectedTaskIndex].isCompleted;
                LOG_PRINTF("[Input] Toggled task %d\n", selectedTaskIndex);
                saveTasks();
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_TYPE_CHAR:
            // 'n'/'N' is the only char that does anything in LIST view
            if (event.param == 'n' || event.param == 'N') {
                LOG_PRINTLN("[Input] N - opening Add Task view");
                inputBuffer[0] = '\0';
                inputBufferLen = 0;
                currentState = SystemState::STATE_UI_ADD_TASK;
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}

static void handleUIAddTaskEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            // ESC in ADD_TASK = cancel, not sleep
            LOG_PRINTLN("[Input] ESC in ADD - canceling");
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_TYPE_CHAR: {
            int c = event.param;
            if (inputBufferLen < INPUT_BUFFER_SIZE - 1 &&
                c >= 0x20 && c <= 0x7E) {
                inputBuffer[inputBufferLen++] = static_cast<char>(c);
                inputBuffer[inputBufferLen] = '\0';
                uiDirty = true;
            }
            break;
        }

        case SystemEventType::EVENT_BACKSPACE:
            if (inputBufferLen > 0) {
                inputBuffer[--inputBufferLen] = '\0';
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_SELECT:
            if (inputBufferLen > 0 && taskCount < MAX_TASKS) {
                tasks[taskCount] = TaskItem(inputBuffer, false, millis());
                taskCount++;
                LOG_PRINTF("[Input] Added task: %s\n", inputBuffer);
                if (selectedTaskIndex < 0) {
                    selectedTaskIndex = 0;
                }
                saveTasks();
            }
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        default:
            break;
    }
}

void handleSleepState() {
    LOG_PRINTF("[Sleep] Cycle %u, view %d\n", sleepCycleCount, currentEpaperView);

#ifndef DIAG_UI_ONLY
    displayMgr.updateEpaperPartial(currentEpaperView);
#endif
    currentEpaperView = (currentEpaperView + 1) % EPAPER_VIEW_COUNT;
    sleepCycleCount++;

    // Configure wake-up sources
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_sleep_enable_gpio_wakeup();

    // Clear wake flag before sleeping
    wakeRequested = false;

    // Power down VDDSDIO domain to reduce power consumption during sleep
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);

    // Enter light sleep
    esp_light_sleep_start();

    // Check wake reason immediately after waking
    if (wakeRequested) {
        LOG_PRINTLN("[Sleep] Woke from GPIO interrupt");
        wakeToActive();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    LOG_PRINTLN("\n=== StikE Firmware Starting ===");

    LOG_PRINTLN("[Setup] Calling displayMgr.initTFT()...");
    displayMgr.initTFT();
    LOG_PRINTLN("[Setup] displayMgr.initTFT() returned");
    
    LOG_PRINTLN("[Setup] Calling displayMgr.initEpaper()...");
    displayMgr.initEpaper();
    LOG_PRINTLN("[Setup] displayMgr.initEpaper() returned");
    
    LOG_PRINTLN("[Setup] Calling keyboardMgr.init()...");
    keyboardMgr.init();
    LOG_PRINTLN("[Setup] keyboardMgr.init() returned");

    pinMode(Pins::WAKE_BTN, INPUT_PULLUP);
    attachInterrupt(Pins::WAKE_BTN, wakeButtonISR, FALLING);
    esp_sleep_enable_gpio_wakeup();

    // Create system event queue
    systemEventQueue = xQueueCreate(10, sizeof(SystemEvent));
    if (systemEventQueue == nullptr) {
        LOG_PRINTLN("[ERROR] Failed to create system event queue");
    }

    // Create keyboard task on core 0
    BaseType_t result = xTaskCreatePinnedToCore(
        keyboardTask,
        "KeyboardTask",
        2048,  // Stack size
        nullptr,
        1,     // Priority
        nullptr,
        0      // Core 0
    );
    
    if (result != pdPASS) {
        LOG_PRINTLN("[ERROR] Failed to create keyboard task");
    }

    // Smoke-test drawing path to aid debugging UI rendering
#ifdef STike_SYSTEM_TEST
    Serial.println("[SYS_TEST] Triggering UI smoke test (MAGENTA)");
    displayMgr.drawSmokeTest();
#endif

// Also run a full-red screen diagnostic to ensure the end-to-end write path remains healthy
#ifdef STike_SYSTEM_TEST
    Serial.println("[SYS_TEST] Triggering full-red screen diagnostic");
    displayMgr.drawTestFullRed();
#endif

// Overlay diagnostic to verify end-to-end render path (cross on screen)
//#ifdef STike_SYSTEM_TEST
//    Serial.println("[SYS_TEST] Triggering diagnostic overlay");
//    displayMgr.drawTestOverlay();
//#endif

// Direct diagnostic path: drawMagenta directly bypassing the sprite to confirm TFT path
#ifdef STike_SYSTEM_TEST
    Serial.println("[SYS_TEST] Triggering direct color frame (MAGENTA) bypass sprite");
    displayMgr.drawDirectColorFrame(TFT_MAGENTA);
#endif

// Simple sprite test - tiny sprite to verify sprite path works
#ifdef STike_SYSTEM_TEST
    Serial.println("[SYS_TEST] Triggering simple sprite test (64x32)");
    displayMgr.drawActiveGUISimpleTest();
#endif

// Tiny extra test to verify full-screen write path (red screen)


    // Load tasks from NVS
    loadTasks();
    if (taskCount == 0) {
        addDemoTasks();
        LOG_PRINTLN("[Setup] No saved tasks found, using demo tasks");
    }
    LOG_PRINTF("[Setup] Loaded %u tasks\n", taskCount);

    LOG_PRINTLN("[Setup] Entering UI_LIST state");
    currentState = SystemState::STATE_UI_LIST;
    selectedTaskIndex = (taskCount > 0) ? 0 : -1;
    inputBuffer[0] = '\0';
    inputBufferLen = 0;
    uiDirty = true;
    
    // TEST_START: Systems Test initialization
    #ifdef STike_SYSTEM_TEST
    systemsTest.init();
    #endif
    // TEST_END: Systems Test
}

void loop() {
    // TEST_START: Systems Test
    #ifdef STike_SYSTEM_TEST
    if (SystemsTest::isTestMode()) {
        systemsTest.update();
        delay(50);
        return;
    }
    #endif
    // TEST_END: Systems Test
    
    if (currentState == SystemState::STATE_SLEEP) {
        handleSleepState();
        return;
    }

    // Drain all pending events, dispatching by current UI state
    SystemEvent event;
    while (xQueueReceive(systemEventQueue, &event, 0) == pdTRUE) {
        switch (currentState) {
            case SystemState::STATE_UI_LIST:
                handleUIListEvent(event);
                break;
            case SystemState::STATE_UI_ADD_TASK:
                handleUIAddTaskEvent(event);
                break;
            default:
                break;
        }
        // A sleep transition mid-drain: stop processing immediately
        if (currentState == SystemState::STATE_SLEEP) {
            return;
        }
    }

    // Only redraw when state has actually changed
    if (uiDirty) {
        switch (currentState) {
            case SystemState::STATE_UI_LIST:
                displayMgr.drawActiveGUI(tasks, taskCount, selectedTaskIndex);
                break;
            case SystemState::STATE_UI_ADD_TASK:
                displayMgr.drawAddViewGUI(inputBuffer);
                break;
            default:
                break;
        }
        uiDirty = false;
    }

    delay(50);
}
