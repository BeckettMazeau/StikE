// GxEPD2_Example.h - E-Paper Display Example Header
// Header Guard
#pragma once

// Include libraries
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

// ============================================================================
// IO PIN CONFIGURATION
// ============================================================================
// GPIO Pin assignments for e-paper display module
extern int BUSY_Pin;    // Busy pin (Input) - A14
extern int RES_Pin;     // Reset pin (Output) - A15
extern int DC_Pin;      // Data/Command pin (Output) - A16
extern int CS_Pin;      // Chip Select pin (Output) - A17

// ============================================================================
// MACRO DEFINES FOR GPIO CONTROL
// ============================================================================
// SCLK -- GPIO23
// MOSI -- GPIO18

#define EPD_W21_CS_0   digitalWrite(CS_Pin, LOW)
#define EPD_W21_CS_1   digitalWrite(CS_Pin, HIGH)
#define EPD_W21_DC_0   digitalWrite(DC_Pin, LOW)
#define EPD_W21_DC_1   digitalWrite(DC_Pin, HIGH)
#define EPD_W21_RST_0  digitalWrite(RES_Pin, LOW)
#define EPD_W21_RST_1  digitalWrite(RES_Pin, HIGH)
#define isEPD_W21_BUSY digitalRead(BUSY_Pin)

// ============================================================================
// FUNCTION PROTOTYPES - SPI & DATA HANDLING
// ============================================================================
void SPI_Write(unsigned char value);
void Epaper_Write_Command(unsigned char command);
void Epaper_Write_Data(unsigned char data);

// ============================================================================
// FUNCTION PROTOTYPES - HARDWARE CONTROL
// ============================================================================
void Epaper_HW_SW_RESET(void);                    // Hardware/software reset
void EPD_HW_Init(void);                           // Electronic paper initialization (full display)
void EPD_HW_Init_Fast(void);                      // Fast initialization (no background color)
void EPD_HW_Init_GUI(void);                       // EPD init for GUI usage
void EPD_HW_Init_P(void);                         // Partial refresh initialization
void EPD_Standby(void);                           // Standby mode
void Epaper_READBUSY(void);                       // Wait until display is ready (busy flag cleared)

// ============================================================================
// FUNCTION PROTOTYPES - DISPLAY UPDATE & CONTROL
// ============================================================================
void EPD_Update(void);                            // Update full display
void EPD_Update_Fast(void);                       // Fast update (no border wavefrom change)
void EPD_Part_Update(void);                       // Partial refresh update

// ============================================================================
// FUNCTION PROTOTYPES - SCREEN CONTENT
// ============================================================================
void EPD_WhiteScreen_White(void);                 // Fill screen with white
void EPD_WhiteScreen_Black(void);                 // Fill screen with black
void EPD_WhiteScreen_ALL(const unsigned char *datas);        // Write full screen data (white/black contrast)
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas);   // Fast full screen write
void EPD_SetRAMValue_BaseMap(const unsigned char *datas);    // Set base map for partial refresh

// ============================================================================
// FUNCTION PROTOTYPES - PARTIAL DISPLAY (LOCAL REFRESH)
// ============================================================================
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start, 
                  const unsigned char * datas, 
                  unsigned int PART_COLUMN, unsigned int PART_LINE);
                                                        
void EPD_Dis_Part_myself(unsigned int x_startA, unsigned int y_startA, const unsigned char * datasA,
                         unsigned int x_startB, unsigned int y_startB, const unsigned char * datasB,
                         unsigned int x_startC, unsigned int y_startC, const unsigned char * datasC,
                         unsigned int x_startD, unsigned int y_startD, const unsigned char * datasD,
                         unsigned int x_startE, unsigned int y_startE, const unsigned char * datasE,
                         unsigned int PART_COLUMN, unsigned int PART_LINE);

// ============================================================================
// FUNCTION PROTOTYPES - DEEP SLEEP
// ============================================================================
void EPD_DeepSleep(void);                         // Enter deep sleep mode

// ============================================================================
// MAIN EXAMPLE ENTRY POINT
// ============================================================================
/**
 * @brief Main example function that runs the complete e-paper demo
 * 
 * This function encapsulates all initialization, display cycles, and cleanup.
 * Call this from main.cpp to run the full example.
 */
void example(void);