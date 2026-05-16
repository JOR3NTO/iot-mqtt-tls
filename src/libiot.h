/**
 * @file    libiot.h
 * @brief   Módulo IoT: conexión MQTT/TLS, alertas, y publicación de datos GPS
 * @details Gestiona la conexión segura al broker EMQX vía TLS, la suscripción
 *          a tópicos MQTT, la recepción de alertas y comandos OTA, y la
 *          publicación de coordenadas GPS del rastreador de mascotas.
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

#ifndef LIBIOT_H
#define LIBIOT_H

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include <Arduino.h>
#include <libgps.h>

#define ALERT_DURATION 60           ///< Duración aproximada en la pantalla de las alertas que se reciban

extern const char* MQTT_TOPIC_PUB; ///< El tópico de publicación: <país>/<estado>/<ciudad>/<usuario>/out
extern const char* MQTT_TOPIC_SUB; ///< El tópico de suscripción: <país>/<estado>/<ciudad>/<usuario>/in
extern const char* mqtt_server;     ///< Dirección del servidor MQTT (broker EMQX)
extern const int mqtt_port;         ///< Puerto seguro (TLS) — típicamente 8883
extern const char* mqtt_user;       ///< Usuario MQTT
extern const char* mqtt_password;   ///< Contraseña MQTT
extern const char* root_ca;         ///< Certificado raíz de la CA en formato PEM
extern WiFiClientSecure espClient;  ///< Conexión TLS/SSL
extern PubSubClient client;         ///< Cliente MQTT

extern time_t now;                  ///< Timestamp de la fecha actual
extern long long int alertTime;     ///< Tiempo en que inició la última alerta
extern String alert;                ///< Mensaje de alerta para mostrar en la pantalla

/**
 * @brief   Ajusta el tiempo del dispositivo con servidores SNTP
 * @return  Timestamp actual (epoch)
 */
time_t setTime();

/**
 * @brief   Verifica la conexión MQTT y reconecta si es necesario
 */
void checkMQTT();

/**
 * @brief   Reconecta al broker MQTT usando las credenciales configuradas
 */
void reconnect();

/**
 * @brief   Configura el certificado raíz, servidor MQTT y puerto
 * @details Inicializa I2C, TLS, callback de recepción y sincroniza la hora.
 */
void setupIoT();

/**
 * @brief   Verifica si ha llegado alguna alerta al dispositivo
 * @return  El mensaje de alerta, o "OK" si no hay alertas activas
 */
String checkAlert();

/**
 * @brief   Callback ejecutado al recibir un mensaje MQTT
 * @param   topic   Tópico del mensaje recibido
 * @param   payload Contenido del mensaje
 * @param   length  Longitud del payload en bytes
 */
void receivedCallback(char* topic, byte* payload, unsigned int length);

/**
 * @brief   Publica los datos GPS al tópico MQTT configurado
 * @details Siempre publica, incluso si fix es inválido (fix:false),
 *          para que Grafana pueda detectar la pérdida de señal GPS.
 *
 *          Formato del JSON publicado:
 *          {
 *            "device": "<MAC>",
 *            "lat": 4.123456,
 *            "lon": -76.123456,
 *            "alt": 1023.5,
 *            "spd": 2.3,
 *            "sats": 8,
 *            "fix": true,
 *            "ts": "14:32:01"
 *          }
 *
 * @param   data  Puntero a la estructura GPSData con los datos a publicar
 */
void sendGPSData(GPSData* data);

/**
 * @brief   Obtiene la dirección MAC del ESP32 en formato cadena
 * @return  String con formato "ESP32-XXXXXXXXXXXX"
 */
String getMacAddress();

#endif /* LIBIOT_H */
