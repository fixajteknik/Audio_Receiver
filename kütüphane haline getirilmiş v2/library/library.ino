#include "AudioSender.h"

AudioSender telsiz; // Kütüphanemizi "telsiz" adıyla çağırıyoruz

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Tek satırla hem I2S hem de LoRa donanımlarını hazırla
  if (!telsiz.begin()) {
    Serial.println("Sistem baslatilamadi, lutfen donanimi kontrol edin.");
    while(true); // Hata varsa sistemi kilitle
  }
}

void loop() {
  
  // 1. Aşama: Sesi Kaydet
  if (telsiz.record()) {
    
    // 2. Aşama: Ses başarıyla kaydedildiyse gönder
    telsiz.send();
    
    // Sürekli gönderim testi yapmak istemiyorsan aşağıdaki bekleme süresini artırabilirsin
    delay(5000); 
  }
}