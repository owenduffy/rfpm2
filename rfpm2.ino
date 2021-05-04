
//AD8307 power meter for ESP8266 - NodeMCU 1.0 (ESP-12E Module)
//Copyright: Owen Duffy 2018/04/07
//I2C LCD

#define RESULTL 50
#define PAGEBUFRESSIZE 3000
extern "C" {
#include "user_interface.h"
}
#include <LittleFS.h>
#include <TimeLib.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <LcdBarGraphX.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include <DNSServer.h>

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#endif
#include <PageBuilder.h>
#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer  server;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer  server;
#endif
#include <WiFiManager.h>
#define ARDUINOJSON_USE_DOUBLE 1
#include <ArduinoJson.h>
#define LCDSTEPS 48

#define LCDTYPE 1
const char ver[]="0.02";
char hostname[11]="rfpm2";
WiFiManager wifiManager;
int t=0;
byte lcdNumCols=16;
int sensorPin=A0; // select the input pin for the AD8307
long unsigned AdcAccumulator; // variable to accumulate the value coming from the sensor
float vin;
float rt;
float vref=3.3;
float intercept=-84;
float slope=0.1;
int avg=3;
float lcdmin,lcdmax,lcdslope;
char unit[9]="";
char name[21],configfilename[32];
float db;
char result1[RESULTL][21]; //timestamp array
float result2[RESULTL]; //db array
int resulti,resultn;
int i,j,ticks,interval;
bool tick1Occured,timeset;
const int timeZone=0;
static const char ntpServerName[]="pool.ntp.org";

#if LCDTYPE == 1
LiquidCrystal_I2C lcd(0x20,4,5,6,0,1,2,3);  //set the LCD I2C address and pins
#endif
#if LCDTYPE == 2
LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7);  //set the LCD I2C address and pins
#endif
LcdBarGraphX lbg(&lcd,lcdNumCols);
WiFiUDP udp;
String header; //HTTP request
unsigned int localPort=8888; //local port to listen for UDP packets
Ticker ticker1;
PageElement  elm;
PageBuilder  page;
String currentUri;
char ts[21],ts2[7];

//----------------------------------------------------------------------------------
void cbtick1(){
  if(ticks)
    ticks--;
  else{
    ticks=interval-1;
    tick1Occured=true;
  }
}
//----------------------------------------------------------------------------------
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while(udp.parsePacket() > 0); //discard any previously received packets
  Serial.println(F("Transmit NTP Request"));
  // get a random server from the pool
  WiFi.hostByName(ntpServerName,ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(F(": "));
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait=millis();
  while (millis()-beginWait<1500) {
    int size=udp.parsePacket();
    if(size>=NTP_PACKET_SIZE){
      Serial.println(F("Received NTP response"));
      udp.read(packetBuffer,NTP_PACKET_SIZE); //read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900=(unsigned long)packetBuffer[40]<<24;
      secsSince1900|=(unsigned long)packetBuffer[41]<<16;
      secsSince1900|=(unsigned long)packetBuffer[42]<<8;
      secsSince1900|=(unsigned long)packetBuffer[43];
      return secsSince1900-2208988800UL+timeZone*SECS_PER_HOUR;
    }
  }
  Serial.println(F("No NTP response"));
  return 0; //return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer,0,NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0]=0b11100011; //LI, Version, Mode
  packetBuffer[1]=0; //Stratum, or type of clock
  packetBuffer[2]=6; //Polling Interval
  packetBuffer[3]=0xEC; //Peer Clock Precision
  //8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]=49;
  packetBuffer[13]=0x4E;
  packetBuffer[14]=49;
  packetBuffer[15]=52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address,123); //NTP requests are to port 123
  udp.write(packetBuffer,NTP_PACKET_SIZE);
  udp.endPacket();
}
//----------------------------------------------------------------------------------
void lcdrst(){
  lcd.clear(); //clear lcd screen
  lbg.begin(); //restart LCD bar graph
  ticks=interval-1;
  tick1Occured=true;
}
//----------------------------------------------------------------------------------
int config(const char* cfgfile){
//  StaticJsonDocument<2000> doc; //on stack  arduinojson.org/assistant
  DynamicJsonDocument doc(1024);//arduinojson.org/assistant
  Serial.println(F("config file"));
  Serial.println(cfgfile);
  if (LittleFS.exists(cfgfile)){
    //file exists, reading and loading
    lcd.clear();
    lcd.print(F("Loading config:       "));
    lcd.setCursor(0,1);
    lcd.print(cfgfile);
    Serial.println(F("Reading config file"));
    delay(1000);
    File configFile=LittleFS.open(cfgfile,"r");
    if (configFile){
      resulti=0;
      resultn=0;
      Serial.println(F("Opened config file"));
      size_t size=configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(),size);
      DeserializationError error = deserializeJson(doc, buf.get());
      if (error) {
          Serial.println(F("Failed to load JSON config"));
          lcd.clear();
          lcd.print(F("Error: cfg.json"));
          while(1);
      }
      JsonObject json = doc.as<JsonObject>();
      Serial.println(F("\nParsed json"));
      strncpy(hostname,json[F("hostname")],11);
      hostname[10]='\0';
      Serial.println(hostname);
      lcdmin=json[F("lcdmin")];
      lcdmax=json[F("lcdmax")];
      lcdslope=LCDSTEPS/(lcdmax-lcdmin);
      vref=json[F("vref")];
      interval=1; //default
      vref=json[F("interval")];
      slope=json[F("slope")];
      intercept=json[F("intercept")];
      avg=json[F("avg")];
      strncpy(unit,json[F("unit")],sizeof(unit));
      strncpy(name,json[F("name")],sizeof(name));
      unit[sizeof(unit)-1]='\0';
      Serial.print(F("Slope: "));
      Serial.print(slope,5);
      Serial.print(F(", Intercept: "));
      Serial.println(intercept,5);
      lcdrst();
      return 0;
    }
  }
  lcdrst();
  return 1;
}
//----------------------------------------------------------------------------------
String rootPage(PageArgument& args) {
  String buf((char *)0);
  char line[300];
  buf.reserve(PAGEBUFRESSIZE);
  sprintf(line,"<h3><a href=\"/config\">Configuration</a>: %s</h3>\n",name);
  buf=line;
  buf+=F("<h3><a href=\"/wifi\">WiFi OFF</a></h3>\n");
  sprintf(line,"<p>Time: %s Value: %0.1f %s\n<pre>\n",name,ts,db,unit);
  buf+=line;
  i=resultn<RESULTL?0:resulti;
  for(j=-resultn+1;j<=0;j++){
    sprintf(line,"%s,%0.1f\n",result1[i],result2[i]);
    buf+=line;
    if(++i==RESULTL){i=0;}
  }
  buf+=F("</pre>");
  return buf;
}
//----------------------------------------------------------------------------------
String cfgPage(PageArgument& args) {
  String buf((char *)0);
  String filename((char *)0);
  char line[200];

  buf.reserve(PAGEBUFRESSIZE);
  if (args.hasArg(F("filename"))){
    File mruFile=LittleFS.open("/mru.txt","w");
    if(mruFile){
      mruFile.print(args.arg(F("filename")).c_str());
      mruFile.close();
      Serial.print(F("wrote: "));
      Serial.println(args.arg(F("filename")).c_str());
    }
    if(!config(args.arg(F("filename")).c_str())) buf+=F("<p>Done...");
    else buf+=F("<p>Config failed...");
  }
  else{
    Dir dir = LittleFS.openDir(F("/"));
    buf=F("<h3>Click on desired configuration file:</h3>");
    while (dir.next()){
      if (dir.isFile()){
        filename=dir.fileName();
        if (filename.endsWith(F(".cfg"))){
          Serial.println(filename);
          sprintf(line,"<p><a href=\"/config?filename=%s\">%s</a>\n",filename.c_str(),filename.c_str());
          buf+=line;
        }
      }
    }
  }
  return buf;
}  
//----------------------------------------------------------------------------------
String wifiPage(PageArgument& args) {
  Serial.println(F("WiFi OFF"));
  WiFi.mode(WIFI_OFF);
  return F("");
}
//----------------------------------------------------------------------------------
// This function creates dynamic web page by each request.
// It is called twice at one time URI request that caused by the structure
// of ESP8266WebServer class.
bool handleAcs(HTTPMethod method, String uri) {
  if (uri==currentUri){
    // Page is already prepared.
    return true;
  }
  else{
    currentUri=uri;
    page.clearElement();          // Discards the remains of PageElement.
    page.addElement(elm);         // Register PageElement for current access.

    Serial.println("Request:" + uri);

    if(uri=="/"){
      page.setUri(uri.c_str());
      elm.setMold(PSTR(
        "<html>"
        "<body>"
        "<h1><a href=/>RF power meter 2 (RFPM2)</a></h1>"
        "{{ROOT}}"
        "</body>"
        "</html>"));
      elm.addToken("ROOT", rootPage);
      return true;
    }
    else if(uri=="/config"){
      page.setUri(uri.c_str());
      elm.setMold(PSTR(
        "<html>"
        "<body>"
        "<h1><a href=/>RF power meter 2 (RFPM2)</a></h1>"
        "<h2>RFPM2 configuration</h2>"
        "{{CONFIG}}"
        "</body>"
        "</html>"));
      elm.addToken("CONFIG",cfgPage);
      return true;
    }
    else if(uri=="/wifi"){
      page.setUri(uri.c_str());
      elm.setMold(PSTR(
        "<html>"
        "<body>"
        "<h1><a href=/>RF power meter 2 (RFPM2)</a></h1>"
        "<h2>wifi configuration</h2>"
        "{{WIFI}}"
        "</body>"
        "</html>"));
      elm.addToken("WIFI",wifiPage);
      return true;
    }
    else{
      return false;    // Not found to accessing exception URI.
    }
  }
}
//----------------------------------------------------------------------------------
void setup(){
  WiFi.mode(WIFI_OFF);
  //WiFi.setOutputPower 0-20.5 dBm in 0.25 increments
  //WiFi.setOutputPower(0); //min power for ADC noise reduction
  lcd.begin(16,2);
  lcd.clear();
  lcd.print(F("rfpm2 v"));
  lcd.print(ver);
  lcd.setCursor(0,1);
  lcd.print(F("Initialising..."));
  Serial.begin(9600);
  while (!Serial){;} // wait for serial port to connect. Needed for Leonardo only
  Serial.print(F("\nSketch size: "));
  Serial.print(ESP.getSketchSize());
  Serial.print(F("\nFree size: "));
  Serial.print(ESP.getFreeSketchSpace());
  Serial.print(F("\n\n"));
    
  if (LittleFS.begin()){
    Serial.println(F("Mounted file system"));
    strcpy(configfilename,"/default.cfg");
    File mruFile=LittleFS.open("/mru.txt","r");
    if(mruFile){
      size_t mrusize=mruFile.size();
      std::unique_ptr<char[]> buf(new char[mrusize]);
      mruFile.readBytes(buf.get(),mrusize);
      mruFile.close();
      strncpy(configfilename,buf.get(),mrusize);
      configfilename[mrusize]='\0';
    }
    config(configfilename);
  }
  else{
    Serial.println(F("Failed to mount FS"));
    lcd.clear();
    lcd.print(F("Failed to mount FS"));
    while(1);
  }
  lcd.clear();
  lcd.print(F("Auto WiFi..."));
  WiFi.hostname(hostname);
  wifiManager.setDebugOutput(true);
  wifiManager.setHostname(hostname);
  wifiManager.setConfigPortalTimeout(120);
  Serial.println(F("Connecting..."));
  Serial.print(WiFi.hostname());
  Serial.print(F(" connecting to "));
  Serial.println(WiFi.SSID());
  wifiManager.autoConnect("rfpmcfg");
  if(WiFi.status()==WL_CONNECTED){
    lcd.clear();
    lcd.print(F("Host: "));
    lcd.print(WiFi.hostname());
  //  lcd.print(F("Connecting..."));
    lcd.setCursor(0,1);
  //  lcd.print(F("IP: "));
    lcd.print(WiFi.localIP().toString().c_str());
    Serial.println(WiFi.localIP().toString().c_str());
  //  lcd.print(F(" connecting to "));
  //  lcd.print(WiFi.SSID());
    delay(2000);
  
    // Prepare dynamic web page
    page.exitCanHandle(handleAcs);    // Handles for all requests.
    page.insert(server);
  
    // Print local IP address and start web server
    Serial.println(F(""));
    Serial.println(F("WiFi connected."));
    Serial.println(F("IP address: "));
    Serial.println(WiFi.localIP());
    Serial.println(F("Hostname: "));
    Serial.println(WiFi.hostname());
    server.begin();

    Serial.println(F("Starting UDP"));
    udp.begin(localPort);
    Serial.print(F("Local port: "));
    Serial.println(udp.localPort());
    Serial.println(F("waiting for sync"));
    setSyncProvider(getNtpTime);
    timeset=timeStatus()==timeSet;
    setSyncInterval(36000);
  }
  ticker1.attach(1,cbtick1);
  lcdrst();
  Serial.print(F("DateTime, "));
  Serial.println(unit);
}

void loop(){
  long prevmillis;

  prevmillis=-millis();
  if (tick1Occured == true){
    tick1Occured = false;
    t=now();
    sprintf(ts,"%04d-%02d-%02dT%02d:%02d:%02dZ",year(t),month(t),day(t),hour(t),minute(t),second(t));
    sprintf(ts2,"%02d%02d%02d",hour(t),minute(t),second(t));
    AdcAccumulator=0;
    for(i=avg;i--;){
      //read the value from the AD8307:
      AdcAccumulator+=analogRead(sensorPin);
//      AdcAccumulator+=random(0,1023);
      delay(200);
      }
    db=intercept+AdcAccumulator*slope/avg;
    //write circular buffer
    result2[resulti]=db;
    strcpy(result1[resulti],ts);
    if(++resulti==RESULTL){resulti=0;}
    if(resultn<RESULTL){resultn++;}
    Serial.print(ts);
    Serial.print(F(","));
    Serial.println(db,1);
    // Print a message to the LCD.
    lbg.drawValue((db-lcdmin)*lcdslope,LCDSTEPS);
    lcd.setCursor(0,1);
    lcd.print(ts2);
    lcd.print(F(" "));
    lcd.print(db,1);
    lcd.print(F(" dB      "));
  }
  
  server.handleClient();
  prevmillis+=millis();
//    if(prevmillis>20){Serial.print("dur :"); Serial.println(prevmillis);}

}
