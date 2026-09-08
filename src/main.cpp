#include "HLK_LD2413.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "credentials.h"

// Pines de Hardware
#define SENSOR_POWER_PIN 6 // GPIO para controlar el transistor (PNP)
#define BATT_ADC_PIN     0 // GPIO analógico para leer el divisor de tensión de la bateria (WARN, mejor elegir otro porque este PIN se usa en boot)
#define LED_PIN          8 // GPIO para el LED
#define UART1_RX_PIN     4 // GPIO para RX de UART1
#define UART1_TX_PIN     3 // GPIO para TX de UART1

HLK_LD2413 sensor;
Preferences preferences;


// Configuración de red (IP Estática para ahorrar batería de 2 a 3 segundos)

IPAddress local_IP(192, 168, 1, 200); // Cambia según tu subred
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Configuración MQTT
// ---------- Topics ----------
const char* MQTT_DATA_TOPIC =
    "devices/gasoil_sensor";

const char* MQTT_HA_DISCOVERY_LEVEL =
    "homeassistant/sensor/gasoil_sensor_level/config";

const char* MQTT_HA_DISCOVERY_BATTERY =
    "homeassistant/sensor/gasoil_sensor_battery/config";

const char* MQTT_HA_DISCOVERY_COUNT =
    "homeassistant/sensor/gasoil_sensor_count/config";

// Puntos de acceso
const char* AP_SSID = "GasoilSensor";
const char* AP_PASSWORD = "12345678";

#define CONFIG_TIMEOUT_MS 120000UL       // 2 minutos
#define DNS_PORT 53

// Deep sleep utiliza microsegundos
#define US_TO_S 1000000ULL


// Tiempo de sueño profundo: 24 horas (en microsegundos)
//#define LONG_TIME_TO_SLEEP  86400000000ULL

// 12 horas
//#define LONG_TIME_TO_SLEEP  43200000000ULL

// 1 hora
//#define LONG_TIME_TO_SLEEP  3600000000ULL
                      
// 30 segundos
#define SHORT_TIME_TO_SLEEP 30000000ULL

// ============================================================
// CALIBRACIÓN DEL DEPÓSITO
// ============================================================
//
// IMPORTANTE:
//
// Estos valores son de ejemplo.
// Tendremos que sustituirlos por los valores reales
// obtenidos durante la calibración.
//
// distance_mm = distancia entre LD2413 y superficie
//
// level_percent = porcentaje de llenado  o litros
//
// Como el depósito NO es lineal, podemos poner tantos
// puntos como necesitemos.
//
// La tabla debe estar ordenada de menor distancia
// a mayor distancia.
//

//#define HEIGHT_SENSOR 30 // Altura del sensor en mm sobre el deposito

struct CalibrationPoint {
    float distance_mm;
    int level;
};



// ESTOS VALORES SON SOLO DE EJEMPLO.

const CalibrationPoint calibrationTable[] = {

    { 150.0, 100 },
    { 340.0,  80 },
    { 700.0,  60 },
    { 1000.0, 40 },
    { 1200.0, 30 },
    { 1570.0, 10 },
    { 1700.0,  0 }

};

const size_t CALIBRATION_POINTS =
    sizeof(calibrationTable) /
    sizeof(calibrationTable[0]);


// ============================================================
// ADC / BATERÍA
// ============================================================

// Divisor 1:1
// Vbattery = Vadc * 2
const float BATT_DIVIDER_FACTOR = 2.1; // Factor de corrección para el divisor de tensión (10k/10k)

#define SENSOR

WiFiClient espClient;
PubSubClient mqttClient(espClient);


WebServer server(80);
DNSServer dnsServer;

// Tiempo de la última actividad HTTP
unsigned long lastConfigActivity;


struct Config {
    String wifiSSID;
    String wifiPassword;
    uint32_t sleepInterval; // segundos
    uint32_t sensorHeight; // cm

    String mqttServer;  // MQTT parameters
    int mqttPort;
    String mqttUser;
    String mqttPassword;
};
Config config;

// RTC Variables

RTC_DATA_ATTR int counter = 0;
RTC_DATA_ATTR int reconnectCounter = 0;

void normalOperation();
void stopConfigurationMode();

// ============================================================
// BATERÍA
// ============================================================
float readBatteryVoltage()
{
    // Leer varias veces para estabilizar ADC

    const int samples = 5;

    uint32_t total = 0;

    for (int i = 0; i < samples; i++)
    {
        total += analogReadMilliVolts(
            BATT_ADC_PIN
        );

        delay(5);
    }

    float adcVoltage =
        (total / (float)samples) /
        1000.0;

    float batteryVoltage =
        adcVoltage *
        BATT_DIVIDER_FACTOR;

    return batteryVoltage;
}

// ============================================================
// WIFI
// ============================================================
// bool connectWiFi_NOUSADO()
// {
//     Serial.println("Conectando WiFi...");
//     if (!WiFi.config(local_IP, gateway, subnet)) {
//         Serial.println("Error configurando IP Estática");
//     }

//     WiFi.mode(WIFI_STA);
//     WiFi.begin(ssid, password);

//     unsigned long start = millis();

//     while (WiFi.status() != WL_CONNECTED)
//     {
//         if (millis() - start > 15000)
//         {
//             Serial.println("Timeout WiFi");
//             return false;
//         }

//         delay(100);
//     }

//     Serial.print("WiFi conectado. IP: ");
//     Serial.println(WiFi.localIP());

//     return true;
// }

// ============================================================
// CONNECT WIFI
// ============================================================

bool connectWiFi()
{
    Serial.println();
    if (!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("Error configurando IP Estática");
    }

    Serial.print(
        "Conectando a WiFi: "
    );
    Serial.println(
        config.wifiSSID
    );

    WiFi.mode(
        WIFI_STA
    );

    WiFi.begin(
        config.wifiSSID.c_str(),
        config.wifiPassword.c_str()
    );

    unsigned long start =
        millis();


    // Timeout WiFi: 20 segundos

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 20000
    )
    {
        delay(250);
        Serial.print(".");
    }

    Serial.println();

    if (
        WiFi.status() == WL_CONNECTED
    )
    {
        Serial.println(
            "WiFi conectado"
        );

        Serial.print(
            "IP: "
        );

        Serial.println(
            WiFi.localIP()
        );

        return true;
    }

    Serial.println(
        "ERROR: no se pudo conectar al WiFi"
    );

    return false;
}

// ============================================================
// MQTT
// ============================================================

bool connectMQTT()
{
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    Serial.println("Conectando MQTT...");

    unsigned long start = millis();

    while (!mqttClient.connected())
    {
        if (millis() - start > 10000)
        {
            Serial.println("Timeout MQTT");
            return false;
        }

        String clientId =
            "gasoil_sensor_" +
            String((uint32_t)ESP.getEfuseMac(), HEX);

        if (mqttClient.connect(
                clientId.c_str(),
                MQTT_USER,
                MQTT_PASSWORD))
        {
            Serial.println("MQTT conectado");
            return true;
        }

        delay(500);
    }

    return true;
}
// ============================================================
// FUNCIONES MQTT DISCOVERY
// ============================================================

void publishDiscovery()
{
    // --------------------------------------------------------
    // Nivel gasoil
    // --------------------------------------------------------

    const char* levelConfig = R"json(
    {
    "name": "Nivel Gasoil",
    "unique_id": "gasoil_sensor_level",
    "state_topic": "devices/gasoil_sensor",
    "value_template": "{{ value_json.gasoil_level }}",
    "unit_of_measurement": "%",
    "device_class": "volume_storage",
    "state_class": "measurement",
    "icon": "mdi:fuel",
    "device": {
        "identifiers": ["gasoil_sensor"],
        "name": "Sensor depósito de gasoil",
        "manufacturer": "MBC",
        "model": "ESP32-C3 + HLK-LD2413"
    }
    }
    )json";

    bool result = mqttClient.publish(
        MQTT_HA_DISCOVERY_LEVEL,
        levelConfig,
        true
    );



    // --------------------------------------------------------
    // Batería
    // --------------------------------------------------------

    const char* batteryConfig = R"json(
    {
    "name": "Batería Sensor Gasoil",
    "unique_id": "gasoil_sensor_battery",
    "state_topic": "devices/gasoil_sensor",
    "value_template": "{{ value_json.batt_volt }}",
    "unit_of_measurement": "V",
    "device_class": "voltage",
    "state_class": "measurement",
    "icon": "mdi:battery",
    "device": {
        "identifiers": ["gasoil_sensor"],
        "name": "Sensor depósito de gasoil",
        "manufacturer": "MBC",
        "model": "ESP32-C3 + HLK-LD2413"
    }
    }
    )json";

    mqttClient.publish(
        MQTT_HA_DISCOVERY_BATTERY,
        batteryConfig,
        true
    );


    // --------------------------------------------------------
    // Contador
    // --------------------------------------------------------

    const char* countConfig = R"json(
    {
    "name": "Ciclos de medida",
    "unique_id": "gasoil_sensor_count",
    "state_topic": "devices/gasoil_sensor",
    "value_template": "{{ value_json.counter }}",
    "state_class": "measurement",
    "icon": "mdi:counter",
    "device": {
        "identifiers": ["gasoil_sensor"],
        "name": "Sensor depósito de gasoil",
        "manufacturer": "MBC",
        "model": "ESP32-C3 + HLK-LD2413"
    }
    }
    )json";

    mqttClient.publish(
        MQTT_HA_DISCOVERY_COUNT,
        countConfig,
        true
    );

    Serial.println("MQTT Discovery publicado");
}

// ============================================================
// GUARDAR CONFIGURACIÓN
// ============================================================
void saveConfig() {

    if (!preferences.begin("config", false))
    {
        Serial.println(
            "ERROR: No se puede abrir NVS para guardar"
        );

        return;
    }

    preferences.putString("ssid", config.wifiSSID);
    preferences.putString("password", config.wifiPassword);
    preferences.putUInt("sleep", config.sleepInterval); // Segundos
    preferences.putUInt("height", config.sensorHeight);
    preferences.putString("mqtt_server", config.mqttServer);
    preferences.putUInt("mqtt_port", config.mqttPort);
    preferences.putString("mqtt_user", config.mqttUser);
    preferences.putString("mqtt_password", config.mqttPassword);

    preferences.end();
}

// ============================================================
// CARGAR CONFIGURACIÓN
// ============================================================
bool loadConfig() {

    if (!preferences.begin("config", false))
    {
        Serial.println("ERROR: No se puede abrir NVS");

        // Valores por defecto
        config.wifiSSID = "";
        config.wifiPassword = "";
        config.sleepInterval = 60 * 60; // 60 minutos
        config.sensorHeight = 5;
        config.mqttServer = MQTT_SERVER;
        config.mqttPort = MQTT_PORT;
        config.mqttUser = MQTT_USER;
        config.mqttPassword = MQTT_PASSWORD;

        return false;
    }

    config.wifiSSID =
        preferences.getString("ssid", "");

    config.wifiPassword =
        preferences.getString("password", "");

    config.sleepInterval =
        preferences.getULong("sleep", 60 * 60);

    config.sensorHeight =
        preferences.getUInt("height", 5);

    config.mqttServer =
        preferences.getString("mqtt_server", MQTT_SERVER);

    config.mqttPort =
        preferences.getUInt("mqtt_port", MQTT_PORT);

    config.mqttUser =
        preferences.getString("mqtt_user", MQTT_USER);

    config.mqttPassword =
        preferences.getString("mqtt_password", MQTT_PASSWORD);

    preferences.end();

    Serial.println("Configuracion cargada desde NVS");
    Serial.printf("SSID: %s\n", config.wifiSSID.c_str());
    Serial.printf("Password: %s\n", config.wifiPassword.c_str());
    Serial.printf("Sleep Interval: %lu\n", config.sleepInterval);
    Serial.printf("Sensor Height: %u\n", config.sensorHeight);
    Serial.printf("MQTT Server: %s\n", config.mqttServer.c_str());
    Serial.printf("MQTT Port: %u\n", config.mqttPort);
    Serial.printf("MQTT User: %s\n", config.mqttUser.c_str());
    Serial.printf("MQTT Password: %s\n", config.mqttPassword.c_str());

    return true;
}


// ============================================================
// INICIAR PUNTO DE ACCESO
// ============================================================
void startConfigAP() {

    WiFi.mode(WIFI_AP);

    WiFi.softAP(AP_SSID, AP_PASSWORD);

    IPAddress ip = WiFi.softAPIP();

    Serial.println("================================");
    Serial.println(" MODO CONFIGURACION");
    Serial.println("================================");

    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("IP: ");
    Serial.println(ip);
}



// ============================================================
// ACTIVITY
// ============================================================

void registerActivity()
{
    lastConfigActivity = millis();
}

// ============================================================
// PAGINA WEB DE CONFIGURACION
// ============================================================
String getConfigPage()
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">

    <title>Gasoil Sensor</title>

    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f2f2f2;
            margin: 0;
            padding: 20px;
        }

        .container {
            max-width: 500px;
            margin: auto;
            background: white;
            padding: 25px;
            border-radius: 12px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.15);
        }

        h1 {
            text-align: center;
            margin-bottom: 25px;
        }

        label {
            display: block;
            margin-top: 15px;
            margin-bottom: 5px;
            font-weight: bold;
        }

        input, select {
            width: 100%;
            padding: 12px;
            box-sizing: border-box;
            border: 1px solid #bbb;
            border-radius: 6px;
            font-size: 16px;
        }

        button {
            width: 100%;
            padding: 12px;
            margin-top: 10px;
            border: none;
            border-radius: 6px;
            font-size: 16px;
            cursor: pointer;
        }

        .refresh {
            background: #777;
            color: white;
        }

        .save {
            background: #2196F3;
            color: white;
            margin-top: 25px;
        }

        .status {
            margin-top: 10px;
            text-align: center;
            color: #666;
            font-size: 14px;
        }

        .manual {
            margin-top: 8px;
            font-size: 13px;
            color: #666;
        }

        .signal {
            color: #777;
            font-size: 13px;
        }

        .password-container {
            display: flex;
            gap: 8px;
        }

        .password-container input {
            flex: 1;
        }

        .show-password {
            width: auto;
            margin-top: 0;
            padding: 8px 12px;
            background: #777;
            color: white;
            white-space: nowrap;
        }
    </style>
</head>

<body>

<div class="container">

    <h1>Sensor Gasoil</h1>
    <h2>Configuración</h2>

    <form method="POST" action="/save">

    <label for="ssidSelect">Red WiFi</label>

        <select id="ssidSelect" onchange="selectSSID()">
            <option value="">-- Selecciona una red WiFi --</option>
        </select>

        <div class="manual">
            O introduce manualmente el SSID si la red está oculta:
        </div>

        <input
            type="text"
            id="ssid"
            name="ssid"
            value="SSID_VALUE"
            placeholder="SSID"
        >

        <button
            type="button"
            class="refresh"
            onclick="scanNetworks()">
            🔄 Actualizar redes
        </button>



        <div id="scanStatus" class="status">
            Buscando redes WiFi...
        </div>


        <label for="password">Contraseña WiFi</label>

    <div class="password-container">

        <input
            type="password"
            name="password"
            id="password"
            value="PASSWORD_VALUE"
            placeholder="Contraseña WiFi"
        >

        <button
            type="button"
            class="show-password"
            onclick="togglePassword()"
            id="passwordButton">
            👁 Mostrar
        </button>

    </div>

        <label for="sleep">Intervalo de medida(minutos)</label>

        <input
            type="number"
            name="sleep"
            value="SLEEP_VALUE"
            min="15"
        >


        <label for="height">Altura del sensor (cm)</label>

        <input
            type="number"
            name="height"
            value="HEIGHT_VALUE"
            min="1"
        >

        <label for="mqtt_server">Servidor MQTT</label>

        <input
            type="text"
            name="mqtt_server"
            value="MQTT_SERVER_VALUE"
            placeholder="Servidor MQTT"
        >

        <label for="mqtt_port">Puerto MQTT</label>

        <input
            type="number"
            name="mqtt_port"
            value="MQTT_PORT_VALUE"
            min="1"
        >

        <label for="mqtt_user">Usuario MQTT</label>

        <input
            type="text"
            name="mqtt_user"
            value="MQTT_USER_VALUE"
            placeholder="Usuario MQTT"
        >

        <label for="mqtt_password">Contraseña MQTT</label>

        <input
            type="password"
            name="mqtt_password"
            value="MQTT_PASSWORD_VALUE"
            placeholder="Contraseña MQTT"
        >

        <button type="submit" class="save">
            💾 Guardar configuración
        </button>

    </form>

</div>


<script>

let savedSSID = "SSID_VALUE";


function signalText(rssi)
{
    if (rssi >= -50)
        return "Excelente";

    if (rssi >= -60)
        return "Buena";

    if (rssi >= -70)
        return "Media";

    return "Débil";
}


function scanNetworks()
{
    const select = document.getElementById("ssidSelect");
    const status = document.getElementById("scanStatus");

    select.innerHTML =
        '<option value="">Buscando redes...</option>';

    status.innerText = "Buscando redes WiFi...";

    fetch("/scan")
        .then(response => response.json())
        .then(networks => {

            select.innerHTML = "";

            if (networks.length === 0)
            {
                select.innerHTML =
                    '<option value="">No se encontraron redes</option>';

                status.innerText =
                    "No se encontraron redes WiFi.";

                return;
            }

            networks.forEach(network => {

                const option = document.createElement("option");

                option.value = network.ssid;

                let text =
                    network.ssid +
                    "  (" +
                    network.rssi +
                    " dBm - " +
                    signalText(network.rssi) +
                    ")";

                option.text = text;

                if (network.ssid === savedSSID)
                {
                    option.selected = true;
                }

                select.appendChild(option);
            });

            status.innerText =
                networks.length +
                " redes encontradas.";

            /*
             * Si existe un SSID guardado, mantenemos ese SSID.
             * Si no, seleccionamos la primera red.
             */
            if (savedSSID !== "")
            {
                select.value = savedSSID;
            }
            else if (networks.length > 0)
            {
                document.getElementById("ssid").value =
                    networks[0].ssid;
            }

        })
        .catch(error => {

            console.error(error);

            select.innerHTML =
                '<option value="">Error al buscar redes</option>';

            status.innerText =
                "Error realizando el escaneo.";

        });
}

function selectSSID()
{
    const select = document.getElementById("ssidSelect");
    const input = document.getElementById("ssid");

    if (select.value !== "")
    {
        input.value = select.value;
    }
}

function togglePassword()
{
    const password =
        document.getElementById("password");

    const button =
        document.getElementById("passwordButton");

    if (password.type === "password")
    {
        password.type = "text";
        button.innerHTML = "🙈 Ocultar";
    }
    else
    {
        password.type = "password";
        button.innerHTML = "👁 Mostrar";
    }
}

/*
 * Escaneo automático al cargar la página.
 */
window.onload = function()
{
    scanNetworks();
};

</script>

</body>
</html>
)rawliteral";


    /*
     * Sustituimos los valores almacenados
     * en la configuración.
     */

    html.replace("SSID_VALUE", config.wifiSSID);
    html.replace("PASSWORD_VALUE", config.wifiPassword);

    html.replace(
        "SLEEP_VALUE",
        String(config.sleepInterval/60)
    );

    html.replace(
        "HEIGHT_VALUE",
        String(config.sensorHeight)
    );

    html.replace(
        "MQTT_SERVER_VALUE",
        config.mqttServer
    );

    html.replace(
        "MQTT_PORT_VALUE",
        String(config.mqttPort)
    );

    html.replace(
        "MQTT_USER_VALUE",
        config.mqttUser
    );

    html.replace(
        "MQTT_PASSWORD_VALUE",
        config.mqttPassword
    );

    return html;
}

// ============================================================
// SCAN NETWORKS
// ============================================================

void handleScan()
{
    Serial.println("Iniciando escaneo WiFi...");

    /*
     * No desconectamos el SoftAP.
     * El ESP32 puede mantener el AP mientras realiza
     * el escaneo de redes.
     */

    int n = WiFi.scanNetworks(
        false,      // async = false
        true        // show_hidden = true
    );

    Serial.printf(
        "Redes encontradas: %d\n",
        n
    );

    String json = "[";

    bool first = true;

    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);

        int rssi = WiFi.RSSI(i);

        /*
         * Ignoramos SSID vacío.
         */
        if (ssid.length() == 0)
            continue;

        /*
         * Evitamos duplicados.
         */
        bool duplicate = false;

        for (int j = 0; j < i; j++)
        {
            if (WiFi.SSID(j) == ssid)
            {
                duplicate = true;
                break;
            }
        }

        if (duplicate)
            continue;


        if (!first)
            json += ",";

        first = false;

        /*
         * Escapamos caracteres especiales básicos
         * del SSID para generar JSON válido.
         */
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");

        json += "{";

        json += "\"ssid\":\"";
        json += ssid;
        json += "\",";

        json += "\"rssi\":";
        json += String(rssi);

        json += "}";
    }

    json += "]";


    /*
     * Liberamos los resultados del escaneo.
     */
    WiFi.scanDelete();


    server.send(
        200,
        "application/json",
        json
    );
}



// ============================================================
// SAVE CONFIGURATION
// ============================================================

void handleSave()
{
    registerActivity();

    Serial.printf("Argumentos recibidos: %d\n", server.args());

    for (int i = 0; i < server.args(); i++)
    {
        Serial.printf(
            "  %s = %s\n",
            server.argName(i).c_str(),
            server.arg(i).c_str()
        );
    }


    // Comprobamos que están los cuatro parámetros

    if (!server.hasArg("ssid") ||
        !server.hasArg("password") ||
        !server.hasArg("sleep") ||
        !server.hasArg("height")  ||
        !server.hasArg("mqtt_server") ||
        !server.hasArg("mqtt_port") ||
        !server.hasArg("mqtt_user") ||
        !server.hasArg("mqtt_password")
    )
    {
        Serial.println("ERROR: Faltan parametros");

        server.send(
            400,
            "text/html",
            "<!DOCTYPE html>"
            "<html lang='es'>"
            "<head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<style>"
            "body{font-family:Arial;text-align:center;padding:30px;font-size:20px;}"
            ".error{background:#ffdddd;border:2px solid #cc0000;"
            "padding:20px;border-radius:10px;color:#990000;}"
            "button{font-size:20px;padding:12px 25px;margin-top:20px;}"
            "</style>"
            "</head>"
            "<body>"
            "<div class='error'>"
            "<h2>Error</h2>"
            "<p>Faltan parametros de configuracion.</p>"
            "</div>"
            "<button onclick='history.back()'>Volver</button>"
            "</body>"
            "</html>"
        );

        return;
    }

    // --------------------------------------------------------
    // Recuperar parámetros
    // --------------------------------------------------------

    config.wifiSSID =
        server.arg("ssid");

    config.wifiPassword =
        server.arg("password");

    config.sleepInterval =
        server.arg("sleep").toInt() * 60; // Convertir a segundos

    config.sensorHeight =
        server.arg("height").toInt();

    config.mqttServer =
        server.arg("mqtt_server");

    config.mqttPort =
        server.arg("mqtt_port").toInt();

    config.mqttUser =
        server.arg("mqtt_user");

    config.mqttPassword =
        server.arg("mqtt_password");


    // --------------------------------------------------------
    // Validación
    // --------------------------------------------------------

    if (config.wifiSSID.length() == 0 ||
        config.wifiPassword.length() == 0)
    {
        server.send(
            400,
            "text/html",
            "<!DOCTYPE html>"
            "<html lang='es'>"
            "<head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<style>"
            "body{font-family:Arial;text-align:center;padding:30px;font-size:20px;}"
            ".error{background:#ffdddd;border:2px solid #cc0000;"
            "padding:20px;border-radius:10px;color:#990000;}"
            "button{font-size:20px;padding:12px 25px;margin-top:20px;}"
            "</style>"
            "</head>"
            "<body>"
            "<div class='error'>"
            "<h2>Error</h2>"
            "<p>SSID y password son obligatorios.</p>"
            "</div>"
            "<button onclick='history.back()'>Volver</button>"
            "</body>"
            "</html>"
        );

        return;
    }


    server.send(
        200,
        "text/html",
        "<!DOCTYPE html>"
        "<html lang='es'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;text-align:center;"
        "padding:30px;font-size:20px;}"
        ".ok{background:#ddffdd;border:2px solid #008800;"
        "padding:20px;border-radius:10px;color:#006600;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='ok'>"
        "<h2>Configuracion guardada</h2>"
        "<p>El sensor iniciara su funcionamiento normal.</p>"
        "</div>"
        "</body>"
        "</html>"
    );


    delay(1500);


    // --------------------------------------------------------
    // Guardar
    // --------------------------------------------------------

    saveConfig();

    // --------------------------------------------------------
    // MARCAR COMO CONFIGURADO
    // --------------------------------------------------------

    counter = 1;
    Serial.println("Configuracion guardada.");


    // --------------------------------------------------------
    // Página de confirmación
    // --------------------------------------------------------

    String response = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>Configuración guardada</title>

</head>

<body>

<h2>Configuración guardada</h2>

<p>
El dispositivo se reiniciará en unos segundos.
</p>

</body>

</html>

)rawliteral";


    server.send(
        200,
        "text/html",
        response
    );

    delay(2000);

    stopConfigurationMode();

    // IMPORTANTE: no hacer ESP.restart()
    normalOperation();

}


// ============================================================
// CAPTIVE PORTAL
// ============================================================

void handleNotFound()
{
    registerActivity();

    // Cualquier URL que solicite el móvil
    // será redirigida a nuestra página.

    server.sendHeader(
        "Location",
        "http://192.168.4.1/",
        true
    );

    server.send(
        302,
        "text/plain",
        ""
    );
}


// ============================================================
// START CONFIGURATION MODE
// ============================================================

void startConfigurationMode()
{
    Serial.println();
    Serial.println(
        "======================================"
    );

    Serial.println(
        " MODO CONFIGURACION"
    );

    Serial.println(
        "======================================"
    );


    // --------------------------------------------------------
    // Crear Access Point
    // --------------------------------------------------------

    WiFi.mode(WIFI_AP);


    // Podemos utilizar la MAC para identificar
    // cada dispositivo.

    uint64_t chipid = ESP.getEfuseMac();

    char apName[32];

    snprintf(
        apName,
        sizeof(apName),
        "GasoilSensor-%04X",
        (uint16_t)(chipid & 0xFFFF)
    );


    WiFi.softAP(
        apName,
        "12345678"
    );


    IPAddress apIP =
        WiFi.softAPIP();


    Serial.print(
        "SSID: "
    );

    Serial.println(
        apName
    );


    Serial.print(
        "IP: "
    );

    Serial.println(
        apIP
    );


    // --------------------------------------------------------
    // DNS
    // --------------------------------------------------------

    dnsServer.start(
        DNS_PORT,
        "*",
        apIP
    );


    // --------------------------------------------------------
    // HTTP endpoints
    // --------------------------------------------------------

    server.on("/", HTTP_GET, []()
    {
        server.send(
            200,
            "text/html",
            getConfigPage()
        );
    });

    server.on("/scan", HTTP_GET, handleScan);

    server.on("/save", HTTP_POST, handleSave);


    server.onNotFound(
        handleNotFound
    );


    server.begin();


    Serial.println(
        "Servidor HTTP iniciado"
    );


    Serial.println(
        "Conectate a la red WiFi y abre cualquier pagina."
    );


    // --------------------------------------------------------
    // Timeout
    // --------------------------------------------------------

    lastConfigActivity =
        millis();


    // --------------------------------------------------------
    // Loop de configuración
    // --------------------------------------------------------

    while (true)
    {

        dnsServer.processNextRequest();

        server.handleClient();


        // ----------------------------------------------------
        // Timeout de 2 minutos desde última actividad
        // ----------------------------------------------------

        if (
            millis() - lastConfigActivity
            >= CONFIG_TIMEOUT_MS
        )
        {

            Serial.println();

            Serial.println(
                "Timeout de configuracion."
            );

            counter = 1;

            delay(500);

            stopConfigurationMode();

            normalOperation();
        }


        delay(2);
    }
}

void stopConfigurationMode()
{
    Serial.println(
        "Deteniendo modo configuracion..."
    );

    server.stop();

    dnsServer.stop();

    WiFi.softAPdisconnect(true);

    delay(2000);
}

// ============================================================
// MEDIANA
// ============================================================

float calculateMedian(
    float* values,
    size_t count)
{
    std::sort(
        values,
        values + count
    );

    if (count % 2 == 0)
    {
        return (
            values[count / 2 - 1] +
            values[count / 2]
        ) / 2.0;
    }

    return values[count / 2];
}

// ============================================================
// MEDICIÓN RADAR
// ============================================================
#define NUM_MEASUREMENTS 5

bool measureDistance(float& distance_mm)
{
    float measurements[NUM_MEASUREMENTS];

    int validMeasurements = 0;

    Serial.println(
        "Comenzando mediciones..."
    );

    // Limpiar UART
    //while (RadarSerial.available())
    //    RadarSerial.read();


    unsigned long start = millis();

    while (
        validMeasurements < NUM_MEASUREMENTS &&
        millis() - start < 5000
    )
    {
        float distance;

        sensor.update();
        if (sensor.hasNewData()) {
            distance = sensor.getDistanceMm();
            // Validación básica
            if (
                distance >= 150.0 &&
                distance <= 10500.0
            )
            {
                measurements[
                    validMeasurements++
                ] = distance;


            }
        }
        delay(100);
    }

    if (validMeasurements == 0)
    {
        Serial.println(
            "No se obtuvieron medidas válidas"
        );

        return false;
    }

    distance_mm =
        calculateMedian(
            measurements,
            validMeasurements
        );

    Serial.println(""
        "Mediana: "+ String(distance_mm) + " mm"
    );

    return true;
}

// ============================================================
// DISTANCIA → NIVEL
// ============================================================

int distanceToLevel(
    float distance_mm)
{
    // Fuera de rango
    if (
        distance_mm <=
        calibrationTable[0].distance_mm
    )
    {
        return calibrationTable[0].level;
    }

    if (
        distance_mm >=
        calibrationTable[
            CALIBRATION_POINTS - 1
        ].distance_mm
    )
    {
        return calibrationTable[
            CALIBRATION_POINTS - 1
        ].level;
    }


    // Buscar intervalo
    for (
        size_t i = 0;
        i < CALIBRATION_POINTS - 1;
        i++
    )
    {
        const auto& p1 =
            calibrationTable[i];

        const auto& p2 =
            calibrationTable[i + 1];

        if (
            distance_mm >= p1.distance_mm &&
            distance_mm <= p2.distance_mm
        )
        {
            float ratio =
                (
                    distance_mm -
                    p1.distance_mm
                ) /
                (
                    p2.distance_mm -
                    p1.distance_mm
                );

            float level =
                p1.level +
                ratio *
                (
                    p2.level -
                    p1.level
                );

            return constrain(
                level,
                0,
                100
            );
        }
    }

    return 0.0;
}

// --------------------------------------------------------
// setup ()
// --------------------------------------------------------
void setup() {

    //long level = 2;


    //Serial.begin(115200);
    Serial.begin(9600);
    Serial.println("Gasoil Level Sensor - Starting...");
    pinMode(LED_PIN, OUTPUT);

    pinMode(SENSOR_POWER_PIN, OUTPUT);

    // ========================================================
    // PRIMER ARRANQUE
    // ========================================================

    if (counter == 0)
    {

        // Cargar posibles valores anteriores.
        // Aunque normalmente será la primera vez.
        loadConfig();

        startConfigurationMode();

        // Esta función no debería retornar.
        // Solo retornaría en circunstancias excepcionales.
        Serial.println("Saliendo del modo de configuración...");

        return;
    }

    counter++;

    normalOperation();
}

// ==========================================
// Función para el funcionamiento normal
// ==========================================
void normalOperation() {

    long level = 2;

    if (!loadConfig())
    {

        Serial.println(
            "ERROR: no existe configuracion."
        );

        /*
           Por seguridad, si counter != 0 pero
           no tenemos configuración, podemos
           volver a configuración.

           Esto evita que el ESP32 quede
           permanentemente sin funcionar.
        */

        counter = 0;

        startConfigurationMode();

        return;
    }

    // Configuración del sensor
#ifdef SENSOR
    digitalWrite(SENSOR_POWER_PIN, HIGH); // ENCENDER el sensor (2N3904 se activa con HIGH)

    // Initialize with your Serial port (e.g. Serial1 for ESP32)
    Serial1.begin(115200, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN); // RX on GPIO4, TX on GPIO3
    sensor.begin(Serial1);
    delay(500); // Esperar medio segundo para que el sensor se estabilice

    bool hasNewData = false;
    int count = 10;

    while (!hasNewData && count > 0) {
        sensor.update();
        if (sensor.hasNewData()) {
            float distance = sensor.getDistanceMm() - (config.sensorHeight * 10);
            level = distanceToLevel(distance);
            hasNewData = true;
        }
        count--;
        delay(100); // Esperar 100 ms antes de la siguiente lectura
    }
    if (!hasNewData) {
        Serial.println("No new data");
        level = 1;
    }

    float batt_v = readBatteryVoltage();

    digitalWrite(SENSOR_POWER_PIN, LOW);
#endif


    // --------------------------------------------------------
    // Conectar WiFi
    // --------------------------------------------------------

    if (!connectWiFi())
    {
        /*
           Si no podemos conectar al WiFi,
           no tiene sentido mantener el ESP32
           despierto indefinidamente.

           Volvemos a dormir y lo intentamos
           en el siguiente ciclo.
        */

        esp_sleep_enable_timer_wakeup(config.sleepInterval * US_TO_S); // Sleep for a long time
        Serial.println("Entrando en Deep Sleep...");
        esp_deep_sleep_start();

        return;
    }
 

    // Si logramos conectar, enviamos por MQTT
    if (WiFi.status() == WL_CONNECTED) {
        mqttClient.setBufferSize(1024);

        Serial.println("Conectado a WiFi, enviando datos por MQTT...");
        mqttClient.setServer(config.mqttServer.c_str(), config.mqttPort);

        if (mqttClient.connect("ESP32C3_Gasoil_Sensor", config.mqttUser.c_str(), config.mqttPassword.c_str())) {

            // --------------------------------------------------------
            // MQTT HA DISCOVERY
            // --------------------------------------------------------

            publishDiscovery();

            delay(100);

            // --------------------------------------------------------
            // DATOS
            // --------------------------------------------------------

            // Creamos un JSON simple
            String payload = "{\"gasoil_level\":" + String(level) +
                         ",\"batt_volt\":" + String(batt_v) +
                         ",\"counter\":" + String(counter) + "}";
            mqttClient.publish(MQTT_DATA_TOPIC, payload.c_str(),true); //retained = true
            delay(100);

            mqttClient.disconnect();
            delay(100);
            reconnectCounter = 0; // Reset counter on successful connection

        }
        //else {
        //    reconnectCounter++; // Increment counter on failed MQTT connection
        //}
    } else {
        Serial.println("No se pudo conectar a WiFi, no se enviarán datos por MQTT.");
        reconnectCounter++; // Increment counter on failed WiFi connection
    }

    // Reset counter if it exceeds 5
    if (reconnectCounter > 5) reconnectCounter = 0;

    // --------------------------------------------------------
    // DEEP SLEEP
    // --------------------------------------------------------    

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    if (reconnectCounter == 0) {
        esp_sleep_enable_timer_wakeup(config.sleepInterval * US_TO_S); // Sleep for a long time
    } else {
        esp_sleep_enable_timer_wakeup(SHORT_TIME_TO_SLEEP); // Sleep for a short time
    }
    Serial.println("Entrando en Deep Sleep...");
    esp_deep_sleep_start();

    
    
}


void loop() {

}

