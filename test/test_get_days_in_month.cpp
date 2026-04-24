#include <Arduino.h>
#include <unity.h>
#include "display_mgr.h"

void test_getDaysInMonth_31_days() {
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 1));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 3));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 5));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 7));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 8));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 10));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 12));
}

void test_getDaysInMonth_30_days() {
    TEST_ASSERT_EQUAL(30, DisplayManager::getDaysInMonth(2023, 4));
    TEST_ASSERT_EQUAL(30, DisplayManager::getDaysInMonth(2023, 6));
    TEST_ASSERT_EQUAL(30, DisplayManager::getDaysInMonth(2023, 9));
    TEST_ASSERT_EQUAL(30, DisplayManager::getDaysInMonth(2023, 11));
}

void test_getDaysInMonth_february() {
    // Common year
    TEST_ASSERT_EQUAL(28, DisplayManager::getDaysInMonth(2023, 2));
    TEST_ASSERT_EQUAL(28, DisplayManager::getDaysInMonth(2100, 2));

    // Leap year
    TEST_ASSERT_EQUAL(29, DisplayManager::getDaysInMonth(2024, 2));
    TEST_ASSERT_EQUAL(29, DisplayManager::getDaysInMonth(2000, 2));
}

void test_getDaysInMonth_edge_cases() {
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 0));
    TEST_ASSERT_EQUAL(31, DisplayManager::getDaysInMonth(2023, 13));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_getDaysInMonth_31_days);
    RUN_TEST(test_getDaysInMonth_30_days);
    RUN_TEST(test_getDaysInMonth_february);
    RUN_TEST(test_getDaysInMonth_edge_cases);
    UNITY_END();
}

void loop() {}
