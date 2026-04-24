# `include/icons.h`

This header file contains the bitmap data for icons used in the StikE graphical user interface. The icons are stored in PROGMEM (program memory/flash) to save RAM.

## Key Components

- **`task_icon`**: A 16x16 pixel icon representing a task (a checkbox, either with a checkmark or empty). It is defined as a byte array (`const unsigned char task_icon[] PROGMEM`).
- **`event_icon`**: A 16x16 pixel icon representing an event (a clock face). It is defined as a byte array (`const unsigned char event_icon[] PROGMEM`).

## Dependencies
- `<Arduino.h>`: The Arduino core library, needed for `PROGMEM` and standard types.
