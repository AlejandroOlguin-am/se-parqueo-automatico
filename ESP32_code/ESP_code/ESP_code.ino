#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> // Necesitarás instalar la librería "ArduinoJson"
#include <BluetoothSerial.h> 

// ====================================================================
// --- 1. CONFIGURACIÓN Y ESTRUCTURAS DE DATOS ---
// ====================================================================

// --- Credenciales Wi-Fi ---
const char* ssid = "O Creator's";
const char* password = "Pizza12345*";
//const char* ssid = "HUAWEI-2.4G-W8yg";
//const char* password = "PGbdC7X9";
// --- Bluetooth Serial ---
unsigned long last_pic_sync_time = 0;
const long PIC_SYNC_INTERVAL = 8000; // 8 segundos

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
// --- FUNCIÓN PARA ENVIAR EL ESTADO FINAL DE LAS PLAZAS AL PIC ---
void send_control_to_pic() {
    if (!SerialBT.hasClient()) {
        Serial.println("Advertencia: No se envió comando BT, cliente desconectado.");
        return;
    }
    
    String command = "R:"; // Prefijo de Control R:
    
    // Construir la trama completa R:X,X,X,X
    for (int i = 0; i < NUM_SPACES; i++) {
        command += String(plazas[i].estado_final); 
        if (i < NUM_SPACES - 1) {
            command += ","; 
        }
    }
    
    SerialBT.println(command); 
    Serial.println("Comando BT enviado al PIC: " + command);
    SerialBT.flush();
}
// Función para determinar el estado final que se muestra en la web
void applyLogic(int index) {
  char reserva = plazas[index].estado_reserva;
  char fisico = plazas[index].estado_fisico;
  char prev_estado_final = plazas[index].estado_final; // Guardar estado anterior
  
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
  // **** SINCRONIZACIÓN CON EL PIC ****
  if (plazas[index].estado_final != prev_estado_final) {
      // Si el estado final lógico (que se muestra en la web) cambió, 
      // debemos enviar el comando R: completo para que el PIC actualice su LED.
      send_control_to_pic();
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
    else if (action == "M") {
        plazas[plazaIndex].estado_reserva = 'M'; // Forzar mantenimiento
        server.send(200, "application/json", "{\"success\": true, \"message\": \"Plaza en MANTENIMIENTO\"}");
    }
    else if (action == "X") {
        plazas[plazaIndex].estado_reserva = 'N'; // Volver a estado normal (Ninguna reserva)
        server.send(200, "application/json", "{\"success\": true, \"message\": \"Mantenimiento finalizado\"}");
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
    
    //command += "\n"; // Fin de línea

    //** ENVÍO VÍA BLUETOOTH **
    send_control_to_pic();
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
/* 1. RESET UNIVERSAL: CRÍTICO PARA CORREGIR EL DESFASE */
* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body { 
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
    background-color: #f0f2f5; 
    /* Quitamos padding del body para evitar conflictos de ancho */
    width: 100%;
    min-height: 100vh;
}

h1 { 
    text-align: center; 
    color: #1e3c72; 
    margin: 30px 0; /* Margen solo vertical */
    padding: 0 10px; /* Un poco de aire lateral para pantallas muy pequeñas */
}

/* CONTENEDOR PRINCIPAL */
.container { 
    display: flex; 
    flex-wrap: wrap; 
    /* Usamos center en lugar de space-around para control total */
    justify-content: center; 
    width: 100%;
    padding: 20px; /* El padding va aquí, no en el body */
    gap: 20px; /* Espacio entre tarjetas automático */
}

/* TARJETAS */
.plaza-card {
    background-color: #fff; 
    border-radius: 12px; 
    box-shadow: 0 6px 15px rgba(0,0,0,0.1);
    padding: 25px; 
    text-align: center; 
    transition: all 0.4s ease-in-out;
    
    /* Dimensiones escritorio */
    width: 280px; 
    /* No usamos margin aquí, usamos gap en el container */
}

.status-text { font-size: 1.5em; font-weight: bold; margin: 10px 0; }

/* CLASES DE ESTADO */
.L { background-color: #e6ffe6; color: #008000; border-left: 8px solid #00c853; }
.O { background-color: #ffe6e6; color: #cc0000; border-left: 8px solid #ff4444; }
.R { background-color: #e6f0ff; color: #0000cc; border-left: 8px solid #3366ff; }
.M { background-color: #fffde7; color: #ff8800; border-left: 8px solid #ffbb33; }

/* BOTONES */
.action-btn {
    display: flex; 
    justify-content: center;
    align-items: center;
    padding: 12px 25px; 
    font-size: 1em; 
    margin-top: 15px; 
    cursor: pointer;
    border: none; 
    border-radius: 6px; 
    font-weight: bold;
    transition: background-color 0.3s;
}

.btn-reserve { background-color: #007bff; color: white; }
.btn-reserve:hover { background-color: #0056b3; }
.btn-cancel { background-color: #dc3545; color: white; }
.btn-cancel:hover { background-color: #a71d2a; }
.action-btn:disabled { background-color: #ccc; cursor: not-allowed; color: #666; }

/* BOTONES MANTENIMIENTO */
.btn-maint { padding: 12px; width: 50px; font-size: 1.2em; }
.btn-maint-active { background-color: #ff8800 !important; color: white; }
.btn-maint-enable { background-color: #f7c769ff !important; font-size: 1em; }

/* --- RESPONSIVE MOVIL --- */
@media (max-width: 650px) {
    .container { 
        flex-direction: column; 
        align-items: center; /* Esto centra los hijos verticalmente apilados */
        padding: 10px; /* Menos padding en móvil */
    }

    .plaza-card { 
        /* En móvil forzamos el ancho y el margen automático */
        width: 100% !important; 
        max-width: 320px; /* Tope para que no se vea gigante en tablets */
        margin-left: auto !important;
        margin-right: auto !important;
    }
}
)rawliteral";

// Script JS para la lógica del Frontend
const char* SCRIPT_JS = R"rawliteral(
function updateStatus() {
    fetch('/status') 
    .then(response => response.json())
    .then(data => {
        data.forEach(plaza => {
            const card = document.getElementById(`${plaza.id}-card`);
            const statusText = card.querySelector('.status-text');
            // Buscamos el contenedor de acciones (o lo creamos si usaste mi HTML anterior, 
            // pero para no complicar, asumiremos que reemplazamos el boton existente)
            
            // Vamos a regenerar el HTML interno de la tarjeta para incluir los 2 botones
            let btnMainClass = '';
            let btnMainText = '';
            let btnMainAction = '';
            let btnMaintClass = 'action-btn btn-maint'; // Clase para boton mantenimiento
            let btnMaintText = '🔧';
            let btnMaintAction = 'M'; // Activar mantenimiento
            
            // Logica visual
            if (plaza.status === 'M') {
                btnMainText = 'En Mantenimiento';
                btnMainClass = 'action-btn'; 
                btnMainAction = ''; // No hace nada
                btnMaintText = '✅'; // Habilitar
                btnMaintAction = 'X'; // X = Salir de mantenimiento (Exit)
                btnMaintClass += ' btn-maint-enable';
                card.className = 'plaza-card M';
            } else if (plaza.status === 'R') {
                btnMainText = 'Cancelar Reserva';
                btnMainClass = 'action-btn btn-cancel';
                btnMainAction = 'C';
                card.className = 'plaza-card R';
            } else if (plaza.status === 'O') {
                btnMainText = 'Ocupado';
                btnMainClass = 'action-btn';
                btnMainAction = '';
                card.className = 'plaza-card O';
            } else { // Libre
                btnMainText = 'Reservar';
                btnMainClass = 'action-btn btn-reserve';
                btnMainAction = 'R';
                card.className = 'plaza-card L';
            }

            statusText.textContent = plaza.text;

            // Inyectamos HTML de botones dinámicamente
            // Boton Principal + Boton Mantenimiento (pequeño al lado)
            const buttonsHTML = `
                <div style="display:flex; gap:5px; justify-content:center;">
                    <button class="${btnMainClass}" onclick="sendAction('${plaza.id}', '${btnMainAction}')" ${btnMainAction==''?'disabled':''}>${btnMainText}</button>
                    <button class="${btnMaintClass}" style="width:${plaza.status === 'M' ? '65px' : '50px'};" onclick="sendAction('${plaza.id}', '${btnMaintAction}')">${btnMaintText}</button>
                </div>
            `;
            
            // Acceder o crear el contenedor de acciones
            let actionContainer = card.querySelector('.actions');
            if(!actionContainer) {
                 // Si no existe container, lo creamos (esto va al final de la card)
                 const newDiv = document.createElement('div');
                 newDiv.className = 'actions';
                 card.appendChild(newDiv);
                 actionContainer = newDiv;
            }
            actionContainer.innerHTML = buttonsHTML;
        });
    })
    .catch(error => console.error('Error:', error));
}

// Función global para enviar acciones
function sendAction(plazaId, action) {
    if(!action) return;
    
    fetch('/reserve', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ plaza: plazaId, action: action })
    })
    .then(r => r.json())
    .then(d => { alert(d.message); updateStatus(); });
}

document.addEventListener('DOMContentLoaded', () => {
    updateStatus(); 
    setInterval(updateStatus, 2000); 
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
    <div id="P1-card" class="plaza-card loading"><h3>Plaza P1</h3><p class="status-text">Cargando...</p></div>
    <div id="P2-card" class="plaza-card loading"><h3>Plaza P2</h3><p class="status-text">Cargando...</p></div>
    <div id="P3-card" class="plaza-card loading"><h3>Plaza P3</h3><p class="status-text">Cargando...</p></div>
    <div id="P4-card" class="plaza-card loading"><h3>Plaza P4</h3><p class="status-text">Cargando...</p></div>
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
  if (SerialBT.available())
  {
    String data = SerialBT.readStringUntil('\n'); // Lee la trama completa hasta el newline
    data.trim();
    if (data.length() > 0)
    {
      Serial.print("BT Data RAW: ");
      Serial.println(data);
      parseAndApplySerialData(data); // Procesa el mensaje S:L,O,L,L
    }
    else if (data.startsWith("ACK:"))
    {
      Serial.print("PIC ACK: ");
      Serial.println(data);
    }
    else
    {
      Serial.print("IGNORED BT: ");
      Serial.println(data);
    }
  }
  // ----------------------------------------------------
  // --- Tarea 3: Envío Periódico de Control (Heartbeat) ---
  // ----------------------------------------------------
  unsigned long currentMillis = millis();
  
  if (currentMillis - last_pic_sync_time >= PIC_SYNC_INTERVAL) {
      if (SerialBT.hasClient()) {
        send_control_to_pic(); // Re-enviar el estado lógico completo
        Serial.println("Heartbeat R: enviado.");
      }
      last_pic_sync_time = currentMillis;
  }
}