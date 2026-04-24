#pragma once

#include <cstdint>
#include <cstring>

enum class SystemState {
    STATE_UI_LIST,
    STATE_UI_ADD_TASK,
    STATE_UI_ALIGN,
    STATE_SLEEP,
    STATE_EPAPER_UPDATE
};

enum class SystemEventType {
    EVENT_NAV_UP,
    EVENT_NAV_DOWN,
    EVENT_NAV_LEFT,
    EVENT_NAV_RIGHT,
    EVENT_SELECT,
    EVENT_TYPE_CHAR,
    EVENT_BACKSPACE,
    SLEEP_REQ,
    WAKE_REQ
};

struct SystemEvent {
    SystemEventType type;
    int param;
};

struct TaskItem {
    char title[32];
    bool isCompleted;
    uint32_t timestamp;

    TaskItem() : isCompleted(false), timestamp(0) {
        title[0] = '\0';
    }
    TaskItem(const char* t, bool completed = false, uint32_t ts = 0)
        : isCompleted(completed), timestamp(ts) {
        if (t) {
            strncpy(title, t, 31);
            title[31] = '\0';
        } else {
            title[0] = '\0';
        }
    }
};

constexpr uint32_t MAX_TASKS = 20;
constexpr uint32_t EPAPER_VIEW_COUNT = 5;
constexpr uint64_t SLEEP_DURATION_US = 10 * 1000000ULL;
constexpr uint32_t INPUT_BUFFER_SIZE = 32;
