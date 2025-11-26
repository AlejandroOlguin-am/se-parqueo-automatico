/* PIC16F877A - Sistema parking (env�a sensores y recibe comandos LED)
   CCS C - 4 MHz
   Sensores: RA0..RA3
   LEDs:     RB0..RB7, RD0..RD7 (4 espacios x 4 leds cada uno)
   UART: baud 9600 (HC-05)
*/

#include <16F877A.h>
#fuses XT, NOWDT, NOLVP, NOBROWNOUT
#use delay(clock=4000000)

// UART hacia HC-05
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1)

#define NUM_SPACES 4

// Pines sensores
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };

// Mapeo LEDs [space][color]  color: 0=Green,1=Red,2=Blue,3=Yellow
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },  // space 0
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },  // space 1
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },  // space 2
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }   // space 3
};

void set_space_led(int8 space, int8 state) {
   // state: 0=Green,1=Red,2=Blue,3=Yellow
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

void send_sensor_packet(int8 sensors[]) {
   // Nuevo Formato: S:L,O,L,L\n  (L=Libre, O=Ocupado)
   int8 i;
   putchar('S'); putchar(':'); // Prefijo S:

   for (i = 0; i < NUM_SPACES; i++) {
      // Si el sensor es '1' (Ocupado) envía 'O', si es '0' (Libre) envía 'L'
      putchar( sensors[i] ? 'O' : 'L' ); 
      if (i < NUM_SPACES - 1) {
         putchar(','); // Separador de coma
      }
   }
   // Nuevo fin de línea: solo \n (El ESP32 lo gestiona mejor)
   putchar('\n'); 
   // Opcional: Quitar el delay_ms(200) y send_sensor_packet(sensors) del main() para empezar más rápido
}
#define RXBUF_LEN 32
char rxbuf[RXBUF_LEN];
int rxidx = 0;

// parsea una l�nea completa en rxbuf
void parse_rx_line(char *line) {
   // Espera: R:L,R,O,M
   int8 vals[NUM_SPACES]; // Aquí almacenaremos los códigos 0, 1, 2, 3
   int8 i = 0;
   char *p = line;

   // Verificar prefijo "R:" (Comando de Reserva/Control)
   if (!(p[0]=='R' && p[1]==':')) return; 
   p += 2; // Avanzar después de R:

   while (*p && i < NUM_SPACES) {
      // Saltar espacios y tabs
      while (*p == ' ' || *p == '\t') p++;

      int8 led_code = -1;

      if (*p == 'L') led_code = 0; // Libre -> Green
      else if (*p == 'O') led_code = 1; // Ocupado -> Red
      else if (*p == 'R') led_code = 2; // Reservado -> Blue
      else if (*p == 'M') led_code = 3; // Mantenimiento -> Yellow

      if (led_code != -1) {
         vals[i] = led_code;
         i++;
      }

      // Avanzar hasta la coma o fin
      p++;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
   }

   if (i == NUM_SPACES) {
      // Aplicar estados
      for (i = 0; i < NUM_SPACES; i++) {
         set_space_led(i, vals[i]);
      }
   }
}

void process_incoming_serial() {
   // Lectura no bloqueante del UART (CCS: kbhit() y getch())
   char c;
   while (kbhit()) {
      c = getch(); // lee un byte
      // Normalizar fin de linea
      if (c == '\r') continue;
      if (c == '\n') {
         rxbuf[rxidx] = '\0';
         if (rxidx > 0) parse_rx_line(rxbuf);
         rxidx = 0;
      } else {
         if (rxidx < RXBUF_LEN-1) {
            rxbuf[rxidx++] = c;
         } else {
            // overflow => reset buffer
            rxidx = 0;
         }
      }
   }
}

void main() {
   int8 sensors[NUM_SPACES];
   int8 prev_sensors[NUM_SPACES];
   int i;
   unsigned int tickCounter = 0; // cada tick ~50 ms -> 40 ticks = 2000 ms

   // Config puerto
   set_tris_b(0x00); // PORTB output (LEDs)
   set_tris_d(0x00); // PORTD output (LEDs)
   set_tris_a(0x0F); // RA0..RA3 inputs (sensores), otros como output

   // Inicializar leds (por defecto Green)
   for (i = 0; i < NUM_SPACES; i++) set_space_led(i, 0);

   // Leer inicial sensores
   for (i = 0; i < NUM_SPACES; i++) {
      sensors[i] = input(sensorPins[i]) ? 1 : 0;
      prev_sensors[i] = sensors[i];
   }

   // Env�o inicial
   //delay_ms(200);
   //send_sensor_packet(sensors);

   while(TRUE) {
      process_incoming_serial();

      // Leer sensores peri�dicamente
      for (i = 0; i < NUM_SPACES; i++) {
         sensors[i] = input(sensorPins[i]) ? 1 : 0;
      }

      // Si cambi� alg�n sensor, notificar inmediatamente
      int8 changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         if (sensors[i] != prev_sensors[i]) { changed = 1; break; }
      }
      if (changed) {
         send_sensor_packet(sensors);
         for (i = 0; i < NUM_SPACES; i++) prev_sensors[i] = sensors[i];
      }

      // Env�o peri�dico cada ~2000 ms
      tickCounter++;
      if (tickCounter >= 40) {  // ~40 * 50ms = 2000ms
         send_sensor_packet(sensors);
         tickCounter = 0;
      }

      delay_ms(50);
   }
}

