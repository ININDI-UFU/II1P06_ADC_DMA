// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/timer.html
#include <Arduino.h>
#include <Wire.h>
#include "services/lasecNet.h"
#include "services/wserial.h"
#include "services/display_ssd1306.h"
#include "services/ads1115.h"

constexpr uint8_t def_pin_SDA = 21;
constexpr uint8_t def_pin_SCL = 22;

// ---------- Timer de hardware (Arduino-ESP32 3.x) ----------
// timerBegin recebe a frequência desejada em Hz. Com 1 MHz, cada tick vale 1 µs.
static hw_timer_t* _timer = nullptr;

// ---------- Flag de sincronização ISR → loop() ----------
//
// volatile: o compilador não pode cachear — ISR e loop() são contextos distintos.
// IRAM_ATTR: função fica na RAM interna (executa mesmo se a flash estiver ocupada).
// Regra de ouro: ISR faz o MÍNIMO — apenas sinaliza com a flag.
volatile bool _sampleReady = false;

void IRAM_ATTR onTimer() {
    _sampleReady = true;
}

// ---------- Dados lidos no loop() ----------
static uint16_t _pot1        = 0;
static uint16_t _pot2        = 0;
static uint32_t _sampleCount = 0;

// ---------- Funções auxiliares ----------
void readAndSend() {
    // Leitura bloqueante via I2C (~9 ms por canal a 128 SPS)
    _pot1 = ads1115.analogReadPot1(); // canal A1
    _pot2 = ads1115.analogReadPot2(); // canal A0
    _sampleCount++;

    // Envia via UDP (ou Serial se não houver link UDP ativo)
    wserial.plot("pot1", _pot1, "counts");
    wserial.plot("pot2", _pot2, "counts");
}

void updateDisplay() {
    disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
    disp.setText(3, ("n:" + String(_sampleCount)).c_str());
}

// ---------- Setup ----------
void setup() {
    wserial.begin();
    Wire.begin(def_pin_SDA, def_pin_SCL);
    disp.begin(Wire);
    net.begin(KIT_HOSTNAME);

    disp.setText(1, (WiFi.localIP().toString() + " ID:" + String(KIT_ID)).c_str());
    disp.setText(2, KIT_HOSTNAME);
    disp.setText(3, "ISR ADS1115 20Hz");

    ads1115.begin(Wire);

    // Configura o contador do timer para 1 MHz: cada tick equivale a 1 µs.
    _timer = timerBegin(1000000);
    // timerBegin retorna nullptr se não conseguir inicializar o timer.
    if (_timer == nullptr) return;

    // Associa onTimer à interrupção. A função apenas sinaliza _sampleReady;
    // a leitura do ADS1115 acontece no loop(), fora da ISR.
    timerAttachInterrupt(_timer, &onTimer);

    // Dispara após 50 000 ticks: 50 000 µs = 50 ms = 20 Hz.
    // autoreload=true repete o alarme; reload_count=0 não limita as repetições.
    timerAlarm(_timer, 50000, true, 0);
}

// ---------- Loop principal ----------
//
// Duas estratégias de temporização lado a lado:
//
//   ISR (hardware):  precisa → o timer dispara exatamente a cada 50 ms
//                    independente do que o loop() estiver fazendo.
//
//   millis():        suficiente para tarefas tolerantes a jitter,
//                    como atualizar o display ou log periódico.
//                    Vantagem: sem hardware extra, fácil de ler.
void loop() {
    wserial.update();
    disp.update();
    net.update();

    // 'now' calculado uma vez por iteração — todos os timers usam o mesmo snapshot
    const uint64_t now = millis();

    // Atualiza display a cada 1 s
    static uint64_t tDisplay = 0;
    if (now - tDisplay >= 1000) {
        tDisplay = now;
        updateDisplay();
    }

    // --- Leitura do ADS1115 controlada pela ISR do timer ---
    // _sampleReady é setado pela ISR exatamente a cada 50 ms (20 Hz)
    if (!_sampleReady) return; // CPU livre para as tarefas acima enquanto espera

    _sampleReady = false; // limpa ANTES de ler: não perde o próximo tick
    readAndSend();
}
