#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> // Necesitarás instalar la librería "ArduinoJson"
#include <BluetoothSerial.h> 

// ====================================================================
// --- 1. CONFIGURACIÓN Y ESTRUCTURAS DE DATOS ---
// ====================================================================

// --- Credenciales Wi-Fi ---
//const char* ssid = "O Creator's";
//const char* password = "Pizza12345*";
const char* ssid = "HUAWEI-2.4G-W8yg";
const char* password = "PGbdC7X9";

// Objeto para manejar la comunicación Bluetooth
BluetoothSerial SerialBT;
bool bt_client_connected = false; 

// Número de plazas de parqueo
const int NUM_SPACES = 4;

// Crear servidor en puerto 80
WebServer server(80);

// Estructura para almacenar el estado de cada plaza
struct PlazaStatus {
  String id;             // P1, P2, P3, P4
  char estado_fisico;    // 'L' (Libre), 'O' (Ocupado) - Vía Serial (PIC)
  char estado_reserva;   // 'N' (Ninguna), 'R' (Reservada), 'M' (Mantenimiento) - Vía Web (Usuario)
  char estado_final;     // 'L', 'O', 'R', 'M' - Lo que se muestra al usuario
};

// Inicialización de las 4 plazas
PlazaStatus plazas[4] = {
  {"P1", 'L', 'N', 'L'},
  {"P2", 'L', 'N', 'L'},
  {"P3", 'L', 'N', 'L'},
  {"P4", 'L', 'N', 'L'}
};
// Declaración de hadlers (para que el compilador lso conozca)
void handleRoot();
void handleStatus();
void handleReserve();
void handleNotFound();
void parseAndApplySerialData(String data);
void applyLogic(int index);
// ====================================================================
// --- 2. LÓGICA DE GESTIÓN DE ESTADOS ---
// ====================================================================

// Función para determinar el estado final que se muestra en la web
void applyLogic(int index) {
  char reserva = plazas[index].estado_reserva;
  char fisico = plazas[index].estado_fisico;
  
  // 1. Prioridad: Mantenimiento
  if (reserva == 'M') {
    plazas[index].estado_final = 'M';
  } 
  // 2. Reserva
  else if (reserva == 'R') {
    if (fisico == 'L') {
      plazas[index].estado_final = 'R'; // Reservado (Azul)
    } else {
      plazas[index].estado_final = 'O'; // Ocupado (Rojo, el reservante llegó)
      // *Lógica extra: Aquí se podría liberar la reserva si el vehículo ya llegó.*
      plazas[index].estado_reserva = 'N'; // Ya no está "esperando" reserva
    }
  }
  // 3. Estado Físico Normal
  else { // reserva == 'N'
    plazas[index].estado_final = fisico; // Libre ('L') u Ocupado ('O')
  }
}

// Función para parsear y actualizar el estado desde el PIC (Vía Serial/BT simulado)
void parseAndApplySerialData(String data) {
  // Trama esperada del PIC: "S:L,O,L,L" (L=Libre, O=Ocupado)
  const int NUM_SPACES = 4;

  if (data.startsWith("S:")) {
    String payload = data.substring(2); // Elimina "S:"
    int startIndex = 0;
    
    for (int i = 0; i < NUM_SPACES; i++) {
      int endIndex = payload.indexOf(',', startIndex);
      // Extrae la letra del estado (L u O)
      String statusStr = (endIndex == -1) ? payload.substring(startIndex) : payload.substring(startIndex, endIndex);
      
      char statusChar = statusStr.charAt(0);
      
      // La lógica ahora acepta 'L' y 'O' directamente
      if (statusChar == 'L' || statusChar == 'O') {
        plazas[i].estado_fisico = statusChar;
        applyLogic(i);
        //Serial.printf("Plaza %d actualizada a físico: %c\n", i + 1, statusChar);
      }
      
      if (endIndex == -1) break;
      startIndex = endIndex + 1;
    }
  } else {
    // Si no empieza con S:, ahora detectará la trama "#,0,0,0,0;" como no reconocida.
    Serial.print("Trama Serial no reconocida: ");
    Serial.println(data);
  }
}

// ====================================================================
// --- 3. SERVIDOR WEB - HANDLERS (BACKEND) ---
// ====================================================================

// Handler para /status - Devuelve el JSON con el estado actual
void handleStatus() {
  String jsonText = "";
  
  // Usamos ArduinoJson para construir la respuesta
  const int capacity = JSON_ARRAY_SIZE(4) + 4 * JSON_OBJECT_SIZE(4);
  StaticJsonDocument<capacity> doc;
  
  for (int i = 0; i < 4; i++) {
    JsonObject plaza = doc.createNestedObject();
    plaza["id"] = plazas[i].id;
    plaza["status"] = String(plazas[i].estado_final);
    
    // Asignar el texto y la capacidad de reserva
    String text;
    bool canReserve = false;
    
    switch (plazas[i].estado_final) {
      case 'L': text = "DISPONIBLE"; canReserve = true; break;
      case 'O': text = "OCUPADO"; break;
      case 'R': text = "RESERVADO"; break;
      case 'M': text = "MANTENIMIENTO"; break;
    }
    
    plaza["text"] = text;
    plaza["can_reserve"] = canReserve || (plazas[i].estado_final == 'R'); // Puedes cancelar si está reservada
  }

  serializeJson(doc, jsonText);
  server.send(200, "application/json", jsonText);
}

// Handler para /reserve - Recibe comandos POST de reserva/cancelación
void handleReserve() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  // Intenta parsear el JSON
  const int capacity = JSON_OBJECT_SIZE(2);
  StaticJsonDocument<capacity> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  if (error) {
    server.send(400, "application/json", "{\"success\": false, \"message\": \"Error parsing JSON\"}");
    return;
  }

  String plazaId = doc["plaza"].as<String>();
  String action = doc["action"].as<String>(); // 'R' (Reservar) o 'C' (Cancelar)

  int plazaIndex = -1;
  if (plazaId == "P1") plazaIndex = 0;
  else if (plazaId == "P2") plazaIndex = 1;
  else if (plazaId == "P3") plazaIndex = 2;
  else if (plazaId == "P4") plazaIndex = 3;

// 1. Aplicar la lógica de reserva/cancelación (R o C)
if (plazaIndex != -1) {    

    // 1. ACTUALIZAR EL ESTADO DE RESERVA LOCAL

    if (action == "R") {
      // Reservar
      plazas[plazaIndex].estado_reserva = 'R';
      Serial.printf("Plaza %s RESERVADA localmente\n", plazaId.c_str());
    } 
    else if (action == "C") {
      // Cancelar
      plazas[plazaIndex].estado_reserva = 'N';
      Serial.printf("Plaza %s CANCELADA localmente\n", plazaId.c_str());
    }
    
    // Aplicar la lógica para actualizar estado_final
    applyLogic(plazaIndex);
    
    // ----------------------------------------------------------
    String command = "R:"; // Prefijo de Control R:
    
    // El ESP32 envía el estado FINAL de la web para que el PIC solo lo replique.
    for (int i = 0; i < NUM_SPACES; i++) {
        // Enviar el estado final (L, O, R, M)
        command += String(plazas[i].estado_final); 
        if (i < NUM_SPACES - 1) {
            command += ","; // Separador de coma
        }
    }
    
    command += "\n"; // Fin de línea

    // ** PUNTO CLAVE: ENVÍO VÍA BLUETOOTH **
    if (SerialBT.hasClient()) {
        SerialBT.println(command); 
        Serial.println("Comando BT enviado al PIC: ");
        Serial.println(command);
    } else {
        Serial.println("Advertencia: No se envió comando BT, cliente desconectado.");
        Serial.println("Comando que se intentó enviar: " + command);
    }
    // ----------------------------------------------------------
    
    // ... (restablecer la respuesta JSON al cliente web) ...
    return;
}
// ...

  server.send(400, "application/json", "{\"success\": false, \"message\": \"ID de Plaza o Acción Inválida.\"}");
}

// ====================================================================
// --- 4. SERVIDOR WEB - FRONTEND (HTML/CSS/JS) ---
// ====================================================================

// Estilos CSS
const char* STYLE_CSS = R"rawliteral(
body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background-color: #f0f2f5; }
h1 { text-align: center; color: #1e3c72; margin-bottom: 30px; }
.container { display: flex; flex-wrap: wrap; justify-content: space-around; }
.plaza-card {
    background-color: #fff; border-radius: 12px; box-shadow: 0 6px 15px rgba(0,0,0,0.1);
    padding: 25px; margin: 15px; width: 100%; max-width: 280px;
    text-align: center; transition: all 0.4s ease-in-out;
}
.status-text { font-size: 1.5em; font-weight: bold; margin: 10px 0; }

/* Clases dinámicas para el estado */
.L { background-color: #e6ffe6; color: #008000; border-left: 8px solid #00c853; } /* Libre (Verde) */
.O { background-color: #ffe6e6; color: #cc0000; border-left: 8px solid #ff4444; } /* Ocupado (Rojo) */
.R { background-color: #e6f0ff; color: #0000cc; border-left: 8px solid #3366ff; } /* Reservado (Azul) */
.M { background-color: #fffde7; color: #ff8800; border-left: 8px solid #ffbb33; } /* Mantenimiento (Amarillo) */

/* Botones */
.action-btn {
    padding: 12px 25px; font-size: 1em; margin-top: 15px; cursor: pointer;
    border: none; border-radius: 6px; font-weight: bold;
    transition: background-color 0.3s;
}
.btn-reserve { background-color: #007bff; color: white; }
.btn-reserve:hover { background-color: #0056b3; }
.btn-cancel { background-color: #dc3545; color: white; }
.btn-cancel:hover { background-color: #a71d2a; }
.action-btn:disabled { background-color: #ccc; cursor: not-allowed; color: #666; }

/* Responsive */
@media (max-width: 650px) {
    .container { flex-direction: column; align-items: center; }
    .plaza-card { max-width: 90%; }
}
)rawliteral";

// Script JS para la lógica del Frontend
const char* SCRIPT_JS = R"rawliteral(
// Función principal para consultar el estado del ESP32
function updateStatus() {
    fetch('/status') 
    .then(response => response.json())
    .then(data => {
        data.forEach(plaza => {
            const card = document.getElementById(`${plaza.id}-card`);
            const statusText = card.querySelector('.status-text');
            const actionBtn = card.querySelector('.action-btn');

            // 1. Actualizar clases (L, O, R, M) para cambiar color
            card.className = `plaza-card ${plaza.status}`;
            
            // 2. Actualizar texto de estado
            statusText.textContent = plaza.text;
            
            // 3. Actualizar el botón (Reservar/Cancelar/No Disponible)
            actionBtn.disabled = !plaza.can_reserve && plaza.status !== 'R'; // Deshabilita si no está L y no está R
            actionBtn.setAttribute('data-current-status', plaza.status); // Guardamos el estado actual

            if (plaza.status === 'R') {
                actionBtn.textContent = 'Cancelar Reserva';
                actionBtn.className = 'action-btn btn-cancel';
            } else if (plaza.status === 'L') {
                actionBtn.textContent = 'Reservar';
                actionBtn.className = 'action-btn btn-reserve';
            } else { // Ocupado o Mantenimiento
                actionBtn.textContent = 'No Disponible';
                actionBtn.className = 'action-btn';
            }
        });
    })
    .catch(error => console.error('Error al obtener estado:', error));
}

// Manejador de eventos para el botón de Reserva/Cancelación
document.addEventListener('DOMContentLoaded', () => {
    // Inicializa y repite la consulta cada 2 segundos
    updateStatus(); 
    setInterval(updateStatus, 2000); 

    document.getElementById('status-container').addEventListener('click', function(e) {
        if (e.target.classList.contains('action-btn') && !e.target.disabled) {
            const plazaId = e.target.getAttribute('data-plaza-id');
            const currentStatus = e.target.getAttribute('data-current-status');
            
            // Si está Libre -> Reservar ('R'); Si está Reservado -> Cancelar ('C')
            const action = (currentStatus === 'L') ? 'R' : 'C'; 

            // Crear el cuerpo de la petición POST
            const payload = JSON.stringify({ plaza: plazaId, action: action });
            
            fetch('/reserve', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: payload
            })
            .then(response => response.json())
            .then(data => {
                alert(data.message); 
                updateStatus(); // Forzar actualización para ver el cambio
            })
            .catch(error => console.error('Error de reserva:', error));
        }
    });
});
)rawliteral";

// HTML Principal (Inyecta JS y CSS)
String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Parqueadero Inteligente</title>
  <style>%STYLE_CSS%</style>
</head>
<body>
  <h1>🅿️ Parqueadero Inteligente - Estado</h1>
  <div id="status-container" class="container">
    <div id="P1-card" class="plaza-card loading"><h3>Plaza P1</h3><p class="status-text">Cargando...</p><button class="action-btn" data-plaza-id="P1" disabled>...</button></div>
    <div id="P2-card" class="plaza-card loading"><h3>Plaza P2</h3><p class="status-text">Cargando...</p><button class="action-btn" data-plaza-id="P2" disabled>...</button></div>
    <div id="P3-card" class="plaza-card loading"><h3>Plaza P3</h3><p class="status-text">Cargando...</p><button class="action-btn" data-plaza-id="P3" disabled>...</button></div>
    <div id="P4-card" class="plaza-card loading"><h3>Plaza P4</h3><p class="status-text">Cargando...</p><button class="action-btn" data-plaza-id="P4" disabled>...</button></div>
  </div>
  <script>%SCRIPT_JS%</script>
</body>
</html>
)rawliteral";

  String page = String(html);
  page.replace("%STYLE_CSS%", STYLE_CSS);
  page.replace("%SCRIPT_JS%", SCRIPT_JS);
  return page;
}

// Handler para la página principal
void handleRoot() {
  server.send(200, "text/html", getHTML());
}

// Página no encontrada
void handleNotFound() {
  server.send(404, "text/plain", "404: No encontrado");
}


// ====================================================================
// --- 5. SETUP Y LOOP ---
// ====================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- 4.1. Configuración Bluetooth (Tu base) ---
  if (!SerialBT.begin("ESP32_PARKING_SERVER")) {
    Serial.println("Error iniciando BluetoothSerial.");
    while (1) delay(1000);
  }
  SerialBT.setPin("1234", 4); 
  Serial.println("\n--- Bluetooth Configurado (Servidor SPP) ---");
  Serial.println("Nombre BT: ESP32_PARKING_SERVER | PIN: 1234");

  Serial.println("\n--- ESP32 WiFi Station Mode ---");
  
  // Conexión Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 40) { // 20 segundos máximo
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n¡Conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n¡Falló la conexión Wi-Fi! Reiniciando...");
    ESP.restart(); // Reinicia si no se conecta
  }
  
  // Configurar rutas del servidor
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/reserve", handleReserve);
  server.onNotFound(handleNotFound);
  
  // Iniciar servidor
  server.begin();
  Serial.println("Servidor web iniciado");
  Serial.print("Accede desde: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient(); // Atender peticiones HTTP (Servidor Web)

  // ----------------------------------------------------
  // --- Tarea 1.2: Recepción de Estado del PIC (Bluetooth) ---
  // ----------------------------------------------------
  
  // 1. Monitorear conexión
  bool client_current = SerialBT.hasClient();
  if (client_current && !bt_client_connected) {
    Serial.println("Cliente PIC CONECTADO por Bluetooth.");
    bt_client_connected = true;
  } else if (!client_current && bt_client_connected) {
    Serial.println("Cliente PIC DESCONECTADO.");
    bt_client_connected = false;
  }

  // 2. Leer datos del PIC
  if (SerialBT.available()) {
    String data = SerialBT.readStringUntil('\n'); // Lee la trama completa hasta el newline
    data.trim(); 
    if (data.length() > 0) {
      Serial.print("BT Data RAW: ");
      Serial.println(data);
      parseAndApplySerialData(data); // Procesa el mensaje S:L,O,L,L
    }
  }
}