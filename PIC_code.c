/* PIC16F877A - Sistema Parking Unificado
   CCS C - 4 MHz
   Sensores: RA0..RA3
   LEDs:     RB0..RB7, RD0..RD7 (4 espacios x 4 leds cada uno)
   Servo (barrera central): PIN_C0
   UART: baud 9600 (HC-05)
*/

/* FUSES y configuración */
#include <16F877A.h>
#fuses XT, NOWDT, NOLVP, NOBROWNOUT
#use delay(clock=4000000)

/* UART hacia HC-05 */
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1)

/* Constantes */
const int8 NUM_SPACES = 4;
#define SERVO_ENTRY_PIN PIN_C0

/* Pines sensores */
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };

/* Mapeo LEDs [space][color]  color: 0=Green(L),1=Red(O),2=Blue(R),3=Yellow(M) */
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },  // space 0
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },  // space 1
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },  // space 2
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }   // space 3
};

/* Buffers RX */
#define RXBUF_LEN  48
char rxbuf[RXBUF_LEN];
int rxidx = 0;

/* Estados locales */
char physical[NUM_SPACES]; // 'L' o 'O' (según sensor)
char final[NUM_SPACES];    // 'L','O','R','M' <-- decidido por ESP (recibido)
char prev_physical[NUM_SPACES]; // para detectar cambios

/* Prototipos */
void set_space_led(int8 space, int8 state);
void set_barrier_open();   // abrir (1)
void set_barrier_close();  // cerrar (0)
char read_sensor_stable(int8 pin);
void enviar_estados_sensores(void);
void parse_rx_line(char *line);
void process_incoming_serial(void);

/* Implementaciones */

/* Enciende un LED (solo uno) por plaza según state:
   state: 0=Green(L), 1=Red(O), 2=Blue(R), 3=Yellow(M)  */
void set_space_led(int8 space, int8 state) {
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

/* Barrera: simplificada (usa pulso); notar que servo ideal requiere PWM continuo.
   CERRAR = lower position (por ejemplo 0°), ABRIR = raised (90°) */
void set_barrier_close() {
    // Pulsos cortos; en la práctica usar PWM por timer para mantener posición estable
    // Aquí enviamos varios pulsos rápidos para intentar fijar la posición
    int k;
    for (k=0; k<25; k++) {           // ~25 pulsos => aproximación a 20-50Hz por bloque
        output_high(SERVO_ENTRY_PIN);
        delay_us(600);               // ~0.6 ms -> cerca de 0°
        output_low(SERVO_ENTRY_PIN);
        delay_ms(20);                // período ~20ms
    }
}

void set_barrier_open() {
    int k;
    for (k=0; k<25; k++) {
        output_high(SERVO_ENTRY_PIN);
        delay_us(1500);              // ~1.5 ms -> aproximación a 90°
        output_low(SERVO_ENTRY_PIN);
        delay_ms(20);
    }
}

/* Debounce simple: lee 3 veces con 20ms y devuelve el valor mayoritario
   Retorna 'L' si 0 (LOW) o 'O' si 1 (HIGH) */
char read_sensor_stable(int8 pin) {
    int v1, v2, v3;
    v1 = input(pin);
    delay_ms(20);
    v2 = input(pin);
    delay_ms(20);
    v3 = input(pin);
    int sum = v1 + v2 + v3;
    if (sum >= 2) return 'O'; // ocupado
    return 'L'; // libre
}

/* Enviar estado FÍSICO (S:L,O,L,L\n) por UART — mantiene el formato que ya tenías */
void enviar_estados_sensores() {
   // Formato: S:L,O,L,L\n
   putchar('S'); putchar(':');
   int i;
   for (i = 0; i < NUM_SPACES; i++) {
      putchar( physical[i] );
      if (i < NUM_SPACES - 1) putchar(',');
   }
   putchar('\n');
}

/* Procesa una línea recibida por UART.
   Espera tramas del tipo R:L,O,R,L  (sin el \n) */
void parse_rx_line(char *line) {
   char *p = line;
   int i = 0;
   int8 led_code;

   // Validar prefijo R:
   if (!(p[0]=='R' && p[1]==':')) return;
   p += 2;

   // Parsear hasta 4 estados separados por coma
   while (*p && i < NUM_SPACES) {
      // saltar espacios
      while (*p == ' ' || *p == '\t') p++;
      char c = *p;
      if (c == 'L' || c == 'O' || c == 'R' || c == 'M') {
         final[i] = c; // actualizar estado final de la plaza i
         // Actualizar LED inmediatamente según final:
         switch (c) {
            case 'L': led_code = 0; break; // Verde
            case 'O': led_code = 1; break; // Rojo
            case 'R': led_code = 2; break; // Azul
            case 'M': led_code = 3; break; // Amarillo
            default: led_code = 0; break;
         }
         set_space_led(i, led_code);
         i++;
      }
      // avanzar hasta próximo separador o fin
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
   }

   // Una vez parseado, evaluar la barrera central:
   {
      int any_reserved = 0;
      int j;
      for (j = 0; j < NUM_SPACES; j++) {
         if (final[j] == 'R') { any_reserved = 1; break; }
      }
      if (any_reserved) {
         // cerrar barrera (hay al menos una reserva)
         //set_barrier_close();
      } else {
         // abrir barrera (ninguna reserva)
         //set_barrier_open();
      }
   }

   // Opcional: enviar ACK de recepción para debug (descomentar si lo quieres)
   printf("ACK:R,OK\n");
}

/* Lee serial no bloqueante y arma líneas hasta '\n' */
void process_incoming_serial() {
   char c;

   while (kbhit()) {
      c = getch();
      if (c == '\r') continue;
      if (c == '\n') {
         if (rxidx > 0) {
             rxbuf[rxidx] = '\0';
             parse_rx_line(rxbuf);
         }
         rxidx = 0;
      } else {
         if (rxidx < RXBUF_LEN - 1) {
            rxbuf[rxidx++] = c;
         } else {
            // overflow -> resetear buffer
            rxidx = 0;
         }
      }
   }
}

/* MAIN */
void main() {
   int i;
   unsigned int tickCounter = 0; // pacing para envío periódico
   int8 changed;

   /* Configuración de puertos */
   set_tris_b(0x00); // PORTB outputs (LEDs)
   set_tris_d(0x00); // PORTD outputs (LEDs)
   set_tris_a(0x0F); // RA0..RA3 inputs (sensores)
   set_tris_c(0x80); // PORTC outputs (servo y UART TX/RX)

   /* Inicializar LEDs y arrays */
   for (i = 0; i < NUM_SPACES; i++) {
      final[i] = 'L';         // por defecto libre
      physical[i] = read_sensor_stable(sensorPins[i]);
      prev_physical[i] = physical[i];
      set_space_led(i, 0);    // verde
   }

   // Enviar estado inicial al ESP
   enviar_estados_sensores();

   /* Loop principal */
   while(TRUE) {

      // 2) Leer sensores con debounce
      changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         char newp = read_sensor_stable(sensorPins[i]);
         if (newp != physical[i]) {
            physical[i] = newp;
            changed = 1;
         }
      }

      // 3) Enviar si hubo cambio en sensores
      if (changed) {
         enviar_estados_sensores();
         for (i = 0; i < NUM_SPACES; i++) prev_physical[i] = physical[i];
      }

      // 4) Envío periódico cada ~1000 ms (opcional, mantiene sincronía)
      tickCounter++;
      if (tickCounter >= 20) { // 20 * ~50ms = ~1000ms (el delay_ms al final define el tick)
         enviar_estados_sensores();
         tickCounter = 0;
      }

      delay_ms(50); // pacing del loop

      // 1) Procesar serial entrante (comandos R:)
      process_incoming_serial();
   }
}
