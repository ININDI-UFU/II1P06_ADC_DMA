// Exemplo de ADC contínuo com DMA para ESP32 e ESP32-S3.
#include <Arduino.h>
#include <Wire.h>
#include "services/AdcDmaEsp.h"
#include "services/lasecNet.h"
#include "services/wserial.h"
#include "services/display_ssd1306.h"

constexpr uint8_t PIN_SDA = 21; // Pino SDA do display OLED.
constexpr uint8_t PIN_SCL = 22; // Pino SCL do display OLED.
constexpr uint32_t SAMPLE_HZ = 20000; // Taxa total de conversão: 20 kHz.

// Cada frame tem 64 amostras (256 bytes); os buffers ficam alocados estaticamente.
static AdcDmaSample samples[ADC_DMA_SAMPLES_PER_FRAME];
static uint16_t values[ADC_DMA_SAMPLES_PER_FRAME];

void setup() {
    wserial.begin(); // Inicia comunicação Serial/UDP.
    Wire.begin(PIN_SDA, PIN_SCL); // Inicializa o barramento I2C nos pinos definidos.
    disp.begin(Wire); // Inicializa o OLED usando o barramento I2C configurado.
    net.begin(KIT_HOSTNAME); // Inicializa Wi-Fi, OTA e mDNS.
    disp.setText(1, KIT_HOSTNAME); // Exibe o nome do kit.
    disp.setText(3, "ADC DMA 20kHz"); // Exibe o modo de aquisição.

    // ADC1_CH4: GPIO32 no ESP32 comum e GPIO5 no ESP32-S3.
    // Configura a taxa de amostragem; interrompe se a inicialização falhar.
    if (!adcDma.begin(ADC_CHANNEL_4, SAMPLE_HZ)) return;
    // Inicia conversões; o hardware grava os resultados no buffer DMA.
    adcDma.start();
}

void loop() {
    wserial.update(); // Mantém a comunicação Serial/UDP ativa.
    disp.update(); // Atualiza o OLED quando o texto mudar.
    net.update(); // Processa serviços de rede e OTA.

    // A ISR do driver sinaliza quando um frame DMA está pronto.
    if (!adcDma.available()) return;
    // Lê e decodifica até 64 amostras do frame; aguarda o próximo se vazio.
    const size_t count = adcDma.read(samples, ADC_DMA_SAMPLES_PER_FRAME);
    if (count == 0) return;
    // Copia os valores do ADC para o formato aceito pelo envio em bloco.
    for (size_t i = 0; i < count; ++i) values[i] = samples[i].value;
    // Envia o frame por UDP ou Serial, com intervalo entre amostras de 0,05 ms.
    wserial.plot("adc_raw", 0, values, count, "counts");
}
