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
void mailStartSystem();
void mailErrorValve();
void mailErrorDHT11();
void mailErrorSensorHigro();
ESP_Mail_Session session;
SMTP_Message mailStartSDS;
SMTP_Message mailDripOn;
SMTP_Message mailErrValve;
SMTP_Message mailErrorFlowSensor;
SMTP_Message mailErrorDHT;
SMTP_Message mailErrorHigro;
bool mailIni, mailRiego, mailErrV, mailErrFS, mailErrDht, mailErrHig; 
bool mailRiegoEnviado = false; 
bool mailErrorValveEnviado = false;

/* Configuración Firebase */
#define DB_URL "https://terrazaiot-default-rtdb.europe-west1.firebasedatabase.app/" 
#define API_KEY "AIzaSyAGU4eDmNZntr_sB0dTmnL90M1dytiq37g"                 
#define USER_EMAIL "falder24@gmail.com"
#define USER_PASS "123456"
FirebaseData myFirebaseData;                                                   
FirebaseAuth auth;
FirebaseConfig config;
String myJsonStr;
String datosEmail; 
String uID;                                    
FirebaseJson Medidas, DHT11, DatosRiego, limitesRiego, horarioRiego; 
int caudalRiego, caudalTotal;
unsigned long TiempoFirebase = 0;
#define DelayFirebase 1000  // cambio a 1000 para regular la subida a la base de datos antes 100

/* Timers */
volatile bool toggle = true;
void IRAM_ATTR onTimer1();
hw_timer_t *timer1 = NULL;
void IRAM_ATTR onTimer2();
hw_timer_t *timer2 = NULL;

/* Métodos para abrir y cerrar la electroválvula */
void openDripValve();
void closeDripValve();
void cortarPulso();
void closeValveError();

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

/* Sincronización del RTC con NTP */
const char* ntpServer = "pool.ntp.org";
const long gmtOFFset_sec = 3600;
const int daylightOffset_sec = 3600;

/* Configuración de terminales para higrómetro y DHT11  */
#define PinHigro 34  // Nueva configuración de pines antes 34. volvemos al pin 34 desde el 13
#define PinDHT 4    // El pin del sensor DHT tiene q ser el 4 si se trabaja con la biblioteca SimpleDHT
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

/* Pin de entrada para el sensor de caudal */
// const int sensorPin = 34; // Cambia el número del pin según cómo lo hayas conectado

/* Variables para el cálculo del caudal */
volatile int pulsos = 0;
float caudal = 0.0;
float litrosRiego = 0;
float totalLitros = 0;
unsigned long oldTime = 0;
bool estadoSensorFlujo = false;

/* Interrupción llamada cada vez que se detecta un pulso del sensor */
void pulseCounter(){
  pulsos++;
}
void contadorLitros();

/* Instancia para almacenar datos de riego en memoria flash */
Preferences preferences;

#define valvulaRiegoVin1 27 // Nueva configuración de pines antes 32. Salida Electroválvula 1
#define valvulaRiegoGND1 26 // Nueva configuración de pines antes 25. Salida Electroválvula 1
#define valvulaRiegoVin2 25
#define valvulaRiegoGND2 33
#define sensorFlujo 13     // Nueva configuración de pines antes 20 pendiente test pin 13

bool AUTO, Ok, valvulaRiego, reset, pulsoActivo; 
const unsigned long duracionPulso = 100; // Duración del pulso en milisegundos
unsigned long tiempoPulsoInicio = 0;
int contadorCierreValve = 10;

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

  pinMode(valvulaRiegoVin1, OUTPUT);
  digitalWrite(valvulaRiegoVin1, LOW);
  pinMode(valvulaRiegoGND1, OUTPUT);
  digitalWrite(valvulaRiegoGND1, LOW);
  pinMode(sensorFlujo, INPUT);

  /* Configuración de la interrupción para detectar los pulsos del sensor de flujo */
  attachInterrupt(digitalPinToInterrupt(sensorFlujo), pulseCounter, FALLING);

  /* Configuración temporizador */
  timer1 = timerBegin(0, 80, true);
  timerAttachInterrupt(timer1, &onTimer1, true);
  timerAlarmWrite(timer1, 1000000, true);
  timerAlarmEnable(timer1);
  timerAlarmDisable(timer1);
  
  /* Recuperar datos de memoria flash */
  preferences.begin("MiTerrazaIoT", false);
  LimiteRiego = preferences.getUInt("LimiteRiego", 10);
  LimiteHumedad = preferences.getUInt("LimiteHumedad", 40);
  LimiteHoraInicio = preferences.getString("LimiteHoraIni", "21:00");
  LimiteHoraFin = preferences.getString("LimiteHoraFin", "23:30");
  //totalLitros = preferences.getString("totalLitros", "");     // Añadida linea de recuperación de consumo total de agua
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
  mailErrValve.sender.name = "Smart Drip System";
  mailErrValve.sender.email = AUTHOR_EMAIL;
  mailErrValve.subject = "Estado válvula de Smart Drip";
  mailErrValve.addRecipient("Pablo", "falder24@gmail.com");
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
  if(mailIni){
    mailStartSystem();
  }    
  cortarPulso();
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
  contadorLitros();
  if (pulsoActivo && (millis() - tiempoPulsoInicio >= duracionPulso)) {
    cortarPulso();
  } 
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
    Firebase.RTDB.getInt(&myFirebaseData, "/" + uID + "/totalLitros"); // Añadida lineas para recuperar dato de consumo de la base de datos
    totalLitros = myFirebaseData.intData();
    Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/OK", false);
    preferences.begin("MiTerrazaIoT", false);
    preferences.putUInt("LimiteRiego", LimiteRiego);
    preferences.putUInt("LimiteHumedad", LimiteHumedad);
    preferences.putBool("RiegoAuto", AUTO);
    preferences.putString("LimiteHoraIni", LimiteHoraInicio);
    preferences.putString("LimiteHoraFin", LimiteHoraFin);
    preferences.putBool("mailIni", mailIni);
    preferences.putBool("mailRiego", mailRiego);
    preferences.putUInt("totalLitros", totalLitros);
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
    Serial.print("Humedad suelo: "); Serial.print(PorcentajeHumedad); Serial.println("%");
  }
  Medidas.set("/DHT11", DHT11);
  Medidas.set("/Higro/HumedadSuelo", PorcentajeHumedad);
  Medidas.toString(myJsonStr);
  
  /* Subir a la base de datos cada segundo los valores del Higro y DHT11 */
  if(millis() > TiempoFirebase + DelayFirebase){  //pendiente comprobación tiempo de subida de datos
    Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&myFirebaseData, "/" + uID + "/Medidas", &Medidas) ? "ok" : myFirebaseData.errorReason().c_str());
    TiempoFirebase = millis();
  }
  
  /* Activar motor de riego */
  if (AUTO != false){
    Serial.println("Riego AUTO ON");
    bool CheckTimer = timerAlarmEnabled(timer1);
    Serial.print("Checktimer: ");
    Serial.println(CheckTimer);
    if (CheckTimer != true){
      TiempoRIEGO = LimiteRiego;
      HumedadRiego = LimiteHumedad;
      Serial.println("Temporizador desconectado");
    }else{
      Serial.println("Temporizador conectado");
      Serial.println("Proceso de riego en marcha");
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
        Serial.println("Suelo seco, necesita regarse");
        timerAlarmEnable(timer1);
        if(!valvulaRiego){
          openDripValve();
          contadorLitros();
          Serial.println("Proceso de riego iniciado");  
        }else{
          contadorLitros();
          if(valvulaRiego && estadoSensorFlujo){
            Serial.print("Caudal: ");
            Serial.print(caudal);
            Serial.print(" L/min - Volumen acumulado: ");
            Serial.print(totalLitros);
            Serial.println(" L.");
            if(mailRiego && !mailRiegoEnviado){
              mailSmartDripOn();
              mailRiegoEnviado = true;
            }
          }
          /* Subir a la base de datos actualizados de humedad de sustrato y riego conectado */
          Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/RiegoConectado", true);  
          Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/HumRiego", HumedadRiego);
          Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/CantidadAgua", litrosRiego);
          Serial.println("Riego Auto Conectado");        
        }   
        valvulaRiego = true;
        Serial.print("Salida ValvulaRiego: ");
        Serial.println(valvulaRiego);
        Serial.print("Estado sensor flujo: ");
        Serial.println(estadoSensorFlujo);
        /* Subir tiempo de riego restante */
        Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/DatosRiego/TiempoRiego", TiempoRIEGO);
        Serial.print("Contador conectado: ");
        Serial.println(contador);
        Serial.print("Tiempo de riego: ");
        Serial.println(TiempoRIEGO + " min.");
        if(valvulaRiego & !estadoSensorFlujo){     
          Serial.println("Error en apertura de valvula. Se reinicia el proceso de riego");
          closeDripValve();
          timerAlarmDisable(timer1);
          mailRiegoEnviado = false;
        }
      }
    }else{
      Serial.println(" Fuera de horario de riego");
      Serial.print(" Caudal de riego fuera de horario: ");
      Serial.println(caudalRiego);
      if(!valvulaRiego && caudalRiego != 0){
        if(contadorCierreValve != 0){
          closeValveError();
        }
        if(estadoSensorFlujo && !mailErrorValveEnviado && contadorCierreValve == 0){
          mailErrorValve();
          mailErrorValveEnviado = true;
          Serial.println("Email de Error en válvula enviado");
          AUTO = false;
          Firebase.RTDB.setBool(&myFirebaseData,"/" + uID + "/RiegoAUTO", false);
        }
      }
    }
    if(TiempoRIEGO <= 0 && AUTO != false){
      Serial.println("Tiempo de Riego terminado");
      timerAlarmDisable(timer1);
      if (valvulaRiego == true){
        closeDripValve();
        valvulaRiego = false;
        mailRiegoEnviado = false;
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
        closeDripValve();
    }
    valvulaRiego = false;
    Firebase.RTDB.setBool(&myFirebaseData, "/" + uID + "/RiegoConectado", false);
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

void openDripValve(){
  Serial.println("Apertura de válvula de riego");
  digitalWrite(valvulaRiegoVin1, HIGH);
  digitalWrite(valvulaRiegoGND1, LOW);
  int estadoSalida = digitalRead(valvulaRiegoVin1);
  Serial.print("VálvulaRiegoVin: ");
  Serial.println(estadoSalida);
  estadoSalida = digitalRead(valvulaRiegoGND1);
  Serial.print("VálvulaRiegoGND: ");
  Serial.println(estadoSalida);
  tiempoPulsoInicio = millis();
  pulsoActivo = true;
  Serial.println("Pulso Activo");
  //timerAlarmEnable(timer2);
}

void closeDripValve(){
  Serial.println("Cierre de válvula de riego");
  digitalWrite(valvulaRiegoVin1, LOW);
  digitalWrite(valvulaRiegoGND1, HIGH);
  int estadoSalida = digitalRead(valvulaRiegoVin1);
  Serial.print("VálvulaRiegoVin: ");
  Serial.println(estadoSalida);
  estadoSalida = digitalRead(valvulaRiegoGND1);
  Serial.print("VálvulaRiegoGND: ");
  Serial.println(estadoSalida);
  valvulaRiego = false;
  tiempoPulsoInicio = millis();
  pulsoActivo = true;
  Serial.println("Pulso Activo");
  //timerAlarmEnable(timer2);
}

void closeValveError(){
  Serial.println("Cierre de válvula de riego de emergencia");
  digitalWrite(valvulaRiegoVin1, LOW);
  digitalWrite(valvulaRiegoGND1, HIGH);
  int estadoSalida = digitalRead(valvulaRiegoVin1);
  Serial.print("VálvulaRiegoVin: ");
  Serial.println(estadoSalida);
  estadoSalida = digitalRead(valvulaRiegoGND1);
  Serial.print("VálvulaRiegoGND: ");
  Serial.println(estadoSalida);
  valvulaRiego = false;
  tiempoPulsoInicio = millis();
  pulsoActivo = true;
  Serial.println("Pulso Activo");
  delay(150);
  if (pulsoActivo && (millis() - tiempoPulsoInicio >= duracionPulso)) {  
    cortarPulso();
  } 
  contadorCierreValve--;
  Serial.print("Intentos: ");
  Serial.println(contadorCierreValve);
  delay(1000);
}

void cortarPulso(){
  digitalWrite(valvulaRiegoVin1, LOW);
  digitalWrite(valvulaRiegoGND1, LOW);
  Serial.println("Corta corriente salidas electrovalvula");
  int estadoSalida = digitalRead(valvulaRiegoVin1);
  Serial.print("VálvulaRiegoVin: ");
  Serial.println(estadoSalida);
  estadoSalida = digitalRead(valvulaRiegoGND1);
  Serial.print("VálvulaRiegoGND: ");
  Serial.println(estadoSalida);
  pulsoActivo = false;
  Serial.println("Pulso no activo");
}

void contadorLitros(){
    /* Cálculo del caudal cada segundo */
    if ((millis() - oldTime) > 1000){
      /* Desactiva las interrupciones mientras se realiza el cálculo */
      detachInterrupt(digitalPinToInterrupt(sensorFlujo));
      Serial.print("Pulsos: ");
      Serial.println(pulsos);
      /* Calcula el caudal en litros por minuto */
      caudal = pulsos / 5.5;                         // factor de conversión, siendo K=7.5 para el sensor de ½”, K=5.5 para el sensor de ¾” y 3.5 para el sensor de 1”
      // Reinicia el contador de pulsos
      pulsos = 0;
      // Calcula el volumen de agua en mililitros
      litrosRiego = (caudal / 60) * 1000/1000;
      // Incrementa el volumen total acumulado
      totalLitros += litrosRiego;
      // Activa las interrupciones nuevamente
      attachInterrupt(digitalPinToInterrupt(sensorFlujo), pulseCounter, FALLING);
      // Actualiza el tiempo anterior
      oldTime = millis();
      caudalRiego = caudal;
      caudalTotal = totalLitros; 
      if(caudal != 0){
            estadoSensorFlujo = true;
            Serial.println(" Sensor de riego conectado");
          }else{
            estadoSensorFlujo = false;
            Serial.println(" Sensor de riego desconectado");
          } 
      Firebase.RTDB.setInt(&myFirebaseData, "/" + uID + "/CaudalRiego", caudalRiego); 
    }
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
  String textMsg = " SmartDrip conectado a la red y en funcionamiento. \n";   // cambio en el email de inicio de SmartDrip para mejorar la visualización sin las llaves de json
                   /* " Datos de configuración guardados: \n" + 
                   " Tiempo de riego: " + LimiteRiego + "min. \n" + 
                   " Limite de humedad de riego: " + LimiteHumedad + "% \n" +
                   " Horario de activación de riego: \n" +
                   " Hora de inicio: " + LimiteHoraInicio + " \n" +
                   " Hora de fin: " + LimiteHoraFin + " \n"; */
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
  String textMsg = "Error en la electrovávula de riego. Se detiene el proceso de riego automático. Revise la instalación lo antes posible. \n";
                   /* "Los sensores indican que el agua continúa fluyendo. \n" +
                   "Por favor revise la instalación lo antes posible."; */
  mailErrValve.text.content = textMsg.c_str();
  mailErrValve.text.charSet = "us-ascii";
  mailErrValve.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  mailErrValve.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

   if(!smtp.connect(&session))
    return;

  if(!MailClient.sendMail(&smtp, &mailErrValve))
    Serial.println("Error envío Email, " + smtp.errorReason());

  ESP_MAIL_PRINTF("Liberar memoria: %d/n", MailClient.getFreeHeap()); 
} 

void mailErrorDHT11(){
  String textMsg = "El sensor de datos ambientales está desconectado o dañado";
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
  String textMsg = "El sensor de humedad del sustrato está fuera de rango o dañado";
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