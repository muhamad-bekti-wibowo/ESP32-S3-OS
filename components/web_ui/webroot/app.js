/* Logic UI editor FBD: registrasi node Drawflow, converter dua arah
 * Drawflow <-> schema.md, dan fetch save/load ke /api/program.
 * Lihat specs/04-drawflow-editor.md untuk scope & kriteria selesai. */

let editor;

function setStatus(msg, isError) {
    const el = document.getElementById('toolbar-status');
    el.textContent = msg;
    el.style.color = isError ? '#c00' : '#080';
}

/* ---- Setup Drawflow + palette ---- */

function fieldInputHtml(nodeDfId, field, value) {
    const id = `field-${nodeDfId}-${field.key}`;
    if (field.type === 'select') {
        const opts = field.options.map(o =>
            `<option value="${o}" ${o === value ? 'selected' : ''}>${o}</option>`
        ).join('');
        return `<label>${field.label}: <select id="${id}" df-field="${field.key}">${opts}</select></label>`;
    }
    if (field.type === 'checkbox') {
        return `<label><input type="checkbox" id="${id}" df-field="${field.key}" ${value ? 'checked' : ''}> ${field.label}</label>`;
    }
    if (field.type === 'number') {
        return `<label>${field.label}: <input type="number" id="${id}" df-field="${field.key}" value="${value}"></label>`;
    }
    return `<label>${field.label}: <input type="text" id="${id}" df-field="${field.key}" value="${value}"></label>`;
}

function buildNodeHtml(type, params) {
    const def = NODE_TYPES[type];
    let html = `<div class="fbd-node-title">${def.label}</div>`;
    def.fields.forEach(f => {
        const value = (params && params[f.key] !== undefined) ? params[f.key] : f.default;
        html += `<div class="fbd-field">${fieldInputHtml('X', f, value)}</div>`;
    });
    return html;
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
    const html = buildNodeHtml(type, params);
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

function initEditor() {
    const container = document.getElementById('drawflow');
    editor = new Drawflow(container);
    editor.reroute = true;
    editor.start();

    /* Simpan input field ke data node saat berubah, supaya editor.export()
     * membawa params terbaru (Drawflow tidak auto-sync form -> data). */
    container.addEventListener('input', (e) => {
        const fieldKey = e.target.getAttribute('df-field');
        if (!fieldKey) return;
        const nodeEl = e.target.closest('.drawflow-node');
        if (!nodeEl) return;
        const dfId = nodeEl.id.replace('node-', '');
        const nodeData = editor.getNodeFromId(dfId);
        let value = e.target.type === 'checkbox' ? e.target.checked : e.target.value;
        if (e.target.type === 'number') value = parseFloat(value);
        nodeData.data.params[fieldKey] = value;
        editor.updateNodeDataFromId(dfId, nodeData.data);
    });

    buildPalette();
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
                const targetNode = dfNodes[conn.node];
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
            html: buildNodeHtml(node.type, node.params || {}),
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
        setStatus('Load sukses');
    } catch (err) {
        setStatus(`Load error: ${err.message}`, true);
    }
}

initEditor();
