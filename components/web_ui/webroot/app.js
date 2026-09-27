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
    return `<div class="fbd-node-title">
        <span class="fbd-node-label">${def.label}</span>
        <span class="fbd-node-marker" aria-hidden="true"></span>
    </div>`;
}

function defaultParams(type) {
    const def = NODE_TYPES[type];
    const params = {};
    def.fields.forEach(f => { params[f.key] = f.default; });
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

function buildPalette() {
    const list = document.getElementById('palette-list');
    Object.keys(NODE_TYPES).forEach(type => {
        const btn = document.createElement('button');
        btn.textContent = NODE_TYPES[type].label;
        btn.onclick = () => {
            const rect = editor.precanvas.getBoundingClientRect();
            addNodeToCanvas(type, 100 - rect.x / editor.zoom, 100 - rect.y / editor.zoom);
        };
        list.appendChild(btn);
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
    if (field.type === 'number') {
        return `<input type="number" id="${id}" df-field="${field.key}" value="${value}">`;
    }
    return `<input type="text" id="${id}" df-field="${field.key}" value="${value}">`;
}

function renderProperties(dfId) {
    const panel = document.getElementById('properties-body');
    if (dfId === null) {
        panel.innerHTML = '<p class="properties-empty">Pilih node untuk mengatur properties</p>';
        return;
    }

    const nodeData = editor.getNodeFromId(dfId);
    const def = NODE_TYPES[nodeData.data.fbdType];

    let html = `<div class="properties-node-type">${def.label}</div>`;
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

function initEditor() {
    const container = document.getElementById('drawflow');
    editor = new Drawflow(container);
    editor.reroute = true;
    editor.start();

    editor.on('nodeSelected', (dfId) => selectNode(dfId));
    editor.on('nodeUnselected', () => selectNode(null));
    editor.on('nodeRemoved', (dfId) => {
        if (String(selectedDfId) === String(dfId)) selectNode(null);
    });

    /* Simpan input field panel properties -> data node saat berubah,
     * supaya editor.export() membawa params terbaru. */
    document.getElementById('properties-body').addEventListener('input', (e) => {
        const fieldKey = e.target.getAttribute('df-field');
        if (!fieldKey || selectedDfId === null) return;
        const nodeData = editor.getNodeFromId(selectedDfId);
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        if (e.target.type === 'number') value = parseFloat(value);
        nodeData.data.params[fieldKey] = value;
        editor.updateNodeDataFromId(selectedDfId, nodeData.data);
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
