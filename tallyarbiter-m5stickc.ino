#define C_PLUS 1 //CHANGE TO 1 IF YOU USE THE M5STICK-C PLUS

#if C_PLUS == 1
#include <M5StickCPlus.h>
#else
#include <M5StickC.h>
#endif

#include <WiFi.h>
#include <ESPmDNS.h>
#include <SocketIoClient.h>
#include <Arduino_JSON.h>
#include <PinButton.h>
#include <Preferences.h>

#define GRAY  0x0020 //   8  8  8
#define GREEN 0x0200 //   0 64  0
#define RED   0xF800 // 255  0  0

/* USER CONFIG VARIABLES
 *  Change the following variables before compiling and sending the code to your device.
 */
 
//Wifi SSID and password
const char * networkSSID = "NetworkSSID";
const char * networkPass = "NetworkPass";

//For static IP Configuration, change USE_STATIC to true and define your IP address settings below
bool USE_STATIC = false; // true = use static, false = use DHCP
IPAddress clientIp(192, 168, 2, 5); // Static IP
IPAddress subnet(255, 255, 255, 0); // Subnet Mask
IPAddress gateway(192, 168, 2, 1); // Gateway

//Tally Arbiter Server mDNS name 
const char * tallyarbiter_host = "tallyserver"; //IP address of the Tally Arbiter Server (only use host name and remove .local)
const int tallyarbiter_port = 4455;
/* END OF USER CONFIG */


/* END OF USER CONFIG */

//M5StickC variables
PinButton btnM5(37); //the "M5" button on the device
PinButton btnAction(39); //the "Action" button on the device
Preferences preferences;
uint8_t wasPressed();

//Tally Arbiter variables
SocketIoClient socket;
JSONVar BusOptions;
JSONVar Devices;
JSONVar DeviceStates;
String DeviceId = "unassigned";
String DeviceName = "Unassigned";
String ListenerType = "m5-stickc";
bool mode_preview = false;  
bool mode_program = false;
const byte led_program = 10;
String LastMessage = "";

//General Variables
bool networkConnected = false;
int currentScreen = 0; //0 = Tally Screen, 1 = Settings Screen
int currentBrightness = 11; //12 is Max level

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize the M5StickC object
  logger("Initializing M5StickC+.", "info-quiet");
  M5.begin();
  setCpuFrequencyMhz(80);    //Save battery by turning down the CPU clock
  btStop();                 //Save battery by turning off BlueTooth
  M5.Lcd.setRotation(3);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);
  logger("Tally Arbiter M5StickC+ Listener Client booting.", "info");

  delay(100); //wait 100ms before moving on
  connectToNetwork(); //starts Wifi connection
  while (!networkConnected) {
    delay(200);
  }

  if (!MDNS.begin("esp32")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }

  // Enable interal led for program trigger
  pinMode(led_program, OUTPUT);
  digitalWrite(led_program, HIGH);

  preferences.begin("tally-arbiter", false);
  if(preferences.getString("deviceid").length() > 0){
    DeviceId = preferences.getString("deviceid");
  }
  if(preferences.getString("devicename").length() > 0){
    DeviceName = preferences.getString("devicename");
  }
  preferences.end();
  
  connectToServer();
}

void loop() {
  socket.loop();
  btnM5.update();
  btnAction.update();
  
  if (btnM5.isClick()) {
    switch (currentScreen) {
      case 0:
        showSettings();
        currentScreen = 1;
        break;
      case 1:
        showDeviceInfo();
        currentScreen = 0;
        break;
    }
  }

  if (btnAction.isClick()) {
    updateBrightness();
  }
}

void showSettings() {
  //displays the current network connection and Tally Arbiter server data
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.println("SSID: " + String(networkSSID));
  M5.Lcd.println(WiFi.localIP());
  M5.Lcd.println();
  M5.Lcd.println("Tally Arbiter Server:");
  M5.Lcd.println(String(tallyarbiter_host) + ":" + String(tallyarbiter_port));
  M5.Lcd.println();
  M5.Lcd.println("Battery:");
  int batteryLevel = floor(100.0 * (((M5.Axp.GetVbatData() * 1.1 / 1000) - 3.0) / (4.07 - 3.0)));
  batteryLevel = batteryLevel > 100 ? 100 : batteryLevel;
   if(batteryLevel >= 100){
  M5.Lcd.println("Battery charging...");   // show when M5 is plugged in
  }
  else {
    M5.Lcd.println("Battery:" + String(batteryLevel) + "%");
    }
}

void showDeviceInfo() {
  //displays the currently assigned device and tally data
  evaluateMode();
}

void updateBrightness() {
  if(currentBrightness >= 12) {
    currentBrightness = 7;
  }
  else {
    currentBrightness++;
  }
  M5.Axp.ScreenBreath(currentBrightness);
}

void logger(String strLog, String strType) {
  if (strType == "info") {
    Serial.println(strLog);
    M5.Lcd.println(strLog);
  }
  else {
    Serial.println(strLog);
  }
}

void connectToNetwork() {
  logger("Connecting to SSID: " + String(networkSSID), "info");

  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);
  
  WiFi.mode(WIFI_STA); //station
  WiFi.setSleep(false);
  
  if(USE_STATIC == true) {
    WiFi.config(clientIp, gateway, subnet);
  }
   
  WiFi.begin(networkSSID, networkPass);
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      logger("Network connected!", "info");
      logger(WiFi.localIP().toString(), "info");
      networkConnected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      logger("Network connection lost!", "info");
      networkConnected = false;
      break;
  }
}

void connectToServer() {
  logger("Connecting to Tally Arbiter host: " + String(tallyarbiter_host), "info");
  socket.on("connect", socket_Connected);
  socket.on("bus_options", socket_BusOptions);
  socket.on("deviceId", socket_DeviceId);
  socket.on("devices", socket_Devices);
  socket.on("device_states", socket_DeviceStates);
  socket.on("flash", socket_Flash);
  socket.on("reassign", socket_Reassign);
  socket.on("messaging", socket_Messaging);

  IPAddress serverIp = MDNS.queryHost(tallyarbiter_host);

  if (serverIp) {
    Serial.print("IP address of server: ");
    Serial.println(serverIp.toString());
    socket.begin(serverIp.toString().c_str(), tallyarbiter_port);
  } else {  
    socket.begin(tallyarbiter_host, tallyarbiter_port);
  }
}

void socket_Connected(const char * payload, size_t length) {
  logger("Connected to Tally Arbiter server.", "info");
  String deviceObj = "{\"deviceId\": \"" + DeviceId + "\", \"listenerType\": \"" + ListenerType + "\"}";
  char charDeviceObj[1024];
  strcpy(charDeviceObj, deviceObj.c_str());
  socket.emit("bus_options");
  socket.emit("device_listen_m5", charDeviceObj);
}

void socket_BusOptions(const char * payload, size_t length) {
  BusOptions = JSON.parse(payload);
}

void socket_Devices(const char * payload, size_t length) {
  Devices = JSON.parse(payload);
  SetDeviceName();
}

void socket_DeviceId(const char * payload, size_t length) {
  DeviceId = String(payload);
  SetDeviceName();
  showDeviceInfo();
  currentScreen = 0;
}

void socket_DeviceStates(const char * payload, size_t length) {
  DeviceStates = JSON.parse(payload);
  processTallyData();
}

void socket_Flash(const char * payload, size_t length) {
  //flash the screen white 3 times
  M5.Lcd.fillScreen(WHITE);
  delay(500);
  M5.Lcd.fillScreen(TFT_BLACK);
  delay(500);
  M5.Lcd.fillScreen(WHITE);
  delay(500);
  M5.Lcd.fillScreen(TFT_BLACK);
  delay(500);
  M5.Lcd.fillScreen(WHITE);
  delay(500);
  M5.Lcd.fillScreen(TFT_BLACK);
  
  //then resume normal operation
  switch (currentScreen) {
      case 0:
        showDeviceInfo();
        break;
      case 1:
        showSettings();
        break;
  }
}

void socket_Reassign(const char * payload, size_t length) {
  String oldDeviceId = String(payload).substring(0,8);
  String newDeviceId = String(payload).substring(11);
  String reassignObj = "{\"oldDeviceId\": \"" + oldDeviceId + "\", \"newDeviceId\": \"" + newDeviceId + "\"}";
  char charReassignObj[1024];
  strcpy(charReassignObj, reassignObj.c_str());
  socket.emit("listener_reassign_object", charReassignObj);
  M5.Lcd.fillScreen(WHITE);
  delay(200);
  M5.Lcd.fillScreen(TFT_BLACK);
  delay(200);
  M5.Lcd.fillScreen(WHITE);
  delay(200);
  M5.Lcd.fillScreen(TFT_BLACK);
  DeviceId = newDeviceId;
  preferences.begin("tally-arbiter", false);
  preferences.putString("deviceid", newDeviceId);
  preferences.end();
  SetDeviceName();
}

void socket_Messaging(const char * payload, size_t length) {
  String strPayload = String(payload);
  int typeQuoteIndex = strPayload.indexOf("\"");
  String messageType = strPayload.substring(0, typeQuoteIndex);
  int messageQuoteIndex = strPayload.lastIndexOf("\"");
  String message = strPayload.substring(messageQuoteIndex+1);
  LastMessage = messageType + ": " + message;
  evaluateMode();
}

void processTallyData() {
  for (int i = 0; i < DeviceStates.length(); i++) {
    if (getBusTypeById(JSON.stringify(DeviceStates[i]["busId"])) == "\"preview\"") {
      if (DeviceStates[i]["sources"].length() > 0) {
        mode_preview = true;
      }
      else {
        mode_preview = false;
      }
    }
    if (getBusTypeById(JSON.stringify(DeviceStates[i]["busId"])) == "\"program\"") {
      if (DeviceStates[i]["sources"].length() > 0) {
        mode_program = true;
      }
      else {
        mode_program = false;
      }
    }
  }

  evaluateMode();
}

String getBusTypeById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return JSON.stringify(BusOptions[i]["type"]);
    }
  }

  return "invalid";
}

void SetDeviceName() {
  for (int i = 0; i < Devices.length(); i++) {
    if (JSON.stringify(Devices[i]["id"]) == "\"" + DeviceId + "\"") {
      String strDevice = JSON.stringify(Devices[i]["name"]);
      DeviceName = strDevice.substring(1, strDevice.length() - 1);
      break;
    }
  }
  preferences.begin("tally-arbiter", false);
  preferences.putString("devicename", DeviceName);
  preferences.end();

  evaluateMode();
}

void evaluateMode() {
  M5.Lcd.setCursor(0, 30);
  M5.Lcd.setTextSize(2);
  
  if (mode_preview && !mode_program) {
    logger("The device is in preview.", "info-quiet");
    digitalWrite(led_program, HIGH);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.fillScreen(GREEN);
  }
  else if (!mode_preview && mode_program) {
    logger("The device is in program.", "info-quiet");
    digitalWrite(led_program, LOW);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.fillScreen(RED);
  }
  else if (mode_preview && mode_program) {
    logger("The device is in preview+program.", "info-quiet");
    digitalWrite(led_program, LOW);
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.fillScreen(YELLOW);
  }
  else {
    digitalWrite(led_program, HIGH);
    M5.Lcd.setTextColor(GRAY);
    M5.Lcd.fillScreen(TFT_BLACK);
  }

  M5.Lcd.println(DeviceName);
  M5.Lcd.println(LastMessage);
}
