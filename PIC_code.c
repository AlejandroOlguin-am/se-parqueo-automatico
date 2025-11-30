/* PIC16F877A - Sistema Parking FINAL CORREGIDO
   CCS C - 4 MHz
   Sensores: RA0..RA3
   LEDs:     RB0..RB7, RD0..RD7
   Servo:    PIN_C0
   UART:     HC-05 (9600 baud)
*/

#include <16F877A.h>
#fuses XT, NOWDT, NOLVP, NOBROWNOUT
#use delay(clock=4000000)

/* UART Configuración */
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1, ERRORS)

#include "lcd_i2c_ale.c" // Librería LCD I2C
#define LCD_I2C_ADDRESS 0x4E
#use i2c(MASTER, SDA=PIN_C4, SCL=PIN_C3, FAST)

/* Constantes */
const int8 NUM_SPACES = 4;
// Pines Servos
#define NUM_SERVOS 4
const int8 servoPins[NUM_SERVOS] = { PIN_C0, PIN_C1, PIN_C2, PIN_E0 };

// Nuevo estado de los 4 servos: 1=Abierto, 0=Cerrado
int1 barrier_target[NUM_SERVOS] = {1, 1, 1, 1};

/* Pines Sensores */
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };

/* Mapeo LEDs */
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }
};

/* Variables Globales */
#define RXBUF_LEN 40
char recibido[RXBUF_LEN];
int8 rxidx = 0;
int1 buffer_full = 0; // Bandera para indicar al main que hay datos

char physical[NUM_SPACES];
char prev_physical[NUM_SPACES];
// Estado final recibido del ESP: L, O, R, M
char final[NUM_SPACES] = {'L', 'L', 'L', 'L'}; 


/* --- INTERRUPCIÓN RDA (Recepción Serial) --- */
#int_rda
void rda_isr() {
   char c;
   if(kbhit()) {
      c = getch();
      
      // Ignorar el retorno de carro (\r) que envía println
      if(c == '\r') return; 
      
      // Si llega nueva línea (\n), terminamos el paquete
      if(c == '\n') {
         recibido[rxidx] = '\0'; // Finalizar string correctamente
         buffer_full = 1;        // Avisar al main
         rxidx = 0;              // Resetear índice para el próximo
         return;
      }
      
      // Guardar carácter si hay espacio
      if(rxidx < (RXBUF_LEN - 1)) {
         recibido[rxidx] = c;
         rxidx++;
      }
   }
}

/* --- FUNCIONES AUXILIARES --- */

void set_space_led(int8 space, int8 state) {
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

void enviar_estados_sensores() {
   // Envía: S:L,O,L,L
   printf("S:%c,%c,%c,%c\n", physical[0], physical[1], physical[2], physical[3]);
}

// Genera pulsos para todos los servos
void update_servos() {
    for (int8 i = 0; i < NUM_SERVOS; i++) {
        if (barrier_target[i] == 1) { // Abrir
            output_high(servoPins[i]);
            delay_us(1500); 
            output_low(servoPins[i]);
        } else { // Cerrar
            output_high(servoPins[i]);
            delay_us(600); 
            output_low(servoPins[i]);
        }
    }
}

char read_sensor_debounce(int8 pin) {
   if(input(pin)) { delay_ms(10); if(input(pin)) return 'O'; }
   return 'L';
}

void parse_rx_line(char *buffer) {
   // Esperamos formato: R:X,X,X,X
   // buffer[0] debe ser 'R' y buffer[1] debe ser ':'
   if(buffer[0] != 'R' || buffer[1] != ':') return;
   
   // Puntero manual para recorrer la cadena "R:L,O,R,M"
   // Índices esperados: 
   // R : L , O , R , M
   // 0 1 2 3 4 5 6 7 8
   
   int8 i = 0;
   int8 p_idx = 2; // Empezamos después de "R:"
   int1 hay_reserva = 0;
   
   while(buffer[p_idx] != '\0' && i < NUM_SPACES) {
      char c = buffer[p_idx];
      
      if(c == 'L' || c == 'O' || c == 'R' || c == 'M') {
         final[i] = c;
         
         // Actualizar LED inmediatamente
         if(c == 'L') set_space_led(i, 0); // Verde
         if(c == 'O') set_space_led(i, 1); // Rojo
         if(c == 'R') { set_space_led(i, 2); hay_reserva = 1; } // Azul
         if(c == 'M') set_space_led(i, 3); // Amarillo
         
         // Lógica de barrera individual
      if(c == 'R') { 
            barrier_target[i] = 0; // Cerrar barrera de la plaza i si se reservó (R)
        } 
        else if (c == 'L') {
            barrier_target[i] = 1; // Abrir barrera de la plaza i si se liberó (L)
        }

         i++;
      }
      
      p_idx++;
   }
}

// Función para actualizar pantalla con información de estado
void update_lcd_info() {
   int8 libres = 0;
   int8 reservados = 0;
   int8 i;
   
   // Contar estados en array 'final' (lo que dicta el ESP32)
   for(i=0; i<NUM_SPACES; i++) {
      if(final[i] == 'L') libres++; // Estado Libre
      if(final[i] == 'R') reservados++; // Estado Reservado
   }
   
   // --- Línea 1: Libres y Reservados ---
   // Si tienes una librería estándar, usa lcd_gotoxy y printf/fprintf
   lcd_gotoxy(1,1);
   // Ejemplo: "Libres: 2 Rsv: 1"
   fprintf(lcd_putc, "Libres:%u Rsv:%u  ", libres, reservados); // Asume que tu librería usa fprintf(address, ...)
   
   // --- Línea 2: Estado del Sistema (Podemos mostrar la comunicación BT) ---
   lcd_gotoxy(1,2);
   fprintf(lcd_putc, "Bienvenido!     ");
   // Ejemplo de mensaje de estado:
   if (reservados > 0) {
      fprintf(lcd_putc, "Esperando Cliente "); 
   } else if (libres == 0) {
      fprintf(lcd_putc, "PARQUEO LLENO!  ");
   } else {
      fprintf(lcd_putc, "Bienvenido!     ");
   }
}
/* --- MAIN --- */
void main() {
   int i;
   int16 tick_counter = 0;
   int1 changed = 0;
   
   // Configuración Puertos
   set_tris_a(0x0F); // Entradas Sensores
   set_tris_b(0x00); // Salidas LEDs
   set_tris_d(0x00); // Salidas LEDs
   set_tris_c(0x40); // RC7 RX (In), RC6 TX (Out), RC0 Servo (Out)
   
   // Inicializar Hardware
   enable_interrupts(INT_RDA);
   enable_interrupts(GLOBAL);

   // --- INICIALIZACIÓN LCD ---
   lcd_init(LCD_I2C_ADDRESS); // Tu función de init debe recibir la dirección
   lcd_clear(LCD_I2C_ADDRESS); 
   fprintf(LCD_I2C_ADDRESS, "SISTEMA INICIANDO");
   delay_ms(500);
   
   // Estado Inicial
   for(i=0; i<NUM_SPACES; i++) {
      set_space_led(i, 0); // Todos Verde al inicio
      physical[i] = read_sensor_debounce(sensorPins[i]);
      prev_physical[i] = physical[i];
   }
   
   delay_ms(500); // Estabilizar
   enviar_estados_sensores();
   
   while(TRUE) {
      
      // 1. PROCESAR DATOS RECIBIDOS (Si la interrupción marcó buffer_full)
      if(buffer_full) {
         parse_rx_line(recibido);
         buffer_full = 0; // Limpiar bandera
      }
      
      // 2. LEER SENSORES
      changed = 0;
      for(i=0; i<NUM_SPACES; i++) {
         char lectura = read_sensor_debounce(sensorPins[i]);
         if(lectura != physical[i]) {
            physical[i] = lectura;
            changed = 1;
         }
      }
      
      // 3. ENVIAR SI HAY CAMBIOS
      if(changed) {
         enviar_estados_sensores();
         // Actualizar previos
         for(i=0; i<NUM_SPACES; i++) prev_physical[i] = physical[i];
      }
      
      // 4. ENVÍO PERIÓDICO (Heartbeat) - Cada ~2 segundos
      tick_counter++;
      if(tick_counter >= 100) { // 100 * 20ms = 2000ms
         enviar_estados_sensores();

         update_lcd_info();

         tick_counter = 0;
      }
      
      // 5. CONTROL SERVO
      update_servos();
      
      delay_ms(20);
   }
}