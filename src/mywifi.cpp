#include "mywifi.h"
#include "configuration.h"
uint32_t rebootOnMsec; // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t lastRun = 0;
// put function declarations here:
void rebootCheck()
{
  if (rebootOnMsec && millis() > rebootOnMsec)
  {

    // DEBUGLN("Rebooting for update");
    ESP.restart();
  }
}

void ota_handle(void *parameter)
{
  for (;;)
  {
    ArduinoOTA.handle();
    vTaskDelay(3500 / portTICK_PERIOD_MS);
  }
}

void setupOTA(const char *nameprefix, const char *ssid, const char *password)
{
  // Configure the hostname
  uint16_t maxlen = strlen(nameprefix) + 7;
  char *fullhostname = new char[maxlen];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  LOG_INFO(fullhostname, maxlen, "%s-%02x%02x%02x", nameprefix, mac[3], mac[4], mac[5]);
  ArduinoOTA.setHostname(fullhostname);
  delete[] fullhostname;

  // Configure and start the WiFi station
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed!");
  }

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232); // Use 8266 port if you are working in Sloeber IDE, it is fixed there and not adjustable

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]()
                     {
  //NOTE: make .detach() here for all functions called by Ticker.h library - not to interrupt transfer process in any way.

    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    LOG_INFO("Start updating %s", type); });

  ArduinoOTA.onEnd([]()
                   { LOG_INFO("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { LOG_INFO("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    LOG_INFO("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("\nAuth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("\nBegin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("\nConnect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("\nReceive Failed");
    else if (error == OTA_END_ERROR) Serial.println("\nEnd Failed"); });

  ArduinoOTA.begin();
  // TelnetStream.begin();

  LOG_INFO("OTA Initialized");
  LOG_INFO("IP address: ");
  LOG_INFO("%s", WiFi.localIP());

  // xTaskCreate(
  //     ota_handle,   /* Task function. */
  //     "OTA_HANDLE", /* String with name of task. */
  //     10000,        /* Stack size in bytes. */
  //     NULL,         /* Parameter passed as input of the task */
  //     1,            /* Priority of the task. */
  //     NULL);        /* Task handle. */
}

void wifiRunOnce()
{
  if (millis() - lastRun > 3000)
  {
    ArduinoOTA.handle();
    lastRun = millis();
    // LOG_DEBUG("RAN OTA");
  }
}