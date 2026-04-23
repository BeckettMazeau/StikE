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

SystemState currentState = SystemState::STATE_ACTIVE;
TaskItem tasks[MAX_TASKS];
uint32_t taskCount = 0;
int selectedTaskIndex = -1;

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
                // Convert key to system event
                switch (key) {
                    case 0x1B: // ESC
                        sendSystemEvent(SystemEventType::SLEEP_REQ);
                        break;
                    case 'n':
                    case 'N':
                        sendSystemEvent(SystemEventType::TASK_ADDED);
                        break;
                    case 'j':
                    case 'J':
                    case 0x34: // Down arrow
                        sendSystemEvent(SystemEventType::KEY_PRESS, 'j');
                        break;
                    case 'k':
                    case 'K':
                    case 0x35: // Up arrow
                        sendSystemEvent(SystemEventType::KEY_PRESS, 'k');
                        break;
                    case 'x':
                    case 'X':
                        sendSystemEvent(SystemEventType::TASK_TOGGLED, selectedTaskIndex);
                        break;
                    default:
                        // Ignore other keys
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
    currentState = SystemState::STATE_ACTIVE;
    selectedTaskIndex = -1;
}

void handleActiveState() {
    // Process events from queue
    SystemEvent event;
    while (xQueueReceive(systemEventQueue, &event, 0) == pdTRUE) {
        switch (event.type) {
            case SystemEventType::SLEEP_REQ:
                LOG_PRINTLN("[Input] ESC pressed - entering sleep");
                enterSleepMode();
                return;
                
            case SystemEventType::TASK_ADDED:
                if (taskCount < MAX_TASKS) {
                    char newTitle[32];
                    snprintf(newTitle, sizeof(newTitle), "New Task %lu", taskCount + 1);
                    tasks[taskCount] = TaskItem(newTitle, false, millis());
                    taskCount++;
                    LOG_PRINTF("[Input] Added task %lu\n", taskCount);
                    saveTasks();  // Save immediately when task is added
                }
                break;
                
            case SystemEventType::KEY_PRESS:
                switch (event.param) {
                    case 'j': // Down arrow
                    case 0x34:
                        if (selectedTaskIndex < static_cast<int>(taskCount) - 1) {
                            selectedTaskIndex++;
                        }
                        break;
                        
                    case 'k': // Up arrow
                    case 0x35:
                        if (selectedTaskIndex > 0) {
                            selectedTaskIndex--;
                        }
                        break;
                        
                    default:
                        break;
                }
                break;
                
            case SystemEventType::TASK_TOGGLED:
                if (event.param >= 0 && event.param < static_cast<int>(taskCount)) {
                    tasks[event.param].isCompleted = !tasks[event.param].isCompleted;
                    LOG_PRINTF("[Input] Toggled task %d\n", event.param);
                    saveTasks();  // Save immediately when task is toggled
                }
                break;
                
            default:
                break;
        }
    }

    // Draw GUI
    displayMgr.drawActiveGUI(tasks, taskCount, selectedTaskIndex);
}

void handleSleepState() {
    LOG_PRINTF("[Sleep] Cycle %u, view %d\n", sleepCycleCount, currentEpaperView);

    displayMgr.updateEpaperPartial(currentEpaperView);
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

    // Load tasks from NVS
    loadTasks();
    if (taskCount == 0) {
        addDemoTasks();
        LOG_PRINTLN("[Setup] No saved tasks found, using demo tasks");
    }
    LOG_PRINTF("[Setup] Loaded %u tasks\n", taskCount);

    LOG_PRINTLN("[Setup] Entering ACTIVE state");
    currentState = SystemState::STATE_ACTIVE;
    
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
    
    switch (currentState) {
        case SystemState::STATE_ACTIVE:
            handleActiveState();
            break;

        case SystemState::STATE_SLEEP:
            handleSleepState();
            break;

        default:
            LOG_PRINTLN("[Error] Unknown state, resetting to ACTIVE");
            currentState = SystemState::STATE_ACTIVE;
            break;
    }

    delay(50);
}