# AI Implementation Prompts for StikE

This document contains structured prompts that can be provided to an AI coding agent to implement specific feature enhancements for the StikE project.

---

## 1. Premium Visual Overhaul
**Prompt:**
> "Refactor the `DisplayManager` in `src/display_mgr.cpp` to elevate the UI aesthetics. 
> 1. Replace solid header/footer colors with vertical linear gradients using `guiSprite->drawGradientLine` or manual pixel-strips to create depth. 
> 2. Implement a 'Smooth Scroll' system for the Task List: instead of an instant highlight jump, add a float-based target Y for the selection bar and use an easing function in the main loop to slide the bar to its new position.
> 3. Enable Smooth Fonts: modify `initBusesAndDisplays` to load a high-quality VLW font from SPIFFS/LittleFS (ensure it matches the 'Inter' or 'Outfit' aesthetic) and update all `draw` methods to use this font instead of the default `setTextFont(1)`."

## 2. E-Paper Information Density
**Prompt:**
> "Optimize the ePaper rendering logic in `DisplayManager::drawEpaperView`. 
> 1. Redesign the layout to support up to 6 items per screen by reducing the baseline 'stride' and using a smaller font (TextSize 1) for metadata like due dates or notes.
> 2. Replace the dash bullet `-` with custom bitmapped icons for Tasks and Calendar Events. 
> 3. Implement a 'Bento Grid' view for the first ePaper screen: the top half should show the single most urgent upcoming item in a large, bold frame, while the bottom half shows a 2-column grid of smaller tiles for secondary items."

## 3. Navigation Hardening & Quick-Add
**Prompt:**
> "Update the system state machine in `src/main.cpp` to improve navigation consistency.
> 1. Standardize 'ESC' (0x1B) to always act as a 'Cancel' or 'Return to Previous State' action across all UI modes, and reserve 'Backspace' (0x08) exclusively for character deletion in text fields.
> 2. Create a 'Quick Add' mode: when 'Q' is pressed in the List view, open a simplified version of `STATE_UI_ADD_TASK` that contains only the Title field. Upon pressing Enter, save the task immediately with default settings.
> 3. Add simple natural language parsing to the Task Title input: if the string contains '@' followed by a time (e.g., '@5pm'), automatically extract and set the due-hour field to 17."

## 4. System Status & RTC Integration
**Prompt:**
> "Implement a global system status bar in the `drawActiveGUI` and `drawCalendarGUI` methods.
> 1. Reserve 10px at the top of the header for a status row.
> 2. Add a battery icon that reflects the voltage from the appropriate GPIO (use `analogRead`).
> 3. Integrate an RTC (Real Time Clock) or NTP sync routine: update the system time on boot and ensure the header displays the current HH:MM.
> 4. Update the '24-hour' logic in `display_mgr.cpp` to use the actual system clock instead of the current year-2000 sentinel value."

## 5. Persistence & Performance
**Prompt:**
> "Optimize the NVS storage and performance in `src/main.cpp`.
> 1. Refactor `saveTasks` and `loadTasks` to use a single binary blob for all tasks instead of individual `prefs.putString` calls for every field; this will reduce NVS overhead and improve save/load speeds.
> 2. Implement 'Dirty Rect' rendering in `DisplayManager`: instead of pushing the full 160x128 sprite on every frame, track which rows have changed and only push those specific areas to the TFT to increase the effective frame rate."

