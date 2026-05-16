/**
 * @file    libdisplay.cpp
 * @brief   Implementación del módulo de pantalla OLED para rastreador de mascotas
 * @details Muestra datos GPS (coordenadas, velocidad, altitud, satélites)
 *          en la pantalla OLED SSD1306 128x64 por I2C. Incluye dos layouts:
 *          uno para cuando hay fix GPS válido y otro para cuando se están
 *          buscando satélites.
 *
 *          Se removieron las funciones de visualización de temperatura/humedad
 *          (displayHeader, displayMeasures, displayMessage, displayLoop).
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

#include <libdisplay.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // Pantalla OLED vinculada al dispositivo

/**
 * @brief   Vincula la pantalla al dispositivo y asigna el color de texto blanco como predeterminado.
 * @details Si no es exitosa la vinculación, se muestra un mensaje en consola
 *          y el programa se detiene indefinidamente.
 */
void startDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Dirección 0x3D para 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // No continúa si no se puede vincular la pantalla
  }
  display.setTextColor(SSD1306_WHITE); // Color de texto blanco
}

/**
 * @brief   Imprime en la pantalla un mensaje de "No hay señal".
 */
void displayNoSignal() {
  display.clearDisplay(); // Limpia la pantalla
  display.setTextSize(2); // Tamaño de texto 2
  display.setCursor(10, 10); // Posición del cursor
  display.println("No hay señal"); 
  display.display();
}

/**
 * @brief   Muestra en la pantalla el mensaje de "Conectando a:"
 *          y luego el nombre de la red a la que se conecta.
 * @param   ssid  Nombre de la red WiFi
 */
void displayConnecting(String ssid) {
  display.clearDisplay();      // Limpia la pantalla
  display.setTextSize(1);      // Tamaño de texto 1
  display.println("Conectando a:\n"); 
  display.println(ssid);      // Se imprime el nombre de la red
  display.display();          // Se muestra el contenido en la pantalla
}

/**
 * @brief   Muestra los datos GPS en la pantalla OLED 128x64
 * @details Usa tamaño de texto 1 (6x8 px por carácter) para aprovechar
 *          las 8 líneas disponibles (64px / 8px = 8 líneas).
 *
 *          Si hay fix válido, muestra:
 *          - Línea 0: Título "=== PetTracker ="
 *          - Línea 1: Latitud con 5 decimales
 *          - Línea 2: Longitud con 5 decimales
 *          - Línea 3: Velocidad en km/h
 *          - Línea 4: Satélites y altitud
 *          - Línea 5: Timestamp UTC (HH:MM:SS)
 *
 *          Si NO hay fix, muestra:
 *          - Título, mensaje "Buscando sats" y conteo de satélites visibles
 *
 * @param   data  Puntero a la estructura GPSData con los datos a mostrar
 */
void displayGPSLoop(GPSData* data) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);  // Cada carácter ocupa 6x8 px → 21 chars × 8 líneas

  // Línea 0: Título
  display.println("=== PetTracker =");

  if (data->valid) {
    // ── Layout con fix válido ──────────────────────────────────────

    // Línea 1: Latitud (5 decimales)
    display.print("Lat: ");
    display.println(data->latitude, 5);

    // Línea 2: Longitud (5 decimales)
    display.print("Lon:");
    display.println(data->longitude, 5);

    // Línea 3: Velocidad en km/h
    display.print("Vel: ");
    display.print(data->speed, 1);
    display.println(" km/h");

    // Línea 4: Satélites y altitud compactos
    display.print("Sats:");
    display.print(data->satellites);
    display.print("  Alt:");
    display.print((int)data->altitude);
    display.println("m");

    // Línea 5: Timestamp UTC
    display.println(data->timestamp);

  } else {
    // ── Layout sin fix (buscando satélites) ────────────────────────
    display.println("");          // Línea 1: vacía
    display.println(" Buscando sats");  // Línea 2: mensaje centrado
    display.println("");          // Línea 3: vacía
    display.print(" Sats visibles:");
    display.println(data->satellites);
  }

  display.display();  // Enviar el buffer a la pantalla
}