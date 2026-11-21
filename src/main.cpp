#include <Arduino.h>
#include <avr/io.h>
#include <SPI.h>       
#include <Ethernet2.h>
#include <avr/wdt.h>
#include "nodes.h"
#include "web.h"

#define UDP_TX_PACKET_MAX_SIZE numLeds * 4                                  //Set to 32 for 8 node devices, 64 for 16 node and 128 for 32 node.

//Control Pins
#define W5500_EN PD7
#define RESET PD5
#define OE PD4

//Ethernet Parameters
EthernetUDP Udp;
uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xCE};                      //Default network settings, replaced at startup by settings saved from the web UI.
IPAddress ip(10, 0, 0, 50);
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(10, 0, 0, 1);
const uint32_t localPort = 6000;
char packetBuffer[NUM_LEDS * 4 + 1];                                        //Buffer to hold incoming packet (sized for the maximum number of nodes).

//Timer Parameters
uint32_t TIME;                                                              //Global time in milliseconds.

//Functions
bool UDP_COLLECT(){                                                         //Reads incoming UDP data and stores it into packetBuffer.
  if(Udp.parsePacket()){
    int n = Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[n] = 0;
    return true;
  }
  else{return false;};
}
void UDP_STARTUP(){                                                         //Waits for initial UDP packet to arrive before continuing loop.
  bool LED_STARTUP = 0;
  bool STARTUP = 0;

  while(!STARTUP){
    WEB_HANDLE();                                                           //Web UI stays usable while waiting for the first UDP packet.

    if(!LED_STARTUP){
      LED_STARTUP = 1;
      for(int e = 0; e < numLeds; e ++){                                    //LEDs red while waiting for first UDP packet to arrive.
        RGBLED[e] = CHSV(0, 255, 50);
      }
      FastLED.show();
    };

    if(Udp.parsePacket()){                                                  //LEDs blink green upon first UDP packet arrival.
      STARTUP = 1;
      for(int e = 0; e < numLeds; e ++){
        RGBLED[e] = CHSV(86, 255, 50);
      };
      FastLED.show();
      delay(250);
      for(int e = 0; e < numLeds; e ++){
        RGBLED[e] = CHSV(0, 0, 0);
      };
      FastLED.show();
    };
  };
}

void setup() {
  MCUSR = 0;                                                                //Stops the watchdog after a web UI restart so it doesn't keep resetting.
  wdt_disable();
  NET_LOAD();                                                               //Uses settings saved from the web UI (network and number of nodes), if any.

  SPI.setClockDivider(SPI_CLOCK_DIV2);                                      //Sets SPI clock to 8MHz.
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.begin();

  for(int j = 0; j < UDP_TX_PACKET_MAX_SIZE + 1; j++){                      //Sets all packetBuffer values to 0.
    packetBuffer[j] = 0;
  };

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(RGBLED, numLeds);

  DDRD = DDRD | 0b10110100;                                                 //Sets output pins.
  PORTD = PORTD | 10110100;

  PORTD &= ~(1<<S_LATCH);                                                   //Clears shift registers before enabling them.
  SOL_DATAWRITE(true);
  LED_DATAWRITE(true);

  PORTD |= 1<<W5500_EN;                                                     //Enables W5500 Ethernet IC.
  Ethernet.begin(mac, ip, gateway, gateway, subnet);                        //Starts Ethernet (mac, IP, DNS, gateway, subnet). DNS is unused; set to the gateway as before.
  Udp.begin(localPort);
  webServer.begin();

  PORTD &= ~(1<<RESET);
  PORTD &= ~(1<<OE);                                                        //Turns on shift registers.

  UDP_STARTUP();                                                            //Waits until 1st UDP packet is received before continuing to loop().
}

void loop() {
  TIME = millis();

  if(UDP_COLLECT()){                                                        //Processes data available in packetBuffer.
    for(int s = 0; s < (UDP_TX_PACKET_MAX_SIZE + 1); s ++){
      if(packetBuffer[s] <= 32){break;}                                     //Breaks for loop if packetBuffer receives a non-valid command or null value.
      else if(packetBuffer[s] == '_'){packetBuffer[s] = 0;}                 //Placeholder byte- can be used for pinging device.
      else if(packetBuffer[s] == 'R'){PORTD |= 1<<RESET;}                   //Resets device.
      else{
        LED_CHECK(s);
        SOL_CHECK(s);
        HUE_CHECK(s);
        DECAY_CHECK(s);
      }
    }
  };        
                                        
  LED_FADE();                                                               //Processes LED timers/parameters.
  SOL_DATAWRITE(SOL_ZERO_CHECK());                                          //Outputs new solenoid on/off data.
  LED_DATAWRITE(LED_ZERO_CHECK());                                          //Outputs new LED brightness/hue data.
  WEB_HANDLE();                                                             //Serves the network settings web UI.
}
