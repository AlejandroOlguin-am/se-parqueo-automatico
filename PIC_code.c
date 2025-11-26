/* PIC16F877A - Sistema parking (envía sensores y recibe comandos LED)
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
   // Formato: SENS:0101\r\n  (1=ocupado,0=libre)
   int8 i;
   putchar('S'); putchar('E'); putchar('N'); putchar('S'); putchar(':');
   for (i = 0; i < NUM_SPACES; i++) {
      putchar( sensors[i] ? '1' : '0' );
   }
   // CR LF
   putchar('\r'); putchar('\n');
}

#define RXBUF_LEN 32
char rxbuf[RXBUF_LEN];
int rxidx = 0;

// parsea una línea completa en rxbuf
void parse_rx_line(char *line) {
   // Espera: "LEDS:x,x,x,x"  con x en [0..3]
   int8 vals[NUM_SPACES];
   int8 i = 0;
   char *p = line;

   // Verificar prefijo "LEDS:"
   if (!(p[0]=='L' && p[1]=='E' && p[2]=='D' && p[3]=='S' && p[4]==':')) return;
   p += 5;

   while (*p && i < NUM_SPACES) {
      // Saltar espacios y tabs
      while (*p == ' ' || *p == '\t') p++;
      if (*p >= '0' && *p <= '3') {
         vals[i] = *p - '0';
         i++;
         p++;
      } else {
         // avanzar hasta coma o fin
         while (*p && *p != ',') p++;
      }
      if (*p == ',') p++;
   }

   if (i == NUM_SPACES) {
      // Aplicar estados
      for (i = 0; i < NUM_SPACES; i++) {
         set_space_led(i, vals[i]);
      }
      // Opcional: enviar ack
      putchar('A'); putchar('C'); putchar('K'); putchar(':');
      putchar('L'); putchar('E'); putchar('D'); putchar('S');
      putchar('\r'); putchar('\n');
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

   // Envío inicial
   delay_ms(200);
   send_sensor_packet(sensors);

   while(TRUE) {
      process_incoming_serial();

      // Leer sensores periódicamente
      for (i = 0; i < NUM_SPACES; i++) {
         sensors[i] = input(sensorPins[i]) ? 1 : 0;
      }

      // Si cambió algún sensor, notificar inmediatamente
      int8 changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         if (sensors[i] != prev_sensors[i]) { changed = 1; break; }
      }
      if (changed) {
         send_sensor_packet(sensors);
         for (i = 0; i < NUM_SPACES; i++) prev_sensors[i] = sensors[i];
      }

      // Envío periódico cada ~2000 ms
      tickCounter++;
      if (tickCounter >= 40) {  // ~40 * 50ms = 2000ms
         send_sensor_packet(sensors);
         tickCounter = 0;
      }

      delay_ms(50);
   }
}

