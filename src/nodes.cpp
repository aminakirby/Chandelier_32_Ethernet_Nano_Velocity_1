#include <Arduino.h>
#include <SPI.h>
#include "nodes.h"

CRGB RGBLED[NUM_LEDS];
uint8_t numLeds = NUM_LEDS;                                                 //Number of nodes in use (8, 16 or 32), set from the web UI. To use with for loops.

//Solenoid Parameters
uint8_t SPI_BYTES = NUM_LEDS / 8;                                           //Number of solenoid shift registers in use (numLeds / 8).
const uint8_t SOL_NUM[] = {                                                 //Solenoid header bytes.
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 
  'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 
  '\'', '&', '/', '<', '>', '?', '!', '1', 
  '2', '3', '4', '5', '6', '7', '8', '9'
};
uint8_t SHIFT_VAL[NUM_LEDS / 8];                                             //Records last on/off data for each of the 8 solenoids to compare with incoming data.
const uint8_t MULT[] = {1, 2, 4, 8, 16, 32, 64, 128};                       //Bitmask used to write on/off data to corresponding bit of the byte shifted out to the solenoid shift register.
bool SOL_EN[NUM_LEDS];                                                      //Records if solenoid has been turned on or not.
uint32_t SOL_TIME[NUM_LEDS];                                                //Records how long solenoid has been engaged for.
uint8_t SOL_VELOCITY[NUM_LEDS];                                             //Records solenoid "velocity", i.e. how long in milliseconds it is engaged for until disengaged.
uint8_t SOL_VELOCITY_SCALING[NUM_LEDS] = {                                  //Used for setting the maximum on/off time for each solenoid so that max velocity = max amplitude. Max values(ms): 1=127, 2=64, 3=42, 4=32, 5=25, 6=21.
  4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4
};
bool SOL_WRITE_EN;                                                          //Enabled when any solenoid on/off value changes.

//LED Parameters
const uint8_t LED_NUM[] = {                                                 //LED header bytes.
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 
  'Q', 'S', 'T', 'U', '~', '+', '=', '-', 
  ':', '|', '{', '}', '[', ']', '`', '.'
};
uint8_t LED_VAL[NUM_LEDS];                                                  //LED brightness.
uint8_t LED_HUE[NUM_LEDS];                                                  //LED color.
uint8_t LED_WHITE[NUM_LEDS];                                                //LED white- needed because white isn't contained within the hue parameter of FastLED CHSV command.
const uint8_t HUE_NUM[] = {'q', 'r', 's', 't', 'u', 'v', 'w', 'x'};         //Hue header bytes.
const uint8_t HUE_VAL[] = {0, 20, 45, 86, 120, 160, 200, 240};              //Color numbers in hue- red, orange, yellow, green, blue, indigo, violet, pink.
uint8_t HUE = 0;
const uint8_t WHITE_NUM = 'z';                                              //White enable header bytes
uint8_t WHITE_VAL = 0;                                                      //White amount (0-255)
const uint8_t DECAY_NUM[] = {'V', 'W', 'X', 'Y', 'Z'};                      //Decay header bytes
const uint8_t DECAY_VAL[] = {2, 5, 10, 25, 40};                             //Sets speed at which LEDs fade out after being triggered.
uint8_t DECAY = 10;                                                         //Default decay is 10 milliseconds per fade increment.
bool RAINBOW_EN = 0;
const uint8_t RAINBOW_NUM = 'y';                                            //RAINBOW_EN enable header byte.
uint32_t RAINBOW_TIME = 0;                                                  //If RAINBOW_EN is enabled, changes HUE values every 40 milliseconds.
uint32_t FADE_TIME = 0;                                                     //For comparing with TIME in if() statement for each LED fade increment.
uint32_t LED_EN_TIME = 0;                                                   //Disables LED_DATAWRITE() after 5ms of all LED brightness values at 0.

//Functions
void LED_CHECK(uint8_t s){                                                  //Turns on a specific LED.
  for(int q = 0; q < numLeds; q ++){
    if(packetBuffer[s] == LED_NUM[q]){
      packetBuffer[s] = 0;
      LED_VAL[q] = 250;                                                     //Sets corresponding LED to 250.
      LED_HUE[q] = HUE;
      LED_WHITE[q] = WHITE_VAL;
      return;        
    };
  };
}
void HUE_CHECK(uint8_t s){                                                  //Changes LED colors based on header bytes
    for(int g = 0; g < 8; g ++){
      if(packetBuffer[s] == HUE_NUM[g]){
        packetBuffer[s] = 0;
        WHITE_VAL = 255;
        RAINBOW_EN = 0;
        HUE = HUE_VAL[g];
        return;        
      };
    };
    if(packetBuffer[s] == WHITE_NUM){
      packetBuffer[s] = 0;
      WHITE_VAL = 0;
      RAINBOW_EN = 0;
      return;
    };
    if(packetBuffer[s] == RAINBOW_NUM){
      packetBuffer[s] = 0;
      WHITE_VAL = 255;
      RAINBOW_EN = 1;
      RAINBOW_TIME = TIME;
      return;
    };
}
void DECAY_CHECK(uint8_t s){                                                //Changes LED brightness decay time based on header bytes
  for(int w = 0; w < 5; w ++){
      if(packetBuffer[s] == DECAY_NUM[w]){
        packetBuffer[s] = 0;
        DECAY = DECAY_VAL[w];
        return;        
      };
  };
}
bool LED_ZERO_CHECK(){                                                      //If all LED values are set to zero, the program stops writing to the LEDs
  uint32_t LED_VAL_TOTAL = 0;
  for(int y = 0; y < numLeds; y ++){
    LED_VAL_TOTAL += LED_VAL[y];
  };
  if(LED_VAL_TOTAL != 0){ 
    LED_EN_TIME = TIME; 
    return true;
  }
  else if(TIME - LED_EN_TIME >= 5){return false;}
  else{return true;};
}
void LED_FADE(){                                                            //Fades LEDs over time based on DECAY value
  if(TIME - FADE_TIME >= DECAY){
    FADE_TIME = TIME;
    for(int t = 0; t < numLeds; t ++){
      if(LED_VAL[t] != 0){
        if(LED_VAL[t] > 0){
          LED_VAL[t] -= 10;
        }
        else{LED_VAL[t] = 0;};
      };
    };
  };

  if(RAINBOW_EN && TIME - RAINBOW_TIME >= 40){                              //Makes HUE change over time if RAINBOW_EN == true.
    RAINBOW_TIME = TIME;
    HUE += 1; 
  }
  return;
};
void LED_DATAWRITE(bool ledWriteEnable){                                    //Writes new LED values to LEDs if values have changed within the last 5ms.
  if(ledWriteEnable){
    for(int x = 0; x < numLeds; x ++){
      RGBLED[x] = CHSV(LED_HUE[x], LED_WHITE[x], LED_VAL[x]);
    };
    FastLED.show();
  }
};
void SOL_CHECK(uint8_t s){                                                  //Turns on solenoids based on header bytes and sets velocity for each solenoid.
  for(int q = 0; q < SPI_BYTES; q ++){                     
    for(int v = 0; v < 8; v ++){
      uint8_t solNumber = v + (q * 8);
      if(packetBuffer[s] == SOL_NUM[solNumber]){
        SOL_TIME[solNumber] = TIME;
        uint8_t velocity = 0;                                               //Velocity comes in hex as 2 bytes of packetBuffer per note, which are collected in this int.
        for(int t = 1; t < 3; t++){
          if(packetBuffer[s+t] >= 97){packetBuffer[s+t] -= 39;};            //For hex values a-f, subtracts 39 so that it converts with other ASCII values in next line.
          velocity += ((packetBuffer[s+t] - 48) * pow(16,(2-t)));           //ASCII 0 = 48, so 48 is subtracted to convert velocity values back to integers.
          packetBuffer[s+t]='_';                                            //Sets value to placeholder after adding to velocity.
        }
        SOL_VELOCITY[solNumber] = velocity/SOL_VELOCITY_SCALING[solNumber]; //Assigns velocity value to specific solenoid and adds velocity scaling.
        packetBuffer[s] = 0;                                                //Removes header byte.  
        if(SHIFT_VAL[q] ^ MULT[v] && !SOL_EN[solNumber]){                   //Turns on corresponding solenoid if it is off.
          SHIFT_VAL[q] ^= MULT[v];                   
          SOL_EN[solNumber] = 1;
          if(!SOL_WRITE_EN){SOL_WRITE_EN = 1;};
          return;
        }
        else{return;};                                                       //Or ends the for statement.
      };
    };
  };
}
bool SOL_ZERO_CHECK(){                                                      //Turns off solenoids after an amount of time specified by solenod velocity.
  for(int l = 0; l < SPI_BYTES; l ++){                       
    for(int w = 0; w < 8; w ++){
      uint8_t solNumber = w + (l * 8);
      if(SOL_EN[solNumber] && TIME - SOL_TIME[solNumber] >= SOL_VELOCITY[solNumber]){
        if(SHIFT_VAL[l] & MULT[w]){                        
          SHIFT_VAL[l] ^= MULT[w];                   
          SOL_EN[solNumber] = 0;
          if(!SOL_WRITE_EN){SOL_WRITE_EN = 1;};
        }
      }
    }
  }
  if(SOL_WRITE_EN){return true;}
  else{return false;};
}
void SOL_DATAWRITE(bool solWriteEnable){                                    //Writes solenoid values to shift register
  if(solWriteEnable){
    SOL_WRITE_EN = 0;
    for(int v = 0; v < 4; v ++){
      SPI.transfer(SHIFT_VAL[3 - v]);
    };
    PORTD |= 1<<S_LATCH;                                                    //New data pushed to outputs with latch
    PORTD &= ~(1<<S_LATCH);
  };
}
