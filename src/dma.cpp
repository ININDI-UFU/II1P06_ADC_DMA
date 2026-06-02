/**
 * @file dma.cpp
 * @brief Exemplo: leitura de ADC com DMA no ESP32-S3 + envio UDP via wserial.
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Fluxo geral                                                            │
 * │                                                                         │
 * │  GPIO4 (ADC1_CH3) ──► ADC DMA ──► frame 64 amostras ──► UDP/Serial    │
 * │                                                         (wserial.plot) │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Hardware (ESP32-S3):
 *   GPIO4 = ADC1_CH3  →  entrada analógica 0–3.3 V
 *
 * Protocolo de plot (compatível com plotRawUDPServer.py):
 *   >adc_raw:<timestamp_ms>:<valor>§counts
 *   ou, para blocos:
 *   >adc_raw:<t0>:<v0>;<t1>:<v1>;...§counts
 */

// KIT_HOSTNAME e KIT_ID devem ser definidos pelo platformio.ini (-D flag).
// Os #ifndef abaixo servem de fallback para compilação manual.
#ifndef KIT_HOSTNAME
#define KIT_HOSTNAME "iikit-dma"
#endif
#ifndef KIT_ID
#define KIT_ID 0
#endif

#include "iikit.h"              ///< WiFi, Serial/UDP, display, GPIOs
#include "services/AdcDmaEsp.h" ///< ADC contínuo com DMA (ESP32-S3)

// ── Parâmetros de aquisição ──────────────────────────────────────────────────

/** Canal ADC1 usado. ADC_CHANNEL_3 = GPIO4 no ESP32-S3. */
static constexpr adc_channel_t ADC_CH = ADC_CHANNEL_3;

/**
 * Frequência de amostragem total em Hz.
 * Com 1 canal: 20 000 amostras/s → período de 50 µs entre amostras.
 * Limite do ESP32-S3: 611 Hz (mín) a ~83 333 Hz (máx).
 */
static constexpr uint32_t SAMPLE_HZ = 20000;

/**
 * Período entre frames em ms (para o plot de array).
 * dt_ms por amostra = 1 000 000 / SAMPLE_HZ µs = 50 µs → usamos 0 ms
 * (o wserial assume timestamps consecutivos ao receber dt_ms = 0).
 *
 * Se quiser ver o eixo de tempo corretamente no plotter, aumente dt_ms
 * para um valor inteiro em ms (ex.: 1 ms → taxa efetiva de 1 kHz).
 */
static constexpr uint32_t PLOT_DT_MS = 0;

// ── Buffers locais ───────────────────────────────────────────────────────────

/** Buffer para decodificação das amostras de cada frame. */
static AdcDmaSample _samples[ADC_DMA_SAMPLES_PER_FRAME];

/** Valores brutos extraídos para envio via wserial.plot (array). */
static uint16_t _rawValues[ADC_DMA_SAMPLES_PER_FRAME];

// ── Tarefas periódicas (ltask) ───────────────────────────────────────────────

/** Contador de frames processados (atualizado no loop). */
static volatile uint32_t _frameCount = 0;

/**
 * @brief Envia diagnóstico a cada 1 segundo pelo wserial.
 * Registrada em ltask.attach(); executada fora de ISR.
 */
static void taskStatus() {
    wserial.log(("DMA ok | frames=" + String(_frameCount) +
                 " | heap=" + String(esp_get_free_heap_size())).c_str());
}

// ── Setup ────────────────────────────────────────────────────────────────────

void setup() {

    // 1. Inicializa todo o kit: Serial, WiFi (via portal se necessário),
    //    mDNS, OTA, display OLED, GPIOs e debounce.
    IIKit.begin();

    wserial.println("=== ADC DMA ESP32-S3 ===");
    wserial.println("Canal:  ADC1_CH3 (GPIO4)");
    wserial.println("Taxa:   " + String(SAMPLE_HZ) + " Hz");
    wserial.println("Frame:  " + String(ADC_DMA_SAMPLES_PER_FRAME) + " amostras");

    // 2. Configura o ADC DMA.
    //    begin() internamente:
    //      a) Aloca o pool DMA (ADC_DMA_POOL_SIZE bytes)
    //      b) Configura ADC1_CH3, atenuação 0–3.3 V, 12 bits
    //      c) Registra a ISR que seta _ready quando o frame estiver cheio
    if (!adcDma.begin(ADC_CH, SAMPLE_HZ)) {
        wserial.println("ERRO: falha ao inicializar ADC DMA");
        while (true) delay(1000);  // para aqui — verifique a Serial
    }

    // 3. Inicia as conversões DMA.
    //    A partir daqui, o hardware começa a amostrar e preencher o buffer;
    //    quando o frame atingir ADC_DMA_FRAME_SIZE bytes, _ready = true.
    if (!adcDma.start()) {
        wserial.println("ERRO: falha ao iniciar ADC DMA");
        while (true) delay(1000);
    }

    // 4. Configura o escalonador de tarefas periódicas.
    //    ltask dispara a 1 kHz; taskStatus é chamada a cada 1000 ticks = 1 s.
    ltask.begin(1000);
    ltask.attach(taskStatus, 1000);

    wserial.println("Pronto. Aguardando amostras DMA...");
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {

    // ── Serviços de fundo ────────────────────────────────────────────────────
    // Mantém WiFi/OTA, processa UDP, atualiza display e debounce dos botões.
    IIKit.update();

    // Drena a fila do escalonador e executa taskStatus se o tick chegou.
    ltask.update();

    // ── Leitura DMA ──────────────────────────────────────────────────────────
    // available() retorna true quando a ISR do DMA sinalizou frame completo.
    // Isso acontece a cada ADC_DMA_FRAME_SIZE / (4 * SAMPLE_HZ) segundos
    //   = 256 / (4 * 20000) = 3.2 ms para 20 kHz.
    if (!adcDma.available()) return;

    // read() drena o frame, decodifica TYPE2→AdcDmaSample e limpa o flag.
    // Retorna o número real de amostras decodificadas (≤ ADC_DMA_SAMPLES_PER_FRAME).
    size_t n = adcDma.read(_samples, ADC_DMA_SAMPLES_PER_FRAME);
    if (n == 0) return;

    _frameCount++;

    // Extrai apenas os valores brutos (uint16_t) para o wserial.plot de array.
    // O canal de origem está em _samples[i].channel — útil com múltiplos canais.
    for (size_t i = 0; i < n; i++) {
        _rawValues[i] = _samples[i].value;
    }

    // Envia o bloco completo via UDP (ou Serial se UDP não estiver ligado).
    // Formato: >adc_raw:<t>:<v0>;<t+dt>:<v1>;...§counts
    // PLOT_DT_MS=0 → wserial usa timestamps consecutivos de 1 ms.
    wserial.plot("adc_raw", PLOT_DT_MS, _rawValues, n, "counts");

    // ── Exemplo de filtragem simples (média do frame) ────────────────────────
    // Descomente para enviar também a média por frame (taxa muito menor).
    /*
    uint32_t soma = 0;
    for (size_t i = 0; i < n; i++) soma += _rawValues[i];
    uint16_t media = (uint16_t)(soma / n);
    wserial.plot("adc_media", media, "counts");
    */
}
