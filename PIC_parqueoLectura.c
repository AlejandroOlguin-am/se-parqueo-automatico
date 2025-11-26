/* PIC16F877A - Sistema Parking Unificado
   CCS C - 4 MHz
   Sensores: RA0..RA3
   LEDs:     RB0..RB7, RD0..RD7 (4 espacios x 4 leds cada uno)
   Servo:    PIN_C0 (Barrera de Entrada)
   UART: baud 9600 (HC-05)
*/

#include <16F877A.h>
#fuses XT, NOWDT, NOLVP, NOBROWNOUT // Asegura que no hay ADC/Comparadores activos por defecto
#use delay(clock=4000000)

// UART hacia HC-05
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1)

// --- Pines y Constantes ---
const int8 NUM_SPACES = 4;
#define SERVO_ENTRY_PIN PIN_C0 

// Pines sensores
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PINCH_A1, PIN_A2, PIN_A3 };

// Mapeo LEDs [space][color]  color: 0=Green,1=Red,2=Blue,3=Yellow
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },  // space 0
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },  // space 1
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },  // space 2
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }   // space 3
};

// --- Funciones de Control ---

void set_space_led(int8 space, int8 state) {
   // state: 0=Green, 1=Red, 2=Blue, 3=Yellow
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

void set_barrier_state(int8 open_close) {
    // 0: Cerrar (Servo a 0°); 1: Abrir (Servo a 90°)
    if (open_close == 1) {
        // Lógica real de PWM para abrir servo a 90 grados
        output_high(SERVO_ENTRY_PIN);
        delay_us(1500); // Pulso ~1.5ms
        output_low(SERVO_ENTRY_PIN);
    } else {
        // Lógica real de PWM para cerrar servo a 0 grados
        output_high(SERVO_ENTRY_PIN);
        delay_us(500); // Pulso ~0.5ms
        output_low(SERVO_ENTRY_PIN);
    }
    // Nota: El servo requiere un loop PWM constante. Esto es un ejemplo simple.
}


// --- Lógica de Envío (PIC -> ESP32) ---

void enviar_estados_sensores(int8 sensors[]) {
   // Formato UNIFICADO: S:L,O,L,L\n  (L=Libre, O=Ocupado)
   int8 i;
   putchar('S'); putchar(':'); // Prefijo S:

   for (i = 0; i < NUM_SPACES; i++) {
      // Si el sensor es '1' (Ocupado/HIGH) envía 'O', si es '0' (Libre/LOW) envía 'L'
      putchar( sensors[i] ? 'O' : 'L' ); 
      if (i < NUM_SPACES - 1) {
         putchar(','); // Separador de coma
      }
   }
   putchar('\n'); 
}

// --- Lógica de Recepción (ESP32 -> PIC) ---

#define RXBUF_LEN 32
char rxbuf[RXBUF_LEN];
int rxidx = 0;

void parse_rx_line(char *line) {
   // Espera: R:L,R,O,M  (Estado final decidido por el ESP32)
   int8 vals[NUM_SPACES]; // Códigos LED 0..3
   int8 i = 0;
   char *p = line;
   int8 is_reserved_now = 0;

   // 1. Verificar prefijo "R:" (Comando de Reserva/Control)
   if (!(p[0]=='R' && p[1]==':')) return; 
   p += 2; 

   // 2. Parsear los 4 estados de la web (L, R, O, M)
   while (*p && i < NUM_SPACES) {
      while (*p == ' ' || *p == '\t') p++;
      
      int8 led_code = -1;
      
      if (*p == 'L') led_code = 0; // Libre -> Green
      else if (*p == 'O') led_code = 1; // Ocupado -> Red
      else if (*p == 'R') led_code = 2; // Reservado -> Blue
      else if (*p == 'M') led_code = 3; // Mantenimiento -> Yellow

      if (led_code != -1) {
         vals[i] = led_code;
         if (led_code == 2) is_reserved_now = 1; // Si hay alguna R activa
         i++;
      }
      
      // Avanzar al siguiente estado
      p++;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
   }

   // 3. Aplicar estados LED y Lógica de Barrera
   if (i == NUM_SPACES) {
      for (i = 0; i < NUM_SPACES; i++) {
         set_space_led(i, vals[i]);
      }
      
      // Lógica de Barrera Centralizada:
      // La barrera se mantiene cerrada (0) si hay alguna plaza en estado 'R'
      // para obligar al usuario a interactuar. Si no hay reservas, se abre (1)
      if (is_reserved_now) {
          // set_barrier_state(0); // Cerrar si hay una reserva activa esperando
      } else {
          // set_barrier_state(1); // Abrir si no hay reservas pendientes (permitir entrada/salida)
      }
   }
}

void process_incoming_serial() {
   // Lectura no bloqueante del UART
   char c;
   while (kbhit()) {
      c = getch(); 
      if (c == '\r') continue;
      if (c == '\n') { // Fin de trama
         rxbuf[rxidx] = '\0';
         if (rxidx > 0) parse_rx_line(rxbuf);
         rxidx = 0;
      } else {
         if (rxidx < RXBUF_LEN-1) {
            rxbuf[rxidx++] = c;
         } else {
            rxidx = 0; // overflow
         }
      }
   }
}

// --- MAIN LOOP ---

void main() {
   int8 sensors[NUM_SPACES];
   int8 prev_sensors[NUM_SPACES];
   int i;
   unsigned int tickCounter = 0; // cada tick ~50 ms -> 20 ticks = 1000 ms

   // Configuración de puertos
   set_tris_b(0x00); // PORTB output (LEDs)
   set_tris_d(0x00); // PORTD output (LEDs)
   set_tris_a(0x0F); // RA0..RA3 inputs (sensores)
   set_tris_c(0x00); // PORTC output (Servos y UART TX/RX)

   // Inicializar leds (por defecto Green)
   for (i = 0; i < NUM_SPACES; i++) set_space_led(i, 0);
   // set_barrier_state(0); // Inicializar barrera cerrada

   // Leer inicial sensores
   sensors[0]=INPUT(PIN_A0);
   sensors[1]=INPUT(PIN_A1);
   sensors[2]=INPUT(PIN_A2);
   sensors[3]=INPUT(PIN_A3);
   
   for (i = 0; i < NUM_SPACES; i++) {
      prev_sensors[i] = sensors[i];
   }

   while(TRUE) {
      process_incoming_serial(); // Tarea principal 1: Recibir comandos del ESP32

      // Leer sensores
      sensors[0]=INPUT(PIN_A0);
      sensors[1]=INPUT(PIN_A1);
      sensors[2]=INPUT(PIN_A2);
      sensors[3]=INPUT(PIN_A3);

      // Tarea principal 2: Envío de estado al ESP32 (por cambio o periódico)
      
      // 1. Envío Inmediato si hay cambio
      int8 changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         if (sensors[i] != prev_sensors[i]) { changed = 1; break; }
      }
      if (changed) {
         enviar_estados_sensores(sensors);
         for (i = 0; i < NUM_SPACES; i++) prev_sensors[i] = sensors[i];
      }

      // 2. Envío Periódico cada ~1000 ms
      tickCounter++;
      if (tickCounter >= 20) {  // 20 * 50ms = 1000ms
         enviar_estados_sensores(sensors);
         tickCounter = 0;
      }

      delay_ms(50);
   }
}