//Project Notes:https://github.com/JJSlabbert/iTilt-digital-hydrometer
//Support Telegram messages via your own bot
//Support BLE for upload data to BrewPiLess software https://github.com/vitotai/BrewPiLess ESP32 only

float firmwareversion=1.09;

//WiFiManager global declerations
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#ifdef ESP32
  #include <SPIFFS.h>
  #include <HTTPClient.h>
  #include "BLEDevice.h"
  #include "BLEUtils.h"
  #include "BLEBeacon.h"
  #define BEACON_UUID             "DE742DF0-7013-12B5-444B-B1C510BB95A4" // UUID Tilt red  
  
#else
 #include <ESP8266HTTPClient.h>
#endif

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//define your default values here, if there are different values in config.json, they are overwritten.
char portalTimeOut[5] = "9000";
char mqtt_username[47]; //Cayenne details
char mqtt_password[41];
char mqtt_clientid[37];
char mqtt_service[20]="TELEGRAM";//*******************************************************************************
char coefficientx3[16] = "0.0000010000000"; //model to convert tilt to gravity
char coefficientx2[16] = "-0.000131373000";
char coefficientx1[16] = "0.0069679520000";
char constantterm[16] = "0.8923835598800";
#ifdef ESP8266
  char batconvfact[8] = "196.00"; //converting reading to battery voltage 196.25
#endif
#ifdef ESP32
  char batconvfact[8]="872.48";
#endif
char pubint[6] = "0"; // publication interval in seconds
char originalgravity[6] = "1.05";
char tiltOffset[5] = "0";
char itiltnum[4]="0";
char dummy[3];
RTC_DATA_ATTR static int bootcount;

WiFiManager wm;

//flag for saving data (Custom params for WiFiManager
bool shouldSaveConfig = false;


//MQTT global declerations
#include <PubSubClient.h>
WiFiClient wifiClient;  //MQTT things
//const char broker[] = "mqtt.mydevices.com";
//int        port     = 1883;


//DS18B20 global decleration
#include <OneWire.h>  //Instal from Arduino IDE
#include <DallasTemperature.h> //Instal from Arduino IDE
#ifdef ESP8266
char onewire_pin[3]="12";
#endif
#ifdef ESP32
char onewire_pin[3]="16";
#endif


//MPU6050 global decleration 
//https://www.i2cdevlib.com/docs/html/class_m_p_u6050.html#a196404ef04b959083d4bf5e6f1cd8b98
#include <MPU6050.h> //Instal from Arduino IDE
#include <Wire.h>
#ifdef ESP8266
  char i2c_sda_pin[3]="21";
  char i2c_scl_pin[3]="22";
#endif
#ifdef ESP32
  char i2c_sda_pin[3]="27";
  char i2c_scl_pin[3]="26";
#endif
int i2c_address=0x68;
MPU6050 accelgyro(i2c_address);
int16_t ax, ay, az;

//ESP32 POWER PIN
#ifdef ESP32
  char power_pin[3]= "33";
  char power_pin2[3]= "25";
  //#include "esp_sleep.h"  //https://github.com/espressif/arduino-esp32/issues/2712 To keep GPIO power pin LOW during Deep Sleep
  //#include "driver/gpio.h"
  //#include "esp_err.h"
#endif
#ifdef ESP32
  char mpu_orientation[2]="1"; // If Vcc is on Top when iTilt is in wort, this should be 1, else 0. The value can be overwritten in 192.168.4.1/pinconfinput?
#endif

#ifdef ESP32
  char batvolt_pin[3]="36";
#endif
//GLOBAL HTML STRINGS
String htmlMenueText="<a href='/wifi?' class='button'>CONFIGURE WIFI, CAYENNE AND CALIBRATION</a>\
<br><a href='/readings?' class='button'>SENSOR READINGS</a>\
<br><a href='/offsetcalibration?' class='button'>OFFSET CALIBRATION</a>\    
<br><a href='/polynomialcalibrationstart?' class='button'>POLYNOMIAL CALIBRATION</a>\
<br><a href='/info?' class='button'>INFO</a>\ 
<br><a href='/exit?' class='button'>EXIT</a>\  
<br><a href='/update?' class='button'>FIRMWARE UPDATE</a>\
<br><a href='/pinconfinput?' class='button'>PIN AND SENSOR CONFIGURATIONS</a>";  

String htmlStyleText= "<style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;  max-width:900px; align-content:center; margin: auto; font-size: 30px; } p {font-size: 30px;} h1 {text-align: center;}\
.button {  background-color: blue;  border: none;  color: white;  padding: 30px 15px;  text-align: center;  text-decoration: none;  display: inline-block;  font-size: 30px;  margin: 4px 2px;  cursor: pointer; width: 900px; border-radius: 20px;} </style>";

String htmlWiFiConfStyleText= "<style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088;  max-width:450px; align-content:center; margin: auto; } p {font-size: 30px;} h1 {text-align: center;}\
.button {  background-color: blue;  border: none;  color: white;  padding: 30px 15px;  text-align: center;  text-decoration: none;  display: inline-block;  font-size: 30px;  margin: 4px 2px;  cursor: pointer; width: 450px; border-radius: 20px;} </style>";

//Other global declerations
#include <curveFitting.h>  //used for polynomial calibration v1.06 from Arduino IDE


void pubToAdafruit(String batcap,String tempgyro,String temp,String tilt,String batvolt,String grav,String abv, String  signalstrength, String mqtt_username, String mqtt_password, String mqtt_client_id)
{
   PubSubClient client("io.adafruit.com", 1883,wifiClient);
   if (client.connect(mqtt_client_id.c_str(), mqtt_username.c_str(), mqtt_password.c_str())) 
   {
    Serial.println("Connected to Adafruit IO");
   }
   else
   {
    Serial.println("Failed to connect to Adafruit IO");
 }

  String topic_batcap=mqtt_username+"/feeds/battery-capacity"; 
  String topic_tempgyro=mqtt_username+"/feeds/gyro-temperature";     
  String topic_temp=mqtt_username+"/feeds/beer-temperature";
  String topic_tilt=mqtt_username+"/feeds/tilt";
  String topic_batvolt=mqtt_username+"/feeds/battery-voltage";
  String topic_grav=mqtt_username+"/feeds/gravity";
  String topic_abv=mqtt_username+"/feeds/alcohol-by-volume";
  String topic_signalstrength=mqtt_username+"/feeds/wifi-signalstrength";
  String topic_pubint=mqtt_username+"/feeds/publication-interval";
  String topic_originalgravity=mqtt_username+"/feeds/original-gravity";
  String topic_firmwareversion=mqtt_username+"/feeds/firmware-version";

  client.publish(topic_batcap.c_str(),batcap.c_str());
  client.publish(topic_tempgyro.c_str(),tempgyro.c_str()); 
  client.publish(topic_temp.c_str(),temp.c_str());
  client.publish(topic_tilt.c_str(),tilt.c_str());
  client.publish(topic_batvolt.c_str(),batvolt.c_str());
  client.publish(topic_grav.c_str(),grav.c_str());
  client.publish(topic_abv.c_str(),abv.c_str());
  client.publish(topic_signalstrength.c_str(),signalstrength.c_str());

  client.publish(topic_pubint.c_str(),pubint);
  client.publish(topic_originalgravity.c_str(),originalgravity);
  client.publish(topic_firmwareversion.c_str(),String(firmwareversion).c_str());
  delay(500);

}

void pubToCayenne(String batcap,String tempgyro,String temp,String tilt,String batvolt,String grav,String abv, String  signalstrength, String mqtt_username, String mqtt_password, String mqtt_client_id)
{
 PubSubClient client("mqtt.mydevices.com", 1883,wifiClient);

 if (client.connect(mqtt_client_id.c_str(), mqtt_username.c_str(), mqtt_password.c_str())) 
 {
  Serial.println("Connected to Cayenne");
 }
 else
 {
  Serial.println("Failed to connect to Cayenne");
 }
 //delay(1000);
 String topic="v1/" +mqtt_username +"/things/" +mqtt_client_id+"/data/json";        
 String payload1; //Cayenne payload is limited to 5 channels / varabls in the json string
 String payload2;
 String payload3; 
 payload1="[";
 
 payload1+="{\"channel\": 0,\"value\": "+String(001)+"}";  //iTilt id number
 payload1+=",{\"channel\": 1,\"value\": "+batcap+"}";
 payload1+=",{\"channel\": 2,\"value\": "+tempgyro+"}";
 payload1+=",{\"channel\": 3,\"value\": "+String(firmwareversion)+"}";  //Firmware Version
 payload1+=",{\"channel\": 4,\"value\": "+temp+"}";
 payload1+="]";


 payload2="[";
 payload2+="{\"channel\": 5,\"value\": "+tilt+"}";
 payload2+=",{\"channel\": 6,\"value\": "+batvolt+"}";
 payload2+=",{\"channel\": 7,\"value\": "+grav+"}";
 payload2+=",{\"channel\": 8,\"value\": "+String(pubint)+"}"; //pub interval
 payload2+=",{\"channel\": 9,\"value\": "+String(originalgravity)+"}"; //original gravity
 payload2+="]";


 payload3="["; 
 payload3+="{\"channel\": 10,\"value\": "+abv+"}";
 payload3+=",{\"channel\": 11,\"value\": "+signalstrength+"}"; 
 payload3+="]";

 Serial.println("Topic: "+topic);
 Serial.println("Payload1: "+payload1);
 Serial.println("Payload2: "+payload2);
 Serial.println("Payload3: "+payload3);  
 client.publish(topic.c_str(),payload1.c_str());
 client.publish(topic.c_str(),payload2.c_str());
 client.publish(topic.c_str(),payload3.c_str());
 delay(700);
}


void pubToUbidots(String batcap,String tempgyro,String temp,String tilt,String batvolt,String grav,String abv, String  signalstrength, String mqtt_username, String mqtt_password, String mqtt_device_name)
{
 PubSubClient client("industrial.api.ubidots.com", 1883,wifiClient);

 if (client.connect(mqtt_device_name.c_str(), mqtt_username.c_str(), mqtt_password.c_str())) 
 {
  Serial.println("Connected to Ubidots");
 }
 else
 {
  Serial.println("Failed to connect to Ubidots");
 }
 
 String topic="/v1.6/devices/"+mqtt_device_name;
 String payload;
 payload="{";
 
 payload+="\"Battery Capacity\":";
 payload+=batcap;

 payload+=",\"Temperature in Gyro\":";
 payload+=tempgyro;
 
 payload+=",\"Temperature from DS18B20\":";
 payload+=temp;

 payload+=",\"Tilt\":";
 payload+=tilt;

 payload+=",\"Battery Voltage\":";
 payload+=batvolt;

 payload+=",\"Gravity\":";
 payload+=grav;

 payload+=",\"ABV\":";
 payload+=abv;

 payload+=",\"WiFi Signalstrength\":";
 payload+=signalstrength;

// payload+=",\"Publication Interval\":";
// payload+=String(pubint);

// payload+=",\"Original Gravity\":";
// payload+=String(originalgravity);

// payload+=",\"Firmware Version\":";
// payload+=String(firmwareversion);

 payload+="}";

 Serial.println("Topic: "+topic);
 Serial.println("Payload: "+payload);

 client.publish(topic.c_str(), payload.c_str());
 delay(500);
}

void pubToTelegram(String batcap,String tempgyro,String temp,String tilt,String batvolt,String grav,String abv, String  signalstrength, String token, String ChatID) {

#ifdef ESP32
    WiFiClientSecure *client = new WiFiClientSecure;
#else
std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
#endif

client->setInsecure();
  HTTPClient https;
  
  String payload;
 payload+="Battery Capacity:";
 payload+=batcap;

 payload+="\nTemperature in Gyro:";
 payload+=tempgyro;
 
 payload+="\nTemperature from DS18B20:";
 payload+=temp;

 payload+="\nTilt:";
 payload+=tilt;

 payload+="\nBattery Voltage:";
 payload+=batvolt;

 payload+="\nGravity:";
 payload+=grav;

 payload+="\nABV::";
 payload+=abv;

 payload+="\nWiFi Signalstrength:";
 payload+=signalstrength;

  
  if (https.begin(*client, "https://api.telegram.org/bot"+token+"/")) { 
    https.addHeader("Content-Type", "application/json");
    https.POST("{\"method\":\"sendMessage\",\"chat_id\":" + ChatID + ",\"text\":\"" + payload + "\"}");
    https.end();
  }

//Serial.println("https://api.telegram.org/bot"+token+"/");
//Serial.println("{\"method\":\"sendMessage\",\"chat_id\":" + ChatID + ",\"text\":\"" + payload + "\"}");
  
}


void inviniteSleep()  //(Hybernation Mode) This is used when battery is charged or iTilt is stored
{ 
  Serial.println("Roll is <20 or >160. Device will enter Invinite deep sleep.");
  #ifdef ESP32
  digitalWrite(atoi(power_pin), LOW);
  digitalWrite(atoi(power_pin2), LOW);
  digitalWrite(LED_BUILTIN,HIGH);

  //https://github.com/espressif/arduino-esp32/issues/2712 To keep GPIO power pin LOW during Deep Sleep 
  gpio_hold_en(GPIO_NUM_2); 
  gpio_hold_en(GPIO_NUM_33);
  gpio_hold_en(GPIO_NUM_0);
  //GPIO 1 and 3 ist TXD and RXD
  gpio_hold_en(GPIO_NUM_4);
  gpio_hold_en(GPIO_NUM_5);
  gpio_hold_en(GPIO_NUM_12);
  gpio_hold_en(GPIO_NUM_13);
  gpio_hold_en (GPIO_NUM_14);
  gpio_hold_en (GPIO_NUM_15);
  gpio_hold_en (GPIO_NUM_16); 
  gpio_hold_en (GPIO_NUM_17);
  gpio_hold_en (GPIO_NUM_18);
  gpio_hold_en (GPIO_NUM_19);
  gpio_hold_en (GPIO_NUM_21);
 // gpio_hold_en (GPIO_NUM_22);
  gpio_hold_en (GPIO_NUM_23);
  gpio_hold_en (GPIO_NUM_25);
  gpio_hold_en (GPIO_NUM_26);
  gpio_hold_en (GPIO_NUM_27);
  gpio_hold_en (GPIO_NUM_32);
  gpio_hold_en (GPIO_NUM_33);
  gpio_deep_sleep_hold_en();      
  #endif

  #ifdef ESP8266
  ESP.deepSleep(999999999999999999);
  delay(1000);
  #endif
  #ifdef ESP32
  esp_sleep_enable_timer_wakeup(999999999999999999);  //How long can esp32 hibernate
  esp_deep_sleep_start();
  delay(1000);
  #endif
  
}


void startDeepSleep()
{
  #ifdef ESP32
  digitalWrite(atoi(power_pin), LOW);
  digitalWrite(atoi(power_pin2), LOW);
  digitalWrite(LED_BUILTIN,HIGH);

  //https://github.com/espressif/arduino-esp32/issues/2712 To keep GPIO power pin LOW during Deep Sleep 
  gpio_hold_en(GPIO_NUM_2); 
  gpio_hold_en(GPIO_NUM_33);
  gpio_hold_en(GPIO_NUM_0);
  //GPIO 1 and 3 ist TXD and RXD

  gpio_hold_en(GPIO_NUM_4);
  gpio_hold_en(GPIO_NUM_5);
  gpio_hold_en(GPIO_NUM_12);
  gpio_hold_en(GPIO_NUM_13);
  gpio_hold_en (GPIO_NUM_14);
  gpio_hold_en (GPIO_NUM_15);
  gpio_hold_en (GPIO_NUM_16); 
  gpio_hold_en (GPIO_NUM_17);
  gpio_hold_en (GPIO_NUM_18);
  gpio_hold_en (GPIO_NUM_19);
  gpio_hold_en (GPIO_NUM_21);
  //gpio_hold_en (GPIO_NUM_22);
  gpio_hold_en (GPIO_NUM_23);
  gpio_hold_en (GPIO_NUM_25);
  gpio_hold_en (GPIO_NUM_26);
  gpio_hold_en (GPIO_NUM_27);
  gpio_hold_en (GPIO_NUM_32);
  gpio_hold_en (GPIO_NUM_33);
  gpio_deep_sleep_hold_en();      
  #endif
  
  float fpubint = atof(pubint);
  if (fpubint <60)
  {
    Serial.println("Data Publication Interval is less than 60 seconds, iTilt will not go to deep sleep but to setup()");
    delay(30000);  //Why large delay?
    setup();
  } 
  digitalWrite(LED_BUILTIN,HIGH);
  Serial.println("Entering deep sleep for " + String(fpubint) + " seconds");
  #ifdef ESP8266
  ESP.deepSleep(fpubint * 1000000);
  delay(1000);
  #endif
  #ifdef ESP32
  esp_sleep_enable_timer_wakeup(fpubint *  10000);
  esp_deep_sleep_start();
  delay(1000);
  #endif
}

float calcOffset()
{ 
  float reading;
  float areading=0;
  float n=100;
  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
  accelgyro.initialize();
  for (int i=0; i<n; i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    areading+=(1/n)*reading;
    Serial.println("Calibrating: "+String(reading));
    Serial.println("Avarage Reading: "+String(areading));
  }
  float offset;
  offset=89.0-areading;
  #ifdef ESP32
    if (atoi(mpu_orientation)==0)
      {offset=91-areading;} //This must be tested?
  #endif
  return offset;
}
float calcBatCap(float volts)  //linear interpolation used, data from http://www.benzoenergy.com/blog/post/what-is-the-relationship-between-voltage-and-capacity-of-18650-li-ion-battery.html
{
  float capacity;
  Serial.println("You are in calcBatCap(): volts is: "+String(volts));
  if (volts<=3)
  {
    capacity=0;
    return capacity;
  }
  if (volts>3 and volts<=3.45)
  {
    capacity=0+((volts-3)/(3.45-3))*5;
    return capacity;    
  }
  if (volts>3.45 and volts<=3.68)
  {
    capacity=5+((volts-3.45)/(3.68-3.45))*5;
    return capacity;    
  }
  if (volts>3.68 and volts<=3.74)
  {
   capacity=10+((volts-3.68)/(3.74-3.68))*10;
   return capacity;   
  }
  if (volts>3.74 and volts<=3.77)
  {
    capacity=20+((volts-3.74)/(3.77-3.74))*10;
    return capacity;    
  }
  if (volts>3.77 and volts<=3.79)
  {
    capacity=30+((volts-3.77)/(3.79-3.77))*10;
    return capacity;    
  }
    
  if (volts>3.79 and volts<=3.82)
  {
    capacity=40+((volts-3.79)/(3.82-3.79))*10;
    return capacity;    
  }
  if (volts>3.82 and volts<=3.87)
  {
    capacity=50+((volts-3.82)/(3.87-3.82))*10;
    return capacity;    
  }                
  if (volts>3.87 and volts<=3.92)
  {
    capacity=60+((volts-3.87)/(3.92-3.87))*10;
    return capacity;    
  }       
  if (volts>3.92 and volts<=3.98)
  {
    capacity=70+((volts-3.92)/(3.98-3.92))*10;
    return capacity;    
  }       
  if (volts>3.98 and volts<=4.06)
  {
    capacity=80+((volts-3.98)/(4.06-3.98))*10;
    return capacity;    
  }      
  if (volts>4.06)
  {
    capacity=90+((volts-4.06)/(4.16-4.06))*10;
    if (capacity>100)
    {capacity=100;}
    return capacity;    
  }           
}

float calcBatVolt(int samle_size)
{
  float reading=0;
  float n=samle_size;
  for (int i=0;i<n;i++)
  { 
    #ifdef ESP8266
    reading+=(1/n)*analogRead(A0)/atof(batconvfact);
    #endif
    #ifdef ESP32
    reading+=(1/n)*analogRead(atoi(batvolt_pin))/atof(batconvfact);
    #endif
  }

  return reading;
}

float calcTemp()//DS19B20 Sensor Texas Instruments
  { 
    OneWire oneWire(atoi(onewire_pin));
    DallasTemperature sensors(&oneWire);
    sensors.begin();
    sensors.requestTemperatures();
    return sensors.getTempCByIndex(0);
  }

float calcGyroTemp()
{
  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 
  accelgyro.initialize();
  float temp=accelgyro.getTemperature()/340 +36.53;
  accelgyro.setSleepEnabled(true);
  return temp;
}

float calcTilt(int samplesize)
{ 
  float reading;
  float areading=0;
  float n=samplesize;

  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 
  accelgyro.initialize();
  //Get Gyroscope readings untill it stoped reading nan (Not a Number)
  for (int i=0;i<200;i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    Serial.println("You are in calcTilt(). reading="+String(reading));
    if (String(reading)!="nan")
    {
      break;
    }
    if (i==999)
    {
      Serial.println("You are in calcTilt(). The MPU6050 could not provide numerical readings for 1000 times. Check your soldering and Test the MPU6050.");
    }
  }
  
  for (int i=0; i<n; i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    areading+=(1/n)*reading;
//    Serial.println("Calibrating: "+String(reading));
//    Serial.println("Avarage Reading: "+String(areading));
  }
  areading+=atof(tiltOffset);
//  Serial.println("Gyro Readings: "+String(ax)+","+String(ay)+","+String(az));
//  Serial.println("Calculated Tilt: "+String(reading));
  accelgyro.setSleepEnabled(true);//-----------------------------------------------------------------------------------------------
  #ifdef ESP32
    Serial.println("You are in calcTilt() mpu_orientation is: "+String(mpu_orientation));
    if (atoi(mpu_orientation)==0) //This is when Vcc on MPU is vacing down
    {
      areading=90-(areading-90);
    }
  #endif
  return areading;

}


float calcRoll(int samplesize)
{ 
  float reading;
  float areading=0;
  float n=samplesize;

  pinMode(atoi(i2c_sda_pin), OUTPUT);//This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  pinMode(atoi(i2c_scl_pin), OUTPUT); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_sda_pin), LOW); //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  digitalWrite(atoi(i2c_scl_pin), LOW);  //This need to be done, possable conflict between WiFimanager and Wire Library (i2c). It must be before Wire.begin()
  delay(100);
  Wire.begin(atoi(i2c_sda_pin), atoi(i2c_scl_pin));
  Wire.beginTransmission(i2c_address);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);
 
  accelgyro.initialize();
  //Get Gyroscope readings untill it stoped reading nan (Not a Number)
  for (int i=0;i<1000;i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(ax / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;  //reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    Serial.println("You are in calcRoll(). reading="+String(reading));
    if (String(reading)!="nan")
    {
      break;
    }
    if (i==999)
    {
      Serial.println("You are in calcRoll(). The MPU6050 could not provide numerical readings for 1000 times. Check Soldering or MPU6050");
    }
  }
  
  for (int i=0; i<n; i++)
  {
    accelgyro.getAcceleration(&ax, &az, &ay);
    reading=acos(ax / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;  //reading=acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / M_PI;
    areading+=(1/n)*reading;
//    Serial.println("Calibrating: "+String(reading));
//    Serial.println("Avarage Reading: "+String(areading));
  }
  
//  Serial.println("Gyro Readings: "+String(ax)+","+String(ay)+","+String(az));
//  Serial.println("Calculated Roll: "+String(areading));
  accelgyro.setSleepEnabled(true);//-----------------------------------------------------------------------------------------------
  return areading;

}

float calcGrav(float tilt)
{
  float fcoefficientx3 = atof(coefficientx3);
  float fcoefficientx2 = atof(coefficientx2);
  float fcoefficientx1 = atof(coefficientx1);
  float fconstantterm = atof(constantterm);
  return fcoefficientx3 * (tilt * tilt * tilt) + fcoefficientx2 * tilt * tilt + fcoefficientx1 * tilt + fconstantterm;
}

float calcABV(float gravity)
{
  float abv = 131.258 * (atof(originalgravity) - gravity);
  return abv;
}

void bindServerCallback() {

  wm.server->on("/readings", handleReadings);
  wm.server->on("/offsetcalibration", handleOffsetCalibration);
  wm.server->on("/polynomialcalibrationstart", handlePolynomialCalibrationStart);
  wm.server->on("/polynomialcalibrationinput",handlePolynomialCalibrationInput);
  wm.server->on("/polynomialcalibrationresults",handlePolynomialCalibrationResults);
  wm.server->on("/pinconfinput",handlePinConfInput);
  wm.server->on("/updatepinconfresults",handlePinConfResults);
  wm.server->on("/",handleRoute); // you can override wm! main page
}
void handleRoute()
{
  Serial.println("[HTTP] handle Route");
  String htmlText = "<HTML><HEAD><TITLE>iTilt Main Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CONFIGURATION PORTAL for iTilt</h1>";
  htmlText+=htmlMenueText;   
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);
}


void handlePinConfResults()
{
  Serial.println("[HTTP] handle PinConfResults");
  Serial.println("saving pinconfig");  
  DynamicJsonDocument json(1024);
  #ifdef ESP32  
  json["power_pin"]   = wm.server->arg(0);
  json["power_pin2"]   = wm.server->arg(1);
  json["batvolt_pin"]   = wm.server->arg(2);    
  json["onewire_pin"] =wm.server->arg(3);
  json["i2c_sda_pin"]   =wm.server->arg(4);
  json["i2c_scl_pin"]   = wm.server->arg(5);
  json["mpu_orientation"]=wm.server->arg(6);

  
  #endif
  #ifdef ESP8266
  json["onewire_pin"] =wm.server->arg(0);
  json["i2c_sda_pin"]   =wm.server->arg(1);
  json["i2c_scl_pin"]   = wm.server->arg(2);  
  #endif


  File pinConfigFile = SPIFFS.open("/pinconfig.json", "w");
  if (!pinConfigFile) {
    Serial.println("failed to open pinconfig file for writing");
  }

  serializeJson(json, Serial);
  serializeJson(json, pinConfigFile);

  pinConfigFile.close(); 
   
  String htmlText="<HTML><HEAD><TITLE>iTilt Custom Pin Configuration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CUSTOM PIN CONFIGURATION IS UPDATED AS FOLLOW</h1>";
  #ifdef ESP32  
  htmlText+="<p>"+wm.server->argName(0)+" "+String(wm.server->arg(0))+"<br>";
  htmlText+=wm.server->argName(1)+" "+String(wm.server->arg(1))+"<br>";
  htmlText+=wm.server->argName(2)+" "+String(wm.server->arg(2))+"<br>";
  htmlText+=wm.server->argName(3)+" "+String(wm.server->arg(3))+"</p>";
  htmlText+=wm.server->argName(4)+" "+String(wm.server->arg(4))+"</p>";
  htmlText+=wm.server->argName(5)+" "+String(wm.server->arg(5))+"</p>"; 
  htmlText+=wm.server->argName(6)+" "+String(wm.server->arg(6))+"</p>";       
  #endif
  #ifdef ESP8266
  htmlText+=wm.server->argName(0)+" "+String(wm.server->arg(0))+"<br>";
  htmlText+=wm.server->argName(1)+" "+String(wm.server->arg(1))+"<br>";
  htmlText+=wm.server->argName(2)+" "+String(wm.server->arg(2))+"</p>";    
  #endif

  htmlText+="<p> The iTilt will restart in less than 30 seconds. Updates will take effect after restart.</p>";
 
  wm.server->send(200, "text/html", htmlText);
  delay(2000);
  #ifdef ESP32
  ESP.restart();
  delay(3000); 
  #endif
  #ifdef ESP8266  //For some reason the ESP8266 AP Config Portal does not terminate on reset, restart, WiFi.disconnect()?
  ESP.deepSleep(20 * 1000000);
  delay(3000);
  #endif
     
}

void handlePinConfInput()
{
  Serial.println("[HTTP] handle PinCpnfInput");
  String htmlText = "<HTML><HEAD><TITLE>iTilt Custom Pin Configuration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>CUSTOM PIN CONFIGURATION for iTilt</h1>";
  htmlText+="<p>Thi iTilt uses default pin configurations";
  htmlText+="<br>You may re congigure these pins for your custom design";
  htmlText+="<br>Use only numerical values for GPIO numbers.";

  #ifdef ESP32
  htmlText+="<b><br>ESP32 (Works very well with ESP32-E Firebeetle:</b><br> Power=25,25, Battery Voltage=36, One Wire(DS18B20 Data)=26,<br> GYRO MPU6050_SDA=17, GYRO MPU6050_SCL=16 <br>";
  htmlText+="<br>To see what pins suitable for Temperature and Gyroscope sensors, <a href='https://randomnerdtutorials.com/esp32-pinout-reference-gpios/'>https://randomnerdtutorials.com/esp32-pinout-reference-gpios/</a>";
  htmlText+="<br>The ESP32 has 2 ADC chips. ADC 2 is used by WiFi. You should only use pins GPIO 39, 36, 34, 35, 32, 33 to measure battery voltage"; 
  #endif
  #ifdef ESP8266
  htmlText+="<b>ESP8266 (iSpindel):</b><br> One Wire(DS18B20 TEMP Data)=12,<br> GYRO MPU6050_SDA=0, MPU6050_SCL=2<br>";
  #endif
  htmlText+="<p>Current Pin Configuration<br>";
  #ifdef ESP32
  htmlText+="Power Pin (MPU6050): "+String(power_pin)+"<br>";
  htmlText+="Power Pin2 (DS18B20): "+String(power_pin2)+"<br>";
  htmlText+="Battery Voltage Pin: "+String(batvolt_pin)+"</p>";
  htmlText+="Gyro Orientation: Vcc up=1, Vcc down=0"+String(mpu_orientation)+"</p>";     
  #endif
  htmlText+="One Wire Pin: "+String(onewire_pin)+"<br>";
  htmlText+="i2c SDA Pin: "+String(i2c_sda_pin)+"<br>"; 
  htmlText+="i2C SCL Pin: "+String(i2c_scl_pin)+"</p>";
  
  htmlText+="<p><form action='/updatepinconfresults?' method ='POST'>";
  htmlText+="<table border='1' style='font-size:30pt;'>";
  #ifdef ESP32
  htmlText+="<tr><td>POWER PIN (ESP32 ONLY)</td><td><input type='number' name='Power Pin' size='5' min='0' max='40' step='1' value="+String(power_pin)+" style='height:80px;font-size:30pt;'></td></tr>";
  htmlText+="<tr><td>POWER PIN 2 (ESP32 ONLY)</td><td><input type='number' name='Power Pin2' size='5' min='0' max='40' step='1' value="+String(power_pin2)+" style='height:80px;font-size:30pt;'></td></tr>"; 
  htmlText+="<tr><td>Battery Voltage Measure Pin (ESP32 ONLY)</td><td><input type='number' name='Battery Voltage Pin' size='5' min='0' max='40' step='1' value="+String(batvolt_pin)+" style='height:80px;font-size:30pt;'></td></tr>";
  htmlText+="<tr><td>ONE WIRE PIN (DS18B20) Temperature Sensor</td><td><input type='number' name='One Wire Pin' size='5' min='0' max='40' step='1' value="+String(onewire_pin)+" style='height:80px;font-size:30pt;'></td></tr>"; 
  htmlText+="<tr><td>i2c SDA MPU6050 GYRO</td><td><input type='number' name='i2c SDA Pin' size='5' min='0' max='40' step='1' value="+String(i2c_sda_pin)+" style='height:80px;font-size:30pt;'></td></tr>"; 
  htmlText+="<tr><td>i2c SCL MPU6050 GYRO</td><td><input type='number' name='i2c SCL' size='5' min='0' max='40' step='1' value="+String(i2c_scl_pin)+" style='height:80px;font-size:30pt;'></td></tr>";
  htmlText+="<tr><td>Gyro Orientation</td><td><input type='number' name='mpu orientation' size='5' min='0' max='40' step='1' value="+String(mpu_orientation)+" style='height:80px;font-size:30pt;'></td></tr></table>";  
  #endif
  #ifdef ESP8266
  htmlText+="<tr><td>ONE WIRE PIN (DS18B20) Temperature Sensor</td><td><input type='number' name='One Wire Pin' size='5' min='0' max='40' step='1' value="+String(onewire_pin)+" style='height:80px;font-size:30pt;'></td></tr>"; 
  htmlText+="<tr><td>i2c SDA MPU6050 GYRO</td><td><input type='number' name='i2c SDA Pin' size='5' min='0' max='40' step='1' value="+String(i2c_sda_pin)+" style='height:80px;font-size:30pt;'></td></tr>"; 
  htmlText+="<tr><td>i2c SCL MPU6050 GYRO</td><td><input type='number' name='i2c SCL' size='5' min='0' max='40' step='1' value="+String(i2c_scl_pin)+" style='height:80px;font-size:30pt;'></td></tr></table>";
  #endif
  
  htmlText+="<br><input type='submit' value='UPDATE PINS AND RESTART' style='height:80px;font-size:30pt;'>";
  htmlText+="</form></p>";         
  htmlText+=htmlMenueText;
  wm.server->send(200, "text/html", htmlText);
}
  
void handleOffsetCalibration()
{ 
  float offset=calcOffset(); 
  Serial.println("[HTTP] handle Offset Calibration");
  Serial.println("OFFSET Calculated, Send to /offsetcalibration?: "+String(offset));
  String htmlText = "<HTML><HEAD><TITLE>iTilt Offset Calibration</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>OFFSET CALIBRATION</h1>";
  htmlText+="<p> Calculated Offset: "+String(offset)+"</p>";
  htmlText+="<p> Insert this value, with iets sign in the <a href='/wifi?' target='_blank'>Configure WiFi</a> page.</p>";
  htmlText+=htmlMenueText;
  htmlText += "</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}
void handlePolynomialCalibrationResults()
{
  Serial.println("[HTTP] handle Polynomial Calibration Results");
  Serial.println("Number of server arguments: "+String(wm.server->args()));
  int n=wm.server->args()/2;
  int numargs=wm.server->args();
  Serial.println("Sample Size in handlePolynomialCalibrationResults() : "+String(n));

  String htmlText = "<HTML><HEAD><TITLE>iTilt POLYNOMIAL RESULTS</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD: RESULTS</h1>";

//  for (int i=0; i<wm.server->args()-1;i++)
//      {
//        text+= "Argument "+String(i)+", Argument Name: "+wm.server->argName(i)+", Argument Value: "+String(wm.server->arg(i));
//        text+="<br>";        
//      }

  for (int i=0; i<numargs;i++)
  {
    Serial.println("Argument number: "+String(i)+", Argument Name: "+wm.server->argName(i)+", Argument Value: "+String(wm.server->arg(i)));
  }

  
  double sampledtilt[n];
  double sampledgrav[n];
  int recordnumber=0;
  htmlText+="<table border='1'><align='right'><tr><td>TILT VALUES</td><td>GRAVITY VALUES</td></tr>";
  for (int i=0; i<numargs; i=i+2)
  {
    sampledtilt[recordnumber]=atof(wm.server->arg(i).c_str());
    sampledgrav[recordnumber]=atof(wm.server->arg(i+1).c_str());
    htmlText+="<tr><td>"+String(sampledtilt[recordnumber])+"</td><td>"+String(sampledgrav[recordnumber],3)+"</td></tr>";
    recordnumber++;
  }
//  {
//    sampledtilt[i]=atof(wm.server->arg(i).c_str());
//    Serial.println("sampled tilt"+String(i)+": "+String(sampledtilt[i]));
//    sampledgrav[i]=atof(wm.server->arg(i+n).c_str());
//    Serial.println("sampled gravity"+String(i)+": "+String(sampledgrav[i],3)); 
//    htmlText+="<tr><td>"+String(sampledtilt[i])+"</td><td>"+String(sampledgrav[i],3)+"</td></tr>";
//    
//    //htmlText+= "Sampled_Tilt"+String(sampledtilt[i]) +", Sampled_Gravity"+String(sampledgrav[i],3)+"<br>";  
//  }
  htmlText+="<align></table>";
  int order = 3;
  double coeffs[order+1];   
  int ret = fitCurve(order, sizeof(sampledgrav)/sizeof(double), sampledtilt, sampledgrav, sizeof(coeffs)/sizeof(double), coeffs);
  if (ret==0)
  {
    Serial.println("Coefficiant of tilt^3: "+String(coeffs[0],15));
    Serial.println("Coefficiant of tilt^2: "+String(coeffs[1],15));
    Serial.println("Coefficiant of tilt^1: "+String(coeffs[2],15));
    Serial.println("Constant Term: "+String(coeffs[3],15));
    htmlText+="<br> COPY AND PASTE THE FOLOWING VALUES TO THE <a href='/wifi?' target='_blank'>CONFIGURE WIFI PAGE</a></br>";
    htmlText+="<br>Coefficiant of tilt^3: "+String(coeffs[0],15)+"<br> Coefficiant of tilt^2: "+String(coeffs[1],15)+"<br> Coefficiant of tilt^1: "+String(coeffs[2],15)+"<br> Constant Term: "+String(coeffs[3],15);
  }
  else
  {
    Serial.println("Failed to calculate Coefficients.");
  }
  //Calculating R^2 Rsquare
  float agrav=0;  //average of gravity
  for (int i=0 ; i<n; i++)
  {
    agrav+=(1/float(n))*sampledgrav[i];
    Serial.println("Calculating agrav: "+String(agrav,5));
  }
  Serial.println("Average Gravity: "+String(agrav,8));
  float SSres=0;
  for (int i=0 ; i<n; i++)
  {
    SSres+=pow((sampledgrav[i]-(coeffs[0]*sampledtilt[i]*sampledtilt[i]*sampledtilt[i]+coeffs[1]*sampledtilt[i]*sampledtilt[i]+coeffs[2]*sampledtilt[i]+coeffs[3])),2);
  }
  Serial.println("SSres: "+String(SSres,8));
  float SStot=0;
  for (int i=0 ; i<n; i++)
  {
    SStot+=pow((sampledgrav[i]-agrav),2);
  }
  Serial.println("SStot: "+String(SStot,8));
  float Rsquare=1-SSres/SStot;
  htmlText+="<br> Coefficient of determination: "+String(Rsquare,15)+" (This measures the strenght of the statistical fit. You should get something in the range of 0.98-0.99"; 
  htmlText+=htmlMenueText; 
  htmlText+="</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
  
}

void handlePolynomialCalibrationInput()
{   
  String htmlText="<HTML><HEAD><TITLE>iTilt Polynomial Calibration</TITLE></HEAD><style>  body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; } </style>";
  htmlText+= htmlStyleText;
  htmlText += "<BODY><h1>POLYNOMIAL (MODEL) CALIBRATION WIZARD: SAMPLED DATA</h1>";
  htmlText+="<p>This wizard should assist you in calibrating the iTilt. If you are here, you may already have a calibration data sample set (ordered pairs of Tilt(Measured in <a href='/readings?'>SENSOR READINGS</a>) ";
  htmlText+="and Gravity (Measured with a Hydrometer). If not, you can do it now. Make sure your publication interval is set to 0, and portal time out is 9999. </p>";
  htmlText+="<p>We suggest you use a 3L measuring jar, boil 0.55kg sugar in 1.5L clean water. Let the mix cool down to about 20 degrees Celsius. Add your mix to the jar. ";
  htmlText+="Full the jar with extra water until it reaches 2.5L. Stir the content properly (before each measurement). Measure your Tilt, Measure you Gravity (If you have a hydro meter). If not, use the theoretical values.";
  htmlText+="<p>The instructions in the 3rd column is a guide only, you may ignore them</p>";
  htmlText+="<p>ALL RECORDS MUST BE COMLETED. The polynomial will be WRONG otherwise</p>";
  htmlText+="<p>IMPORTANT: Make sure your iTilt is free floating. It must not touch the bottom or two sides of the jar.</p>";
  htmlText+= "<p><table style='height:80px;font-size:20pt;' border='1'><tr><td><br><form action='/polynomialcalibrationresults?' method ='POST'><b> TILT:</b></td><td><b>GRAVITY:</b></td><td><b>WIZARD INSTRUCTIONS AND THEORETICAL GRAVITY</b></td></tr>";
  int n=atoi(wm.server->arg(0).c_str());
  Serial.println("Sample Size in handlePolynomialCalibrationInput() : "+String(n));
  for (int i=0; i<n; i++)
  {
    htmlText+= "<tr><td><input type='number' name=+""tilt"+String(i) +" size='5' min='12.60' max='80.00' step='0.01' style='height:80px;font-size:30pt;'></td>";
    htmlText+= "<td><input type='number' name=+""gravity"+String(i) +" size='5' min='1' max='1.12' step='0.001' style='height:80px;font-size:30pt;'></td>";
    if (i==0)
    {
     htmlText+="<td>";
     htmlText+="Your sugar content in the jar of 2.5L is 500g. The Theoretical Gravity is 1.084 SG";
     htmlText+="</td></tr>";     
    }
    else if (i==1)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 600 ml of mix in jar and replace it with 600 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 410 g. The Theoretical Gravity is 1.064 SG";           
     htmlText+="</td></tr>";          
    }
    else if (i==2)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 700 ml of mix in jar and replace it with 700 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 301 g. The Theoretical Gravity is 1.046 SG";       
     htmlText+="</td></tr>";          
    }
    else if (i==3)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 800 ml of mix in jar and replace it with 800 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 205 g. The Theoretical Gravity is 1.032 SG";              
     htmlText+="</td></tr>";        
    } 
    else if (i==4)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 900 ml of mix in jar and replace it with 900 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 131 g. The Theoretical Gravity is 1.020 SG";              
     htmlText+="</td></tr>";        
    } 
    else if (i==5)
    {
     htmlText+="<td>";
     htmlText+="Before any measurement, remove 1100 ml of mix in jar and replace it with 1100 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 73 g. The Theoretical Gravity is 1.011 SG";              
     htmlText+="</td></tr>";          
    } 
    else if (i==6)
    {
     htmlText+="<td>"; 
     htmlText+="Before any measurement, remove 1300 ml of mix in jar and replace it with 1300 ml clean water. ";
     htmlText+="Your sugar content in the jar of 2.5L is 35 g. The Theoretical Gravity is 1.005";             
     htmlText+="</td></tr>";           
    }
    else
     htmlText+="<td></td></tr>";     
  }
  htmlText += "</table><br><input type='submit' value='Submit' style='height:80px;font-size:30pt;'>  </form></p><br>";
  htmlText += "<br></BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);
}

void handlePolynomialCalibrationStart()
{
  String htmlText = "<HTML><HEAD><TITLE>iTilt Calibration Page</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>POLYNOMIAL CALIBRATION WIZARD</h1>";
  htmlText+="<p> This links will not work if you are connected to the iTilt Access point.</p>";
  htmlText+="<p>Sugar Wash calculators will indicate how much sugar you need to add into a specific amount of water to reach certain gravity</p>";
  htmlText+="<p>Sugar wash Calculator at https://chasethecraft.com/calculators</p>";
  htmlText+="<p>Sugar wash Calculator at https://www.hillbillystills.com/distilling-calculator</p>";  

  htmlText+="<p>It is recomended to use a sample size of 7 and the and water volumes and sugar weights provided in the next step.</p>";  
  htmlText+="<p><form action='/polynomialcalibrationinput?' method ='POST'> SAMPLE SIZE (6-30): <input type='number' name='sample_size' size='5' min='6' max='30' step='1' style='height:80px;font-size:30pt;'><input type='submit' value='Submit' style='height:80px;font-size:30pt;'>  </form></p><br>";
  //htmlText += "<p><a href='/wifi?'>--Configure Wifi--</a><a href='/readings?'>--Sensor Readings--</a> <a href='/offsetcalibration?'>--Offset Calibration--</a><a href='/polynomialcalibrationstart?'>--Polynomial Calibration--</a><a href='/'>--Main Page--</a></p>";
  htmlText+=htmlMenueText;   
  
  htmlText += "</BODY></HTML>";  
  wm.server->send(200, "text/html", htmlText);    
}

void handleReadings() 
{
  float batvolt=calcBatVolt(200);
  float tilt= calcTilt(40);
  float roll= calcRoll(40);
  float grav=calcGrav(tilt);
  float abv=calcABV(grav);
  Serial.println("[HTTP] handle Readings");
  String htmlText = "<HTML><HEAD><meta http-equiv='refresh' content='1'><TITLE>iTilt Sensor Readings</TITLE></HEAD>";
  htmlText+= htmlStyleText;
  htmlText+= "<BODY><h1>iTilt Sensor Readings</h1>";
  htmlText+= "<p>Sensor Readings will update every 2 seconds.</p>";
  htmlText+= "<p>Battery Voltage: " + String(batvolt,2) + "</p>";
  htmlText+= "<p>Battery Remaining Capacity: " + String(calcBatCap(batvolt),0) + " percent</p>";  
  htmlText+= "<p>Tilt: " + String(tilt) + " Degrees. This value should be about 89 degrees if the iTilt is on a horizontal surface. If not, do a Tilt Ofset calibration or resolder your MPU6050 Gyroscope</p>";
  htmlText+="<p>Roll: "+String(roll)+" Degrees. This value should be close to 90 when iTilt is free flowing in liquid. . </p>";
  htmlText+= "<p>Gravity: " + String(grav, 5) + " SG</p>";
  htmlText+= "<p>Temperature according to DS18B20 (Very accurate): " + String(calcTemp()) + " Degrees Celcius</p>";
  htmlText+="<p>Gyro Internal Temperature (1 degree resolution): "+String(calcGyroTemp())+"</p>";
  htmlText+= "<p>Alcohol by Volume (ABV): " + String(abv) + " %</p>";
  htmlText+=htmlMenueText; 
  htmlText += "</BODY></HTML>";
  wm.server->send(200, "text/html", htmlText);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void iBeacon(float temp, float grav){
  BLEAdvertising *pAdvertising;
  BLEDevice::init("TILT");
  pAdvertising = BLEDevice::getAdvertising();
  BLEBeacon oBeacon = BLEBeacon();
  oBeacon.setManufacturerId(0x4C00); // fake Apple 0x004C LSB (ENDIAN_CHANGE_U16!)
  oBeacon.setProximityUUID(BLEUUID(BEACON_UUID));
  oBeacon.setMajor(int(temp*9/5+32));
  oBeacon.setMinor(int(grav*1000));
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
//  BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
  
  oAdvertisementData.setFlags(0x04); // BR_EDR_NOT_SUPPORTED 0x04
  
  std::string strServiceData = "";
  
  strServiceData += (char)26;     // Len
  strServiceData += (char)0xFF;   // Type
  strServiceData += oBeacon.getData(); 
  oAdvertisementData.addData(strServiceData);
  
  pAdvertising->setAdvertisementData(oAdvertisementData);
//  pAdvertising->setScanResponseData(oScanResponseData);
  pAdvertising->start();

}










void setup() 
{ 

  Serial.begin(115200);
  Serial.println();
  Serial.println("The iTilt is starting up....");

  accelgyro.setSleepEnabled(true);

  delay(100);  //This is for accurate battery voltage reading
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
  //clean FS, for testing
  //SPIFFS.format();
  //wm.resetSettings();
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {

          Serial.println("\nparsed json");
          strcpy(portalTimeOut, json["portalTimeOut"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_clientid, json["mqtt_clientid"]);
          strcpy(mqtt_service, json["mqtt_service"]);    //*******************************************************************     
          strcpy(coefficientx3, json["coefficientx3"]);
          strcpy(coefficientx2, json["coefficientx2"]);
          strcpy(coefficientx1, json["coefficientx1"]);
          strcpy(constantterm, json["constantterm"]);
          strcpy(batconvfact, json["batconvfact"]);
          strcpy(pubint, json["pubint"]);
          strcpy(originalgravity, json["originalgravity"]);
          strcpy(tiltOffset, json["tiltOffset"]);
          strcpy(itiltnum,json["itiltnum"]);
          strcpy(dummy,json["dummy"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }

      if (SPIFFS.exists("/pinconfig.json")) {
      //file exists, reading and loading
      Serial.println("reading Pin config file");
      File pinConfigFile = SPIFFS.open("/pinconfig.json", "r");
      if (pinConfigFile) {
        Serial.println("opened Pin config file");
        size_t size = pinConfigFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        pinConfigFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024*2);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {

          Serial.println("\nparsed json");
          #ifdef ESP32
          strcpy(power_pin, json["power_pin"]);
          strcpy(power_pin2, json["power_pin2"]);
          strcpy(batvolt_pin, json["batvolt_pin"]);
          strcpy(onewire_pin, json["onewire_pin"]);          
          strcpy(i2c_sda_pin, json["i2c_sda_pin"]);
          strcpy(i2c_scl_pin, json["i2c_scl_pin"]);
          strcpy(mpu_orientation, json["mpu_orientation"]);                  
          #endif
          #ifdef ESP8266
          strcpy(onewire_pin, json["onewire_pin"]);          
          strcpy(i2c_sda_pin, json["i2c_sda_pin"]);
          strcpy(i2c_scl_pin, json["i2c_scl_pin"]);
          #endif

        } else {
          Serial.println("failed to load json pinconfig");
        }
        pinConfigFile.close();
      }
    }

    
  } else {
    Serial.println("failed to mount FS");  //This hapen with new ESP32 KOALA WROVER-Format Spifs?
    SPIFFS.format();  //May be needed to format ESP32 with MicroPython or iSpindel Firmware
  }

  #ifdef ESP32  //power the MPU6050 and DS18B20 from GPIO PINS OR SWITCH THEM WITH TRANSISTORS
    gpio_hold_dis(GPIO_NUM_2);
    gpio_hold_dis(GPIO_NUM_33);
    gpio_hold_dis(GPIO_NUM_0);  //https://github.com/espressif/arduino-esp32/issues/2712 To Remove the lock on the GPIO
    //GPIO 1 and 3 ist TXD and RXD
  
    gpio_hold_dis(GPIO_NUM_4);  //https://github.com/espressif/arduino-esp32/issues/2712 To Remove the lock on the GPIO
    gpio_hold_dis(GPIO_NUM_5);  //https://github.com/espressif/arduino-esp32/issues/2712  To Remove the lock on the GPIO
    gpio_hold_dis(GPIO_NUM_12);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis(GPIO_NUM_13);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_14);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_15);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_16);  //https://github.com/espressif/arduino-esp32/issues/2712     
    gpio_hold_dis (GPIO_NUM_17);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_18);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_19);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_21);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_22);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_23);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_25);  //https://github.com/espressif/arduino-esp32/issues/2712  
    gpio_hold_dis (GPIO_NUM_26);  //https://github.com/espressif/arduino-esp32/issues/2712  
    gpio_hold_dis (GPIO_NUM_27);  //https://github.com/espressif/arduino-esp32/issues/2712  
    gpio_hold_dis (GPIO_NUM_32);  //https://github.com/espressif/arduino-esp32/issues/2712 
    gpio_hold_dis (GPIO_NUM_33);  //https://github.com/espressif/arduino-esp32/issues/2712     
    Serial.println("Power Pin: "+String(atoi(power_pin)) +" must go high");
    Serial.println("Power Pin: "+String(atoi(power_pin2)) +" must go high");    
    pinMode(atoi(power_pin),OUTPUT);
    digitalWrite(atoi(power_pin),HIGH);
    pinMode(atoi(power_pin2),OUTPUT);
    digitalWrite(atoi(power_pin2),LOW);
  #endif

  float tilt=calcTilt(50);
  float roll=calcRoll(20);
  if (roll<20 or roll>160)
  {inviniteSleep();}
  float grav=calcGrav(tilt);
  float abv=calcABV(grav);
  float temp=calcTemp();
  float tempgyro=calcGyroTemp();  
  #ifdef ESP32
    digitalWrite(atoi(power_pin),LOW); 
    digitalWrite(atoi(power_pin2),LOW);
    iBeacon(tempgyro,grav);   
    Serial.println("First run of mesurement"); 
  #endif    
  /* for what purpose it is in this place?
  float fcoefficientx3;
  float fcoefficientx2;
  float fcoefficientx1;
  float fconstantterm; */
  float foriginalgravity; 
  float fpubint;
  float batvolt=calcBatVolt(500);
  if (batvolt<3)
  {
    Serial.println("Battery voltage is less than 3 volt. Recharge the battery or fix the battery conversion factor.");
    inviniteSleep();
  }
  float batcap=calcBatCap(batvolt);
  
  Serial.println("You are in setup(), Tilt is: "+String(tilt)+ ", If tilt <12 degrees, between 85 and 95 degress or nan, The Wifi Manager configuration portal will run");
  if ((tilt<95 and tilt>85) or tilt<12 or String(tilt)=="nan")  //Condition to run The Wifi Manager portal.  
  {
    pinMode(LED_BUILTIN,OUTPUT);  //It seems like WiFimanager or WiFi frequently overwright this statement
    for (int i=0;i<10;i++)
      {
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      }
 
    digitalWrite(LED_BUILTIN, HIGH);    
    Serial.println("iTilt will enter the WiFi configuration portal");
    Serial.println("Should run portal");

    WiFiManagerParameter custom_portalTimeOut("portalTimeOut", "WiFi Manager Configuration Portal Time Out", portalTimeOut, 4);
    WiFiManagerParameter custom_mqttHTML("<p><font size='4'><b>Provide the Parameters of Cayenne</b></font></p>");
    WiFiManagerParameter custom_mqtt_service("service", "MQTT SERVICE (CAYENNE, UBIDOTS, ADAFRUIT) OR TELEGRAM SUPORTED", mqtt_service, 21);    //****************************
    WiFiManagerParameter custom_mqtt_username("usernname", "MQTT USERNAME/telegram token", mqtt_username, 46);
    WiFiManagerParameter custom_mqtt_password("password", "MQTT PASSWORD", mqtt_password, 40); //*****pasword was spelled wrong
    WiFiManagerParameter custom_mqtt_clientid("clientid", "MQTT CLIENT ID/telgram chat ID", mqtt_clientid, 36);
    WiFiManagerParameter custom_polynomialHTML("<p><font size='4'><b>Provide the Parameters of you Polynomial</b></font></p>");                                                      //Custom HTML
    WiFiManagerParameter custom_coefficientx3("coefficientx3", "Coefficient of Tilt^3 (Regression Model Polynomial)", coefficientx3, 15);
    WiFiManagerParameter custom_coefficientx2("coefficientx2", "Coefficient of Tilt^2 (Regression Model Polynomial)", coefficientx2, 15);
    WiFiManagerParameter custom_coefficientx1("coefficientx1", "Coefficient of Tilt^1 (Regression Model Polynomial)", coefficientx1, 15);
    WiFiManagerParameter custom_constantterm("constantterm", "Constant Term (Regression Model Polynomial)", constantterm, 15);
    WiFiManagerParameter custom_iTiltotherHTML("<p><font size='4'><b>Other Parameters for the iTilt</b></font></p>");                                                           //Custom HTML
    WiFiManagerParameter custom_batconvfact("batconvfact", "Battery Conversion Factor", batconvfact, 7);
    WiFiManagerParameter custom_pubint("pubint", "Data Publication Interval", pubint, 5);
    WiFiManagerParameter custom_originalgravity("originalgravity", "Original Gravity", originalgravity, 5);
    WiFiManagerParameter custom_tiltOffset("tiltOffset", "Calibrated Tilt Offset", tiltOffset, 4);
    WiFiManagerParameter custom_itiltnum("itiltnum", "iTilt ID Number", itiltnum, 3);
           
    String menue="<p>"+htmlMenueText+"</p>";
    WiFiManagerParameter custom_menue(menue.c_str());
    
  
    //wm.resetSettings();
    //Setting Callbacks
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setWebServerCallback(bindServerCallback);  

  
    //add all your parameters here
    wm.addParameter(&custom_portalTimeOut);
    wm.addParameter(&custom_mqttHTML);
    wm.addParameter(&custom_mqtt_service);//********************************************************************    
    wm.addParameter(&custom_mqtt_username);
    wm.addParameter(&custom_mqtt_password);
    wm.addParameter(&custom_mqtt_clientid);
    wm.addParameter(&custom_polynomialHTML);
    wm.addParameter(&custom_coefficientx3);
    wm.addParameter(&custom_coefficientx2);
    wm.addParameter(&custom_coefficientx1);
    wm.addParameter(&custom_constantterm);
    wm.addParameter(&custom_iTiltotherHTML);
    wm.addParameter(&custom_batconvfact);
    wm.addParameter(&custom_pubint);
    wm.addParameter(&custom_originalgravity);
    wm.addParameter(&custom_tiltOffset);
    wm.addParameter(&custom_itiltnum);      
    wm.addParameter(&custom_menue);
    wm.setConfigPortalTimeout(atoi(portalTimeOut));
    wm.setCustomHeadElement(htmlWiFiConfStyleText.c_str());

    
    #ifdef ESP32
      pinMode(atoi(power_pin),OUTPUT);
      digitalWrite(atoi(power_pin),HIGH);
      pinMode(atoi(power_pin2),OUTPUT);
      digitalWrite(atoi(power_pin2),LOW);     
    #endif        
    String APName="iTilt_";
    Serial.println("WiFi Mac adress is: "+WiFi.macAddress());
    APName+=itiltnum;
    if (!wm.startConfigPortal(APName.c_str(),WiFi.macAddress().c_str()))
    {
      Serial.println("WiFi Manager Portal: User failed to connect to the portal or dit not save. Time Out");
      ESP.restart();
      digitalWrite(LED_BUILTIN, HIGH);
      delay(5000);
    }

    //read updated parameters
    strcpy(portalTimeOut, custom_portalTimeOut.getValue());
    strcpy(mqtt_username, custom_mqtt_username.getValue());
    strcpy(mqtt_password, custom_mqtt_password.getValue());
    strcpy(mqtt_clientid, custom_mqtt_clientid.getValue());
    strcpy(mqtt_service, custom_mqtt_service.getValue());  //*********************************************  
    strcpy(coefficientx3, custom_coefficientx3.getValue());
    strcpy(coefficientx2, custom_coefficientx2.getValue());
    strcpy(coefficientx1, custom_coefficientx1.getValue());
    strcpy(constantterm, custom_constantterm.getValue());
    strcpy(batconvfact, custom_batconvfact.getValue());
    strcpy(pubint, custom_pubint.getValue());
    strcpy(originalgravity, custom_originalgravity.getValue());
    strcpy(tiltOffset, custom_tiltOffset.getValue());
    strcpy(itiltnum, custom_itiltnum.getValue());
    strcpy(dummy,"xx");
  
    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("shouldSaveConfig is: "+String(shouldSaveConfig));
      Serial.println("saving config");  
      DynamicJsonDocument json(1024);  
      json["portalTimeOut"]   = portalTimeOut;
      json["mqtt_username"] = mqtt_username;
      json["mqtt_password"]   = mqtt_password;
      json["mqtt_clientid"]   = mqtt_clientid;
      json["mqtt_service"]   = mqtt_service;     
      json["coefficientx3"] = coefficientx3;
      json["coefficientx2"]  = coefficientx2;
      json["coefficientx1"]  = coefficientx1;
      json["constantterm"]   = constantterm;
      json["batconvfact"]   = batconvfact;
      json["pubint"]   = pubint;
      json["originalgravity"]   = originalgravity;
      json["tiltOffset"]   = tiltOffset;
      json["itiltnum"]   = itiltnum;     
      json["dummy"]   = dummy;  
  
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }
  
      serializeJson(json, Serial);
      serializeJson(json, configFile);
  
      configFile.close();
      //end save
    }    

//    mqttClient.setUsernamePassword(String(mqtt_username), String(mqtt_password));
//    mqttClient.setId(String(mqtt_clientid));
    foriginalgravity=atof(originalgravity);
    fpubint=atof(pubint); 
    tilt=calcTilt(50);
    grav=calcGrav(tilt);
    abv=calcABV(grav);
    temp=calcTemp();
    tempgyro=calcGyroTemp();
    batvolt=calcBatVolt(200);
    batcap=calcBatCap(batvolt); 
    #ifdef ESP32
      pinMode(atoi(power_pin),OUTPUT);
      digitalWrite(atoi(power_pin),LOW);
      pinMode(atoi(power_pin2),OUTPUT);
      digitalWrite(atoi(power_pin2),LOW);
      iBeacon(tempgyro,grav);
      Serial.println("Second run of mesurement");
    #endif  
  }
  else  //This is when tilt>12, nan or between 85 and 95 degrees and setup should not run
  {
    Serial.println("Should not run portal");
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if (tilt>85)
  {
    Serial.println("Tilt is larger than 85 degrees. It seems like it is not in your brew yet. ESP will restart");
    ESP.restart();
  }
   
    if (bootcount++ > 100) {
    bootcount=0;  
    Serial.println("10th run from sleep,WiFi need to connect with WiFi.begin(). Cridentials stored by WiFi manager in flash memory");
    WiFi.begin();
  
  for (int i=0;i<60;i++)  //Check if WiFi is connected
  {
    if (WiFi.status()==WL_CONNECTED)
    {
      break;
    }
    if (i==44)
    {
      Serial.println("The iTilt Could not connect to your WiFi Access Point. Deep Sleep will start...");
      startDeepSleep();
    }
    delay(200);
  }
  float signalstrength=(WiFi.RSSI()+90)*1.80;
  if (signalstrength>100)
  {signalstrength=100;}
  Serial.println("The iTilt connected to your WiFi access point");
  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  Serial.println("Selected Cloud Service: "+String(mqtt_service));
  if (String(mqtt_service)=="CAYENNE"){
    pubToCayenne(String(batcap,4),String(tempgyro,2), String(temp,4),String(tilt,4),String(batvolt,4),String(grav,4), String(abv,4), String(signalstrength), String(mqtt_username), String(mqtt_password), String(mqtt_clientid));
  }
  else if (String(mqtt_service)=="UBIDOTS"){
    pubToUbidots(String(batcap,2),String(tempgyro,2), String(temp,2),String(tilt,1),String(batvolt,2),String(grav,4), String(abv,2), String(signalstrength), String(mqtt_username), String(mqtt_password), String(mqtt_clientid));
  }
  else if (String(mqtt_service)=="ADAFRUIT"){
    pubToAdafruit(String(batcap,4),String(tempgyro,2), String(temp,4),String(tilt,4),String(batvolt,4),String(grav,4), String(abv,4), String(signalstrength), String(mqtt_username), String(mqtt_password), String(mqtt_clientid));
  }  
  else if (String(mqtt_service)=="TELEGRAM"){
    pubToTelegram(String(batcap,2),String(tempgyro,2), String(temp,2),String(tilt,4),String(batvolt,2),String(grav,4), String(abv,4), String(signalstrength), String(mqtt_username),String(mqtt_clientid));
  }
    } 
    else delay(3000);
  startDeepSleep();      
}

void loop()
{
  //void loop() should never execute

}
