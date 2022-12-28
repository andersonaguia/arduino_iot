#include <ArduinoHttpClient.h>
#include <Ethernet.h>
#include <avr/wdt.h>
#include <LiquidCrystal_I2C.h>

#define led 13
#define piscina 29
#define fonte 30
#define borda 31

#define statusPiscina 36
#define statusFonte 37
#define statusBorda 38

char serverAddress[] = "172.31.210.70"; // server address
int port = 3008;

byte mac[] = {0xFE, 0xA2, 0xDA, 0xAE, 0xFF, 0xF0};
IPAddress ip(172, 31, 210, 29);

EthernetClient client;
HttpClient clientHttp = HttpClient(client, serverAddress, port);

boolean alarmMessageSent = false;
boolean currentAlarmPiscina = false;
boolean currentAlarmFonte = false;
boolean currentAlarmBorda = false;
boolean previousAlarmPiscina = false;
boolean previousAlarmFonte = false;
boolean previousAlarmBorda = false;

boolean lastPiscinaState = false;
boolean lastFonteState = false;
boolean lastBordaState = false;

unsigned long lastDebounceTimePiscina = 0;
unsigned long lastDebounceTimeFonte = 0;
unsigned long lastDebounceTimeBorda = 0;
unsigned long lastTimeRequest = 0;
unsigned long debounceDelay = 5000;

String cmdPumps = "";
String previousStatePumps = "";

int tryAgain = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup()
{
  Serial.begin(115200);
  lcd.init();
  lcd.setBacklight(HIGH);

  Serial.print(F("Iniciando Setup"));

  clearLCD();
  lcd.print("Starting...");

  pinMode(led, OUTPUT);
  pinMode(piscina, OUTPUT);
  pinMode(fonte, OUTPUT);
  pinMode(borda, OUTPUT);
  pinMode(statusPiscina, INPUT_PULLUP);
  pinMode(statusFonte, INPUT_PULLUP);
  pinMode(statusBorda, INPUT_PULLUP);

  digitalWrite(piscina, true);
  digitalWrite(fonte, true);
  digitalWrite(borda, true);

  clearLCD();
  lcd.print("Config. Network");

  Ethernet.begin(mac, ip);

  Serial.print(F("Conectado! IP: "));
  Serial.println(Ethernet.localIP());

  for (int i = 0; i < 20; i++)
  {
    delay(500);
    lcd.print(".");
    Serial.print(F("."));
  }
  clearLCD();
  lcd.print("Connected!");
  lcd.setCursor(0,1); 

  clearLCD();

  lcd.print("System is run");

  lastTimeRequest = millis();
}

void loop()
{
  imAlive();

  if (millis() - lastTimeRequest > debounceDelay)
  {
    getRequest();
    lastTimeRequest = millis();
  }

  changeStatusPumps();

  //checkAlarms();

  if (previousStatePumps != cmdPumps)
  {
    statusRequest();
  }

  if (tryAgain > 50)
  {
    wdt_enable(WDTO_2S);
    delay(15000);
  }
}

void alarmRequest(String device, String message, boolean activated)
{
  Serial.println("Making POST request");
  String contentType = "application/x-www-form-urlencoded";
  String postData = "activated=";

  postData.concat(activated);
  postData.concat("&device=");
  postData.concat(device);
  postData.concat("&message=");
  postData.concat(message);

  wdt_enable(WDTO_8S);
  clientHttp.post("/system_alarms", contentType, postData);
  wdt_reset();
  // read the status code and body of the response
  int statusCode = clientHttp.responseStatusCode();
  wdt_reset();
  String response = clientHttp.responseBody();
  wdt_reset();
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  if (statusCode > 0)
  {
    wdt_disable();
    switch (statusCode)
    {
      yield();
    case 201:
      alarmMessageSent = true;
      break;

    case 404:
      Serial.println("Site nao encontrado!");
      alarmMessageSent = false;
      break;

    default:
      alarmMessageSent = false;
      break;
    }
  }
  else
  {
    wdt_disable();
    Serial.println("Fail Request");
  }
}

void statusRequest()
{
  Serial.println("Making POST request");
  String contentType = "application/x-www-form-urlencoded";
  String postData = "piscina=";
  postData.concat(cmdPumps[0]);
  postData.concat("&fonte=");
  postData.concat(cmdPumps[1]);
  postData.concat("&borda=");
  postData.concat(cmdPumps[2]);

  wdt_enable(WDTO_8S);
  clientHttp.post("/pumps", contentType, postData);
  wdt_reset();
  // read the status code and body of the response
  int statusCode = clientHttp.responseStatusCode();
  wdt_reset();
  String response = clientHttp.responseBody();
  wdt_reset();
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  if (statusCode > 0)
  {
    wdt_disable();
    switch (statusCode)
    {
      yield();
    case 201:
      previousStatePumps = cmdPumps;
      break;

    case 404:
      Serial.println("Site nao encontrado!");
      break;

    default:
      break;
    }
  }
  else
  {
    wdt_disable();
    Serial.println("Fail Request");
  }
}

void getRequest()
{
  cmdPumps = "";
  Serial.println("making GET request");
  wdt_enable(WDTO_8S);
  clientHttp.get("/pumps");
  wdt_reset();
  int statusCode = clientHttp.responseStatusCode();
  wdt_reset();
  String response = clientHttp.responseBody();
  wdt_reset();
  Serial.print("Status code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
  Serial.println("Wait five seconds");

  if (statusCode > 0)
  {
    wdt_disable();
    switch (statusCode)
    {
      yield();
    case 200:
      tryAgain = 0;
      cmdPumps = response;
      checkState();
      break;

    case 404:
      Serial.println("Site nao encontrado!");
      tryAgain++;
      break;

    default:
      tryAgain++;
      break;
    }
  }
  else
  {
    wdt_disable();
    Serial.println("Fail Request");
    tryAgain++;
  }
  Serial.println("Waiting for next read");
  delay(5000);
}

void checkAlarms()
{
  Serial.println("Alarm checking...");

  boolean activatedPiscina = false;
  boolean activatedFonte = false;
  boolean activatedBorda = false;

  boolean readingPiscina = digitalRead(statusPiscina);
  boolean readingFonte = digitalRead(statusFonte);
  boolean readingBorda = digitalRead(statusBorda);

  String device;
  String message;

  Serial.print("Bomba Piscina: ");

  if (readingPiscina != lastPiscinaState)
  {
    lastDebounceTimePiscina = millis();
  }

  if (millis() - lastDebounceTimePiscina > debounceDelay)
  {
    if (readingPiscina)
    {
      Serial.println("NORMAL");
      activatedPiscina = false;
      currentAlarmPiscina = false;
      device = "Bomba Piscina";
    }
    else
    {
      Serial.println("ALARME ATIVADO");
      activatedPiscina = true;
      currentAlarmPiscina = true;
      device = "Bomba Piscina";
      message = "A bomba da piscina parou de funcionar.";
      cmdPumps[0] = 'F';
    }
    if (sendAlarmMessage(currentAlarmPiscina, previousAlarmPiscina, device, message, activatedPiscina))
    {
      lastPiscinaState = readingPiscina;
    }
  }

  Serial.print("Bomba Fonte: ");

  if (readingFonte != lastFonteState)
  {
    lastDebounceTimeFonte = millis();
  }

  if (millis() - lastDebounceTimeFonte > debounceDelay)
  {
    if (readingFonte)
    {
      Serial.println("NORMAL");
      activatedFonte = false;
      currentAlarmFonte = false;
      device = "Bomba Fonte";
    }
    else
    {
      Serial.println("ALARME ATIVADO");
      activatedFonte = true;
      currentAlarmFonte = true;
      device = "Bomba Fonte";
      message = "A bomba da fonte parou de funcionar.";
      cmdPumps[1] = 'F';
    }
    if (sendAlarmMessage(currentAlarmFonte, previousAlarmFonte, device, message, activatedFonte))
    {
      lastFonteState = readingFonte;
    }
  }

  Serial.print("Bomba Borda Infinita: ");

  if (readingBorda != lastBordaState)
  {
    lastDebounceTimeBorda = millis();
  }

  if (millis() - lastDebounceTimeBorda > debounceDelay)
  {
    if (readingBorda)
    {
      Serial.println("NORMAL");
      activatedBorda = false;
      currentAlarmBorda = false;
      device = "Bomba Borda Infinita";
    }
    else
    {
      Serial.println("ALARME ATIVADO");
      activatedBorda = true;
      currentAlarmBorda = true;
      device = "Bomba Borda Infinita";
      message = "A bomba da borda infinita parou de funcionar.";
      cmdPumps[2] = 'F';
    }
    if (sendAlarmMessage(currentAlarmBorda, previousAlarmBorda, device, message, activatedBorda))
    {
      lastBordaState = readingBorda;
    }
  }
}

boolean sendAlarmMessage(boolean currentAlarm, boolean previousAlarm, String device, String message, boolean activated)
{
  if (currentAlarm != previousAlarm)
  {
    alarmRequest(device, message, activated);
    if (alarmMessageSent == true)
    {
      previousAlarmPiscina = currentAlarmPiscina;
      previousAlarmFonte = currentAlarmFonte;
      previousAlarmBorda = currentAlarmBorda;
      alarmMessageSent = false;
      return true;
    }
    return false;
  }
}

void imAlive()
{
  clearLCD();
  lcd.print("Running...");
}

void checkState()
{ 
  /* 
  Serial.print("piscina: ");
  Serial.println(cmdPumps[0]);
  Serial.print("fonte: ");
  Serial.println(cmdPumps[1]);
  Serial.print("borda: ");
  Serial.println(cmdPumps[2]);
  */
  clearLCD();
  lcd.print("RL 1: ");
  lcd.print(cmdPumps[0]);
  lcd.print(" RL 2: ");
  lcd.print(cmdPumps[1]);
  lcd.setCursor(0, 1);
  lcd.print("RL 3: ");
  lcd.print(cmdPumps[2]);
  lcd.print("RL 4: ");
  lcd.print(cmdPumps[3]);
}

void changeStatusPumps()
{
  digitalWrite(piscina, !(cmdPumps[0] == 'L'));
  digitalWrite(fonte, !(cmdPumps[1] == 'L'));
  digitalWrite(borda, !(cmdPumps[2] == 'L'));
}

void clearLCD(){
  lcd.clear();
  lcd.setCursor(0, 0);
}