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
SystemState previousState = SystemState::STATE_UI_LIST;
TaskItem tasks[MAX_TASKS];
uint32_t taskCount = 0;
int selectedTaskIndex = -1;

// Add-task view input buffer
char inputBuffer[INPUT_BUFFER_SIZE];
uint32_t inputBufferLen = 0;

// Calendar state
CalendarView currentCalendarView = CalendarView::MONTH;
int calYear = 2000;
int calMonth = 1;
int calDay = 1;
int selectedEventIndex = -1;
CalendarEvent calendarEvents[MAX_CALENDAR_EVENTS];
uint32_t calendarEventCount = 0;

// Calendar add/edit state
int eventEditHour = 9;
int eventEditDuration = 60;
int eventEditField = 0; // 0: Title, 1: Hour, 2: Duration

// Task add/edit state
int taskEditField = 0; // 0: Title, 1: Has Due Date, 2: Day, 3: Month, 4: Year, 5: Hour, 6: Minute
bool taskEditHasDue = false;
int taskEditYear = 2026;
int taskEditMonth = 4;
int taskEditDay = 24;
int taskEditHour = 9;
int taskEditMinute = 0;

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
                    case 0xB4: // Left arrow
                        sendSystemEvent(SystemEventType::EVENT_NAV_LEFT);
                        break;
                    case 0xB7: // Right arrow
                        sendSystemEvent(SystemEventType::EVENT_NAV_RIGHT);
                        break;
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
                    case 0x7F: // Del key
                        // We'll repurpose a special param for Delete
                        sendSystemEvent(SystemEventType::EVENT_TYPE_CHAR, 0x7F);
                        break;
                    case 0x1B: // ESC — sleep in LIST, cancel in ADD/ALIGN
                        sendSystemEvent(SystemEventType::SLEEP_REQ);
                        break;
                    case 0x9A: // Fn + A — trigger alignment mode
                        // We will repurpose EVENT_TYPE_CHAR with a special param
                        sendSystemEvent(SystemEventType::EVENT_TYPE_CHAR, 0x9A);
                        break;
                    case 0xA8: // Fn + C — trigger calendar mode
                        sendSystemEvent(SystemEventType::EVENT_TYPE_CHAR, 0xA8);
                        break;
                    case 0x9F: // Fn + H — trigger help screen
                        sendSystemEvent(SystemEventType::EVENT_TYPE_CHAR, 0x9F);
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

        snprintf(key, sizeof(key), "task_%lu_hasdue", i);
        prefs.putBool(key, tasks[i].hasDueDate);

        snprintf(key, sizeof(key), "task_%lu_duey", i);
        prefs.putUShort(key, tasks[i].dueYear);

        snprintf(key, sizeof(key), "task_%lu_duem", i);
        prefs.putUChar(key, tasks[i].dueMonth);

        snprintf(key, sizeof(key), "task_%lu_dued", i);
        prefs.putUChar(key, tasks[i].dueDay);

        snprintf(key, sizeof(key), "task_%lu_dueh", i);
        prefs.putUChar(key, tasks[i].dueHour);

        snprintf(key, sizeof(key), "task_%lu_duemin", i);
        prefs.putUChar(key, tasks[i].dueMinute);
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

        snprintf(key, sizeof(key), "task_%lu_hasdue", i);
        tasks[i].hasDueDate = prefs.getBool(key, false);

        snprintf(key, sizeof(key), "task_%lu_duey", i);
        tasks[i].dueYear = prefs.getUShort(key, 0);

        snprintf(key, sizeof(key), "task_%lu_duem", i);
        tasks[i].dueMonth = prefs.getUChar(key, 0);

        snprintf(key, sizeof(key), "task_%lu_dued", i);
        tasks[i].dueDay = prefs.getUChar(key, 0);

        snprintf(key, sizeof(key), "task_%lu_dueh", i);
        tasks[i].dueHour = prefs.getUChar(key, 0);

        snprintf(key, sizeof(key), "task_%lu_duemin", i);
        tasks[i].dueMinute = prefs.getUChar(key, 0);
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

// Add demo events
void addDemoEvents() {
    calendarEvents[0] = CalendarEvent("Breakfast", 2000, 1, 1, 8, 0, 30, "Eat healthy!", "Kitchen");
    calendarEvents[1] = CalendarEvent("Coding StikE", 2000, 1, 1, 9, 30, 120, "Work on UI", "Office");
    calendarEvents[2] = CalendarEvent("Lunch", 2000, 1, 1, 12, 0, 60, "Meeting with team", "Cafe");
    calendarEvents[3] = CalendarEvent("Workout", 2000, 1, 1, 17, 0, 45, "Leg day", "Gym");
    calendarEvents[4] = CalendarEvent("Dinner", 2000, 1, 1, 19, 0, 60, "Relax", "Home");
    calendarEvents[5] = CalendarEvent("Planning", 2000, 1, 2, 10, 0, 90, "Plan next week", "Office");
    calendarEvents[6] = CalendarEvent("Call Alice", 2000, 1, 3, 14, 0, 30, "Discuss project", "Phone");
    calendarEvents[7] = CalendarEvent("Gym", 2000, 1, 4, 7, 0, 60, "Cardio", "Gym");
    calendarEvents[8] = CalendarEvent("Weekly Sync", 2000, 1, 5, 11, 0, 60, "All hands", "Conference Room");
    calendarEventCount = 9;
}

void removeLinkedEvent(uint32_t taskId) {
    if (taskId == 0) return;
    for (uint32_t i = 0; i < calendarEventCount; i++) {
        if (calendarEvents[i].linkedTaskId == taskId) {
            LOG_PRINTF("[Sync] Removing linked event: %s\n", calendarEvents[i].title);
            for (uint32_t j = i; j < calendarEventCount - 1; j++) {
                calendarEvents[j] = calendarEvents[j+1];
            }
            calendarEventCount--;
            return; // Assume 1:1 mapping
        }
    }
}

void syncTaskToCalendar(const TaskItem& task) {
    // First, remove any existing linked event
    removeLinkedEvent(task.timestamp);

    if (task.hasDueDate) {
        if (calendarEventCount < MAX_CALENDAR_EVENTS) {
            calendarEvents[calendarEventCount] = CalendarEvent(
                task.title, task.dueYear, task.dueMonth, task.dueDay, 
                task.dueHour, task.dueMinute, 60, "Linked Task", "StikE", task.timestamp
            );
            calendarEventCount++;
            LOG_PRINTF("[Sync] Created calendar event for task: %s at %02d:%02d\n", task.title, task.dueHour, task.dueMinute);
        }
    }
}

void enterSleepMode() {
    LOG_PRINTLN("[State] Entering SLEEP mode");

    displayMgr.turnOffTFT();
    displayMgr.prepareEpaperViews(tasks, taskCount, calendarEvents, calendarEventCount, calYear, calMonth, calDay, 0);

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

        case SystemEventType::EVENT_BACKSPACE:
            if (selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(taskCount)) {
                LOG_PRINTF("[Input] Deleting task %d\n", selectedTaskIndex);
                removeLinkedEvent(tasks[selectedTaskIndex].timestamp);
                for (uint32_t i = selectedTaskIndex; i < taskCount - 1; ++i) {
                    tasks[i] = tasks[i + 1];
                }
                taskCount--;
                if (selectedTaskIndex >= static_cast<int>(taskCount)) {
                    selectedTaskIndex = taskCount - 1;
                }
                saveTasks();
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_TYPE_CHAR:
            // 'n'/'N' opens Add Task view
            if (event.param == 'n' || event.param == 'N') {
                LOG_PRINTLN("[Input] N - opening Add Task view");
                inputBuffer[0] = '\0';
                inputBufferLen = 0;
                taskEditField = 0;
                taskEditHasDue = false;
                taskEditYear = calYear;
                taskEditMonth = calMonth;
                taskEditDay = calDay;
                taskEditHour = 9;
                taskEditMinute = 0;
                currentState = SystemState::STATE_UI_ADD_TASK;
                uiDirty = true;
            }
            // 'e'/'E' opens Edit Task view
            else if (event.param == 'e' || event.param == 'E') {
                if (selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(taskCount)) {
                    LOG_PRINTF("[Input] E - opening Edit Task view for index %d\n", selectedTaskIndex);
                    strncpy(inputBuffer, tasks[selectedTaskIndex].title, INPUT_BUFFER_SIZE - 1);
                    inputBuffer[INPUT_BUFFER_SIZE - 1] = '\0';
                    inputBufferLen = strlen(inputBuffer);
                    taskEditField = 0;
                    taskEditHasDue = tasks[selectedTaskIndex].hasDueDate;
                    taskEditYear = tasks[selectedTaskIndex].dueYear > 0 ? tasks[selectedTaskIndex].dueYear : calYear;
                    taskEditMonth = tasks[selectedTaskIndex].dueMonth > 0 ? tasks[selectedTaskIndex].dueMonth : calMonth;
                    taskEditDay = tasks[selectedTaskIndex].dueDay > 0 ? tasks[selectedTaskIndex].dueDay : calDay;
                    taskEditHour = tasks[selectedTaskIndex].dueHour;
                    taskEditMinute = tasks[selectedTaskIndex].dueMinute;
                    currentState = SystemState::STATE_UI_EDIT_TASK;
                    uiDirty = true;
                }
            }
            // 'd'/'D' deletes task
            else if (event.param == 'd' || event.param == 'D') {
                if (selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(taskCount)) {
                    LOG_PRINTF("[Input] D - deleting task %d\n", selectedTaskIndex);
                    removeLinkedEvent(tasks[selectedTaskIndex].timestamp);
                    for (uint32_t i = selectedTaskIndex; i < taskCount - 1; ++i) {
                        tasks[i] = tasks[i + 1];
                    }
                    taskCount--;
                    if (selectedTaskIndex >= static_cast<int>(taskCount)) {
                        selectedTaskIndex = taskCount - 1;
                    }
                    saveTasks();
                    uiDirty = true;
                }
            }
            // 0x9A (Fn+A) opens Alignment Mode
            else if (event.param == 0x9A) {
                LOG_PRINTLN("[Input] Fn+A - entering alignment mode");
                currentState = SystemState::STATE_UI_ALIGN;
                displayMgr.clearFullHardwareScreen();
                uiDirty = true;
            }
            // 0xA8 (Fn+C) opens Calendar
            else if (event.param == 0xA8) {
                LOG_PRINTLN("[Input] Fn+C - entering calendar mode");
                currentState = SystemState::STATE_UI_CALENDAR;
                uiDirty = true;
            }
            // 0x9F (Fn+H) opens Help
            else if (event.param == 0x9F) {
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
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
            if (taskEditField == 0) { // Title
                if (inputBufferLen < INPUT_BUFFER_SIZE - 1 &&
                    c >= 0x20 && c <= 0x7E) {
                    inputBuffer[inputBufferLen++] = static_cast<char>(c);
                    inputBuffer[inputBufferLen] = '\0';
                    uiDirty = true;
                }
            } else if (taskEditField == 1) { // Has Due Date
                if (c == 'y' || c == 'Y') { taskEditHasDue = true; uiDirty = true; }
                else if (c == 'n' || c == 'N') { taskEditHasDue = false; uiDirty = true; }
            }
            if (event.param == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            }
            break;
        }

        case SystemEventType::EVENT_NAV_UP:
            if (taskEditField == 2) { taskEditDay = (taskEditDay % 31) + 1; uiDirty = true; }
            else if (taskEditField == 3) { taskEditMonth = (taskEditMonth % 12) + 1; uiDirty = true; }
            else if (taskEditField == 4) { taskEditYear++; uiDirty = true; }
            else if (taskEditField == 5) { taskEditHour = (taskEditHour + 1) % 24; uiDirty = true; }
            else if (taskEditField == 6) { taskEditMinute = (taskEditMinute + 5) % 60; uiDirty = true; }
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (taskEditField == 2) { taskEditDay = (taskEditDay > 1) ? taskEditDay - 1 : 31; uiDirty = true; }
            else if (taskEditField == 3) { taskEditMonth = (taskEditMonth > 1) ? taskEditMonth - 1 : 12; uiDirty = true; }
            else if (taskEditField == 4) { taskEditYear--; uiDirty = true; }
            else if (taskEditField == 5) { taskEditHour = (taskEditHour + 23) % 24; uiDirty = true; }
            else if (taskEditField == 6) { taskEditMinute = (taskEditMinute > 0) ? taskEditMinute - 5 : 55; uiDirty = true; }
            break;

        case SystemEventType::EVENT_BACKSPACE:
            if (taskEditField == 0) {
                if (inputBufferLen > 0) {
                    inputBuffer[--inputBufferLen] = '\0';
                    uiDirty = true;
                }
            } else {
                taskEditField--;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_SELECT:
            if (taskEditField == 0) {
                taskEditField = 1;
                uiDirty = true;
            } else if (taskEditField == 1) {
                if (taskEditHasDue) taskEditField = 2;
                else goto save_task;
                uiDirty = true;
            } else if (taskEditField < 6) {
                taskEditField++;
                uiDirty = true;
            } else {
            save_task:
                if (inputBufferLen > 0 && taskCount < MAX_TASKS) {
                    tasks[taskCount] = TaskItem(inputBuffer, false, millis(), taskEditHasDue, taskEditYear, taskEditMonth, taskEditDay, taskEditHour, taskEditMinute);
                    syncTaskToCalendar(tasks[taskCount]);
                    taskCount++;
                    LOG_PRINTF("[Input] Added task: %s\n", inputBuffer);
                    if (selectedTaskIndex < 0) {
                        selectedTaskIndex = 0;
                    }
                    saveTasks();
                }
                inputBuffer[0] = '\0';
                inputBufferLen = 0;
                taskEditField = 0;
                currentState = SystemState::STATE_UI_LIST;
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}

static void handleUIEditTaskEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            // ESC in EDIT_TASK = cancel
            LOG_PRINTLN("[Input] ESC in EDIT - canceling");
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_TYPE_CHAR: {
            int c = event.param;
            if (taskEditField == 0) { // Title
                if (inputBufferLen < INPUT_BUFFER_SIZE - 1 &&
                    c >= 0x20 && c <= 0x7E) {
                    inputBuffer[inputBufferLen++] = static_cast<char>(c);
                    inputBuffer[inputBufferLen] = '\0';
                    uiDirty = true;
                }
            } else if (taskEditField == 1) { // Has Due Date
                if (c == 'y' || c == 'Y') { taskEditHasDue = true; uiDirty = true; }
                else if (c == 'n' || c == 'N') { taskEditHasDue = false; uiDirty = true; }
            }
            if (event.param == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            }
            break;
        }

        case SystemEventType::EVENT_NAV_UP:
            if (taskEditField == 2) { taskEditDay = (taskEditDay % 31) + 1; uiDirty = true; }
            else if (taskEditField == 3) { taskEditMonth = (taskEditMonth % 12) + 1; uiDirty = true; }
            else if (taskEditField == 4) { taskEditYear++; uiDirty = true; }
            else if (taskEditField == 5) { taskEditHour = (taskEditHour + 1) % 24; uiDirty = true; }
            else if (taskEditField == 6) { taskEditMinute = (taskEditMinute + 5) % 60; uiDirty = true; }
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (taskEditField == 2) { taskEditDay = (taskEditDay > 1) ? taskEditDay - 1 : 31; uiDirty = true; }
            else if (taskEditField == 3) { taskEditMonth = (taskEditMonth > 1) ? taskEditMonth - 1 : 12; uiDirty = true; }
            else if (taskEditField == 4) { taskEditYear--; uiDirty = true; }
            else if (taskEditField == 5) { taskEditHour = (taskEditHour + 23) % 24; uiDirty = true; }
            else if (taskEditField == 6) { taskEditMinute = (taskEditMinute > 0) ? taskEditMinute - 5 : 55; uiDirty = true; }
            break;

        case SystemEventType::EVENT_BACKSPACE:
            if (taskEditField == 0) {
                if (inputBufferLen > 0) {
                    inputBuffer[--inputBufferLen] = '\0';
                    uiDirty = true;
                }
            } else {
                taskEditField--;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_SELECT:
            if (taskEditField == 0) {
                taskEditField = 1;
                uiDirty = true;
            } else if (taskEditField == 1) {
                if (taskEditHasDue) taskEditField = 2;
                else goto save_edit;
                uiDirty = true;
            } else if (taskEditField < 6) {
                taskEditField++;
                uiDirty = true;
            } else {
            save_edit:
                if (inputBufferLen > 0 && selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(taskCount)) {
                    strncpy(tasks[selectedTaskIndex].title, inputBuffer, 31);
                    tasks[selectedTaskIndex].title[31] = '\0';
                    tasks[selectedTaskIndex].hasDueDate = taskEditHasDue;
                    tasks[selectedTaskIndex].dueYear = taskEditYear;
                    tasks[selectedTaskIndex].dueMonth = taskEditMonth;
                    tasks[selectedTaskIndex].dueDay = taskEditDay;
                    tasks[selectedTaskIndex].dueHour = taskEditHour;
                    tasks[selectedTaskIndex].dueMinute = taskEditMinute;
                    
                    syncTaskToCalendar(tasks[selectedTaskIndex]);
                    LOG_PRINTF("[Input] Edited task %d: %s\n", selectedTaskIndex, inputBuffer);
                    saveTasks();
                }
                inputBuffer[0] = '\0';
                inputBufferLen = 0;
                taskEditField = 0;
                currentState = SystemState::STATE_UI_LIST;
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}

static void handleUIAlignEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            // ESC in ALIGN = return to LIST
            LOG_PRINTLN("[Input] ESC in ALIGN - returning to list");
            currentState = SystemState::STATE_UI_LIST;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;

        case SystemEventType::EVENT_TYPE_CHAR:
            if (event.param == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_NAV_UP:
            displayMgr.offsetY--;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            displayMgr.offsetY++;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;
            
        case SystemEventType::EVENT_NAV_LEFT:
            displayMgr.offsetX--;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;
            
        case SystemEventType::EVENT_NAV_RIGHT:
            displayMgr.offsetX++;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;

        default:
            break;
    }
}

static void handleUICalendarEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            LOG_PRINTLN("[Input] ESC in CALENDAR - returning to list");
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_UP:
            if (currentCalendarView == CalendarView::MONTH) {
                calDay -= 7;
                if (calDay < 1) calDay = 1;
            } else if (currentCalendarView == CalendarView::DAY) {
                if (selectedEventIndex > 0) selectedEventIndex--;
            } else {
                calDay--;
                if (calDay < 1) calDay = 1;
            }
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (currentCalendarView == CalendarView::MONTH) {
                calDay += 7;
                if (calDay > 31) calDay = 31;
            } else if (currentCalendarView == CalendarView::DAY) {
                // Count events for this day
                int dayCount = 0;
                for (uint32_t i = 0; i < calendarEventCount; i++) {
                    if (calendarEvents[i].day == calDay && calendarEvents[i].month == calMonth && calendarEvents[i].year == calYear) dayCount++;
                }
                if (selectedEventIndex < dayCount - 1) selectedEventIndex++;
            } else {
                calDay++;
                if (calDay > 31) calDay = 31;
            }
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_LEFT:
            calDay--;
            if (calDay < 1) calDay = 1;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_RIGHT:
            calDay++;
            if (calDay > 31) calDay = 31;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_SELECT:
            if (currentCalendarView == CalendarView::MONTH) {
                currentCalendarView = CalendarView::DAY;
                selectedEventIndex = 0;
            } else if (currentCalendarView == CalendarView::DAY) {
                if (selectedEventIndex >= 0) {
                    currentState = SystemState::STATE_UI_EVENT_DETAIL;
                } else {
                    currentCalendarView = CalendarView::MONTH;
                }
            }
            uiDirty = true;
            break;

        case SystemEventType::EVENT_TYPE_CHAR:
            if (event.param == 'v' || event.param == 'V') {
                if (currentCalendarView == CalendarView::MONTH) currentCalendarView = CalendarView::WEEK;
                else if (currentCalendarView == CalendarView::WEEK) currentCalendarView = CalendarView::DAY;
                else currentCalendarView = CalendarView::MONTH;
                selectedEventIndex = 0;
                uiDirty = true;
            } else if (event.param == 'n' || event.param == 'N') {
                inputBuffer[0] = '\0';
                inputBufferLen = 0;
                eventEditField = 0;
                eventEditHour = 9;
                eventEditDuration = 60;
                currentState = SystemState::STATE_UI_ADD_EVENT;
                uiDirty = true;
            } else if (event.param == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            } else if (event.param == 0x7F) { // Delete
                if (currentCalendarView == CalendarView::DAY && selectedEventIndex >= 0) {
                    int dayIdx = 0;
                    for (uint32_t i = 0; i < calendarEventCount; i++) {
                        if (calendarEvents[i].day == calDay && calendarEvents[i].month == calMonth && calendarEvents[i].year == calYear) {
                            if (dayIdx == selectedEventIndex) {
                                for (uint32_t j = i; j < calendarEventCount - 1; j++) {
                                    calendarEvents[j] = calendarEvents[j+1];
                                }
                                calendarEventCount--;
                                break;
                            }
                            dayIdx++;
                        }
                    }
                    uiDirty = true;
                }
            }
            break;

        case SystemEventType::EVENT_BACKSPACE:
            if (currentCalendarView != CalendarView::MONTH) {
                currentCalendarView = CalendarView::MONTH;
                uiDirty = true;
            } else {
                currentState = SystemState::STATE_UI_LIST;
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}

static void handleUIAddEventEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::SLEEP_REQ:
            currentState = SystemState::STATE_UI_CALENDAR;
            uiDirty = true;
            break;

        case SystemEventType::EVENT_TYPE_CHAR: {
            int c = event.param;
            if (eventEditField == 0) {
                if (inputBufferLen < INPUT_BUFFER_SIZE - 1 && c >= 0x20 && c <= 0x7E) {
                    inputBuffer[inputBufferLen++] = static_cast<char>(c);
                    inputBuffer[inputBufferLen] = '\0';
                    uiDirty = true;
                }
            }
            if (c == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            }
            break;
        }

        case SystemEventType::EVENT_NAV_UP:
            if (eventEditField == 1) { eventEditHour = (eventEditHour + 1) % 24; uiDirty = true; }
            else if (eventEditField == 2) { eventEditDuration += 15; uiDirty = true; }
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (eventEditField == 1) { eventEditHour = (eventEditHour + 23) % 24; uiDirty = true; }
            else if (eventEditField == 2) { if (eventEditDuration > 15) eventEditDuration -= 15; uiDirty = true; }
            break;

        case SystemEventType::EVENT_SELECT:
            if (eventEditField < 2) {
                eventEditField++;
                uiDirty = true;
            } else {
                if (calendarEventCount < MAX_CALENDAR_EVENTS) {
                    calendarEvents[calendarEventCount] = CalendarEvent(inputBuffer, calYear, calMonth, calDay, eventEditHour, 0, eventEditDuration);
                    calendarEventCount++;
                }
                currentState = SystemState::STATE_UI_CALENDAR;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_BACKSPACE:
            if (eventEditField == 0 && inputBufferLen > 0) {
                inputBuffer[--inputBufferLen] = '\0';
                uiDirty = true;
            } else if (eventEditField > 0) {
                eventEditField--;
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}


static void handleUIEventDetailEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::EVENT_BACKSPACE:
            currentState = SystemState::STATE_UI_CALENDAR;
            uiDirty = true;
            break;
        case SystemEventType::EVENT_TYPE_CHAR:
            if (event.param == 0x9F) { // Fn + H
                previousState = currentState;
                currentState = SystemState::STATE_UI_HELP;
                uiDirty = true;
            }
            break;
        default:
            break;
    }
}

static void handleUIHelpEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::EVENT_BACKSPACE:
            currentState = previousState;
            uiDirty = true;
            break;
        default:
            break;
    }
}

void handleSleepState() {
    LOG_PRINTF("[Sleep] Cycle %u, view %d\n", sleepCycleCount, currentEpaperView);

#ifndef DIAG_UI_ONLY
    uint32_t totalViews = displayMgr.getEpaperViewCount();
    if (totalViews > 0) {
        displayMgr.updateEpaperPartial(currentEpaperView);
        currentEpaperView = (currentEpaperView + 1) % totalViews;
    }
#endif
    sleepCycleCount++;

    // Configure wake-up sources
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    
    // Enable GPIO 14 LOW_LEVEL wake only when going to sleep!
    // Doing this in setup() causes an interrupt storm if the button is held low.
    gpio_wakeup_enable(static_cast<gpio_num_t>(Pins::WAKE_BTN), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Clear wake flag before sleeping
    wakeRequested = false;

    // DO NOT power down VDDSDIO during light sleep! It crashes the flash memory 
    // causing a reboot (which explains the random color flashing on wake).
    // esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);

    // Enter light sleep
    esp_light_sleep_start();

    // Disable low level wakeup immediately after waking so it doesn't cause WDT panics
    gpio_wakeup_disable(static_cast<gpio_num_t>(Pins::WAKE_BTN));
    
    // Re-attach active-mode falling edge interrupt just to be safe
    attachInterrupt(Pins::WAKE_BTN, wakeButtonISR, FALLING);

    // Check wake reason immediately after waking
    if (wakeRequested) {
        LOG_PRINTLN("[Sleep] Woke from GPIO interrupt");
        wakeToActive();
    }
}

void setup() {
    // Give serial a moment to initialize before beginning
    delay(2000); 
    Serial.begin(115200);
    // Wait for Serial to connect (up to 3 seconds) for easier debugging
    while (!Serial && millis() < 5000); 

    delay(500);
    LOG_PRINTLN("\n=== StikE Firmware Starting ===");

    LOG_PRINTLN("[Setup] Calling displayMgr.initBusesAndDisplays()...");
    displayMgr.initBusesAndDisplays();
    LOG_PRINTLN("[Setup] displayMgr.initBusesAndDisplays() returned");
    delay(100); // Stabilization delay
    
    LOG_PRINTLN("[Setup] Calling keyboardMgr.init()...");
    keyboardMgr.init();
    LOG_PRINTLN("[Setup] keyboardMgr.init() returned");
    delay(100); // Stabilization delay

    pinMode(Pins::WAKE_BTN, INPUT_PULLUP);
    attachInterrupt(Pins::WAKE_BTN, wakeButtonISR, FALLING);

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

    addDemoEvents();
    LOG_PRINTLN("[Setup] Demo events initialized");

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
            case SystemState::STATE_UI_ALIGN:
                handleUIAlignEvent(event);
                break;
            case SystemState::STATE_UI_CALENDAR:
                handleUICalendarEvent(event);
                break;
            case SystemState::STATE_UI_EDIT_TASK:
                handleUIEditTaskEvent(event);
                break;
            case SystemState::STATE_UI_ADD_EVENT:
                handleUIAddEventEvent(event);
                break;
            case SystemState::STATE_UI_EVENT_DETAIL:
                handleUIEventDetailEvent(event);
                break;
            case SystemState::STATE_UI_HELP:
                handleUIHelpEvent(event);
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
                displayMgr.drawAddViewGUI(inputBuffer, taskEditField, taskEditHasDue, taskEditYear, taskEditMonth, taskEditDay, taskEditHour, taskEditMinute);
                break;
            case SystemState::STATE_UI_ALIGN:
                displayMgr.drawAlignGUI();
                break;
            case SystemState::STATE_UI_CALENDAR:
                displayMgr.drawCalendarGUI(currentCalendarView, calYear, calMonth, calDay, calendarEvents, calendarEventCount, selectedEventIndex);
                break;
            case SystemState::STATE_UI_EDIT_TASK:
                displayMgr.drawEditViewGUI(inputBuffer, taskEditField, taskEditHasDue, taskEditYear, taskEditMonth, taskEditDay, taskEditHour, taskEditMinute);
                break;
            case SystemState::STATE_UI_ADD_EVENT:
                displayMgr.drawAddEventGUI(inputBuffer, eventEditHour, eventEditDuration, eventEditField);
                break;
            case SystemState::STATE_UI_EVENT_DETAIL: {
                // Find selected event
                int dayIdx = 0;
                for (uint32_t i = 0; i < calendarEventCount; i++) {
                    if (calendarEvents[i].day == calDay && calendarEvents[i].month == calMonth) {
                        if (dayIdx == selectedEventIndex) {
                            displayMgr.drawEventDetailGUI(calendarEvents[i]);
                            break;
                        }
                        dayIdx++;
                    }
                }
                break;
            }
            case SystemState::STATE_UI_HELP:
                displayMgr.drawHelpGUI(previousState);
                break;
            default:
                break;
        }
        uiDirty = false;
    }

    delay(50);
}
