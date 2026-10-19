'use strict';

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
let ws;
let volTimeout, eqTimeout;
let wsReconnectDelay = 250;
let currentStation      = null;
let currentStationIndex = null;
let stationsList        = [];
const pendingStateCommands = new Map();

// These commands describe the desired final state, so keeping only the newest
// value is safe while the WebSocket handshake is still in progress.
const queueableCommands = new Set([
    'set_volume', 'set_eq_10', 'bt_enable', 'bt_volume', 'bt_sync_vol',
    'play_index', 'stop'
]);

const codecMap = {
    0:"UNK", 1:"RAW", 2:"WAV", 3:"MP3", 4:"AAC", 5:"OPUS",
    6:"M4A", 7:"MP4", 8:"FLAC", 9:"OGG", 10:"TSAAC",
    11:"AMR-NB", 12:"AMR-WB", 13:"PCM", 14:"M3U8", 15:"PLS", 16:"UNSUP"
};

const freqs = ["31","62","125","250","500","1k","2k","4k","8k","16k"];

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket
// ─────────────────────────────────────────────────────────────────────────────
function connect() {
    ws = new WebSocket(`ws://${location.host}/ws`);

    ws.onopen  = () => {
        console.log('WS connected');
        wsReconnectDelay = 250;
        for (const obj of pendingStateCommands.values()) {
            ws.send(JSON.stringify(obj));
        }
        pendingStateCommands.clear();
    };
    ws.onclose = () => {
        const delay = wsReconnectDelay;
        wsReconnectDelay = Math.min(wsReconnectDelay * 2, 3000);
        console.log(`WS disconnected — retry in ${delay} ms`);
        setTimeout(connect, delay);
    };
    ws.onerror = () => console.warn('WS error');

    ws.onmessage = (msg) => {
        console.log('RX:', msg.data);
        let data;
        try { data = JSON.parse(msg.data); } catch { return; }

        if (data.type === 'state') {
            applyRadioFromState(data);
            setVolumeUI(data.volume);

            if (data.curr_index !== undefined) {
                currentStationIndex = data.curr_index;
                selectStation(currentStationIndex);
            }
            if (data.station_name?.length) {
                document.getElementById('station_name').innerText = data.station_name;
                if (!data.title?.length)
                    document.getElementById('title').innerText = data.station_name;
            }
            if (data.title !== undefined) {
                const titleEl = document.getElementById('title');
                if (data.title?.length) titleEl.innerText = data.title;
                else if (currentStation)  titleEl.innerText = currentStation.name;
                else                      titleEl.innerText = '---';
            }
            if (data.sr !== undefined) {
                const infoEl = document.getElementById('audio_info');
                if (data.sr > 0) {
                    const codec = codecMap[data.fmt] || 'UNK';
                    const sr = data.sr >= 1000 ? (data.sr / 1000) + 'kHz' : data.sr + 'Hz';
                    const ch = data.ch === 2 ? 'stereo' : (data.ch === 1 ? 'mono' : data.ch + 'ch');
                    let txt = `${codec} · ${sr} · ${data.bits}bit · ${ch}`;
                    if (data.br) txt += ` · ${Math.round(data.br / 1000)} kbps`;
                    infoEl.innerText = txt;
                } else {
                    infoEl.innerText = '---';
                }
            }
            if (data.eq !== undefined) setEqUI(data.eq);
            if (data.bt_enable !== undefined) updateAudioSource(data.bt_enable);
        }
    };
}

function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
    else if (obj && queueableCommands.has(obj.cmd)) {
        pendingStateCommands.set(obj.cmd, obj);
        console.log('WS connecting — command queued:', obj.cmd);
    } else console.warn('WS not connected');
}

// ─────────────────────────────────────────────────────────────────────────────
// Playback — HTTP is the command path (works even if the WebSocket is down).
// WS is still used for live status.
// ─────────────────────────────────────────────────────────────────────────────
const RADIO_NUM = ['stopped', 'playing', 'buffering', 'error', 'finished'];
let radioUi = 'stopped';

function radioLabel(s) {
    if (typeof s === 'string' && s && s !== 'unknown') return s;
    if (typeof s === 'number' && RADIO_NUM[s]) return RADIO_NUM[s];
    return null;
}

function applyRadioFromState(s, force) {
    const radio = radioLabel(s && s.radio) || radioLabel(s && s.radio_state);
    if (radio) updateRadioStatus(radio, force);
}

async function fetchRadioState() {
    const res = await fetch('/api/state', { cache: 'no-store' });
    if (!res.ok) throw new Error('state ' + res.status);
    return res.json();
}

async function waitForRadio(doneStates, ms) {
    const deadline = Date.now() + ms;
    while (Date.now() < deadline) {
        try {
            const s = await fetchRadioState();
            applyRadioFromState(s, true);
            const radio = radioLabel(s.radio) || radioLabel(s.radio_state);
            if (radio && doneStates.includes(radio)) return radio;
        } catch (e) {
            console.warn('state poll failed', e);
        }
        await new Promise(r => setTimeout(r, 250));
    }
    return radioUi;
}

async function playCurrent() {
    if (currentStationIndex === null) {
        if (!stationsList.length) return;
        currentStationIndex = 0;
    }
    const index = currentStationIndex;
    updateRadioStatus('buffering', true);
    try {
        const res = await fetch('/api/play', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index }),
        });
        if (!res.ok) {
            console.warn('play failed', res.status, await res.text());
            updateRadioStatus('error', true);
            return;
        }
        await waitForRadio(['playing', 'error', 'stopped'], 8000);
    } catch (e) {
        console.warn('play request failed', e);
    }
}

async function stop() {
    try {
        await fetch('/api/play', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ stop: true }),
        });
        await waitForRadio(['stopped', 'error'], 3000);
    } catch (e) {
        console.warn('stop request failed', e);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────────────────────────────────────
function updateRadioStatus(state, force) {
    const badge = document.getElementById('status');
    const playBtn = document.getElementById('play_btn');

    const states = {
        playing:   { text: '▶ Playing',   cls: 'playing' },
        stopped:   { text: '⏹ Stopped',   cls: '' },
        buffering: { text: '⏳ Buffering', cls: 'buffering' },
        error:     { text: '❌ Error',     cls: 'error' },
        finished:  { text: '⏹ Stopped',   cls: '' },
    };

    if (!states[state]) return;
    // A queued WS "buffering" frame must not overwrite Playing after MUSIC_INFO.
    if (!force && state === 'buffering' && radioUi === 'playing') return;
    radioUi = state;

    const s = states[state];
    badge.innerText = s.text;
    badge.className = 'status-badge' + (s.cls ? ' ' + s.cls : '');

    if (state === 'playing' || state === 'buffering') {
        playBtn.innerText = '⏹ Stop';
        playBtn.onclick   = stop;
    } else {
        playBtn.innerText = '▶ Play';
        playBtn.onclick   = playCurrent;
    }

    if (state === 'stopped') document.getElementById('audio_info').innerText = '---';
}

// ─────────────────────────────────────────────────────────────────────────────
// Volume
// ─────────────────────────────────────────────────────────────────────────────
function onVolumeChange(v) {
    document.getElementById('vol_value').innerText = v;
    clearTimeout(volTimeout);
    volTimeout = setTimeout(() => send({ cmd: 'set_volume', value: parseInt(v) }), 150);
}

function setVolumeUI(v) {
    document.getElementById('volume_slider').value = v;
    document.getElementById('vol_value').innerText = v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Playlist / stations
// ─────────────────────────────────────────────────────────────────────────────
async function loadPlaylist() {
    try {
        const res  = await fetch('/api/playlist', { cache: 'no-store' });
        const arr  = await res.json();
        stationsList = (Array.isArray(arr) ? arr : []).map(s => ({
            name: s.name, url: s.url, selected: false
        }));
        renderStations(stationsList);
    } catch (e) {
        console.error('Playlist load error', e);
    }
}

function selectStation(index) {
    if (!stationsList?.[index]) return;
    currentStationIndex = index;
    currentStation = stationsList[index];
    document.getElementById('station_name').innerText = currentStation.name;
    document.getElementById('title').innerText        = currentStation.name;
    document.querySelectorAll('.station').forEach(e => e.classList.remove('active'));
    const el = document.getElementById('stations').children[index];
    if (el) { el.classList.add('active'); el.scrollIntoView({ behavior: 'auto', block: 'nearest' }); }
}

function renderStations(stations) {
    const container = document.getElementById('stations');
    container.innerHTML = '';
    stations.forEach((st, index) => {
        const el = document.createElement('div');
        el.className  = 'station';
        el.innerText  = st.name;
        if (currentStationIndex === index) {
            el.classList.add('active');
            currentStation = st;
            document.getElementById('station_name').innerText = st.name;
            document.getElementById('title').innerText        = st.name;
        }
        el.onclick = () => {
            selectStation(index);
            playCurrent();
        };
        container.appendChild(el);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Equalizer
// ─────────────────────────────────────────────────────────────────────────────
function renderEQ() {
    const container = document.getElementById('eq_bands');
    container.innerHTML = '';
    freqs.forEach((f, i) => {
        const el = document.createElement('div');
        el.innerHTML = `
            <label>${f}</label>
            <input type="range" min="-13" max="6" value="0" id="eq_${i}" oninput="onEqBandChange()" />
        `;
        container.appendChild(el);
    });
}

function onEqBandChange() {
    clearTimeout(eqTimeout);
    eqTimeout = setTimeout(() => {
        const values = freqs.map((_, i) => parseInt(document.getElementById(`eq_${i}`).value));
        drawEQ(values);
        send({ cmd: 'set_eq_10', bands: values });
    }, 150);
}

function setEqUI(eq) {
    eq.forEach((v, i) => { const s = document.getElementById(`eq_${i}`); if (s) s.value = v; });
    drawEQ(eq);
}

function drawEQ(values) {
    const canvas = document.getElementById('eq_canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#0a0a0e'; ctx.fillRect(0, 0, W, H);
    ctx.strokeStyle = '#1e1e28'; ctx.lineWidth = 1;
    [.25, .5, .75].forEach(r => {
        ctx.beginPath(); ctx.moveTo(0, H * r); ctx.lineTo(W, H * r); ctx.stroke();
    });
    ctx.strokeStyle = '#00aaff'; ctx.lineWidth = 2;
    ctx.beginPath();
    const step = W / (values.length - 1);
    values.forEach((v, i) => {
        const x = i * step, y = H / 2 - v * (H / 28);
        if (i === 0) { ctx.moveTo(x, y); return; }
        const px = (i - 1) * step, py = H / 2 - values[i-1] * (H / 28);
        ctx.quadraticCurveTo(px, py, (px + x) / 2, (py + y) / 2);
    });
    ctx.stroke();
}

function resetEQ() {
    const values = new Array(freqs.length).fill(0);
    setEqUI(values);
    send({ cmd: 'set_eq_10', bands: values });
}

function openEq()  { document.getElementById('eq_modal').classList.remove('hidden'); }
function closeEq() { document.getElementById('eq_modal').classList.add('hidden'); }

// ─────────────────────────────────────────────────────────────────────────────
// Audio source (Radio / Bluetooth) — drives bluetooth.enable
// ─────────────────────────────────────────────────────────────────────────────
function setAudioSource(bt) {
    updateAudioSource(bt);
    send({ cmd: 'bt_enable', value: bt });
}

function updateAudioSource(bt) {
    document.getElementById('srcRadioBtn')?.classList.toggle('active', !bt);
    document.getElementById('srcBtBtn')   ?.classList.toggle('active',  bt);
}

// ─────────────────────────────────────────────────────────────────────────────
// Modal helpers
// ─────────────────────────────────────────────────────────────────────────────
function onModalBgClick(e, id) {
    if (e.target === document.getElementById(id)) {
        document.getElementById(id).classList.add('hidden');
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Welcome / first-run help — flag stored in localStorage (per browser, no firmware)
// ─────────────────────────────────────────────────────────────────────────────
const HELP_SEEN_KEY = 'ac_help_seen';

function openWelcome() {
    document.getElementById('welcome_modal').classList.remove('hidden');
}

function closeWelcome() {
    if (document.getElementById('welcome_dontshow')?.checked) {
        localStorage.setItem(HELP_SEEN_KEY, '1');
    } else {
        localStorage.removeItem(HELP_SEEN_KEY);
    }
    document.getElementById('welcome_modal').classList.add('hidden');
}

function maybeShowWelcome() {
    if (!localStorage.getItem(HELP_SEEN_KEY)) openWelcome();
}

// ─────────────────────────────────────────────────────────────────────────────
// Firmware version — lets users confirm what was flashed
// ─────────────────────────────────────────────────────────────────────────────
async function loadVersion() {
    try {
        const res = await fetch('/api/state', { cache: 'no-store' });
        const s   = await res.json();
        if (s && s.version) {
            document.getElementById('fwVersion').textContent = s.version;
        }
        applyRadioFromState(s, true);
        const bar = document.getElementById('wwwOutdatedBar');
        if (bar && s) bar.style.display = s.www_outdated ? '' : 'none';
    } catch (_) {
        /* ignore — version is non-critical */
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────
connect();
loadPlaylist();
renderEQ();
loadVersion();
maybeShowWelcome();
