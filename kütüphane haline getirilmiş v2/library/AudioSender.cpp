#include "AudioSender.h"
#include "gizli.h" // Kütüphane gizli.h içerisindeki donanım ayarlarını kullanacak

AudioSender::AudioSender() {
  rx_chan = NULL;
  genel_cerceve_sayaci = 0;
}

bool AudioSender::begin() {
  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  
  Serial.println("[SETUP] Donanimlar Baslatiliyor...");
  
  // 1. I2S Başlat
  if (!initI2S()) {
    Serial.println("[SETUP] HATA: I2S baslatilamadi!");
    return false;
  }
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

  // 2. LoRa Başlat
  FixajSerial.begin();
  initLoRa();

  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);
  delay(500);
  
  Serial.println("[SETUP] Sistem Hazir.");
  return true;
}

bool AudioSender::record() {
  size_t bytes_read = 0;
  
  // ESP32 Core 3.0+ I2S API: Belirlenen byte kadar veriyi DMA'dan okur, yoksa zaman aşımına uğrar.
  esp_err_t res = i2s_channel_read(
    rx_chan,
    pcm_buffer,
    BYTES_PER_FRAME,
    &bytes_read,
    pdMS_TO_TICKS(FRAME_TIME_MS + 20) 
  );

  if (res == ESP_ERR_TIMEOUT) {
    Serial.println("[REC] HATA: Zaman asimi (I2S Saat Sinyali Yok)");
    return false;
  } else if (res != ESP_OK) {
    Serial.printf("[REC] HATA: %s\n", esp_err_to_name(res));
    return false;
  } else if (bytes_read != BYTES_PER_FRAME) {
    Serial.println("[REC] HATA: Eksik veri okumasi!");
    return false;
  }

  return true; // Kayıt başarılı
}

void AudioSender::send() {
  genel_cerceve_sayaci++;
  int gonderilen_toplam_byte = 0;
  uint8_t toplam_paket_sayisi = (BYTES_PER_FRAME + MAX_AUDIO_PAYLOAD_SIZE - 1) / MAX_AUDIO_PAYLOAD_SIZE;
  uint8_t mevcut_paket_indeksi = 0;

  Serial.printf("[TX] %d Numarali Cerceve iletiliyor...\n", genel_cerceve_sayaci);

  while (gonderilen_toplam_byte < BYTES_PER_FRAME) {
    AudioPacket tx_paket;
    memset(&tx_paket, 0, sizeof(AudioPacket));

    // Header Doldurma
    tx_paket.cerceve_no = genel_cerceve_sayaci;
    tx_paket.kaynak_id = 1453;
    tx_paket.paket_no = mevcut_paket_indeksi;
    tx_paket.toplam_paket = toplam_paket_sayisi;
    strncpy(tx_paket.sifre, "Fixaj.com", sizeof(tx_paket.sifre) - 1);
    tx_paket.sifre[sizeof(tx_paket.sifre) - 1] = '\0';

    // Veri Hesaplama ve Taşıma
    int kalan_byte = BYTES_PER_FRAME - gonderilen_toplam_byte;
    int bu_pakete_sigacak_byte = (kalan_byte > MAX_AUDIO_PAYLOAD_SIZE) ? MAX_AUDIO_PAYLOAD_SIZE : kalan_byte;
    tx_paket.veri_uzunlugu = bu_pakete_sigacak_byte;
    
    memcpy(tx_paket.ses_verisi, (uint8_t *)pcm_buffer + gonderilen_toplam_byte, bu_pakete_sigacak_byte);

    // LoRa ile Gönder
    ResponseStatus rs = FixajSerial.sendFixedMessage(
      highByte(GonderilecekAdres),
      lowByte(GonderilecekAdres),
      Kanal,
      &tx_paket,
      sizeof(AudioPacket)
    );

    if (rs.code != 1) {
      Serial.printf("[TX HATA] Paket %d yazilamadi! Kod: %d\n", mevcut_paket_indeksi, rs.code);
    }

    gonderilen_toplam_byte += bu_pakete_sigacak_byte;
    mevcut_paket_indeksi++;
    delay(2000); // Air-Data-Rate uyumu
  }
  Serial.println("[TX] Cerceve iletimi tamamlandi.");
}

// --- PRIVATE FONKSİYONLAR (Sadece Sınıf İçi Kullanım) ---

bool AudioSender::initI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 2;
  chan_cfg.dma_frame_num = SAMPLES_PER_FRAME;
  chan_cfg.auto_clear = false;
  
  if (i2s_new_channel(&chan_cfg, NULL, &rx_chan) != ESP_OK) return false;

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK,
      .ws = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)I2S_SD,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  if (i2s_channel_init_std_mode(rx_chan, &std_cfg) != ESP_OK) return false;
  
  return true;
}

void AudioSender::initLoRa() {
#ifdef e22900t22d
  LoraE22Ayarlar();
  Serial.print("[SETUP] E22 900t22d Kullaniliyor\n");
#else
  LoraE32Ayarlar();
  Serial.print("[SETUP] E32 433t20d Kullaniliyor\n");
#endif
}

#ifdef e22900t22d
void AudioSender::LoraE22Ayarlar() {
  digitalWrite(M0, LOW);
  digitalWrite(M1, HIGH);
  ResponseStructContainer c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration *)c.data;
  configuration.ADDL = lowByte(Adres);
  configuration.ADDH = highByte(Adres);
  configuration.NETID = Netid;
  configuration.CHAN = Kanal;
  configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
  configuration.OPTION.subPacketSetting = SPS_240_00;
  configuration.OPTION.transmissionPower = POWER_22;
  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.uartParity = MODE_00_8N1;
  configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
  configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
  configuration.TRANSMISSION_MODE.WORTransceiverControl = WOR_RECEIVER;
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
  configuration.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;
  configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
  FixajSerial.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  c.close();
}
#else
void AudioSender::LoraE32Ayarlar() {
  digitalWrite(M0, HIGH);
  digitalWrite(M1, HIGH);
  ResponseStructContainer c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration *)c.data;
  configuration.ADDL = lowByte(Adres);
  configuration.ADDH = highByte(Adres);
  configuration.CHAN = Kanal;
  configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
  configuration.OPTION.transmissionPower = POWER_20;
  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.uartParity = MODE_00_8N1;
  configuration.OPTION.fec = FEC_0_OFF;
  configuration.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  configuration.OPTION.wirelessWakeupTime = WAKE_UP_250;
  configuration.OPTION.ioDriveMode = IO_D_MODE_PUSH_PULLS_PULL_UPS;
  FixajSerial.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  c.close();
}
#endif