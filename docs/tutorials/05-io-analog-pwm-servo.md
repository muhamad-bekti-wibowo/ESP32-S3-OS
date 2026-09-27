# 5. I/O Analog, PWM, dan Servo

Ketiga node ini juga punya `hw_mode` (`simulated`/`real`) seperti I/O
digital, dan pin `real` divalidasi dengan aturan sama (lihat
[04-io-digital.md](04-io-digital.md) untuk tabel pin terlarang).

## analog_input (ADC)

Params: `pin`, `resolution` (9-12 bit), `attenuation` (0/2/6/11 dB —
menentukan rentang tegangan terukur; `11` ≈ 0-3.3V, paling umum
dipakai), `hw_mode`, `sim_value`.

- Di `hw_mode: simulated`, nilai output node = `sim_value`, yang diatur
  lewat **slider** di panel Properties (bukan angka polos) — geser
  slider untuk mensimulasikan potensiometer/sensor tanpa hardware nyata.
- Di `hw_mode: real`, device membaca ADC sungguhan lewat driver
  `adc_oneshot` ESP-IDF; `sim_value` diabaikan.

Contoh pakai: baca potensiometer di GPIO4, lalu `scale` ke rentang 0-100
untuk ditampilkan sebagai persentase:

```
analog_input(pin=4, resolution=12, attenuation=11) --> scale(in_min=0, in_max=4095, out_min=0, out_max=100)
```

## pwm_output

Params: `pin`, `frequency` (Hz), `resolution` (bit duty cycle LEDC),
`hw_mode`. **Input node (`inputs[0]`) adalah duty cycle dalam PERSEN
(0-100), bukan raw register** — jadi kamu bisa langsung sambungkan
output dari `scale`/`const` tanpa konversi manual.

Contoh: dimmer LED sederhana, duty cycle tetap 75%:

```
const(datatype=float, value=75) --> pwm_output(pin=5, frequency=1000, resolution=12, hw_mode=real)
```

## servo

Params: `pin`, `min_us`/`max_us` (pulse width di sudut 0°/180°),
`hw_mode`. **Input node adalah sudut 0-180 derajat** (otomatis di-clamp
kalau di luar rentang itu).

Contoh: servo bergerak ke 90 derajat:

```
const(datatype=float, value=90) --> servo(pin=18, min_us=500, max_us=2500, hw_mode=real)
```

## Kriteria konsistensi mode (penting)

Sama seperti I/O digital: desain dulu di `hw_mode: simulated` (pakai
slider untuk `analog_input`, angka konstan untuk `pwm_output`/`servo`)
sampai yakin arah logika benar, baru pindah ke `real`. Nilai presisi
boleh sedikit berbeda di hardware nyata (ADC punya noise, potensiometer
fisik tidak pas angka bulat), tapi arah naik/turun dan threshold logic
harus tetap sama.
