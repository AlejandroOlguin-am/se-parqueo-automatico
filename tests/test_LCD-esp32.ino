#include <Wire.h>
#include <hd44780.h>                      
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ⚠️ Cambia 21 y 22 por tus pines SDA y SCL reales (si es necesario)
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

hd44780_I2Cexp lcd(0x27); // Forzamos la dirección 0x27
const int LCD_COLS = 20;
const int LCD_ROWS = 4;

void setup() {
  // 1. Usa BAUDIOS BAJOS para estabilidad (9600)
  Serial.begin(9600); 
  delay(1000); 
  Serial.println(">>> Iniciando I2C con retraso..."); 

  // 2. Inicializa el bus I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); 

  // 3. Pequeño retraso después de inicializar Wire (ayuda al ESP)
  delay(50); 
  
  // 4. Intentar inicializar la pantalla
  int status = lcd.begin(LCD_COLS, LCD_ROWS);

  if(status) {
    Serial.print("Error de inicializacion hd44780 (status: ");
    Serial.print(status);
    Serial.println(")");
  } else {
    Serial.println("LCD Inicializado OK.");
  }

  // Comandos de impresión simples
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sincronizacion OK!");
  lcd.setCursor(0, 1);
  lcd.print("9600 Baudios Fix");
  
  // Limpia el cursor parpadeante si no lo queremos
  lcd.noBlink(); 
  lcd.noCursor(); 
}

void loop() {
  // Mantener el bucle lo más vacío posible para estabilidad
}