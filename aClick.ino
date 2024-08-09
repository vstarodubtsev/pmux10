#include <esp_task_wdt.h>
#include <GyverPortal.h>
#include <Ethernet.h>
#include <WebServer_WT32_ETH01.h>
#include <GyverShift.h>
#include <GParser.h>
#include "ESPTelnet.h"

#define FIRMWARE_VERSION "1.0"

#define WDT_TIMEOUT 3

#define DEBUG_ETHERNET_WEBSERVER_PORT Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3

#define CHIP_AMOUNT 2

#define CLK_595 12
#define CS_595 4
#define DAT_595 14
#define nOE_595 15

#define CH9_GPIO 17
#define CH10_GPIO 2

#define RST9_GPIO 32
#define RST10_GPIO 33

#define WDT_LED_GPIO 5  //35

#define TELNET_PORT 2424

#define JEROME_PORT_COUNT 22

GyverPortal ui;

WebServer wserver(80);

ESPTelnet telnet;
IPAddress ip;

IPAddress myIP(192, 168, 1, 101);
IPAddress myGW(192, 168, 1, 1);
IPAddress mySN(255, 255, 255, 0);
IPAddress myDNS(8, 8, 8, 8);

GyverShift<OUTPUT, CHIP_AMOUNT> outp(CS_595, DAT_595, CLK_595);

bool jerome_output[JEROME_PORT_COUNT];

int lastWdt = millis();
int loopCounter = 0;

const char* swPwrPrefix = "sw";

//const char* swPower[] = { "sw1", "sw2", "sw3", "sw4", "sw5", "sw6", "sw7", "sw8", "sw9", "sw10" };
const char* swRst[] = { "rst1", "rst2", "rst3", "rst4", "rst5", "rst6", "rst7", "rst8", "rst9", "rst10" };
String valIpv4;
String valIpv4Mask;
String valMac;

// конструктор страницы
void build() {
  GP.BUILD_BEGIN(GP_DARK, 480);

  GP.PAGE_TITLE("PMUX10");

  // ОБНОВЛЕНИЯ
  String s;
  // формируем список для UPDATE
  // вида "lbl/0,lbl/1..."
  for (int i = 0; i < 10; i++) {
    s += swPwrPrefix;
    s += "/";
    s += i;
    s += ',';
  }

  for (uint8_t i = 0; i < (sizeof(swRst) / sizeof(swRst[0])); i++) {
    s += swRst[i];
    s += ',';
  }

  //s += "Uptime";

  GP.UPDATE(s);

  //GP.UPDATE("sw1,sw2,swRst,Uptime");  //TODO

  GP.TITLE("PMUX10", "t1");
  GP.HR();

  GP.NAV_TABS("Home,Information,Settings");

  GP.NAV_BLOCK_BEGIN();
  M_BOX(
    M_BLOCK_TAB(
      "Power",

      for (uint8_t i = 0; i < 10; i++) {
        M_BOX(GP.LABEL(String("power") + (i + 1) + ": "); GP.SWITCH(String(swPwrPrefix) + "/" + i, jerome_output[i]););
      }

      /*
      M_BOX(GP.LABEL("power1: "); GP.SWITCH(swPower[0], jerome_output[0]););
      M_BOX(GP.LABEL("power2: "); GP.SWITCH(swPower[1], jerome_output[1]););
      M_BOX(GP.LABEL("power3: "); GP.SWITCH(swPower[2], jerome_output[2]););
      M_BOX(GP.LABEL("power4: "); GP.SWITCH(swPower[3], jerome_output[3]););
      M_BOX(GP.LABEL("power5: "); GP.SWITCH(swPower[4], jerome_output[4]););
      M_BOX(GP.LABEL("power6: "); GP.SWITCH(swPower[5], jerome_output[5]););
      M_BOX(GP.LABEL("power7: "); GP.SWITCH(swPower[6], jerome_output[6]););
      M_BOX(GP.LABEL("power8: "); GP.SWITCH(swPower[7], jerome_output[7]););
      M_BOX(GP.LABEL("power9: "); GP.SWITCH(swPower[8], jerome_output[8]););
      M_BOX(GP.LABEL("power10: "); GP.SWITCH(swPower[9], jerome_output[9]););*/
    );

    M_BLOCK_TAB(
      "Reset",
      M_BOX(GP.LABEL("reset1: "); GP.SWITCH(swRst[0], jerome_output[21]););
      M_BOX(GP.LABEL("reset2: "); GP.SWITCH(swRst[1], jerome_output[20]););
      M_BOX(GP.LABEL("reset3: "); GP.SWITCH(swRst[2], jerome_output[19]););
      M_BOX(GP.LABEL("reset4: "); GP.SWITCH(swRst[3], jerome_output[18]););
      M_BOX(GP.LABEL("reset5: "); GP.SWITCH(swRst[4], jerome_output[17]););
      M_BOX(GP.LABEL("reset6: "); GP.SWITCH(swRst[5], jerome_output[16]););
      M_BOX(GP.LABEL("reset7: "); GP.SWITCH(swRst[6], jerome_output[15]););
      M_BOX(GP.LABEL("reset8: "); GP.SWITCH(swRst[7], jerome_output[14]););
      M_BOX(GP.LABEL("reset9: "); GP.SWITCH(swRst[8], jerome_output[13]););
      M_BOX(GP.LABEL("reset10: "); GP.SWITCH(swRst[9], jerome_output[12]););););
  GP.NAV_BLOCK_END();

  GP.NAV_BLOCK_BEGIN();
  GP.SYSTEM_INFO(FIRMWARE_VERSION);
  GP.NAV_BLOCK_END();

  GP.NAV_BLOCK_BEGIN();
  M_BOX(GP.LABEL("IP address"); GP.TEXT("ipv4_addr", "192.168.1.101", valIpv4); GP.BUTTON_MINI("apply_ipv4_btn", "Change"););
  M_BOX(GP.LABEL("IP mask"); GP.TEXT("ipv4_mask", "255.255.255.0", valIpv4Mask); GP.BUTTON_MINI("apply_ipv4_mask_btn", "Change"););
  M_BOX(GP.LABEL("MAC address"); GP.TEXT("mac", "00:01:02:03:04:05", valMac); GP.BUTTON_MINI("apply_mac_btn", "Change"););

  GP.NAV_BLOCK_END();

  GP.BUILD_END();
}

void action() {
  // был клик по компоненту
  if (ui.click()) {

    if (ui.clickSub(swPwrPrefix)) {  // начинается с sld

      Serial.print("switch ");
      Serial.print(ui.clickNameSub(1));  // получаем цифру
      Serial.print(": ");
      uint8_t index = atoi(ui.clickNameSub().c_str());
      Serial.print(index);
      Serial.print(": ");
      Serial.println(ui.getBool());

      jerome_output[index] = ui.getBool();

      // if (ui.clickBool(ui.clickName(), jerome_output[index])) {
      set_power(index + 1, jerome_output[index]);
      //}
    }

    /*
    for (uint8_t i = 0; i < (sizeof(swPower) / sizeof(swPower[0])); i++) {
      if (ui.clickBool(swPower[i], jerome_output[i])) {
        set_power(i + 1, jerome_output[i]);
      }
    }
*/
    for (uint8_t i = 0; i < (sizeof(swRst) / sizeof(swRst[0])); i++) {
      if (ui.clickBool(swRst[i], jerome_output[21 - i])) {
        set_rst(i + 1, jerome_output[21 - i]);
      }
    }

    if (ui.click("apply_ipv4_btn")) Serial.println("ipv4 button click");
    if (ui.click("apply_ipv4_mask_btn")) Serial.println("ipv4 mask button click");
    if (ui.click("apply_mac_btn")) Serial.println("mac button click");
  }

  if (ui.update()) {
    if (ui.updateSub(swPwrPrefix)) {
      ui.answer(jerome_output[atoi(ui.updateNameSub().c_str())]);
    }

    for (uint8_t i = 0; i < (sizeof(swRst) / sizeof(swRst[0])); i++) {
      if (ui.update(swRst[i])) ui.answer(jerome_output[21 - i]);
    }

    if (ui.update("Uptime")) ui.answer(millis() / 1000ul);
  }
}

void init_peripheral() {
  digitalWrite(nOE_595, 1);
  pinMode(nOE_595, OUTPUT);
  outp.clearAll();
  outp.update();
  digitalWrite(nOE_595, 0);

  digitalWrite(CH9_GPIO, 0);
  digitalWrite(CH10_GPIO, 0);
  digitalWrite(RST9_GPIO, 0);
  digitalWrite(RST10_GPIO, 0);
  digitalWrite(WDT_LED_GPIO, 1);
  pinMode(CH9_GPIO, OUTPUT);
  pinMode(CH10_GPIO, OUTPUT);
  pinMode(RST9_GPIO, OUTPUT);
  pinMode(RST10_GPIO, OUTPUT);
  pinMode(WDT_LED_GPIO, OUTPUT);
}

void set_power(int num, uint8_t state) {
  if (num > 0 && num <= 8) {
    outp[num - 1] = state;
    outp.update();

  } else if (num == 9) {
    digitalWrite(CH9_GPIO, state);

  } else if (num == 10) {
    digitalWrite(CH10_GPIO, state);
  }
}

void set_rst(int num, int state) {
  if (num > 0 && num <= 8) {
    outp[16 - num] = state;
    outp.update();

  } else if (num == 9) {
    digitalWrite(RST9_GPIO, state);

  } else if (num == 10) {
    digitalWrite(RST10_GPIO, state);
  }
}

void jerome_set(int num, int state) {
  if (num < 1 || num > JEROME_PORT_COUNT) {
    return;
  }

  if (num >= 1 && num <= 10) {
    set_power(num, state);
  } else if (num >= 13 && num <= JEROME_PORT_COUNT) {
    set_rst(JEROME_PORT_COUNT + 1 - num, state);
  }

  jerome_output[num - 1] = state;
}

void jerome_set_all(int state) {
  if (state) {
    outp.setAll();
    outp.update();

    digitalWrite(CH9_GPIO, 1);
    digitalWrite(CH10_GPIO, 1);
    digitalWrite(RST9_GPIO, 1);
    digitalWrite(RST10_GPIO, 1);

  } else {
    outp.clearAll();
    outp.update();

    digitalWrite(CH9_GPIO, 0);
    digitalWrite(CH10_GPIO, 0);
    digitalWrite(RST9_GPIO, 0);
    digitalWrite(RST10_GPIO, 0);
  }

  for (uint8_t i = 0; i < JEROME_PORT_COUNT; i++) {
    jerome_output[i] = state;
  }
}

bool jerome_get(int num) {
  if (num < 1 || num > JEROME_PORT_COUNT) {
    return false;
  }

  return jerome_output[num - 1];
}

void onTelnetConnect(String ip) {
  //Serial.print("Telnet: ");
  //Serial.print(ip);
  //Serial.println(" connected");

  telnet.println("\nWelcome " + telnet.getIP());
  telnet.println("(Use ^] + q or type bye to disconnect.)");
}

void onTelnetInput(String str) {
  // checks for a certain command

  // disconnect the client
  if (str == "bye") {
    telnet.println("> disconnecting you...");
    telnet.disconnectClient();

    return;
  }

  if (str.startsWith("$KE")) {
    GParser data(str.begin(), ',');
    int am = data.split();

    if (strcmp(data[0], "$KE") != 0) {
      telnet.println("#ERR");

      return;
    }

    if (am == 1) {
      telnet.println("#OK");

      return;
    }

    if (strcmp(data[1], "WR") == 0 && am == 4) {
      if (strcmp(data[2], "ALL") == 0) {
        if (strcmp(data[3], "ON") == 0) {
          jerome_set_all(1);
          telnet.println("#WR,OK");
          return;
        }

        if (strcmp(data[3], "OFF") == 0) {
          jerome_set_all(0);
          telnet.println("#WR,OK");

          return;
        }
      } else {
        int32_t line = data.getInt(2);
        int32_t state = data.getInt(3);

        if (line > 0 && line <= JEROME_PORT_COUNT && state >= 0 && state <= 1) {
          jerome_set(line, state);
          telnet.println("#WR,OK");

          return;
        }
      }
    } else if (strcmp(data[1], "WRA") == 0 && am == 3) {
      int len1 = strlen(data[2]);
      if (len1 <= JEROME_PORT_COUNT) {
        int affected = 0;
        for (uint8_t i = 0; i < len1; i++) {
          const char c = data[2][i];

          if (c == '1') {
            jerome_set(i + 1, 1);
            affected++;

          } else if (c == '0') {
            jerome_set(i + 1, 0);
            affected++;

          } else if (c == 'x' || c == 'X') {

          } else {
            telnet.println("#ERR");

            return;
          }
        }

        telnet.print("#WRA,OK,");
        telnet.print(affected);
        telnet.println();

        return;
      }
    } else if (strcmp(data[1], "RID") == 0 && am == 3) {
      if (strcmp(data[2], "ALL") == 0) {
        telnet.print("#RID,ALL,");
        for (uint8_t i = 1; i <= JEROME_PORT_COUNT; i++) {
          if (jerome_get(i)) {
            telnet.print("1");
          } else {
            telnet.print("0");
          }
        }

        telnet.println();

        return;
      }

      int32_t line = data.getInt(2);

      if (line >= 1 && line <= JEROME_PORT_COUNT) {
        bool state = jerome_get(line);

        telnet.print("#RID,");
        telnet.print(line);
        telnet.print(",");
        telnet.println(state);

        return;
      }
    }
  }

  telnet.println("#ERR");
}

void setupTelnet() {
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  //telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  //telnet.onReconnect(onTelnetReconnect);
  //telnet.onDisconnect(onTelnetDisconnect);
  telnet.onInputReceived(onTelnetInput);

  Serial.print("Telnet: ");
  if (telnet.begin(TELNET_PORT, false)) {
    Serial.println("running");
  } else {
    Serial.println("error.");
    // errorMsg("Will reboot...");
  }
}

void setup() {
  init_peripheral();

  Serial.begin(115200);
  /*WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());*/

  // To be called before ETH.begin()
  WT32_ETH01_onEvent();

  ETH.begin();

  // Static IP, leave without this line to get IP via DHCP
  //bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = 0, IPAddress dns2 = 0);
  ETH.config(myIP, myGW, mySN, myDNS);

  WT32_ETH01_waitForConnect();

  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << 2) - 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  // esp_task_wdt_init(WDT_TIMEOUT, true);  //enable panic so ESP32 restarts
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);  //add current thread to WDT watch
  esp_task_wdt_reset();

  // подключаем конструктор и запускаем
  ui.attachBuild(build);
  ui.attach(action);
  ui.start();

  Serial.print(F("HTTP EthernetWebServer is @ IP : "));
  Serial.println(ETH.localIP());

  setupTelnet();
}

void loop() {
  ui.tick();

  telnet.loop();

  if (millis() - lastWdt >= 2000) {
    loopCounter++;
    // Serial.println("Resetting WDT...");
    digitalWrite(WDT_LED_GPIO, loopCounter % 2);
    esp_task_wdt_reset();
    lastWdt = millis();
  }
}
