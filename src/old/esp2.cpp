//https://docs.espressif.com/projects/arduino-esp32/en/latest/api/timer.html
#include <Arduino.h>   // Biblioteca base do framework Arduino, necessária para funções básicas como Serial e delays.

#define LASEC_MAX_TASKS 3
#include "util/lasecTask.h"

#include "services/lasecNet.h"
#include "services/wserial.h"
#include "services/ads1115.h"
#include "services/display_ssd1306.h"
#include "util/lasecDebounce.h"

constexpr uint8_t def_pin_D1 = 23;
constexpr uint8_t def_pin_D2 = 19;
constexpr uint8_t def_pin_SCL = 22;     ///< GPIO para SCL do display OLED.
constexpr uint8_t def_pin_SDA = 21;     ///< GPIO para SDA do display OLED.

//Funçao de alterar o estado de um led
void blinkLEDFunc(uint8_t pin) {
    digitalWrite(pin, !digitalRead(pin));
}

//Função que le os valores dos POT e das Entradas 4 a 20 mA e plota no display
void managerInputFunc(void) {
    const uint16_t vlPOT1 = ads1115.analogReadPot1();
    const uint16_t vlPOT2 = ads1115.analogReadPot2();
    disp.setText(2, ("P1:" + String(vlPOT1) + "  P2:" + String(vlPOT2)).c_str());    
    wserial.plot("vlPOT1", vlPOT1);
    wserial.plot("vlPOT2", vlPOT2);
}

//Configuração inicial do programa
void setup() {
    wserial.begin();
    disp.begin(def_pin_SDA, def_pin_SCL);
    ads1115.begin();
    net.begin(KIT_HOSTNAME);
    
    disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
    disp.setText(2, KIT_HOSTNAME);
    disp.setText(3, "");

    pinMode(def_pin_D1, OUTPUT);
    pinMode(def_pin_D2, OUTPUT);  
    
    delay(50); 
    
    ltask.begin(1000);
    ltask.attach(managerInputFunc, 50);               //anexa uma função e sua base de tempo para ser executada
    ltask.attach([](){blinkLEDFunc(def_pin_D1);}, 500);   //anexa uma função e sua base de tempo para ser executada
    ltask.attach([](){blinkLEDFunc(def_pin_D2);}, 1000);  //anexa uma função e sua base de tempo para ser executada
}

//Loop principal
void loop() {
  disp.update();    
  wserial.update();
  net.update();
  ltask.update();
}