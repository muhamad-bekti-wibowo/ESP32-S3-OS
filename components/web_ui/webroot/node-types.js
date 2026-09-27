/* Metadata node type Level 0-1, sesuai schema.md.
 * Satu sumber kebenaran untuk: palette, registrasi Drawflow node, dan
 * converter drawflowToSchema/schemaToDrawflow di app.js.
 *
 * Field:
 *   inputs / outputs: jumlah port.
 *   fields: daftar field di params, dipakai untuk generate form HTML dan
 *     untuk baca/tulis nilai saat convert ke/dari schema.md.
 *     { key, label, type: 'number'|'text'|'select'|'checkbox', options?, default }
 */
const NODE_TYPES = {
    // ---- Logic ----
    and:  { label: 'AND',  inputs: 2, outputs: 1, fields: [] },
    or:   { label: 'OR',   inputs: 2, outputs: 1, fields: [] },
    not:  { label: 'NOT',  inputs: 1, outputs: 1, fields: [] },
    xor:  { label: 'XOR',  inputs: 2, outputs: 1, fields: [] },
    nand: { label: 'NAND', inputs: 2, outputs: 1, fields: [] },
    nor:  { label: 'NOR',  inputs: 2, outputs: 1, fields: [] },
    compare: {
        label: 'Compare', inputs: 2, outputs: 1,
        fields: [
            { key: 'op', label: 'Op', type: 'select',
              options: ['gt', 'lt', 'eq', 'neq', 'gte', 'lte'], default: 'gt' },
        ],
    },

    // ---- Data ----
    const: {
        label: 'Constant', inputs: 0, outputs: 1,
        fields: [
            { key: 'datatype', label: 'Type', type: 'select',
              options: ['bool', 'int32', 'float'], default: 'float' },
            { key: 'value', label: 'Value', type: 'number', default: 0 },
        ],
    },
    var_get: {
        label: 'Var Get', inputs: 0, outputs: 1,
        fields: [{ key: 'name', label: 'Name', type: 'text', default: 'var1' }],
    },
    var_set: {
        label: 'Var Set', inputs: 1, outputs: 1,
        fields: [{ key: 'name', label: 'Name', type: 'text', default: 'var1' }],
    },

    // ---- Math ----
    math: {
        label: 'Math', inputs: 2, outputs: 1,
        fields: [
            { key: 'op', label: 'Op', type: 'select',
              options: ['add', 'sub', 'mul', 'div'], default: 'add' },
        ],
    },
    min:  { label: 'Min', inputs: 2, outputs: 1, fields: [] },
    max:  { label: 'Max', inputs: 2, outputs: 1, fields: [] },
    abs:  { label: 'Abs', inputs: 1, outputs: 1, fields: [] },
    scale: {
        label: 'Scale', inputs: 1, outputs: 1,
        fields: [
            { key: 'in_min', label: 'In min', type: 'number', default: 0 },
            { key: 'in_max', label: 'In max', type: 'number', default: 4095 },
            { key: 'out_min', label: 'Out min', type: 'number', default: 0 },
            { key: 'out_max', label: 'Out max', type: 'number', default: 100 },
        ],
    },
    clamp: {
        label: 'Clamp', inputs: 1, outputs: 1,
        fields: [
            { key: 'min', label: 'Min', type: 'number', default: 0 },
            { key: 'max', label: 'Max', type: 'number', default: 100 },
        ],
    },

    // ---- Timing ----
    ton: {
        label: 'TON', inputs: 1, outputs: 1,
        fields: [{ key: 'delay_ms', label: 'Delay (ms)', type: 'number', default: 1000 }],
    },
    tof: {
        label: 'TOF', inputs: 1, outputs: 1,
        fields: [{ key: 'delay_ms', label: 'Delay (ms)', type: 'number', default: 1000 }],
    },
    tp: {
        label: 'TP', inputs: 1, outputs: 1,
        fields: [{ key: 'pulse_ms', label: 'Pulse (ms)', type: 'number', default: 500 }],
    },
    ctu: {
        label: 'Counter (CTU)', inputs: 2, outputs: 1,
        fields: [{ key: 'preset', label: 'Preset', type: 'number', default: 3 }],
    },

    // ---- I/O digital (Level 1) ----
    digital_input: {
        label: 'Digital In', inputs: 0, outputs: 1,
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
        label: 'Digital Out', inputs: 1, outputs: 0,
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 0 },
            { key: 'invert', label: 'Invert', type: 'checkbox', default: false },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },

    // ---- I/O analog & PWM (Level 1) ----
    analog_input: {
        label: 'Analog In', inputs: 0, outputs: 1,
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
        label: 'PWM Out', inputs: 1, outputs: 0,
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 5 },
            { key: 'frequency', label: 'Frequency (Hz)', type: 'number', default: 1000 },
            { key: 'resolution', label: 'Resolution (bit)', type: 'number', default: 12 },
            { key: 'hw_mode', label: 'HW Mode', type: 'select',
              options: ['simulated', 'real'], default: 'simulated' },
        ],
    },
    servo: {
        label: 'Servo', inputs: 1, outputs: 0,
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
        label: 'I2C Read Reg', inputs: 0, outputs: 2,
        fields: [
            { key: 'bus', label: 'Bus', type: 'number', default: 0 },
            { key: 'address', label: 'Address (7-bit)', type: 'number', default: 0x27 },
            { key: 'register', label: 'Register', type: 'number', default: 0 },
            { key: 'length', label: 'Length (1-8)', type: 'number', default: 2 },
        ],
    },
    i2c_write_reg: {
        label: 'I2C Write Reg', inputs: 0, outputs: 1,
        fields: [
            { key: 'bus', label: 'Bus', type: 'number', default: 0 },
            { key: 'address', label: 'Address (7-bit)', type: 'number', default: 0x27 },
            { key: 'register', label: 'Register', type: 'number', default: 0 },
            { key: 'data', label: 'Data (csv, e.g. 16,32)', type: 'csv-bytes', default: [0] },
        ],
    },

    // ---- System variable read-only (WiFi, dst - BUKAN dikonfigurasi di sini) ----
    sys_var_get: {
        label: 'Sys Var Get', inputs: 0, outputs: 1,
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
