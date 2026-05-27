/**
 * @file    libgps.h
 * @brief   Módulo GPS para rastreo de mascotas con NEO-6M
 * @details Define la estructura GPSData y las funciones para inicializar
 *          el módulo GPS por UART2, parsear tramas NMEA (GPRMC/GPGGA),
 *          y serializar las coordenadas a JSON para publicación MQTT.
 *          No utiliza librerías externas — el parser NMEA es propio.
 */

#ifndef LIBGPS_H
#define LIBGPS_H

#include <Arduino.h>

/**
 * @brief   Estructura que almacena los datos parseados del GPS
 * @details Contiene coordenadas en grados decimales, velocidad en km/h,
 *          altitud en metros, cantidad de satélites, estado del fix
 *          y timestamp UTC.
 */
typedef struct {
  float   latitude;    ///< Latitud en grados decimales (negativo = Sur)
  float   longitude;   ///< Longitud en grados decimales (negativo = Oeste)
  float   speed;       ///< Velocidad en km/h (convertida desde nudos)
  float   altitude;    ///< Altitud en metros sobre el nivel del mar
  int     satellites;  ///< Número de satélites en uso
  bool    valid;       ///< true = fix GPS válido (status 'A' en GPRMC)
  String  timestamp;   ///< Hora UTC en formato HH:MM:SS
} GPSData;

/**
 * @brief   Inicializa UART2 para comunicación con el módulo GPS
 * @details Configura HardwareSerial(2) a 9600 baud con GPIO16 (RX) y GPIO17 (TX).
 *          Debe llamarse una vez en setup() antes de readGPS().
 */
void setupGPS();

/**
 * @brief   Lee y parsea datos NMEA del GPS de forma no bloqueante
 * @details Consume los bytes disponibles en el buffer UART sin usar delay().
 *          Acumula caracteres en un buffer interno hasta recibir '\n',
 *          momento en el cual procesa la trama NMEA ($GPRMC o $GPGGA).
 * @param   data  Puntero a la estructura GPSData donde se almacenan los resultados
 * @return  true si hay un fix válido (status 'A' en GPRMC), false en caso contrario
 */
bool readGPS(GPSData* data);

/**
 * @brief   Imprime los datos GPS por Serial para debug
 * @param   data  Puntero a la estructura GPSData a imprimir
 */
void printGPS(GPSData* data);

/**
 * @brief   Serializa los datos GPS a formato JSON
 * @details Genera un JSON con los campos: device, lat, lon, alt, spd, sats, fix, ts.
 *          El campo "device" contiene la dirección MAC del ESP32.
 * @param   data  Puntero a la estructura GPSData a serializar
 * @return  String con el JSON serializado
 */
String gpsToJson(GPSData* data);

#endif /* LIBGPS_H */
