Here are the three high-impact, low-risk structural improvements identified to reduce redundancy and consolidate the codebase:

### 1. Redundant Imports
**File Path(s):** `src/main.cpp`
**The Issue:** There is a duplicated `#include <WiFi.h>` directive at the top of `src/main.cpp`. This clutters the compilation environment and provides no additional value.
**Proposed Fix:** Remove the second inclusion of `<WiFi.h>`.
```diff
<<<<<<< SEARCH
#include <Arduino.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <esp_log.h>
#include <Preferences.h>
#include <WiFi.h>
=======
#include <Arduino.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <esp_log.h>
#include <Preferences.h>
>>>>>>> REPLACE
```
**Risk Assessment:** Zero risk. The compiler automatically handles redundant includes via header guards anyway, but removing it cleans up the source file and prevents confusion. No behavior will change.

### 2. Fragmented Utilities (Text Wrapping)
**File Path(s):** `src/display_mgr.cpp`
**The Issue:** Inside `DisplayManager::drawEventDetailGUI`, the exact same string chunking and word-wrapping logic is duplicated three times sequentially for parsing the `event.title`, `event.location`, and `event.notes`.
**Proposed Fix:** Consolidate this repeated block into a single private helper function `drawWrappedText` within `DisplayManager` that takes the string pointer, maximum characters per line, starting Y position, and text color.
```cpp
// Helper addition instruction:
int DisplayManager::drawWrappedText(int startY, const char* text, int maxChars, uint16_t color) {
    int len = strnlen(text, 64); // max buffer
    int pos = 0;
    int curY = startY;
    while (pos < len) {
        char line[64];
        int take = (len - pos > maxChars) ? maxChars : (len - pos);
        if (take > (int)sizeof(line) - 1) take = sizeof(line) - 1;
        strncpy(line, text + pos, take);
        line[take] = '\0';
        drawBodyText(4, curY, line, color);
        curY += 10;
        pos += take;
    }
    return curY;
}
```
Replace the three identical while-loops in `drawEventDetailGUI` with calls to this helper.
**Risk Assessment:** Very low risk. The helper abstracts identical pointer arithmetic and `strncpy` usage without altering the external display API.

### 3. Duplicate Logic (`strncpy` Truncation Blocks)
**File Path(s):** `src/display_mgr.cpp` and `src/main.cpp`
**The Issue:** There is pervasive, near-identical repetitive logic used to safely copy strings into fixed buffers while manually null-terminating them.
For example, in `src/main.cpp` lines 282-286:
```cpp
    strncpy(wifiSSID, s.c_str(), INPUT_BUFFER_SIZE); wifiSSID[INPUT_BUFFER_SIZE-1] = '\0';
    s = prefs.getString("wifiPass", "");
    strncpy(wifiPassword, s.c_str(), INPUT_BUFFER_SIZE); wifiPassword[INPUT_BUFFER_SIZE-1] = '\0';
    s = prefs.getString("gcalURL", "");
    strncpy(gcalURL, s.c_str(), INPUT_BUFFER_SIZE); gcalURL[INPUT_BUFFER_SIZE-1] = '\0';
```
And in `src/display_mgr.cpp` lines 381 and 389:
```cpp
strncpy(trunc, item.task.title, 15); trunc[15] = '\0';
// ...
strncpy(trunc, item.event.title, 15); trunc[15] = '\0';
```
**Proposed Fix:** Replace these manual, repetitive `strncpy` + null-termination pairs with the safer `snprintf` pattern that is already known to the memory context (e.g., `snprintf(dest, sizeof(dest), "%.*s", (int)sizeof(src), src)` or simply `snprintf(dest, sizeof(dest), "%s", src)`). This removes the repetitive manual array indexing.
**Risk Assessment:** Low risk. `snprintf` inherently guarantees null-termination, which actually decreases the risk of out-of-bounds reads compared to manual pointer/array indexing.
