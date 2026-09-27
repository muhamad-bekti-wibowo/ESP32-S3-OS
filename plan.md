# FBD/Virtual PLC Runtime untuk ESP32-S3 — Planning Lengkap (v2)

Dokumen ini gabungan dari dua sumber: rencana proses implementasi (fase +
kriteria selesai + anti-pola gagal) dan rencana arsitektur data + roadmap
peripheral bertingkat. Statusnya: proyek open-source, target awal berjalan
tanpa hardware tambahan apa pun (semua bisa disimulasikan), baru bertahap ke
hardware fisik.

---

## 1. Tujuan & Prinsip Proyek

**Tujuan:** Runtime Function Block Diagram (FBD) / virtual PLC open-source
untuk ESP32-S3, dieksekusi dari JSON tanpa kompilasi ulang firmware. Editor
visual jalan di browser, di-hosting langsung dari ESP32-S3.

**Prinsip yang mengikat semua keputusan di bawah:**
1. **Level 0 dulu, tuntas, baru naik level.** Semua yang bisa dites tanpa
   hardware (logic, math, timer, data) harus solid dulu sebelum menyentuh
   GPIO fisik.
2. **I2C pakai register generik dulu, bukan driver sensor.** `i2c_read_reg` /
   `i2c_write_reg` sebagai primitive, bukan node "INA219" atau "LCD1602".
   Driver sensor spesifik adalah *komposisi* dari primitive ini, dibangun di
   layer FBD sendiri (bukan kode C++ baru tiap sensor).
3. **SPI dan I2S ditunda total.** Tidak masuk roadmap sampai Level 0–2 selesai
   dan stabil dipakai nyata.
4. **Semua peripheral fisik punya mode simulasi.** ADC, PWM, Servo minimal —
   supaya development bisa jalan di PC tanpa board sama sekali.
5. **Skala data tumbuh dari boolean ke tagged union sejak awal.** Jangan
   desain schema hanya untuk boolean lalu dipaksa muat float/bytes belakangan
   — itu penyebab desain ulang paling umum di proyek serupa.

---

## 2. Kenapa Rencana Sejenis Ini Sering Gagal (dan cara dokumen ini menghindarinya)

| # | Pola gagal | Pencegahan di dokumen ini |
|---|---|---|
| 1 | Bikin graph editor canvas dari nol | WAJIB pakai library existing (Drawflow) — lihat §7 |
| 2 | Schema berubah-ubah di tengah jalan | Schema dan `FBDValue` di-lock di §4 sebelum interpreter ditulis |
| 3 | Node dieksekusi tidak sesuai dependency → nilai "telat 1 cycle" | Topological sort wajib ada dari awal — lihat §8 |
| 4 | Web server & scan cycle di loop yang sama → saling nge-lag | FreeRTOS dual-task terpisah sejak Fase 1 — lihat §8.3 |
| 5 | Testing ditunda ke akhir | Tiap fase punya kriteria selesai yang dites sebelum lanjut — lihat §9–§13 |
| 6 | Loncat langsung ke peripheral kompleks (SPI/I2S/driver sensor) sebelum fondasi jadi | Roadmap 4-level eksplisit — lihat §5, urutan wajib dari Level 0 |
| 7 | I2C langsung diikat ke driver sensor spesifik → runtime jadi kumpulan library, bukan automation engine | Primitive I2C register-level saja di Level 2 — lihat §5.3 |
| 8 | Race condition saat program di-update ketika device sedang running | Dual-buffer graph swap — lihat §11.1 |
| 9 | Alokasi dinamis (String, heap malloc) di dalam scan loop → fragmentasi & jitter | `FBDValue` tagged union fixed-size, tanpa alokasi dinamis di hot path — lihat §4 |
| 10 | I2C bus hang menahan seluruh scan cycle | Timeout pendek wajib + fallback error flag, bukan blocking — lihat §5.3 |

---

## 3. Arsitektur Sistem

```
[Browser: Drawflow editor]
       │  HTTP GET  (load index.html/JS/CSS dari LittleFS)
       │  HTTP POST /api/program (kirim JSON saat Save)
       │  WebSocket /ws (live status tiap node)
       ▼
[ESP32-S3: AsyncWebServer + WebSocket]  ── Core 0
       │
       │  standby_graph diisi & di-compile (topological sort)
       │  lalu di-swap ke active_graph di awal cycle berikutnya
       ▼
[FreeRTOS Task: FBD Scan Cycle]  ── Core 1, independen dari web server
       │  1. Baca input (digital/analog/I2C real atau simulated)
       │  2. Eksekusi node sesuai urutan topological sort
       │  3. Tulis output (digital/PWM/servo/I2C)
       ▼
[GPIO / ADC / PWM / Servo / I2C]  <-- atau Simulator Backend (browser) jika mode simulasi
```

**Dual backend (fitur pembeda proyek ini):** setiap node peripheral (ADC, PWM,
Servo, I2C nanti) punya dua implementasi di belakang interface yang sama —
`real` (hardware ESP32-S3) dan `simulated` (nilai dari slider/input di
browser, atau nilai dummy). Ini membuat pengembangan logic FBD bisa 100%
selesai di PC/browser sebelum menyentuh board.

```
                ┌──────────────┐
                │ FBD Program  │
                └──────┬───────┘
                       │  Runtime API (interface sama)
             ┌─────────┴─────────┐
             ▼                   ▼
       ESP32 Backend       Simulator Backend
       (GPIO/I2C/ADC/      (Virtual GPIO/ADC/
        PWM/Servo/WiFi)     PWM di browser)
```

---

## 4. Skema Data — `FBDValue` (Tagged Union, dikunci sebelum Fase 1 selesai)

Alasan tagged union, bukan `String`/`JsonVariant` langsung di hot path: ukuran
tetap (~16 byte), tanpa alokasi heap di dalam scan loop, jadi scan cycle tetap
deterministik dan tidak fragmentasi memori seiring waktu jalan lama.

```cpp
enum class DataType : uint8_t {
    EMPTY = 0,
    BOOL,
    INT32,
    FLOAT,
    BYTES   // cukup untuk payload register I2C kecil (maks 8 byte)
};

struct FBDValue {
    DataType type = DataType::EMPTY;
    union {
        bool b;
        int32_t i;
        float f;
        struct { uint8_t data[8]; uint8_t len; } bytes;
    };

    static FBDValue makeBool(bool v);
    static FBDValue makeInt(int32_t v);
    static FBDValue makeFloat(float v);
    static FBDValue makeBytes(const uint8_t* src, uint8_t len);

    float toFloat() const;  // auto-convert BOOL/INT32 -> float
    bool  toBool()  const;  // auto-convert INT32/FLOAT -> bool
};
```

**Kriteria selesai bagian ini:** struct ini dites terpisah di konsol/PC
(tanpa ESP32 sama sekali) — buat beberapa `FBDValue`, konversi bolak-balik,
pastikan `sizeof(FBDValue)` sesuai ekspektasi (tidak membengkak karena padding
aneh).

---

## 5. Roadmap Peripheral — 4 Level (urutan wajib, jangan diloncat)

### Level 0 — Pure Software (tidak ada hardware sama sekali)

Ini harus **tuntas dan stabil** sebelum Level 1 disentuh.

| Kategori | Node | Catatan implementasi |
|---|---|---|
| Logic | AND, OR, NOT | sudah ada di v1 dokumen |
| Logic | XOR, NAND, NOR | pola sama dengan AND/OR, tinggal tambah operator |
| Logic | Compare `== != > < >= <=` | 1 node generik dengan param `operator`, bukan 6 node terpisah |
| Data | Constant | `{"type":"constant","params":{"datatype":"float","value":25.5}}` |
| Data | Variable (`variable_set` / `variable_get`) | butuh storage global key-value di runtime, terpisah dari graph node |
| Math | ADD, SUB, MUL, DIV | 2 input tetap, sama seperti AND/OR di v1 |
| Math | MIN, MAX, ABS | 1-2 input |
| Math | SCALE | linear map dari `[in_min,in_max]` ke `[out_min,out_max]` — lihat kode `MATH_SCALE` di §8 |
| Math | CLAMP | batasi nilai ke rentang tertentu |
| Timing | TON (timer on-delay) | sudah ada di v1 sebagai `timer_on_delay` |
| Timing | TOF (timer off-delay) | output tetap ON sesaat setelah input OFF |
| Timing | TP (pulse/one-shot) | trigger OFF→ON hasilkan pulsa durasi tetap |
| Timing | Counter (CTU) | naik tiap pulsa, punya `state.count`, output `Q` saat capai `CV` |

**Kriteria selesai Level 0:** semua node ini dites di **konsol C++ murni**
(lihat contoh di §8.4), tanpa menyentuh ESP32 sama sekali. Kombinasi
Constant → Math → Compare → Timer harus menghasilkan nilai benar sebelum
lanjut ke Level 1.

### Level 1 — ESP32 Basic I/O (dengan mode simulasi wajib)

| Node | Real mode | Simulated mode | Prioritas |
|---|---|---|---|
| `digital_input` / `digital_output` | GPIO fisik | — (sudah cukup sederhana, langsung real) | sudah ada |
| `analog_input` (ADC) | `analogRead()` dengan `resolution`/`attenuation` | nilai dari slider UI, `params.mode: "simulated"` | Tinggi |
| `pwm_output` | LEDC channel/frequency/duty | tampilkan angka Freq/Duty di UI tanpa nyala LED | Tinggi |
| `servo` | konversi `angle` → pulsa `min_us`–`max_us` via LEDC | tampilkan indikator sudut 0–180° di UI | Menengah |

Schema contoh (dikunci sebelum implementasi):
```json
{ "type": "analog_input", "params": { "pin": 4, "resolution": 12, "attenuation": 11, "mode": "simulated", "value": 2048 } }
{ "type": "pwm_output",   "params": { "pin": 5, "frequency": 1000, "resolution": 12 } }
{ "type": "servo",        "params": { "pin": 18, "min_us": 500, "max_us": 2500 } }
```

**Kriteria selesai Level 1:** rangkaian `analog_input (simulated) → SCALE →
Compare > 50 → digital_output` jalan benar tanpa hardware ADC nyata. Baru
setelah itu, ganti `mode: "real"` dan uji dengan potensiometer fisik — hasil
harus konsisten dengan versi simulasi.

### Level 2 — Communication (I2C fokus utama, WiFi/DNS/mDNS sebagai system service)

**I2C — primitive register-level, BUKAN driver sensor:**
```json
{ "id": "i2c1", "type": "i2c_write_reg", "params": { "bus": 0, "address": 64, "register": 0, "data": [16, 32] } }
{ "id": "i2c2", "type": "i2c_read_reg",  "params": { "bus": 0, "address": 64, "register": 1, "length": 2 }, "outputs": ["raw_bytes", "error"] }
```
Aturan wajib:
- Node I2C **tidak boleh blocking lama** di dalam scan task. Timeout pendek
  (5–10ms), kalau NACK/timeout → set output `error = true` dan nilai
  fallback, jangan tahan seluruh cycle.
- Sensor spesifik (INA219, PCF8575, dst) **bukan node C++ baru** — itu adalah
  *komposisi* dari `i2c_read_reg`/`i2c_write_reg` yang disusun di canvas FBD
  itu sendiri (atau "sub-graph template" nanti kalau runtime sudah mendukung).

**WiFi/DNS/mDNS — system service, bukan node eksekutif di canvas:**
- Konfigurasi (SSID, password, DHCP/static IP, hostname) disimpan di NVS
  lewat tab "System > Network" terpisah di web UI, bukan node yang ditarik
  garis di canvas.
- FBD hanya mengakses status sebagai **read-only system variable**, mirip
  `%SM` di PLC Siemens/Omron:
  - `SYS.WIFI_CONNECTED` (BOOL)
  - `SYS.WIFI_RSSI` (INT32)
  - `SYS.IP_ADDRESS` (BYTES/STRING representasi)
  - `SYS.HOSTNAME` (mDNS, misal `fbd-esp32.local`)
- Contoh pemakaian di FBD: `SYS.WIFI_RSSI → Compare < -80 → digital_output (warning LED)`.

**Kriteria selesai Level 2:** baca 2 byte dari alamat I2C tertentu lewat
`i2c_read_reg`, tampilkan hasilnya di live monitor WebSocket — tanpa kode
driver sensor apa pun ditulis khusus. WiFi status bisa dibaca sebagai system
variable dan dipakai dalam logic Compare.

### Level 3 — Peripheral Kompleks (DITUNDA, bukan bagian roadmap aktif)

SPI, I2S, UART advanced, CAN, USB, SD, Ethernet, RMT, MCPWM, PCNT, DMA.
**Tidak dikerjakan sampai Level 0–2 stabil dan benar-benar dipakai**, sesuai
arahan awal proyek ("periferal yang banyak konfignya nanti dulu").

---

## 6. Partition Table (tetap dari v1, tidak berubah)

File: `partitions.csv`
```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x300000
app1,     app,  ota_1,   0x310000, 0x300000
littlefs, data, spiffs,  0x610000, 0x9F0000
```
- `app0`/`app1`: 3MB masing-masing — cukup lega untuk AsyncWebServer +
  ArduinoJson + WebSocket + runtime FBD sampai Level 2 tanpa mepet.
- `littlefs`: ~10MB untuk web assets (Drawflow) + banyak file JSON program.
- Dual OTA disiapkan dari awal walau baru dipakai nanti.

**Kriteria selesai:** `esp_partition_find` konfirmasi ukuran partisi benar
sebelum kode lain ditulis.

---

## 7. Fase 1 — Fondasi (Web Server + Schema, TANPA logic FBD)

- Library: `ESPAsyncWebServer` + `AsyncTCP` (bukan `WebServer.h` blocking).
- LittleFS mount di `setup()`, serve file statis.
- **Kriteria selesai:** buka `http://<ip-esp32>/`, dapat HTML statis
  (boleh placeholder). Tidak ada logic FBD sama sekali di titik ini.

### 7.1 Schema JSON (final untuk Level 0–1, dikunci sebelum interpreter ditulis)

```json
{
  "version": 1,
  "nodes": [
    { "id": "n1", "type": "digital_input",  "params": { "pin": 4, "mode": "pullup", "invert": false } },
    { "id": "n2", "type": "timer_on_delay", "params": { "delay_ms": 2000 }, "state": { "start_time": 0, "active": false } },
    { "id": "n3", "type": "AND" },
    { "id": "n4", "type": "digital_output", "params": { "pin": 2, "invert": false } }
  ],
  "links": [
    { "from": "n1.out", "to": "n3.in1" },
    { "from": "n2.out", "to": "n3.in2" },
    { "from": "n3.out", "to": "n4.in" }
  ]
}
```

Aturan tetap dari v1 (masih berlaku, jangan dilanggar saat menambah node baru
di Level 0–2):
- `params` (konfigurasi statis dari user) **selalu terpisah** dari `state`
  (data internal yang berubah tiap cycle, misal timer/counter).
- `version` di root, untuk migrasi schema di masa depan.
- Node `id` berupa string, bukan index array — supaya hapus/tambah node di
  editor tidak memaksa reindex seluruh `links`.

### 7.2 Daftar node dasar prototipe awal (Level 0 dari §5, jangan lebih dulu)
`digital_input`, `digital_output`, `AND`, `OR`, `NOT`, `timer_on_delay` — enam
ini harus beres end-to-end (Fase 3) sebelum node lain ditambah.

---

## 8. Fase 2 & 3 — Struktur Data Runtime + Interpreter

### 8.1 Struktur Node (fixed-size, hindari overhead vector dinamis di hot path)

```cpp
constexpr size_t MAX_NODE_INPUTS = 4;
constexpr size_t MAX_NODE_OUTPUTS = 2;

enum class NodeType : uint8_t {
    CONST_VAL, LOGIC_AND, LOGIC_OR, LOGIC_NOT,
    MATH_ADD, MATH_SCALE,
    TIMER_ON_DELAY,
    DIGITAL_IN, DIGITAL_OUT
    // Level 1-2 ditambah bertahap: ANALOG_IN, PWM_OUT, SERVO, I2C_READ_REG, I2C_WRITE_REG
};

struct NodeParams {
    int32_t pin = -1;
    uint32_t delay_ms = 0;
    float in_min = 0.0f, in_max = 4095.0f, out_min = 0.0f, out_max = 100.0f;
    FBDValue const_val;
};

struct NodeState {
    uint32_t timer_start_ms = 0;
    bool timer_running = false;
    bool prev_input = false;
};

struct FBDNode {
    std::string id;
    NodeType type;
    NodeParams params;
    NodeState state;
    FBDValue inputs[MAX_NODE_INPUTS];
    FBDValue outputs[MAX_NODE_OUTPUTS];

    void evaluate(uint32_t current_time_ms) {
        switch (type) {
            case NodeType::CONST_VAL:
                outputs[0] = params.const_val; break;
            case NodeType::LOGIC_AND:
                outputs[0] = FBDValue::makeBool(inputs[0].toBool() && inputs[1].toBool()); break;
            case NodeType::LOGIC_OR:
                outputs[0] = FBDValue::makeBool(inputs[0].toBool() || inputs[1].toBool()); break;
            case NodeType::LOGIC_NOT:
                outputs[0] = FBDValue::makeBool(!inputs[0].toBool()); break;
            case NodeType::MATH_ADD:
                outputs[0] = FBDValue::makeFloat(inputs[0].toFloat() + inputs[1].toFloat()); break;
            case NodeType::MATH_SCALE: {
                float val = inputs[0].toFloat();
                float in_span = params.in_max - params.in_min;
                float out_span = params.out_max - params.out_min;
                outputs[0] = FBDValue::makeFloat(
                    in_span == 0.0f ? params.out_min
                                    : params.out_min + ((val - params.in_min) / in_span) * out_span);
                break;
            }
            case NodeType::TIMER_ON_DELAY: {
                bool in = inputs[0].toBool();
                if (in && !state.prev_input) { state.timer_start_ms = current_time_ms; state.timer_running = true; }
                else if (!in) { state.timer_running = false; }
                state.prev_input = in;
                outputs[0] = FBDValue::makeBool(
                    state.timer_running && (current_time_ms - state.timer_start_ms >= params.delay_ms));
                break;
            }
            default: break;
        }
    }
};

struct FBDLink {
    size_t from_node_idx; uint8_t from_port;
    size_t to_node_idx;   uint8_t to_port;
};
```

### 8.2 Topological Sort (Kahn's Algorithm) — wajib sebelum eksekusi pertama

```cpp
struct FBDGraph {
    std::vector<FBDNode> nodes;
    std::vector<FBDLink> links;
    std::vector<size_t> execution_order;
    std::unordered_map<std::string, size_t> id_to_index;

    void addNode(const FBDNode& node) {
        nodes.push_back(node);
        id_to_index[node.id] = nodes.size() - 1;
    }

    bool addLink(const std::string& from_id, uint8_t from_port,
                 const std::string& to_id, uint8_t to_port) {
        if (!id_to_index.count(from_id) || !id_to_index.count(to_id)) return false;
        links.push_back({id_to_index[from_id], from_port, id_to_index[to_id], to_port});
        return true;
    }

    bool compile() {
        size_t n = nodes.size();
        std::vector<int> in_degree(n, 0);
        std::vector<std::vector<size_t>> adj(n);
        for (const auto& link : links) {
            adj[link.from_node_idx].push_back(link.to_node_idx);
            in_degree[link.to_node_idx]++;
        }
        std::queue<size_t> q;
        for (size_t i = 0; i < n; ++i) if (in_degree[i] == 0) q.push(i);
        execution_order.clear();
        while (!q.empty()) {
            size_t u = q.front(); q.pop();
            execution_order.push_back(u);
            for (size_t v : adj[u]) if (--in_degree[v] == 0) q.push(v);
        }
        return execution_order.size() == n;  // false = ada cyclic dependency
    }

    void executeCycle(uint32_t now_ms) {
        for (size_t idx : execution_order) {
            nodes[idx].evaluate(now_ms);
            for (const auto& link : links)
                if (link.from_node_idx == idx)
                    nodes[link.to_node_idx].inputs[link.to_port] = nodes[idx].outputs[link.from_port];
        }
    }
};
```

**Kenapa wajib:** kalau `AND` dieksekusi sebelum kedua inputnya dievaluasi di
cycle yang sama, nilai yang dipakai adalah nilai cycle sebelumnya — bug
"telat 1 cycle" yang sulit dilacak begitu logic makin kompleks.

### 8.3 Scan Cycle sebagai FreeRTOS Task Terpisah (Core 1)

```cpp
void fbd_scan_task(void *pvParameters) {
    FBDGraph* active_graph = static_cast<FBDGraph*>(pvParameters);
    const TickType_t scan_interval = pdMS_TO_TICKS(50); // 50ms = 20Hz
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        active_graph->executeCycle(now_ms);
        vTaskDelayUntil(&last_wake, scan_interval);
    }
}

// di setup():
xTaskCreatePinnedToCore(fbd_scan_task, "fbd_scan", 4096, &active_graph, 2, NULL, 1);
// pin ke core 1, web server (AsyncWebServer) tetap di core 0
```
Ini mencegah HTTP request lambat ikut menahan scan cycle FBD — dua task
FreeRTOS independen.

### 8.4 Uji Konsol (WAJIB sebelum Fase 3 dianggap selesai)

Buat program C++ murni (tanpa ESP32/Arduino) yang:
1. Menyusun node **dengan urutan sengaja diacak** di memori (misal `scale_1`
   ditambah duluan sebelum `const_1`/`const_2`/`add_1`).
2. Menjalankan `compile()` lalu print `execution_order`.
3. Menjalankan `executeCycle()` sekali, verifikasi hasil akhir benar.

Contoh hasil yang diharapkan:
```
Urutan eksekusi node:
 -> const_1
 -> const_2
 -> add_1
 -> scale_1

Hasil output akhir scale_1 (50.0 di-scale ke 0-1):
Value: 0.5
```
Kalau urutan eksekusi tetap mengikuti dependency (bukan urutan penambahan di
memori) dan hasil akhir benar, topological sort terbukti bekerja — **sebelum**
kode ini menyentuh ESP32 sama sekali.

**Kriteria selesai Fase 3 (versi hardware):** dengan node dari §7.2, colok 2
tombol fisik ke pin input, LED ke pin output, nyala/mati LED sesuai logic AND
secara real-time — TANPA web UI sama sekali.

---

## 9. Fase 4 — Editor Visual (Browser, Drawflow)

- Library: **Drawflow** — paling ringan untuk kasus FBD blok-dan-garis,
  dokumentasi pendek, tidak over-engineered untuk skala proyek ini.
- Tiap node di Drawflow di-mapping 1:1 ke tipe di §5 — jangan buat abstraksi
  tambahan di sisi editor dulu.
- Tombol "Save": serialize graph Drawflow → **fungsi converter eksplisit**
  (format internal Drawflow BEDA dari schema §7.1) → `POST /api/program`.
- Tombol "Load": `GET /api/program` → converter balik → render.
- **Mode simulasi Level 1 di UI:** slider untuk `analog_input` (0–4095),
  indikator angka untuk `pwm_output` (Freq/Duty %), indikator sudut untuk
  `servo` (0–180°) — semua tanpa hardware fisik.

**Kriteria selesai:** susun AND dari 2 digital_input ke 1 digital_output,
Save, refresh, Load, graph sama muncul lagi. Belum ada eksekusi logic di titik
ini.

---

## 10. Fase 5 — I2C Primitive (Level 2, setelah Level 0-1 stabil)

- Implementasi `i2c_read_reg` / `i2c_write_reg` sesuai schema di §5.3.
- **Timeout wajib pendek** (5–10ms) di dalam scan task — kalau device NACK
  atau bus hang, node mengeluarkan `error=true` + nilai fallback, TIDAK
  menahan seluruh scan cycle.
- Live monitor: hasil `i2c_read_reg` (raw bytes) ditampilkan di WebSocket,
  supaya user bisa verifikasi bus jalan tanpa menulis driver sensor apa pun.

**Kriteria selesai:** baca 2 byte dari device I2C nyata (misal EEPROM murah
atau sensor apa saja yang kamu punya) lewat `i2c_read_reg`, nilai tampil
benar di live monitor.

---

## 11. Fase 6 — Integrasi & Live Update Tanpa Reboot

### 11.1 Dual-Buffer Graph (mencegah race condition)

Saat web handler (Core 0) menerima JSON baru sementara scan task (Core 1)
sedang berjalan, menimpa graph aktif langsung berisiko corruption. Solusi:

```cpp
FBDGraph* active_graph;
FBDGraph* standby_graph;
volatile bool reload_requested = false;

// Scan task (Core 1), di awal tiap cycle:
if (reload_requested) {
    std::swap(active_graph, standby_graph);
    reload_requested = false;
}

// Web handler (Core 0), saat terima POST /api/program:
// 1. Parse JSON ke *standby_graph
// 2. standby_graph->compile()  (topological sort)
// 3. reload_requested = true;
```

Pergantian program berlangsung aman tepat di awal siklus scan berikutnya,
tanpa mutex kompleks.

### 11.2 Validasi & Live Monitoring
- WebSocket push status tiap node per cycle (atau tiap N cycle biar tidak
  membanjiri koneksi) untuk indikator nyala/mati di editor.
- Validasi graph sebelum disimpan: cek `compile()` tidak gagal (artinya tidak
  ada cyclic dependency), cek semua `links` merujuk ke id/port valid.
- Auto-backup: sebelum overwrite file JSON aktif, simpan salinan lama dengan
  timestamp di LittleFS.

**Kriteria selesai:** ubah logic di editor saat ESP32-S3 sedang running, klik
Save, tanpa reboot, logic baru langsung aktif — terlihat di LED fisik maupun
indikator di browser.

---

## 12. Prioritas Eksekusi (Ringkasan Actionable)

| Tahap | Fokus | Target Deliverable |
|---|---|---|
| 1 | Schema & `FBDValue` | Struct tagged union + engine Math/Logic murni, dites di **konsol C++, tanpa board** |
| 2 | Web server fondasi | AsyncWebServer serve LittleFS, schema JSON Level 0 di-freeze |
| 3 | FreeRTOS scan task + topological sort | Runtime jalan di ESP32-S3, dites dengan GPIO dummy (Serial print) dan `timer_on_delay` |
| 4 | Editor Drawflow | Save/Load JSON dari canvas, converter Drawflow↔schema |
| 5 | Level 1 I/O (simulated dulu) | `analog_input` (simulated), `pwm_output`, `servo` — semua bisa dites tanpa hardware |
| 6 | Level 1 I/O (real) | Ganti `mode: real`, uji hardware fisik (ADC/LEDC/servo), konsisten dengan hasil simulasi |
| 7 | Level 2 — I2C primitive | `i2c_read_reg`/`i2c_write_reg`, timeout pendek, live monitor |
| 8 | Level 2 — WiFi/DNS/mDNS sebagai system service | `SYS.WIFI_CONNECTED`, `SYS.WIFI_RSSI`, dst sebagai read-only variable |
| 9 | Dual-buffer graph + live update | Update program tanpa reboot, race-condition aman |
| — | Level 3 (SPI/I2S/dst) | **DITUNDA**, bukan bagian roadmap aktif |

---

## 13. Checklist Anti-Gagal (final, cek sebelum lanjut fase berikutnya)

- [ ] `FBDValue` dites di konsol C++ murni, `sizeof` sesuai ekspektasi, tanpa alokasi heap di hot path.
- [ ] Level 0 (Logic/Math/Timer/Data) tuntas dan dites di konsol SEBELUM Level 1 disentuh.
- [ ] Schema JSON Level 0-1 di-freeze dan ditulis di `schema.md` terpisah sebelum editor (Fase 4) dimulai.
- [ ] Topological sort dites eksplisit dengan node yang sengaja disusun terbalik urutannya — hasil eksekusi tetap benar.
- [ ] Scan cycle jalan di FreeRTOS task terpisah (Core 1) — dibuktikan dengan HTTP request lambat (delay 2 detik) sambil LED tetap berkedip sesuai logic tanpa telat.
- [ ] Fase editor (Drawflow) diuji dengan siklus save→load TANPA GPIO/logic real dulu.
- [ ] Level 1 (`analog_input`, `pwm_output`, `servo`) dites dulu di mode `simulated`, baru pindah ke `real` — hasil harus konsisten.
- [ ] I2C primitive (`i2c_read_reg`/`i2c_write_reg`) TIDAK menahan scan cycle lebih dari timeout yang ditentukan — dibuktikan dengan sengaja putus salah satu device I2C dan lihat scan cycle tetap jalan.
- [ ] Tidak ada node driver sensor spesifik (INA219, LCD, dst) ditulis sebagai kode C++ baru — semua komposisi dari primitive I2C generik.
- [ ] WiFi/DNS/mDNS diakses FBD hanya sebagai read-only system variable, bukan node yang dikonfigurasi di canvas.
- [ ] Dual-buffer graph swap dites: update program saat scan task sedang jalan, tidak ada crash/corruption.
- [ ] SPI, I2S, dan peripheral Level 3 lainnya belum disentuh sama sekali sampai semua di atas tercentang.