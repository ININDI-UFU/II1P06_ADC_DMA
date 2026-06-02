// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/timer.html
#include "Arduino.h"
#include "services/lasecNet.h"
#include "services/wserial.h"
#include "services/ads1115.h"
#include "services/display_ssd1306.h"

constexpr uint8_t def_pin_D1 = 23;
constexpr uint8_t def_pin_D2 = 19;
constexpr uint8_t def_pin_SCL = 22;     ///< GPIO para SCL do display OLED.
constexpr uint8_t def_pin_SDA = 21;     ///< GPIO para SDA do display OLED.

// ---------- Flags setadas pela ISR do timer ----------
// volatile: impede que o compilador otimize o acesso a essas variáveis
volatile bool flagBlink = false;
volatile bool flagInput = false;

// ---------- ISR do timer (roda a cada 50 ms) ----------
// IRAM_ATTR: mantém a função na RAM para execução rápida
hw_timer_t* timer = nullptr;
uint8_t contadorBlink = 0; // conta quantas vezes o timer disparou
void IRAM_ATTR onTimer() {
    contadorBlink++;
    flagInput = true;           // sinaliza leitura dos POTs (a cada 50 ms)
    if (contadorBlink >= 10) {  // 10 x 50 ms = 500 ms
        contadorBlink = 0;
        flagBlink = true;       // sinaliza pisca-LED
    }
}

// ---------- Funções auxiliares ----------
void blinkLEDFunc(uint8_t pin) {
    digitalWrite(pin, !digitalRead(pin));
}

void managerInputFunc(void) {
    const uint16_t vlPOT1 = ads1115.analogReadPot1();
    const uint16_t vlPOT2 = ads1115.analogReadPot2();
    disp.setText(2, ("P1:" + String(vlPOT1) + "  P2:" + String(vlPOT2)).c_str());    
    wserial.plot("vlPOT1", vlPOT1);
    wserial.plot("vlPOT2", vlPOT2);
}

// ---------- Setup ----------
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

    timer = timerBegin(0, 80, true); // frequência do timer: 1 MHz (1 tick = 1 µs)
    timerAttachInterrupt(timer, &onTimer, true); //vincula a função onTimer à interrupção do timer 
    timerAlarmWrite(timer, 50000, true); // dispara a cada 50.000 µs = 50 ms
    timerAlarmEnable(timer);
}

// ---------- Loop principal ----------
void loop() {
    wserial.update();
    disp.update(); 
    net.update(); 
     
    if (flagInput) {
        flagInput = false;
        managerInputFunc();
    }
    if (flagBlink) {
        flagBlink = false;
        blinkLEDFunc(def_pin_D1);
    }
}