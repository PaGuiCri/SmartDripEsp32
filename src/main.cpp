#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <SimpleDHT.h>
#include <NTPClient.h>
#include <iostream>
#include <string>
#include <Arduinojson.h> 
#include <ESP32Time.h>
#include <Preferences.h>
#include <ESP_Mail_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

/* Red WiFi */
#define SSID "MiFibra-21E0_EXT"
#define PASS "2SSxDxcYNh"
void InitWiFi();

/* Email */
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465 
#define AUTHOR_EMAIL "falder24@gmail.com"
#define AUTHOR_PASSWORD "kcjbfgngmfgkcxtw"
SMTPSession smtp;
void smtpCallback(SMTP_Status status);
void mailSmartDripOn();
ESP_Mail_Session session;
SMTP_Message mailStartSDS;
SMTP_Message mailDripOn;
SMTP_Message mailErrorValve;
SMTP_Message mailErrorFlowSensor;
SMTP_Message mailErrorDHT;
SMTP_Message mailErrorHigro;
bool mailIni, mailRiego, mailErrV, mailErrFS, mailErrDht, mailErrHig;

/* Firebase */
#define DB_URL "https://terrazaiot-default-rtdb.europe-west1.firebasedatabase.app/" 
#define API_KEY "AIzaSyAGU4eDmNZntr_sB0dTmnL90M1dytiq37g"                 
#define USER_EMAIL "falder24@hotmail.com"
#define USER_PASS "85090405pGc"
FirebaseData myFirebaseData;                                                   
FirebaseAuth auth;
FirebaseConfig config;
String myJsonStr;
String datosEmail; 
String uID;                                    
FirebaseJson Medidas, DHT11, DatosRiego, limitesRiego, horarioRiego;
unsigned long TiempoFirebase = 0;
#define DelayFirebase 100

/* Timers */
volatile bool toggle = true;
void IRAM_ATTR onTimer1();
hw_timer_t *timer1 = NULL;
void IRAM_ATTR onTimer2();
hw_timer_t *timer2 = NULL;

/* Define NTP Client to get time */
ESP32Time rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

/* Variables to save date and time */
String Hora;
String formattedTime;
String dayStamp;
String timeStamp;
String LimiteHoraInicio;
String LimiteHoraFin;
int HoraInicioCheck, HoraFinCheck;

/* Sincronizacion del RTC con NTP */
const char* ntpServer = "pool.ntp.org";
const long gmtOFFset_sec = 3600;
const int daylightOffset_sec = 3600;

/* Configuracion de terminales para Higrometro y DHT11  */
#define PinHigro 13  // Nueva configuración de pines antes 34
#define PinDHT 35     // Nueva configuración de pines antes 4
float Temperatura = 0, Humedad = 0;
SimpleDHT11 DHT(PinDHT);
unsigned long TiempoDHT = 0;
#define SampleDHT 1200
int PorcentajeHumedad = 40;
int contador;
const int dry = 445;
const int wet = 32;
int TiempoRIEGO;
int HumedadRiego;
int LimiteRiego;
int LimiteHumedad;  

/* Instancia para almacenar datos de riego en memoria flash */
Preferences preferences;

#define pinLed 2
#define valvulaRiegoVin1 27 // Nueva configuración de pines antes 32. Salida Electroválvula 1
#define valvulaRiegoGND1 26 // Nueva configuración de pines antes 25. Salida Electroválvula 1
#define valvulaRiegoVin2 25
#define valvulaRiegoGND2 33
#define sensorFlujo 34     // Nueva configuración de pines antes 20

unsigned long sendDataPrevMillis = 0;
bool AUTO, Ok, valvulaRiego, reset; 
bool signupOK = false;
 
void setup() {
  Serial.begin(9600);

  /* Inicio conexión WiFi */
  InitWiFi();

  /* Comprobar Hora RTC con NTP */
  configTime(gmtOFFset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    rtc.setTimeStruct(timeinfo);
  }

  /* Conexión Firebase */
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASS;
  config.database_url = DB_URL;
  myFirebaseData.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback;
  config.max_token_generation_retry = 5;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  Serial.println("Getting User UID");
  while ((auth.token.uid) == ""){
    Serial.print('.');
    delay(1000);
  }
  uID = auth.token.uid.c_str();  
  Serial.println(uID);
  
  analogReadResolution(9);

  pinMode(pinLed, OUTPUT);
  pinMode(valvulaRiegoVin1, OUTPUT);
  digitalWrite(valvulaRiegoVin1, LOW);
  pinMode(valvulaRiegoGND1, OUTPUT);
  digitalWrite(valvulaRiegoGND1, LOW);
  
  //Temporizadores
  timer1 = timerBegin(0, 80, true);
  timerAttachInterrupt(timer1, &onTimer1, true);
  timerAlarmWrite(timer1, 1000000, true);
  timerAlarmEnable(timer1);
  timerAlarmDisable(timer1);
  timer2 = timerBegin(3, 80, true);
  timerAttachInterrupt(timer2, &onTimer2, true);
  timerAlarmWrite(timer2, 500000, true);
  timerAlarmEnable(timer2);
  timerAlarmDisable(timer2);
  
  /* Recuperar datos de memoria flash */
  preferences.begin("MiTerrazaIoT", false);
  LimiteRiego = preferences.getUInt("LimiteRiego", 10);
  LimiteHumedad = preferences.getUInt("LimiteHumedad", 40);
  LimiteHoraInicio = preferences.getString("LimiteHoraIni", "17:00");
  LimiteHoraFin = preferences.getString("LimiteHoraFin", "23:00");
  AUTO = preferences.getBool("AUTO", true);
  mailIni = preferences.getBool("mailIni", true);
  mailRiego = preferences.getBool("mailRiego", true);
  preferences.end();

  /* Json Mail Inicio */
  limitesRiego.set("LimiteRiego", LimiteRiego);
  limitesRiego.set("LimiteHumedad", LimiteHumedad);
  horarioRiego.set("LimiteHoraIni", LimiteHoraInicio);
  horarioRiego.set("LimiteHoraFin", LimiteHoraFin);
  DatosRiego.set("limitesRiego", limitesRiego);
  DatosRiego.set("horarioRiego", horarioRiego);
  DatosRiego.toString(datosEmail, true);
  
  //smtp.debug(1);
  //smtp.callback(smtpCallback);
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  /* Mail de inicio de Smart Drip System */
  mailStartSDS.sender.name = "Smart Drip System";
  mailStartSDS.sender.email = AUTHOR_EMAIL;
  mailStartSDS.subject = "Estado ESP32 MiTerrazaIoT";
  mailStartSDS.addRecipient("Pablo", "falder24@gmail.com");
  /* Mail de Estado de riego de Smart Drip System */
  mailDripOn.sender.name = "Smart Drip System";
  mailDripOn.sender.email = AUTHOR_EMAIL;
  mailDripOn.subject = "Estado Riego MiTerrazaIoT";
  mailDripOn.addRecipient("Pablo", "falder24@gmail.com");
  /* Mail de error en electrválvula de riego */
  mailErrorValve.sender.name = "Smart Drip System";
  mailErrorValve.sender.email = AUTHOR_EMAIL;
  mailErrorValve.subject = "Estado válvula de Smart Drip";
  mailErrorValve.addRecipient("Pablo", "falder24@gmail.com");
  /* Mail de error en sensor de flujo */
  mailErrorFlowSensor.sender.name = "Smart Drip System";
  mailErrorFlowSensor.sender.email = AUTHOR_EMAIL;
  mailErrorFlowSensor.subject = "Estado sensor de flujo";
  mailErrorFlowSensor.addRecipient("Pablo", "falder24@gmail.com");
  /* Mail de error en sensor DHT11 */
  mailErrorDHT.sender.name = "Smart Drip System";
  mailErrorDHT.sender.email = AUTHOR_EMAIL;
  mailErrorDHT.subject = "Estado sensor medio ambiente";
  mailErrorDHT.addRecipient("Pablo", "falder24@gmail.com");
  /* Envío mail inicio sistema Smart Drip correctamente*/
  if(mailIni)
    mailStartSystem();  
}

void loop() {

  /*Leer Hora */
  Hora = rtc.getTime();
  Serial.print("Hora: ");
  Serial.println(Hora);

  if(Firebase.isTokenExpired()){
  Firebase.refreshToken(&config);
  }
  
  /* Leer Valores APP prefijados */
  Firebase.RTDB.getBool(&myFirebaseData, "/" + uID + "/OK");
  Ok = myFirebaseData.boolData();
  delay(500);
 
  if (Ok == true){
    Firebase.RTDB.getInt(&myFirebaseData, "/" + uID + "/LimiteRiego");
    LimiteRiego = myFirebaseData.intData();
    Firebase.RTDB.getInt(&myFirebaseData, "/" + uID +  "/LimiteHumedad");
    LimiteHumedad = myFirebaseData.intData();
    Firebase.RTDB.getBool(&myFirebaseData, "/" + uID + "/RiegoAUTO");
    AUTO = myFirebaseData.boolData();
    Firebase.RTDB.getString(&myFirebaseData, "/" + uID + "/LimiteHoraInicio");
    LimiteHoraInicio = myFirebaseData.stringData();
    Firebase.RTDB.getString(&myFirebaseData, "/" + uID + "/LimiteHoraFin");
    LimiteHoraFin = myFirebaseData.stringData();
    Firebase.RTDB.getBool(&myFirebaseData, "/" + uID + "/MailIni");
    mailIni = myFirebaseData.boolData();
    Firebase.RTDB.getBool(&myFirebaseData, "/" + uID + "/MailRiego");
    mailRiego = myFirebaseData.boolData();
    Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/OK", false);
    delay(500);
    preferences.begin("MiTerrazaIoT", false);
    preferences.putUInt("LimiteRiego", LimiteRiego);
    preferences.putUInt("LimiteHumedad", LimiteHumedad);
    preferences.putBool("RiegoAuto", AUTO);
    preferences.putString("LimiteHoraIni", LimiteHoraInicio);
    preferences.putString("LimiteHoraFin", LimiteHoraFin);
    preferences.putBool("mailIni", mailIni);
    preferences.putBool("mailRiego", mailRiego);
    preferences.end();
  }

  Serial.println("Hora Inicio: ");
  Serial.println(LimiteHoraInicio);
  Serial.println("Hora Fin: ");
  Serial.println(LimiteHoraFin);
  HoraInicioCheck = Hora.compareTo(LimiteHoraInicio);
  HoraFinCheck = Hora.compareTo(LimiteHoraFin);

  /* Reset */
  Firebase.RTDB.getBool(&myFirebaseData,"/" + uID + "/Reset");
  reset = myFirebaseData.boolData();

  if (reset == true){
    Firebase.RTDB.setBool(&myFirebaseData,"/" + uID + "/Reset", false);
    esp_restart;
  }  

  /* Lectura del Higro y DHT11 cada 1.2s */
  int HumedadSuelo = analogRead(PinHigro);
  PorcentajeHumedad = map(HumedadSuelo, wet, dry, 100, 0);
  if (millis() > TiempoDHT + SampleDHT){
    if (DHT.read2(&Temperatura, &Humedad, NULL) == SimpleDHTErrSuccess) {
      Serial.println("DHT11 OK");
      DHT11.set("/Temperatura", Temperatura);
      DHT11.set("/Humedad", Humedad);
      TiempoDHT = millis();
    } else {
      mailErrorDHT11();
      Serial.println("Error DHT11");
    }
    Serial.println("Humedad suelo: "); Serial.print(PorcentajeHumedad); Serial.println("%");
  }
  
  Medidas.set("/DHT11", DHT11);
  Medidas.set("/Higro/HumedadSuelo", PorcentajeHumedad);
  Medidas.toString(myJsonStr);
  delay(1000);
  
  /* Subir a la base de datos cada segundo los valores del Higro y DHT11 */
  if(millis() > TiempoFirebase + DelayFirebase){
    Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&myFirebaseData, "/" + uID + "/Medidas", &Medidas) ? "ok" : myFirebaseData.errorReason().c_str());
    TiempoFirebase = millis();
  }
  
  /* Activar motor de riego */
  if (AUTO != false){

    Serial.println("Riego AUTO ON");
    bool CheckTimer = timerAlarmEnabled(timer1);
    delay(500);

    Serial.print("Checktimer: ");
    Serial.println(CheckTimer);
    if (CheckTimer != true){
      TiempoRIEGO = LimiteRiego;
      HumedadRiego = LimiteHumedad;
      Serial.println("Temporizador desconectado");
    }
    Serial.print("TiempoRIEGO: ");
    Serial.println(TiempoRIEGO);
    Serial.print("HumedadRiego: ");
    Serial.println(HumedadRiego);

    /* Comprobacion horario riego */
    if(HoraInicioCheck >= 0 && HoraFinCheck <=0){
      Serial.println("Horario de riego activo");
      if(PorcentajeHumedad > HumedadRiego){
        if(!CheckTimer){
        Serial.println("Suelo húmedo, no necesita regarse");
        }else {
          Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/TiempoRiego", TiempoRIEGO); 
        }
      }
      else {
        timerAlarmEnable(timer1);
        if(!valvulaRiego){
          openDripValve();
          delay(1000); 
          
          if(valvulaRiego & !sensorFlujo){     //prueba configuracion sensor de  flujo
            closeDripValve();
            timerAlarmDisable(timer1);
          }else{
            Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/RiegoConectado", true);  
            Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/HumRiego", HumedadRiego);
            Serial.println("Riego Auto Conectado");
            if(mailRiego){
              mailSmartDripOn();
            }        
          }
        }                   //pendiente continuar comprobacion inclusion sensor desde aqui
        valvulaRiego = true;
        Serial.println("Salida ValvulaRiego: ");
        Serial.println(valvulaRiego);
        digitalWrite(PinLed, toggle);
        Serial.println("Suelo seco, necesita regarse");
        Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/TiempoRiego", TiempoRIEGO);
        Serial.println(contador);
        Serial.println(TiempoRIEGO);
        delay(1000);
      }
    }
    if(TiempoRIEGO <= 0 && AUTO != false){
      Serial.println("Tiempo de Riego terminado");
      timerAlarmDisable(timer1);
      if (valvulaRiego == true){
        closeDripValve();
        /* Serial.println("Salida ValvulaVin: ");
        Serial.println(ValvulaRiegoVin);
        Serial.println("Salida ValvulaGND: ");
        Serial.println(ValvulaRiegoGND); */
        valvulaRiego = false;
      }
      Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/RiegoConectado", false);
    }
  }
  else{
    Serial.println("AUTO OFF");
    timerAlarmDisable(timer1);
    contador = 0;
    LimiteRiego = 0;

    if (valvulaRiego == true){
        digitalWrite(valvulaRiegoVin1, LOW);
        digitalWrite(valvulaRiegoGND1, HIGH);
        Serial.println("Salida ValvulaVin: ");
        Serial.println(valvulaRiegoVin);
        Serial.println("Salida ValvulaGND: ");
        Serial.println(valvulaRiegoGND);
        timerAlarmEnable(timer2);
    }
    valvulaRiego = false;
    Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/RiegoConectado", false);
    delay(500);
  }
}
/* Método temporizador 1min */
void IRAM_ATTR onTimer1() {
    toggle ^= true;
    if(toggle == true){
      contador++;
      if(contador == 59){
        contador = 0;
        TiempoRIEGO--;
      }
    }
}
/* Método temporizador para cortar tensión al puente H */
void IRAM_ATTR onTimer2() {
  digitalWrite(valvulaRiegoVin1, LOW);
  digitalWrite(valvulaRiegoGND1, LOW);
  timerAlarmDisable(timer2);
}

void openDripValve(){
  digitalWrite(valvulaRiegoVin1, HIGH);
  digitalWrite(valvulaRiegoGND1, LOW);
  timerAlarmEnable(timer2);
}

void closeDripValve(){
  digitalWrite(valvulaRiegoVin1, LOW);
  digitalWrite(valvulaRiegoGND1, HIGH);
  timerAlarmEnable(timer2);
}

void InitWiFi() {
  WiFi.begin(SSID, PASS);                 // Inicializamos el WiFi con nuestras credenciales.

  Serial.print("Conectando a ");
  Serial.print(SSID);
  while(WiFi.status() != WL_CONNECTED){  
    Serial.print(".");
    delay(5000);
    WiFi.reconnect();   // probando reinicio wifi después de una caida de tensión
    //InitWiFi();
  }
  if(WiFi.status() == WL_CONNECTED){      // Si el estado del WiFi es conectado entra al If
    Serial.println("");
    Serial.println("");
    Serial.println("Conexion exitosa!!!");
  }
  Serial.println("");
  Serial.print("Tu IP es: ");
  Serial.println(WiFi.localIP());
}

void mailStartSystem(){
  String textMsg = "ESP32 conectado correctamente a la red y en funcionamiento \n" + datosEmail;
  mailStartSDS.text.content = textMsg.c_str();
  mailStartSDS.text.charSet = "us-ascii";
  mailStartSDS.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailStartSDS.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailStartSDS))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d\n", MailClient.getFreeHeap());
}

void mailSmartDripOn(){
  Hora = rtc.getTime();   
  String textMsg = "Riego conectado correctamente a las: " + Hora; //Añadido envío de hora de activación del riego
  mailDripOn.text.content = textMsg.c_str();
  mailDripOn.text.charSet = "us-ascii";
  mailDripOn.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailDripOn.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailDripOn))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d/n", MailClient.getFreeHeap()); 
}

void mailErrorValve(){
  String textMsg = "Error en la electrovávula de riego. \n" +
                   "Los sensores indican que el agua continúa fluyendo. \n" +
                   "Por favor revise la instalación lo antes posible.";
  mailDripOn.text.content = textMsg.c_str();
  mailDripOn.text.charSet = "us-ascii";
  mailDripOn.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailDripOn.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

   if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailErrV))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d/n", MailClient.getFreeHeap()); 
}

void mailErrorDHT11(){
  String textMsg = "El sensor de medio ambiente está desconectado o dañado";
  mailErrorDHT.text.content = textMsg.c_str();
  mailErrorDHT.text.charSet = "us-ascii";
  mailErrorDHT.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailErrorDHT.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailErrorDHT))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d/n", MailClient.getFreeHeap()); 
}

void mailErrorSensorHigro(){
  String textMsg = "El sensor de humedad del sustrato está fuera de rango";
  mailErrorHigro.text.content = textMsg.c_str();
  mailErrorHigro.text.charSet = "us-ascii";
  mailErrorHigro.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailErrorHigro.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailErrorHigro))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d/n", MailClient.getFreeHeap()); 
}
/*
void smtpCallback(SMTP_Status status){
  Serial.println(status.info());

  if(status.success()){
    Serial.println("......................");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
    Serial.println("......................");
    struct tm dt;
    for(size_t i = 0; i < smtp.sendingResult.size(); i++){
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipent: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    Serial.println(".....................\n");
  }
}
*/