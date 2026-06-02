// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html
#include <Arduino.h>
#include "services/lasecNet.h"
#include "services/wserial.h"
#include "services/ads1115.h"
#include "services/display_ssd1306.h"
#include "util/lasecDebounce.h"

constexpr uint8_t def_pin_D1 = 23;
constexpr uint8_t def_pin_D2 = 19;
constexpr uint8_t def_pin_SCL = 22;     ///< GPIO para SCL do display OLED.
constexpr uint8_t def_pin_SDA = 21;     ///< GPIO para SDA do display OLED.

// ---------- Parâmetros das tasks (passados via pvParameters) ----------
struct BlinkParams {
    uint8_t  pin;      // Pino do LED
    uint32_t delayMs;  // Período do pisca em milissegundos
};

struct InputParams {
    uint32_t delayMs;  // Período de leitura em milissegundos
};

// ---------- Funções auxiliares ----------

// Altera o estado de um LED
void blinkLEDFunc(uint8_t pin) {
    digitalWrite(pin, !digitalRead(pin));
}

// Lê os POTs e atualiza o display
void managerInputFunc(void) {
    const uint16_t vlPOT1 = ads1115.analogReadPot1();
    const uint16_t vlPOT2 = ads1115.analogReadPot2();
    disp.setText(2, ("P1:" + String(vlPOT1) + "  P2:" + String(vlPOT2)).c_str());
    wserial.plot("vlPOT1", vlPOT1);
    wserial.plot("vlPOT2", vlPOT2);
}

// ---------- Tasks FreeRTOS ----------

// Task genérica de pisca-LED: recebe pino e período via pvParameters
void taskBlink(void* pvParameters) {
    const BlinkParams* params = static_cast<BlinkParams*>(pvParameters);
    for (;;) {
        blinkLEDFunc(params->pin);
        vTaskDelay(pdMS_TO_TICKS(params->delayMs));
    }
}

// Task de leitura analógica: recebe período via pvParameters
void taskManagerInput(void* pvParameters) {
    const InputParams* params = static_cast<InputParams*>(pvParameters);
    for (;;) {
        managerInputFunc();
        vTaskDelay(pdMS_TO_TICKS(params->delayMs));
    }
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

    // Cria as tasks no núcleo 1, passando os parâmetros via pvParameters
    static BlinkParams paramsD1    = { def_pin_D1,  500  };
    xTaskCreatePinnedToCore(taskBlink,        "BlinkD1",    2048, &paramsD1,    1, nullptr, 1);
    static BlinkParams paramsD2    = { def_pin_D2,  1000 };    
    xTaskCreatePinnedToCore(taskBlink,        "BlinkD2",    2048, &paramsD2,    1, nullptr, 1);
    static InputParams paramsInput = { 50 };    
    xTaskCreatePinnedToCore(taskManagerInput, "ManagerPOT", 4096, &paramsInput, 1, nullptr, 1);
}

// ---------- Loop principal ----------
void loop() {
    wserial.update();
    disp.update();
    net.update();
}
