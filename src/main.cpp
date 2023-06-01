/** @file module.c
 * 
 * @brief A description of the module’s purpose. 
 *
 * @par       
 * COPYRIGHT NOTICE: (c) 2018 Barr Group. All rights reserved.

 * Pin	Function	ESP-8266 Pin
 * TX	TXD	TXD
 * RX	RXD	RXD
 * A0	Analog input, max 3.2V	A0
 * D0	IO	GPIO16
 * D1	IO, SCL	GPIO5
 * D2	IO, SDA	GPIO4
 * D3	IO, 10k Pull-up	GPIO0
 * D4	IO, 10k Pull-up, BUILTIN_LED	GPIO2
 * D5	IO, SCK	GPIO14
 * D6	IO, MISO	GPIO12
 * D7	IO, MOSI	GPIO13
 * D8	IO, 10k Pull-down, SS	GPIO15
 * G	  Ground	GND
 * 5V	5V	-
 * 3V3	3.3V	3.3V
 * RST	Reset	RST
*/

#include <Arduino.h>
#include "DHTesp.h"
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>   // Universal Telegram Bot Library written by Brian Lough: https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <ArduinoJson.h>
#include "time.h"
#include "mylibrary.h"
#include <EasyButton.h>

#undef OLED
#ifdef OLED
#include <Wire.h>
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "images.h"
#endif 

// Use RESET_WIFI_AS_ACCESSPOINT Button to restart WaterFlow as Access Point
#define RESET_WIFI_AS_ACCESSPOINT D3
#define FLASH_BUTTON_TIMER_MSEC 2000
EasyButton button(RESET_WIFI_AS_ACCESSPOINT);

WiFiManager wm;

// Default Trigger used to close the valve
#define DEFAULT_WATER_SENSOR_PAY_ATTENTION 800
#define MILLISECONDS                       1000
#define DEFAULT_WATER_SENSOR_DELAY         MILLISECONDS*30 // 60[Sec]

// Valve Status
typedef enum _ValveStatus
{
  OPEN  = 0U,
  CLOSE = 1U 
} ValveStatus;
static ValveStatus valveStatus;
int lastStatus;
static bool bClosePumpForever;

// Builtin LED 
//#define BUILTIN_LED D4  // IO, 10k Pull-up
bool ReleState = LOW;

// Used to signal that the System has restarted and 
#define WIFI_UNCONNECTED D7  // IO, MOSI	GPIO13 (WiFi As Access Point)
#define WIFI_CONNECTED   D6  // IO, MISO	GPIO12 (WiFi Connected)
#define RELAY_VALVE      D5  // IO, SCK GPIO14
#define WATER_SENSOR_PIN A0  // Analog Input Water Flow Sensor

// Initialize Telegram BOT
#define BOTtoken "6278941177:AAGzrzmT7cSrl21bAtq0RjkCdnrcCSui6WY"  // your Bot Token (Get from Botfather)

// Use @myidbot to find out the chat ID of an individual or a group
// Also note that you need to click "start" on a bot before it can
// message you
#define CHAT_ID "6232372096"
#define BOT_MESSAGE_DELAY 1000

#ifdef ESP8266
  X509List cert(TELEGRAM_CERTIFICATE_ROOT);
#endif

#ifdef OLED
SSD1306Wire display(0x3c, SDA, SCL);
OLEDDisplayUi ui     ( &display );
#endif 

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

static String dd    = "0";
static String mm    = "0";
static String yyyy  = "0";
static String hh    = "0";
static String mnts  = "0";
static String ss    = "0";
static String dweek = "0";

// Checks for new messages every 1 second.
static   int  botRequestDelay = BOT_MESSAGE_DELAY;
unsigned long lastTimeBotRan;

// NTP Protocol 
       const char *ntpServer = "pool.ntp.org";
       const long  gmtOffset_sec = 0;
       const int   daylightOffset_sec = 3600;
static       int ntpStatus = 0;

#if 0
// lowest and highest sensor readings:
const int sensorMin = 0;     // sensor minimum
const int sensorMax = 1024;  // sensor maximum
#else
static int uwTriggerBlockPump    = DEFAULT_WATER_SENSOR_PAY_ATTENTION;
static int iReadFromWaterSensor  = DEFAULT_WATER_SENSOR_DELAY;
unsigned long lastTimeWaterSensorRun;
#endif

/*!
 * @brief Sends a 10ms impulse to open the valve.
 *
 * @return void
 */
void open_valve(void) 
{
  valveStatus = OPEN;
  digitalWrite(RELAY_VALVE, LOW);
  delay(10); 
  Serial.println("WateFlow> Valve Opened. The Water Is Flowing\n");
}

/*!
 * @brief Identify the larger of two 8-bit integers.
 *
 * @param[in] num1  The first number to be compared.
 * @param[in] num2  The second number to be compared.
 *
 * @return The value of the larger number.
 */
void close_valve() 
{
  valveStatus = CLOSE;
  // Sends a 10ms impulse to open the valve.
  digitalWrite(RELAY_VALVE, HIGH);
  delay(10);
  Serial.println("WateFlow> Valve closed. Water not Flowing\n");
}

/*!
 * @brief Turn the WiFi as Access Point.
 *
 * @return The value of the larger number.
 */
void backToWiFiAsAccessPoint(void)
{
   Serial.println("WateFlow> WateFlow> WIFI Reset as Access Point");  
  wm.resetSettings();
  ESP.eraseConfig(); 
  delay(2000);
  ESP.reset(); 
  ESP.restart();
}

/*!
 * @brief Callback function to be called after FLASH_BUTTON_TIMER_MSEC [mSec].
 *
 * @return void.
 */
void onPressedForDuration(void)
{
  backToWiFiAsAccessPoint();
}

/*!
 * @brief  Handle what happens when you receive new messages.
 *
 * @param[in] numNewMessages  The number of Received Messages.
 *
 * @return void.
 */
void handleNewMessages(int numNewMessages) 
{
  String strCmd;
  Serial.print("WateFlow> handleNewMessages;"); Serial.print(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "WateFlow> Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/help") {
      String welcome = "WateFlow> Welcome, " + from_name + ".\n";
      welcome += "WateFlow> Use the following commands to control your outputs.\n\n";      
      welcome += "          /GetPumpTrigger          Get The Pump Trigger\n";      
      welcome += "          /SetPumpTrigger          Set The Pump Trigger [500..1024]\n";
      welcome += "          /OpenValv                Open the flow of water\n";      
      welcome += "          /CloseValv               Closes the flow of water\n";   
      welcome += "          /GetWaterSensorPollTimer Get The Polling Of The Water Flow Sensor [mSec]\n";                  
      welcome += "          /WaterSensorPollTimer    Set The Polling To Read The Water Flow Sensor [1 to 60 mSec]\n";            
      welcome += "          /state                   Get The Pump Status\n";
      welcome += "          /date                    Get The Current Date And Hour\n";
      bot.sendMessage(chat_id, welcome, "");
    }
    else if (text == "/GetPumpTrigger") 
    {
        bot.sendMessage(chat_id, "WateFlow> Get The Pump Trigger : ", "");  
        bot.sendMessage(chat_id, String(uwTriggerBlockPump), "");        
    }
    else if (splitString(text, ':', 0) == "/SetPumpTrigger") //(text =="/SetPumpTrigger")
    { 
      strCmd = splitString(text, ':', 1);
      //Serial.print("Received Trigger Value: ");
      Serial.println(strCmd.toInt());
      //Serial.println(strCmd);
      if ( (strCmd.toInt() < 1024) &&
           (strCmd.toInt() > 500) )
      {
        uwTriggerBlockPump = strCmd.toInt();
        bot.sendMessage(chat_id, "WateFlow> Pump Trigger Set To: ", "");  
        bot.sendMessage(chat_id, strCmd, "");        
      }
      else
      {
        bot.sendMessage(chat_id, "WateFlow> Pump Out Of Range ", ""); 
      }
      
    }
    else if (text == "/OpenValv") 
    {    
      if (bClosePumpForever == false)  
      {
        bot.sendMessage(chat_id, "WateFlow> Pump set to OPEN", "");      
        Serial.println(valveStatus);
        open_valve();   
      }
      else
      {
        bot.sendMessage(chat_id, "WateFlow> Pump Error It Shall Be Remain CLOSED", ""); 
      }
    }     
    else if (text == "/CloseValv") 
    {
      bot.sendMessage(chat_id, "WateFlow> Pump set to CLOSED", "");
      Serial.println(valveStatus);
      close_valve();
    }
    else if (text == "/state") 
    {
      if(valveStatus == OPEN)
      {
        bot.sendMessage(chat_id, "WateFlow> Pump Status: OPEN", "");
      }
      else
      {
        bot.sendMessage(chat_id, "WateFlow> Pump Status: CLOSE", "");
      }     
    }   
    else if (splitString(text, ':', 0) == "/WaterSensorPollTimer")     
    {    
      strCmd = splitString(text, ':', 1);
     // Serial.print("Received Trigger Value: ");
      Serial.println(strCmd.toInt());
      if ( (strCmd.toInt() <= 60) &&
           (strCmd.toInt() >= 1) )
      {
        iReadFromWaterSensor = strCmd.toInt() * MILLISECONDS;;        
        bot.sendMessage(chat_id, "WateFlow> Water Sensor Polling to [Sec] : ", "");  
        bot.sendMessage(chat_id, String(strCmd.toInt()), "");        
      }
      else
      {
        bot.sendMessage(chat_id, "WateFlow> Water Sensor Polling Out Of Range ", ""); 
      }
    }
    else if (text == "/GetWaterSensorPollTimer") 
    {
      bot.sendMessage(chat_id, "WateFlow> Get The Water Sensor Polling Timer [Sec] : ", "");  
      bot.sendMessage(chat_id, String(iReadFromWaterSensor/MILLISECONDS), "");     
    }
    else if (text == "/date") 
    {
      char dateHour[29];
      snprintf_P(dateHour, sizeof(dateHour), PSTR("%4s %02s/%02s/%4s   %02s:%02s:%02s"), dweek, dd, mm, yyyy, hh, mnts, ss);
      if(ntpStatus == 1) 
      {
        bot.sendMessage(chat_id, "Today is: " + (String) dateHour, "");
      } 
      else 
      {
        bot.sendMessage(chat_id, "Cannot connect to NTP time server", "");
      }         
    }
    else
    {
      bot.sendMessage(chat_id, "WateFlow> Unknown Command", "");
    }
  }
}



bool getMyLocalTime(struct tm * info, uint32_t ms)
{
    uint32_t start = millis();
    time_t now;
    while((millis()-start) <= ms) {
        time(&now);
        localtime_r(&now, info);
        if(info->tm_year > (2016 - 1900)){
            return true;
        }
        delay(10);
    }
    return false;
}

/*!
 * @brief  Update Local Time.
 *
 * @return void.
 */
void updateLocalTime(void)
{
  struct tm timeinfo;
  if(!getMyLocalTime(&timeinfo,30000))
  {
    Serial.println("WateFlow> Failed to obtain time");
    ntpStatus = 0;
    return;
  }

  ntpStatus = 1;

  dd = String(timeinfo.tm_mday);
  mm = String(timeinfo.tm_mon + 1);
  yyyy = String(timeinfo.tm_year + 1900);
  hh = String(timeinfo.tm_hour);
  if(hh.length() == 1) {
    hh = "0" + hh;
  }
  mnts = String(timeinfo.tm_min);
  if(mnts.length() == 1) {
    mnts = "0" + mnts;
  }
  ss = String(timeinfo.tm_sec);
  if(ss.length() == 1) {
    ss = "0" + ss;
  }

  switch(timeinfo.tm_wday){

    case 0:
        dweek = "Sun";
        break;

    case 1:
        dweek = "Mon";
        break;

    case 2:
        dweek = "Tue";
        break;

    case 3:
        dweek = "Wed";
        break;

    case 4:
        dweek = "Thu";
        break;

    case 5:
        dweek = "Fri";
        break;
    
    case 6:
        dweek = "Sat";
        break;

    default:
        dweek = "";
        break;

  }

} 

#ifdef OLED 
void InitDisplay() 
{
 
  display.init();
  display.flipScreenVertically();
 // display.setColor(WHITE);
 // display.setContrast(100, 241, 64);

}

void ClearDisplay() 
{
 
    display.clear();    
    display.display();
 
}


void BootDisplay(int16_t x, 
                 int16_t y, 
                 const String &text,
                 OLEDDISPLAY_TEXT_ALIGNMENT textAlignment,
                 const uint8_t *fontData ) 
{
 
    display.clear();
    display.setTextAlignment(textAlignment);
    display.setFont(fontData);
    display.drawString(x, y, text);
    display.display();
    
}


void drawWiFiIcon() 
{

    // see http://blog.squix.org/2015/05/esp8266-nodemcu-how-to-create-xbm.html
    // on how to create xbm files
    display.drawXbm(34, 14, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
    display.display();

}
#endif    

/*!
 * @brief  Setup For ESP8266.
 *
 * @return void.
 */

void setup() 
{
  bool res;
  Serial.begin(9600);
  WiFi.mode(WIFI_AP_STA); // explicitly set mode, esp defaults to STA+AP
  button.begin();   // Initialize the button.
 
 // pinMode(BUILTIN_LED, OUTPUT);
  pinMode(WIFI_UNCONNECTED, OUTPUT);
  pinMode(WIFI_CONNECTED, OUTPUT);
  pinMode(RELAY_VALVE, OUTPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);

  // Set the WIFI_UNCONNECTED RED: The system is in Access Point Mode (wifi connection to router is again in fault mode)
  digitalWrite(WIFI_UNCONNECTED, HIGH);
  digitalWrite(WIFI_CONNECTED, LOW);
 
  // At Startup the valve is closed
  close_valve();
  bClosePumpForever = false;

#ifdef OLED 
  InitDisplay();
  BootDisplay(0,20,"Water Flow Boot....",TEXT_ALIGN_LEFT,ArialMT_Plain_16);

  delay(5000);
  ClearDisplay();
#endif
  //blocking loop awaiting configuration and will return success result
  res = wm.autoConnect("WaterFlow","1234567890");  
  if(!res) 
  {
    Serial.println("WateFlow> Failed to connect");
    digitalWrite(WIFI_CONNECTED, LOW);
    digitalWrite(WIFI_UNCONNECTED, HIGH);
    // ESP.restart();
  } 
  else 
  {
#ifdef OLED     
    drawWiFiIcon();   
    delay(5000); 
    ClearDisplay();
#endif
    //if you get here you have connected to the WiFi    
    Serial.println("WateFlow> Connected");
    digitalWrite(WIFI_UNCONNECTED, LOW);
    digitalWrite(WIFI_CONNECTED, HIGH);
    bot.sendMessage(CHAT_ID, "Hi! I'm online!", "");
  }

   // Add the callback function to be called when the button is pressed for at least the given time: FLASH_BUTTON_TIMER_MSEC [mSec]
  button.onPressedFor(FLASH_BUTTON_TIMER_MSEC, onPressedForDuration);

 #ifdef ESP8266
    configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
    client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);      // get UTC time via NTP
    client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
  #endif
      
}

/*!
 * @brief  loop function for ESP8266.
 *
 * @return void.
 */
void loop() 
{
  unsigned long iActualTime = millis();

  // Continuously read the status of the button.
  button.read();

  if(WiFi.status()  != WL_CONNECTED)
  {
    Serial.println("WateFlow> Failed to connect");    
    digitalWrite(WIFI_UNCONNECTED, LOW/*HIGH*/);
    digitalWrite(WIFI_CONNECTED, HIGH/*LOW*/);
    close_valve();    
    delay(2000);    
    ESP.restart();
  }  

  // Telegram Message check  
  if (iActualTime > (lastTimeBotRan + botRequestDelay))
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages) 
    {
      Serial.println("WateFlow> got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  // WATER SENSOR Check
  if (iActualTime >  (lastTimeWaterSensorRun + iReadFromWaterSensor))
  {
    int sensorReading = analogRead(A0);
    Serial.print("WateFlow> A0 = ");
    Serial.printf("%d",sensorReading);
    Serial.println();
    if (sensorReading < uwTriggerBlockPump)
    {
      close_valve();
      bClosePumpForever = true;
      Serial.println("WateFlow> CAUTION!!!! VALVE FOREVER CLOSED");     
    }
   // else
    //{
      if (bClosePumpForever == false)
      {
        open_valve();
        //Serial.println("WateFlow> Open Valve"); 
      }
   // }
    lastTimeWaterSensorRun = millis();
  }  
  // Temp/Hum Sensor Check

}