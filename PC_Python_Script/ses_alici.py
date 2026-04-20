import numpy as np
import sounddevice as sd
import soundfile as sf
from IPython.display import Audio # Import Audio for direct Colab playback

def parse_rx_data(raw_text):
    """Metin içindeki başlıkları ve virgülle ayrılmış değerleri ayıklar."""
    audio_values = []

    for line in raw_text.strip().split('\n'):
        if ',' not in line:
            continue

        if '->' in line:
            line = line.split('->')[-1]

        parts = line.split(',')
        for p in parts:
            p = p.strip()
            if p:
                try:
                    audio_values.append(int(p))
                except ValueError:
                    pass

    return audio_values

def main():
    # 1000 Hz Sinüs Dalgası Test Verisi
    rx_veri = """
    [SES VERISI] pcm_buffer (512 Ornek):
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800,
       0,  3800,  7000, 10000, 12000, 10000,  7000,  3800,     0, -3800, -7000,-10000,-12000,-10000, -7000, -3800
    """

    print("Veri ayrıştırılıyor...")
    parsed_data = parse_rx_data(rx_veri)

    if len(parsed_data) == 0:
        print("HATA: Okunabilir bir ses verisi bulunamadı!")
        return

    # Numpy dizisine çevir (ESP32 için genelde int16 kullanılır)
    audio_array = np.array(parsed_data, dtype=np.int16)

    # DİKKAT: Sesin duyulabilmesi için test verisini 50 kez ardışık kopyalıyoruz.
    # Kendi ESP32'nden gelen gerçek uzun veriyi kopyaladığında aşağıdaki satırı SİLEBİLİRSİN.
    audio_array = np.tile(audio_array, 50)

    print(f"Toplam {len(audio_array)} örnek işleniyor...")

    # Örnekleme hızı 16000 Hz (ESP32 kodundaki ayarına göre değiştirebilirsin)
    SAMPLE_RATE = 16000

    # 1. Aşama: .wav dosyası olarak kaydet
    wav_filename = "test_bip.wav"
    sf.write(wav_filename, audio_array, SAMPLE_RATE)
    print(f"Dosya '{wav_filename}' olarak kaydedildi.")

    # 2. Aşama: Sesi doğrudan çal (IPython.display.Audio ile Colab içinde)
    print("Ses Colab içinde çalınıyor...")
    display(Audio(wav_filename))

    print("Test başarılı!")

if __name__ == "__main__":
    main()