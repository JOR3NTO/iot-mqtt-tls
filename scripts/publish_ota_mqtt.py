#!/usr/bin/env python3
"""Publish OTA firmware update via MQTT with TLS support."""

import os
import json
import sys
import paho.mqtt.publish as publish

def main():
    # Debug: verificar variables
    print("DEBUG - Verificando variables de entorno:")
    print(f"  MQTT_SERVER: {os.getenv('MQTT_SERVER')}")
    print(f"  MQTT_PORT: {os.getenv('MQTT_PORT')}")
    print(f"  MQTT_USER: {'***' if os.getenv('MQTT_USER') else 'NOT SET'}")
    print(f"  MQTT_PASSWORD: {'***' if os.getenv('MQTT_PASSWORD') else 'NOT SET'}")
    print(f"  MQTT_TLS: {os.getenv('MQTT_TLS', 'false')}")

    # Validar que no sean vacíos/None
    if not os.getenv('MQTT_SERVER') or not os.getenv('MQTT_PORT'):
        print("ERROR: MQTT_SERVER y MQTT_PORT son requeridos")
        sys.exit(1)

    try:
        # Construir URL de S3
        url = f"https://{os.getenv('S3_BUCKET_NAME')}.s3.{os.getenv('AWS_REGION')}.amazonaws.com/{os.getenv('FIRMWARE_NAME')}"
        
        # Construir payload
        payload = json.dumps({
            'version': os.getenv('FIRMWARE_VERSION'),
            'url': url
        })

        # Construir auth
        auth = None
        if os.getenv('MQTT_USER'):
            auth = {
                'username': os.getenv('MQTT_USER'),
                'password': os.getenv('MQTT_PASSWORD')
            }
            print("DEBUG - Usando autenticación MQTT")
        else:
            print("DEBUG - Conexión sin autenticación")

        # Construir TLS
        tls_params = None
        if os.getenv('MQTT_TLS', 'false').lower() == 'true':
            tls_params = {
                'ca_certs': None,
                'certfile': None,
                'keyfile': None,
                'cert_reqs': 1,
                'tls_version': 4,
                'ciphers': None
            }
            print("DEBUG - Usando TLS")
        else:
            print("DEBUG - Sin TLS")

        print("DEBUG - Enviando mensaje MQTT...")
        publish.single(
            topic=os.getenv('DEVICE_TOPIC'),
            payload=payload,
            hostname=os.getenv('MQTT_SERVER'),
            port=int(os.getenv('MQTT_PORT')),
            auth=auth,
            tls=tls_params
        )
        print('✅ Mensaje OTA enviado correctamente')
        sys.exit(0)

    except Exception as e:
        print(f'❌ Error: {type(e).__name__}: {e}')
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
