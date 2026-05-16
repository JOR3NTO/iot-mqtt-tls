/**
 * @file    main.cpp
 * @brief   Punto de entrada del rastreador GPS de mascotas (PetTracker)
 * @details Inicializa WiFi, MQTT/TLS, GPS, OLED y OTA. En el loop principal
 *          lee coordenadas GPS de forma no bloqueante y las publica al broker
 *          EMQX cada GPS_PUBLISH_INTERVAL milisegundos.
 *
 *          El intervalo de publicación es de 5 segundos (5000 ms) para mantener
 *          un balance entre resolución de rastreo y uso de ancho de banda.
 *          La lectura del GPS se hace en cada iteración del loop sin bloquear,
 *          mientras que la publicación MQTT y actualización del OLED solo se
 *          ejecutan cada 5 segundos para no saturar el broker.
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

#include <WiFi.h>
#include <libiot.h>
#include <libwifi.h>
#include <libdisplay.h>
#include <libota.h>
#include <libstorage.h>
#include <libprovision.h>
#include <libgps.h>

// Versión del firmware
#define FIRMWARE_VERSION "v2.0.0"

/// Intervalo de publicación GPS por MQTT (en milisegundos).
/// 5 segundos ofrece un buen balance entre resolución de rastreo y
/// uso de ancho de banda en el broker EMQX.
#define GPS_PUBLISH_INTERVAL 5000

/// Estructura global para almacenar las coordenadas GPS parseadas
GPSData gpsData;

/// Timestamp de la última publicación MQTT (para temporización no bloqueante)
static unsigned long lastPublish = 0;

/**
 * @brief   Configura el dispositivo: WiFi, MQTT/TLS, GPS, OLED, OTA
 * @details Secuencia de inicialización:
 *          1. Serial a 115200 baud
 *          2. Verificar factory reset (botón BOOT por 3s)
 *          3. Escanear redes WiFi
 *          4. Inicializar pantalla OLED
 *          5. Provisioning AP si no hay credenciales WiFi
 *          6. Conectar WiFi
 *          7. Inicializar MQTT/TLS (setupIoT)
 *          8. Inicializar GPS UART2 (setupGPS)
 */
void setup() {
  Serial.begin(115200);     // Paso 1. Inicializa el puerto serie
  delay(1000);              // Espera a que el puerto serie se estabilice
  
  // Imprimir información del firmware al inicio
  // Usar la versión guardada en memoria no volátil (si existe) o la constante por defecto
  String firmwareVersion = getFirmwareVersion();
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("  PetTracker GPS - firmware info");
  Serial.print("  Firmware Version: ");
  Serial.println(firmwareVersion);
  Serial.println("========================================");
  Serial.println();
  
  // Factory reset si el botón BOOT (GPIO0) está presionado al arrancar
  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) == LOW) {
    unsigned long t0 = millis();
    while (digitalRead(0) == LOW && (millis() - t0) < 3000) {
      delay(10);
    }
    if ((millis() - t0) >= 3000) {
      factoryReset();
    }
  }
  listWiFiNetworks();       // Paso 2. Lista las redes WiFi disponibles
  delay(1000);              // -- Espera 1 segundo para ver las redes disponibles
  startDisplay();           // Paso 3. Inicializa la pantalla OLED
  // Si no hay credenciales, iniciar modo provisioning (AP)
  if (!hasWiFiCredentials()) {
    displayConnecting("Modo Configuracion AP");
    startProvisioningAP();
    return; // el loop manejará el portal
  }
  // Mostrar SSID que se intentará usar
  String showSsid;
  String tmpPwd;
  if (loadWiFiCredentials(showSsid, tmpPwd)) {
    displayConnecting(showSsid.c_str());
  } else {
    displayConnecting(ssid);
  }
  startWiFi("");            // Paso 5. Inicializa el servicio de WiFi
  setupIoT();               // Paso 6. Inicializa el servicio de IoT (MQTT/TLS)
  setupGPS();               // Paso 7. Inicializa UART2 para GPS NEO-6M
  
  // Inicializar estructura GPS con valores por defecto
  gpsData.latitude   = 0.0f;
  gpsData.longitude  = 0.0f;
  gpsData.speed      = 0.0f;
  gpsData.altitude   = 0.0f;
  gpsData.satellites = 0;
  gpsData.valid      = false;
  gpsData.timestamp  = "00:00:00";

  // Mostrar versión al finalizar inicialización
  Serial.println();
  Serial.println("========================================");
  Serial.print("Sistema inicializado - Firmware>> ");
  Serial.println(firmwareVersion);
  Serial.println("GPS UART2 configurado (9600 baud)");
  Serial.println("========================================");
  Serial.println();
}

/**
 * @brief   Loop principal del rastreador de mascotas
 * @details Ejecuta las siguientes tareas en cada iteración:
 *          1. Si estamos en modo provisioning, atender el portal web
 *          2. Verificar conexión WiFi (reconectar si es necesario)
 *          3. Verificar conexión MQTT (reconectar si es necesario)
 *          4. Leer GPS de forma no bloqueante (siempre, en cada iteración)
 *          5. Cada GPS_PUBLISH_INTERVAL ms:
 *             - Actualizar pantalla OLED con datos GPS
 *             - Publicar datos GPS por MQTT al broker EMQX
 *
 *          readGPS() se llama en cada iteración para no perder bytes del buffer
 *          UART. La publicación y display se hacen cada 5 segundos para no
 *          saturar el broker ni parpadear la pantalla demasiado rápido.
 */
void loop() {
  if (isProvisioning()) {   // Si estamos en modo configuración, atender portal
    provisioningLoop();
    return;
  }
  checkWiFi();              // Paso 1. Verifica conexión WiFi, reconecta si es necesario
  checkMQTT();              // Paso 2. Verifica conexión MQTT, reconecta si es necesario

  // Paso 3. Leer GPS de forma no bloqueante (siempre, cada iteración del loop).
  // Esto consume los bytes disponibles en el buffer UART2 y parsea tramas NMEA.
  // No usa delay() — solo procesa lo que haya disponible en ese momento.
  readGPS(&gpsData);

  // Paso 4. Cada GPS_PUBLISH_INTERVAL ms: actualizar OLED y publicar MQTT
  if (millis() - lastPublish >= GPS_PUBLISH_INTERVAL) {
    lastPublish = millis();
    displayGPSLoop(&gpsData);   // Actualiza la pantalla OLED con datos GPS
    sendGPSData(&gpsData);      // Publica coordenadas GPS al broker EMQX vía MQTT/TLS
  }
}
