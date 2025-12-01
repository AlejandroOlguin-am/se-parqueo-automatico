# 🅿️ Sistema de Parqueadero Inteligente

## Descripción General

Sistema IoT completo para gestionar un parqueadero con **4 plazas de estacionamiento**. Utiliza un **PIC16F877A** como controlador de hardware (sensores, LEDs, servos) y un **ESP32** como servidor central que proporciona una interfaz web y comunica los estados.

**Características principales:**
- 🌐 Interfaz web responsive para gestionar reservas
- 📡 Comunicación Bluetooth entre PIC y ESP32
- 🚗 Detección de vehículos con sensores
- 💡 LEDs indicadores de estado por plaza
- 🔓 Control de barreras con servomotores
- 📱 Reservas, cancelaciones y mantenimiento de plazas
- 📊 Pantalla LCD I2C para información en tiempo real

---

## Arquitectura del Sistema

```
┌─────────────────┐
│   NAVEGADOR WEB │ (HTTP)
└────────┬────────┘
         │
    ┌────▼────────────────────┐
    │   ESP32 (Servidor Web)  │
    │  192.168.1.X:80         │
    │ ─ Lógica de Estados     │
    │ ─ WebServer             │
    │ ─ ArduinoJSON           │
    └────┬────────────────────┘
         │ Bluetooth Serial
    ┌────▼──────────────────┐
    │  PIC16F877A           │
    │ ─ Sensores (RA0-RA3)  │
    │ ─ LEDs (RB, RD)       │
    │ ─ Servos (RC, RE)     │
    │ ─ LCD I2C             │
    └───────────────────────┘
```

---

## Componentes de Hardware

### **ESP32**
- Servidor web en puerto 80
- Comunicación Bluetooth SPP (Serial Profile)
- Gestión de lógica de estados

### **PIC16F877A**
- **Sensores:** RA0-RA3 (detectan vehículos)
- **LEDs:** RB0-RB7, RD0-RD7 (4 LEDs por plaza: Libre, Ocupado, Reservado, Mantenimiento)
- **Servos:** RC0, RC1, RC2, RE0 (barreras de acceso)
- **UART:** RC6 (TX), RC7 (RX) - Bluetooth
- **I2C:** RC3 (SCL), RC4 (SDA) - LCD 16x2

---

## Estados de las Plazas

Cada plaza puede estar en uno de estos estados:

| Estado | Símbolo | Color | Descripción |
|--------|---------|-------|-------------|
| **Libre** | L | 🟢 Verde | Plaza disponible para estacionar |
| **Ocupado** | O | 🔴 Rojo | Vehículo estacionado |
| **Reservado** | R | 🔵 Azul | Plaza reservada por usuario |
| **Mantenimiento** | M | 🟡 Naranja | Plaza fuera de servicio |

### Lógica de Prioridad
1. **Mantenimiento (M)** → Máxima prioridad
2. **Reserva (R)** → Media prioridad
3. **Estado Físico (L/O)** → Baja prioridad

---

## Flujo de Comunicación

### Dirección: PIC → ESP32
```
Trama: "S:L,O,L,L\r\n"
       ↓
       S: = Prefijo de estado (Sensor)
       L,O,L,L = Estado físico de cada plaza
```

### Dirección: ESP32 → PIC
```
Trama: "R:L,R,O,M\n"
       ↓
       R: = Prefijo de control
       L,R,O,M = Estado final calculado por ESP32
```

---

## Instalación y Configuración

### **Requisitos**
- Arduino IDE
- Librería **ArduinoJson** (v6.x)
- Librería **BluetoothSerial** (integrada en ESP32)
- CCS C Compiler (para PIC)
- Librería LCD I2C (para PIC)

### **Paso 1: Configurar ESP32**

1. Abre `ESP32_code/ESP_code/ESP_code.ino` en Arduino IDE
2. Modifica las credenciales WiFi:
```cpp
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_CONTRASEÑA";
```

3. Instala la librería ArduinoJson:
   - Sketch → Include Library → Manage Libraries
   - Busca "ArduinoJson" e instala v6.x

4. Selecciona placa: **ESP32 Dev Module**

5. Sube el código

### **Paso 2: Configurar PIC**

1. Abre `PIC_code.c` en CCS C Compiler
2. Verifica las conexiones de pines
3. Compila y carga el firmware en el PIC

### **Paso 3: Emparejar Bluetooth**

1. En tu dispositivo, busca dispositivos Bluetooth
2. Encuentra **"ESP32_PARKING_SERVER"**
3. Contraseña: **1234**
4. Conecta

---

## Uso de la Interfaz Web

### Acceso
```
http://<IP_DEL_ESP32>
Ejemplo: http://192.168.1.100
```

### Funcionalidades

#### **Reservar una Plaza**
- Click en botón **"Reservar"** en una plaza Libre
- La plaza cambia a estado **Reservado (Azul)**
- Se activa la barrera

#### **Cancelar Reserva**
- Click en botón **"Cancelar Reserva"** en una plaza Reservada
- Vuelve a estado **Libre**

#### **Modo Mantenimiento**
- Click en botón **🔧** (esquina derecha de la tarjeta)
- Plaza pasa a **Mantenimiento (Amarillo)**
- Click en **✅** para salir de mantenimiento

#### **Monitor en Vivo**
- Se actualiza automáticamente cada 2 segundos
- Muestra plazas libres, ocupadas, reservadas

---

## Estructura de Archivos

```
Parqueo-PIC-ESP32/
├── codigos_parqueo/
│   ├── ESP32_code/
│   │   └── ESP_code/
│   │       └── ESP_code.ino          ← Código ESP32
│   ├── PIC_code.c                    ← Código PIC
│   └── README.md                     ← Este archivo
└── tests/
    ├── bluetooth_test.ino
    └── test_LCD-esp32.ino
```

---

## API REST del ESP32

### **GET /status**
Retorna estado de todas las plazas
```json
[
  {
    "id": "P1",
    "status": "L",
    "text": "DISPONIBLE",
    "can_reserve": true
  },
  ...
]
```

### **POST /reserve**
Envía acción de reserva/cancelación
```json
{
  "plaza": "P1",
  "action": "R"  // R=Reservar, C=Cancelar, M=Mantenimiento, X=Salir Mant.
}
```

Respuesta:
```json
{
  "success": true,
  "message": "Plaza reservada exitosamente"
}
```

---

## Solución de Problemas

| Problema | Solución |
|----------|----------|
| **ESP32 no se conecta a WiFi** | Verifica SSID y contraseña, reinicia el ESP32 |
| **No hay comunicación Bluetooth** | Asegúrate de que el PIC está encendido y emparejado |
| **LEDs no se encienden** | Verifica conexiones en el PIC y la polaridad |
| **Sensores no detectan vehículos** | Revisa el debouncing y calibra los sensores |
| **LCD no muestra información** | Verifica dirección I2C (0x4E) y conexiones |
| **Interfaz web no responde** | Comprueba que el ESP32 tiene IP asignada |

---

## Notas Importantes

⚠️ **Seguridad WiFi**: En producción, cambia las credenciales por defecto  
⚠️ **PIN Bluetooth**: Personaliza el PIN "1234"  
⚠️ **Debouncing de Sensores**: Ajusta `delay_ms(10)` si es necesario  
⚠️ **Intervalo de Sincronización**: `PIC_SYNC_INTERVAL = 8000ms` (8 segundos)  

---

## Descripción Técnica de Funciones Clave

### **applyLogic(int index)**
Determina el estado final de una plaza basándose en:
1. **Estado de Mantenimiento**: Si es 'M', permanece en 'M'
2. **Estado de Reserva**: Si es 'R', prevalece sobre estado físico
3. **Estado Físico**: L (Libre) u O (Ocupado)

```cpp
void applyLogic(int index) {
  if (plazas[index].estado_reserva == 'M') {
    plazas[index].estado_final = 'M';
  } 
  else if (plazas[index].estado_reserva == 'R') {
    plazas[index].estado_final = 'R';
  } 
  else {
    plazas[index].estado_final = plazas[index].estado_fisico;
  }
}
```

### **parseAndApplySerialData(String data)**
Procesa los datos recibidos del PIC vía Bluetooth:
- Parsea trama: `"S:L,O,L,L\r\n"`
- Actualiza `estado_fisico` de cada plaza
- Llama a `applyLogic()` para recalcular estado final

### **handleReserve()**
Maneja las solicitudes POST de reserva/cancelación:
- Actualiza `estado_reserva`
- Llama a `applyLogic()` para aplicar la lógica
- Envía comando de control al PIC
- Retorna respuesta JSON

### **send_control_to_pic()**
Envía el estado final calculado al PIC:
- Forma trama: `"R:L,R,O,M\n"`
- El PIC actualiza LEDs y controla servos según esta trama

---

## Ciclo de Operación

1. **PIC lee sensores** cada 20ms y envía cambios al ESP32
2. **ESP32 recibe estado físico** vía Bluetooth
3. **ESP32 aplica lógica** (mantenimiento > reserva > físico)
4. **ESP32 envía estado final** al PIC
5. **PIC actualiza LEDs y servos** según estado final
6. **Web se actualiza** cada 2 segundos con GET /status

---

## Contacto y Soporte

Para reportar bugs o sugerencias, por favor abre un issue en el repositorio.

---

**Versión:** 1.0  
**Última actualización:** Diciembre 2025  
**Autores:** 
- Diseño Electrónico: [Percy Viza](https://github.com/percyviza)      
- Programación: [Alejandro Olguin](https://github.com/alejandroolguin-am) 
