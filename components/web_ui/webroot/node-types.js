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

    // ---- I/O digital (Level 1 dasar) ----
    digital_input: {
        label: 'Digital In', inputs: 0, outputs: 1,
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 0 },
            { key: 'mode', label: 'Mode', type: 'select',
              options: ['pullup', 'pulldown', 'floating'], default: 'pullup' },
            { key: 'invert', label: 'Invert', type: 'checkbox', default: false },
        ],
    },
    digital_output: {
        label: 'Digital Out', inputs: 1, outputs: 0,
        fields: [
            { key: 'pin', label: 'Pin', type: 'number', default: 0 },
            { key: 'invert', label: 'Invert', type: 'checkbox', default: false },
        ],
    },
};

/* Label port input per tipe, khusus yang bukan generik "in0/in1". Dipakai
 * di app.js untuk render label port yang lebih jelas di canvas. */
const INPUT_PORT_LABELS = {
    ctu: ['clk', 'reset'],
    var_set: ['in'],
};
