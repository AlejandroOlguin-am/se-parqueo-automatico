// =======================================================
//  LIBRERÍA LCD 16X2 I2C (PCF8574) PARA CCS C
// =======================================================

// Pines del PCF8574:
// P0, P1, P2, P3 -> D4, D5, D6, D7 (Data)
// P4 -> RS (Register Select)
// P5 -> E (Enable)
// P6 -> RW (Read/Write - Asumimos cableado a GND)
// P7 -> Backlight (Luz de fondo)

#define PIN_RS     0b00000001
#define PIN_EN     0b00000100
#define PIN_BL     0b00001000 // Backlight (Depende del módulo)

// --- Comandos LCD ---
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME   0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET  0x20
#define LCD_SETDDRAMADDR 0x80

// --- Prototipos (Para ser usados en el .c principal) ---
void lcd_pulse_en(int8 data);
void lcd_write_nibble(int8 nibble, int8 mode);
void lcd_command(int8 cmd);
void lcd_data(int8 data);
void lcd_init(int8 address);
void lcd_gotoxy(int8 x, int8 y);
void lcd_clear();

// --- Buffer de datos (Estado actual de Backlight y pines de control) ---
int8 backlight_state = 0; // 0 o PIN_BL

// Dirección I2C usada internamente por la librería. Se configura en `lcd_init`.
int8 lcd_i2c_addr = 0;

// Función auxiliar: Genera el pulso de Enable (E)
void lcd_pulse_en(int8 data) {
    // 1. E HIGH
    i2c_start();
    i2c_write(lcd_i2c_addr << 1);
    i2c_write(data | PIN_EN);
    i2c_stop();
    delay_us(50);
    // 2. E LOW
    i2c_start();
    i2c_write(lcd_i2c_addr << 1);
    i2c_write(data & ~PIN_EN);
    i2c_stop();
    delay_us(50);
}

// Función auxiliar: Escribe 4 bits (nibble) al PCF8574
void lcd_write_nibble(int8 nibble, int8 mode) {
    int8 data = (nibble << 4) | mode | backlight_state;
    i2c_start();
    i2c_write(lcd_i2c_addr << 1);
    i2c_write(data);
    i2c_stop();
    lcd_pulse_en(data);
}

// Envía un comando de 8 bits (Instrucción)
void lcd_command(int8 cmd) {
    lcd_write_nibble(cmd >> 4, 0); // High nibble (RS=0)
    lcd_write_nibble(cmd & 0x0F, 0); // Low nibble (RS=0)
}

// Envía un byte de datos (Carácter)
void lcd_data(int8 data) {
    lcd_write_nibble(data >> 4, PIN_RS); // High nibble (RS=1)
    lcd_write_nibble(data & 0x0F, PIN_RS); // Low nibble (RS=1)
}

// Inicialización de la LCD
void lcd_init(int8 address) {
    // Establecer la dirección I2C (se pasa desde el main)
    lcd_i2c_addr = address;
    // Inicialización del display según protocolo 4-bit
    delay_ms(50); // Tiempo de espera inicial

    // 1. Función Set (3 veces con delay)
    lcd_write_nibble(0x03, 0); delay_ms(5);
    lcd_write_nibble(0x03, 0); delay_us(100);
    lcd_write_nibble(0x03, 0); delay_us(100);

    // 2. Modo 4-bit
    lcd_write_nibble(0x02, 0); delay_ms(2);
    
    // 3. Comandos finales de inicialización
    lcd_command(LCD_FUNCTIONSET | 0x08); // DL=0 (4-bit), N=1 (2 lineas), F=0 (5x8)
    lcd_command(LCD_DISPLAYCONTROL | 0x04); // D=1 (Display ON), C=0 (Cursor OFF), B=0 (Blink OFF)
    lcd_command(LCD_CLEARDISPLAY); // Limpiar pantalla
    delay_ms(2);
    lcd_command(LCD_ENTRYMODESET | 0x02); // I/D=1 (Incrementar), S=0 (Shift OFF)
    
    backlight_state = PIN_BL; // Encender backlight por defecto
}

// Borrar pantalla
void lcd_clear() {
    lcd_command(LCD_CLEARDISPLAY);
    delay_ms(2);
}

// Posicionar cursor
void lcd_gotoxy(int8 x, int8 y) {
    int8 address;
    // Fila 1 (y=1) -> 0x00..0x0F
    // Fila 2 (y=2) -> 0x40..0x4F
    if(y == 1) address = x - 1;
    else address = 0x40 + x - 1;
    
    lcd_command(LCD_SETDDRAMADDR | address);
}

// Función que se usa con printf/fprintf (sustituto de 'putc')
void lcd_putc(char c) {
   switch (c) {
      case '\f': lcd_clear(); break; // Limpiar pantalla
      case '\n': lcd_gotoxy(1, 2); break; // Salto de línea a segunda fila
      //case '\b': lcd_command(0x10); break; // Backspace
      default: lcd_data(c); break;
   }
}

// =======================================================