# ADR-20260523 — Wave 83: Forward+ / clustered light culling architecture

## Bağlam

Phase 7 Sprint 9 = Phase 7 routing tablosundaki son büyük item:
"Forward+ / clustered light culling". Bu küme:

- N ışıklı sahnede her piksel için sadece **etkili** ışıkları
  shader'a verir (klasik forward = her piksel × her ışık).
- 100+ dinamik nokta/spot ışıklı sahnelerde forward'dan 5-20×
  hızlanma sağlar; deferred'tan farklı olarak MSAA, OIT, transparency
  ve material-per-pixel ile native uyumlu.
- Modern motorlarda standart: Doom 2016 (clustered forward),
  Frostbite/Killzone (tile-based forward+), Unreal Engine 5 (cluster
  + tile hybrid).

Phase 7 önceki sprint'leri (FLIP / scripting / audio) library-tier
mantıksal işti; bu sprint **render pipeline** içine giriyor. CPU
baseline + sample bu wave'lerde iniyor; GPU compute shader portu
ayrı bir sprint olarak Phase 7+ veya Phase 8'e bırakılıyor.

## Karar

**Clustered Forward+ mimarisi (tile değil cluster).** Cluster grid
3D — frustum X × Y × log(Z) bölmesinde. Tile-based (2D X × Y +
per-pixel z-range) yerine cluster çünkü:

- View-space depth slices light coverage'i nesnel olarak ayırıyor;
  derinlik aralığı geniş sahnelerde (open-world, scope-zoom)
  tile'a göre çok daha az ışık per-fragment.
- 3D cluster grid'i compute shader'da paralelize etmek tile'a göre
  marjinal ek karmaşıklık.

Boyutlar (varsayılan):

- X = 16 (frustum genişliği boyunca)
- Y = 9  (frustum yüksekliği boyunca; 16:9'a göre)
- Z = 24 (yakın → uzak log-z bölmesi)

Toplam 16 × 9 × 24 = 3456 cluster. Bellek: 4 bayt × 3456 = 13.5 KB
cluster offset + ~64 KB light index buffer (2048 ışık × 32 byte
ortalama list head). MOBILE'da X/Y/Z half (768 cluster ≈ 3 KB +
16 KB) yapılabilir.

## Cluster bölme şeması

### X / Y (frustum-aligned uniform):

```
cluster_x = floor(NDC_x_in_[0,1] * X)
cluster_y = floor(NDC_y_in_[0,1] * Y)
```

### Z (log-z):

```
cluster_z = floor(log(z_view / near) / log(far / near) * Z)
```

Log-z near boyutunda yoğun, far boyutunda seyrek bölmesi yapıyor —
kameraya yakın detay için ışıkların daha iyi izole edilmesi.

## Light culling (CPU baseline, Wave 85)

Her ışık (sphere {center, radius}) için:

1. Sphere'in view-space AABB'sini hesapla.
2. AABB'yi clip space'e projeksiyonla (her köşeyi).
3. Min/max clip → cluster_x / _y / _z aralığı.
4. Aralık içindeki her cluster'a bu ışığın index'ini ekle.

Optimizasyon (Wave 85+): sphere-vs-cluster-AABB tam intersection
test (false positive azaltır). Wave 84-85 baseline'da AABB-AABB
overlap yeterli.

## Data structures

```cpp
namespace cd::render::cluster {

struct ClusterConfig {
    std::uint32_t cells_x { 16 };
    std::uint32_t cells_y { 9 };
    std::uint32_t cells_z { 24 };
    float near_plane { 0.1f };
    float far_plane { 1000.0f };
};

struct Light {
    cd::math::Vec3f position;  // view space
    float radius;
    // color / intensity tabi shader tarafında; cluster grid sadece
    // hangi ışıkların hangi cluster'a etki ettiğini bilir.
};

class ClusterGrid {
public:
    explicit ClusterGrid(const ClusterConfig& cfg) noexcept;

    void clear() noexcept;
    void assign_light(std::uint32_t light_index, const Light& light);

    [[nodiscard]] std::span<const std::uint32_t> lights_in_cluster(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept;

    [[nodiscard]] std::size_t total_cluster_count() const noexcept;
    [[nodiscard]] std::size_t total_light_count() const noexcept;

private:
    ClusterConfig cfg_;
    // SoA storage: per-cluster index ranges into a single flat
    // `light_indices_` vector. Avoids per-cluster vector growth.
    std::vector<std::uint32_t> cluster_offsets_;  // size = X*Y*Z + 1
    std::vector<std::uint32_t> light_indices_;    // packed list
};

}  // namespace cd::render::cluster
```

## GPU port path (Phase 7+ wave)

Yapısal olarak: cluster_offsets_ ve light_indices_ buffer'larını
storage buffer olarak Vulkan'a upload. Compute shader assignment
yapar (her cluster X*Y*Z = 3456 thread). PBR fragment shader bu
buffer'ı sampling eder. CPU baseline'ın test edilebilirliği +
non-Vulkan platformlarda da çalışması GPU port öncesi şart.

## Reddedilen alternatifler

- **Tile-based (Killzone 2 style):** 2D tile + per-pixel z-range.
  Open-world sahnelerde cluster'dan kötü; tile depth-min/max overdraw
  yaratıyor.
- **Pre-sorted light lists (eski forward):** O(pixels × lights);
  modern sahnelerde rejim dışı.
- **Deferred shading:** Transparency, MSAA, material variation'da
  forward+ daha esnek. Engine zaten forward; tier değiştirmek
  büyük refactor.
- **Tüm sprint'i GPU compute ile başlatmak:** Compute shader + Vulkan
  pipeline + descriptor set yazımı tek sprint'e sığmaz; CPU baseline
  hem test edilebilir hem GPU port'a referans davranış sağlıyor.

## Sonuçlar

Sprint 9 CPU baseline + sample + closure ADR ile kapanacak. GPU
compute port resmi olarak Phase 7+ (veya Phase 8) candidate'i olarak
v0.14.0 closure ADR'sinde belgelenecek.

Sırada Wave 84: ClusterGrid CPU implementasyon + assign_light
sphere-vs-grid AABB testi.

## Açık sorular

- **Spot light support:** Sphere + cone yarım açısı; tighter cluster
  assignment. Phase 7+ wave.
- **Light data layout for shader sampling:** SoA per attribute
  (position, radius, color, intensity) vs. AoS struct. SoA performans
  + alignment için tercih edilebilir; karar Wave 86 (sample) sırasında.
- **Dynamic light count:** Wave 85'te `assign_light` çağrı sayısı
  cluster grid maliyetini orantısal şişiriyor. Hierarchical clustering
  (2-level grid) çok ışıklı sahneler için Phase 7+ candidate.
