/* Logic UI editor FBD: registrasi node Drawflow, converter dua arah
 * Drawflow <-> schema.md, dan fetch save/load ke /api/program.
 * Lihat specs/04-drawflow-editor.md untuk scope & kriteria selesai.
 *
 * UI: node di canvas hanya tampil label + segitiga indicator (menyala
 * saat selected). Form params dipindah ke panel kanan (#properties),
 * diisi sesuai node yang sedang diklik - bukan di dalam node itu sendiri
 * (dulu bikin node crowded). */

let editor;
let selectedDfId = null;

function setStatus(msg, isError) {
    const el = document.getElementById('toolbar-status');
    el.textContent = msg;
    el.style.color = isError ? '#c00' : '#080';
}

/* ---- Node di canvas: label + segitiga, tanpa form ---- */

function buildNodeHtml(type) {
    const def = NODE_TYPES[type];
    /* Marker segitiga sengaja DI LUAR .fbd-node-title (bukan child-nya) -
     * posisinya absolute relatif ke kartu node penuh (lihat style.css
     * .fbd-node-marker), supaya selalu nempel di tepi bawah kartu, bukan
     * ikut tinggi baris judul. */
    return `<div class="fbd-node-title">
        ${svgIcon(def.icon, 'fbd-node-icon')}
        <span class="fbd-node-label">${def.label}</span>
    </div>
    <span class="fbd-node-marker" aria-hidden="true"></span>`;
}

function defaultParams(type) {
    const def = NODE_TYPES[type];
    const params = {};
    /* structuredClone/JSON roundtrip supaya array/object default (csv-bytes,
     * command-list) tidak dishare-reference antar node yang sama type-nya -
     * tanpa ini, edit command di satu node bisa "bocor" ke node lain. */
    def.fields.forEach(f => { params[f.key] = JSON.parse(JSON.stringify(f.default)); });
    return params;
}

function addNodeToCanvas(type, x, y) {
    const def = NODE_TYPES[type];
    const params = defaultParams(type);
    const html = buildNodeHtml(type);
    editor.addNode(
        type, def.inputs, def.outputs, x, y,
        'fbd-node', { fbdType: type, params }, html
    );
}

/* Palette dikelompokkan per category (lihat CATEGORY_LABELS di
 * node-types.js), tiap kelompok bisa dibuka/tutup (klik header), dan
 * ada kotak pencarian di atas yang memfilter berdasarkan label node -
 * kelompok yang tidak punya hasil otomatis disembunyikan, kelompok yang
 * punya hasil otomatis dibuka supaya hasil pencarian langsung terlihat. */
function groupNodeTypesByCategory() {
    const groups = {};
    Object.keys(NODE_TYPES).forEach(type => {
        const cat = NODE_TYPES[type].category || 'other';
        if (!groups[cat]) groups[cat] = [];
        groups[cat].push(type);
    });
    return groups;
}

function buildPalette() {
    const list = document.getElementById('palette-list');
    const groups = groupNodeTypesByCategory();
    list.innerHTML = '';

    Object.keys(groups).forEach(cat => {
        const section = document.createElement('div');
        section.className = 'palette-group';
        section.dataset.category = cat;

        const header = document.createElement('button');
        header.type = 'button';
        header.className = 'palette-group-header';
        header.innerHTML = `<span class="palette-group-caret">&#9656;</span><span>${CATEGORY_LABELS[cat] || cat}</span>`;
        header.onclick = () => section.classList.toggle('collapsed');
        section.appendChild(header);

        const body = document.createElement('div');
        body.className = 'palette-group-body';
        groups[cat].forEach(type => {
            const def = NODE_TYPES[type];
            const btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'palette-item';
            btn.dataset.label = def.label.toLowerCase();
            btn.innerHTML = `${svgIcon(def.icon, 'palette-item-icon')}<span>${def.label}</span>`;
            btn.onclick = () => {
                const rect = editor.precanvas.getBoundingClientRect();
                addNodeToCanvas(type, 100 - rect.x / editor.zoom, 100 - rect.y / editor.zoom);
            };
            body.appendChild(btn);
        });
        section.appendChild(body);
        list.appendChild(section);
    });
}

function filterPalette(query) {
    const q = query.trim().toLowerCase();
    document.querySelectorAll('.palette-group').forEach(section => {
        let anyMatch = false;
        section.querySelectorAll('.palette-item').forEach(item => {
            const matches = q === '' || item.dataset.label.includes(q);
            item.style.display = matches ? '' : 'none';
            if (matches) anyMatch = true;
        });
        section.style.display = anyMatch ? '' : 'none';
        if (q !== '' && anyMatch) section.classList.remove('collapsed');
    });
}

/* ---- Panel properties kanan ---- */

function fieldInputHtml(field, value) {
    const id = `prop-${field.key}`;
    if (field.type === 'select') {
        const opts = field.options.map(o =>
            `<option value="${o}" ${o === value ? 'selected' : ''}>${o}</option>`
        ).join('');
        return `<select id="${id}" df-field="${field.key}">${opts}</select>`;
    }
    if (field.type === 'checkbox') {
        return `<input type="checkbox" id="${id}" df-field="${field.key}" ${value ? 'checked' : ''}>`;
    }
    if (field.type === 'range') {
        /* Slider dengan angka live di sebelahnya - dipakai analog_input.sim_value
         * (spec 05: "slider untuk analog_input"), bukan number input polos. */
        return `<div class="properties-range-row">
            <input type="range" id="${id}" df-field="${field.key}" min="${field.min}" max="${field.max}" value="${value}"
                   oninput="document.getElementById('${id}-out').textContent = this.value">
            <span id="${id}-out" class="properties-range-value">${value}</span>
        </div>`;
    }
    if (field.type === 'number') {
        return `<input type="number" id="${id}" df-field="${field.key}" value="${value}">`;
    }
    if (field.type === 'csv-bytes') {
        /* Array byte (i2c_write_reg.data) diedit sebagai text "16,32" ->
         * di-parse jadi [16, 32] saat disimpan, lihat handler input di
         * initEditor(). Ditampilkan sebagai csv karena Drawflow/HTML form
         * tidak punya widget array bawaan yang sederhana. */
        const csv = Array.isArray(value) ? value.join(',') : value;
        return `<input type="text" id="${id}" df-field="${field.key}" df-field-type="csv-bytes" value="${csv}">`;
    }
    if (field.type === 'command-list') {
        /* i2c_write_burst.commands: daftar {register, data[]} yang bisa
         * ditambah/dihapus baris - dirender manual (bukan <input> tunggal),
         * lihat renderCommandListField() + wireCommandListEvents(). */
        return renderCommandListField(id, field, Array.isArray(value) ? value : []);
    }
    return `<input type="text" id="${id}" df-field="${field.key}" value="${value}">`;
}

function renderCommandListField(id, field, commands) {
    const rows = commands.map((cmd, i) => {
        const dataCsv = Array.isArray(cmd.data) ? cmd.data.join(',') : '';
        return `<div class="command-row" data-index="${i}">
            <span class="command-row-num">#${i + 1}</span>
            <input type="number" class="command-reg" value="${cmd.register || 0}" placeholder="reg">
            <input type="text" class="command-data" value="${dataCsv}" placeholder="data csv, mis. 16,32">
            <button type="button" class="command-row-remove" title="Hapus command ini">&times;</button>
        </div>`;
    }).join('');
    return `<div class="command-list" id="${id}" df-field="${field.key}" df-field-type="command-list">
        ${rows}
        <button type="button" class="command-list-add">+ Tambah command</button>
    </div>`;
}

/* Baca ulang seluruh command-list dari DOM (dipanggil setiap ada
 * perubahan - tambah/hapus baris atau edit input) -> array
 * [{register, data}] yang cocok dengan schema.md i2c_write_burst. */
function readCommandListFromDom(container) {
    const commands = [];
    container.querySelectorAll('.command-row').forEach(row => {
        const reg = parseInt(row.querySelector('.command-reg').value, 10) || 0;
        const data = row.querySelector('.command-data').value
            .split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));
        commands.push({ register: reg, data: data.length ? data : [0] });
    });
    return commands;
}

function commitCommandListField(container) {
    if (selectedDfId === null) return;
    const fieldKey = container.getAttribute('df-field');
    const nodeData = editor.getNodeFromId(selectedDfId);
    nodeData.data.params[fieldKey] = readCommandListFromDom(container);
    editor.updateNodeDataFromId(selectedDfId, nodeData.data);
}

/* Event delegation untuk command-list: tombol +/x dan edit input dalam
 * baris - dipasang sekali di panel Properties (lihat initEditor()),
 * bukan per-field, supaya tetap bekerja walau field di-render ulang. */
function wireCommandListEvents(panel) {
    panel.addEventListener('click', (e) => {
        const container = e.target.closest('.command-list');
        if (!container) return;
        if (e.target.classList.contains('command-list-add')) {
            const addBtn = e.target;
            const row = document.createElement('div');
            const index = container.querySelectorAll('.command-row').length;
            row.className = 'command-row';
            row.dataset.index = String(index);
            row.innerHTML = `<span class="command-row-num">#${index + 1}</span>
                <input type="number" class="command-reg" value="0" placeholder="reg">
                <input type="text" class="command-data" value="" placeholder="data csv, mis. 16,32">
                <button type="button" class="command-row-remove" title="Hapus command ini">&times;</button>`;
            container.insertBefore(row, addBtn);
            commitCommandListField(container);
        } else if (e.target.classList.contains('command-row-remove')) {
            e.target.closest('.command-row').remove();
            container.querySelectorAll('.command-row').forEach((row, i) => {
                row.dataset.index = String(i);
                row.querySelector('.command-row-num').textContent = `#${i + 1}`;
            });
            commitCommandListField(container);
        }
    });
    panel.addEventListener('input', (e) => {
        const container = e.target.closest('.command-list');
        if (!container) return;
        if (e.target.classList.contains('command-reg') || e.target.classList.contains('command-data')) {
            commitCommandListField(container);
        }
    });
}

function renderProperties(dfId) {
    const panel = document.getElementById('properties-body');
    if (dfId === null) {
        panel.innerHTML = '<p class="properties-empty">Pilih node untuk mengatur properties</p>';
        return;
    }

    const nodeData = editor.getNodeFromId(dfId);
    const def = NODE_TYPES[nodeData.data.fbdType];

    let html = `<div class="properties-node-header">
        <div class="properties-node-type">${def.label}</div>
        <button type="button" id="properties-delete-btn" class="properties-delete-btn" title="Hapus node ini">
            ${svgIcon('trash', '')} Hapus
        </button>
    </div>`;
    if (def.help) {
        html += `<div class="properties-help">${def.help}</div>`;
    }
    if (def.fields.length === 0) {
        html += '<p class="properties-empty">Node ini tidak punya parameter</p>';
    } else {
        def.fields.forEach(f => {
            const value = nodeData.data.params[f.key] !== undefined ? nodeData.data.params[f.key] : f.default;
            html += `<div class="properties-field">
                <label for="prop-${f.key}">${f.label}</label>
                ${fieldInputHtml(f, value)}
            </div>`;
        });
    }
    panel.innerHTML = html;

    const deleteBtn = document.getElementById('properties-delete-btn');
    if (deleteBtn) {
        deleteBtn.onclick = () => {
            editor.removeNodeId(`node-${dfId}`);
            selectNode(null);
        };
    }
}

function selectNode(dfId) {
    if (selectedDfId !== null) {
        const prevEl = document.getElementById(`node-${selectedDfId}`);
        if (prevEl) prevEl.classList.remove('fbd-selected');
    }
    selectedDfId = dfId;
    if (dfId !== null) {
        const el = document.getElementById(`node-${dfId}`);
        if (el) el.classList.add('fbd-selected');
    }
    renderProperties(dfId);
}

/* ---- Setup Drawflow ---- */

/* Drawflow bawaan panggil e.preventDefault() pada contextmenu (dipakai
 * untuk menu hapus node klik-kanan bawaannya) - ini menutup menu klik-
 * kanan asli browser (Inspect Element dkk) di seluruh area canvas.
 * Dipasang di CAPTURE phase (argumen ketiga true) supaya listener kita
 * jalan LEBIH DULU dari listener Drawflow (yang dipasang di bubble
 * phase) dan stopImmediatePropagation() mencegah listener Drawflow
 * sempat jalan sama sekali - klik kanan jadi menu browser normal,
 * bukan menu Drawflow. Fitur hapus node tetap ada lewat tombol Hapus
 * merah di panel Properties (lihat renderProperties()), jadi tidak
 * kehilangan fungsi apa pun dengan mematikan ini. */
function disableDrawflowContextMenu(container) {
    container.addEventListener('contextmenu', (e) => {
        e.stopImmediatePropagation();
    }, true);
}

function initEditor() {
    const container = document.getElementById('drawflow');
    editor = new Drawflow(container);
    editor.reroute = true;
    editor.start();
    disableDrawflowContextMenu(container);

    editor.on('nodeSelected', (dfId) => selectNode(dfId));
    editor.on('nodeUnselected', () => selectNode(null));
    editor.on('nodeRemoved', (dfId) => {
        if (String(selectedDfId) === String(dfId)) selectNode(null);
    });

    /* Simpan input field panel properties -> data node saat berubah,
     * supaya editor.export() membawa params terbaru. command-list punya
     * jalur commit sendiri (readCommandListFromDom), dilewati di sini. */
    const propertiesBody = document.getElementById('properties-body');
    propertiesBody.addEventListener('input', (e) => {
        const fieldKey = e.target.getAttribute('df-field');
        if (!fieldKey || selectedDfId === null) return;
        if (e.target.getAttribute('df-field-type') === 'command-list') return;
        const nodeData = editor.getNodeFromId(selectedDfId);
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        if (e.target.type === 'number' || e.target.type === 'range') value = parseFloat(value);
        if (e.target.getAttribute('df-field-type') === 'csv-bytes') {
            value = value.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));
        }
        nodeData.data.params[fieldKey] = value;
        editor.updateNodeDataFromId(selectedDfId, nodeData.data);
    });
    wireCommandListEvents(propertiesBody);

    document.getElementById('palette-search').addEventListener('input', (e) => {
        filterPalette(e.target.value);
    });

    buildPalette();
    renderProperties(null);
}

/* ---- Converter: Drawflow export format -> schema.md ---- */

function drawflowToSchema(drawflowExport) {
    const dfNodes = drawflowExport.drawflow.Home.data;
    const nodes = [];
    const links = [];

    /* Drawflow id numeric per-canvas -> id string schema.md (dipakai apa
     * adanya sebagai id node, cukup unik dalam satu document). */
    const idOf = (dfId) => `n${dfId}`;

    Object.keys(dfNodes).forEach(dfId => {
        const dfNode = dfNodes[dfId];
        nodes.push({
            id: idOf(dfId),
            type: dfNode.data.fbdType,
            params: dfNode.data.params,
        });

        Object.keys(dfNode.outputs || {}).forEach(outputKey => {
            const fromPort = parseInt(outputKey.replace('output_', ''), 10) - 1;
            (dfNode.outputs[outputKey].connections || []).forEach(conn => {
                const toPort = parseInt(conn.output.replace('input_', ''), 10) - 1;
                links.push({
                    from: { node: idOf(dfId), port: fromPort },
                    to: { node: idOf(conn.node), port: toPort },
                });
            });
        });
    });

    return { version: 1, nodes, links };
}

/* ---- Converter: schema.md -> Drawflow import format ---- */

function schemaToDrawflow(schemaObj) {
    const dfNodes = {};
    /* Map id string schema.md -> dfId numeric, dipakai untuk translate links. */
    const dfIdOf = {};
    let nextDfId = 1;

    schemaObj.nodes.forEach((node, idx) => {
        const dfId = nextDfId++;
        dfIdOf[node.id] = dfId;
        const def = NODE_TYPES[node.type];
        if (!def) {
            throw new Error(`node type tidak dikenal di editor: ${node.type}`);
        }
        const x = 100 + (idx % 5) * 220;
        const y = 80 + Math.floor(idx / 5) * 160;

        const inputs = {};
        for (let i = 0; i < def.inputs; i++) {
            inputs[`input_${i + 1}`] = { connections: [] };
        }
        const outputs = {};
        for (let i = 0; i < def.outputs; i++) {
            outputs[`output_${i + 1}`] = { connections: [] };
        }

        dfNodes[dfId] = {
            id: dfId,
            name: node.type,
            data: { fbdType: node.type, params: node.params || {} },
            class: 'fbd-node',
            html: buildNodeHtml(node.type),
            typenode: false,
            inputs,
            outputs,
            pos_x: x,
            pos_y: y,
        };
    });

    (schemaObj.links || []).forEach(link => {
        const fromDfId = dfIdOf[link.from.node];
        const toDfId = dfIdOf[link.to.node];
        const outputKey = `output_${link.from.port + 1}`;
        const inputKey = `input_${link.to.port + 1}`;

        dfNodes[fromDfId].outputs[outputKey].connections.push({
            node: String(toDfId), output: inputKey,
        });
        dfNodes[toDfId].inputs[inputKey].connections.push({
            node: String(fromDfId), input: outputKey,
        });
    });

    return { drawflow: { Home: { data: dfNodes } } };
}

/* ---- Save / Load ---- */

async function saveProgram() {
    try {
        const exported = editor.export();
        const schema = drawflowToSchema(exported);
        const res = await fetch('/api/program', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(schema),
        });
        if (!res.ok) {
            const body = await res.json().catch(() => ({ message: res.statusText }));
            setStatus(`Save gagal: ${body.message || res.statusText}`, true);
            return;
        }
        setStatus('Save sukses');
    } catch (err) {
        setStatus(`Save error: ${err.message}`, true);
    }
}

async function loadProgram() {
    try {
        const res = await fetch('/api/program');
        if (!res.ok) {
            setStatus(`Load gagal: ${res.statusText}`, true);
            return;
        }
        const schema = await res.json();
        const drawflowData = schemaToDrawflow(schema);
        editor.clear();
        editor.import(drawflowData);
        selectNode(null);
        setStatus('Load sukses');
    } catch (err) {
        setStatus(`Load error: ${err.message}`, true);
    }
}

initEditor();
