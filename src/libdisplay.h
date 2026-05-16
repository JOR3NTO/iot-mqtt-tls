/**
 * @file    libdisplay.h
 * @brief   Módulo de pantalla OLED SSD1306 para rastreador de mascotas
 * @details Gestiona la pantalla OLED I2C 128x64 para mostrar datos GPS
 *          (coordenadas, velocidad, altitud, satélites) y mensajes de estado.
 *          Se removieron las funciones de visualización de temperatura/humedad.
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

#ifndef LIBDISPLAY_H
#define LIBDISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <libgps.h>

#define SCREEN_WIDTH 128    ///< Ancho de la pantalla (en pixeles)
#define SCREEN_HEIGHT 64    ///< Alto de la pantalla (en pixeles)

extern Adafruit_SSD1306 display; ///< Pantalla OLED vinculada al dispositivo

/**
 * @brief   Vincula la pantalla al dispositivo y asigna el color de texto blanco
 */
void startDisplay();

/**
 * @brief   Imprime en la pantalla un mensaje de "No hay señal"
 */
void displayNoSignal();

/**
 * @brief   Muestra en la pantalla el mensaje de "Conectando a:" con el SSID
 * @param   ssid  Nombre de la red WiFi a la que se conecta
 */
void displayConnecting(String ssid);

/**
 * @brief   Muestra los datos GPS en la pantalla OLED 128x64
 * @details Layout con fix válido:
 *          ┌────────────────┐
 *          │=== PetTracker =│
 *          │Lat: 4.12345    │  ← 5 decimales
 *          │Lon:-76.12345   │  ← 5 decimales
 *          │Vel: 2.3 km/h   │
 *          │Sats:8  Alt:NNNm│
 *          │HH:MM:SS        │
 *          └────────────────┘
 *
 *          Layout sin fix:
 *          ┌────────────────┐
 *          │=== PetTracker =│
 *          │                │
 *          │ Buscando sats  │
 *          │                │
 *          │ Sats visibles:N│
 *          └────────────────┘
 *
 * @param   data  Puntero a la estructura GPSData con los datos a mostrar
 */
void displayGPSLoop(GPSData* data);

#endif /* LIBDISPLAY_H */