/* Metadata node type Level 0-2, sesuai schema.md.
 * Satu sumber kebenaran untuk: palette, registrasi Drawflow node, dan
 * converter drawflowToSchema/schemaToDrawflow di app.js.
 *
 * Field:
 *   inputs / outputs: jumlah port.
 *   category: kategori palette (dipakai untuk grouping buka/tutup dan
 *     header pencarian, lihat app.js buildPalette()).
 *   icon: nama ikon SVG, harus ada di NODE_ICONS (app.js) - ditampilkan
 *     di palette dan di header node pada canvas.
 *   help: penjelasan singkat node ini, ditampilkan live di panel
 *     Properties saat node diklik di canvas (lihat renderProperties()
 *     di app.js) - ini adalah "tutorial di dalam web", bukan dokumen
 *     terpisah.
 *   fields: daftar field di params, dipakai untuk generate form HTML dan
 *     untuk baca/tulis nilai saat convert ke/dari schema.md.
 *     { key, label, type: 'number'|'text'|'select'|'checkbox'|'range'|
 *       'csv-bytes'|'command-list', options?, default }
 */
const CATEGORY_LABELS = {
    logic: 'Logic',
    data: 'Data',
    math: 'Math',
    timing: 'Timing',
    io_digital: 'I/O Digital',
    io_analog: 'I/O Analog & PWM',
    i2c: 'I2C',
    system: 'System',
};

const NODE_TYPES = {
    // ---- Logic ----
    and:  { label: 'AND',  inputs: 2, outputs: 1, fields: [], category: 'logic', icon: 'and',
        help: 'Output true hanya kalau kedua input true.' },
    or:   { label: 'OR',   inputs: 2, outputs: 1, fields: [], category: 'logic', icon: 'or',
        help: 'Output true kalau salah satu input true.' },
    not:  { label: 'NOT',  inputs: 1, outputs: 1, fields: [], category: 'logic', icon: 'not',
        help: 'Membalik nilai input (true jadi false, sebaliknya).' },
    xor:  { label: 'XOR',  inputs: 2, outputs: 1, fields: [], category: 'logic', icon: 'xor',
        help: 'Output true kalau kedua input berbeda.' },
    nand: { label: 'NAND', inputs: 2, outputs: 1, fields: [], category: 'logic', icon: 'and',
        help: 'Kebalikan dari AND: output false hanya kalau kedua input true.' },
    nor:  { label: 'NOR',  inputs: 2, outputs: 1, fields: [], category: 'logic', icon: 'or',
        help: 'Kebalikan dari OR: output true hanya kalau kedua input false.' },
    compare: {
        label: 'Compare', inputs: 2, outputs: 1, category: 'logic', icon: 'compare',
        help: 'Membandingkan in0 dengan in1 sesuai operator Op, output bool.',
        fields: [
            { key: 'op', label: 'Op', type: 'select',
              options: ['gt', 'lt', 'eq', 'neq', 'gte', 'lte'], default: 'gt' },
        ],
    },

    // ---- Data ----
    const: {
        label: 'Constant', inputs: 0, outputs: 1, category: 'data', icon: 'const',
        help: 'Nilai tetap yang bisa disambungkan ke node lain. Tidak punya input.',
        fields: [
            { key: 'datatype', label: 'Type', type: 'select',
              options: ['bool', 'int32', 'float'], default: 'float' },
            { key: 'value', label: 'Value', type: 'number', default: 0 },
        ],
    },
    var_get: {
        label: 'Var Get', inputs: 0, outputs: 1, category: 'data', icon: 'var',
        help: 'Baca nilai dari storage key-value global bernama Name.',
        fields: [{ key: 'name', label: 'Name', type: 'text', default: 'var1' }],
    },
    var_set: {
        label: 'Var Set', inputs: 1, outputs: 1, category: 'data', icon: 'var',
        help: 'Tulis nilai input ke storage global bernama Name, lalu pass-through nilainya ke output.',
        fields: [{ key: 'name', label: 'Name', type: 'text', default: 'var1' }],
    },

    // ---- Math ----
    math: {
        label: 'Math', inputs: 2, outputs: 1, category: 'math', icon: 'math',
        help: 'Operasi aritmatika sederhana antara in0 dan in1 sesuai Op.',
        fields: [
            { key: 'op', label: 'Op', type: 'select',
              options: ['add', 'sub', 'mul', 'div'], default: 'add' },
        ],
    },
    min:  { label: 'Min', inputs: 2, outputs: 1, fields: [], category: 'math', icon: 'math',
        help: 'Output nilai terkecil antara in0 dan in1.' },
    max:  { label: 'Max', inputs: 2, outputs: 1, fields: [], category: 'math', icon: 'math',
        help: 'Output nilai terbesar antara in0 dan in1.' },
    abs:  { label: 'Abs', inputs: 1, outputs: 1, fields: [], category: 'math', icon: 'math',
        help: 'Output nilai absolut (selalu positif) dari input.' },
    scale: {
        label: 'Scale', inputs: 1, outputs: 1, category: 'math', icon: 'scale',
        help: 'Pemetaan linear dari rentang In min-In max ke rentang Out min-Out max. Cocok untuk konversi raw ADC ke satuan nyata.',
        fields: [
            { key: 'in_min', label: 'In min', type: 'number', default: 0 },
            { key: 'in_max', label: 'In max', type: 'number', default: 4095 },
            { key: 'out_min', label: 'Out min', type: 'number', default: 0 },
            { key: 'out_max', label: 'Out max', type: 'number', default: 100 },
        ],
    },
    clamp: {
        label: 'Clamp', inputs: 1, outputs: 1, category: 'math', icon: 'scale',
        help: 'Membatasi nilai input agar tidak kurang dari Min atau lebih dari Max.',
        fields: [
            { key: 'min', label: 'Min', type: 'number', default: 0 },
            { key: 'max', label: 'Max', type: 'number', default: 100 },
        ],
    },

    // ---- Timing ----
    ton: {
        label: 'TON', inputs: 1, outputs: 1, category: 'timing', icon: 'timer',
        help: 'Timer ON-delay: output jadi true hanya setelah input true selama Delay (ms) terus-menerus.',
        fields: [{ key: 'delay_ms', label: 'Delay (ms)', type: 'number', default: 1000 }],
    },
    tof: {
        label: 'TOF', inputs: 1, outputs: 1, category: 'timing', icon: 'timer',
        help: 'Timer OFF-delay: output tetap true selama Delay (ms) setelah input berubah jadi false.',
        fields: [{ key: 'delay_ms', label: 'Delay (ms)', type: 'number', default: 1000 }],
    },
    tp: {
        label: 'TP', inputs: 1, outputs: 1, category: 'timing', icon: 'timer',
        help: 'Pulse timer: setiap input jadi true, output true selama Pulse (ms) lalu otomatis false lagi.',
        fields: [{ key: 'pulse_ms', label: 'Pulse (ms)', type: 'number', default: 500 }],
    },
    ctu: {
        label: 'Counter (CTU)', inputs: 2, outputs: 1, category: 'timing', icon: 'counter',
        help: 'Counter naik: port clk bertambah 1 tiap transisi false->true, port reset mengembalikan ke 0. Output true kalau hitungan >= Preset.',
        fields: [{ key: 'preset', label: 'Preset', type: 'number', default: 3 }],
    },

    // ---- I/O digital (Level 1) ----
    digital_input: {
        label: 'Digital In', inputs: 0, outputs: 1, category: 'io_digital', icon: 'digital_in',
        help: 'Baca level GPIO digital. HW Mode "simulated" = nilai diatur manual (tidak menyentuh GPIO fisik); "real" = baca pin sungguhan. Pin berbahaya (strapping/USB-JTAG/PSRAM) ditolak saat HW Mode real.',
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 0 },
            { key: 'mode', label: 'Mode', type: 'select',
              options: ['pullup', 'pulldown', 'floating'], default: 'pullup' },
            { key: 'invert', label: 'Invert', type: 'checkbox', default: false },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },
    digital_output: {
        label: 'Digital Out', inputs: 1, outputs: 0, category: 'io_digital', icon: 'digital_out',
        help: 'Menyalakan/mematikan GPIO digital sesuai nilai input. HW Mode "simulated" tidak berefek fisik; "real" mengendalikan pin sungguhan. Pin berbahaya ditolak saat HW Mode real.',
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 0 },
            { key: 'invert', label: 'Invert', type: 'checkbox', default: false },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },

    // ---- I/O analog & PWM (Level 1) ----
    analog_input: {
        label: 'Analog In', inputs: 0, outputs: 1, category: 'io_analog', icon: 'analog_in',
        help: 'Baca ADC. HW Mode "simulated" = pakai slider Sim Value (untuk tes tanpa hardware); "real" = baca ADC sungguhan (Sim Value diabaikan).',
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 4 },
            { key: 'resolution', label: 'Resolution (bit)', type: 'number', default: 12 },
            { key: 'attenuation', label: 'Attenuation (dB)', type: 'number', default: 11 },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
            { key: 'sim_value', label: 'Sim Value', type: 'range', min: 0, max: 4095, default: 0 },
        ],
    },
    pwm_output: {
        label: 'PWM Out', inputs: 1, outputs: 0, category: 'io_analog', icon: 'pwm',
        help: 'Output PWM (misal dimmer LED). Input node adalah duty cycle 0-100 (persen), bukan raw register.',
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 5 },
            { key: 'frequency', label: 'Frequency (Hz)', type: 'number', default: 1000 },
            { key: 'resolution', label: 'Resolution (bit)', type: 'number', default: 12 },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },
    servo: {
        label: 'Servo', inputs: 1, outputs: 0, category: 'io_analog', icon: 'servo',
        help: 'Menggerakkan servo. Input node adalah sudut 0-180 derajat (otomatis di-clamp kalau di luar rentang).',
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 18 },
            { key: 'min_us', label: 'Min us', type: 'number', default: 500 },
            { key: 'max_us', label: 'Max us', type: 'number', default: 2500 },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },

    // ---- I2C primitive (Level 2, register-level, BUKAN driver sensor) ----
    i2c_read_reg: {
        label: 'I2C Read Reg', inputs: 0, outputs: 2, category: 'i2c', icon: 'i2c',
        help: 'Baca register I2C mentah (bukan driver sensor spesifik). Output 0 = raw bytes, output 1 = error (true kalau NACK/timeout). Timeout selalu pendek (8ms), tidak pernah menahan scan cycle.',
        fields: [
            { key: 'bus', label: 'Bus', type: 'number', default: 0 },
            { key: 'address', label: 'Address (7-bit)', type: 'number', default: 0x27 },
            { key: 'register', label: 'Register', type: 'number', default: 0 },
            { key: 'length', label: 'Length (1-8)', type: 'number', default: 2 },
        ],
    },
    i2c_write_reg: {
        label: 'I2C Write Reg', inputs: 0, outputs: 1, category: 'i2c', icon: 'i2c',
        help: 'Tulis satu register I2C mentah. Output = sukses (true kalau ACK diterima). Isi Data sebagai daftar byte dipisah koma, misal "16,32". Untuk device yang butuh BANYAK command berurutan (mis. init LCD), pakai I2C Write Burst.',
        fields: [
            { key: 'bus', label: 'Bus', type: 'number', default: 0 },
            { key: 'address', label: 'Address (7-bit)', type: 'number', default: 0x27 },
            { key: 'register', label: 'Register', type: 'number', default: 0 },
            { key: 'data', label: 'Data (csv, e.g. 16,32)', type: 'csv-bytes', default: [0] },
        ],
    },
    i2c_write_burst: {
        label: 'I2C Write Burst', inputs: 0, outputs: 1, category: 'i2c', icon: 'i2c_burst',
        help: 'Kirim BEBERAPA command register write berurutan dalam satu scan cycle - dipakai device yang butuh command sequence (mis. init LCD1602 lewat backpack PCF8574: nibble tinggi, nibble rendah, toggle bit E, delay). Tetap primitive generik, bukan driver LCD - susun sequence apa pun lewat daftar command di bawah. Berhenti di command pertama yang gagal (output = false). Maks 8 command per node, tiap command maks 4 byte data - sambung beberapa node kalau butuh lebih.',
        fields: [
            { key: 'bus', label: 'Bus', type: 'number', default: 0 },
            { key: 'address', label: 'Address (7-bit)', type: 'number', default: 0x27 },
            { key: 'delay_us', label: 'Delay antar command (us)', type: 'number', default: 0 },
            { key: 'commands', label: 'Commands', type: 'command-list', default: [{ register: 0, data: [0] }] },
        ],
    },

    // ---- System variable read-only (WiFi, dst - BUKAN dikonfigurasi di sini) ----
    sys_var_get: {
        label: 'Sys Var Get', inputs: 0, outputs: 1, category: 'system', icon: 'sys',
        help: 'Baca variabel status sistem read-only (mirip %SM PLC). Konfigurasi WiFi sendiri dilakukan di tab System > Network, bukan di sini.',
        fields: [
            { key: 'name', label: 'Name', type: 'select',
              options: ['SYS.WIFI_CONNECTED', 'SYS.WIFI_RSSI'], default: 'SYS.WIFI_CONNECTED' },
        ],
    },
};

/* Label port input per tipe, khusus yang bukan generik "in0/in1". Dipakai
 * di app.js untuk render label port yang lebih jelas di canvas. */
const INPUT_PORT_LABELS = {
    ctu: ['clk', 'reset'],
    var_set: ['in'],
};
