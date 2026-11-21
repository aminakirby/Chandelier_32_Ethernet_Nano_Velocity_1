#pragma once
#include <Arduino.h>
#include <Ethernet2.h>

//Defined in web.cpp
extern EthernetServer webServer;

//Network settings, defined in main.cpp
extern uint8_t mac[];
extern IPAddress ip;
extern IPAddress subnet;
extern IPAddress gateway;

void NET_LOAD();                                                            //Loads saved settings (network and number of nodes) from EEPROM.
void NET_SAVE(const uint8_t *newMac, const uint8_t *newIp, const uint8_t *newSubnet, const uint8_t *newGateway, uint8_t newNodes);
void NET_REBOOT();
void WEB_HANDLE();                                                          //Serves the settings web UI; call often.
