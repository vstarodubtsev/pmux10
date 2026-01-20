#include <esp_task_wdt.h>
#include <esp_mac.h>
#include <LittleFS.h>  // must be before GyverPortal
#include <GyverPortal.h>
#include <Ethernet.h>
#include <ETH.h>
#include <StringUtils.h>
#include <FileData.h>
#include <ESPTelnet.h>
#include <uButton.h>

#define PROJECT_NAME "Besprizornik"
#define FIRMWARE_VERSION "1.0.0"

#define WDT_TIMEOUT 15

#define DEBUG_ETHERNET_WEBSERVER_PORT Serial

#undef _ETHERNET_WEBSERVER_LOGLEVEL_
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3  // Debug Level from 0 to 4

#define CH1_GPIO 12
#define WDT_LED_GPIO 5
#define BUTTON_GPIO 35

#define TELNET_PORT 2424

#define JEROME_PORT_COUNT 22

#define INTERFACE_ELEMENTS_COUNT 1

#define INPUT_IPV4_ID "ipv4_inp"
#define INPUT_IPV4_MASK_ID "ipv4_mask_inp"
#define INPUT_GW_ID "gw_inp"
#define INPUT_MAC_ID "mac_inp"
#define INPUT_TELNET_PORT_ID "tel_inp"
#define INPUT_TITLE_ID "title_inp"

#define SW_PWR_PFX "sw"
#define SW_PWR_ON_PFX "dsw"

#define BUTTON_RESET_ID "reset_btn"
#define POPUP_RESET_CONFIRM_ID "reset_cnfrm"
#define BUTTON_SAVE_ID "save_btn"
#define BUTTON_RESTART_ID "restart_btn"
#define BUTTON_CLEAR_ALL_ID "clear_all_btn"

#define TITLE_MAX_LEN 32

struct Data {  // 512 bytes
  uint32_t ipv4;
  uint32_t mask;
  uint32_t gw;
  uint8_t mac[ETH_ADDR_LEN];
  char title[TITLE_MAX_LEN];
  uint8_t defaultState[INTERFACE_ELEMENTS_COUNT];
  uint8_t reserved[462 - INTERFACE_ELEMENTS_COUNT];
};
Data nvData;

FileData fData(&LittleFS, "/data.bin", 'B', &nvData, sizeof(nvData));
GyverPortal ui(&LittleFS);
WebServer wserver(80);
ESPTelnet telnet;
uButton btn(BUTTON_GPIO);

static bool jeromeOutput[JEROME_PORT_COUNT];

static bool reboot = false;

int lastWdt = millis();
int loopCounter = 0;

String mac2String(uint8_t m[]) {
  String s;
  for (int i = 0; i < ETH_ADDR_LEN; ++i) {
    char buf[3];
    sprintf(buf, "%02X", m[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

int parseMac(const char* str, char sep, uint8_t* mac) {
  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    mac[i] = strtoul(str, NULL, 16);
    str = strchr(str, sep);
    if (str == NULL || *str == '\0') {
      if (i != 5) return -1;
      break;
    }
    str++;
  }

  return 0;
}

bool validIpMask(const IPAddress& mask) {
  const uint32_t m32 = __ntohl(mask);

  if (m32 == 0) return false;

  if (m32 & (~m32 >> 1)) {
    return false;
  }

  return true;
}

void uiBuild() {
  GP.BUILD_BEGIN(GP_DARK, 480);

  String title = PROJECT_NAME;
  String customTitle(nvData.title);

  if (!customTitle.isEmpty() && customTitle.length() <= TITLE_MAX_LEN) {
    title += " ";
    title += customTitle;
  }

  GP.PAGE_TITLE(title);
  GP.TITLE(title);

  GP.HR();

  GP.NAV_TABS_LINKS("/,/info,/settings,/upgrade", "Home,Information,Settings,Upgrade");

  if (ui.uri("/info")) {
    GP.SYSTEM_INFO(FIRMWARE_VERSION);

  } else if (ui.uri("/settings")) {
    GP.FORM_BEGIN("/settings");
    M_BOX(
      GP.LABEL("IP address");
      GP.TEXT(INPUT_IPV4_ID, "", IPAddress(nvData.ipv4).toString()));
    M_BOX(
      GP.LABEL("IP mask");
      GP.TEXT(INPUT_IPV4_MASK_ID, "", IPAddress(nvData.mask).toString()));
    M_BOX(
      GP.LABEL("IP gateway");
      GP.TEXT(INPUT_GW_ID, "", IPAddress(nvData.gw).toString()));
    M_BOX(
      GP.LABEL("MAC address");
      GP.TEXT(INPUT_MAC_ID, "", mac2String(nvData.mac)));
    M_BOX(
      GP.LABEL("Telnet port");
      GP.TEXT(INPUT_TELNET_PORT_ID, "", String(TELNET_PORT), "", 0, "", true));
    M_BOX(
      GP.LABEL("Custom title");
      GP.TEXT(INPUT_TITLE_ID, "", String(nvData.title), "", TITLE_MAX_LEN));
    for (uint8_t i = 1; i <= INTERFACE_ELEMENTS_COUNT; i++) {
      M_BOX(
        GP.LABEL(String("Power ") + i + " default state");
        GP.SWITCH(String(SW_PWR_ON_PFX) + "/" + i, nvData.defaultState[i - 1]););
    }

    GP.SUBMIT_MINI("Save to NV");
    GP.BUTTON_MINI(BUTTON_RESTART_ID, "Restart");
    GP.BUTTON_MINI(BUTTON_RESET_ID, "Reset to defaults", "", GP_RED);
    GP.FORM_END();

    GP.CONFIRM(POPUP_RESET_CONFIRM_ID, F("Are you sure want to reset?"));
    GP.UPDATE_CLICK(POPUP_RESET_CONFIRM_ID, BUTTON_RESET_ID);

  } else if (ui.uri("/upgrade")) {
    GP.OTA_FIRMWARE("Firmware", GP_GREEN, true);
    // GP.OTA_FILESYSTEM("FS", GP_GREEN, true);
  } else {  //home page
    M_BLOCK_TAB(
      "Power",
      for (uint8_t i = 1; i <= INTERFACE_ELEMENTS_COUNT; i++) {
        M_BOX(
          GP.LABEL(String("power") + i + ": ");
          GP.SWITCH(String(SW_PWR_PFX) + "/" + i, jeromeGet(i)););
      });
    GP.BUTTON(BUTTON_CLEAR_ALL_ID, "Clear all");

    String s;

    for (int i = 1; i <= INTERFACE_ELEMENTS_COUNT; i++) {
      s += SW_PWR_PFX;
      s += "/";
      s += i;
      s += ',';
    }

    GP.UPDATE(s);
  }

  GP.BUILD_END();
}

void uiAction() {
  if (ui.click()) {
    if (ui.clickSub(SW_PWR_PFX)) {
      uint8_t index = atoi(ui.clickNameSub().c_str()) - 1;
      bool val = ui.getBool();

      setPower(index + 1, val);

    } else if (ui.click(POPUP_RESET_CONFIRM_ID)) {
      if (ui.getBool()) {
        eraseNv();
      }
    } else if (ui.click(BUTTON_CLEAR_ALL_ID)) {
      jeromeSetAll(0);
    } else if (ui.click(BUTTON_RESTART_ID)) {
      reboot = true;
    }
  }

  if (ui.update()) {
    if (ui.updateSub(SW_PWR_PFX)) {
      ui.answer(getPower(atoi(ui.updateNameSub().c_str())));

    } else if (ui.update(POPUP_RESET_CONFIRM_ID)) {
      ui.answer(1);
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
        IPAddress mask;

        if (mask.fromString(val) && validIpMask(mask)) {
          Serial.print("Set IP mask: ");
          Serial.println(mask);
          nvData.mask = mask;
          changed = true;
        } else {
          Serial.print("invalid IP mask: ");
          Serial.println(val);
        }
      }

      val = ui.getString(INPUT_GW_ID);

      if (!val.isEmpty()) {
        IPAddress ip;

        if (ip.fromString(val)) {
          nvData.gw = ip;
          changed = true;
        }
      }

      val = ui.getString(INPUT_MAC_ID);

      if (!val.isEmpty()) {
        uint8_t m[ETH_ADDR_LEN];

        if (parseMac(val.c_str(), ':', m) == 0) {
          m[0] &= ~0x01;
          memcpy(nvData.mac, m, sizeof(m));
          changed = true;

          Serial.print("Set MAC: ");
          Serial.println(mac2String(nvData.mac));
        }
      }

      val = ui.getString(INPUT_TITLE_ID);

      if (val.length() <= TITLE_MAX_LEN) {
        strncpy(nvData.title, val.c_str(), sizeof(nvData.title));
        changed = true;
      }

      for (int i = 1; i <= INTERFACE_ELEMENTS_COUNT; i++) {
        String s;

        s += SW_PWR_ON_PFX;
        s += "/";
        s += i;

        bool val1 = ui.getBool(s);

        changed = nvData.defaultState[i - 1] != val1;

        nvData.defaultState[i - 1] = val1;
      }

      if (changed) {
        Serial.println("apply NV");
        fData.update();
      }
    }
  }
}

void initPeripheral() {
  pinMode(CH1_GPIO, OUTPUT);
  pinMode(WDT_LED_GPIO, OUTPUT);
  digitalWrite(CH1_GPIO, 0);
  digitalWrite(WDT_LED_GPIO, 1);
}

void setPower(int num, bool state) {
  if (num == 1) {
    digitalWrite(CH1_GPIO, state);
    jeromeOutput[num - 1] = state;
  }
}

bool getPower(int num) {
  if (num == 1) {
    return jeromeOutput[num - 1];
  }

  return false;
}

void jeromeSet(int num, bool state) {
  if (num < 1 || num > JEROME_PORT_COUNT) {
    return;
  }

  if (num == 1) {
    setPower(num, state);
  }
}

void jeromeSetAll(bool state) {
  setPower(1, state);
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

  str.toUpperCase();

  // disconnect the client
  if (str == "BYE") {
    telnet.println("> disconnecting you...");
    telnet.disconnectClient();

    return;
  }

  if (str.startsWith("$KE")) {
    su::Splitter argv(str, ',');

    const uint8_t argc = argv.length();

    if (argv[0] != "$KE") {
      telnet.println("#ERR");

      return;
    }

    if (argc == 1) {
      telnet.println("#OK");

      return;
    }

    if (argv[1] == "WR" && argc == 4) {
      if (argv[2] == "ALL") {
        if (argv[3] == "ON") {
          jeromeSetAll(1);
          telnet.println("#WR,OK");
          return;
        }

        if (argv[3] == "OFF") {
          jeromeSetAll(0);
          telnet.println("#WR,OK");

          return;
        }
      } else {
        const int32_t line = argv[2].toInt();
        const int32_t state = argv[3].toInt();

        if (line > 0 && line <= JEROME_PORT_COUNT && state >= 0 && state <= 1) {
          jeromeSet(line, state);
          telnet.println("#WR,OK");

          return;
        }
      }
    } else if (argv[1] == "WRA" && argc == 3) {
      int len1 = argv[2].length();
      if (len1 <= JEROME_PORT_COUNT) {
        int affected = 0;
        for (uint8_t i = 0; i < len1; i++) {
          const char c = argv[2][i];

          if (c == '1') {
            jeromeSet(i + 1, 1);
            affected++;

          } else if (c == '0') {
            jeromeSet(i + 1, 0);
            affected++;

          } else if (c == 'X') {
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
    } else if (argv[1] == "RID" && argc == 3) {
      if (argv[2] == "ALL") {
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

      const int32_t line = argv[2].toInt();

      if (line >= 1 && line <= JEROME_PORT_COUNT) {
        bool state = jeromeGet(line);

        telnet.print("#RID,");
        telnet.print(line);
        telnet.print(",");
        telnet.println(state);

        return;
      }
    } else if (argv[1] == "RST" && argc == 2) {
      reboot = true;
      telnet.println("#OK");

      return;

    } else if (argv[1] == "DEFAULT" && argc == 2) {
      eraseNv();
      telnet.println("#OK");

      return;
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

void eraseNv() {
  memset(&nvData, 0, sizeof(nvData));
  fData.update();

  reboot = true;
}

void setupNv() {
  memset(&nvData, 0, sizeof(nvData));

  LittleFS.begin(true);

  FDstat_t stat = fData.read();
  bool nvOk = stat == FD_READ;

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
      Serial.println("NV Data Read OK!");
      break;
    default:
      break;
  }

  if (nvData.ipv4 == 0 || !nvOk) {
    nvData.ipv4 = IPAddress(192, 168, 1, 101);
  }

  if (nvData.mask == 0 || !nvOk) {
    nvData.mask = IPAddress(255, 255, 255, 0);
  }

  if (nvData.gw == 0 || !nvOk) {
    nvData.gw = IPAddress(192, 168, 1, 1);
  }

  if ((nvData.mac[0] == 0 && nvData.mac[1] == 0
       && nvData.mac[2] == 0 && nvData.mac[3] == 0
       && nvData.mac[4] == 0 && nvData.mac[5] == 0)
      || !nvOk) {
    for (int i = 0; i < ETH_ADDR_LEN; i++) {
      nvData.mac[i] = random(0xff);
    }

    nvData.mac[0] &= ~0x01;
    nvData.mac[1] |= 0x02;
  }

  for (int i = 0; i < sizeof(nvData.title); i++) {
    if (nvData.title[i] == 0)
      break;

    if (!isPrintable((nvData.title[i]))) {
      memset(nvData.title, 0, sizeof(nvData.title));
    }
  }

  if (!nvOk) {
    for (int i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
      nvData.defaultState[i] = false;
    }
  }

  Serial.print("NV IPv4 ");
  Serial.print(IPAddress(nvData.ipv4));
  Serial.print("/");
  Serial.println(IPAddress(nvData.mask));
  Serial.print("GW IPv4 ");
  Serial.println(IPAddress(nvData.gw));
  Serial.print("NV MAC: ");
  Serial.println(mac2String(nvData.mac));
}

void setupWdt() {
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << 2) - 1,  // Bitmask of all cores
    .trigger_panic = true,
  };

  esp_task_wdt_deinit();
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);  //add current thread to WDT watch
  esp_task_wdt_reset();
}

void tickWdt() {
  if (reboot) {
    if (millis() - lastWdt >= 200) {
      loopCounter++;
      digitalWrite(WDT_LED_GPIO, loopCounter % 2);
      lastWdt = millis();
    }
  }

  if (millis() - lastWdt >= 2000) {
    loopCounter++;
    // Serial.println("Resetting WDT...");
    digitalWrite(WDT_LED_GPIO, loopCounter % 2);
    esp_task_wdt_reset();
    lastWdt = millis();
  }
}

void setupUi() {
  ui.attachBuild(uiBuild);
  ui.attach(uiAction);
  ui.start();
  ui.enableOTA();  // login pass
  //ui.log.start(64);
}

void onButton() {
  if (reboot) {
    return;
  }

  if (btn.click()) {
    setPower(1, !getPower(1));
    return;
  }

  if (btn.pressFor(10000)) {
    eraseNv();
  }
}

void setup() {
  initPeripheral();

  Serial.begin(115200);

  Serial.print(F("Starting version: "));
  Serial.println(FIRMWARE_VERSION);

  setupNv();

  for (int i = 0; i < INTERFACE_ELEMENTS_COUNT; i++) {
    if (nvData.defaultState[i]) {
      setPower(i + 1, true);
    }
  }

  /*WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());*/

  //Network.onEvent(onEvent);

  esp_iface_mac_addr_set(nvData.mac, ESP_MAC_ETH);
  esp_netif_dhcpc_stop(ETH.netif());
  ETH.begin();
  ETH.config(IPAddress(nvData.ipv4), IPAddress(nvData.gw), IPAddress(nvData.mask));

  setupWdt();
  setupUi();
  setupTelnet();

  Serial.print("Init done, IP: ");
  Serial.println(ETH.localIP());
}

void loop() {
  ui.tick();

  if (fData.tick() == FD_WRITE) Serial.println("NV data updated");

  telnet.loop();
  tickWdt();

  if (btn.tick()) {
    onButton();
  }
}
