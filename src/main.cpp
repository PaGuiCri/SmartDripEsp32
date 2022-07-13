#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <SimpleDHT.h>
#include <NTPClient.h>
#include <iostream>
#include <string>
#include <Arduinojson.h> 
#include <ESP32Time.h>
#include <Preferences.h>
#include <ESP_Mail_Client.h>

/* Red WiFi */
#define SSID "MiFibra-21E0_EXT"
#define PASS "MaGGyv9h"
void InitWiFi();

/* Email */
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "falder24@gmail.com"
#define AUTHOR_PASSWORD "kcjbfgngmfgkcxtw"
SMTPSession smtp;
void smtpCallback(SMTP_Status status);
void mensajeRiego();
ESP_Mail_Session session;
SMTP_Message message;
SMTP_Message messageRiego;

/* Firebase */
#define DB_URL "terrazaiot-default-rtdb.europe-west1.firebasedatabase.app"     // URL de la base de datos.
#define SECRET_KEY "fXRMCESlqDEn832HH0pEAcgywE15eEZpoHHaTjU9"                   // Password de la base de datos.
FirebaseData myFirebaseData;                                                    // Objeto de tipo FirebaseData, ayuda a leer y escribri en la base de datos.
String myJsonStr;                                     
FirebaseJson Medidas, DHT11, Higro;
unsigned long TiempoFirebase = 0;
#define DelayFirebase 100

//Timers
volatile bool toggle = true;
void IRAM_ATTR onTimer1();
hw_timer_t *timer1 = NULL;
void IRAM_ATTR onTimer2();
hw_timer_t *timer2 = NULL;

// Define NTP Client to get time
ESP32Time rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedTime;
String dayStamp;
String timeStamp;
String LimiteHoraInicio;
String LimiteHoraFin;
int HoraInicioCheck, HoraFinCheck;

//sincronizacion del RTC con NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOFFset_sec = 3600;
const int daylightOffset_sec = 3600;

/* Configuracion de terminales para Higrometro y DHT11  */
#define PinHigro 34
#define PinDHT 4
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

//Instancia para almacenar datos en memoria flash
Preferences preferences;

#define PinLed 2
#define ValvulaRiegoVin 32
#define ValvulaRiegoGND 25

bool AUTO, Ok, ValvulaRiego;
 
void setup() {
  Serial.begin(9600);

  InitWiFi();  

  Firebase.begin(DB_URL, SECRET_KEY);                           
  Firebase.reconnectWiFi(true);
  
  analogReadResolution(9);

  //pinMode(ValvulaRiego, OUTPUT);
  //digitalWrite(ValvulaRiego, LOW);
  pinMode(PinLed, OUTPUT);
  pinMode(ValvulaRiegoVin, OUTPUT);
  digitalWrite(ValvulaRiegoVin, LOW);
  pinMode(ValvulaRiegoGND, OUTPUT);
  digitalWrite(ValvulaRiegoGND, LOW);
  
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
  
  //Comprobar Hora RTC con NTP
  configTime(gmtOFFset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)){
    rtc.setTimeStruct(timeinfo);
  }

  //Recuperar datos de memoria flash
  preferences.begin("MiTerrazaIoT", false);
  LimiteRiego = preferences.getUInt("LimiteRiego", 10);
  LimiteHumedad = preferences.getUInt("LimiteHumedad", 40);
  LimiteHoraInicio = preferences.getString("LimiteHoraIni", "17:00");
  LimiteHoraFin = preferences.getString("LimiteHoraFin", "23:00");
  AUTO = preferences.getBool("AUTO", true);
  preferences.end();

  
  //smtp.debug(1);
  //smtp.callback(smtpCallback);

  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  message.sender.name = "ESP32";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "Estado ESP32 MiTerrazaIoT";
  message.addRecipient("Pablo", "falder24@gmail.com");

  messageRiego.sender.name = "ESP32";
  messageRiego.sender.email = AUTHOR_EMAIL;
  messageRiego.subject = "Estado Riego MiTerrazaIoT";
  messageRiego.addRecipient("Pablo", "falder24@gmail.com");

  String textMsg = "ESP32 conectado correctamente a la red y en funcionamiento";
  message.text.content = textMsg.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &message))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d\n", MailClient.getFreeHeap());
}

void loop() {

  //Leer Hora
  String Hora = rtc.getTime();
  Serial.print("Hora: ");
  Serial.println(Hora);

 /* Leer Valores APP prefijados */
  Firebase.get(myFirebaseData,"/OK");
  Ok = myFirebaseData.boolData();
  delay(1000);
  
  if (Ok == true){
    Firebase.get(myFirebaseData, "/LimiteRiego");
    LimiteRiego = myFirebaseData.intData();
    Firebase.get(myFirebaseData, "/LimiteHumedad");
    LimiteHumedad = myFirebaseData.intData();
    Firebase.get(myFirebaseData, "/RiegoAUTO");
    AUTO = myFirebaseData.boolData();
    Firebase.get(myFirebaseData, "/LimiteHoraInicio");
    LimiteHoraInicio = myFirebaseData.stringData();
    Firebase.get(myFirebaseData, "/LimiteHoraFin");
    LimiteHoraFin = myFirebaseData.stringData();
    Firebase.set(myFirebaseData, "/OK", false);
    delay(1000);
    preferences.begin("MiTerrazaIoT", false);
    preferences.putUInt("LimiteRiego", LimiteRiego);
    preferences.putUInt("LimiteHumedad", LimiteHumedad);
    preferences.putBool("RiegoAuto", AUTO);
    preferences.putString("LimiteHoraIni", LimiteHoraInicio);
    preferences.putString("LimiteHoraFin", LimiteHoraFin);
    preferences.end();
  }

  Serial.println("Hora Inicio: ");
  Serial.println(LimiteHoraInicio);
  Serial.println("Hora Fin: ");
  Serial.println(LimiteHoraFin);
  HoraInicioCheck = Hora.compareTo(LimiteHoraInicio);
  HoraFinCheck = Hora.compareTo(LimiteHoraFin);

  /* Lectura del Higro y DHT11 cada 1.2s */
  int HumedadSuelo = analogRead(PinHigro);
  PorcentajeHumedad = map(HumedadSuelo, wet, dry, 100, 0);
  if (millis() > TiempoDHT + SampleDHT){
    if (DHT.read2(&Temperatura, &Humedad, NULL) == SimpleDHTErrSuccess) {
      Serial.println("DHT11 OK");
      DHT11.set("Temperatura", Temperatura);
      DHT11.set("Humedad", Humedad);
      TiempoDHT = millis();
    } else {
      Serial.println("Error DHT11");
    }
    Serial.println("Humedad suelo: "); Serial.print(PorcentajeHumedad); Serial.println("% ");
  }
  
  Medidas.set("DHT11", DHT11);
  Medidas.set("Higro/HumedadSuelo", PorcentajeHumedad);
  Medidas.toString(myJsonStr);

  /* Subir a la base de datos cada segundo los valores del Higro y DHT11 */
  if(millis() > TiempoFirebase + DelayFirebase){
    Firebase.set(myFirebaseData, "Medidas", Medidas);
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

    if(HoraInicioCheck >= 0 && HoraFinCheck <=0){
      Serial.println("Horario de riego activo");
      if(PorcentajeHumedad > HumedadRiego){
        if(CheckTimer != true){
        Serial.println("Suelo húmedo, no necesita regarse");
        }
        if(CheckTimer != false){
          Firebase.set(myFirebaseData, "/DatosRiego/TiempoRiego", TiempoRIEGO);
        }
      }
      else {
        timerAlarmEnable(timer1);
        if( ValvulaRiego != true){
          digitalWrite(ValvulaRiegoVin, HIGH);
          digitalWrite(ValvulaRiegoGND, LOW);
          //Serial.println("Salida ValvulaVin: ");
          //Serial.println(ValvulaRiegoVin);
          //Serial.println("Salida ValvulaGND: ");
          //Serial.println(ValvulaRiegoGND);
          timerAlarmEnable(timer2);
          Firebase.set(myFirebaseData, "/RiegoConectado", true);  
          Firebase.set(myFirebaseData, "/DatosRiego/HumRiego", HumedadRiego);
          Serial.println("Riego Auto Conectado");
          delay(500); 
          mensajeRiego();        
        }
        ValvulaRiego = true;
        Serial.println("Salida ValvulaRiego: ");
        Serial.println(ValvulaRiego);
        digitalWrite(PinLed, toggle);
        Serial.println("Suelo seco, necesita regarse");
        Firebase.set(myFirebaseData, "/DatosRiego/TiempoRiego", TiempoRIEGO);
        //Firebase.set(myFirebaseData,"/HorarioRiego/HoraFinRiego", LimiteHoraFin);
        //Firebase.set(myFirebaseData,"/HorarioRiego/HoraInicioRiego", LimiteHoraInicio);
        Serial.println(contador);
        Serial.println(TiempoRIEGO);
        delay(1000);
      }
    }
    if(TiempoRIEGO <= 0 && AUTO != false){
      Serial.println("Tiempo de Riego terminado");
      timerAlarmDisable(timer1);
      if (ValvulaRiego == true){
        digitalWrite(ValvulaRiegoVin, LOW);
        digitalWrite(ValvulaRiegoGND, HIGH);
        Serial.println("Salida ValvulaVin: ");
        Serial.println(ValvulaRiegoVin);
        Serial.println("Salida ValvulaGND: ");
        Serial.println(ValvulaRiegoGND);
        timerAlarmEnable(timer2);
        ValvulaRiego = false;
      }
      Firebase.set(myFirebaseData, "/RiegoConectado", false);
    }
  }
  else{
    Serial.println("AUTO OFF");
    timerAlarmDisable(timer1);
    contador = 0;
    LimiteRiego = 0;

    if (ValvulaRiego == true){
        digitalWrite(ValvulaRiegoVin, LOW);
        digitalWrite(ValvulaRiegoGND, HIGH);
        Serial.println("Salida ValvulaVin: ");
        Serial.println(ValvulaRiegoVin);
        Serial.println("Salida ValvulaGND: ");
        Serial.println(ValvulaRiegoGND);
        timerAlarmEnable(timer2);
    }
    ValvulaRiego = false;
    Firebase.set(myFirebaseData, "/RiegoConectado", false);
    delay(500);

  }
}

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

void IRAM_ATTR onTimer2() {
  digitalWrite(ValvulaRiegoVin, LOW);
  digitalWrite(ValvulaRiegoGND, LOW);
  timerAlarmDisable(timer2);
}

void InitWiFi() {
  WiFi.begin(SSID, PASS);                 // Inicializamos el WiFi con nuestras credenciales.
  Serial.print("Conectando a ");
  Serial.print(SSID);

  while(WiFi.status() != WL_CONNECTED){  
    Serial.print(".");
    delay(50);
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

void mensajeRiego(){
 
  String textMsg = "Riego conectado correctamente";
  messageRiego.text.content = textMsg.c_str();
  messageRiego.text.charSet = "us-ascii";
  messageRiego.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  messageRiego.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &messageRiego))
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
