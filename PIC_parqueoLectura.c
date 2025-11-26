#include <16F877A.h>
#fuses XT, NOWDT
#use delay(clock=4000000)
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7)

const int8 NUM_SPACES = 4;

// Pines sensores
const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };


void enviar_estados_sensores(int8 sensors[]) {
   // Formato: SENS:0101\r\n  (1=ocupado,0=libre)
   printf("#,%d,%d,%d,%d;\r\n", sensors[0], sensors[1],sensors[2], sensors[3]);
   }

void main() {
    // --- DESACTIVAR ADC y COMPARADORES para usar PORTA como digital ---
   setup_adc_ports(NO_ANALOGS);   // pone PORTA como digital
   setup_adc(ADC_OFF);
   setup_comparator(NC_NC_NC_NC);

   // ---------------------------------------------------------------
   int8 sensors[NUM_SPACES]= { false, false, false, false };
;
   int8 prev_sensors[NUM_SPACES];
   int i;
   unsigned int tickCounter = 0; // cada tick ~50 ms -> 40 ticks = 2000 ms

   
   //ASIGNANDO COMO ENTRADAS
   set_tris_a(0x0F); // RA0..RA3 inputs (sensores), otros como output
  // Leer inicial sensores
   sensors[0]=INPUT(PIN_A0);
   sensors[1]=INPUT(PIN_A1);
   sensors[2]=INPUT(PIN_A2);
   sensors[3]=INPUT(PIN_A3);
   
 
   for (i = 0; i < NUM_SPACES; i++) {
      prev_sensors[i] = sensors[i];
   }

   // Envío inicial
   delay_ms(200);
   
   enviar_estados_sensores(sensors);

   while(TRUE) {
   
      // Leer sensores periódicamente
   sensors[0]=INPUT(PIN_A0);
   sensors[1]=INPUT(PIN_A1);
   sensors[2]=INPUT(PIN_A2);
   sensors[3]=INPUT(PIN_A3);
      // Si cambió algún sensor, notificar inmediatamente
      int8 changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         if (sensors[i] != prev_sensors[i]) { changed = 1; break; }
      }
      if (changed==1) {
         enviar_estados_sensores(sensors);
         for (i = 0; i < NUM_SPACES; i++) prev_sensors[i] = sensors[i];
      }
// Envío periódico cada ~1000 ms
      tickCounter++;
      if (tickCounter >= 20) {  // ~40 * 50ms = 2000ms
         enviar_estados_sensores(sensors);
         delay_ms(50);
         tickCounter = 0;
      }

      delay_ms(50);
   }
}

