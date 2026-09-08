# GasoilSensor

Sistema IoT basado en **ESP32-C3** para monitorización del nivel de gasoil de un depósito mediante un sensor radar y publicación de los datos mediante **MQTT** para su integración con **Home Assistant**.

El dispositivo está diseñado para funcionar con alimentación mediante baterías y minimizar el consumo utilizando **Deep Sleep**.

---

## Características

* ESP32-C3 SuperMini.
* Medición del nivel del depósito mediante sensor radar **HLK-LD2413 24 GHz**.
* Alimentación mediante 3 pilas de 1,5 V.
* Alimentación del sensor independiente de la del ESP32.
* Medición de la tensión de batería mediante divisor resistivo.
* Desconexión de la alimentación del sensor durante Deep Sleep.
* Conexión WiFi a una red de 2,4 GHz.
* Publicación de datos mediante MQTT.
* Integración con Home Assistant mediante MQTT.
* Configuración inicial mediante una interfaz web.
    * Modo Access Point para configuración.
    * Escaneo automático de redes WiFi disponibles.
    * Visualización de la intensidad de señal RSSI.
    * Selección de SSID desde la interfaz web.
    * Posibilidad de introducir manualmente redes WiFi ocultas.
    * Visualización/ocultación de la contraseña WiFi.
    * Configuración del intervalo de Deep Sleep.
    * Configuración de la altura del sensor.
    * Configuración parámetros MQTT
* Almacenamiento persistente de la configuración mediante NVS (`Preferences`).


---

## Arquitectura

El funcionamiento general del dispositivo es:

```text

                 ┌──────────────────────┐
                 │    ESP32-C3           │
                 │    SuperMini          │
                 └──────────┬───────────┘
                            │
                ┌───────────┴───────────┐
                │                       │
                ▼                       ▼
       ┌────────────────┐      ┌────────────────┐
       │ HLK-LD2413     │      │ Battery        │
       │ Radar 24 GHz   │      │ Voltage        │
       └────────────────┘      └────────────────┘
                │                       │
                └───────────┬───────────┘
                            │
                            ▼
                     ┌─────────────┐
                     │    WiFi     │
                     └──────┬──────┘
                            │
                            ▼
                     ┌─────────────┐
                     │ MQTT Broker │
                     │  Mosquitto  │
                     └──────┬──────┘
                            │
                            ▼
                     ┌─────────────┐
                     │ Home        │
                     │ Assistant   │
                     └─────────────┘


```

---

## Hardware

### Controlador

* ESP32-C3 SuperMini.

### Sensor

Sensor radar de 24 GHz:

**HLK-LD2413**

El sensor se alimenta únicamente durante el periodo necesario para realizar la medición. Durante Deep Sleep su alimentación se desconecta mediante la etapa de transistores.

### Alimentación

El dispositivo utiliza:

```text
3 x 1,5 V
   │
   ▼
Etapa de alimentación
   │
   ├──► ESP32-C3
   │
   └──► |Switch| ─► |divisor|
                 ─► Sensor radar
```

La alimentación del sensor se desconecta durante Deep Sleep para reducir el consumo mediante un switch construido con dos transistores (PNP y NPN) que controla el ESP32

### Medición de batería

La tensión de las baterías se mide mediante un divisor resistivo conectado a una entrada analógica del ESP32.

El divisor está conectado después del switch de transistores,para evitar consumo permanente de las baterías a través del divisor.

---

# Software

El proyecto está desarrollado utilizando:

* PlatformIO
* Visual Studio Code
* Arduino Framework
* ESP32-C3

Principales componentes software:

```text
WiFi
WebServer
DNSServer
Preferences
ESP32 Deep Sleep
MQTT
```

---

# Modo de funcionamiento

El dispositivo tiene dos modos principales:

1. Modo configuración.
2. Modo funcionamiento normal.

## Primer arranque

Al arrancar por primera vez:

```text
counter == 0
```

el dispositivo entra automáticamente en modo configuración.

En este modo crea un Access Point WiFi:

```text
GasoilSensor-XXXX
PWD: 12345678
```

y proporciona una interfaz web para configurar el dispositivo.




# Configuración WiFi

La página de configuración permite seleccionar la red WiFi.

Al abrir la página se realiza automáticamente un escaneo de redes disponibles.

Ejemplo:

```text
Red WiFi

┌──────────────────────────────────────┐
│ MiCasa_2.4G (-45 dBm - Excelente) ▼ │
└──────────────────────────────────────┘

[ 🔄 Actualizar redes ]
```

Las redes se muestran junto con su intensidad de señal RSSI.

También es posible introducir manualmente el SSID para redes ocultas.

La contraseña puede visualizarse u ocultarse mediante el botón correspondiente.

---

# Parámetros configurables

Actualmente se pueden configurar:

| Parámetro      | Descripción                                      |
| -------------- | ------------------------------------------------ |
| SSID           | Red WiFi a utilizar                              |
| Password       | Contraseña de la red WiFi                        |
| Sleep Interval | Intervalo entre mediciones  (minutos)            |
| Sensor Height  | Altura (cm) del sensor respecto al techo deposito|
| MQTT broker    | IP/URL Broker MQTT                               |
| MQTT port      | Port Broker MQTT                                 |
| MQTT username  | User MQTT                                        |
| MQTT password  | Passwrod user MQTT                               |



# Almacenamiento de configuración

La configuración se almacena en la memoria NVS del ESP32 mediante `Preferences`.
La configuración permanece almacenada aunque el dispositivo entre en Deep Sleep.

---

# Contador de ciclos

El proyecto utiliza:

```cpp
RTC_DATA_ATTR uint32_t counter = 0;
```

para mantener un contador entre ciclos de Deep Sleep.




# Timeout de configuración

El modo configuración permanece activo durante un tiempo limitado

Actualmente:

```text
120 segundos
```

Si durante ese tiempo no se guarda ninguna configuración, el dispositivo abandona el modo configuración y continúa con el funcionamiento normal.

---

# Funcionamiento normal

Una vez configurado, el ciclo de funcionamiento es:

```text
Wake-up
   │
   ▼
Activar sensor
   │
   ▼
Realizar medición
   │
   ▼
Medir tensión batería
   │
   ▼
Conectar WiFi
   │
   ▼
Conectar MQTT
   │
   ▼
Publicar datos
   │
   ▼
Desconectar MQTT
   │
   ▼
Desconectar WiFi
   │
   ▼
Desconectar sensor
   │
   ▼
Deep Sleep
   │
   └──────────────► Wake-up
```

El objetivo es mantener el ESP32 activo durante el menor tiempo posible para maximizar la autonomía de las baterías.

---

# MQTT

El dispositivo publica sus datos mediante MQTT.

Topic principal:

```text
devices/gasoil_sensor
```

El payload utiliza formato JSON.

Ejemplo:

```json
{
  "gasoil_level": 12,
  "battery_voltage": 4.12,
  "count": 15
}
```

Los campos principales son:

| Campo             | Descripción                 |
| ----------------- | --------------------------- |
| `gasoil_level`    | Nivel estimado de gasoil    |
| `battery_voltage` | Tensión de la batería       |
| `count`           | Número de ciclo de medición |

---

# Home Assistant

Los datos publicados mediante MQTT pueden integrarse en Home Assistant.

La arquitectura es:

```text
ESP32
  │
  │ MQTT
  ▼
Mosquitto
  │
  │ MQTT
  ▼
Home Assistant
```

El proyecto contempla además el uso de **MQTT Discovery** para facilitar la creación automática de las entidades correspondientes en Home Assistant.

---

# Cálculo del nivel

La altura física aproximada del depósito se utiliza como referencia para calcular el nivel.

Por ejemplo:

```text
Altura del sensor = 3 cm

Sensor
  |← Altura sensor
---------------  ← Altura deposito
  │
  │
  │  distancia medida
  │
  ▼
~~~~~~~~~~~~~~~  ← superficie del gasoil
  │
  │
  ▼
┌───────────────┐
│               │
│    GASOIL     │
│               │
└───────────────┘
---------------
```

El depósito no tiene necesariamente una geometría lineal, por lo que la conversión entre distancia medida y cantidad de gasoil es una aproximación.

La lógica de cálculo puede evolucionar posteriormente para incorporar una curva de calibración específica del depósito.

---

# Estructura del proyecto

La estructura prevista para PlatformIO es:

```text
GasoilSensor/
│
├── include/
│
├── lib/
│
├── src/
│   └── main.cpp
│
├── test/
│
├── platformio.ini
├── .gitignore
└── README.md
```

---

# Compilación

El proyecto utiliza PlatformIO.

Desde Visual Studio Code:

```text
PlatformIO
    │
    └── Build
```

O desde la línea de comandos:

```bash
pio run
```

Para cargar el firmware:

```bash
pio run --target upload
```

Para abrir el monitor serie:

```bash
pio device monitor
```

---

# Monitor serie

Durante el funcionamiento el ESP32 proporciona información de diagnóstico mediante el puerto serie.

Por ejemplo:

```text
======================
Gasoil Sensor
======================

Counter: 15

Configuracion cargada desde NVS

Ciclo de funcionamiento: 15

Conectando a WiFi...
WiFi conectado

IP: 192.168.1.120

Conectando a MQTT...
MQTT conectado

Publicando datos...

Entrando en Deep Sleep...
```

Durante el modo configuración se muestran también los parámetros recibidos y el resultado del escaneo WiFi.

---

# Seguridad

Las credenciales WiFi se almacenan en la memoria NVS del ESP32.

**No deben almacenarse credenciales reales dentro del código fuente ni subirse al repositorio Git.**

Antes de realizar un `git commit`, comprobar que no existen:

```text
SSID
Password
MQTT credentials
API keys
Tokens
```

hardcodeados en los archivos del proyecto.

---

# Dependencias

Las principales funcionalidades utilizan las librerías proporcionadas por el framework Arduino para ESP32:

```cpp
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_sleep.h>
```

La comunicación MQTT requiere la librería MQTT utilizada por el proyecto.

---

# Estado del proyecto

El proyecto se encuentra en desarrollo.

### Implementado

* [x] ESP32-C3
* [x] Lectura del sensor radar
* [x] Medición de batería
* [x] Deep Sleep
* [x] WiFi
* [x] MQTT
* [x] Configuración mediante página web
* [x] Access Point de configuración
* [x] Persistencia mediante Preferences/NVS
* [x] Contador mediante RTC memory
* [x] Escaneo de redes WiFi
* [x] RSSI
* [x] Selección de SSID
* [x] Introducción manual de SSID
* [x] Mostrar/ocultar contraseña
* [x] Timeout del modo configuración
* [x] Integración MQTT con Home Assistant

### Pendiente / mejoras futuras

* [ ] Mejorar la calibración del nivel de gasoil.
* [ ] Incorporar curva de calibración específica del depósito.
* [ ] Mejorar la gestión de errores de conexión WiFi.
* [ ] Mejorar la gestión de errores MQTT.
* [ ] Añadir watchdog.
* [ ] Añadir mecanismos de recuperación ante fallos.
* [ ] Optimizar consumo energético.
* [ ] Añadir información de estado en Home Assistant.
* [ ] Mejorar MQTT Discovery.
* [ ] Añadir versión del firmware.
* [ ] Implementar actualización OTA.

---

# Licencia

Pendiente de definir.

---

# Autor

Proyecto **GasoilSensor**
Autor: Manuel Baz

Desarrollado sobre ESP32-C3 utilizando PlatformIO y Arduino Framework.
