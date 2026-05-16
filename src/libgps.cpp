/**
 * @file    libgps.cpp
 * @brief   Implementación del parser NMEA y comunicación UART con módulo GPS NEO-6M
 * @details Este módulo implementa un parser NMEA propio (sin librerías externas)
 *          para las tramas $GPRMC y $GPGGA. La lectura es completamente no bloqueante:
 *          solo consume los bytes disponibles en el buffer UART en cada llamada.
 *
 *          Algoritmo de acumulación del buffer NMEA:
 *          - Se mantiene un buffer estático char[128] y un índice.
 *          - Cada byte recibido se evalúa:
 *            · '$' → resetea el índice a 0 (inicio de nueva trama)
 *            · '\n' → marca fin de trama, se procesa el contenido acumulado
 *            · Otro → se acumula en el buffer si hay espacio
 *          - Esto permite que readGPS() se llame repetidamente en loop()
 *            sin bloquear, procesando solo los bytes que estén disponibles.
 *
 *          Conversión NMEA a grados decimales:
 *          - NMEA reporta coordenadas en formato DDDMM.MMMM
 *            donde DDD son grados y MM.MMMM son minutos decimales.
 *          - Fórmula: grados_decimales = DDD + (MM.MMMM / 60.0)
 *          - Hemisferio Sur ('S') → latitud negativa
 *          - Hemisferio Oeste ('W') → longitud negativa
 */

#include "libgps.h"
#include <libiot.h>  // Para getMacAddress()

/// UART2 del ESP32-S3 para comunicación con el módulo GPS
static HardwareSerial GPSSerial(2);

/// Pines UART2: TX del GPS → GPIO16 (RX del ESP32), RX del GPS → GPIO17 (TX del ESP32)
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD   9600

// ── Buffer NMEA ──────────────────────────────────────────────────────
// Se usa un buffer estático de 128 bytes para acumular una trama NMEA.
// El tamaño es suficiente ya que las tramas NMEA estándar tienen máximo 82 caracteres.
static char nmeaBuffer[128];
static int  nmeaIndex = 0;

// ── Prototipos internos ──────────────────────────────────────────────
static void  processNMEA(const char* sentence, GPSData* data);
static void  parseGPRMC(const char* sentence, GPSData* data);
static void  parseGPGGA(const char* sentence, GPSData* data);
static float nmeaToDegrees(const char* raw, char hemisphere);
static int   getField(const char* sentence, int fieldIndex, char* out, int outSize);

// ═════════════════════════════════════════════════════════════════════

/**
 * @brief   Inicializa UART2 para comunicación con el módulo GPS
 * @details Configura HardwareSerial(2) a 9600 baud en los pines GPIO16 (RX)
 *          y GPIO17 (TX). El módulo NEO-6M transmite tramas NMEA a esta velocidad.
 */
void setupGPS() {
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[GPS] UART2 inicializado → 9600 baud, RX=GPIO16, TX=GPIO17");
}

/**
 * @brief   Lee y parsea datos NMEA de forma no bloqueante
 * @details Consume todos los bytes disponibles en el buffer UART2 sin usar delay().
 *          Por cada byte:
 *          - '$': reinicia el buffer (inicio de nueva trama NMEA)
 *          - '\n': procesa la trama completa acumulada
 *          - otro: acumula en el buffer si no se ha llenado
 *
 *          Las tramas procesadas son $GPRMC (posición, velocidad, timestamp, fix)
 *          y $GPGGA (altitud, satélites). Los datos se acumulan en la estructura
 *          GPSData apuntada por el parámetro data.
 *
 * @param   data  Puntero a estructura GPSData donde se guardan los resultados
 * @return  true si el último fix parseado es válido (status 'A' en GPRMC)
 */
bool readGPS(GPSData* data) {
  // Consumir todos los bytes disponibles en el buffer UART (no bloqueante)
  while (GPSSerial.available()) {
    char c = GPSSerial.read();

    if (c == '$') {
      // Inicio de nueva trama NMEA: reiniciar buffer
      nmeaIndex = 0;
    }

    if (nmeaIndex < (int)(sizeof(nmeaBuffer) - 1)) {
      nmeaBuffer[nmeaIndex++] = c;
    }

    if (c == '\n') {
      // Fin de trama: agregar terminador y procesar
      nmeaBuffer[nmeaIndex] = '\0';
      processNMEA(nmeaBuffer, data);
      nmeaIndex = 0;
    }
  }

  return data->valid;
}

/**
 * @brief   Imprime los datos GPS actuales por Serial (debug)
 * @param   data  Puntero a la estructura GPSData a imprimir
 */
void printGPS(GPSData* data) {
  Serial.println("──── GPS Data ────");
  if (data->valid) {
    Serial.print("  Lat: ");  Serial.println(data->latitude, 6);
    Serial.print("  Lon: ");  Serial.println(data->longitude, 6);
    Serial.print("  Vel: ");  Serial.print(data->speed, 1); Serial.println(" km/h");
    Serial.print("  Alt: ");  Serial.print(data->altitude, 1); Serial.println(" m");
    Serial.print("  Sats: "); Serial.println(data->satellites);
    Serial.print("  UTC: ");  Serial.println(data->timestamp);
  } else {
    Serial.print("  Sin fix (sats visibles: ");
    Serial.print(data->satellites);
    Serial.println(")");
  }
  Serial.println("──────────────────");
}

/**
 * @brief   Serializa los datos GPS a JSON para publicación MQTT
 * @details Formato del JSON publicado:
 *          {
 *            "device": "ESP32-XXXXXXXXXXXX",  ← MAC del dispositivo
 *            "lat": 4.123456,                 ← grados decimales
 *            "lon": -76.123456,               ← grados decimales
 *            "alt": 1023.5,                   ← metros
 *            "spd": 2.3,                      ← km/h
 *            "sats": 8,                       ← satélites en uso
 *            "fix": true,                     ← estado del fix
 *            "ts": "14:32:01"                 ← hora UTC
 *          }
 * @param   data  Puntero a la estructura GPSData a serializar
 * @return  String JSON con exactamente los campos: device, lat, lon, alt, spd, sats, fix, ts
 */
String gpsToJson(GPSData* data) {
  // Se construye el JSON manualmente para evitar dependencias adicionales
  // y tener control total sobre el formato y la precisión de los valores.
  String json = "{";
  json += "\"device\":\"" + getMacAddress() + "\",";
  json += "\"lat\":" + String(data->latitude, 6) + ",";
  json += "\"lon\":" + String(data->longitude, 6) + ",";
  json += "\"alt\":" + String(data->altitude, 1) + ",";
  json += "\"spd\":" + String(data->speed, 1) + ",";
  json += "\"sats\":" + String(data->satellites) + ",";
  json += "\"fix\":" + String(data->valid ? "true" : "false") + ",";
  json += "\"ts\":\"" + data->timestamp + "\"";
  json += "}";
  return json;
}

// ═════════════════════════════════════════════════════════════════════
// FUNCIONES INTERNAS (static)
// ═════════════════════════════════════════════════════════════════════

/**
 * @brief   Identifica el tipo de trama NMEA y la despacha al parser correspondiente
 * @details Solo se procesan tramas $GPRMC y $GPGGA. Las demás se ignoran.
 *          También acepta $GNRMC y $GNGGA (GNSS multi-constelación).
 * @param   sentence  Cadena NMEA completa (desde '$' hasta '\n')
 * @param   data      Puntero a GPSData donde se almacenan los resultados
 */
static void processNMEA(const char* sentence, GPSData* data) {
  // Verificar que empieza con '$'
  if (sentence[0] != '$') return;

  // Comparar los primeros caracteres para identificar el tipo de trama
  // Se aceptan tanto GP (GPS) como GN (GNSS multi-constelación)
  if (strstr(sentence, "RMC") != NULL &&
      (strncmp(sentence + 1, "GP", 2) == 0 || strncmp(sentence + 1, "GN", 2) == 0)) {
    parseGPRMC(sentence, data);
  } else if (strstr(sentence, "GGA") != NULL &&
             (strncmp(sentence + 1, "GP", 2) == 0 || strncmp(sentence + 1, "GN", 2) == 0)) {
    parseGPGGA(sentence, data);
  }
}

/**
 * @brief   Extrae un campo de una trama NMEA separada por comas
 * @details Los campos NMEA están separados por ','. Esta función copia
 *          el campo en la posición fieldIndex al buffer out.
 * @param   sentence    Trama NMEA completa
 * @param   fieldIndex  Índice del campo a extraer (0 = identificador de trama)
 * @param   out         Buffer de salida donde se copia el campo
 * @param   outSize     Tamaño máximo del buffer de salida
 * @return  Longitud del campo extraído, o 0 si no se encontró
 */
static int getField(const char* sentence, int fieldIndex, char* out, int outSize) {
  int currentField = 0;
  int i = 0;
  int outIdx = 0;

  while (sentence[i] != '\0') {
    if (sentence[i] == ',' || sentence[i] == '*') {
      if (currentField == fieldIndex) {
        out[outIdx] = '\0';
        return outIdx;
      }
      currentField++;
      outIdx = 0;
    } else {
      if (currentField == fieldIndex && outIdx < outSize - 1) {
        out[outIdx++] = sentence[i];
      }
    }
    i++;
  }

  // Último campo (si termina sin ',' ni '*')
  if (currentField == fieldIndex) {
    out[outIdx] = '\0';
    return outIdx;
  }

  out[0] = '\0';
  return 0;
}

/**
 * @brief   Convierte coordenadas NMEA (DDDMM.MMMM) a grados decimales
 * @details La conversión NMEA a grados decimales funciona así:
 *          - NMEA usa formato DDDMM.MMMM (grados + minutos decimales)
 *          - Se extraen los grados enteros: DDD = (int)(valor / 100)
 *          - Se extraen los minutos: MM.MMMM = valor - (DDD * 100)
 *          - Resultado: grados_decimales = DDD + (MM.MMMM / 60.0)
 *          - Si el hemisferio es 'S' o 'W', el resultado es negativo
 *
 *          Ejemplo: 0407.3850, N
 *          → DDD = 4, MM.MMMM = 07.3850
 *          → 4 + (7.3850 / 60) = 4.123083°
 *
 * @param   raw         Cadena con el valor NMEA (ej: "0407.3850")
 * @param   hemisphere  Carácter de hemisferio: 'N','S','E','W'
 * @return  Coordenada en grados decimales (negativa para S y W)
 */
static float nmeaToDegrees(const char* raw, char hemisphere) {
  if (raw[0] == '\0') return 0.0f;

  float value = atof(raw);
  // Extraer grados enteros (parte entera de valor/100)
  int degrees = (int)(value / 100);
  // Extraer minutos decimales (resto)
  float minutes = value - (degrees * 100);
  // Convertir: grados + minutos/60
  float result = degrees + (minutes / 60.0f);

  // Hemisferio Sur u Oeste → coordenada negativa
  if (hemisphere == 'S' || hemisphere == 'W') {
    result = -result;
  }

  return result;
}

/**
 * @brief   Parsea una trama $GPRMC para extraer posición, velocidad y timestamp
 * @details Campos de $GPRMC:
 *          0: $GPRMC
 *          1: Hora UTC (HHMMSS.SSS)
 *          2: Status ('A' = válido, 'V' = inválido)
 *          3: Latitud (DDMM.MMMM)
 *          4: N/S
 *          5: Longitud (DDDMM.MMMM)
 *          6: E/W
 *          7: Velocidad en nudos
 *          8: Curso
 *          9: Fecha (DDMMYY)
 *
 *          La velocidad se convierte de nudos a km/h: nudos × 1.852
 *
 * @param   sentence  Trama $GPRMC completa
 * @param   data      Puntero a GPSData donde se guardan los resultados
 */
static void parseGPRMC(const char* sentence, GPSData* data) {
  char field[20];

  // Campo 1: Hora UTC → formato HH:MM:SS
  if (getField(sentence, 1, field, sizeof(field)) >= 6) {
    // field contiene "HHMMSS.SSS", extraer HH:MM:SS
    char ts[9];
    ts[0] = field[0]; ts[1] = field[1]; // HH
    ts[2] = ':';
    ts[3] = field[2]; ts[4] = field[3]; // MM
    ts[5] = ':';
    ts[6] = field[4]; ts[7] = field[5]; // SS
    ts[8] = '\0';
    data->timestamp = String(ts);
  }

  // Campo 2: Status → 'A' = fix válido, 'V' = sin fix
  if (getField(sentence, 2, field, sizeof(field)) > 0) {
    data->valid = (field[0] == 'A');
  }

  // Campo 3 y 4: Latitud + hemisferio N/S
  char latRaw[20], latHem[4];
  if (getField(sentence, 3, latRaw, sizeof(latRaw)) > 0 &&
      getField(sentence, 4, latHem, sizeof(latHem)) > 0) {
    data->latitude = nmeaToDegrees(latRaw, latHem[0]);
  }

  // Campo 5 y 6: Longitud + hemisferio E/W
  char lonRaw[20], lonHem[4];
  if (getField(sentence, 5, lonRaw, sizeof(lonRaw)) > 0 &&
      getField(sentence, 6, lonHem, sizeof(lonHem)) > 0) {
    data->longitude = nmeaToDegrees(lonRaw, lonHem[0]);
  }

  // Campo 7: Velocidad en nudos → convertir a km/h (nudos × 1.852)
  if (getField(sentence, 7, field, sizeof(field)) > 0) {
    float knots = atof(field);
    data->speed = knots * 1.852f;
  }
}

/**
 * @brief   Parsea una trama $GPGGA para extraer altitud y satélites
 * @details Campos de $GPGGA:
 *          0: $GPGGA
 *          1: Hora UTC
 *          2: Latitud
 *          3: N/S
 *          4: Longitud
 *          5: E/W
 *          6: Calidad del fix (0=inválido, 1=GPS, 2=DGPS)
 *          7: Número de satélites en uso
 *          8: HDOP
 *          9: Altitud sobre nivel del mar (metros)
 *          10: Unidad de altitud (M)
 *
 * @param   sentence  Trama $GPGGA completa
 * @param   data      Puntero a GPSData donde se guardan los resultados
 */
static void parseGPGGA(const char* sentence, GPSData* data) {
  char field[20];

  // Campo 7: Número de satélites en uso
  if (getField(sentence, 7, field, sizeof(field)) > 0) {
    data->satellites = atoi(field);
  }

  // Campo 9: Altitud en metros sobre el nivel del mar
  if (getField(sentence, 9, field, sizeof(field)) > 0) {
    data->altitude = atof(field);
  }
}
