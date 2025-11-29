/* PIC16F877A - Sistema Parking Unificado (ISR para UART)
   CCS C - 4 MHz
   Sensores: RA0..RA3
   LEDs:     RB0..RB7, RD0..RD7 (4 espacios x 4 leds cada uno)
   Servo (barrera central): RC0 (pulso repetido no bloqueante)
   UART: baud 9600 (HC-05) - ISR rda_isr para recepción
*/

/* Configuración */
#include <16F877A.h>
#fuses XT, NOWDT, NOLVP, NOBROWNOUT
#use delay(clock=4000000)

/* UART a HC-05 */
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1)

/* Constantes */
const int8 NUM_SPACES = 4;
#define SERVO_PIN PIN_C0

/* Pines sensores */
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };

/* Mapeo LEDs [space][color]  color: 0=Green(L),1=Red(O),2=Blue(R),3=Yellow(M) */
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },  // space 0
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },  // space 1
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },  // space 2
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }   // space 3
};

/* RX buffer para ISR */
#define RXBUF_LEN  64
volatile char recibido[RXBUF_LEN];
volatile int8 rxidx = 0;       // indice de escritura (ISR)
volatile int1 recibir = 0;     // flag: paquete listo (ISR -> main)

/* Estados */
char physical[NUM_SPACES];     // 'L' o 'O'
char final[NUM_SPACES];        // 'L','O','R','M' recibido del ESP
char prev_physical[NUM_SPACES];

/* Barrera no bloqueante */
volatile int1 barrier_target = 1; // 1=open, 0=closed (estado deseado)
volatile int1 barrier_pulse_pending = 0; // pulse generado esta iter

/* Prototipos */
void set_space_led(int8 space, int8 state);
void enviar_estados_sensores(void);
void parse_rx_line(char *line);
void process_incoming_serial_ISRcopy(void);
char read_sensor_debounce(int8 pin);
void servo_pulse_once(); // emite un pulso (según barrier_target)
int any_reserved();

/* -------------------- ISR UART -------------------- */
#int_rda
void rda_isr() {
   char c;
   if (!kbhit()) return;    // seguridad
   c = getch();             // lee 1 byte

   // overflow handling: si buffer lleno, ignoramos hasta terminador y resincronizamos
   if (rxidx >= RXBUF_LEN - 1) {
      if (c == '\r' || c == '\n') rxidx = 0;
      return;
   }

   recibido[rxidx++] = c;

   if (c == '\r' || c == '\n') {
      recibido[rxidx-1] = '\0'; // sustituir terminador por NULL
      recibir = 1;              // marcar paquete listo para main
      rxidx = 0;                // preparar para siguiente
   }
}
/* ------------------------------------------------- */

/* Encender solo el LED correspondiente en una plaza */
void set_space_led(int8 space, int8 state) {
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

/* Construir y enviar trama de sensores: S:L,O,L,L\n (solo estado físico) */
void enviar_estados_sensores() {
   putchar('S'); putchar(':');
   int i;
   for (i = 0; i < NUM_SPACES; i++) {
      putchar( physical[i] );
      if (i < NUM_SPACES - 1) putchar(',');
   }
   putchar('\n');
}

/* Devuelve 1 si hay alguna plaza en 'R' dentro de final[] */
int any_reserved() {
   int j;
   for (j = 0; j < NUM_SPACES; j++) if (final[j] == 'R') return 1;
   return 0;
}

/* Pulsar servo una vez según barrier_target (no bloqueante) */
void servo_pulse_once() {
   // Generamos un pulso simple; el main debe llamar esto cada ~20 ms
   if (barrier_target) {
      // abrir -> pulso ~1500us (approx 90°)
      output_high(SERVO_PIN);
      delay_us(1500);
      output_low(SERVO_PIN);
   } else {
      // cerrar -> pulso ~600us (approx 0°)
      output_high(SERVO_PIN);
      delay_us(600);
      output_low(SERVO_PIN);
   }
   // marcamos que ya pulsemos en esta iter
   barrier_pulse_pending = 1;
}

/* Parser de la línea completa (R:L,O,R,L por ejemplo) */
void parse_rx_line(char *line) {
   char *p = line;
   int idx = 0;
   if (!(p[0] == 'R' && p[1] == ':')) {
      // no es comando R:, ignorar
      return;
   }
   p += 2;

   // parsear 4 estados separados por coma
   while (*p && idx < NUM_SPACES) {
      while (*p == ' ' || *p == '\t') p++;
      char c = *p;
      if (c == 'L' || c == 'O' || c == 'R' || c == 'M') {
         final[idx] = c;
         // actualizar LED inmediatamente segun final:
         switch (c) {
            case 'L': set_space_led(idx, 0); break; // verde
            case 'O': set_space_led(idx, 1); break; // rojo
            case 'R': set_space_led(idx, 2); break; // azul
            case 'M': set_space_led(idx, 3); break; // amarillo
         }
         idx++;
      }
      // avanzar hasta siguiente coma o fin
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
   }

   // ajustar barrera_target: si hay alguna R -> cerrar (0), si no -> abrir (1)
   //if (any_reserved()) barrier_target = 0;
   //else barrier_target = 1;

   // ACK para debug (confirmación de aplicación)
   printf("ACK:R,OK\n");
}

/* Leer sensores con debounce rápido (3 lecturas, 10ms entre ellas) */
char read_sensor_debounce(int8 pin) {
   int v1, v2, v3;
   v1 = input(pin);
   delay_ms(10);
   v2 = input(pin);
   delay_ms(10);
   v3 = input(pin);
   int sum = v1 + v2 + v3;
   return (sum >= 2) ? 'O' : 'L';
}

/* Copia segura (atomica) del buffer ISR y procesar */
void process_incoming_serial_ISRcopy() {
   if (!recibir) return;
   char copia[RXBUF_LEN];

   disable_interrupts(INT_RDA);
   strcpy(copia, (char*)recibido);
   recibir = 0;
   enable_interrupts(INT_RDA);

   if (copia[0] != '\0') {
      // Debug: mostrar lo recibido
      printf("RCV: %s\r\n", copia);
      parse_rx_line(copia);
   }
}

/* -------------------- MAIN -------------------- */
void main() {
   int i;
   unsigned int tickCounter = 0;
   int8 changed;

   /* Configuracion puertos:
      - PORTA RA0..RA3 inputs (sensores)
      - PORTB, PORTD outputs (LEDs)
      - PORTC: RC7 must be input (RX), RC6 TX output, RC0 servo output
      TRISC bit7 = 1 (input), bit6 = 0 (output)
   */
   set_tris_b(0x00);   // PORTB outputs (LEDs)
   set_tris_d(0x00);   // PORTD outputs (LEDs)
   set_tris_a(0x0F);   // RA0..RA3 inputs
   set_tris_c(0x80);   // RC7 input (bit7=1), others outputs (incl. RC0, RC6)

   /* Inicializar arrays */
   for (i = 0; i < NUM_SPACES; i++) {
      final[i] = 'L';
      physical[i] = read_sensor_debounce(sensorPins[i]);
      prev_physical[i] = physical[i];
      set_space_led(i, 0); // verde por defecto
   }

   /* Habilitar interrupciones UART */
   enable_interrupts(INT_RDA);
   enable_interrupts(GLOBAL);

   /* Enviar estado inicial */
   delay_ms(200);
   enviar_estados_sensores();

   /* Loop principal */
   while (TRUE) {
      /* 1) procesar recepción completa (copia atómica desde ISR) */
      process_incoming_serial_ISRcopy();

      /* 2) leer sensores (debounce) */
      changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         char newp = read_sensor_debounce(sensorPins[i]);
         if (newp != physical[i]) {
            physical[i] = newp;
            changed = 1;
         }
      }

      /* 3) si cambió algun sensor, notificar */
      if (changed) {
         enviar_estados_sensores();
         for (i = 0; i < NUM_SPACES; i++) prev_physical[i] = physical[i];
      }

      /* 4) envío periódico (opcional) cada ~1s */
      tickCounter++;
      if (tickCounter >= 50) { // con delay_ms(20) -> 50 * 20ms = 1000ms
         enviar_estados_sensores();
         tickCounter = 0;
      }

      /* 5) Generar un pulso de servo NO bloqueante cada iteración
            Esto permite mantener posición sin bloquear el loop */
      servo_pulse_once();
      barrier_pulse_pending = 0; // marcador por si se usa en lógica extra

      /* 6) pacing: ~20ms por iteración (adecuado para servo ~50Hz) */
      delay_ms(20);
   } /* end while */
}
/* ------------------ end main ------------------- */
