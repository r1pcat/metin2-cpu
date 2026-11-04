# Metin2 CPU Mode

Windows üzerinde çalışan, Metin2 istemcisinin çizimini açıp kapatan C++ aracı. CPU modu açıkken sahne ve arayüz çizimi atlanır; oyun güncellemesi, ağ ve giriş döngüsü çalışmaya devam eder. Kaynak tüketimindeki azalma istemciye ve sahneye bağlıdır; belirli bir tasarruf oranı garanti edilmez.

## Gereksinimler

- Windows ve 32 bit `metin2client.exe`.
- Visual Studio veya Build Tools: **Desktop development with C++**, MSVC x86/x64 araçları ve Windows SDK.
- Kaynakta tanımlı çizim kapısı profiliyle uyumlu istemci.

Python, .NET veya ek bir paket kurulumu gerekmez.

## Derleme

Başlat menüsünden **x86 Native Tools Command Prompt for VS** terminalini açın. Depoyu indirdiğiniz klasöre geçip aşağıdaki komutları sırayla çalıştırın:

```bat
mkdir build
cd build
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /LD ..\src\cpu_bridge.cpp user32.lib /link /OUT:CpuModeBridge.dll
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT ..\src\main.cpp user32.lib /link /OUT:Metin2CpuMode.exe
```

`build` zaten varsa `mkdir build` adımını atlayın. Her komutun hatasız tamamlandığını kontrol edin. **x86** araçlarını kullanın; x64 derleme desteklenmez.

Oluşan `Metin2CpuMode.exe` ve `CpuModeBridge.dll` dosyalarını aynı klasörde tutun. Çalıştırma için `.obj`, `.lib` ve `.exp` dosyaları gerekmez.

## Kullanım

1. Oyunu kendiniz açın ve oyun penceresinin görünür olduğundan emin olun.
2. Görev Yöneticisi → **Ayrıntılar** bölümünde `metin2client.exe` işleminin PID değerini bulun. Birden fazla istemci varsa doğru işlemi seçin.
3. Derleme çıktısının bulunduğu klasörde terminal açıp aşağıdaki komutu çalıştırın. `1234` yerine gerçek PID değerini yazın:

```bat
Metin2CpuMode.exe 1234
```

Bağlantı kurulduğunda CPU modu kapalı başlar. Terminale komutu yazıp Enter'a basın:

| Komut | İşlev |
| --- | --- |
| `on` | CPU modunu açar, oyun çizimini durdurur. |
| `off` | CPU modunu kapatır, normal çizimi geri yükler. |
| `status` | CPU modunun durumunu gösterir. |
| `quit` veya `q` | Çizimi geri yüklemeyi deneyerek araçtan çıkar. |

Komut satırı yardımını görmek için `Metin2CpuMode.exe --help` kullanın. Normal çıkış için `quit` tercih edin. Oyun yönetici yetkisiyle çalışıyorsa aracın da aynı yetki düzeyinde çalışması gerekebilir.

## Uyumluluk ve çalışma şekli

Araç Windows mesaj kancası aracılığıyla oyun penceresinin thread'inde çalışır. Çizim kapısının bellekteki ilk 5 baytını geçici olarak değiştirir; kapatırken özgün baytları geri yüklemeyi dener. Diskteki oyun dosyasını değiştirmez.

Çizim kapısının RVA değeri kaynakta `0x0279D310` olarak tanımlıdır. Yazmadan önce beklenen kod imzası kontrol edilir. Bu profil sürüme özeldir; bütün Metin2 istemcileriyle uyumlu değildir. İmza denetimi tam dosya doğrulaması değildir ve tek başına güvenli çalışma garantisi vermez. İstemci güncellendiğinde profil yeniden incelenmelidir. İmza kontrolünü kaldırarak veya rastgele adres deneyerek uyumluluk sağlamaya çalışmayın.

Araç kapandığında, oyun thread'indeki zamanlayıcı kontrol sürecinin kapanmasını algılayıp çizimi geri yüklemeyi dener. Oyun thread'i donmuşsa veya başka bir araç aynı kodu değiştirmişse bu işlem tamamlanamayabilir. Köprü DLL'i, callback adreslerini geçerli tutmak için oyun kapanana kadar yüklü kalır.

## Kullanım riskleri

- **Hesap yaptırımı riski:** Araç oyun sürecine DLL yükler, mesaj kancası kullanır ve bellekteki kodu değiştirir. Bu işlemler oyun veya sunucu kurallarına aykırı olabilir ve koruma sistemleri tarafından tespit edilebilir. Hesap kısıtlaması veya kalıcı yasaklama riski vardır. Tespit edilmeme garantisi verilmez.
- **Çökme ve veri kaybı riski:** Uyumlu olmayan istemci, güncelleme veya başka bir araçla çakışma oyunun donmasına, kapanmasına ya da oturumun kesilmesine neden olabilir. Kaydedilmemiş ilerleme ve devam eden oyun işlemleri etkilenebilir.
- **Görünmeyen oyun olayları:** CPU modu açıkken görüntü güncellenmez; oyun ve ağ işlemleri devam eder. Karakter hasar alabilir, bağlantı kesilebilir veya ekranda göremediğiniz olaylar gerçekleşebilir. Görüntünün durması oyunun duraklatıldığı anlamına gelmez.
- **Geri yükleme sınırı:** Otomatik geri yükleme her koşulda garanti edilmez. `off` veya `quit` sonrasında görüntü dönmezse ya da geri yükleme hatası alırsanız yeniden bağlanmadan önce oyun istemcisini kapatıp açın.
- **Diğer araçlarla çakışma:** Aynı istemci üzerinde başka çizim yamaları, kancalar veya benzer araçlarla birlikte kullanmayın. Tek CPU kontrolü kilidi diğer bütün araçlarla çakışmayı engellemez.
- **Yetki ve dosya güvenliği:** Yalnız güvendiğiniz kaynaklardan derlediğiniz EXE ve DLL dosyalarını çalıştırın. Yönetici olarak çalıştırmak bu dosyalara daha geniş erişim verir.

Kullanmadan önce ilgili oyun ve sunucu kurallarını kontrol edin. Kullanımın sonuçları kullanıcıya aittir. MIT lisansı oyuna müdahale etme izni veya hesap güvenliği garantisi sağlamaz.

## Doğrulama durumu

Kaynakların x86 derlemesi ve çizim kapısına ilişkin çevrimdışı kontrolleri yapılmıştır. Bu bağımsız sürümün canlı oyun üzerinde çalışması, her istemcide çizimin geri dönmesi ve CPU tasarrufu doğrulanmış değildir.

## Lisans

[MIT License](LICENSE). Yazılım herhangi bir garanti olmaksızın sunulur; ayrıntılar lisans dosyasındadır.
