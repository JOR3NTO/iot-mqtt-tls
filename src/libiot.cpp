/**
 * @file    libiot.cpp
 * @brief   Implementación del módulo IoT: MQTT/TLS, alertas, y publicación GPS
 * @details Gestiona la conexión segura al broker EMQX vía TLS, la reconexión
 *          automática, la recepción de mensajes (alertas y OTA), y la publicación
 *          de datos GPS del rastreador de mascotas en formato JSON.
 *
 *          Se removió toda la lógica del sensor SHT21 (temperatura/humedad).
 *          La función sendGPSData() reemplaza a sendSensorData() y publica
 *          SIEMPRE (incluso sin fix GPS) para que Grafana detecte pérdida de señal.
 */

/*
 * The MIT License
 *
 * Copyright 2024 Alvaro Salazar <alvaro@denkitronik.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libiot.h>
#include <Wire.h>
#include <libota.h>
#include <libstorage.h>
#include "libgps.h"

// Versión del firmware (debe coincidir con main.cpp)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.1.1"
#endif

//#define PRINT
#ifdef PRINT
#define PRINTD(x, y) Serial.println(x, y)
#define PRINTLN(x) Serial.println(x)
#define PRINT(x) Serial.print(x)
#else
#define PRINTD(x, y)
#define PRINTLN(x)
#define PRINT(x)
#endif

String alert = ""; //Mensaje de alerta
extern const char * client_id;  //ID del cliente MQTT


/**
 * @brief   Consulta y guarda el tiempo actual con servidores SNTP.
 * @details Sincroniza con pool.ntp.org y time.nist.gov, zona horaria UTC-5 (Colombia).
 * @return  Timestamp actual (epoch)
 */
time_t setTime() {
  //Sincroniza la hora del dispositivo con el servidor SNTP (Simple Network Time Protocol)
  Serial.print("Ajustando el tiempo usando SNTP");
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov"); //Configura la zona horaria y los servidores SNTP
  now = time(nullptr);              //Obtiene la hora actual
  while (now < 1700000000) {        //Espera a que el tiempo sea mayor a 1700000000 (1 de enero de 2024)
    delay(500);                     //Espera 500ms antes de volver a intentar obtener la hora
    Serial.print(".");
    now = time(nullptr);            //Obtiene la hora actual
  }
  Serial.println(" hecho!");
  struct tm timeinfo;               //Estructura que almacena la información de la hora
  gmtime_r(&now, &timeinfo);        //Obtiene la hora actual
  Serial.print("Tiempo actual: ");  //Una vez obtiene la hora, imprime en el monitor el tiempo actual
  Serial.print(asctime(&timeinfo));
  return now;
}

// Variable para debugging periódico
static unsigned long lastMQTTDebug = 0;
static const unsigned long MQTT_DEBUG_INTERVAL = 30000; // 30 segundos

/**
 * @brief   Conecta el dispositivo con el bróker MQTT usando las credenciales establecidas.
 * @details Si ocurre un error lo imprime en la consola. Hace healthcheck cada 30 segundos.
 */
void checkMQTT() {
  if (!client.connected()) {
    reconnect();
  }
  // Procesa mensajes MQTT entrantes (esto es crítico para recibir mensajes)
  // IMPORTANTE: client.loop() debe llamarse frecuentemente para recibir mensajes
  bool loopResult = client.loop();
  if (!loopResult && client.connected()) {
    // Si loop() retorna false pero estamos conectados, podría haber un problema
    Serial.println("⚠ client.loop() retornó false (podría indicar problema de conexión)");
  }
  
  // Debug periódico cada 30 segundos
  unsigned long now = millis();
  if (now - lastMQTTDebug > MQTT_DEBUG_INTERVAL) {
    lastMQTTDebug = now;
    Serial.println("=== Healthcheck MQTT (cada 30s) ===");
    Serial.print("Conectado: ");
    Serial.println(client.connected() ? "✅UP" : "❌DOWN");
  }
}

/**
 * @brief   Adquiere la dirección MAC del dispositivo y la retorna en formato de cadena.
 * @return  String con formato "ESP32-XXXXXXXXXXXX"
 */
String getMacAddress() {
  uint8_t mac[6];
  char macStr[18];
  WiFi.macAddress(mac);
  snprintf(macStr, sizeof(macStr), "ESP32-%02X%02X%02X%02X%02X%02X", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}


/**
 * @brief   Función que se ejecuta cuando se establece conexión con el servidor MQTT
 * @details Intenta reconectar en bucle, suscribiéndose al tópico de entrada
 *          y configurando OTA al conectar exitosamente.
 */
void reconnect() {
  while (!client.connected()) { //Mientras no esté conectado al servidor MQTT
    Serial.println("=== Intentando conectar a MQTT ===");
    Serial.print("Servidor: ");
    Serial.println(mqtt_server);
    Serial.print("Puerto: ");
    Serial.println(mqtt_port);
    Serial.print("Usuario: ");
    Serial.println(mqtt_user);
    Serial.print("Client ID: ");
    Serial.println(client_id);
    Serial.print("Conectando...");
    if (client.connect(client_id, mqtt_user, mqtt_password)) { //Intenta conectarse al servidor MQTT
      Serial.println(" ✓ CONECTADO");
      
      // CRÍTICO: Reconfigurar el callback después de reconectar
      client.setCallback(receivedCallback);
      Serial.println("✓ Callback reconfigurado después de reconexión");
      
      // Imprimir versión del firmware al conectar (usar versión guardada)
      String firmwareVersion = getFirmwareVersion();
      Serial.print("Firmware: ");
      Serial.println(firmwareVersion);
      
      Serial.println("=== Suscripciones MQTT ===");
      // Se suscribe al tópico de suscripción con QoS 1
      bool subResult = client.subscribe(MQTT_TOPIC_SUB, 1);
      if (subResult) {
        Serial.println("✓ Suscrito exitosamente a " + String(MQTT_TOPIC_SUB));
      } else {
        Serial.println("✗ Error al suscribirse a " + String(MQTT_TOPIC_SUB));
      }
      
      // Procesar mensajes para confirmar suscripciones
      client.loop();
      delay(100); // Dar tiempo para procesar
      
      setupOTA(client); //Configura la funcionalidad OTA
      
      // Procesar mensajes nuevamente después de suscribirse a OTA
      client.loop();
      delay(100);
      
      Serial.println("==========================");
      Serial.println("Listo para recibir mensajes MQTT");
    } else {
      Serial.println(" ✗ FALLÓ");
      Serial.println("Problema con la conexión, revise los valores de las constantes MQTT");
      int state = client.state();
      Serial.print("Código de error = ");
      alert = "MQTT error: " + String(state);
      Serial.println(state);
      if ( client.state() == MQTT_CONNECT_UNAUTHORIZED ) ESP.deepSleep(0);
      delay(5000); // Espera 5 segundos antes de volver a intentar
    }
  }
}


/**
 * @brief   Configura el certificado raíz, el servidor MQTT y el puerto
 * @details Inicializa I2C, TLS, buffer MQTT (1024 bytes), callback de recepción
 *          y sincroniza la hora vía SNTP. Ya NO inicializa el sensor SHT21.
 */
void setupIoT() {
  Wire.begin();                 //Inicializa el bus I2C: (SDA, SCL)
  espClient.setCACert(root_ca); //Configura el certificado raíz de la autoridad de certificación
  client.setServer(mqtt_server, mqtt_port);   //Configura el servidor MQTT y el puerto seguro
  
  // Configurar buffer más grande para mensajes grandes (por defecto es 256 bytes)
  client.setBufferSize(1024);
  
  client.setCallback(receivedCallback);       //Configura la función que se ejecutará cuando lleguen mensajes a la suscripción
  Serial.println("=== Configuración MQTT ===");
  Serial.print("Servidor MQTT: ");
  Serial.println(mqtt_server);
  Serial.print("Puerto MQTT: ");
  Serial.println(mqtt_port);
  Serial.print("Usuario MQTT: ");
  Serial.println(mqtt_user);
  Serial.print("Client ID: ");
  Serial.println(client_id);
  Serial.print("Buffer size: ");
  Serial.println(client.getBufferSize());
  Serial.println("Callback MQTT configurado: receivedCallback");
  Serial.println("==========================");
  setTime();                    //Ajusta el tiempo del dispositivo con servidores SNTP
  // NOTA: Se removió setupSHT() — ya no hay sensor de temperatura/humedad
}


/**
 * @brief   Verifica si ha llegado alguna alerta al dispositivo.
 * @details Si no ha llegado devuelve "OK", de lo contrario retorna la alerta.
 *          La alerta se borra automáticamente después de ALERT_DURATION segundos.
 * @return  Mensaje de alerta activo o "OK"
 */
String checkAlert() {
  if (alert.length() != 0) {
    if ((millis() - alertTime) >= ALERT_DURATION * 1000 ) {
      alert = "";
      alertTime = millis();
    }
    return alert;
  } else return "OK";
}

/**
 * @brief   Publica los datos GPS al tópico MQTT configurado
 * @details Publica SIEMPRE, incluso si el fix es inválido (fix:false).
 *          Esto permite que Grafana detecte cuando el GPS pierde señal.
 *          Usa la función gpsToJson() de libgps.cpp para generar el payload.
 *
 *          El intervalo de publicación se controla desde main.cpp (cada 5 segundos)
 *          para no saturar el broker con mensajes frecuentes pero manteniendo
 *          una resolución temporal adecuada para rastreo de mascotas.
 *
 * 
 */
void sendGPSData(GPSData* data) {
  String json = gpsToJson(data);
  Serial.println("[MQTT] Publicando GPS → " + String(MQTT_TOPIC_PUB));
  Serial.println("[MQTT] Payload: " + json);
  client.publish(MQTT_TOPIC_PUB, json.c_str());
}


/**
 * @brief   Callback ejecutado al recibir un mensaje MQTT
 * @details Construye el mensaje que llegó y si contiene ALERT lo asigna a la variable 
 *          alert que es la que se lee para mostrar los mensajes.
 *          También verifica si el mensaje es para actualización OTA.
 * @param   topic   Tópico del mensaje recibido
 * @param   payload Contenido del mensaje en bytes
 * @param   length  Longitud del payload
 */
void receivedCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n*** CALLBACK MQTT DISPARADO ***");
  Serial.print("Topic recibido: [");
  Serial.print(topic);
  Serial.print("] (longitud payload: ");
  Serial.print(length);
  Serial.println(")");
  
  // Crear buffer para el payload (agregar null terminator)
  // Usar String para evitar problemas con VLA
  String data = "";
  for (unsigned int i = 0; i < length; i++) {
    data += (char)payload[i];
  }
  
  Serial.print("Payload: ");
  Serial.println(data);
  
  // Compara el topic recibido con el topic OTA
  String topicStr = String(topic);
  String otaTopicStr = String(OTA_TOPIC);
  
  Serial.println("--- Comparación de topics ---");
  Serial.print("Topic recibido: '");
  Serial.print(topicStr);
  Serial.print("' (longitud: ");
  Serial.print(topicStr.length());
  Serial.println(")");
  Serial.print("OTA_TOPIC esperado: '");
  Serial.print(otaTopicStr);
  Serial.print("' (longitud: ");
  Serial.print(otaTopicStr.length());
  Serial.println(")");
  Serial.print("¿Coinciden? ");
  Serial.println(topicStr == otaTopicStr ? "SÍ" : "NO");
  
  // Verifica si el mensaje es para actualización OTA
  if (topicStr == otaTopicStr) {
    Serial.println("✓✓✓ Mensaje OTA detectado, procesando...");
    checkOTAUpdate(data.c_str());
    return;
  }
  
  // Verifica si el mensaje contiene una alerta
  if (data.indexOf("ALERT") >= 0) {
    Serial.println("✓ Mensaje ALERT detectado");
    alert = data; // Si el mensaje contiene la palabra ALERT, se asigna a la variable alert
  } else {
    Serial.println("⚠ Mensaje recibido pero no es OTA ni ALERT");
  }
  Serial.println("*** FIN CALLBACK ***\n");
}

/**
 * @brief   Función de prueba: Publica un mensaje de prueba y verifica recepción
 * @details Útil para diagnosticar problemas de MQTT
 */
void testMQTTCallback() {
  if (!client.connected()) {
    Serial.println("⚠ No se puede probar: cliente MQTT no conectado");
    return;
  }
  
  Serial.println("=== TEST MQTT CALLBACK ===");
  Serial.println("Este test verifica que el callback funciona correctamente");
  Serial.println("Publicando mensaje de prueba...");
  
  // Publicar un mensaje de prueba al topic de entrada (para que el dispositivo lo reciba)
  String testTopic = String(MQTT_TOPIC_SUB);
  String testMessage = "TEST_MESSAGE_FROM_SELF";
  
  bool pubResult = client.publish(testTopic.c_str(), testMessage.c_str());
  if (pubResult) {
    Serial.println("✓ Mensaje de prueba publicado");
    Serial.println("Esperando recibirlo en el callback...");
    Serial.println("(Si el callback funciona, deberías ver '*** CALLBACK MQTT DISPARADO ***' arriba)");
  } else {
    Serial.println("✗ Error al publicar mensaje de prueba");
  }
  
  // Procesar mensajes varias veces para asegurar recepción
  for (int i = 0; i < 10; i++) {
    client.loop();
    delay(100);
  }
  
  Serial.println("=== FIN TEST ===");
}