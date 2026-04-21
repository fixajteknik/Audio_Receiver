#ifndef AUDIO_SENDER_H
#define AUDIO_SENDER_H

#include <Arduino.h>
#include "driver/i2s_std.h"

// --- I2S PIN YAPILANDIRMASI (ESP32-S3 GUVENLI PINLERI) ---
#define I2S_WS 48   
#define I2S_SCK 2  
#define I2S_SD 38   

// --- SES KAYIT PARAMETRELERI ---
#define SAMPLE_RATE 16000                                  
#define FRAME_TIME_MS 32                                          
#define SAMPLES_PER_FRAME ((SAMPLE_RATE * FRAME_TIME_MS) / 1000)  
#define BYTES_PER_FRAME (SAMPLES_PER_FRAME * sizeof(int16_t))     
#define MAX_AUDIO_PAYLOAD_SIZE 210

// --- RF PAKET YAPISI ---
#pragma pack(push, 1)
struct AudioPacket {
  uint32_t cerceve_no;    
  uint16_t kaynak_id;
  uint8_t veri_uzunlugu;  
  uint8_t paket_no;
  uint8_t toplam_paket;
  char sifre[10];
  int16_t ses_verisi[MAX_AUDIO_PAYLOAD_SIZE / 2];
};
#pragma pack(pop)

class AudioSender {
  public:
    AudioSender(); // Constructor
    
    // Temel Fonksiyonlar
    bool begin();     // I2S ve LoRa donanımlarını başlatır (Setup'ta kullanılır)
    bool record();    // DMA üzerinden 32ms'lik ses kaydını alır
    void send();      // Alınan sesi parçalar (fragmentation) ve RF ile gönderir

  private:
    i2s_chan_handle_t rx_chan;
    int16_t pcm_buffer[SAMPLES_PER_FRAME];
    uint32_t genel_cerceve_sayaci;

    // Alt Fonksiyonlar
    bool initI2S();
    void initLoRa();
    void LoraE22Ayarlar();
    void LoraE32Ayarlar();
};

#endif