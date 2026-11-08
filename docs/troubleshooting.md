# Sorun Giderme

Her hata ekranda `AŞAMA/KOD/detay` (stage/code/detail) biçiminde gösterilir. Önce **aşamaya**
bakın: sorunun hangi katmanda olduğunu söyler.

## 1. Bağlantı

| Belirti | Olası neden | Ne yapmalı |
|---|---|---|
| COM port listede yok | CP210x sürücüsü yok, kablo yalnızca şarj kablosu, VM USB'yi tutuyor | Aygıt Yöneticisi'nde "Silicon Labs CP210x" arayın; VM'den USB'yi bırakın; **Refresh** |
| "Connection failed: open failed: Access is denied" | Port başka programda açık (idf.py monitor, seri terminal, Qt Creator Serial Terminal) | O programı kapatın; aynı anda yalnızca bir program portu kullanabilir |
| "Connection failed: HELLO failed: SERIAL/TIMEOUT" | Kartta eski/başka firmware var veya kart boot modunda | Firmware'i yükleyin (`build-and-release.md`); kartı resetleyin |
| "unsupported protocol version" | Firmware ile masaüstü farklı sürüm | İkisini aynı commit'ten derleyin |
| Bağlantı kurulur kurulmaz "gateway restarted" | Port açılırken DTR/RTS kartı resetledi | Normaldir; handshake otomatik tekrarlanır |
| "Connection lost: device removed" | USB çıkarıldı / güç gitti | Kabloyu takın, **Refresh → Connect** |
| "gateway stopped answering PING" | Kart kilitlendi veya hat koptu ama port açık | Kartı resetleyin; tekrarlıyorsa Geliştirici ekranındaki sayaçlara bakın |

## 2. CAN / ECU (araç profili)

| Hata | Anlamı | Kontrol |
|---|---|---|
| `CAN_ENQUEUE/NOT_STARTED` | TWAI başlatılamadı (`DIAG can_init`) | Pin ayarları, firmware log |
| `CAN_ENQUEUE/INVALID_STATE` | Denetleyici listen-only'den çıkamadı | `STATUS` → `bus=` |
| `CAN_TX/TX_FAILED` | Frame'i hiçbir düğüm ACK'lemedi; TEC +8 | Transceiver beslemesi, Rs pini, CANH/CANL, bitrate (`hardware-wiring.md`) |
| `CAN_TX/TIMEOUT` | TX tamamlanma bildirimi hiç gelmedi | Bus-off, kablo; `STATUS tec=` |
| `CAN_BUS/BUS_OFF` | TEC ≥ 256, denetleyici hattan çekildi | **Araçtan ayırın**; elektriksel kontrol |
| `RX_TIMEOUT/TIMEOUT/00` | Frame gitti (ACK'lendi) ama ECU cevap vermedi | ECU adresi (0x7E0?), kontak açık mı, 29-bit OBD olabilir |
| `RX_TIMEOUT/TIMEOUT/06` | İlk frame geldi, devamı gelmedi (detay = alınan byte) | Flow control ECU'ya ulaşmadı; trace'e bakın |
| `ISOTP/BAD_SEQUENCE/0102` | 1 numaralı CF beklenirken 2 geldi | Kayıp frame; RX kuyruğu (`rxdrop`) |
| `NRC/NEGATIVE_RESPONSE/22` | ECU "koşullar uygun değil" dedi | Kontak/motor durumu |
| `NRC/NEGATIVE_RESPONSE/31` | PID/servis desteklenmiyor | Canlı veride "unsupported" olarak işaretlenir |
| `OBD_DECODE/MALFORMED_PAYLOAD` | Cevap uzunluğu/sayısı tutmuyor | Trace'i kaydedin, rapor edin |

## 2b. Ölçüm sırasında dikkat

* Pin 8 (Rs) **CAN modülündeki** SN65HVD230 çipinin bacağıdır, ESP32 kartında değil. ESP32 kartının **EN** pinini
  GND'ye bağlamak kartı resette tutar: güç LED'i yanar ama kart cevap vermez. Jumper'ı çekip EN'e basınca düzelir
  (2026-11-08'de yaşandı, zararsız).
* Bench self-test (`tools/bench_check.py`) yalnızca **bench** firmware'i ile çalışır ve CANH/CANL hiçbir hatta
  bağlı olmamalıdır.

## 3. Masaüstü verileri

| Belirti | Neden | Çözüm |
|---|---|---|
| DTC'ler "Knowledge unavailable (NOT_FOUND)" | `data/isuzu-knowledge.db` exe'nin yanında değil | Paketi bütün olarak açın; build klasöründe CMake bunu kopyalar |
| "audit NOT saved" | Audit DB açılamadı / yazılamadı | Ayarlar ekranında yol; disk/izin |
| Audit DB "INVALID_STATE" | DB daha yeni bir uygulama sürümüyle yazılmış | Yeni sürümü kullanın veya DB'yi yedekleyip kaldırın |
| Canlı değerler gri / "stale" | Polling durdu, bağlantı koptu veya örnek eski | Live data → Start; durum çubuğuna bakın |
| Canlı veri kendiliğinden durdu "no_response" | Art arda 3 zaman aşımı; gateway hattı korumak için durdu | ECU/bağlantıyı kontrol edin, sonra yeniden başlatın |

## 4. Firmware derleme (Windows)

* `idf.py` bulunamıyor → önce ortamı açın: `. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1`
* `No module named 'rich'` → eski v5.5.4 ortamı; v6.1 ortamını kullanın.
* Flash sırasında port meşgul → masaüstü uygulamasını kapatın.
