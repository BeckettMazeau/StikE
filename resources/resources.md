# Chosen Pins

### ePaper Display Pins
- CS/SS: 22
- DC: 23
- RST: 24
- BUSY: 25
- SCK: 18
- MISO: NC
- MOSI: 16




# Header File Notes
In prog1.h:
```cpp

    // Source - https://stackoverflow.com/a/25757789
    // Posted by Null, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-04-14, License - CC BY-SA 3.0

    #include <iostream> // and whatever other libraries you need to include

    #define ARRAY_SIZE 100 // and other defines

    // Function declarations

    // Read number from file, return int
    void read_number(int array[ARRAY_SIZE][ARRAY_SIZE]);

    char assign_char(int n);

    void print_array(int array[ARRAY_SIZE][ARRAY_SIZE]);

    void neighbor_check(int array[ARRAY_SIZE][ARRAY_SIZE]);

```

In prog1.cpp:
```cpp

    // Source - https://stackoverflow.com/a/25757789
    // Posted by Null, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-04-14, License - CC BY-SA 3.0

    // included headers, defines, and functions declared in prog1.h are visible
    #include "prog1.h"

    void read_number(int array[ARRAY_SIZE][ARRAY_SIZE]) {
        // implementation here
    }

    char assign_char(int n) {
        char c;
        // implementation here
        return c;

    }

    void print_array(int array[ARRAY_SIZE][ARRAY_SIZE]) {
        // implementation here
    }

    void neighbor_check(int array[ARRAY_SIZE][ARRAY_SIZE]) {
        // implementation here
    }

    // not visible to anything that #includes prog1.h
    // since it is not declared in prog1.h
    void foo() {
        // implementation here
    }

```


# Useful Links
[Arduino Coding Basics - GeeksforGeeks](https://www.geeksforgeeks.org/electronics-engineering/arduino-coding-basics/)

[VS Code PlatformIO IDE for ESP32/ESP8266 - RandomNerdTutorials](https://randomnerdtutorials.com/vs-code-platformio-ide-esp32-esp8266-arduino/#3)

[TFT_SdFat Library by Bodmer on GitHub](https://github.com/Bodmer/TFT_SdFat)

[Good ePaper Display for Arduino - Arduino Forum](https://forum.arduino.cc/t/good-display-epaper-for-arduino/419657)

[ESP32-S3 GPIO Pins Guide - Luis Llamas](https://www.luisllamas.es/en/which-pins-can-i-use-on-esp32-s3/)

[Header Files in C++](https://www.geeksforgeeks.org/cpp/header-files-in-c-c-with-examples/)

### ESP32-S3 Safe Pins Table
[Which pins can I use on an ESP32-S3](https://www.luisllamas.es/en/which-pins-can-i-use-on-esp32-s3/).
| GPIO | Name | Functions | Notes |
| :--- | :--- | :--- | :--- |
| **1** | GPIO1 | RTC_GPIO1, TOUCH1, ADC1_CH0 | ✔️ Safe for general use |
| **2** | GPIO2 | RTC_GPIO2, TOUCH2, ADC1_CH1 | ✔️ Safe for general use |
| **4** | GPIO4 | RTC_GPIO4, TOUCH4, ADC1_CH3 | ✔️ Safe for general use |
| **5** | GPIO5 | RTC_GPIO5, TOUCH5, ADC1_CH4 | ✔️ Safe for general use |
| **6** | GPIO6 | RTC_GPIO6, TOUCH6, ADC1_CH5 | ✔️ Safe for general use |
| **7** | GPIO7 | RTC_GPIO7, TOUCH7, ADC1_CH6 | ✔️ Safe for general use |
| **8** | GPIO8 | RTC_GPIO8, TOUCH8, ADC1_CH7 | ✔️ Safe for general use |
| **9** | GPIO9 | RTC_GPIO9, TOUCH9, ADC1_CH8, FSPIHD | ✔️ Safe for general use |
| **10** | GPIO10 | RTC_GPIO10, TOUCH10, ADC1_CH9, FSPICS0, FSPIIO4 | ✔️ Safe for general use |
| **11** | GPIO11 | RTC_GPIO11, TOUCH11, ADC2_CH0, FSPID, FSPIIO5 | ✔️ Safe for general use |
| **12** | GPIO12 | RTC_GPIO12, TOUCH12, ADC2_CH1, FSPICLK, FSPIIO6 | ✔️ Safe for general use |
| **13** | GPIO13 | RTC_GPIO13, TOUCH13, ADC2_CH2, FSPIQ, FSPIIO7 | ✔️ Safe for general use |
| **14** | GPIO14 | RTC_GPIO14, TOUCH14, ADC2_CH3, FSPIWP, FSPIDQS | ✔️ Safe for general use |
| **15** | GPIO15 | RTC_GPIO15, U0RTS, ADC2_CH4, XTAL_32K_P | ✔️ Safe for general use |
| **16** | GPIO16 | RTC_GPIO16, U0CTS, ADC2_CH5, XTAL_32K_N | ✔️ Safe for general use |
| **17** | GPIO17 | RTC_GPIO17, U1TXD, ADC2_CH6, DAC_1 | ✔️ Safe for general use |
| **18** | GPIO18 | RTC_GPIO18, U1RXD, ADC2_CH7, DAC_2, CLK_OUT3 | ✔️ Safe for general use |
| **21** | GPIO21 | RTC_GPIO21 | ✔️ Safe for general use |
| **22** | GPIO22 | GPIO22 | ✔️ Safe for general use |
| **23** | GPIO23 | GPIO23 | ✔️ Safe for general use |
| **24** | GPIO24 | GPIO24 | ✔️ Safe for general use |
| **25** | GPIO25 | GPIO25 | ✔️ Safe for general use |
| **38** | GPIO38 | GPIO38, FSPIWP | ✔️ Safe for general use |


