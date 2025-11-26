#include "BluetoothSerial.h"
#include "esp_bt_device.h"   // para esp_bt_dev_get_address()

BluetoothSerial SerialBT;
bool wasClient = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  // Inicia SPP en modo esclavo (server) con nombre visible
  if (!SerialBT.begin("ESP32_BT")) {
    Serial.println("Error iniciando BluetoothSerial");
    while (1) delay(1000);
  }

  // setPin requiere 2 argumentos: (pin, longitud)
  SerialBT.setPin("1234", 4); // Asegúrate que el HC-05 tenga PIN 1234

  Serial.println("ESP32 listo como ESCLAVO Bluetooth (SPP).");
  Serial.println("Nombre Bluetooth: ESP32_BT  | PIN: 1234");

  // Imprime la MAC Bluetooth del ESP32 (formato XX:XX:XX:XX:XX:XX)
  const uint8_t* mac = esp_bt_dev_get_address();
  Serial.print("MAC ESP32: ");
  for (int i = 0; i < 6; ++i) {
    if (i) Serial.print(":");
    Serial.printf("%02X", mac[i]);
  }
  Serial.println();
  Serial.println("Esperando conectarse un maestro (HC-05)...");
}

void loop() {
  bool client = SerialBT.hasClient();
  if (client && !wasClient) {
    Serial.println("Cliente CONECTADO!");
    wasClient = true;
  } else if (!client && wasClient) {
    Serial.println("Cliente DESCONECTADO!");
    wasClient = false;
  }

  // Reenvío serial <-> bluetooth
  if (Serial.available()) SerialBT.write(Serial.read());
  if (SerialBT.available()) Serial.write(SerialBT.read());

  delay(20);
}
