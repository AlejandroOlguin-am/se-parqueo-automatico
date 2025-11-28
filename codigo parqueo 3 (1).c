#include <16F877A.h>
#fuses XT, NOWDT
#use delay(clock=4000000)
#use rs232(baud=9600, xmit=PIN_C6, rcv=PIN_C7)


#define RXBUF_LEN 12
volatile char recibido[RXBUF_LEN];   // buffer compartido ISR <-> main
volatile int8 rxidx = 0;            // �ndice de escritura en ISR
volatile int1 recibir = 0;           // flag: paquete listo

//*******************************************************************Sensores envio *******************************************************************
const int8 NUM_SPACES = 4;
// Pines sensores
//const int8 sensorPins[NUM_SPACES] = { PIN_A0, PIN_A1, PIN_A2, PIN_A3 };


void recibir_estados_sensores(int8 sensors[]) {
   // Formato: SENS:0101\r\n  (1=ocupado,0=libre)
   printf("#,%d,%d,%d,%d;\r\n", sensors[0], sensors[1],sensors[2], sensors[3]);
   }
//*******************************************************************Sensores envio *******************************************************************


#int_rda
void rda_isr(){
   char c;
   if (!kbhit()) return;          // por seguridad
   c = getc();                    // lee un �nico byte

   // Si estamos en overflow, descartamos hasta fin de l�nea
   if (rxidx >= RXBUF_LEN-1) {
      if (c == '\r' || c == '\n') rxidx = 0; // sincroniza en pr�ximo paquete
      return;
   }

   // Guardar car�cter (no terminamos aqu� para mantener ISR corta)
   recibido[rxidx++] = c;

   // Si es terminador, cerrar cadena y marcar listo
   if (c == '\r' || c == '\n') {
      // sustituir terminador por NULL y reducir �ndice para pr�ximo mensaje
      recibido[rxidx-1] = '\0';
      recibir = 1;
      // Prepara para el pr�ximo paquete
      rxidx = 0;
   }
}


void main() {

   enable_interrupts(int_rda);
   enable_interrupts(global);

//*******************************************************************Sensores envio *******************************************************************
    // --- DESACTIVAR ADC y COMPARADORES para usar PORTA como digital ---
   setup_adc_ports(NO_ANALOGS);   // pone PORTA como digital
   setup_adc(ADC_OFF);
   setup_comparator(NC_NC_NC_NC);

   // ---------------------------------------------------------------
   int8 sensors[NUM_SPACES];
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

   // Env�o inicial
   delay_ms(200);
   
   enviar_estados_sensores(sensors);
//*******************************************************************Sensores envio *******************************************************************



   while(TRUE) {
  
  
  
  //*******************************************************************Sensores envio *******************************************************************
      // Leer sensores peri�dicamente
   sensors[0]=INPUT(PIN_A0);
   sensors[1]=INPUT(PIN_A1);
   sensors[2]=INPUT(PIN_A2);
   sensors[3]=INPUT(PIN_A3);
      // Si cambi� alg�n sensor, notificar inmediatamente
      int8 changed = 0;
      for (i = 0; i < NUM_SPACES; i++) {
         if (sensors[i] != prev_sensors[i]) { changed = 1; break; }
      }
      if (changed==1) {
         enviar_estados_sensores(sensors);
         for (i = 0; i < NUM_SPACES; i++) prev_sensors[i] = sensors[i];
      }
// Env�o peri�dico cada ~1000 ms
      tickCounter++;
      if (tickCounter >= 20) {  // ~40 * 50ms = 2000ms
         enviar_estados_sensores(sensors);
         delay_ms(50);
         tickCounter = 0;
      }
//*******************************************************************Sensores envio *******************************************************************      
     if (recibir==1) {
         // copiar buffer de ISR a local de forma at�mica
         char copia[RXBUF_LEN];
         disable_interrupts(INT_RDA);   // breve secci�n cr�tica
         strcpy(copia, (char*)recibido); // copiar la cadena terminada por ISR
         recibir = 0;
         enable_interrupts(INT_RDA);

         // ahora procesar la copia fuera de la ISR
         // por ejemplo: imprimirla por debug
     
           // S�lo procesar si hay contenido real
         if (copia[0] != '\0') {
            printf("RCV: %s\r\n", copia);
            // parse_message(copia);  // tu parser
         }

         // o actuar seg�n comando recibido:
         // parse_message(copia);
      }
      delay_ms(50);

      
      
   }
}

