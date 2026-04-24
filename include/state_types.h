#pragma once

#include <cstdint>
#include <cstring>

enum class SystemState {
    STATE_UI_LIST,
    STATE_UI_ADD_TASK,
    STATE_UI_ALIGN,
    STATE_UI_CALENDAR,
    STATE_UI_ADD_EVENT,
    STATE_UI_EDIT_TASK,
    STATE_UI_EVENT_DETAIL,
    STATE_UI_HELP,
    STATE_UI_QUICK_ADD,
    STATE_UI_SETTINGS,
    STATE_SLEEP,
    STATE_EPAPER_UPDATE
};

enum class CalendarView {
    MONTH,
    WEEK,
    DAY
};

struct CalendarEvent {
    char title[24];
    char notes[64];
    char location[32];
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint16_t duration; // in minutes
    uint32_t linkedTaskId; // 0 if not linked to a task

    CalendarEvent() : year(0), month(0), day(0), hour(0), minute(0), duration(0), linkedTaskId(0) {
        title[0] = '\0';
        notes[0] = '\0';
        location[0] = '\0';
    }

    CalendarEvent(const char* t, uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint16_t dur, const char* n = "", const char* l = "", uint32_t ltid = 0)
        : year(y), month(m), day(d), hour(h), minute(min), duration(dur), linkedTaskId(ltid) {
        if (t) { strncpy(title, t, 23); title[23] = '\0'; } else title[0] = '\0';
        if (n) { strncpy(notes, n, 63); notes[63] = '\0'; } else notes[0] = '\0';
        if (l) { strncpy(location, l, 31); location[31] = '\0'; } else location[0] = '\0';
    }
};

enum class SystemEventType {
    EVENT_NAV_UP,
    EVENT_NAV_DOWN,
    EVENT_NAV_LEFT,
    EVENT_NAV_RIGHT,
    EVENT_SELECT,
    EVENT_TYPE_CHAR,
    EVENT_BACKSPACE,
    EVENT_CANCEL,
    SLEEP_REQ,
    WAKE_REQ
};

enum class TaskViewMode {
    ACTIVE,
    COMPLETED,
    BOTH
};

struct SystemEvent {
    SystemEventType type;
    int param;
};

struct TaskItem {
    char title[32];
    bool isCompleted;
    uint32_t timestamp;
    uint16_t completedYear;
    uint8_t completedMonth;
    uint8_t completedDay;
    bool hasDueDate;
    uint16_t dueYear;
    uint8_t dueMonth;
    uint8_t dueDay;
    uint8_t dueHour;
    uint8_t dueMinute;

    TaskItem() : isCompleted(false), timestamp(0), completedYear(0), completedMonth(0), completedDay(0), hasDueDate(false), dueYear(0), dueMonth(0), dueDay(0), dueHour(0), dueMinute(0) {
        title[0] = '\0';
    }
    TaskItem(const char* t, bool completed = false, uint32_t ts = 0, bool hasDue = false, uint16_t dy = 0, uint8_t dm = 0, uint8_t dd = 0, uint8_t dh = 0, uint8_t dmin = 0)
        : isCompleted(completed), timestamp(ts), completedYear(0), completedMonth(0), completedDay(0), hasDueDate(hasDue), dueYear(dy), dueMonth(dm), dueDay(dd), dueHour(dh), dueMinute(dmin) {
        if (t) {
            snprintf(title, sizeof(title), "%s", t);
        } else {
            title[0] = '\0';
        }
    }
};

constexpr uint32_t MAX_TASKS = 20;
constexpr uint32_t MAX_CALENDAR_EVENTS = 50;

enum class EpaperViewType {
    TASK,
    EVENT
};

struct EpaperItem {
    EpaperViewType type;
    TaskItem task;
    CalendarEvent event;

    EpaperItem() : type(EpaperViewType::TASK) {}
};

constexpr uint32_t ITEMS_PER_EPAPER_SCREEN = 6;

struct EpaperViewItem {
    EpaperItem items[ITEMS_PER_EPAPER_SCREEN];
    uint8_t itemCount;

    EpaperViewItem() : itemCount(0) {}
};

constexpr uint32_t EPAPER_VIEW_COUNT = 10;
constexpr uint64_t SLEEP_DURATION_US = 10 * 1000000ULL;
constexpr uint32_t INPUT_BUFFER_SIZE = 128;
