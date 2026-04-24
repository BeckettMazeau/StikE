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
int taskListTopIndex = 0;  // Top of the visible scroll window for the task list
TaskViewMode currentTaskView = TaskViewMode::ACTIVE;
int filteredTaskIndices[MAX_TASKS];
int filteredTaskCount = 0;

void updateFilteredTasks() {
    filteredTaskCount = 0;
    for (uint32_t i = 0; i < taskCount; i++) {
        if (currentTaskView == TaskViewMode::ACTIVE && tasks[i].isCompleted) continue;
        if (currentTaskView == TaskViewMode::COMPLETED && !tasks[i].isCompleted) continue;
        filteredTaskIndices[filteredTaskCount++] = i;
    }
}
// Add-task view input buffer
char inputBuffer[INPUT_BUFFER_SIZE];
uint32_t inputBufferLen = 0;

// Calendar state
int detailScrollY = 0;
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
                    case 0x1B: // ESC — Cancel/Back
                        sendSystemEvent(SystemEventType::EVENT_CANCEL);
                        break;
                    case 0x80: // Fn + ESC — Sleep
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
    
    // Save the entire tasks array as a binary blob for speed and efficiency
    prefs.putBytes("tasks_blob", tasks, sizeof(tasks));
    
    prefs.end();
    LOG_PRINTLN("[NVS] Tasks saved as blob");
}

// Forward declaration - defined later in this file
void removeLinkedEvent(uint32_t taskId);

void cleanupOldCompletedTasks() {
    // No-op: TaskItem does not store a completion date, so age-based cleanup
    // is not possible without extending the struct. Reserved for future use.
    (void)calYear; (void)calMonth; (void)calDay;
}

// Load tasks from NVS
void loadTasks() {
    prefs.begin("stike", true);
    taskCount = prefs.getUInt("taskCount", 0);
    if (taskCount > MAX_TASKS) taskCount = MAX_TASKS;
    
    // Try to load the blob
    size_t len = prefs.getBytesLength("tasks_blob");
    if (len > 0) {
        if (len > sizeof(tasks)) len = sizeof(tasks);
        prefs.getBytes("tasks_blob", tasks, len);
        LOG_PRINTF("[NVS] Loaded %u tasks from blob (%d bytes)\n", taskCount, (int)len);
    } else {
        LOG_PRINTLN("[NVS] No tasks blob found, checking legacy individual keys...");
        // Legacy fallback for individual keys if someone is upgrading
        for (uint32_t i = 0; i < taskCount; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "task_%lu_title", i);
            String titleStr = prefs.getString(key, "");
            snprintf(tasks[i].title, sizeof(tasks[i].title), "%s", titleStr.c_str());
            
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
    }
    
    prefs.end();
    cleanupOldCompletedTasks();
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
    currentTaskView = TaskViewMode::ACTIVE;
    updateFilteredTasks();
    selectedTaskIndex = (filteredTaskCount > 0) ? 0 : -1;
    taskListTopIndex = 0;
    inputBuffer[0] = '\0';
    inputBufferLen = 0;
    uiDirty = true;
}

void parseNaturalLanguageTime(const char* title, int& hour) {
    const char* ptr = strchr(title, '@');
    if (!ptr) return;
    
    ptr++; // move past '@'
    char* endPtr;
    long val = strtol(ptr, &endPtr, 10);
    
    if (ptr == endPtr) return; // No number found
    
    // Check for am/pm
    bool isPm = false;
    bool isAm = false;
    if (strncasecmp(endPtr, "pm", 2) == 0) isPm = true;
    else if (strncasecmp(endPtr, "am", 2) == 0) isAm = true;
    
    if (isPm && val < 12) val += 12;
    else if (isAm && val == 12) val = 0;
    
    if (val >= 0 && val < 24) {
        hour = val;
    }
}

static void handleUIListEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::EVENT_CANCEL:
            LOG_PRINTLN("[Input] ESC in LIST - entering sleep");
            enterSleepMode();
            return;

        case SystemEventType::SLEEP_REQ:
            LOG_PRINTLN("[Input] Fn+ESC in LIST - entering sleep");
            enterSleepMode();
            return;

        case SystemEventType::EVENT_NAV_UP:
            if (selectedTaskIndex > 0) {
                selectedTaskIndex--;
                if (selectedTaskIndex < taskListTopIndex) {
                    taskListTopIndex = selectedTaskIndex;
                }
                uiDirty = true;
            } else if (selectedTaskIndex < 0 && filteredTaskCount > 0) {
                selectedTaskIndex = 0;
                taskListTopIndex = 0;
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_NAV_DOWN:
            if (selectedTaskIndex < static_cast<int>(filteredTaskCount) - 1) {
                selectedTaskIndex++;
                uiDirty = true;
                const int VISIBLE_ROWS = 7;
                if (selectedTaskIndex >= taskListTopIndex + VISIBLE_ROWS) {
                    taskListTopIndex = selectedTaskIndex - VISIBLE_ROWS + 1;
                }
            }
            break;

        case SystemEventType::EVENT_SELECT:
            if (selectedTaskIndex >= 0 &&
                selectedTaskIndex < static_cast<int>(filteredTaskCount)) {
                int realIdx = filteredTaskIndices[selectedTaskIndex];
                tasks[realIdx].isCompleted = !tasks[realIdx].isCompleted;
                if (tasks[realIdx].isCompleted) {
                    tasks[realIdx].completedYear = calYear;
                    tasks[realIdx].completedMonth = calMonth;
                    tasks[realIdx].completedDay = calDay;
                }
                LOG_PRINTF("[Input] Toggled task (filtered %d) real %d\n", selectedTaskIndex, realIdx);
                updateFilteredTasks();
                if (selectedTaskIndex >= filteredTaskCount && filteredTaskCount > 0) {
                    selectedTaskIndex = filteredTaskCount - 1;
                } else if (filteredTaskCount == 0) {
                    selectedTaskIndex = -1;
                }
                if (taskListTopIndex >= filteredTaskCount) {
                    taskListTopIndex = filteredTaskCount > 0 ? filteredTaskCount - 1 : 0;
                }
                saveTasks();
                uiDirty = true;
            }
            break;

        case SystemEventType::EVENT_TYPE_CHAR:
            // 'n'/'N' opens Add Task view (blocked when list is full)
            if (event.param == 'n' || event.param == 'N') {
                if (taskCount >= MAX_TASKS) {
                    // List is full — signal the UI to show feedback on next draw
                    LOG_PRINTLN("[Input] N - task list full, cannot add");
                    uiDirty = true; // Redraw so footer shows the "LIST FULL" status
                } else {
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
            }
            // 'q'/'Q' opens Quick Add view
            else if (event.param == 'q' || event.param == 'Q') {
                if (taskCount >= MAX_TASKS) {
                    LOG_PRINTLN("[Input] Q - task list full");
                    uiDirty = true;
                } else {
                    LOG_PRINTLN("[Input] Q - opening Quick Add view");
                    inputBuffer[0] = '\0';
                    inputBufferLen = 0;
                    currentState = SystemState::STATE_UI_QUICK_ADD;
                    uiDirty = true;
                }
            }
            // 'e'/'E' opens Edit Task view
            else if (event.param == 'e' || event.param == 'E') {
                if (selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(filteredTaskCount)) {
                    int realIdx = filteredTaskIndices[selectedTaskIndex];
                    LOG_PRINTF("[Input] E - opening Edit Task view for index %d\n", realIdx);
                    snprintf(inputBuffer, INPUT_BUFFER_SIZE, "%.*s", (int)sizeof(tasks[realIdx].title), tasks[realIdx].title);
                    inputBufferLen = strlen(inputBuffer);
                    taskEditField = 0;
                    taskEditHasDue = tasks[realIdx].hasDueDate;
                    taskEditYear = tasks[realIdx].dueYear > 0 ? tasks[realIdx].dueYear : calYear;
                    taskEditMonth = tasks[realIdx].dueMonth > 0 ? tasks[realIdx].dueMonth : calMonth;
                    taskEditDay = tasks[realIdx].dueDay > 0 ? tasks[realIdx].dueDay : calDay;
                    taskEditHour = tasks[realIdx].dueHour;
                    taskEditMinute = tasks[realIdx].dueMinute;
                    currentState = SystemState::STATE_UI_EDIT_TASK;
                    uiDirty = true;
                }
            }
            else if (event.param == 'v' || event.param == 'V') {
                if (currentTaskView == TaskViewMode::ACTIVE) currentTaskView = TaskViewMode::COMPLETED;
                else if (currentTaskView == TaskViewMode::COMPLETED) currentTaskView = TaskViewMode::BOTH;
                else currentTaskView = TaskViewMode::ACTIVE;
                updateFilteredTasks();
                selectedTaskIndex = (filteredTaskCount > 0) ? 0 : -1;
                taskListTopIndex = 0;
                uiDirty = true;
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

        case SystemEventType::EVENT_BACKSPACE:
            if (selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(filteredTaskCount)) {
                int realIdx = filteredTaskIndices[selectedTaskIndex];
                LOG_PRINTF("[Input] Backspace - deleting task %d\n", realIdx);
                removeLinkedEvent(tasks[realIdx].timestamp);
                for (uint32_t i = realIdx; i < taskCount - 1; ++i) {
                    tasks[i] = tasks[i + 1];
                }
                taskCount--;
                updateFilteredTasks();
                if (selectedTaskIndex >= static_cast<int>(filteredTaskCount) && filteredTaskCount > 0) {
                    selectedTaskIndex = static_cast<int>(filteredTaskCount) - 1;
                } else if (filteredTaskCount == 0) {
                    selectedTaskIndex = -1;
                }
                // Clamp scroll window after deletion
                if (taskListTopIndex > 0 && taskListTopIndex >= static_cast<int>(filteredTaskCount)) {
                    taskListTopIndex = static_cast<int>(filteredTaskCount) - 1;
                    if (taskListTopIndex < 0) taskListTopIndex = 0;
                }
                saveTasks();
                uiDirty = true;
            }
            break;

        default:
            break;
    }
}

static void handleUIAddTaskEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::EVENT_CANCEL:
            // ESC in ADD_TASK = cancel
            LOG_PRINTLN("[Input] ESC in ADD - canceling");
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
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
                    int finalHour = taskEditHour;
                    parseNaturalLanguageTime(inputBuffer, finalHour);
                    tasks[taskCount] = TaskItem(inputBuffer, false, millis(), taskEditHasDue, taskEditYear, taskEditMonth, taskEditDay, finalHour, taskEditMinute);
                    syncTaskToCalendar(tasks[taskCount]);
                    taskCount++;
                    LOG_PRINTF("[Input] Added task: %s (H:%d)\n", inputBuffer, finalHour);
                    updateFilteredTasks();
                    if (selectedTaskIndex < 0 && filteredTaskCount > 0) {
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
        case SystemEventType::EVENT_CANCEL:
            // ESC in EDIT_TASK = cancel
            LOG_PRINTLN("[Input] ESC in EDIT - canceling");
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
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
                if (inputBufferLen > 0 && selectedTaskIndex >= 0 && selectedTaskIndex < static_cast<int>(filteredTaskCount)) {
                    int realIdx = filteredTaskIndices[selectedTaskIndex];
                    snprintf(tasks[realIdx].title, sizeof(tasks[realIdx].title), "%.*s", (int)INPUT_BUFFER_SIZE, inputBuffer);
                    tasks[realIdx].hasDueDate = taskEditHasDue;
                    tasks[realIdx].dueYear = taskEditYear;
                    tasks[realIdx].dueMonth = taskEditMonth;
                    tasks[realIdx].dueDay = taskEditDay;
                    tasks[realIdx].dueHour = taskEditHour;
                    tasks[realIdx].dueMinute = taskEditMinute;
                    
                    syncTaskToCalendar(tasks[realIdx]);
                    LOG_PRINTF("[Input] Edited task %d: %s\n", realIdx, inputBuffer);
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
        case SystemEventType::EVENT_CANCEL:
            // ESC in ALIGN = return to LIST
            LOG_PRINTLN("[Input] ESC in ALIGN - returning to list");
            currentState = SystemState::STATE_UI_LIST;
            displayMgr.clearFullHardwareScreen();
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
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
        case SystemEventType::EVENT_CANCEL:
            LOG_PRINTLN("[Input] ESC in CALENDAR - returning to list");
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
            break;

        case SystemEventType::EVENT_NAV_UP:
            if (currentCalendarView == CalendarView::MONTH) {
                calDay -= 7;
                if (calDay < 1) {
                    calMonth--;
                    if (calMonth < 1) { calMonth = 12; calYear--; }
                    int prevDays = DisplayManager::getDaysInMonth(calYear, calMonth);
                    calDay = prevDays + calDay; 
                }
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
                int days = DisplayManager::getDaysInMonth(calYear, calMonth);
                calDay += 7;
                if (calDay > days) {
                    int extra = calDay - days;
                    calMonth++;
                    if (calMonth > 12) { calMonth = 1; calYear++; }
                    calDay = extra;
                }
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
            if (currentCalendarView == CalendarView::MONTH) {
                calDay--;
                if (calDay < 1) {
                    calMonth--;
                    if (calMonth < 1) { calMonth = 12; calYear--; }
                    calDay = DisplayManager::getDaysInMonth(calYear, calMonth);
                }
            } else {
                calDay--;
                if (calDay < 1) calDay = 1;
            }
            uiDirty = true;
            break;

        case SystemEventType::EVENT_NAV_RIGHT:
            if (currentCalendarView == CalendarView::MONTH) {
                int days = DisplayManager::getDaysInMonth(calYear, calMonth);
                calDay++;
                if (calDay > days) {
                    calMonth++;
                    if (calMonth > 12) { calMonth = 1; calYear++; }
                    calDay = 1;
                }
            } else {
                calDay++;
                if (calDay > 31) calDay = 31;
            }
            uiDirty = true;
            break;

        case SystemEventType::EVENT_SELECT:
            if (currentCalendarView == CalendarView::MONTH) {
                currentCalendarView = CalendarView::DAY;
                selectedEventIndex = 0;
            } else if (currentCalendarView == CalendarView::DAY) {
                if (selectedEventIndex >= 0) {
                    detailScrollY = 0;
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
        case SystemEventType::EVENT_CANCEL:
            currentState = SystemState::STATE_UI_CALENDAR;
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
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
        case SystemEventType::EVENT_CANCEL:
        case SystemEventType::EVENT_BACKSPACE:
            currentState = SystemState::STATE_UI_CALENDAR;
            uiDirty = true;
            break;
        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
            break;
        case SystemEventType::EVENT_NAV_UP:
            if (detailScrollY > 0) {
                detailScrollY -= 10; // Scroll by 10 pixels (one line height)
                uiDirty = true;
            }
            break;
        case SystemEventType::EVENT_NAV_DOWN:
            // Max scroll depends on content, but let's cap it at 100 for now
            // or we could calculate it in the draw function.
            if (detailScrollY < 120) {
                detailScrollY += 10;
                uiDirty = true;
            }
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
        case SystemEventType::EVENT_CANCEL:
            currentState = previousState;
            uiDirty = true;
            break;
        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
            break;
        default:
            break;
    }
}

static void handleUIQuickAddEvent(const SystemEvent& event) {
    switch (event.type) {
        case SystemEventType::EVENT_CANCEL:
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;

        case SystemEventType::SLEEP_REQ:
            enterSleepMode();
            break;

        case SystemEventType::EVENT_TYPE_CHAR: {
            int c = event.param;
            if (inputBufferLen < INPUT_BUFFER_SIZE - 1 && c >= 0x20 && c <= 0x7E) {
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

        case SystemEventType::EVENT_SELECT: {
            if (inputBufferLen > 0 && taskCount < MAX_TASKS) {
                int finalHour = 9; // Default
                parseNaturalLanguageTime(inputBuffer, finalHour);
                bool hasDue = (strstr(inputBuffer, "@") != nullptr);
                
                tasks[taskCount] = TaskItem(inputBuffer, false, millis(), hasDue, calYear, calMonth, calDay, finalHour, 0);
                syncTaskToCalendar(tasks[taskCount]);
                taskCount++;
                LOG_PRINTF("[QuickAdd] Added task: %s (H:%d)\n", inputBuffer, finalHour);
                updateFilteredTasks();
                saveTasks();
            }
            inputBuffer[0] = '\0';
            inputBufferLen = 0;
            currentState = SystemState::STATE_UI_LIST;
            uiDirty = true;
            break;
        }

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
    updateFilteredTasks();
    selectedTaskIndex = (filteredTaskCount > 0) ? 0 : -1;
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
            case SystemState::STATE_UI_QUICK_ADD:
                handleUIQuickAddEvent(event);
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

    // Process animations
    displayMgr.updateAnimations();
    if (displayMgr.isAnimating()) {
        uiDirty = true;
    }

    // Only redraw when state has actually changed
    if (uiDirty) {
        switch (currentState) {
            case SystemState::STATE_UI_LIST:
                displayMgr.drawActiveGUI(tasks, filteredTaskIndices, filteredTaskCount, selectedTaskIndex, taskListTopIndex, static_cast<int>(currentTaskView));
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
                            displayMgr.drawEventDetailGUI(calendarEvents[i], detailScrollY);
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
            case SystemState::STATE_UI_QUICK_ADD:
                displayMgr.drawQuickAddGUI(inputBuffer);
                break;
            default:
                break;
        }
        uiDirty = false;
    }

    delay(50);
}
