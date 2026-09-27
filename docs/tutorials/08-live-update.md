# 8. Live Monitoring & Update Tanpa Reboot

## Kenapa tidak perlu reboot setiap ganti program

Device menyimpan **dua buffer graph tetap** di memori (bukan alokasi
dinamis). Saat kamu klik **Save**:

1. Program baru di-parse dan divalidasi (cek JSON valid, tidak ada
   rangkaian melingkar/cyclic, pin `real` aman, dst) ke buffer
   **standby** (tidak dipakai scan cycle).
2. Kalau validasi gagal di titik mana pun → device menolak (HTTP 400)
   dan buffer **aktif tidak tersentuh sama sekali** — program yang
   sedang berjalan tetap jalan seperti sebelumnya, tanpa downtime.
3. Kalau validasi lolos semua → flag "reload" di-set. Scan cycle (yang
   jalan terus-menerus di background, siklus per beberapa milidetik)
   menukar pointer aktif↔standby di **awal cycle berikutnya** — swap ini
   selalu terjadi kurang dari ~200ms setelah Save, dan scan cycle tidak
   pernah berhenti/pending menunggu web request selesai (tidak ada
   lock/mutex antara keduanya).

Efek buat kamu: klik Save, tunggu sebentar, program baru langsung aktif
— tidak ada reboot, tidak ada gap eksekusi yang terasa.

## Auto-backup

Setiap kali program lama ditimpa, device otomatis menyimpan salinannya
sebagai `program_backup_<uptime_ms>.json` di SPIFFS, sebelum menulis
program baru. Retensi dibatasi **5 backup terbaru** — backup lebih tua
otomatis dihapus supaya tidak menumpuk memenuhi SPIFFS.

Ini bukan fitur "undo" lewat UI (belum ada tombol restore-dari-backup
di web editor) — kalau perlu me-restore versi lama, ambil file backup
itu langsung dari SPIFFS (misal lewat endpoint file statis atau serial/
JTAG tools, tergantung akses yang kamu punya ke device).

## Persistence

Program yang berhasil di-Save otomatis tersimpan permanen
(`program.json` di SPIFFS) dan **dimuat ulang otomatis saat device
reboot** — jadi tidak perlu Save ulang manual setelah setiap
restart/power cycle.

## Live monitoring (polling, bukan WebSocket)

`GET /api/program` mengembalikan field `outputs` per node — nilai
output aktual dari scan cycle yang sedang berjalan (termasuk hasil
`i2c_read_reg`, status `digital_output`, dst). Ini dipilih sebagai
mekanisme monitoring paling sederhana (dibanding WebSocket) supaya
tidak menambah risiko regresi di server HTTP yang sudah stabil.

Cara pakai: polling endpoint ini secara berkala (misal tiap 500ms-1s
dari script/tool eksternal) untuk melihat nilai node berubah real-time,
tanpa perlu serial monitor atau akses fisik ke device.

Contoh sederhana (Python):

```python
import urllib.request, json, time

while True:
    resp = urllib.request.urlopen("http://<ip-device>/api/program", timeout=5)
    data = json.loads(resp.read())
    for node in data["nodes"]:
        print(node["id"], node.get("outputs"))
    time.sleep(1)
```
