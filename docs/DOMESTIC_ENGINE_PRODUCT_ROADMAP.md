# CHROMODYNAMIC STUDIO — Ticari / Domestik Oyun Motoru Eksik Analizi ve Uygulama Yol Haritası

> **Hedef**: `editor.exe` (`CHROMODYNAMIC Studio`) ve `cd_player.exe` (`CHROMODYNAMIC Player`) ikililerini, yalnızca teknik altyapı testi veya renksiz/yazısız yer tutucu bloklar gösteren bir prototip olmaktan çıkarıp; açıldığı anda anlaşılır, okunaklı, modern koyu tema estetiğine sahip ve **gerçekten sıfırdan sahne/oyun tasarlayıp oynatabilen (Level Design + Physics + Player Controller + AI + PBR Material + Save/Load + Play-In-Editor)** ticari (domestik) kalitede bir oyun geliştirme stüdyosuna dönüştürmek.

---

## 1. Tespit Edilen Kök Sorunlar ve Eksikler (Gap Analysis)

1. **GPU Koordinat Sistemi (Ters Görüntü Hatası — `UiVariant.hpp`)**:
   - `cd::material::UiVariant` (`Route A`) vertex shader'ında yer alan `ndc.y = -ndc.y;` satırı, Vulkan'ın zaten yukarıdan aşağıya (`-1` üst, `+1` alt) olan NDC Y eksenini ters çevirerek tüm editör ekranını baş aşağı çiziyordu.
   - `UiVariant.cpp` içindeki `out_color = base * tint;` çarpanı, köşe renklerini (`v_color`) tema rengiyle çarparak tüm renkleri koyu mor bir tona boğuyordu.
2. **Yer Tutucu (Placeholder) Renk Blokları ve Okunaksız Küçük Yazılar**:
   - 24 adet erken faz paneli (`Inspector`, `MaterialEditor`, `Animator`, `LightEditor`, `BootSplash`, `WelcomeDialog`) metin olmadan yalnızca kırmızı/yeşil/mavi şeritler çizdiği için ekran karmaşık ve anlaşılmaz görünüyordu.
   - 1280x720 ekrana aynı anda 24 panel sıkıştırılması ve `1.0x` (5x7 piksel) ölçekli yazıların küçüklüğü okunabilirliği düşürüyordu.
3. **Gerçek 3B Sahne İnşası ve Oyun Yapım Döngüsünün (Game-Making Loop) Eksikliği**:
   - **Görsel 3B Sahne**: 3B görünümde basit 2B kareler yerine; derinlik sıralamalı (Z-sorted), yüzey normallerine göre dinamik güneş/ortam aydınlatmalı (Lambert + Blinn-Phong speküler) **gerçek 3B katı modeller** (Küre, Kutu/Platform, Oyuncu Karakteri/Kapsül, Araç, Işık Kaynağı, Fizik Objesi, Toplanabilir Hedef/Kristal, Arazi) çizilmelidir.
   - **Gerçek Sahne Düzenleme (Level Authoring)**:
     - **3B Gizmo Sürükleme**: Seçili nesnenin `X` (Kırmızı), `Y` (Yeşil), `Z` (Mavi) eksen oklarını fareyle tutup sürükleyerek nesneyi 3B uzayda canlı taşıma (`Move`), döndürme (`Rotate`) ve ölçekleme (`Scale`).
     - **İnteraktif Inspector (Özellik Denetçisi)**: Seçili nesnenin `Position (X,Y,Z)`, `Rotation`, `Scale (X,Y,Z)`, `Renk (R,G,B)`, `PBR Metallic / Roughness`, `Fizik (Yerçekimi / Kütle / Zıplama)`, `Oyun Rolü (Player / Enemy AI / Platform / Collectible / Light)` değerlerini fareyle kaydırarak (slider) veya `[-]` / `[+]` butonlarıyla canlı değiştirme.
     - **Oyun Nesnesi Ekleme / Silme / Çoğaltma**: Tek tıkla sahneye `+ Oyuncu (Player)`, `+ Platform / Duvar`, `+ Fizik Kutusu`, `+ Düşman AI (SynthAgent)`, `+ Ödül Kristali (Goal)`, `+ Güneş / Nokta Işık`, `+ Araç (Rover)` ekleme, kopyalama (`Duplicate`) ve silme (`Delete`).
   - **Gerçek Play-In-Editor (PIE) Oyun Modu (`[▶ OYNA / F5]`)**:
     - `[▶ OYNA (F5)]` tuşuna basıldığında sahnenin anlık yedeği (snapshot) alınmalı ve **gerçek oyun döngüsü** başlamalıdır:
       - Oyuncu karakteri **`WASD` / Ok Tuşları** ile 3B sahnede hareket etmeli, **`SPACE`** ile zıplamalı, yerçekimi ve platform çarpışmaları (AABB collision) çalışmalıdır.
       - Sahnedeki **Düşman AI (`SynthAgent`)** devriye/takip yapmalı, **Ödül Kristalleri (`Goal`)** toplandığında **Skor (`SCORE`)** artmalı ve oyun içi HUD (Can Çubuğu, Skor, Görev Durumu, Hız) ekranda belirmelidir.
       - `[⏹ DURDUR (F8)]` tuşuna basıldığında sahne düzenleme moduna ve başlangıç konumlarına geri dönmelidir.
   - **Kayıt / Yükleme (`.cdstage`)**:
     - Tasarlanan oyun sahnesi `[KAYDET Ctrl+S]` ile diske (`showcases/custom_game.cdstage`) kaydedilebilmeli ve `cd_player.exe` tarafından doğrudan bağımsız oyun olarak açılabilmelidir.

---

## 2. Uygulama Yol Haritası (Implementation Roadmap)

| Faz | Kapsam | Durum |
| :--- | :--- | :--- |
| **Faz 1: GPU Y-Ekseni & Shader Düzeltmesi** | `UiVariant.hpp` içindeki ters Y (`ndc.y = -ndc.y`) ve `UiVariant.cpp` içindeki mor renk baskılama hatalarının giderilmesi. | `[x] Tamamlandı` |
| **Faz 2: Modern, Okunaklı, Temiz Studio UX Mimarisinin Kurulması** | Karmaşık 24 ham panel yerine; **5 Ana Stüdyo Bölgesi** (Üst Ana Araç Çubuğu, Sol Sahne Hiyerarşisi & Nesne Ekleme, Orta Gerçek 3B Oyun Görünümü, Sağ İnteraktif Özellik Denetçisi, Alt Varlık Tarayıcısı / Konsol / Performans Sekmeleri) ve büyük/okunaklı tipografi (`1.5x`–`2.0x` başlıklar, yüksek kontrastlı modern arayüz). | `[▶] Şimdi Uygulanıyor` |
| **Faz 3: Gerçek 3B Katı Model (Solid 3D Mesh) & Işıklandırma Çizicisi** | Z-buffer derinlik sıralamalı, gerçek 3B perspektif üçgen/poligon tabanlı Küre, Kutu/Platform, Oyuncu Karakteri, Araç, Kristal ve Işık Hüzmesi çizimi + 3B Yörünge/Serbest Kamera (`RMB + Sürükle`, `WASD`). | `[▶] Şimdi Uygulanıyor` |
| **Faz 4: İnteraktif 3B Gizmo, Inspector Slider'ları ve Sahne İnşa Araçları** | Fareyle 3B eksen gizmo sürükleme, Inspector üzerinde canlı çalışan kaydırıcılar (`Position`, `Scale`, `Rotation`, `Color`, `Roughness`, `Metallic`, `Speed`), tek tıkla nesne oluşturma/kopyalama/silme ve `.cdstage` kaydetme/yükleme. | `[▶] Şimdi Uygulanıyor` |
| **Faz 5: Oynanabilir Oyun Döngüsü (Play-In-Editor & `cd_player.exe`)** | `F5` / `[▶ PLAY]` ile başlayan gerçek zamanlı 3B platform/aksiyon oyun mekaniği (`WASD` karakter kontrolü, `Space` zıplama, yerçekimi, platform çarpışması, AI devriyesi, kristal toplama & skor sistemi) ve gerçek pencere görüntüsü doğrulaması. | `[▶] Şimdi Uygulanıyor` |
