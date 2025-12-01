/* PIC16F877A - Sistema Parking FINAL CORREGIDO
   CCS C - 20 MHz (Corregido FUSE HS)
*/

#include <16F877A.h>
// IMPORTANTE: Cambiado XT a HS (Obligatorio para cristales > 4MHz)
#fuses HS, NOWDT, NOLVP, NOBROWNOUT
#use delay(clock=20000000)

/* UART Configuración */
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7, bits=8, parity=N, stop=1, ERRORS)

// Configuración I2C
#use I2C(MASTER, SDA=PIN_C4, SCL=PIN_C3, FAST)

#define ADDRESS_LCD 0x4E
#include <LCD_I2C.c> // Asegúrate de que esta librería use las funciones I2C de CCS

/* Constantes y Variables */
const int8 NUM_SPACES = 4;
#define NUM_SERVOS 4
const int8 servoPins[NUM_SERVOS] = { PIN_C0, PIN_C1, PIN_C2, PIN_E0 };
int1 barrier_target[NUM_SERVOS] = {1, 1, 1, 1};

const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };
const int8 ledPins[NUM_SPACES][4] = {
   { PIN_B0, PIN_B1, PIN_B2, PIN_B3 },
   { PIN_B4, PIN_B5, PIN_B6, PIN_B7 },
   { PIN_D0, PIN_D1, PIN_D2, PIN_D3 },
   { PIN_D4, PIN_D5, PIN_D6, PIN_D7 }
};

#define RXBUF_LEN 40
char recibido[RXBUF_LEN];
int8 rxidx = 0;
int1 buffer_full = 0;

char physical[NUM_SPACES];
char prev_physical[NUM_SPACES];
char final[NUM_SPACES] = {'L', 'L', 'L', 'L'}; 

/* --- INTERRUPCIÓN RDA --- */
#int_rda
void rda_isr() {
   char c;
   if(kbhit()) {
      c = getch();
      if(c == '\r') return; 
      if(c == '\n') {
         recibido[rxidx] = '\0';
         buffer_full = 1;        
         rxidx = 0;              
         return;
      }
      if(rxidx < (RXBUF_LEN - 1)) {
         recibido[rxidx] = c;
         rxidx++;
      }
   }
}

/* --- FUNCIONES --- */
void set_space_led(int8 space, int8 state) {
   int8 i;
   for (i = 0; i < 4; i++) {
      if (i == state) output_high( ledPins[space][i] );
      else output_low( ledPins[space][i] );
   }
}

void enviar_estados_sensores() {
   // Enviamos \r\n explícito para ayudar al parser del ESP32
   printf("S:%c,%c,%c,%c\r\n", physical[0], physical[1], physical[2], physical[3]);
}

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
   if(buffer[0] != 'R' || buffer[1] != ':') return;
   
   int8 i = 0;
   int8 p_idx = 2;
   
   while(buffer[p_idx] != '\0' && i < NUM_SPACES) {
      char c = buffer[p_idx];
      if(c == 'L' || c == 'O' || c == 'R' || c == 'M') {
         final[i] = c;
         // Actualización de LEDs y Barreras según lógica recibida
         if(c == 'L') { set_space_led(i, 0); barrier_target[i] = 1; }
         if(c == 'O') { set_space_led(i, 1); } // Ocupado, barrera se queda como estaba o manual
         if(c == 'R') { set_space_led(i, 2); barrier_target[i] = 0; }
         if(c == 'M') { set_space_led(i, 3); }
         i++;
      }
      p_idx++;
   }
}

void update_lcd_info() {
   int8 libres = 0;
   int8 reservados = 0;
   int8 i;
   
   for(i=0; i<NUM_SPACES; i++) {
      if(final[i] == 'L') libres++;
      if(final[i] == 'R') reservados++;
   }
   
   lcd_gotoxy(1,1);
   printf(lcd_putc, "Libres:%u Rsv:%u  ", libres, reservados);
   
   lcd_gotoxy(1,2);
   if (reservados > 0) {
      lcd_putc("Esperando...    ");
   } else if (libres == 0) {
      lcd_putc("LLENO!          ");
   } else {
      lcd_putc("Bienvenido      ");
   }
}

void main() {
   int i;
   int16 tick_counter = 0;
   int1 changed = 0;
   
   set_tris_a(0x0F);
   set_tris_b(0x00);
   set_tris_d(0x00);
   set_tris_c(0x98); // RC7(RX)=In, RC6(TX)=Out, RC3/4(I2C)
   
   enable_interrupts(INT_RDA);
   enable_interrupts(GLOBAL);

   lcd_init();
   delay_ms(100); // Dar tiempo al LCD para iniciar
   lcd_clear(); 
   printf(lcd_putc, "SISTEMA INICIADO");
   delay_ms(1000);
   lcd_clear();
   
   // Estado Inicial
   for(i=0; i<NUM_SPACES; i++) {
      set_space_led(i, 0); 
      physical[i] = read_sensor_debounce(sensorPins[i]);
      prev_physical[i] = physical[i];
   }
   
   enviar_estados_sensores();
   
   while(TRUE) {
      // 1. Procesar Bluetooth
      if(buffer_full) {
         parse_rx_line(recibido);
         buffer_full = 0;
      }
      
      // 2. Sensores
      changed = 0;
      for(i=0; i<NUM_SPACES; i++) {
         char lectura = read_sensor_debounce(sensorPins[i]);
         if(lectura != physical[i]) {
            physical[i] = lectura;
            changed = 1;
         }
      }
      
      if(changed) {
         enviar_estados_sensores();
         for(i=0; i<NUM_SPACES; i++) prev_physical[i] = physical[i];
      }
      
      // 3. Heartbeat y LCD
      tick_counter++;
      if(tick_counter >= 50) { // Ajustado tiempo (aprox 1s)
         // NO DESHABILITAMOS INTERRUPCIONES AQUI
         // El buffer RDA y los 20MHz del PIC pueden manejarlo.
         update_lcd_info(); 
         
         // Pequeño delay para estabilizar voltaje tras uso de LCD antes de transmitir
         delay_ms(10); 
         enviar_estados_sensores();
         
         tick_counter = 0;
      }
      
      update_servos();
      delay_ms(20);
   }
}