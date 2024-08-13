#include <esp_task_wdt.h>
#include <GyverPortal.h>
#include <Ethernet.h>
#include <WebServer_WT32_ETH01.h>
#include <GyverShift.h>
#include <GParser.h>
#include <FileData.h>
#include <LittleFS.h>
#include "ESPTelnet.h"

#define PROJECT_NAME "PMUX10"
#define FIRMWARE_VERSION "1.0"

#define WDT_TIMEOUT 3

#define DEBUG_ETHERNET_WEBSERVER_PORT Serial

#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3  // Debug Level from 0 to 4

#define MAC_SIZE 6

#define SHIFT_CHIP_AMOUNT 2

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

#define INTERFACE_ELEMENTS_COUNT 10

#define INPUT_IPV4_ID "ipv4_inp"
#define INPUT_IPV4_MASK_ID "ipv4_mask_inp"
#define INPUT_MAC_ID "mac_inp"

#define BUTTON_APPLY_IPV4_ID "apply_ipv4_btn"
#define BUTTON_APPLY_IPV4_MASK_ID "apply_ipv4_mask_btn"
#define BUTTON_APPLY_MAC_ID "apply_mac_btn"
#define BUTTON_RESET_ID "reset_btn"
#define BUTTON_SAVE_ID "save_btn"

struct Data {
  uint32_t ipv4;
  uint32_t mask;
  uint32_t gw;
  char mac[MAC_SIZE];  //18
  char reserved[46];
};
Data nvData;

FileData fData(&LittleFS, "/data.bin", 'B', &nvData, sizeof(nvData));

GyverPortal ui;

WebServer wserver(80);

ESPTelnet telnet;

GyverShift<OUTPUT, SHIFT_CHIP_AMOUNT> shiftRegister(CS_595, DAT_595, CLK_595);

bool jeromeOutput[JEROME_PORT_COUNT];

int lastWdt = millis();
int loopCounter = 0;

const char* swPwrPrefix = "sw";
const char* swRstPrefix = "rst";

String valIpv4;
String valIpv4Mask;
String valMac;

String mac2String(char m[]) {
  String s;
  for (int i = 0; i < MAC_SIZE; ++i) {
    char buf[3];
    sprintf(buf, "%02X", m[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

int parseMac(const char* str, char sep, char* bytes) {
  for (int i = 0; i < MAC_SIZE; i++) {
    bytes[i] = strtoul(str, NULL, 16);
    str = strchr(str, sep);
    if (str == NULL || *str == '\0') {
      if (i != 5) return -1;
      break;
    }
    str++;
  }

  return 0;
}

void uiBuild() {
  GP.BUILD_BEGIN(GP_DARK, 480);
  GP.PAGE_TITLE(PROJECT_NAME);
  GP.TITLE(PROJECT_NAME, "t1");

  String s;

  for (int i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
    s += swPwrPrefix;
    s += "/";
    s += i;
    s += ',';
  }

  for (int i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
    s += swRstPrefix;
    s += "/";
    s += i;
    s += ',';
  }

  GP.UPDATE(s);

  GP.HR();

  GP.NAV_TABS_LINKS("/,/info,/settings", "Home,Information,Settings");

  if (ui.uri("/info")) {
    GP.SYSTEM_INFO(FIRMWARE_VERSION);
  } else if (ui.uri("/settings")) {
    GP.FORM_BEGIN("/settings");
    M_BOX(
      GP.LABEL("IP address");
      GP.TEXT(INPUT_IPV4_ID, IPAddress(nvData.ipv4).toString(), valIpv4));
    M_BOX(
      GP.LABEL("IP mask");
      GP.TEXT(INPUT_IPV4_MASK_ID, IPAddress(nvData.mask).toString(), valIpv4Mask));
    M_BOX(
      GP.LABEL("MAC address");
      GP.TEXT(INPUT_MAC_ID, mac2String(nvData.mac), valMac));
    GP.BUTTON(BUTTON_RESET_ID, "Reset to defaults");
    GP.SUBMIT("Save to NV");
    GP.FORM_END();

  } else {  //home page
    M_BOX(
      M_BLOCK_TAB(
        "Power",
        for (uint8_t i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
          M_BOX(GP.LABEL(String("power") + (i + 1) + ": "); GP.SWITCH(String(swPwrPrefix) + "/" + i, jeromeOutput[i]););
        });

      M_BLOCK_TAB(
        "Reset",
        for (uint8_t i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
          M_BOX(GP.LABEL(String("reset") + (i + 1) + ": "); GP.SWITCH(String(swRstPrefix) + "/" + i, jeromeOutput[JEROME_PORT_COUNT - 1 - i]););
        }););
    GP.BUTTON("clear_all_btn", "Clear all");
  }

  GP.BUILD_END();
}

void uiAction() {
  if (ui.click()) {
    if (ui.clickSub(swPwrPrefix)) {
      uint8_t index = atoi(ui.clickNameSub().c_str());

      jeromeOutput[index] = ui.getBool();
      setPower(index + 1, jeromeOutput[index]);

    } else if (ui.clickSub(swRstPrefix)) {
      uint8_t index = atoi(ui.clickNameSub().c_str());
      bool val = ui.getBool();

      jeromeOutput[JEROME_PORT_COUNT - 1 - index] = val;
      setRst(index + 1, val);

    } else if (ui.click(BUTTON_APPLY_IPV4_ID)) {
      String val;
      ui.copyString(INPUT_IPV4_ID, val);
      Serial.print("ipv4 button click value");
      Serial.println(val);
    }
  }

  if (ui.update()) {
    if (ui.updateSub(swPwrPrefix)) {
      ui.answer(jeromeOutput[atoi(ui.updateNameSub().c_str())]);

    } else if (ui.updateSub(swRstPrefix)) {
      ui.answer(jeromeOutput[JEROME_PORT_COUNT - 1 - atoi(ui.updateNameSub().c_str())]);
    }
  }

  if (ui.form()) {
    if (ui.form("/settings")) {
      bool changed = false;
      String val = ui.getString(INPUT_IPV4_ID);

      if (!val.isEmpty()) {
        IPAddress ip;

        if (ip.fromString(val)) {
          nvData.ipv4 = ip;
          changed = true;
        }
      }

      val = ui.getString(INPUT_IPV4_MASK_ID);

      if (!val.isEmpty()) {
        IPAddress ip;

        if (ip.fromString(val)) {
          // TODO checks
          nvData.mask = ip;
          changed = true;
        }
      }

      val = ui.getString(INPUT_MAC_ID);

      if (!val.isEmpty()) {
        char m[MAC_SIZE];

        if (parseMac(val.c_str(), ':', m) == 0) {
          m[0] &= ~0x01;
          memcpy(nvData.mac, m, sizeof(m));
          changed = true;
        }
      }

      if (changed) {
        Serial.print("apply NV ");
        fData.update();
      }
    }
  }
}

void initPeripheral() {
  digitalWrite(nOE_595, 1);
  pinMode(nOE_595, OUTPUT);
  shiftRegister.clearAll();
  shiftRegister.update();
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

void setPower(int num, bool state) {
  if (num > 0 && num <= 8) {
    shiftRegister[num - 1] = state;
    shiftRegister.update();

  } else if (num == 9) {
    digitalWrite(CH9_GPIO, state);

  } else if (num == 10) {
    digitalWrite(CH10_GPIO, state);
  }
}

void setRst(int num, bool state) {
  if (num > 0 && num <= 8) {
    shiftRegister[16 - num] = state;
    shiftRegister.update();

  } else if (num == 9) {
    digitalWrite(RST9_GPIO, state);

  } else if (num == 10) {
    digitalWrite(RST10_GPIO, state);
  }
}

void jeromeSet(int num, bool state) {
  if (num < 1 || num > JEROME_PORT_COUNT) {
    return;
  }

  if (num >= 1 && num <= 10) {
    setPower(num, state);
  } else if (num >= 13 && num <= JEROME_PORT_COUNT) {
    setRst(JEROME_PORT_COUNT + 1 - num, state);
  }

  jeromeOutput[num - 1] = state;
}

void jeromeSetAll(bool state) {
  if (state) {
    shiftRegister.setAll();
    shiftRegister.update();

    digitalWrite(CH9_GPIO, 1);
    digitalWrite(CH10_GPIO, 1);
    digitalWrite(RST9_GPIO, 1);
    digitalWrite(RST10_GPIO, 1);

  } else {
    shiftRegister.clearAll();
    shiftRegister.update();

    digitalWrite(CH9_GPIO, 0);
    digitalWrite(CH10_GPIO, 0);
    digitalWrite(RST9_GPIO, 0);
    digitalWrite(RST10_GPIO, 0);
  }

  for (uint8_t i = 0; i < JEROME_PORT_COUNT; i++) {
    jeromeOutput[i] = state;
  }
}

bool jeromeGet(int num) {
  if (num < 1 || num > JEROME_PORT_COUNT) {
    return false;
  }

  return jeromeOutput[num - 1];
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
          jeromeSetAll(1);
          telnet.println("#WR,OK");
          return;
        }

        if (strcmp(data[3], "OFF") == 0) {
          jeromeSetAll(0);
          telnet.println("#WR,OK");

          return;
        }
      } else {
        int32_t line = data.getInt(2);
        int32_t state = data.getInt(3);

        if (line > 0 && line <= JEROME_PORT_COUNT && state >= 0 && state <= 1) {
          jeromeSet(line, state);
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
            jeromeSet(i + 1, 1);
            affected++;

          } else if (c == '0') {
            jeromeSet(i + 1, 0);
            affected++;

          } else if (c == 'x' || c == 'X') {
            ;  //skip item
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
          if (jeromeGet(i)) {
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
        bool state = jeromeGet(line);

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

void setupNv() {
  memset(&nvData, 0, sizeof(nvData));

  LittleFS.begin(true);

  FDstat_t stat = fData.read();

  switch (stat) {
    case FD_FS_ERR:
      Serial.println("FS Error");
      break;
    case FD_FILE_ERR:
      Serial.println("Error");
      break;
    case FD_WRITE:
      Serial.println("Data Write");
      break;
    case FD_ADD:
      Serial.println("Data Add");
      break;
    case FD_READ:
      Serial.println("Data Read");
      break;
    default:
      break;
  }

  if (nvData.ipv4 == 0) {
    nvData.ipv4 = IPAddress(192, 168, 1, 101);
  }

  if (nvData.mask == 0) {
    nvData.mask = IPAddress(255, 255, 255, 0);
  }

  if (nvData.gw == 0) {
    nvData.gw = IPAddress(192, 168, 1, 1);
  }

  if (nvData.mac[0] == 0 && nvData.mac[1] == 0
      && nvData.mac[2] == 0 && nvData.mac[3] == 0
      && nvData.mac[4] == 0 && nvData.mac[5] == 0) {
    for (int i = 0; i < MAC_SIZE; i++) {
      nvData.mac[i] = random(0xff);
    }

    nvData.mac[0] &= ~0x01;
    nvData.mac[1] |= 0x02;
  }

  Serial.print("NV IPv4 ");
  Serial.print(IPAddress(nvData.ipv4));
  Serial.print("/");
  Serial.println(IPAddress(nvData.mask));
  Serial.print("NV GW ");
  Serial.println(IPAddress(nvData.gw));
  Serial.print("NV MAC: ");
  Serial.println(mac2String(nvData.mac));
}

void setup() {
  initPeripheral();

  Serial.begin(115200);

  setupNv();

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
  ETH.config(IPAddress(nvData.ipv4), IPAddress(nvData.gw), IPAddress(nvData.mask));

  WT32_ETH01_waitForConnect();

  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT,
    .idle_core_mask = (1 << 2) - 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);  //add current thread to WDT watch
  esp_task_wdt_reset();

  // start user interface
  ui.attachBuild(uiBuild);
  ui.attach(uiAction);
  ui.start();

  Serial.print(F("HTTP EthernetWebServer is @ IP : "));
  Serial.println(ETH.localIP());

  setupTelnet();
}

void loop() {
  ui.tick();

  if (fData.tick() == FD_WRITE) Serial.println("NV data updated");

  telnet.loop();

  if (millis() - lastWdt >= 2000) {
    loopCounter++;
    // Serial.println("Resetting WDT...");
    digitalWrite(WDT_LED_GPIO, loopCounter % 2);
    esp_task_wdt_reset();
    lastWdt = millis();
  }
}
