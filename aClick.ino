#include <esp_task_wdt.h>
#include <GyverPortal.h>
#include <Ethernet.h>
#include <WebServer_WT32_ETH01.h>
#include <GyverShift.h>
#include "ESPTelnet.h"

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

GyverPortal ui;

WebServer wserver(80);

ESPTelnet telnet;
IPAddress ip;

IPAddress myIP(192, 168, 1, 101);
IPAddress myGW(192, 168, 1, 1);
IPAddress mySN(255, 255, 255, 0);
IPAddress myDNS(8, 8, 8, 8);

GyverShift<OUTPUT, CHIP_AMOUNT> outp(CS_595, DAT_595, CLK_595);
bool valSwitch[10];
bool valRst[10];

int lastWdt = millis();
int loopCounter = 0;

// конструктор страницы
void build() {
  GP.BUILD_BEGIN(GP_DARK);

  GP.TITLE("PMUX10", "t1");
  GP.HR();

  // GP.NAV_TABS_LINKS("/,/home,/sett,/kek", "Home,Settings,Kek");

  M_BOX(
    M_BLOCK_TAB(
      "Power",
      M_BOX(GP.LABEL("power1: "); GP.SWITCH("sw1", valSwitch[0]););
      M_BOX(GP.LABEL("power2: "); GP.SWITCH("sw2", valSwitch[1]););
      M_BOX(GP.LABEL("power3: "); GP.SWITCH("sw3", valSwitch[2]););
      M_BOX(GP.LABEL("power4: "); GP.SWITCH("sw4", valSwitch[3]););
      M_BOX(GP.LABEL("power5: "); GP.SWITCH("sw5", valSwitch[4]););
      M_BOX(GP.LABEL("power6: "); GP.SWITCH("sw6", valSwitch[5]););
      M_BOX(GP.LABEL("power7: "); GP.SWITCH("sw7", valSwitch[6]););
      M_BOX(GP.LABEL("power8: "); GP.SWITCH("sw8", valSwitch[7]););
      M_BOX(GP.LABEL("power9: "); GP.SWITCH("sw9", valSwitch[8]););
      M_BOX(GP.LABEL("power10: "); GP.SWITCH("sw10", valSwitch[9]);););

    M_BLOCK_TAB(
      "Reset",
      M_BOX(GP.LABEL("reset1: "); GP.SWITCH("rst1", valRst[0]););
      M_BOX(GP.LABEL("reset2: "); GP.SWITCH("rst2", valRst[1]););
      M_BOX(GP.LABEL("reset3: "); GP.SWITCH("rst3", valRst[2]););
      M_BOX(GP.LABEL("reset4: "); GP.SWITCH("rst4", valRst[3]););
      M_BOX(GP.LABEL("reset5: "); GP.SWITCH("rst5", valRst[4]););
      M_BOX(GP.LABEL("reset6: "); GP.SWITCH("rst6", valRst[5]););
      M_BOX(GP.LABEL("reset7: "); GP.SWITCH("rst7", valRst[6]););
      M_BOX(GP.LABEL("reset8: "); GP.SWITCH("rst8", valRst[7]););
      M_BOX(GP.LABEL("reset9: "); GP.SWITCH("rst9", valRst[8]););
      M_BOX(GP.LABEL("reset10: "); GP.SWITCH("rst10", valRst[9]););););

  /*
  GP.TEXT("txt", "text", valText);
  GP.BREAK();
  GP.NUMBER("num", "number", valNum);
  GP.BREAK();
  GP.PASS("pass", "pass", valPass);
  GP.BREAK();
  GP.SPINNER("spn", valSpin);
  GP.SLIDER("sld", valSlider, 0, 10);
  GP.BREAK();
  GP.DATE("date", valDate);
  GP.BREAK();
  GP.TIME("time", valTime);
  GP.BREAK();
  GP.COLOR("col", valCol);
  GP.BREAK();
  GP.SELECT("sel", "val 1,val 2,val 3", valSelect);
  GP.BREAK();
  GP.RADIO("rad", 0, valRad);
  GP.LABEL("Value 0");
  GP.BREAK();
  GP.RADIO("rad", 1, valRad);
  GP.LABEL("Value 1");
  GP.BREAK();
  GP.RADIO("rad", 2, valRad);
  GP.LABEL("Value 2");
  GP.BREAK();
  GP.RADIO("rad", 3, valRad);
  GP.LABEL("Value 3");
  GP.BREAK();
  GP.BREAK();*/
  //GP.BUTTON("btn", "Button");

  /*
  M_SPOILER(
    "Spoiler",
    GP.LABEL("Hello!"););

  M_BLOCK(
    GP.LABEL("Checks & LED");
    GP.BREAK();
    GP.LABEL_BLOCK("label block");
    GP.LED("");
    GP.CHECK("");
    GP.SWITCH(""););

  M_BLOCK_TAB(
    "Block Tab",
    GP.LABEL("Inputs");
    M_BOX(GP.LABEL("Number"); GP.NUMBER("", "", 123););
    M_BOX(GP.LABEL("Float"); GP.NUMBER_F("", "", 3.14););
    M_BOX(GP.LABEL("Text"); GP.TEXT("", "", "Hello"););
    M_BOX(GP.LABEL("Password"); GP.PASS("", "", "Pass"););
    GP.AREA("", 3, "Text area"););

  M_BLOCK_THIN(
    M_BOX(GP.LABEL("Date"); GP.DATE(""););
    M_BOX(GP.LABEL("Time"); GP.TIME(""););
    M_BOX(GP.LABEL("Color"); GP.COLOR("");););

  M_BLOCK_THIN_TAB(
    "Thin Tab",
    GP.LABEL("Upload File/Folder");
    M_BOX(
      GP_CENTER,
      GP.FILE_UPLOAD("");
      GP.FOLDER_UPLOAD("");););

  M_BOX(GP.LABEL("Select"); GP.SELECT("", "Some,Drop,List"););
  M_BOX(GP.LABEL("Slider"); GP.SLIDER(""););
  M_BOX(GP.LABEL("Spinner"); GP.SPINNER(""););

  GP.BUTTON("", "Button");
  GP.BUTTON_MINI("", "Btn Mini");
*/
  GP.BUILD_END();
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

void onTelnetConnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" connected");

  telnet.println("\nWelcome " + telnet.getIP());
  telnet.println("(Use ^] + q  to disconnect.)");
}

void setupTelnet() {
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  /*telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet.onReconnect(onTelnetReconnect);*/
  //telnet.onDisconnect(onTelnetDisconnect);
  /* telnet.onInputReceived(onTelnetInput);
*/
  Serial.print("- Telnet: ");
  if (telnet.begin(23, false)) {
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

void action() {
  // был клик по компоненту
  if (ui.click()) {
    // проверяем компоненты и обновляем переменные
    /*
    // 1. переписали вручную
    if (ui.click("ch")) {
      valCheck = ui.getBool("ch");
      Serial.print("Check: ");
      Serial.println(valCheck);
      set_rst(1, valCheck);
    }*/

    const char* sw[] = { "sw1", "sw2", "sw3", "sw4", "sw5", "sw6", "sw7", "sw8", "sw9", "sw10" };

    for (uint8_t i = 0; i < (sizeof(sw) / sizeof(sw[0])); i++) {
      if (ui.clickBool(sw[i], valSwitch[i])) {
        set_power(i + 1, valSwitch[i]);
      }
    }

    const char* swRst[] = { "rst1", "rst2", "rst3", "rst4", "rst5", "rst6", "rst7", "rst8", "rst9", "rst10" };

    for (uint8_t i = 0; i < (sizeof(swRst) / sizeof(swRst[0])); i++) {
      if (ui.clickBool(swRst[i], valRst[i])) {
        set_rst(i + 1, valRst[i]);
      }
    }
    /*
    if (ui.clickString("txt", valText)) {
      Serial.print("Text: ");
      Serial.println(valText);
    }

    if (ui.clickInt("num", valNum)) {
      Serial.print("Number: ");
      Serial.println(valNum);
    }

    if (ui.clickStr("pass", valPass)) {
      Serial.print("Password: ");
      Serial.println(valPass);
    }

    if (ui.clickFloat("spn", valSpin)) {
      Serial.print("Spinner: ");
      Serial.println(valSpin);
    }

    if (ui.clickInt("sld", valSlider)) {
      Serial.print("Slider: ");
      Serial.println(valSlider);
    }

    if (ui.clickDate("date", valDate)) {
      Serial.print("Date: ");
      Serial.println(valDate.encode());
    }

    if (ui.clickTime("time", valTime)) {
      Serial.print("Time: ");
      Serial.println(valTime.encode());
    }

    if (ui.clickColor("col", valCol)) {
      Serial.print("Color: ");
      Serial.println(valCol.encode());
    }

    if (ui.clickInt("sel", valSelect)) {
      Serial.print("Select: ");
      Serial.println(valSelect);
    }
    if (ui.clickInt("rad", valRad)) {
      Serial.print("Radio: ");
      Serial.println(valRad);
    }
*/
    //if (ui.click("btn")) Serial.println("Button click");
  }
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
