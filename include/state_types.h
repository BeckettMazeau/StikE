#pragma once

#include <cstdint>
#include <cstring>

enum class SystemState {
    STATE_ACTIVE,
    STATE_SLEEP,
    STATE_EPAPER_UPDATE
};

enum class SystemEventType {
    KEY_PRESS,
    SLEEP_REQ,
    WAKE_REQ,
    TASK_ADDED,
    TASK_TOGGLED
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