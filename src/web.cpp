#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include "web.h"
#include "nodes.h"

//Web UI for changing network settings (browse to http://<device IP>/)
EthernetServer webServer(80);
const uint8_t NET_EEPROM_MAGIC = 0x5A;                                      //EEPROM byte 0 = this value when settings have been saved. Bytes 1-6 MAC, 7-10 IP, 11-14 subnet, 15-18 gateway, 19 number of nodes.
uint8_t webOut[64];                                                         //Small output buffer so the page is sent in chunks instead of one network packet per character.
uint8_t webOutLen = 0;

bool VALID_NODES(uint8_t n){return n == 8 || n == 16 || n == 32;}
void NET_LOAD(){                                                            //Loads saved settings from EEPROM, if there are any.
  if(EEPROM.read(0) != NET_EEPROM_MAGIC){return;};
  for(int i = 0; i < 6; i ++){mac[i] = EEPROM.read(1 + i);};
  for(int i = 0; i < 4; i ++){
    ip[i] = EEPROM.read(7 + i);
    subnet[i] = EEPROM.read(11 + i);
    gateway[i] = EEPROM.read(15 + i);
  };
  uint8_t nodes = EEPROM.read(19);
  if(VALID_NODES(nodes)){                                                   //Otherwise stays at the default of NUM_LEDS (32).
    numLeds = nodes;
    SPI_BYTES = nodes / 8;
  };
}
void NET_SAVE(const uint8_t *newMac, const uint8_t *newIp, const uint8_t *newSubnet, const uint8_t *newGateway, uint8_t newNodes){
  for(int i = 0; i < 6; i ++){EEPROM.update(1 + i, newMac[i]);};
  for(int i = 0; i < 4; i ++){
    EEPROM.update(7 + i, newIp[i]);
    EEPROM.update(11 + i, newSubnet[i]);
    EEPROM.update(15 + i, newGateway[i]);
  };
  EEPROM.update(19, newNodes);
  EEPROM.update(0, NET_EEPROM_MAGIC);                                       //Written last so a half-finished save is never treated as valid.
}
void NET_REBOOT(){                                                          //Restarts the device so new network settings take effect.
  for(int i = 0; i < SPI_BYTES; i ++){SHIFT_VAL[i] = 0;};                   //Switches all solenoids off first so none stay engaged during the restart.
  SOL_DATAWRITE(true);
  wdt_enable(WDTO_15MS);
  while(1){};
}
void WEB_FLUSH(EthernetClient &c){
  if(webOutLen){c.write(webOut, webOutLen); webOutLen = 0;};
}
void WEB_CHAR(EthernetClient &c, char ch){
  webOut[webOutLen++] = ch;
  if(webOutLen == sizeof(webOut)){WEB_FLUSH(c);};
}
void WEB_P(EthernetClient &c, PGM_P s){                                     //Sends a string stored in flash (use with PSTR("...")).
  char ch;
  while((ch = pgm_read_byte(s++))){WEB_CHAR(c, ch);};
}
void WEB_DEC(EthernetClient &c, uint8_t v){
  if(v >= 100){WEB_CHAR(c, '0' + v / 100);};
  if(v >= 10){WEB_CHAR(c, '0' + (v / 10) % 10);};
  WEB_CHAR(c, '0' + v % 10);
}
void WEB_IP(EthernetClient &c, IPAddress a){
  for(int i = 0; i < 4; i ++){
    if(i){WEB_CHAR(c, '.');};
    WEB_DEC(c, a[i]);
  };
}
void WEB_MAC(EthernetClient &c){
  const char hex[] = "0123456789ABCDEF";
  for(int i = 0; i < 6; i ++){
    if(i){WEB_CHAR(c, ':');};
    WEB_CHAR(c, hex[mac[i] >> 4]);
    WEB_CHAR(c, hex[mac[i] & 15]);
  };
}
void WEB_PAGE(EthernetClient &c, PGM_P message){                            //Sends the settings form, pre-filled with the current settings.
  WEB_P(c, PSTR("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width\">"
                "<title>Settings</title></head><body style=\"font-family:sans-serif;background:#777777\">"
                "<h2>Settings</h2><p>"));
  WEB_P(c, message);
  WEB_P(c, PSTR("</p><form action=\"/save\">IP address<br><input name=\"ip\" value=\""));
  WEB_IP(c, ip);
  WEB_P(c, PSTR("\"><br>Subnet mask<br><input name=\"sn\" value=\""));
  WEB_IP(c, subnet);
  WEB_P(c, PSTR("\"><br>Gateway<br><input name=\"gw\" value=\""));
  WEB_IP(c, gateway);
  WEB_P(c, PSTR("\"><br>MAC address<br><input name=\"mac\" value=\""));
  WEB_MAC(c);
  WEB_P(c, PSTR("\"><br>Number of nodes<br><select name=\"n\">"));
  for(uint8_t n = 8; n <= 32; n *= 2){                                      //Options 8, 16, 32 with the current one selected.
    WEB_P(c, n == numLeds ? PSTR("<option selected>") : PSTR("<option>"));
    WEB_DEC(c, n);
    WEB_P(c, PSTR("</option>"));
  };
  WEB_P(c, PSTR("</select><br><br><input type=\"submit\" value=\"Save and restart\"></form></body></html>"));
  WEB_FLUSH(c);
}
const char* WEB_PARAM(const char *q, const char *name){                     //Finds the value of name=value in a query string.
  uint8_t n = strlen(name);
  while(q && *q){
    if(strncmp(q, name, n) == 0 && q[n] == '='){return q + n + 1;};
    q = strchr(q, '&');
    if(q){q ++;};
  };
  return NULL;
}
bool PARSE_IP(const char *s, uint8_t *out){                                 //Parses "a.b.c.d" (each 0-255).
  if(!s){return false;};
  for(int i = 0; i < 4; i ++){
    if(i){
      if(*s != '.'){return false;};
      s ++;
    };
    uint16_t v = 0;
    uint8_t digits = 0;
    while(*s >= '0' && *s <= '9'){
      v = v * 10 + (*s - '0');
      s ++;
      if(++digits > 3){return false;};
    };
    if(digits == 0 || v > 255){return false;};
    out[i] = v;
  };
  return *s == 0 || *s == '&';
}
bool PARSE_MAC(const char *s, uint8_t *out){                                //Parses 12 hex digits, optionally separated by ':' or '-'.
  if(!s){return false;};
  uint8_t nibbles = 0;
  while(*s && *s != '&'){
    if(s[0] == '%' && s[1] == '3' && (s[2] == 'A' || s[2] == 'a')){s += 3; continue;};   //':' as sent by the browser (URL-encoded).
    if(*s == ':' || *s == '-'){s ++; continue;};
    int8_t h = -1;
    if(*s >= '0' && *s <= '9'){h = *s - '0';}
    else if(*s >= 'A' && *s <= 'F'){h = *s - 'A' + 10;}
    else if(*s >= 'a' && *s <= 'f'){h = *s - 'a' + 10;};
    if(h < 0 || nibbles >= 12){return false;};
    if(nibbles % 2 == 0){out[nibbles / 2] = h << 4;}
    else{out[nibbles / 2] |= h;};
    nibbles ++;
    s ++;
  };
  return nibbles == 12 && !(out[0] & 1);                                    //First byte must be even (a multicast MAC would not work).
}
bool PARSE_NODES(const char *s, uint8_t *out){                              //Parses the number of nodes (8, 16 or 32).
  if(!s){return false;};
  uint8_t v = 0;
  uint8_t digits = 0;
  while(*s >= '0' && *s <= '9'){
    v = v * 10 + (*s - '0');
    s ++;
    if(++digits > 2){return false;};
  };
  if(!(*s == 0 || *s == '&') || !VALID_NODES(v)){return false;};
  *out = v;
  return true;
}
void WEB_HANDLE(){                                                          //Serves the settings page and handles saving.
  EthernetClient client = webServer.available();
  if(!client){return;};

  char line[128];                                                           //First line of the request, e.g. "GET /save?ip=... HTTP/1.1".
  uint8_t len = 0;
  bool lineDone = false;
  uint8_t newlines = 0;
  uint32_t start = millis();
  while(client.connected() && millis() - start < 100){                      //Reads the request, giving up after 100ms so the lights/solenoids aren't held up.
    if(!client.available()){continue;};
    char ch = client.read();
    if(!lineDone){
      if(ch == '\r' || ch == '\n'){lineDone = true;}
      else if(len < sizeof(line) - 1){line[len ++] = ch;};
    };
    if(ch == '\n'){
      if(++newlines == 2){break;};                                          //Blank line = end of request headers.
    }
    else if(ch != '\r'){newlines = 0;};
  };
  line[len] = 0;
  if(len > 4){                                                              //Cuts off " HTTP/1.1".
    char *end = strchr(line + 4, ' ');
    if(end){*end = 0;};
  };

  if(strncmp(line, "GET /save?", 10) == 0){
    const char *q = line + 10;
    uint8_t newMac[6], newIp[4], newSubnet[4], newGateway[4], newNodes;
    if(PARSE_IP(WEB_PARAM(q, "ip"), newIp) && PARSE_IP(WEB_PARAM(q, "sn"), newSubnet) &&
       PARSE_IP(WEB_PARAM(q, "gw"), newGateway) && PARSE_MAC(WEB_PARAM(q, "mac"), newMac) &&
       PARSE_NODES(WEB_PARAM(q, "n"), &newNodes)){
      NET_SAVE(newMac, newIp, newSubnet, newGateway, newNodes);
      WEB_P(client, PSTR("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                         "<!DOCTYPE html><html><body style=\"font-family:sans-serif\"><h2>Saved</h2>"
                         "<p>The device is restarting. Open it at its new address: http://"));
      for(int i = 0; i < 4; i ++){
        if(i){WEB_CHAR(client, '.');};
        WEB_DEC(client, newIp[i]);
      };
      WEB_P(client, PSTR("/</p></body></html>"));
      WEB_FLUSH(client);
      client.stop();
      NET_REBOOT();
    }
    else{
      WEB_PAGE(client, PSTR("<b style=\"color:red\">Not saved: check the values. IP, subnet and gateway look like 10.0.0.50; MAC looks like DE:AD:BE:EF:FE:CE.</b>"));
    };
  }
  else if(strcmp(line, "GET /") == 0){
    WEB_PAGE(client, PSTR("Changes take effect after the device restarts."));
  }
  else{
    WEB_P(client, PSTR("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"));
    WEB_FLUSH(client);
  };
  client.stop();
}
