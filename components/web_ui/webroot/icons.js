/* Ikon SVG sederhana (stroke-based, 1 warna, currentColor) untuk palette
 * node dan header node di canvas. Path minimal 24x24 viewBox, digambar
 * garis (bukan brand-specific), dipilih supaya ringan (tanpa file
 * gambar terpisah) dan otomatis ikut warna teks (currentColor). */
const NODE_ICONS = {
    and:        '<path d="M4 4h6a6 6 0 0 1 0 12H4V4Z"/><path d="M20 10h-4"/>',
    or:         '<path d="M4 4c4 0 8 2.5 8 6s-4 6-8 6c2-2 3-4 3-6s-1-4-3-6Z"/><path d="M20 10h-6"/>',
    not:        '<circle cx="17" cy="10" r="2"/><path d="M4 4v12l10-6L4 4Z"/>',
    xor:        '<path d="M6 4c4 0 8 2.5 8 6s-4 6-8 6c2-2 3-4 3-6s-1-4-3-6Z"/><path d="M3 4c1.4 1.7 2.2 3.9 2.2 6s-.8 4.3-2.2 6"/><path d="M20 10h-6"/>',
    compare:    '<path d="M6 8h12"/><path d="M6 16h12"/><path d="M9 8l3-4 3 4"/><path d="M9 16l3 4 3-4"/>',
    const:      '<circle cx="10" cy="10" r="6"/><path d="M20 10h-4"/>',
    var:        '<rect x="4" y="5" width="12" height="10" rx="2"/><path d="M20 10h-4"/><path d="M7 9h6M7 12h4"/>',
    math:       '<path d="M5 7h6M5 13h6"/><path d="M14 6l6 8M20 6l-6 8"/>',
    scale:      '<path d="M4 16h16"/><path d="M7 16V9M12 16V5M17 16v-8"/>',
    timer:      '<circle cx="11" cy="12" r="7"/><path d="M11 8v4l3 2"/><path d="M9 2h4"/>',
    counter:    '<rect x="3" y="6" width="14" height="10" rx="2"/><path d="M9 9v4M13 9v4"/><path d="M21 10v4"/>',
    osc:        '<path d="M2 16h4V8h5v8h5V8h5v8h2"/>',
    digital_in: '<rect x="3" y="7" width="10" height="8" rx="1"/><path d="M21 11h-8"/><circle cx="8" cy="11" r="1.5"/>',
    digital_out:'<rect x="11" y="7" width="10" height="8" rx="1"/><path d="M3 11h8"/><circle cx="16" cy="11" r="1.5"/>',
    analog_in:  '<path d="M3 12c2-4 3-6 4-6s2 8 3 8 2-8 3-8 2 6 4 6"/><path d="M20 12h1"/>',
    pwm:        '<path d="M3 15h3V8h3v7h3V6h3v9h3v-4h2"/>',
    servo:      '<circle cx="9" cy="12" r="4"/><path d="M9 8V4M13 4h4M17 4v4"/><path d="M17 12h4"/>',
    rgb:        '<circle cx="8" cy="8" r="3"/><circle cx="16" cy="8" r="3"/><circle cx="12" cy="15" r="3"/>',
    i2c:        '<rect x="3" y="5" width="8" height="14" rx="1"/><path d="M15 8h6M15 12h6M15 16h6"/>',
    i2c_burst:  '<rect x="3" y="4" width="8" height="16" rx="1"/><path d="M15 6h6M15 10h6M15 14h6M15 18h6"/>',
    sys:        '<circle cx="11" cy="11" r="7"/><path d="M11 4v14M4 11h14"/>',
    trash:      '<path d="M4 6h16"/><path d="M9 6V4h6v2"/><path d="M6 6l1 14h10l1-14"/><path d="M10 10v6M14 10v6"/>',
    default:    '<rect x="4" y="4" width="14" height="14" rx="2"/>',
};

function svgIcon(name, cssClass) {
    const paths = NODE_ICONS[name] || NODE_ICONS.default;
    return `<svg class="${cssClass || ''}" viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths}</svg>`;
}
