#include <Arduino.h>
#include <Wire.h>           //I2C
#include <RTClibExtended.h> //RTC
#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_ADT7410.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <MS5803_01.h> // Pressure sensor
#include <ESP2SOTA.h>

// RTC
RTC_DS3231 rtc;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

String date_now = "";
String date_alarm = "";
String format_data = "";
bool OperationFlag;
String UTC_Value = "0";

int interval_sec = 0; // An alarm every 10 sec
int interval_min = 0; // An alarm every 0 min

// configuration
String loggerName = "LOGGER_001"; // Valeur initiale pour "Nom Logger"
String sampleRate;                // Valeur initiale pour "Sample Rate (min)"

// SERVEUR
const char *passphrase = "12345678";
WebServer server(80);
String wifiSSID;
String wifiPassword;
#define servername "logger" // Define the name to your server...
byte time_1[7];             // Temp buffer used to hold BCD time/date values
char buf[12];               // Buffer used to convert time[] values to ASCII strings
String webpage = "";        // String to save the html code
bool serverActive = false;
unsigned long serverStartTime = 0;
const unsigned long serverTimeout = 40000; // 40 seconds
bool previousButtonState = LOW;

// REED Switch
#define REED_Switch_WakeUP D1          // Webserveur ON
#define RTC_WakeUP D2                  // Webserveur ON
#define BUTTON_PIN_BITMASK 0x200000000 // 2^33 in hex

// ALIM GPIO
int GPIO_ALIM = D10;

// EEPROM Préferences.h
// Preferences preferences;
// Create the ADT7410 temperature sensor object
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();
float ADT7410_Temp_value; // Value T°

// VBATT
float Vbatt = 0;
float VbattPercent = 0;

// PRessure MS5803_01
MS_5803 sensor = MS_5803(4096);
// Profondeur fixe du capteur par rapport au repère zéro (exemple : 150 cm)
const float profondeurCapteur_cm = 150.0;
// Pression atmosphérique de référence en mbar (à mettre à jour manuellement ou via un capteur)
const float pressionAtmo_mbar = 1016.45; // standard, à ajuster si besoin

// File size conversion
String file_size(int bytes)
{
  String fsize = "";
  if (bytes < 1024)
    fsize = String(bytes) + " B";
  else if (bytes < (1024 * 1024))
    fsize = String(bytes / 1024.0, 3) + " KB";
  else if (bytes < (1024 * 1024 * 1024))
    fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";
  else
    fsize = String(bytes / 1024.0 / 1024.0 / 1024.0, 3) + " GB";
  return fsize;
}

// SPIFFS TEST like SD
bool SD_present = false;
File dataF;
File UploadFile;                                 // Handles the file upload a file to the SD
RTC_DATA_ATTR char filename[13] = "/LOG000.TXT"; // V2

void rtc_setup()
{
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  if (!rtc.begin())
  {
    //Serial.println("Couldn't find RTC");
    while (1)
      ;
  }
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  if (rtc.lostPower())
  {
  }
}

void rtc_get_date_and_alarm()
{
  DateTime now = rtc.now();
  // Assemble Strings to log data
  String theyear = String(now.year(), DEC);
  String themon = String(now.month(), DEC);
  String theday = String(now.day(), DEC);
  String thehour = String(now.hour(), DEC);
  String themin = String(now.minute(), DEC);
  String thesec = String(now.second(), DEC);
  // Put all the time and date strings into one String
  date_now = String(theyear + "/" + themon + "/" + theday + ";" + thehour + ":" + themin + ":" + thesec);
  //Serial.println("Date now: " + date_now);
}

void rtc_run_alarm2()
{
    DateTime now = rtc.now();
    int sampleRateInt = sampleRate.toInt();
    DateTime nextAlarm;

    // Pour les intervalles < 60 minutes
    if (sampleRateInt < 60)
    {
        if (sampleRateInt == 15)
        {
            // Prochain quart d'heure (00, 15, 30, 45)
            int nextMinute = ((now.minute() / 15) + 1) * 15;
            int nextHour = now.hour();
            if (nextMinute >= 60) {
                nextMinute = 0;
                nextHour += 1;
            }
            nextAlarm = DateTime(now.year(), now.month(), now.day(), nextHour, nextMinute, 0);
        }
        else if (sampleRateInt == 30)
        {
            // Prochaine demi-heure (00, 30)
            int nextMinute = ((now.minute() / 30) + 1) * 30;
            int nextHour = now.hour();
            if (nextMinute >= 60) {
                nextMinute = 0;
                nextHour += 1;
            }
            nextAlarm = DateTime(now.year(), now.month(), now.day(), nextHour, nextMinute, 0);
        }
        else
        {
            // Cas général (1, 5, 10 min, etc.)
            if (now.second() != 0)
            {
                nextAlarm = DateTime(now.year(), now.month(), now.day(),
                                     now.hour(), now.minute() + 1, 0);
            }
            else
            {
                nextAlarm = DateTime(now.year(), now.month(), now.day(),
                                     now.hour(), now.minute() + sampleRateInt, 0);
            }
            if (nextAlarm.minute() >= 60)
            {
                nextAlarm = DateTime(nextAlarm.year(), nextAlarm.month(), nextAlarm.day(),
                                     nextAlarm.hour() + 1, nextAlarm.minute() - 60, 0);
            }
        }
    }
    // Pour les intervalles horaires (>= 60 minutes)
    else if (sampleRateInt >= 60 && sampleRateInt < 1440)
    {
        int hours = sampleRateInt / 60;
        if (now.minute() != 0 || now.second() != 0)
        {
            nextAlarm = DateTime(now.year(), now.month(), now.day(),
                                 now.hour() + 1, 0, 0);
            nextAlarm = nextAlarm + TimeSpan(0, hours - 1, 0, 0);
        }
        else
        {
            nextAlarm = now + TimeSpan(0, hours, 0, 0);
        }
    }
    // Pour les intervalles journaliers (24 heures)
    else if (sampleRateInt == 1440)
    {
        if (now.hour() != 0 || now.minute() != 0 || now.second() != 0)
        {
            nextAlarm = DateTime(now.year(), now.month(), now.day() + 1, 0, 0, 0);
        }
        else
        {
            nextAlarm = now + TimeSpan(1, 0, 0, 0);
        }
    }

    String theyear = String(now.year(), DEC);
    String themon = String(now.month(), DEC);
    String theday = String(now.day(), DEC);
    String thehour_alarm = String(nextAlarm.hour(), DEC);
    String themin_alarm = String(nextAlarm.minute(), DEC);
    String thesec_alarm = String(nextAlarm.second(), DEC);

    date_alarm = String(theyear + "/" + themon + "/" + theday + ";" +
                        thehour_alarm + ":" + themin_alarm + ":" + thesec_alarm + ";");

    // Utilise le mode le plus précis pour matcher heure, minute et seconde
    rtc.setAlarm(ALM1_MATCH_DATE, nextAlarm.second(), nextAlarm.minute(), nextAlarm.hour(), nextAlarm.day());
    rtc.alarmInterrupt(1, true);
    delay(100);
}

void rtc_run_alarm2_old() // a tester
{
  DateTime now = rtc.now();
  int sampleRateInt = sampleRate.toInt();
  DateTime nextAlarm;

  // Pour les intervalles journaliers (24 heures)
  if (sampleRateInt == 1440)
  {
    // Si on n'est pas à minuit, aller au prochain minuit
    if (now.hour() != 0 || now.minute() != 0 || now.second() != 0)
    {
      nextAlarm = DateTime(now.year(), now.month(), now.day() + 1, 0, 0, 0);
    }
    else
    {
      // Si on est déjà à minuit, aller au prochain jour
      nextAlarm = now + TimeSpan(1, 0, 0, 0);
    }
  }
  // Pour les intervalles horaires (>= 60 minutes)
  else if (sampleRateInt >= 60)
  {
    int hours = sampleRateInt / 60; // Calculer le nombre d'heures

    // Si on n'est pas au début d'une heure, aller à la prochaine heure
    if (now.minute() != 0 || now.second() != 0)
    {
      nextAlarm = DateTime(now.year(), now.month(), now.day(),
                           now.hour() + 1, 0, 0);
      // Ajouter les heures d'intervalle moins 1 (car on a déjà ajouté 1)
      nextAlarm = nextAlarm + TimeSpan(0, hours - 1, 0, 0);
    }
    else
    {
      // Si on est au début d'une heure, ajouter directement les heures
      nextAlarm = now + TimeSpan(0, hours, 0, 0);
    }
  }
  // Pour les intervalles < 60 minutes
  else
  {
    if (now.second() != 0)
    {
      // Si les secondes ne sont pas à 0, aller à la prochaine minute
      nextAlarm = DateTime(now.year(), now.month(), now.day(),
                           now.hour(), now.minute() + 1, 0);
      // Puis ajouter l'intervalle moins 1 minute
      if (sampleRateInt > 1)
      {
        nextAlarm = nextAlarm + TimeSpan(0, 0, sampleRateInt - 1, 0);
      }
    }
    else
    {
      // Si on est déjà au début d'une minute, ajouter directement l'intervalle
      nextAlarm = now + TimeSpan(0, 0, sampleRateInt, 0);
    }
  }

  String theyear = String(now.year(), DEC);
  String themon = String(now.month(), DEC);
  String theday = String(now.day(), DEC);
  String thehour_alarm = String(nextAlarm.hour(), DEC);
  String themin_alarm = String(nextAlarm.minute(), DEC);
  String thesec_alarm = String(nextAlarm.second(), DEC);

  date_alarm = String(theyear + "/" + themon + "/" + theday + ";" +
                      thehour_alarm + ":" + themin_alarm + ":" + thesec_alarm + ";");

  // Pour debug
 /* Serial.println("Current time: " + String(now.hour()) + ":" +
                 String(now.minute()) + ":" + String(now.second()));
  Serial.println("Next alarm: " + String(nextAlarm.hour()) + ":" +
                 String(nextAlarm.minute()) + ":" + String(nextAlarm.second()));
  Serial.println("Sample rate (min): " + String(sampleRateInt));*/

  rtc.setAlarm(ALM1_MATCH_MINUTES, 0, nextAlarm.minute(), nextAlarm.hour(), 1);
  rtc.alarmInterrupt(1, true);
  delay(100);
}

void rtc_run_alarm()
{
  DateTime now = rtc.now();
  int sampleRateInt = sampleRate.toInt();

  // Calculer la prochaine alarme en arrondissant à la minute suivante
  DateTime nextAlarm;

  if (now.second() == 0)
  {
    // Si on est déjà à 0 secondes, prendre la minute suivante
    nextAlarm = now + TimeSpan(0, 0, sampleRateInt, 0);
  }
  else
  {
    // Sinon, aller à la prochaine minute et mettre les secondes à 0
    nextAlarm = DateTime(now.year(), now.month(), now.day(),
                         now.hour(), now.minute() + sampleRateInt, 0);
  }

  String theyear = String(now.year(), DEC);
  String themon = String(now.month(), DEC);
  String theday = String(now.day(), DEC);
  String thehour_alarm = String(nextAlarm.hour(), DEC);
  String themin_alarm = String(nextAlarm.minute(), DEC);
  String thesec_alarm = String(nextAlarm.second(), DEC);

  date_alarm = String(theyear + "/" + themon + "/" + theday + ";" +
                      thehour_alarm + ":" + themin_alarm + ":" + thesec_alarm + ";");

  // Pour debug
  /*Serial.println("Current time: " + String(now.hour()) + ":" +
                 String(now.minute()) + ":" + String(now.second()));
  Serial.println("Next alarm: " + String(nextAlarm.hour()) + ":" +
                 String(nextAlarm.minute()) + ":" + String(nextAlarm.second()));*/

  rtc.setAlarm(ALM1_MATCH_MINUTES, 0, nextAlarm.minute(), nextAlarm.hour(), 1);
  rtc.alarmInterrupt(1, true);
  delay(100);
}

void reset_alarm() // reset Alarms RTC
{
  // clear any pending alarms
  rtc.armAlarm(1, false);
  rtc.clearAlarm(1);
  rtc.alarmInterrupt(1, false);
  rtc.armAlarm(2, false);
  rtc.clearAlarm(2);
  rtc.alarmInterrupt(2, false);
  delay(100);
  rtc.writeSqwPinMode(DS3231_OFF);
}

void VBATT()
{
  for (int i = 0; i < 5; i++)
  {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction
  }
  Vbatt = 2 * Vbatt / 5 / 1000.0; // attenuation ratio 1/2

  // Calculate battery percentage
  float VbattMax = 4.2; // Maximum voltage of a fully charged 18650 battery
  float VbattMin = 3.2; // Minimum voltage of a discharged 18650 battery
  VbattPercent = ((Vbatt - VbattMin) / (VbattMax - VbattMin)) * 100.0;

  // Clamp the percentage between 0 and 100
  if (VbattPercent > 100.0)
    VbattPercent = 100.0;
  else if (VbattPercent < 0.0)
    VbattPercent = 0.0;

 // Save to config.txt
 JsonDocument doc;
 // Check battery voltage and update sampleRate if necessary
 if (Vbatt < 3.15) {
  sampleRate = "0"; // Stop recording
  doc["sampleRate"] = sampleRate; // Update the JSON document
  File configFile = SPIFFS.open("/config.txt", FILE_WRITE);
  if (configFile)
  {
    serializeJson(doc, configFile);
    configFile.close();
  }
 }
 
}

void SPIFFS_New_File()
{
  // create a new file
  for (unsigned int i = 0; i < 1000; i++)
  {
    filename[4] = i / 100 + '0';
    filename[5] = ((i % 100) / 10) + '0';
    filename[6] = i % 10 + '0';
    if (!SPIFFS.exists(filename))
    {
      // only open a new file if it doesn't exist
      dataF = SPIFFS.open(filename, FILE_WRITE);
      break; // leave the loop!
    }
  }
  dataF = SPIFFS.open(filename, FILE_WRITE);
  // if the file is available, write to it:
  if (dataF)
  { // logger name a rajouter
    dataF.println(loggerName);
    dataF.println("Powered By Fabien NAESSENS - Universite de Bordeaux - I2M");
    dataF.println("Date;Heure;UTC;Pression(mBar);TempPression(*C);TempADT(*C);VBATT(V)");
    dataF.close();
  }
}

void SPIFFS_setup()
{
  //Serial.print(F("Initializing SPIFFS..."));
  if (!SPIFFS.begin(true))
  {
    SD_present = false;
    //Serial.println("initialization failed!");
  }
  else
  {
    // Serial.println(F("SPIFFS initialised... file access enabled..."));
    SD_present = true;
  }
  // create a new file
  // SPIFFS_New_File(); // a voir pour la creation d'un nouveau fichier dans le werveur web
}

void SPIFFS_logger()
{
  dataF = SPIFFS.open(filename, FILE_APPEND);
  dataF.println(format_data);
  dataF.close();
}

/*******  FUNCTIONS  ********/
// Config File
void config_value()
{
  // Load configuration from config.txt
  File configFile = SPIFFS.open("/config.txt", FILE_READ);
  if (configFile)
  {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    if (!error)
    {
      if (doc["loggerName"].is<String>())
      {
        loggerName = doc["loggerName"].as<String>();
      }
      if (doc["sampleRate"].is<String>())
      {
        sampleRate = doc["sampleRate"].as<String>();
      }
      if (doc["UTC_Value"].is<String>())
      {
        UTC_Value = doc["UTC_Value"].as<String>();
      }
    }
    configFile.close();
  }
}
// Header HTML
void append_page_header()
{
  config_value();
  webpage = F("<!DOCTYPE html><html>");
  webpage += F("<head>");
  webpage += F("<title>");
  webpage += loggerName; // Ajoutez le contenu de la variable loggerName
  webpage += F("</title>");
  webpage += F("<meta name='viewport' content='user-scalable=yes,initial-scale=1.0,width=device-width'>");
  webpage += F("<meta charset='UTF-8'>"); // Encodage UTF-8
  webpage += F("<style>");                // From here style:
  webpage += F("body{max-width:65%;margin:0 auto;font-family:arial;font-size:100%;}");
  webpage += F("ul{list-style-type:none;padding:0;border-radius:0em;overflow:hidden;background-color:#0080ff;font-size:1em;}");
  webpage += F("li{float:left;border-radius:0em;border-right:0em solid #bbb;}");
  webpage += F("li a{color:white; display: block;border-radius:0.375em;padding:0.44em 0.44em;text-decoration:none;font-size:100%}");
  webpage += F("li a:hover{background-color:#e86b6b;border-radius:0em;font-size:100%}");
  webpage += F("h1{color:white;border-radius:0em;font-size:1.5em;padding:0.2em 0.2em;background:#0080ff;}");
  webpage += F("h2{color:blue;font-size:0.8em;}");
  webpage += F("h3{font-size:0.8em;}");
  webpage += F("table{font-family:arial,sans-serif;font-size:0.9em;border-collapse:collapse;width:85%;}");
  webpage += F("th,td {border:0.06em solid #dddddd;text-align:left;padding:0.3em;border-bottom:0.06em solid #dddddd;}");
  webpage += F("tr:nth-child(odd) {background-color:#eeeeee;}");
  webpage += F(".rcorners_n {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:20%;color:white;font-size:75%;}");
  webpage += F(".rcorners_m {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:50%;color:white;font-size:75%;}");
  webpage += F(".rcorners_w {border-radius:0.5em;background:#558ED5;padding:0.3em 0.3em;width:70%;color:white;font-size:75%;}");
  webpage += F(".column{float:left;width:50%;height:45%;}");
  webpage += F(".row:after{content:'';display:table;clear:both;}");
  webpage += F("*{box-sizing:border-box;}");
  webpage += F("a{font-size:75%;}");
  webpage += F("p{font-size:75%;}");
  webpage += F("</style></head><body><h1>");
  webpage += loggerName; // Ajoutez le contenu de la variable loggerName
  webpage += F("</h1>");
  webpage += F("<ul>");
  webpage += F("<li><a href='/'>Fichiers</a></li>"); // Menu bar with commands
  webpage += F("<li><a href='/configuration'>Configuration</a></li>");
  webpage += F("<li><a href='/infos'>Info</a></li>");
  webpage += F("</ul>");
}
// Saves repeating many lines of code for HTML page footers
void append_page_footer()
{
  webpage += F("</body></html>");
}
// Upload a file to the SD
void File_Upload()
{
  append_page_header();
  webpage += F("<h3>Informations</h3>");
  webpage += "<table>";
  webpage += "<tr><th>Autonomie Batterie estimée</th><th>Durée</th><th>Fichier</th></tr>";
  webpage += "<tr><td>OFF:</td><td>6ans et 6 mois</td><td>NaN</td></tr>";
  webpage += "<tr><td>1 minute:</td><td>40 jours</td><td>56 jours</td></tr>";
  webpage += "<tr><td>15 minute:</td><td>15 mois</td><td>27 mois</td></tr>";
  webpage += "<tr><td>30 minute:</td><td>2 ans</td><td>4 ans</td></tr>";
  webpage += "<tr><td>1 heure:</td><td>3 ans</td><td>9 ans</td></tr>";
  webpage += "<tr><td>6 heure:</td><td>5 ans et 6 mois</td><td>55 ans</td></tr>";
  webpage += "<tr><td>12 heure:</td><td>6ans</td><td>100 ans</td></tr>";
  webpage += "<tr><td>24 heure:</td><td>6ans et 3 mois</td><td>200 ans</td></tr>";
  webpage += "</table>";
  webpage += "<br>";
  webpage += F("<h3>Spécifications</h3>");
  webpage += "<table>";
  webpage += "<tr><th>Capteur 1 Bar</th><th>MS580301BA01-00</th></tr>";
  webpage += "<tr><td>Pression:</td><td>Min</td><td>Typ</td><td>Max</td><td>Unit</td></tr>";
  webpage += "<tr><td>Range:</td><td>10</td><td></td><td>1300</td><td>mbar</td></tr>";
  webpage += "<tr><td>Range:</td><td>0.1</td><td></td><td>13.26</td><td>metres</td></tr>";
  webpage += "<tr><td>Résolution:</td><td>0.2</td><td></td><td>0.6</td><td>mm</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-1.5</td><td></td><td>1.5</td><td>mbar</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-1.53</td><td></td><td>1.53</td><td>cm</td></tr>";
  webpage += "<tr><th>Capteur Température</th><th>MS580301BA01-00</th></tr>";
  webpage += "<tr><td>Range:</td><td>-40</td><td></td><td>+85</td><td>*C</td></tr>";
  webpage += "<tr><td>Résolution:</td><td></td><td>0.01</td><td></td><td>*C</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-0.8</td><td></td><td>+0.8</td><td>*C</td></tr>";
  webpage += "<tr><th>Capteur Température</th><th>PrécisionADT7410</th></tr>";
  webpage += "<tr><td>Range:</td><td>-55</td><td></td><td>+150</td><td>*C</td></tr>";
  webpage += "<tr><td>Résolution:</td><td></td><td>0.0078</td><td></td><td>*C</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-0.5</td><td></td><td>+0.5</td><td>*C</td></tr>";
  webpage += "<tr><th>RTC</th><th>DS3231SN</th></tr>";
  webpage += "<tr><td>Range:</td><td>-40</td><td></td><td>+70</td><td>*C</td></tr>";
  webpage += "<tr><td>Compensation Temp:</td><td></td><td>oui</td><td></td><td>*C</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-2</td><td></td><td>+2</td><td>ppm</td></tr>";
  webpage += "<tr><td>Précision:</td><td>-5</td><td></td><td>+5</td><td>sec/mois</td></tr>";
  webpage += "</table>";
  webpage += "<br>";
  webpage += "<br>";
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html", webpage);
}

void File_Upload2()
{
  append_page_header();
  webpage += F("<h3>Select File for config.txt to Upload</h3>");
  webpage += "<br>";
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data' onsubmit='return checkFileName()'>");
  webpage += F("<script>");
  webpage += F("document.getElementById('fupload').addEventListener('change', function() {");
  webpage += F("  var fileInput = document.getElementById('fupload');");
  webpage += F("  var fileName = fileInput.value.split('\\\\').pop();");
  webpage += F("  if (fileName !== 'config.txt') {");
  webpage += F("    alert('The file must be named config.txt');");
  webpage += F("    fileInput.value = '';"); // Clear the input
  webpage += F("  }");
  webpage += F("});");
  webpage += F("</script>");
  webpage += F("<input class='buttons' type='file' name='fupload' id = 'fupload' value=''>");
  webpage += "<br><br>";
  webpage += F("<button class='buttons' type='submit'>Upload File</button><br><br>");
  webpage += "<br>";
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html", webpage);
}

void SendHTML_Header()
{
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}

void SendHTML_Content()
{
  server.sendContent(webpage);
  webpage = "";
}

void SendHTML_Stop()
{
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}

// Config PAge Web
void SelectInputFAB() // faire deepSleep for ever
{
  rtc_get_date_and_alarm();
  VBATT();
  config_value();
  append_page_header();
  // webpage += "<h3>Formulaire de saisie</h3>";
  webpage += "<form id='myForm' action='/get'>";
  webpage += "Nom du Logger / Wifi SSID: <input type='text' name='loggerName' value='" + loggerName + "'><br>";
  webpage += "<br>";
  webpage += "Tension Batterie: <input type='text' name='VBAT' value='" + String(Vbatt, 2) +"V - "+ String(VbattPercent,0)+ "%" + "' readonly><br>";
  webpage += "<br>";
  webpage += "<table>";
  webpage += "<tr><td>Capteur de pression:</td><td><input type='checkbox' name='capteur_pression' checked></td></tr>";
  webpage += "<tr><td>Température pression:</td><td><input type='checkbox' name='temperature_pression' checked></td></tr>";
  webpage += "<tr><td>Température précision:</td><td><input type='checkbox' name='temperature_precision' checked></td></tr>";
  webpage += "<tr><td>Tension de batterie:</td><td><input type='checkbox' name='vbatt' checked></td></tr>";
  webpage += "</table>";
  webpage += "<br>";
  webpage += "Sample Rate (min): <select name='sampleRate'>";
  webpage += "<br><br>";
  webpage += "<option value='0'" + String(sampleRate == "0" ? " selected" : "") + ">OFF</option>";
  webpage += "<option value='1'" + String(sampleRate == "1" ? " selected" : "") + ">1 minute</option>";
  webpage += "<option value='15'" + String(sampleRate == "15" ? " selected" : "") + ">15 minutes</option>";
  webpage += "<option value='30'" + String(sampleRate == "30" ? " selected" : "") + ">30 minutes</option>";
  webpage += "<option value='60'" + String(sampleRate == "60" ? " selected" : "") + ">1 heure</option>";
  webpage += "<option value='360'" + String(sampleRate == "360" ? " selected" : "") + ">6 heures</option>";
  webpage += "<option value='720'" + String(sampleRate == "720" ? " selected" : "") + ">12 heures</option>";
  webpage += "<option value='1440'" + String(sampleRate == "1440" ? " selected" : "") + ">24 heures</option>";
  webpage += "</select><br>";
  webpage += "<br>";
  webpage += "Date Heure Logger : " + date_now;
  webpage += "<br><br>";
  webpage += "Mise a jour Heure (RTC): <input type='text' name='rtcupdate' id='rtcupdate'><br>";
  webpage += "UTC: <input type='number' name='UTC_Value' min='-12' max='12' value='" + UTC_Value + "'><br>";
  webpage += "</select><br>";
  webpage += "<input type='submit' value='Envoyer'>";
  webpage += "</form>";
  webpage += "<script>\
              function updateDateTime() {\
                var d = new Date();\
                var year = d.getFullYear();\
                var month = ('0' + (d.getMonth() + 1)).slice(-2);\
                var day = ('0' + d.getDate()).slice(-2);\
                var hours = ('0' + d.getHours()).slice(-2);\
                var minutes = ('0' + d.getMinutes()).slice(-2);\
                var seconds = ('0' + d.getSeconds()).slice(-2);\
                var datetime = year + '-' + month + '-' + day + ' ' + hours + ':' + minutes + ':' + seconds;\
                document.getElementById('rtcupdate').value = datetime;\
              }\
              setInterval(updateDateTime, 500);\
              updateDateTime();\
              </script>";
  server.send(200, "text/html", webpage);
}

// Validation Config Web
void handleFormSubmit()
{
  if (server.hasArg("loggerName"))
  {
    loggerName = server.arg("loggerName");
  }
  if (server.hasArg("sampleRate"))
  {
    sampleRate = server.arg("sampleRate");
  }
  if (server.hasArg("UTC_Value"))
  {
    UTC_Value = server.arg("UTC_Value");
  }
  if (server.hasArg("rtcupdate"))
  {
    String rtcUpdate = server.arg("rtcupdate");
    int year = rtcUpdate.substring(0, 4).toInt();
    int month = rtcUpdate.substring(5, 7).toInt();
    int day = rtcUpdate.substring(8, 10).toInt();
    int hour = rtcUpdate.substring(11, 13).toInt();
    int minute = rtcUpdate.substring(14, 16).toInt();
    int second = rtcUpdate.substring(17, 19).toInt();
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
  }
  // Save to config.txt
  JsonDocument doc;
  doc["wifiSSID"] = loggerName;
  doc["loggerName"] = loggerName;
  doc["sampleRate"] = sampleRate;
  doc["UTC_Value"] = UTC_Value;

  File configFile = SPIFFS.open("/config.txt", FILE_WRITE);
  if (configFile)
  {
    serializeJson(doc, configFile);
    configFile.close();
  }
  // Construire la page HTML avec le message de confirmation
  append_page_header();
  webpage += "<h3>Formulaire de saisie</h3>";
  webpage += "<p>Mise à jour effectuée avec succès.</p>";
  webpage += "<form id='myForm' action='/get' method='post'>";
  // Ajoutez ici les autres champs du formulaire avec leurs valeurs actuelles
  webpage += "<h3><a href='/'>[Retour]</a><br><br></h3>";
  server.send(200, "text/html", webpage);
}

// ReportSDNotPresent
void ReportSDNotPresent()
{
  SendHTML_Header();
  webpage += F("<h3>No SPIFFS present</h3>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

// ReportFileNotPresent
void ReportFileNotPresent(String target)
{
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>");
  webpage += F("<a href='/");
  webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

// ReportCouldNotCreateFile
void ReportCouldNotCreateFile(String target)
{
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>");
  webpage += F("<a href='/");
  webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void SPIFFS_file_delete(String filename)
{
  if (SD_present)
  {
    SendHTML_Header();
    File dataFile = SPIFFS.open("/" + filename, FILE_READ); // Now read data from SD Card
    if (dataFile)
    {
      if (SPIFFS.remove("/" + filename))
      {
        // Serial.println(F("File deleted successfully"));
        webpage += "<h3>File '" + filename + "' has been erased</h3>";
        webpage += F("<a href='/'>[Back]</a><br><br>");
      }
      else
      {
        webpage += F("<h3>File was not deleted - error</h3>");
        webpage += F("<a href='/'>[Back]</a><br><br>");
      }
    }
    else
      ReportFileNotPresent("delete");
    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop();
  }
  else
    ReportSDNotPresent();
}

void printDirectory(const char *dirname, uint8_t levels)
{
  File root = SPIFFS.open(dirname);

  if (!root)
  {
    return;
  }
  if (!root.isDirectory())
  {
    return;
  }
  File file = root.openNextFile();

  int i = 0;
  int totalBytes = 0;
  while (file)
  {
    if (webpage.length() > 1000)
    {
      SendHTML_Content();
    }
    if (file.isDirectory())
    {
      webpage += "<tr><td>" + String(file.isDirectory() ? "Dir" : "File") + "</td><td>" + String(file.name()) + "</td><td></td></tr>";
      printDirectory(file.name(), levels - 1);
    }
    else
    {
      webpage += "<tr><td>" + String(file.name()) + "</td>";
      webpage += "<td>" + String(file.isDirectory() ? "Dir" : "File") + "</td>";
      int bytes = file.size();
      totalBytes += bytes;
      String fsize = "";
      if (bytes < 1024)
        fsize = String(bytes) + " B";
      else if (bytes < (1024 * 1024))
        fsize = String(bytes / 1024.0, 3) + " KB";
      else if (bytes < (1024 * 1024 * 1024))
        fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";
      else
        fsize = String(bytes / 1024.0 / 1024.0 / 1024.0, 3) + " GB";
      webpage += "<td>" + fsize + "</td>";
      webpage += "<td>";
      webpage += F("<FORM action='/' method='post'>");
      webpage += F("<button type='submit' name='download'");
      webpage += F("' value='");
      webpage += "download_" + String(file.name());
      webpage += F("'>Download</button>");
      webpage += "</td>";
      webpage += "<td>";
      webpage += F("<FORM action='/' method='post'>");
      webpage += F("<button type='submit' name='delete'");
      webpage += F("' value='");
      webpage += "delete_" + String(file.name());
      webpage += F("'>Delete</button>");
      webpage += "</td>";
      webpage += "</tr>";
    }
    file = root.openNextFile();
    i++;
  }
  file.close();

  // Calculate total size in MB and free space percentage
  float totalSizeMB = totalBytes / 1024.0 / 1024.0;
  float freeSpacePercentage = ((4.0 - totalSizeMB) / 4.0) * 100;

  // Display total size and free space percentage
  webpage += "<tr><td colspan='3'>Total Size: " + String(totalSizeMB, 3) + " MB</td></tr>";
  webpage += "<tr><td colspan='3'>Free Space: " + String(freeSpacePercentage, 2) + " %</td></tr>";
}

// Upload a new file to the Filing system
void handleFileUpload()
{
  HTTPUpload &uploadfile = server.upload(); // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
                                            // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  if (uploadfile.status == UPLOAD_FILE_START)
  {
    String filename = uploadfile.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    // Serial.print("Upload File Name: ");
    // Serial.println(filename);
    SPIFFS.remove(filename);                        // Remove a previous version, otherwise data is appended the file again
    UploadFile = SPIFFS.open(filename, FILE_WRITE); // Open the file for writing in SD (create it, if doesn't exist)
    filename = String();
  }
  else if (uploadfile.status == UPLOAD_FILE_WRITE)
  {
    if (UploadFile)
      UploadFile.write(uploadfile.buf, uploadfile.currentSize); // Write the received bytes to the file
  }
  else if (uploadfile.status == UPLOAD_FILE_END)
  {
    if (UploadFile) // If the file was successfully created
    {
      UploadFile.close(); // Close the file again
      // Serial.print("Upload Size: ");
      // Serial.println(uploadfile.totalSize);
      webpage = "";
      append_page_header();
      webpage += F("<h3>File was successfully uploaded</h3>");
      webpage += F("<h2>Uploaded File Name: ");
      webpage += uploadfile.filename + "</h2>";
      webpage += F("<h2>File Size: ");
      webpage += file_size(uploadfile.totalSize) + "</h2><br><br>";
      webpage += F("<a href='/'>[Back]</a><br><br>");
      append_page_footer();
      server.send(200, "text/html", webpage);
    }
    else
    {
      ReportCouldNotCreateFile("upload");
    }
  }
}

// Download a file from the SD, it is called in void SD_dir() & creation d'un nouveau fichier de log
void SPIFFS_file_download(String filename)
{
  if (SD_present)
  {
    File download = SPIFFS.open("/" + filename);
    if (download)
    {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
      server.sendHeader("Connection", "close");
      server.streamFile(download, "application/octet-stream");
      download.close();
    }
    else
      ReportFileNotPresent("download");
  }
  else
    ReportSDNotPresent();
}

// Initial page of the server web, list directory and give you the chance of deleting and uploading
void SD_dir()
{
  if (SD_present)
  {
    // Action acording to post, dowload or delete, by MC 2022
    if (server.args() > 0) // Arguments were received, ignored if there are not arguments
    {
      // Serial.println(server.arg(0));

      String Order = server.arg(0);
      // Serial.println(Order);

      if (Order.indexOf("download_") >= 0)
      {
        Order.remove(0, 9);
        SPIFFS_file_download(Order);
        // Serial.println(Order);
      }

      if ((server.arg(0)).indexOf("delete_") >= 0)
      {
        Order.remove(0, 7);
        SPIFFS_file_delete(Order);
        // Serial.println(Order);
      }
    }

    File root = SPIFFS.open("/");
    if (root)
    {
      root.rewindDirectory();
      SendHTML_Header();
      webpage += F("<table align='center'>");
      // webpage += F("<tr><th>Name/Type</th><th style='width:20%'>Type File/Dir</th><th>File Size</th></tr>");
      webpage += F("<tr><th>Name/Type</th><th style='width:20%'>Type</th><th>File Size</th></tr>");
      printDirectory("/", 0);
      webpage += F("</table>");
      SendHTML_Content();
      root.close();
    }
    else
    {
      SendHTML_Header();
      webpage += F("<h3>No Files Found</h3>");
    }
    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop(); // Stop is needed because no content length was sent
  }
  else
    ReportSDNotPresent();
}

/*********  END FUNCTIONS  **********/


float ADT7410_Temp()
{
  float sum = 0;
  for (int i = 0; i < 5; i++)
  {
    sum += tempsensor.readTempC();
    delay(100); // Attendre 100ms entre chaque mesure
  }
  return sum / 5; // Retourner la moyenne des 10 mesures
}

void MS5803_Pressure()
{
  // Use readSensor() function to get pressure and temperature reading.
  sensor.readSensor();
  /*float pressionAbsolue_mbar = sensor.pressure(); // pression mesurée en mbar
  // Correction de la pression atmosphérique
  float pressionEau_mbar = pressionAbsolue_mbar - pressionAtmo_mbar;
  // Conversion pression -> hauteur d'eau (1 mbar ≈ 1.02 cm d'eau)
  float hauteurEau_cm = pressionAbsolue_mbar * 1.02;
  // Calcul du niveau d'eau par rapport au repère zéro (zéro hydrométrique)
  float niveauParRapportZero_cm = hauteurEau_cm - profondeurCapteur_cm;*/

  /*Serial.print("Pressure = ");
  Serial.print(sensor.pressure(),3);
  Serial.println(" mbar");

  // Show temperature
  Serial.print("Temperature = ");
  Serial.print(sensor.temperature());
  Serial.println("C");*/

  delay(200); // For readability
}

void Compil_data()
{
  MS5803_Pressure(),
  VBATT();
  format_data = String(date_now) + ";" + String(UTC_Value) + ";" + String(sensor.pressure(), 3) + ";" + String(sensor.temperature() - 0.55, 2) + ";" + String(ADT7410_Temp(), 3) + ";" + String(Vbatt, 1);
 // Serial.println(format_data);
}

void wifi_Server_Setup()
{
  // Load configuration from config.txt
  File configFile = SPIFFS.open("/config.txt", FILE_READ);
  if (configFile)
  {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    if (!error)
    {
      if (doc["wifiSSID"].is<String>())
      {
        wifiSSID = doc["wifiSSID"].as<String>();
        WiFi.softAP(wifiSSID.c_str(), passphrase, 6, 0, 2); // channel 6, hidden SSID (0 = not hidden), max connections 2
      }
      else
      {
        WiFi.softAP(loggerName, passphrase, 6, 0, 2); // Default network and password
      }
    }
    else
    {
      WiFi.softAP(loggerName, passphrase, 6, 0, 2); // Default network and password
    }
    configFile.close();
  }
  else
  {
    WiFi.softAP(loggerName, passphrase, 6, 0, 2); // Default network and password
  }

  // Set additional WiFi parameters Set the IP address of the server
  IPAddress local_IP(10, 10, 1, 1);
  IPAddress gateway(10, 10, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // http://Servername.local/
  if (!MDNS.begin(servername))
  {
    //Serial.println(F("Error setting up MDNS responder!"));
    ESP.restart();
  }

  /*********  Server Commands  **********/
  server.on("/", SD_dir);
  server.on("/configuration", SelectInputFAB);
  server.on("/get", handleFormSubmit); // config validation
  server.on("/infos", File_Upload);
  // server.on("/upload", File_Upload);
  server.on("/fupload", HTTP_POST, []()
            { server.send(200); }, handleFileUpload);

  // Configuration du serveur web
  server.on("/toto", HTTP_GET, []()
            {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "Hello toto!"); });

  ESP2SOTA.begin(&server);
  server.begin();
}

void setup()
{
  delay(3000); // avirer (100)
  //Serial.begin(115200);
 // Serial.println("setup");0000
  // Retrieve filename from RTC memory
  if (filename[0] == '\0')
  {
    strcpy(filename, "/LOG000.TXT");
  }

  Wire.begin();
  SPIFFS_setup();
  pinMode(REED_Switch_WakeUP, INPUT_PULLUP); // Webserveur REED ON
  pinMode(RTC_WakeUP, INPUT_PULLUP);         // RTC interrupt pin
  pinMode(A0, INPUT);                        // ADC BATT
  pinMode(GPIO_ALIM, OUTPUT);                // Alimentation RTC + Capteru par GPIO
  digitalWrite(GPIO_ALIM, HIGH);
  rtc_setup();
  config_value();
  rtc_run_alarm2();
  OperationFlag = false;
  if (!tempsensor.begin())
  {
    // Serial.println("Couldn't find ADT7410!");
    while (1)
      ;
  }
  tempsensor.setResolution(ADT7410_16BIT);
  delay(100);
  // set sensor.initializeMS_5803(false).
  if (sensor.initializeMS_5803())
  {
    //Serial.println("MS5803 CRC check OK.");
  }
  else
  {
    //Serial.println("MS5803 CRC check FAILED!");
  }
  delay(50);

  // Configurer le REED SWITCH comme source d'interruption pour réveiller la carte
  esp_deep_sleep_enable_gpio_wakeup(BIT(D1), ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_enable_gpio_wakeup(BIT(D2), ESP_GPIO_WAKEUP_GPIO_LOW); // RTC
  delay(100);
}

void loop()
{
  OperationFlag = true; // Mode Acquistion
  bool currentButtonState = digitalRead(REED_Switch_WakeUP);
  // Mode Webserveur
  if (currentButtonState == LOW && !serverActive)
  {
    wifi_Server_Setup();
    serverActive = true;
    serverStartTime = millis();
  }
  else if (currentButtonState == HIGH && OperationFlag && !serverActive)
  {
    // DATA LOGGING... "
    if (sampleRate != "0")
    {
      rtc_get_date_and_alarm();
      reset_alarm();
      rtc_run_alarm2();
      Compil_data();
      SPIFFS_logger();
    }
    // Go to sleep now
    digitalWrite(GPIO_ALIM, LOW);
    delay(50);
    esp_deep_sleep_start();
    OperationFlag = false;
  }

  if (serverActive)
  {
    server.handleClient(); // Listen for client connections

    if (millis() - serverStartTime > serverTimeout && WiFi.softAPgetStationNum() == 0)
    {
      if (sampleRate != "0")
      {
        rtc_get_date_and_alarm(); // création new mesure après connexion Webserveur
        reset_alarm();
        rtc_run_alarm2();
        Compil_data();
        SPIFFS_New_File();
        SPIFFS_logger(); // End
      }
      server.stop();
      WiFi.softAPdisconnect(true);
      serverActive = false;
      //Serial.println("HTTP server stopped");
      // Go to sleep now
      digitalWrite(GPIO_ALIM, LOW); // Si deepsleep
      
      delay(50);
      esp_deep_sleep_start();
    }
    else if (WiFi.softAPgetStationNum() > 0)
    {
      serverStartTime = millis(); // Reset the timer if there are active connections
    }
  }
  previousButtonState = currentButtonState;
  delay(50);

}
