/**************************************************************
   This work is based on many others.
   If published then under whatever licence free to be used,
   distibuted, changed and whatsoever.
   This Work is based on many others 
   and heavily modified for my personal use.
   It is basically based on the following great developments:
   - Adafruit Neopixel https://github.com/adafruit/Adafruit_NeoPixel
   - WS2812FX library https://github.com/kitesurfer1404/WS2812FX
   - fhem esp8266 implementation - Idea from https://github.com/sw-home/FHEM-LEDStripe 
   - FastLED library - see http://www.fastLed.io
   - ESPWebserver - see https://github.com/jasoncoon/esp8266-fastled-webserver
  
  My GIT source code storage
  https://github.com/tobi01001/LED_Stripe_Dynamic_web_conf

 **************************************************************/
#include <FS.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER
#define FASTLED_ESP8266_DMA
#define FASTLED_USE_PROGMEM 1

/* use build flags to define these */
#ifndef LED_NAME
  #error "You need to give your LED a Name (build flag e.g. '-DLED_NAME=\"My LED\"')!"
#endif
//#define LED_NAME "\"LED Dev-Board\""
//#define LED_NAME "\"LED Papa Nachttisch\""
//#define LED_NAME "\"LED Hannah Norah\""
//#define LED_NAME "\"LED Wohnzimmer\""
//#define LED_NAME "\"LED Flurbeleuchtung\""

/* use build flags to define these */

//#define DEBUG
#ifndef LED_COUNT
  #error "You need to define the number of Leds by LED_COUND (build flag e.g. -DLED_COUNT=50)"
#endif

#define LED_PIN 3  // Needs to be 3 (raw value) for ESP8266 because of DMA

#define STRIP_FPS 60          // 60 FPS seems to be a good value
#define STRIP_VOLTAGE 5       // fixed to 5 volts
#define STRIP_MILLIAMPS 2500  // can be changed during runtime

// The delay being used for several init phases.
#define INITDELAY 200

// For the Webserver Answers..
#define ANSWERSTATE 0
#define ANSWERSUNRISE 1


// to count the ARRAY SIZE - not used?
// #define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

/*
extern "C" {
#include "user_interface.h"
}
*/

#include "Arduino.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
//#include <Bounce2.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>


// new approach starts here:
#include "led_strip.h"

/* Flash Button can be used here for toggles.... */
bool hasResetButton = false;
//Bounce debouncer = Bounce();


/* Definitions for network usage */
/* maybe move all wifi stuff to separate files.... */
#define WIFI_TIMEOUT 5000
ESP8266WebServer server(80);
WebSocketsServer webSocketsServer = WebSocketsServer(81);
WiFiManager wifiManager;

String AP_SSID = LED_NAME + String(ESP.getChipId());

//char chrResetButtonPin[3]="X";
//char chrLEDCount[5] = "0";
//char chrLEDPin[2] = "0";

//default custom static IP
//char static_ip[16] = "";
//char static_gw[16] = "";
//char static_sn[16] = "255.255.255.0";

//WiFiManagerParameter ResetButtonPin("RstPin", "Reset Pin", chrResetButtonPin, 3);
//WiFiManagerParameter LedCountConf("LEDCount","LED Count", chrLEDCount, 4);
//WiFiManagerParameter LedPinConf("LEDPIN", "Strip Data Pin", chrLEDPin, 3);

/* END Network Definitions */

/* Removed in favor of Webserver SPIFFS 
extern const char index_html[];
extern const char main_js[];
*/

//String modes = "";

//flag for saving data
bool shouldSaveConfig = false;
bool shouldSaveRuntime = false;

typedef struct {
    uint16_t CRC = 0;
    WS2812FX::segment seg;
    uint8_t brightness = DEFAULT_BRIGHTNESS;
    mysunriseParam sParam;
    uint8_t currentEffect = FX_NO_FX;
    uint8_t pal_num;
    CRGBPalette16 pal;
    //String pal_name = "Red Green White Colors";
    bool stripIsOn = false;
} EEPROMSaveData;

EEPROMSaveData myEEPROMSaveData;

unsigned long last_wifi_check_time = 0;


#include "FSBrowser.h"


// function Definitions
void  saveConfigCallback    (void),
      saveEEPROMData        (void),
      readConfigurationFS   (void),
      initOverTheAirUpdate  (void),
      //setupResetButton      (uint8_t buttonPin),
      updateConfiguration   (void),
      setupWiFi             (void),
      handleRoot            (void),
      srv_handle_main_js    (void),      
      srv_handle_modes      (void),
      srv_handle_pals       (void),
      handleSet             (void),
      handleNotFound        (void),
      handleGetModes        (void),
      handleStatus          (void),
      factoryReset          (void),
      handleResetRequest    (void),
      setupWebServer        (void),
      sendInt               (String name, uint16_t value),
      sendString            (String name, String value),
      sendAnswer            (String jsonAnswer),
      broadcastInt          (String name, uint16_t value),
      broadcastString       (String name, String value),
      webSocketEvent        (uint8_t num, WStype_t type, uint8_t * payload, size_t length),
      clearEEPROM           (void);

      
const String 
      pals_setup            (void),
      modes_setup           (void);




void sendInt(String name, uint16_t value)
{
  String answer = F("{ ");
  answer += F("\"currentState\" : { \"");  
  answer += name;
  answer += F("\": ");
  answer += value;
  answer += " } }";
  #ifdef DEBUG
  Serial.print("Send HTML respone 200, application/json with value: ");
  Serial.println(answer);
  #endif
  server.send(200, "application/json", answer);
}

void sendString(String name, String value)
{
  String answer = F("{ ");
  answer += F("\"currentState\" : { \"");  
  answer += name;
  answer += F("\": \"");
  answer += value;
  answer += "\" } }";
  #ifdef DEBUG
  Serial.print("Send HTML respone 200, application/json with value: ");
  Serial.println(answer);
  #endif
  server.send(200, "application/json", answer);
}

void sendAnswer(String jsonAnswer)
{
  String answer = "{ \"currentState\": { " + jsonAnswer + "} }";
  server.send(200, "application/json", answer);
}

void broadcastInt(String name, uint16_t value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":" + String(value) + "}";
  #ifdef DEBUG
  Serial.print("Send websocket broadcast with value: ");
  Serial.println(json);
  #endif
  webSocketsServer.broadcastTXT(json);
}

void broadcastString(String name, String value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":\"" + String(value) + "\"}";
  #ifdef DEBUG
  Serial.print("Send websocket broadcast with value: ");
  Serial.println(json);
  #endif
  webSocketsServer.broadcastTXT(json);
}

unsigned int calc_CRC16(unsigned int crc, unsigned char *buf, int len)
{
	for (int pos = 0; pos < len; pos++)
	{
		crc ^= (unsigned int)buf[pos];    // XOR byte into least sig. byte of crc

		for (int i = 8; i != 0; i--) {    // Loop over each bit
			if ((crc & 0x0001) != 0) {      // If the LSB is set
				crc >>= 1;                    // Shift right and XOR 0xA001
				crc ^= 0xA001;
			}
			else                            // Else LSB is not set
				crc >>= 1;                    // Just shift right
		}
	}
	return crc;
}

// write runtime data to EEPROM
void saveEEPROMData(void) {
  if(!shouldSaveRuntime) return;
  shouldSaveRuntime = false;
  #ifdef DEBUG
    Serial.println("\nGoing to store runtime on EEPROM...");
  
    Serial.println("\tget segments");
  #endif
  myEEPROMSaveData.seg = strip->getSegments()[0];
  #ifdef DEBUG
    Serial.print("\t\tautoPal ");
    Serial.println(myEEPROMSaveData.seg.autoPal);
    Serial.print("\t\tautoPalDuration ");
    Serial.println(myEEPROMSaveData.seg.autoPalDuration);
    Serial.print("\t\tautoplay ");
    Serial.println(myEEPROMSaveData.seg.autoplay);
    Serial.print("\t\t autoplayDuration ");
    Serial.println(myEEPROMSaveData.seg.autoplayDuration);
    Serial.print("\t\t beat88 ");
    Serial.println(myEEPROMSaveData.seg.beat88);
    Serial.print("\t\t blendType ");
    Serial.println(myEEPROMSaveData.seg.blendType);
    Serial.print("\t\t cooling ");
    Serial.println(myEEPROMSaveData.seg.cooling);
    Serial.print("\t\t deltaHue ");
    Serial.println(myEEPROMSaveData.seg.deltaHue);
    Serial.print("\t\t hueTime ");
    Serial.println(myEEPROMSaveData.seg.hueTime);
    Serial.print("\t\t milliamps ");
    Serial.println(myEEPROMSaveData.seg.milliamps);
    Serial.print("\t\t mode ");
    Serial.println(myEEPROMSaveData.seg.mode);
    Serial.print("\t\t reverse ");
    Serial.println(myEEPROMSaveData.seg.reverse);
    Serial.print("\t\t sparking ");
    Serial.println(myEEPROMSaveData.seg.sparking);
  #endif
  myEEPROMSaveData.brightness = strip->getBrightness();
  #ifdef DEBUG
    Serial.print("\tget brightness ");
    Serial.println(myEEPROMSaveData.brightness);
  #endif
  myEEPROMSaveData.sParam = sunriseParam;
  #ifdef DEBUG
    Serial.println("\tget sunriseparam");
  #endif
  myEEPROMSaveData.currentEffect = currentEffect;
  #ifdef DEBUG
    Serial.print("\tget current effect ");
    Serial.println(myEEPROMSaveData.currentEffect);
  #endif
  myEEPROMSaveData.pal_num = strip->getTargetPaletteNumber();
  #ifdef DEBUG
    Serial.print("\tget pal number ");
    Serial.println(myEEPROMSaveData.pal_num);
  #endif
  myEEPROMSaveData.pal = strip->getTargetPalette();
  #ifdef DEBUG
    Serial.println("\tget palette");
  #endif
  myEEPROMSaveData.stripIsOn = stripIsOn;
  #ifdef DEBUG
    Serial.print("\tget stripIsOn ");
    Serial.println(myEEPROMSaveData.stripIsOn);

    Serial.println("\nGoing to calculate the CRC over the data...");
    Serial.print("\tsize of myEEPROMSaveData\t");
    Serial.println(sizeof(myEEPROMSaveData));
    Serial.print("\tsize of myEEPROMSaveData - 2\t");
    Serial.println(sizeof(myEEPROMSaveData)-2);
    Serial.print("\tsize of myEEPROMSaveData.CRC\t");
    Serial.println(sizeof(myEEPROMSaveData.CRC));
  #endif
  
  myEEPROMSaveData.CRC = (uint16_t)calc_CRC16(0x5a5a, (unsigned char*)&myEEPROMSaveData+2, sizeof(myEEPROMSaveData)-2);

  #ifdef DEBUG
  Serial.printf("\tCRC\t0x%04x\n", myEEPROMSaveData.CRC);
  #endif

  //EEPROM.begin(sizeof(myEEPROMSaveData));
  EEPROM.put(0, myEEPROMSaveData);
  EEPROM.commit();
  //EEPROM.end();
  #ifdef DEBUG
    Serial.println("EEPROM write finished...");
  #endif
}

//callback notifying us of the need to save config
void saveConfigCallback(void) {
  #ifdef DEBUG
    Serial.println("\n\tWe are now invited to save the configuration...");
  #endif // DEBUG
  shouldSaveConfig = true;
}


// reads the stored runtime data from EEPROM
// must be called after everything else is already setup to be working
// otherwise this may terribly fail
void readRuntimeDataEEPROM(void) {
  #ifdef DEBUG
    Serial.println("\n\tReading Config From EEPROM...");
  #endif
  //read the configuration from EEPROM into RAM
  EEPROM.begin(sizeof(myEEPROMSaveData));

  EEPROM.get(0, myEEPROMSaveData);
  //EEPROM.end();

  uint16_t mCRC =  (uint16_t)calc_CRC16( 0x5a5a, 
                            (unsigned char*)&myEEPROMSaveData+2, 
                            sizeof(myEEPROMSaveData)-2);

  if( myEEPROMSaveData.CRC == mCRC) 
  {

    /*strip->setSegment(0, myEEPROMSaveData.seg.start,  myEEPROMSaveData.seg.stop,
                         myEEPROMSaveData.seg.mode,   myEEPROMSaveData.seg.cPalette,
                         myEEPROMSaveData.seg.beat88, myEEPROMSaveData.seg.reverse);
    */
    strip->setBrightness(myEEPROMSaveData.brightness);

    if(strip->getSegments()[0].stop != myEEPROMSaveData.seg.stop)
    {
      myEEPROMSaveData.seg.stop = strip->getSegments()[0].stop;
    }

    strip->getSegments()[0] = myEEPROMSaveData.seg;

    sunriseParam = myEEPROMSaveData.sParam;
    currentEffect = myEEPROMSaveData.currentEffect;
    if(myEEPROMSaveData.pal_num < strip->getPalCount())
    {
      strip->setTargetPalette(myEEPROMSaveData.pal_num);
    }
    else
    {
      strip->setTargetPalette(myEEPROMSaveData.pal, "Custom");
    }
    stripIsOn = myEEPROMSaveData.stripIsOn;
    stripWasOff = stripIsOn;
    previousEffect = currentEffect;    
    //setEffect(currentEffect);
  }
  else // load defaults
  {

  }

  #ifdef DEBUG
  Serial.printf("\tCRC stored\t0x%04x\n", myEEPROMSaveData.CRC);
  Serial.printf("\tCRC calculated\t0x%04x\n", mCRC);
    Serial.println("\tRead Segment Data:");
    Serial.print("\t\tautoPal\t\t ");
    Serial.println(myEEPROMSaveData.seg.autoPal);
    Serial.print("\t\tautoPalDuration\t\t ");
    Serial.println(myEEPROMSaveData.seg.autoPalDuration);
    Serial.print("\t\tautoplay\t\t ");
    Serial.println(myEEPROMSaveData.seg.autoplay);
    Serial.print("\t\t autoplayDuration\t\t ");
    Serial.println(myEEPROMSaveData.seg.autoplayDuration);
    Serial.print("\t\t beat88\t\t ");
    Serial.println(myEEPROMSaveData.seg.beat88);
    Serial.print("\t\t blendType\t\t ");
    Serial.println(myEEPROMSaveData.seg.blendType);
    Serial.print("\t\t cooling\t\t ");
    Serial.println(myEEPROMSaveData.seg.cooling);
    Serial.print("\t\t deltaHue\t\t ");
    Serial.println(myEEPROMSaveData.seg.deltaHue);
    Serial.print("\t\t hueTime\t\t ");
    Serial.println(myEEPROMSaveData.seg.hueTime);
    Serial.print("\t\t milliamps\t\t ");
    Serial.println(myEEPROMSaveData.seg.milliamps);
    Serial.print("\t\t mode\t\t ");
    Serial.println(myEEPROMSaveData.seg.mode);
    Serial.print("\t\t reverse\t\t ");
    Serial.println(myEEPROMSaveData.seg.reverse);
    Serial.print("\t\t sparking\t\t ");
    Serial.println(myEEPROMSaveData.seg.sparking);

    Serial.print("\tget brightness\t\t ");
    Serial.println(myEEPROMSaveData.brightness);
    Serial.print("\tget current effect\t\t ");
    Serial.println(myEEPROMSaveData.currentEffect);
    Serial.print("\tget pal number\t\t ");
    Serial.println(myEEPROMSaveData.pal_num);
    Serial.print("\tget stripIsOn\t\t ");
    Serial.println(myEEPROMSaveData.stripIsOn);
  #endif

  // no need to save right now. next save should be after /set?....
  shouldSaveRuntime = false;
}


void readConfigurationFS(void) {}

/*
void readConfigurationFS(void) {
  //read configuration from FS json


  #ifdef DEBUG
    Serial.println("mounting FS...");
  #endif
  if (SPIFFS.begin()) {
    #ifdef DEBUG
    Serial.println("mounted file system");
    #endif
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      #ifdef DEBUG
      Serial.println("reading config file");
      #endif
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        #ifdef DEBUG
        Serial.println("opened config file");
        #endif
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          #ifdef DEBUG
          Serial.println("\nparsed json");
          #endif
          //strcpy(chrResetButtonPin, json["chrResetButtonPin"]);
          //strcpy(chrLEDCount, json["chrLEDCount"]);
          //strcpy(chrLEDPin, json["chrLEDPin"]);
          // This checks if a IP is contained in the file
          // currently not used as no IP will be written
          if(json["ip"]) {
            #ifdef DEBUG
            Serial.println("setting custom ip from config");
            #endif
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            #ifdef DEBUG
            Serial.println(static_ip);
            #endif
          } else {
            #ifdef DEBUG
            Serial.println("no custom ip in config");
            #endif
          }
        } else {
          #ifdef DEBUG
          Serial.println("failed to load json config");
          #endif
        }
      }
    }
  } else {
    #ifdef DEBUG
    Serial.println("failed to mount FS");
    #endif
  }
  //end read
  #ifdef DEBUG
  Serial.print("Static IP: \t");
  Serial.println(static_ip);
  //Serial.print("LED Count: \t");
  //Serial.println(chrLEDCount);
  //Serial.print("LED Pin: \t");
  //Serial.println(chrLEDPin);
  //Serial.print("Rst Btn Pin: \t");
  //Serial.println(chrResetButtonPin);
  #endif
}
*/

bool OTAisRunning = false;

void initOverTheAirUpdate(void) {
  #ifdef DEBUG
  Serial.println("\nInitializing OTA capabilities....");
  #endif
  FastLED.showColor(CRGB::Blue);
  delay(INITDELAY);
  /* init OTA */
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // ToDo: Implement Hostname in config and WIFI Settings?

  // Hostname defaults to esp8266-[ChipID]
  //ArduinoOTA.setHostname("esp8266Toby01");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    #ifdef DEBUG
    Serial.println("OTA start");
    #endif
    
    setEffect(FX_NO_FX);
    reset();
    uint8_t factor = 85;
    for(uint8_t c = 0; c < 4; c++) {

      for(uint16_t i=0; i<strip->getLength(); i++) {
        uint8_t r = 256 - (c*factor);
        uint8_t g = c > 0 ? (c*factor-1) : (c*factor);
        //strip.setPixelColor(i, r, g, 0);
        strip->leds[i] = CRGB(strip_color32(r,g,0));
      }
      strip->show();
      delay(INITDELAY);
      for(uint16_t i=0; i<strip->getLength(); i++) {
        strip->leds[i] = CRGB::Black;
        //strip.setPixelColor(i, 0x000000);
      }
      strip->show();
      delay(INITDELAY);
    }
    webSocketsServer.disconnect();
    server.stop();
    OTAisRunning = true;
  });
  ArduinoOTA.onEnd([]() {
    #ifdef DEBUG
    Serial.println("\nOTA end");
    #endif
    // clearEEPROM();
    // OTA finished.
    // Green Leds fade out.
    for(uint8_t i = strip->leds[i].green; i>0; i--)
    {
      for(uint16_t p=0; p<strip->getLength(); p++)
      {
        strip->leds[p].subtractFromRGB(2);
        //strip.setPixelColor(p, 0, i-1 ,0);
      }
      strip->show();
      delay(2);
    }
    OTAisRunning = false;
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef DEBUG
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    #endif
    // OTA Update will show increasing green LEDs during progress:
    uint8_t color = 0;

    uint16_t progress_value = progress*100 / (total / strip->getLength());
    uint16_t pixel = (uint16_t) (progress_value / 100);
    uint16_t temp_color = progress_value - (pixel*100);
    if(temp_color > 255) temp_color = 255;

    //uint16_t pixel = (uint16_t)(progress / (total / strip.getLength()));
    //strip.setPixelColor(pixel, 0, (uint8_t)temp_color, 0);
    strip->leds[pixel] = strip_color32(0, (uint8_t)temp_color, 0);
    strip->show();
    //delay(1);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
    // something went wrong during OTA.
    // We will fade in to red...
    for(uint16_t c = 0; c<256; c++)
    {
      for(uint16_t i = 0; i<strip->getLength(); i++)
      {
        //strip.setPixelColor(i,(uint8_t)c,0,0);
        strip->leds[i] = strip_color32((uint8_t)c,0,0);
      }
      strip->show();
      delay(2);
    }
    delay(3000);
    ESP.reset();
  });
  ArduinoOTA.begin();
  #ifdef DEBUG
  Serial.println("OTA capabilities initialized....");
  #endif
  FastLED.showColor(CRGB::Green);
  delay(INITDELAY);
  FastLED.showColor(CRGB::Black);
  delay(INITDELAY);
}

/*
void setupResetButton(uint8_t buttonPin){
    pinMode(buttonPin,INPUT_PULLUP);
    // After setting up the button, setup the Bounce instance :
    debouncer.attach(buttonPin);
    debouncer.interval(50); // interval in ms
}
*/

void updateConfiguration(void){}
// void updateConfiguration(void){
//   #ifdef DEBUG
//   Serial.println("Updating configuration just received");
//   #endif
//   // only copy the values in case the Parameter wa sset in config!
//   if(shouldSaveConfig) {
//   //  strcpy(chrResetButtonPin, ResetButtonPin.getValue());
//   //  strcpy(chrLEDCount, LedCountConf.getValue());
//   //  strcpy(chrLEDPin, LedPinConf.getValue());
//   }
//   /*
//   String sLedCount = chrLEDCount;
//   String sLedPin   = chrLEDPin;

//   uint16_t ledCount = sLedCount.toInt();
//   uint8_t ledPin = sLedPin.toInt();
//   */

//   /*
//   uint16_t ledCount = (uint16_t) strtoul(chrLEDCount, NULL, 10);
//   uint8_t ledPin = (uint8_t) strtoul(chrLEDPin, NULL, 10);
//   // if something went wrong here (GPIO = 0 or LEDs = 0)
//   // we reset and start allover again
//   if(ledCount == 0 || ledPin == 0) {
//     #ifdef DEBUG
//     Serial.println("\n\tSomething went wrong! Config will be deleted and ESP Reset!");
//     #endif
//     SPIFFS.format();
//     wifiManager.resetSettings();
//     #ifdef DEBUG
//     Serial.println("\nCountdown to Reset:");
//     #endif
//     for(uint8_t i = 0; i<10; i++) {
//       #ifdef DEBUG
//       Serial.println(10-i);
//       #endif
//     }
//     ESP.reset();
//   }
//   #ifdef DEBUG
//   Serial.print("LEDCount: ");
//   Serial.println(ledCount);

//   Serial.print("LED datapin: ");
//   Serial.println(ledPin);
//   #endif
//   if(chrResetButtonPin[0] == 'x' || chrResetButtonPin[0] == 'X') {
//       hasResetButton = false;
//       #ifdef DEBUG
//       Serial.println("No Reset Button specified.");
//       #endif
//   } else {
//       String srstPin = chrResetButtonPin;
//       uint8_t rstPin =  srstPin.toInt();
//       #ifdef DEBUG
//       Serial.print("Reset Button Pin: ");
//       Serial.println(rstPin);
//       #endif
//       hasResetButton = true;
//       setupResetButton(rstPin);
//   }
//   */

//   /*
//   if (shouldSaveConfig) {
//     #ifdef DEBUG
//     Serial.println("saving config");
//     #endif
//     DynamicJsonBuffer jsonBuffer;
//     JsonObject& json = jsonBuffer.createObject();
//     json["chrResetButtonPin"] = chrResetButtonPin;
//     json["chrLEDCount"] = chrLEDCount;
//     json["chrLEDPin"] = chrLEDPin;
//   */
//     /*  Maybe we deal with static IP later.
//         For now we just don't save it....
//         json["ip"] = WiFi.localIP().toString();
//         json["gateway"] = WiFi.gatewayIP().toString();
//         json["subnet"] = WiFi.subnetMask().toString();
//     */
//   /*
//     File configFile = SPIFFS.open("/config.json", "w");
//     if (!configFile) {
//       #ifdef DEBUG
//       Serial.println("failed to open config file for writing");
//       #endif
//     }
//     #ifdef DEBUG
//     json.prettyPrintTo(Serial);
//     #endif
//     json.printTo(configFile);
//     configFile.close();
//     //end save
//   }
//   */
//   #ifdef DEBUG
//   Serial.println("\nEverything in place... setting up stripe.");
//   #endif
//   //stripe_setup( LED_COUNT, STRIP_FPS, STRIP_VOLTAGE, STRIP_MILLIAMPS, RainbowColors_p, F("Rainbow Colors"), TypicalLEDStrip);
// }

void setupWiFi(void){

  
  FastLED.showColor(CRGB::Blue);
  delay(INITDELAY);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

/*
  wifiManager.addParameter(&ResetButtonPin);
  wifiManager.addParameter(&LedCountConf);
  wifiManager.addParameter(&LedPinConf);
*/
  // 4 Minutes should be sufficient.
  // Especially in case of WiFi loss...
  wifiManager.setConfigPortalTimeout(240);

  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP" with password "password"
  //and goes into a blocking loop awaiting configuration
  #ifdef DEBUG
  Serial.println("Going to autoconnect and/or Start AP");
  #endif
  if (!wifiManager.autoConnect(AP_SSID.c_str())) {
    #ifdef DEBUG
    Serial.println("failed to connect, we should reset as see if it connects");
    #endif
    FastLED.showColor(CRGB::Yellow);
    delay(3000);
    FastLED.showColor(CRGB::Red);
    ESP.reset();
    delay(5000);
  }
  #ifdef DEBUG
  /*
  Serial.println("Print LED config again to be sure: ");
  Serial.print("LED Count: \t");
  Serial.println(chrLEDCount);
  Serial.print("LED Pin: \t");
  Serial.println(chrLEDPin);
  Serial.print("Rst Btn Pin: \t");
  Serial.println(chrResetButtonPin);
  */

  //if you get here you have connected to the WiFi
  Serial.print("local ip: ");
  Serial.println(WiFi.localIP());
  #endif

  FastLED.showColor(CRGB::Green);
  delay(INITDELAY);
  FastLED.showColor(CRGB::Black);
}

/*
void handleRoot(void){
    server.send_P(200,"text/html", index_html);
    #ifdef DEBUG
    Serial.println("\t/ called from server...\n");
    #endif
}
*/
/*
void srv_handle_main_js(void) {
  server.send_P(200,"application/javascript", main_js);
}
*/

const String modes_setup(void) {
  String modes = "";
  uint8_t num_modes = strip->getModeCount();
  for(uint8_t i=0; i < num_modes; i++) {
    uint8_t m = i;
    modes += F("<a href='#' class='mo' id='");
    modes += m;
    modes += F("'>");
    modes += strip->getModeName(m);
    modes += F("</a>");
  }
  return modes;
}

const String pals_setup(void) {
  uint8_t num_modes = strip->getPalCount();
  String palettes = "";
  palettes.reserve(400);
  for(uint8_t i=0; i <  num_modes; i++)
  {
    palettes += F("<a href='#' class='pa' id='");
    palettes += i;
    palettes += F("'>");
    palettes += strip->getPalName(i);
    palettes += F("</a>");
  }
  return palettes;
}

void srv_handle_modes(void) {
  server.send(200,"text/plain", modes_setup());
}

void srv_handle_pals(void) {
  server.send(200,"text/plain", pals_setup());
}

uint8_t changebypercentage (uint8_t value, uint8_t percentage) {
  uint16_t ret = max((value*percentage)/100, 10);
  if (ret > 255) ret = 255;
  return (uint8_t) ret;
}

// if /set was called
void handleSet(void) {

  // Debug only
  #ifdef DEBUG
  Serial.println("<Begin>Server Args:");
  for(uint8_t i = 0; i<server.args(); i++) {
    Serial.print(server.argName(i));
    Serial.print("\t");
    Serial.println(server.arg(i));
    Serial.print(server.argName(i));
    Serial.print("\t char[0]: ");
    Serial.println(server.arg(i)[0]);
  }
  Serial.println("<End> Server Args");
  #endif
  // to be completed in general
  // question: is there enough memory to store color and "timing" per pixel?
  // i.e. uint32_t onColor, OffColor, uint16_t ontime, offtime
  // = 12 * 300 = 3600 byte...???
  // this will be a new branch possibly....

  // mo = mode set (eihter +, - or value)
  // br = brightness (eihter +, - or value)
  // co = color (32 bit unsigned color)
  // re = red value of color (eihter +, - or value)
  // gr = green value of color (eihter +, - or value)
  // bl = blue value of color (eihter +, - or value)
  // sp = speed (eihter +, - or value)
  // sec = sunrise / sunset time in seconds....
  // min = sunrise/ sunset time in minutes
  // pi = pixel to be set (clears others?)
  // rnS = Range start Pixel;
  // rnE = Range end Pixel;

  // here we set a new mode if we have the argument mode
  if(server.hasArg("mo")) {
    bool isWS2812FX = false;
    uint8_t effect = strip->getMode();
    #ifdef DEBUG
    Serial.println("got Argument mo....");
    #endif
    // just switch to the next
    if (server.arg("mo")[0] == 'u') {
    #ifdef DEBUG
    Serial.println("got Argument mode up....");
    #endif
      effect = effect + 1;
      isWS2812FX = true;
    }
    // switch to the previous one
    else if (server.arg("mo")[0] == 'd') {
      #ifdef DEBUG
      Serial.println("got Argument mode down....");
      #endif
      effect = effect - 1;
      isWS2812FX = true;
    }
    else if (server.arg("mo")[0] == 'o') {
      #ifdef DEBUG
      Serial.println("got Argument mode Off....");
      #endif
      sendString("state", "off");
      reset();
      strip_On_Off(false);
      strip->stop();
      
    }
    else if (server.arg("mo")[0] == 'f') {
      #ifdef DEBUG
      Serial.println("got Argument fire....");
      #endif
      effect = FX_MODE_FIRE_FLICKER;
      isWS2812FX = true;
    }
    else if (server.arg("mo")[0] == 'r') {
      #ifdef DEBUG
      Serial.println("got Argument mode rainbow cycle....");
      #endif
      effect = FX_MODE_RAINBOW_CYCLE;
      isWS2812FX = true;
    }
    else if (server.arg("mo")[0] == 'k') {
      #ifdef DEBUG
      Serial.println("got Argument mode KITT....");
      #endif
      effect = FX_MODE_LARSON_SCANNER;
      isWS2812FX = true;
    }
    else if (server.arg("mo")[0] == 's') {
      #ifdef DEBUG
      Serial.println("got Argument mode Twinkle Fox....");
      #endif
      effect = FX_MODE_TWINKLE_FOX;
      isWS2812FX = true;
    }
    else if (server.arg("mo")[0] == 'w') {
      #ifdef DEBUG
      Serial.println("got Argument mode White Twinkle....");
      #endif
      strip->setColor(CRGBPalette16(CRGB::White));
      effect = FX_MODE_TWINKLE_FADE;
      isWS2812FX = true;
    }
    // sunrise effect
    // + delta value
    // ToDo Implement
    //#pragma message "Change Sunrise / Sunset to different unique modes in set call!"
    else if (server.arg("mo") == "Sunrise") {
      #ifdef DEBUG
      Serial.println("got Argument mode sunrise....");
      #endif
      // milliseconds time to full sunrise
      uint32_t mytime = 0;
      const uint16_t mysteps = 512; // defaults to 512;
      // sunrise time in seconds
      if(server.hasArg("sec")) {
        #ifdef DEBUG
        Serial.println("got Argument sec....");
        #endif
        mytime = 1000 * (uint32_t)strtoul(&server.arg("sec")[0], NULL, 10);
      }
      // sunrise time in minutes
      else if(server.hasArg("min")) {
        #ifdef DEBUG
        Serial.println("got Argument min....");
        #endif
        mytime = (1000 * 60) * (uint8_t)strtoul(&server.arg("min")[0], NULL, 10);
      }
      // use default if time = 0;
      // ToDo: Maybe use "stored", i.e. last value?
      if(mytime == 0) {
        // default will be 10 minutes
        // = (1000 ms * 60) = 1 minute *10 = 10 minutes
        mytime = 1000 * 60 * 10; // for readability
      }
      mySunriseStart(mytime, mysteps, true);
      setEffect(FX_SUNRISE);
      handleStatus();
    }
    // sunrise effect
    // + delta value
    // ToDo Implement
    else if (server.arg("mo") == "Sunset") {
      #ifdef DEBUG
      Serial.println("got Argument mode sunset....");
      #endif
      // milliseconds time to full sunrise
      uint32_t mytime = 0;
      const uint16_t mysteps = 512; // defaults to 1000;
      // sunrise time in seconds
      if(server.hasArg("sec")) {
        #ifdef DEBUG
        Serial.println("got Argument sec....");
        #endif
        mytime = 1000 * (uint32_t)strtoul(&server.arg("sec")[0], NULL, 10);
      }
      // sunrise time in minutes
      else if(server.hasArg("min")) {
        #ifdef DEBUG
        Serial.println("got Argument min....");
        #endif
        mytime = (1000 * 60) * (uint8_t)strtoul(&server.arg("min")[0], NULL, 10);
      }
      // use default if time = 0;
      // ToDo: Maybe use "stored", i.e. last value?
      if(mytime == 0) {
        // default will be 10 minutes
        // = (1000 ms * 60) = 1 minute *10 = 10 minutes
        mytime = 1000 * 60 * 10; // for readability
      }
      mySunriseStart(mytime, mysteps, false);
      setEffect(FX_SUNSET);
      handleStatus();
    }
    // finally switch to the one being provided.
    // we don't care if its actually an int or not
    // because it will be zero anyway if not.
    else {
      #ifdef DEBUG
      Serial.println("got Argument mode and seems to be an Effect....");
      #endif
      effect = (uint8_t)strtoul(&server.arg("mo")[0], NULL, 10);
      isWS2812FX = true;
    }
    // make sure we roll over at the max number
    if(effect >= strip->getModeCount()) {
      #ifdef DEBUG
      Serial.println("Effect to high....");
      #endif
      effect = 0;
    }
    // activate the effect and trigger it once...
    if(isWS2812FX) {
      setEffect(FX_WS2812);
      strip->setMode(effect);
      strip->start();
      #ifdef DEBUG
      Serial.println("gonna send mo response....");
      #endif
      //strip->trigger();
      sendAnswer(  "\"mode\": 3, \"modename\": \"" + 
                  (String)strip->getModeName(effect) + 
                  "\", \"wsfxmode\": " + String(effect));
      broadcastInt("mo", effect);
    }
  }
  if(server.hasArg("power"))
  {
    #ifdef DEBUG
      Serial.println("got Argument power....");
      #endif
    if(server.arg("power")[0] == '0')
    {
      //reset();
      strip_On_Off(false);
      //FastLED.clearData();
    }
    else
    {
      strip_On_Off(true);
    }
    sendString("state", stripIsOn?"on":"off");
    broadcastInt("power", stripIsOn);
  }
    

  // if we got a palette change
  if(server.hasArg("pa")) {
    // ToDo: Possibility to setColors and new Palettes...
    uint8_t pal = (uint8_t)strtoul(&server.arg("pa")[0], NULL, 10);
    #ifdef DEBUG
    Serial.print("New palette with value: ");
    Serial.println(pal);
    #endif
    strip->setTargetPalette(pal);
    sendAnswer(   "\"palette\": " + String(pal) + ", \"palette name\": \"" + 
                  (String)strip->getPalName(pal) + "\"");
    broadcastInt("pa", pal);
  }



  // if we got a new brightness value
  if(server.hasArg("br")) {
    #ifdef DEBUG
      Serial.println("got Argument brightness....");
      #endif
    uint8_t brightness = strip->getBrightness();
    if (server.arg("br")[0] == 'u') {
    brightness = changebypercentage(brightness, 110);
    } else if (server.arg("br")[0] == 'd') {
      brightness = changebypercentage(brightness, 90);
    } else {
      brightness = constrain((uint8_t)strtoul(&server.arg("br")[0], NULL, 10), BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    }
    strip->setBrightness(brightness);
    sendInt("brightness", brightness);
    broadcastInt("br", strip->getBrightness());
    //strip->show();
  }
  // if we got a speed value
  if(server.hasArg("sp")) {
    #ifdef DEBUG
      Serial.println("got Argument speed....");
      #endif
    uint16_t speed = strip->getBeat88();
    if (server.arg("sp")[0] == 'u') {
      uint16_t ret = max((speed*115)/100, 10);
      if (ret > BEAT88_MAX) ret = BEAT88_MAX;
      speed = ret;
      //speed = changebypercentage(speed, 110);
    } else if (server.arg("sp")[0] == 'd') {
      uint16_t ret = max((speed*80)/100, 10);
      if (ret > BEAT88_MAX) ret = BEAT88_MAX;
      speed = ret;
      //speed = changebypercentage(speed, 90);
    } else {
      speed = constrain((uint16_t)strtoul(&server.arg("sp")[0], NULL, 10), BEAT88_MIN, BEAT88_MAX);
    }
    strip->setSpeed(speed);
    // delay_interval = (uint8_t)(speed / 256); // obsolete???
    strip->show();
    sendAnswer( "\"speed\": " + String(speed) + ", \"beat88\": \"" + String(speed));
    broadcastInt("sp", strip->getBeat88());
  }

  // if we got a speed value
  if(server.hasArg("be")) {
    #ifdef DEBUG
      Serial.println("got Argument speed (beat)....");
      #endif
    uint16_t speed = strip->getBeat88();
    if (server.arg("be")[0] == 'u') {
      uint16_t ret = max((speed*115)/100, 10);
      if (ret > BEAT88_MAX) ret = BEAT88_MAX;
      speed = ret;
    } else if (server.arg("be")[0] == 'd') {
      uint16_t ret = max((speed*80)/100, 10);
      if (ret > BEAT88_MAX) ret = BEAT88_MAX;
      speed = ret;
    } else {
      speed = constrain((uint16_t)strtoul(&server.arg("be")[0], NULL, 10), BEAT88_MIN, BEAT88_MAX);
    }
    strip->setSpeed(speed);
    strip->show();
    sendAnswer( "\"speed\": " + String(speed) + ", \"beat88\": \"" + String(speed));
    broadcastInt("sp", strip->getBeat88());
  }

  // color handling
  uint32_t color = strip->getColor(0);
  bool setColor = false;
  if(server.hasArg("re")) {
    setColor = true;
    #ifdef DEBUG
      Serial.println("got Argument red....");
      #endif
    uint8_t re = Red(color);
    if(server.arg("re")[0] == 'u') {
        re = changebypercentage(re, 110);
    } else if (server.arg("re")[0] == 'd') {
      re = changebypercentage(re, 90);
    } else {
      re = constrain((uint8_t)strtoul(&server.arg("re")[0], NULL, 10), 0, 255);
    }
    color = (color & 0x00ffff) | (re << 16);
  }
  if(server.hasArg("gr")) {
    setColor = true;
    #ifdef DEBUG
      Serial.println("got Argument green....");
      #endif
    uint8_t gr = Green(color);
    if(server.arg("gr")[0] == 'u') {
        gr = changebypercentage(gr, 110);
    } else if (server.arg("gr")[0] == 'd') {
      gr = changebypercentage(gr, 90);
    } else {
      gr = constrain((uint8_t)strtoul(&server.arg("gr")[0], NULL, 10), 0, 255);
    }
    color = (color & 0xff00ff) | (gr << 8);
  }
  if(server.hasArg("bl")) {
    setColor = true;
    #ifdef DEBUG
      Serial.println("got Argument blue....");
      #endif
    uint8_t bl = Blue(color);
    if(server.arg("bl")[0] == 'u') {
        bl = changebypercentage(bl, 110);
    } else if (server.arg("bl")[0] == 'd') {
      bl = changebypercentage(bl, 90);
    } else {
      bl = constrain((uint8_t)strtoul(&server.arg("bl")[0], NULL, 10), 0, 255);
    }
    color = (color & 0xffff00) | (bl << 0);
  }
  if(server.hasArg("co")) {
    setColor = true;
    #ifdef DEBUG
      Serial.println("got Argument color....");
      #endif
    color = constrain((uint32_t)strtoul(&server.arg("co")[0], NULL, 16), 0, 0xffffff);
  }
  if(server.hasArg("solidColor"))
  {
    setColor = true;
    #ifdef DEBUG
      Serial.println("got Argument solidColor....");
      #endif
    uint8_t r,g,b;
    r = constrain((uint8_t)strtoul(&server.arg("r")[0], NULL, 10), 0, 255);  
    g = constrain((uint8_t)strtoul(&server.arg("g")[0], NULL, 10), 0, 255);  
    b = constrain((uint8_t)strtoul(&server.arg("b")[0], NULL, 10), 0, 255); 
    color = (r << 16) | (g << 8) | (b << 0);
    CRGB solidColor(color);
    
    //sendString(String(solidColor.r) + "," + String(solidColor.g) + "," + String(solidColor.b));
    //broadcastString("solidColor", String(solidColor.r) + "," + String(solidColor.g) + "," + String(solidColor.b));
    broadcastInt("pa", strip->getPalCount());
  }
  if(server.hasArg("pi")) {
    #ifdef DEBUG
      Serial.println("got Argument pixel....");
      #endif
    //setEffect(FX_NO_FX);
    uint16_t pixel = constrain((uint16_t)strtoul(&server.arg("pi")[0], NULL, 10), 0, strip->getLength()-1);
    strip_setpixelcolor(pixel, color);
    handleStatus();  
  } else if (server.hasArg("rnS") && server.hasArg("rnE")) {
    #ifdef DEBUG
      Serial.println("got Argument range start / range end....");
      #endif
    uint16_t start = constrain((uint16_t)strtoul(&server.arg("rnS")[0], NULL, 10), 0, strip->getLength());
    uint16_t end = constrain((uint16_t)strtoul(&server.arg("rnE")[0], NULL, 10), start, strip->getLength());
    set_Range(start, end, color);
    handleStatus();
  } else if (server.hasArg("rgb")) {
    #ifdef DEBUG
      Serial.println("got Argument rgb....");
      #endif
    strip->setColor(color);
    setEffect(FX_WS2812);
    strip->setMode(FX_MODE_STATIC);
    handleStatus();
  } else {
    if(setColor) {
      strip->setColor(color);
      handleStatus();
    }
  }

  if(server.hasArg("autoplay"))
  {
    uint16_t value = String(server.arg("autoplay")).toInt();
    strip->getSegments()[0].autoplay = value;
    sendInt("Autoplay Mode", value);
    broadcastInt("autoplay", value);
  }

  if(server.hasArg("autoplayDuration"))
  {
    uint16_t value = String(server.arg("autoplayDuration")).toInt();
    strip->setAutoplayDuration(value);
    sendInt("Autoplay Mode Interval", value);
    broadcastInt("autoplayDuration", value);
  }

  if(server.hasArg("autopal"))
  {
    uint16_t value = String(server.arg("autopal")).toInt();
    strip->getSegments()[0].autoPal = value;
    sendInt("Autoplay Palette", value);
    broadcastInt("autopal", value);
  }

  if(server.hasArg("autopalDuration"))
  {
    uint16_t value = String(server.arg("autopalDuration")).toInt();
    strip->setAutoPalDuration(value);
    sendInt("Autoplay Palette Interval", value);
    broadcastInt("autopalDuration", value);
  }


  if(server.hasArg("huetime"))
  {
    uint16_t value = String(server.arg("huetime")).toInt();
    sendInt("Hue change time", value);
    broadcastInt("huetime", value);
    strip->sethueTime(value);
  }

  if(server.hasArg("deltahue"))
  {
    uint16_t value = constrain(String(server.arg("deltahue")).toInt(), 0, 255);
    sendInt("Delta hue per change", value);
    broadcastInt("deltahue", value);
    strip->getSegments()[0].deltaHue = value;
  }

  if(server.hasArg("cooling"))
  {
    uint16_t value = String(server.arg("cooling")).toInt();
    sendInt("Fire Cooling", value);
    broadcastInt("cooling", value);
    strip->getSegments()[0].cooling = value;
  }


  if(server.hasArg("sparking"))
  {
    uint16_t value = String(server.arg("sparking")).toInt();
    sendInt("Fire sparking", value);
    broadcastInt("sparking", value);
    strip->getSegments()[0].sparking = value;
  }

  if(server.hasArg("twinkleSpeed"))
  {
    uint16_t value = String(server.arg("twinkleSpeed")).toInt();
    sendInt("Twinkle Speed", value);
    broadcastInt("twinkleSpeed", value);
    strip->getSegments()[0].twinkleSpeed = value;
  }

  if(server.hasArg("twinkleDensity"))
  {
    uint16_t value = String(server.arg("twinkleDensity")).toInt();
    sendInt("Twinkle Density", value);
    broadcastInt("twinkleDensity", value);
    strip->getSegments()[0].cooling = value;
  }

  if(server.hasArg("blendType"))
  {
    uint16_t value = String(server.arg("blendType")).toInt();
    
    broadcastInt("blendType", value);
    if(value) {
      strip->getSegments()[0].blendType = LINEARBLEND;
      sendString("BlendType", "LINEARBLEND");
    }
    else {
      strip->getSegments()[0].blendType = NOBLEND;
      sendString("BlendType", "NOBLEND");
    }
  }

  if(server.hasArg("reverse"))
  {
    uint16_t value = String(server.arg("reverse")).toInt();
    sendInt("Reverse", value);
    broadcastInt("reverse", value);
    strip->getSegments()[0].reverse = value;
  }

  if(server.hasArg("current"))
  {
    uint16_t value = String(server.arg("current")).toInt();
    sendInt("Lamp Max Current", value);
    broadcastInt("current", value);
    strip->setMilliamps(value);
  }

  if(server.hasArg("LEDblur"))
  {
    uint8_t value = String(server.arg("LEDblur")).toInt();
    sendInt("LEDblur", value);
    broadcastInt("LEDblur", value);
    strip->setBlurValue(value);
  }


  //handleStatus();
  shouldSaveRuntime = true;
}

// if something unknown was called...
void handleNotFound(void){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleGetModes(void){
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(56) + 1070;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject& root = jsonBuffer.createObject();

  JsonObject& modeinfo = root.createNestedObject("modeinfo");
  modeinfo["count"] = strip->getModeCount();

  JsonObject& modeinfo_modes = modeinfo.createNestedObject("modes");
  for(uint8_t i=0; i<strip->getModeCount(); i++) {
      modeinfo_modes[strip->getModeName(i)] = i;
  }

  #ifdef DEBUG
  root.printTo(Serial);
  #endif

  String message = "";
  root.prettyPrintTo(message);
  server.send(200, "application/json", message);
}

void handleGetPals(void){
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(56) + 1070;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject& root = jsonBuffer.createObject();

  JsonObject& modeinfo = root.createNestedObject("palinfo");
  modeinfo["count"] = strip->getPalCount();

  JsonObject& modeinfo_modes = modeinfo.createNestedObject("pals");
  for(uint8_t i=0; i<strip->getPalCount(); i++) {
      modeinfo_modes[strip->getPalName(i)] = i;
  }

  #ifdef DEBUG
  root.printTo(Serial);
  #endif

  String message = "";
  root.prettyPrintTo(message);
  server.send(200, "application/json", message);
}

void handleStatus(void){
  uint32_t answer_time = millis();

  String message;
  message.reserve(1500);
  uint16_t num_leds_on = 0;
  // if brightness = 0, no LED can be lid.
  if(strip->getBrightness()) {
    // count the number of active LEDs
    // in rare occassions, this can still be 0, depending on the effect.
    for(uint16_t i=0; i<strip->getLength(); i++) {
      if(strip->leds[i]) num_leds_on++;
    }
  }

  message += F("{\n  \"currentState\": {\n    \"state\": ");
  if(stripIsOn) {
    message += F("\"on\"");
  } else {
    message += F("\"off\"");
  }
  message += F(",\n    \"Lampenname\": ");
  message += String(LED_NAME);
  message += F(",\n    \"Anzahl Leds\": ");
  message += String(strip->getLength());
  message += F(",\n    \"Lamp Voltage\": ");
  message += String(strip->getVoltage());
  message += F(",\n    \"Lamp Max Current\": ");
  message += String(strip->getMilliamps());
  message += F(",\n    \"Lamp Max Power (mW)\": ");
  message += String(strip->getVoltage() * strip->getMilliamps());
  message += F(",\n    \"Lamp current Power\": ");
  message += String(strip->getCurrentPower());
  message += F(",\n    \"Leds an\": ");
  message += String(num_leds_on) ;
  message += F(",\n    \"mode\": "); 
  message += String(currentEffect); 
  message += F(",\n    \"modename\": ");
  switch (currentEffect) {
    case FX_NO_FX :
      message += F("\"No FX\"");
      break;
    case FX_SUNRISE :
      message += F("\"Sunrise Effect\"");
      break;
    case FX_SUNSET :
      message += F("\"Sunset Effect\"");
      break;
    case FX_WS2812 :
      message += F("\"WS2812fx "); 
      message += String(strip->getModeName(strip->getMode()));
      break;
    default :
      message += F("\"UNKNOWN\"");
      break;
  }
  message += F("\", \n    \"wsfxmode\": "); 
  message += String(strip->getMode());
  message += F(", \n    \"beat88\": ");
  message += String(strip->getBeat88());
  message += F(", \n    \"speed\": ");
  message += String(strip->getBeat88());
  message += F(", \n    \"brightness\": ");
  message += String(strip->getBrightness());
  
  // Palettes and Colors
  message += F(", \n    \"palette count\": ");
  message += String(strip->getPalCount()); 
  message += F(", \n    \"palette\": ");
  message += String(strip->getTargetPaletteNumber()); 
  message += F(", \n    \"palette name\": \"");
  message += String(strip->getTargetPaletteName());
  message += F("\"");

  CRGB col = CRGB::Black;
  // We return either black (strip effectively off)
  // or the color of the first pixel....
  for(uint16_t i = 0; i<strip->getLength(); i++)
  {
    if(strip->leds[i])
    {
      col = strip->leds[i];
      break;
    }
  }
  message += F(", \n    \"rgb\": ");
  message += String( ((col.r << 16) | 
                      (col.g <<  8) | 
                      (col.b <<  0)) & 0xffffff );
  message += F(", \n    \"color red\": ");
  message += String(col.red);
  message += F(", \n    \"color green\": ");
  message += String(col.green);
  message += F(", \n    \"color blue\": ");
  message += String(col.blue);

  message += F(", \n    \"BlendType\": ");
  if(strip->getSegments()[0].blendType == NOBLEND)
  {
    message += "\"No Blend\"";
  }
  else if (strip->getSegments()[0].blendType == LINEARBLEND)
  {
    message += "\"Linear Blend\"";
  }
  else
  {
    message += "\"Unknown Blend\"";
  }
  
  message += F(", \n    \"Reverse\": ");
  message += getReverse();

  message += F(", \n    \"Hue change time\": ");
  message += getHueTime();
  message += F(", \n    \"Delta hue per change\": ");
  message += getDeltaHue();

  message += F(", \n    \"Autoplay Mode\": ");
  message += getAutoplay();
  message += F(", \n    \"Autoplay Mode Interval\": ");
  message += getAutoplayDuration();

  message += F(", \n    \"Autoplay Palette\": ");
  message += getAutopal();

  message += F(", \n    \"Autoplay Palette Interval\": ");
  message += getAutopalDuration();

  message += F(", \n    \"Fire Cooling\": ");
  message += getCooling();

  message += F(", \n    \"Fire sparking\": ");
  message += getSparking();

  message += F(", \n    \"Twinkle Speed\": ");
  message += getTwinkleSpeed();

  message += F(", \n    \"Twinkle Density\": ");
  message += getTwinkleDensity();

  message += F("\n  },\n  \"sunRiseState\": {\n    \"sunRiseMode\": ");


  if(sunriseParam.isSunrise) {
    message += F("\"Sunrise\"");
  } else {
    message += F("\"Sunset\"");
  }
  message += F(",\n    \"sunRiseActive\": ");
  if(sunriseParam.isRunning) {
    message += F("\"on\"");
    message += F(", \n    \"sunRiseCurrStep\": ");
    message += String(sunriseParam.step);
    message += F(", \n    \"sunRiseTotalSteps\": ");
    message += String(sunriseParam.steps);
    if(sunriseParam.isSunrise) {
      message += F(", \n    \"sunRiseTimeToFinish\": ");
      message += String(((sunriseParam.steps - sunriseParam.step) * sunriseParam.deltaTime)/1000);
    } else {
      message += F(", \n    \"sunRiseTimeToFinish\": ");
      message += String(((sunriseParam.step) * sunriseParam.deltaTime)/1000);
    }
  } else {
    message += F("\"off\", \n    \"sunRiseCurrStep\": 0, \n    \"sunRiseTotalSteps\": 0, \n    \"sunRiseTimeToFinish\": 0"); 
  }

  message += F("\n  }");

  #ifdef DEBUG
  message += F(",\n  \"ESP_Data\": {\n    \"DBG_Debug code\": \"On\",\n    \"DBG_CPU_Freq\": ");
  message += String(ESP.getCpuFreqMHz());
  message += F(",\n    \"DBG_Flash Real Size\": ");
  message += String(ESP.getFlashChipRealSize());
  message += F(",\n    \"DBG_Free RAM\": ");
  message += String(ESP.getFreeHeap());
  message += F(",\n    \"DBG_Free Sketch Space\": ");
  message += String(ESP.getFreeSketchSpace());
  message += F(",\n    \"DBG_Sketch Size\": ");
  message += String(ESP.getSketchSize());
  message += F("\n  }");
  
  message += F(",\n  \"Server_Args\": {");
  for(uint8_t i = 0; i<server.args(); i++) {
    message += F("\n    \"");
    message += server.argName(i);
    message += F("\": \"");
    message += server.arg(i);
    if( i < server.args()-1)
      message += F("\",");
    else
      message += F("\"");
  }
  message += F("\n  }");
  #endif
  message += F(",\n  \"Stats\": {\n    \"Answer_Time\": ");
  answer_time = millis() - answer_time;
  message += answer_time;
  message += F(",\n    \"FPS\": ");
  message += FastLED.getFPS();
  message += F("\n  }");
  message += F("\n}");

  #ifdef DEBUG
  Serial.println(message);
  #endif
  
  server.send(200, "application/json", message);
}


void factoryReset(void){
  #ifdef DEBUG
  Serial.println("Someone requested Factory Reset");
  #endif
  // on factory reset, each led will be red
  // increasing from led 0 to max.
  for(uint16_t i = 0; i<strip->getLength(); i++) {
    strip->leds[i] = 0xa00000;
    strip->show();
    delay(2);
  }
  strip->show();
  // formatting File system
  #ifdef DEBUG
  Serial.println("Format File System");
  #endif
  delay(INITDELAY);
  //SPIFFS.format();
  delay(INITDELAY);
  #ifdef DEBUG
  Serial.println("Reset WiFi Settings");
  #endif
  //wifiManager.resetSettings();
  delay(INITDELAY);
  clearEEPROM();
  //reset and try again
  #ifdef DEBUG
  Serial.println("Reset ESP and start all over...");
  #endif
  delay(3000);
  ESP.reset();
}

void clearEEPROM(void) {
  //Clearing EEPROM
  #ifdef DEBUG
  Serial.println("Clearing EEPROM");
  #endif
  EEPROM.begin(sizeof(myEEPROMSaveData)+10);
  for(int i = 0; i< EEPROM.length(); i++)
  {
    EEPROM.write(i,0);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Received Factoryreset request.
// To be sure we check the related parameter....
void handleResetRequest(void){
  if(server.arg("rst") == "FactoryReset") {
    server.send(200, "text/plain", "Will now Reset to factory settings. You need to connect to the WLAN AP afterwards....");
    factoryReset();
  } else if(server.arg("rst") == "Defaults") {
    uint32_t colors[3];
    colors[0] = 0xff0000;
    colors[1] = 0x00ff00;
    colors[2] = 0x0000ff;
    strip->setSegment(0, 0, strip->getLength()-1, FX_MODE_STATIC, colors, DEFAULT_BEAT88, false);
    setEffect(FX_NO_FX);
    strip->stop();
    strip_On_Off(false);
    server.send(200, "text/plain", "Strip was reset to the default values...");
    shouldSaveRuntime = true;
  }
}

void setupWebServer(void){
  //modes.reserve(5000);
  //modes_setup();
  FastLED.showColor(CRGB::Blue);
  delay(INITDELAY);
  SPIFFS.begin();
  {
   
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      #ifdef DEBUG
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), String(fileSize).c_str());
      #endif
    }
    #ifdef DEBUG
    Serial.printf("\n");
    #endif
  }

  server.on("/all", HTTP_GET, []() {
    #ifdef DEBUG
    Serial.println("Called /all!");
    #endif
    String json = getFieldsJson(fields, fieldCount);
    server.send(200, "text/json", json);
  });

  server.on("/fieldValue", HTTP_GET, []() {
    String name = server.arg("name");
    #ifdef DEBUG
    Serial.println("Called /fieldValue with arg name =");
    Serial.println(name);
    #endif
   
    String value = getFieldValue(name, fields, fieldCount);
    server.send(200, "text/json", value);
  });

  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });

  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  

  //server.on("/main.js", srv_handle_main_js);
  server.on("/modes", srv_handle_modes);
  server.on("/pals", srv_handle_pals);
  //server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/getmodes", handleGetModes);
  server.on("/getpals", handleGetPals);
  server.on("/status", handleStatus);
  server.on("/reset", handleResetRequest);
  server.onNotFound(handleNotFound);
  
  server.serveStatic("/", SPIFFS, "/", "max-age=86400");
  delay(10);
  server.begin();

  FastLED.showColor(CRGB::Yellow);
  delay(INITDELAY);
  #ifdef DEBUG
  Serial.println("HTTP server started.\n");
  #endif

  webSocketsServer.begin();
  webSocketsServer.onEvent(webSocketEvent);

  FastLED.showColor(CRGB::Green);
  delay(INITDELAY);
  #ifdef DEBUG
  Serial.println("webSocketServer started.\n");
  #endif
  FastLED.showColor(CRGB::Black);
  delay(INITDELAY);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      #ifdef DEBUG
      Serial.printf("[%u] Disconnected!\n", num);
      #endif
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocketsServer.remoteIP(num);
        #ifdef DEBUG
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        #endif

        // send message to client
        // webSocketsServer.sendTXT(num, "Connected");
      }
      break;

    case WStype_TEXT:
      #ifdef DEBUG
      Serial.printf("[%u] get Text: %s\n", num, payload);
      #endif

      // send message to client
      // webSocketsServer.sendTXT(num, "message here");

      // send data to all connected clients
      // webSocketsServer.broadcastTXT("message here");
      break;

    case WStype_BIN:
      #ifdef DEBUG
      Serial.printf("[%u] get binary length: %u\n", num, length);
      #endif
      hexdump(payload, length);

      // send message to client
      // webSocketsServer.sendBIN(num, payload, lenght);
      break;
  }
}

// setup network and output pins
void setup() {
  // Sanity delay to get everything settled....
  delay(500);
  #ifdef DEBUG
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println(F("Booting"));
  #endif
  
  readConfigurationFS();

  stripe_setup( LED_COUNT, 
                STRIP_FPS, 
                STRIP_VOLTAGE, 
                STRIP_MILLIAMPS, 
                RainbowColors_p, 
                F("Rainbow Colors"), 
                TypicalLEDStrip);

  setupWiFi();

  setupWebServer();

  updateConfiguration();

  initOverTheAirUpdate();

  // if we got that far, we show by a nice little animation
  // as setup finished signal....
  #ifdef DEBUG
  Serial.println("Init done - fading green in..");
  Serial.print("\tcurrent Effect = ");
  Serial.println(currentEffect);
  #endif
  for(uint8_t a = 0; a < 1; a++) {
    for(uint16_t c = 0; c<256; c+=3) {
      for(uint16_t i = 0; i<strip->getLength(); i++) {
        strip->leds[i].green = c;
      }
      strip->show();
      delay(1);
    }
    #ifdef DEBUG
  Serial.println("Init done - fading green out..");
  Serial.print("\tcurrent Effect = ");
  Serial.println(currentEffect);
  #endif
    delay(2);
    for(uint8_t c = 255; c>0; c-=3) {
      for(uint16_t i = 0; i<strip->getLength(); i++) {
        strip->leds[i].subtractFromRGB(4);
      }
      strip->show();
      delay(1);
    }
  }
  //strip->stop();
  delay(INITDELAY);
  #ifdef DEBUG
  Serial.println("Init finished.. Read runtime data");
  #endif
  readRuntimeDataEEPROM();
  #ifdef DEBUG
  Serial.println("Runtime Data loaded");
  FastLED.countFPS(60);
  #endif

  set_max_power_indicator_LED(BUILTIN_LED);
  //if(stripIsOn) strip_On_Off(true);
}

// request receive loop
void loop() {
  unsigned long now = millis();
  #ifdef DEBUG
  static uint8_t life_sign = 0;
  static unsigned long last_status_msg = 0;
  #endif
  if (OTAisRunning) return;
  // if someone requests a call to the factory reset...
  //static bool ResetRequested = false;

  #ifdef DEBUG
    // Debug Watchdog. to be removed for "production".
  if(now - last_status_msg > 10000) {
    last_status_msg = now;
    Serial.print("\n\t");
     switch (currentEffect) {
      case FX_NO_FX :
        Serial.println("No FX");
        break;
      case FX_SUNRISE :
        Serial.println("Sunrise");
        break;
      case FX_SUNSET :
        Serial.println("Sunset");
        break;
      case FX_WS2812 :
        Serial.print("WS2812FX");
        Serial.print("\t");
        Serial.print(strip->getMode());
        Serial.print("\t");
        Serial.println(strip->getModeName(strip->getMode()));
        break;
      default:
        Serial.println("This is a problem!!!");
    }
    Serial.print("\tC:\t");
    Serial.print(strip->getCurrentPaletteName());
    Serial.print("\tT:\t");
    Serial.println(strip->getTargetPaletteName());
    Serial.print("\tFPS:\t");
    Serial.println(FastLED.getFPS());
  }
  #endif
  // Checking WiFi state every WIFI_TIMEOUT
  // Reset on disconnection
  if(now - last_wifi_check_time > WIFI_TIMEOUT) {
    //Serial.print("\nChecking WiFi... ");
    if(WiFi.status() != WL_CONNECTED) {
      #ifdef DEBUG
      Serial.println("WiFi connection lost. Reconnecting...");
      Serial.println("Lost Wifi Connection....");
      #endif
      // Show the WiFi loss with yellow LEDs.
      // Whole strip lid finally.
      for(uint16_t i = 0; i<strip->getLength(); i++)
      {
        strip->leds[i] = 0xa0a000;
        strip->show();
      }
      // Reset after 6 seconds....
      delay(3000);
      #ifdef DEBUG
      Serial.println("Resetting ESP....");
      #endif
      delay(3000);
      ESP.reset();
    }
    last_wifi_check_time = now;
  }

  ArduinoOTA.handle(); // check and handle OTA updates of the code....

  
  webSocketsServer.loop();
  
  server.handleClient();
  
  effectHandler();

  if(shouldSaveRuntime) {
    saveEEPROMData();
    shouldSaveRuntime = false;
  }

}
