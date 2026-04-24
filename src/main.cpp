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

// Versi?n del firmware
#define FIRMWARE_VERSION "v1.1.1"

SensorData data;  // Estructura para almacenar los datos de temperatura y humedad del SHT21
time_t hora;      // Timestamp de la hora actual

/**
 * Configura el dispositivo para conectarse a la red WiFi y ajusta parametros IoT
 */
void setup() {
  Serial.begin(115200);     // Paso 1. Inicializa el puerto serie
  delay(1000);              // Espera a que el puerto serie se estabilice
  
  // Imprimir informaci?n del firmware al inicio
  // Usar la versi?n guardada en memoria no vol?til (si existe) o la constante por defecto
  String firmwareVersion = getFirmwareVersion();
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("  IoT MQTT TLS Device - firmware info");
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
  setupIoT();               // Paso 6. Inicializa el servicio de IoT
  hora = setTime();         // Paso 7. Ajusta el tiempo del dispositivo con servidores SNTP
  
  // Mostrar version al finalizar inicializacion (reutilizar variable ya declarada arriba)
  Serial.println();
  Serial.println("========================================");
  Serial.print("Sistema inicializado - Firmware>> ");
  Serial.println(firmwareVersion);
  Serial.println("========================================");
  Serial.println();
}

// Función loop
void loop() {
  if (isProvisioning()) {   // Si estamos en modo configuración, atender portal
    provisioningLoop();
    return;
  }
  checkWiFi();                                                   // Paso 1. Verifica la conexión a la red WiFi y si no está conectado, intenta reconectar
  checkMQTT();                                                   // Paso 2. Verifica la conexión al servidor MQTT y si no está conectado, intenta reconectar
  String message = checkAlert();                                 // Paso 3. Verifica si hay alertas y las retorna en caso de haberlas
  if(measure(&data)){                                            // Paso 4. Realiza una medición de temperatura y humedad
    displayLoop(message, hora, data.temperature, data.humidity); // Paso 5. Muestra en la pantalla el mensaje recibido, las medidas de temperatura y humedad
    sendSensorData(data.temperature, data.humidity);             // Paso 6. Envía los datos de temperatura y humedad al servidor MQTT
  }   
}
