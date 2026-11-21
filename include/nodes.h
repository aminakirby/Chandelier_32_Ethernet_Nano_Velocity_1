#pragma once
#include <Arduino.h>
#include <FastLED.h>

//LED Parameters
#define DATA_PIN 3
#define NUM_LEDS 32                                                         //Maximum number of nodes (arrays are sized for this).
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

#define S_LATCH PD2                                                         //Solenoid shift register latch pin.

//Defined in nodes.cpp
extern CRGB RGBLED[NUM_LEDS];
extern uint8_t numLeds;                                                     //Number of nodes in use (8, 16 or 32).
extern uint8_t SPI_BYTES;                                                   //Number of solenoid shift registers in use (numLeds / 8).
extern uint8_t SHIFT_VAL[NUM_LEDS / 8];

//Defined in main.cpp
extern char packetBuffer[];
extern uint32_t TIME;

//LED functions
void LED_CHECK(uint8_t s);
void HUE_CHECK(uint8_t s);
void DECAY_CHECK(uint8_t s);
bool LED_ZERO_CHECK();
void LED_FADE();
void LED_DATAWRITE(bool ledWriteEnable);

//Solenoid functions
void SOL_CHECK(uint8_t s);
bool SOL_ZERO_CHECK();
void SOL_DATAWRITE(bool solWriteEnable);
