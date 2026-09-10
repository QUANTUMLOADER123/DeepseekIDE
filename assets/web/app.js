/* ================================================================
 * DeepSeekIDE — фронтенд (ванильный JS, без сборки)
 * Бэкенд: локальный HTTP-сервер deepseekide.exe (эндпоинты /api/*)
 * ================================================================ */
'use strict';

// ------------------------- токен / api -------------------------
const TOKEN = (() => {
  const u = new URL(location.href);
  let t = u.searchParams.get('t') || sessionStorage.getItem('dside.t') || '';
  if (t) {
    sessionStorage.setItem('dside.t', t);
    if (u.searchParams.get('t')) { u.searchParams.delete('t'); history.replaceState(null, '', u); }
  }
  return t;
})();

async function api(path, opts = {}) {
  opts.headers = Object.assign({ 'X-DeepSeekIDE-Token': TOKEN }, opts.headers || {});
  if (opts.json !== undefined) {
    opts.method = opts.method || 'POST';
    opts.body = JSON.stringify(opts.json);
    opts.headers['Content-Type'] = 'application/json';
  }
  const res = await fetch(path, opts);
  const text = await res.text();
  let data;
  try { data = JSON.parse(text); } catch { data = { error: text.slice(0, 300) }; }
  if (!res.ok && !data.error) data.error = 'HTTP ' + res.status;
  return data;
}

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const hsize = (b) => b > 1e6 ? (b/1e6).toFixed(1)+' МБ' : b > 1024 ? (b/1024).toFixed(1)+' КБ' : (b||0)+' B';
function sysMsg(text) { addFeedText('sys', 'ide', text); }

// ------------------------- лента агента -------------------------
function addFeedText(kind, whom, text) {
  const feed = $('agentFeed');
  const el = document.createElement('div');
  el.className = 'msg ' + kind;
  el.innerHTML = '<div class="whom">' + esc(whom) + '</div><div class="body"></div>';
  el.querySelector('.body').textContent = text;
  feed.appendChild(el);
  feed.scrollTop = feed.scrollHeight;
  while (feed.children.length > 120) feed.firstChild.remove();
}

// ------------------------- монако -------------------------
let monacoEditor = null, monacoReady = false;

function langFor(path) {
  const m = /\.([a-z0-9]+)$/i.exec(path || '');
  const ext = m ? m[1].toLowerCase() : '';
  return ({
    js:'javascript', mjs:'javascript', jsx:'javascript', ts:'typescript', tsx:'typescript',
    json:'json', html:'html', htm:'html', css:'css', scss:'scss', less:'less', md:'markdown',
    py:'python', cpp:'cpp', cxx:'cpp', cc:'cpp', c:'c', h:'cpp', hpp:'cpp', cs:'csharp',
    java:'java', go:'go', rs:'rust', php:'php', rb:'ruby', sh:'shell', bat:'bat', cmd:'bat',
    yml:'yaml', yaml:'yaml', xml:'xml', sql:'sql', lua:'lua', kt:'kotlin', swift:'swift',
    txt:'plaintext', log:'plaintext', ini:'ini', toml:'ini', dockerfile:'dockerfile', cmake:'cmake'
  })[ext] || 'plaintext';
}

function loadMonaco() {
  return new Promise((resolve) => {
    const s = document.createElement('script');
    s.src = 'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/vs/loader.js';
    s.onload = () => {
      /* global require */
      require.config({ paths: { vs: 'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/vs' } });
      // Кросс-доменный воркер: оборачиваем в dataURL (официальный трюк для CDN)
      window.MonacoEnvironment = {
        getWorkerUrl: function () {
          const proxy =
            'data:text/javascript;charset=utf-8,' +
            encodeURIComponent(
              "self.MonacoEnvironment={baseUrl:'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/'};" +
              "importScripts('https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min/vs/base/worker/workerMain.js');"
            );
          return proxy;
        },
      };
      require(['vs/editor/editor.main'], () => {
        monaco.editor.defineTheme('dsLight', {
          base: 'vs', inherit: true,
          rules: [
            { token: '', foreground: '2b4354', background: 'f2f7fb' },
            { token: 'comment', foreground: '8fa8bc', fontStyle: 'italic' },
            { token: 'keyword', foreground: '2b87c4', fontStyle: 'bold' },
            { token: 'string', foreground: '3e9e6e' },
            { token: 'number', foreground: 'c07f1f' },
            { token: 'type', foreground: '0f7c9e' },
            { token: 'function', foreground: '7a5fd1' },
          ],
          colors: {
            'editor.background': '#f2f7fb',
            'editor.lineHighlightBackground': '#eaf2f8',
            'editorLineNumber.foreground': '#a9c3d6',
            'editorLineNumber.activeForeground': '#57a9df',
            'editor.overviewRulerBorder': '#d7e6f0',
            'editorIndentGuide.background1': '#dbe9f2',
            'editorCursor.foreground': '#3290c8',
            'editor.selectionBackground': '#bfe3f7',
          },
        });
        monaco.editor.setTheme('dsLight');
        monacoEditor = monaco.editor.create($('editor'), {
          value: '', language: 'plaintext', theme: 'dsLight',
          fontFamily: '"Cascadia Code", Consolas, "JetBrains Mono", monospace',
          fontSize: 14, fontLigatures: true,
          minimap: { enabled: true, scale: 0.8 },
          automaticLayout: true,
          scrollBeyondLastLine: false,
          renderWhitespace: 'selection',
          smoothScrolling: true, cursorSmoothCaretAnimation: 'on',
          padding: { top: 10 },
        });
        monacoEditor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, saveCurrentFile);
        monacoEditor.onDidChangeModelContent(() => {
          const t = tabs[cur];
          if (!t) return;
          t.text = monacoEditor.getValue();
          setDirty(t, t.text !== t.savedText);
        });
        monacoReady = true;
        resolve(true);
      });
    };
    s.onerror = () => resolve(false);
    setTimeout(() => { if (!monacoReady) resolve(false); }, 20000);
    document.body.appendChild(s);
  });
}

// Фоллбэк без Монако: голый textarea (редактирование всё равно работает)
let fallbackTA = null;
function makeFallback() {
  fallbackTA = document.createElement('textarea');
  fallbackTA.className = 'fallback';
  fallbackTA.spellcheck = false;
  fallbackTA.addEventListener('input', () => {
    const t = tabs[cur];
    if (!t) return;
    t.text = fallbackTA.value;
    setDirty(t, t.text !== t.savedText);
  });
  fallbackTA.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); saveCurrentFile(); }
    if (e.key === 'Tab') {
      e.preventDefault();
      const { selectionStart: a, selectionEnd: b } = fallbackTA;
      fallbackTA.setRangeText('    ', a, b, 'end');
    }
  });
  $('editor').appendChild(fallbackTA);
  sysMsg('Monaco не загрузился (нет интернета?) — включён простой редактор.');
}

// ------------------------- табы -------------------------
let tabs = [];      // {path, name, text, savedText}
let cur = -1;

function renderTabs() {
  const w = $('editorTabs');
  if (!tabs.length) { w.innerHTML = '<div class="hint">Откройте файл слева</div>'; }
  else {
    w.innerHTML = '';
    tabs.forEach((t, i) => {
      const el = document.createElement('div');
      el.className = 'tab' + (i === cur ? ' active' : '');
      el.innerHTML = esc(t.name) +
        (t.dirty ? '<span class="dirty-dot">●</span>' : '') +
        '<span class="close" data-i="' + i + '">×</span>';
      el.onclick = (e) => {
        if (e.target.dataset.i !== undefined) { closeTab(+e.target.dataset.i); return; }
        activateTab(i);
      };
      w.appendChild(el);
    });
  }
  const t = tabs[cur];
  $('esPath').textContent = t ? t.path : '';
  $('esSaved').textContent = t ? (t.dirty ? '● не сохранено — Ctrl+S' : 'сохранено ✓') : '';
}

function setDirty(t, v) { t.dirty = v; renderTabs(); }

function activateTab(i) {
  if (i < 0 || i >= tabs.length) return;
  // синхронизируем текущий
  if (monacoEditor && cur >= 0 && tabs[cur]) tabs[cur].text = monacoEditor.getValue();
  if (fallbackTA && cur >= 0 && tabs[cur]) tabs[cur].text = fallbackTA.value;
  cur = i;
  const t = tabs[i];
  if (monacoEditor) {
    monaco.editor.setModelLanguage(monacoEditor.getModel(), langFor(t.path));
    monacoEditor.setValue(t.text);
  } else if (fallbackTA) {
    fallbackTA.value = t.text;
  }
  $('esMeta').textContent = langFor(t.path) + ' · ' + hsize(t.text.length);
  renderTabs();
  // подсветим в дереве
  document.querySelectorAll('.tree .node.active').forEach((n) => n.classList.remove('active'));
  const node = document.querySelector('.tree .node[data-path="' + CSS.escape(t.path) + '"]');
  if (node) node.classList.add('active');
}

function closeTab(i) {
  const t = tabs[i];
  if (t && t.dirty && !confirm('Файл «' + t.name + '» не сохранён. Закрыть без сохранения?')) return;
  tabs.splice(i, 1);
  if (cur >= tabs.length) cur = tabs.length - 1;
  if (cur >= 0) activateTab(cur);
  else { if (monacoEditor) monacoEditor.setValue(''); if (fallbackTA) fallbackTA.value = ''; renderTabs(); $('esMeta').textContent = ''; }
}

async function openFile(path) {
  const idx = tabs.findIndex((t) => t.path === path);
  if (idx >= 0) { activateTab(idx); return; }
  $('esMeta').textContent = 'читаю…';
  const r = await api('/api/file?path=' + encodeURIComponent(path));
  if (r.error) { sysMsg('Не открыть «' + path + '»: ' + r.error); $('esMeta').textContent = ''; return; }
  tabs.push({ path, name: path.split('/').pop(), text: r.content, savedText: r.content, dirty: false });
  activateTab(tabs.length - 1);
}

async function saveCurrentFile() {
  const t = tabs[cur];
  if (!t) return;
  if (monacoEditor) t.text = monacoEditor.getValue();
  else if (fallbackTA) t.text = fallbackTA.value;
  const r = await api('/api/file/save', { json: { path: t.path, content: t.text } });
  if (r.error) { sysMsg('Сохранение «' + t.path + '»: ' + r.error); return; }
  t.savedText = t.text;
  setDirty(t, false);
}

// ------------------------- дерево -------------------------
function renderTreeNode(node, depth) {
  const ul = document.createElement('ul');
  if (depth > 0) ul.className = 'kids';
  const items = node.children || [];
  for (const kid of items) {
    const li = document.createElement('li');
    const div = document.createElement('div');
    div.className = 'node';
    div.dataset.path = kid.path;
    const ico = kid.dir ? '📁' : fileIco(kid.name);
    div.innerHTML = '<span class="ico">' + ico + '</span><span>' + esc(kid.name) + '</span>' +
                    (kid.dir ? '' : '<span class="sz">' + hsize(kid.size) + '</span>');
    li.appendChild(div);
    if (kid.dir) {
      const kids = renderTreeNode(kid, depth + 1);
      kids.classList.add('kids');
      kids.hidden = depth > 0;
      li.appendChild(kids);
      div.onclick = () => { kids.hidden = !kids.hidden; };
    } else {
      div.onclick = () => openFile(kid.path);
    }
    ul.appendChild(li);
  }
  return ul;
}

function fileIco(name) {
  const m = /\.([a-z0-9]+)$/i.exec(name || '');
  const ext = m ? m[1].toLowerCase() : '';
  return ({ cpp:'⚙️', h:'⚙️', c:'⚙️', js:'🟨', ts:'🟦', py:'🐍', md:'📝', json:'🧾',
            txt:'📄', cmake:'🧱', html:'🌐', css:'🎨', png:'🖼️', svg:'🖼️', jpg:'🖼️' })[ext] || '📄';
}

// ------------------------- состояние -------------------------
let lastProjRoot = null;
async function pollState() {
  const st = await api('/api/state');
  if (st.error) return;

  // проект
  const open = st.project && st.project.open;
  const root = open ? st.project.root : null;
  if (open) {
    $('projName').textContent = (root || '').split(/[\\/]/).pop() + ' — ' + root;
    if (root !== lastProjRoot || st.dirty) refreshTree(st.project.tree);
    lastProjRoot = root;
  } else {
    $('projName').textContent = 'проект не открыт';
    $('tree').innerHTML = '<div class="hint">Откройте папку проекта</div>';
    lastProjRoot = null;
  }

  // чат
  const cs = $('chatStatus');
  const ch = st.chat || {};
  cs.classList.remove('on', 'off', 'busy');
  let dot = 'off', text = ch.stageText || '—';
  if (ch.stage === 'online') dot = 'on';
  else if (ch.stage === 'busy') dot = 'busy';
  else if (ch.stage === 'launching' || ch.stage === 'connecting') dot = 'busy';
  cs.classList.add(dot);
  cs.classList.add(dot === 'off' ? 'off' : dot);
  $('chatStatusText').textContent = text + (ch.chatPresent ? '' : (ch.stage === 'online' || ch.stage === 'busy' ? ' · войдите в окне браузера' : ''));
  $('btnConnect').textContent = (ch.stage === 'online' || ch.stage === 'busy') ? '→ Открыть вкладку' : '▶ Подключить чат';
  $('btnStop').classList.toggle('hidden', ch.stage !== 'busy');
}

function refreshTree(tree) {
  const w = $('tree');
  w.innerHTML = '';
  if (!tree) { w.innerHTML = '<div class="hint">Пусто</div>'; return; }
  w.appendChild(renderTreeNode(tree, 0));
}

// ------------------------- события (файл изменён агентом) -------------------------
// храним «уже показанные» события простым снапшотом: сервер держит кольцо — поступим честно:
// при первой прорисовке запоминаем список, дальше показываем разность.
let seenEvents = null;
async function pollEventsSmart() {
  const r = await api('/api/events');
  if (!r.events) return;
  const ids = r.events.map((e) => (e.type + '|' + (e.path || '')));
  if (seenEvents === null) { seenEvents = new Set(ids); return; }
  for (let i = 0; i < r.events.length; ++i) {
    if (seenEvents.has(ids[i])) continue;
    const ev = r.events[i];
    seenEvents.add(ids[i]);
    if (seenEvents.size > 500) seenEvents = new Set(ids.slice(-200));
    if (ev.type === 'fileChanged') {
      const idx = tabs.findIndex((t) => t.path === ev.path);
      if (idx >= 0) {
        const fresh = await api('/api/file?path=' + encodeURIComponent(ev.path));
        if (!fresh.error && !tabs[idx].dirty) {
          tabs[idx].text = fresh.content;
          tabs[idx].savedText = fresh.content;
          if (idx === cur) activateTab(cur);
        }
      }
    }
  }
}

// ------------------------- ответ агента -------------------------
let pending = null;
async function pollReply() {
  if (pending) return;
  const r = await api('/api/chat/reply');
  if (!r.ready) return;
  pending = r;
  addFeedText('agent', 'deepseek', r.text || '(пусто)');
  showPending(r);
}

function showPending(r) {
  const box = $('pendingReply');
  box.classList.remove('hidden');
  $('prText').textContent = r.text || '';
  $('prOpsCount').textContent = (r.ops || []).length + ' операций';
  $('applyReport').classList.add('hidden');
  const ops = $('prOps');
  ops.innerHTML = '';
  (r.ops || []).forEach((op, i) => {
    const div = document.createElement('label');
    div.className = 'op ' + opClass(op.name);
    div.innerHTML = '<input type="checkbox" checked data-i="' + i + '"><span class="desc">' +
                    esc(op.describe || op.name) + '</span>';
    ops.appendChild(div);
  });
  (r.errs || []).forEach((e) => sysMsg('парсер: ' + e));
}

function opClass(name) {
  if (name === 'write_file' || name === 'append_file') return 'write';
  if (name.startsWith('edit') || name.startsWith('insert') || name.startsWith('replace')) return 'edit';
  if (name.startsWith('delete')) return 'delete';
  return 'mkdir';
}

async function applyPending(selectedOnly) {
  if (!pending) return;
  const chosen = [];
  document.querySelectorAll('#prOps input[type=checkbox]').forEach((cb) => {
    if (cb.checked) chosen.push(pending.ops[+cb.dataset.i]);
  });
  if (!chosen.length) { sysMsg('Ни одной выбранной операции.'); return; }
  const r = await api('/api/chat/apply', { json: { ops: chosen } });
  if (r.error) { sysMsg('Применение: ' + r.error); return; }
  addFeedText('sys', 'применено', r.report || 'готово');
  const rep = $('applyReport');
  rep.textContent = r.report || '';
  rep.classList.remove('hidden');
  $('prOpsCount').textContent = 'готово: ' + r.done + '/' + r.total;
}

// ------------------------- журнал -------------------------
let logShown = -1;
async function pollLog() {
  const r = await api('/api/log');
  if (!r.entries) { $('log').innerHTML = '<div class="line info">…</div>'; return; }
  if (r.entries.length < logShown) logShown = -1;  // перезапуск сервера
  const w = $('log');
  const stick = w.scrollHeight - w.scrollTop - w.clientHeight < 30;
  for (let i = Math.max(0, logShown); i < r.entries.length; ++i) {
    const e = r.entries[i];
    const line = document.createElement('div');
    line.className = 'line ' + e.level;
    const t = new Date(e.at);
    const hh = String(t.getHours()).padStart(2, '0');
    const mm = String(t.getMinutes()).padStart(2, '0');
    const ss = String(t.getSeconds()).padStart(2, '0');
    line.innerHTML = '<span class="time">' + hh + ':' + mm + ':' + ss + '</span>' + esc(e.msg);
    w.appendChild(line);
  }
  logShown = r.entries.length;
  while (w.children.length > 500) w.firstChild.remove();
  if (stick) w.scrollTop = w.scrollHeight;
}

// ------------------------- снапшоты -------------------------
async function pollSnaps() {
  const r = await api('/api/snapshots');
  const w = $('snaps');
  if (!r.snapshots || !r.snapshots.length) {
    w.innerHTML = '<div class="hint">Снимков пока нет — появятся при правках от агента</div>';
    return;
  }
  w.innerHTML = '';
  r.snapshots.slice().reverse().forEach((s) => {
    const div = document.createElement('div');
    div.className = 'snap';
    div.innerHTML =
      '<div class="title">' + esc(s.title || s.id) + '</div>' +
      '<div class="meta">' + esc((s.iso || '').replace('T', ' ').slice(0, 16)) + ' · ' + s.files.length + ' файл(ов)</div>' +
      '<div class="row">' +
      '  <button class="btn tiny" data-roll="' + esc(s.id) + '">откатить к этому</button>' +
      '  <button class="btn tiny danger" data-forget="' + esc(s.id) + '">забыть</button>' +
      '</div>' +
      '<div class="files">' + esc(s.files.join(', ')) + '</div>';
    w.appendChild(div);
  });
  w.querySelectorAll('[data-roll]').forEach((b) => {
    b.onclick = async () => {
      if (!confirm('Откатить проект К ЗНЕМУ и всё новее? Правки придётся доделывать руками.')) return;
      const rr = await api('/api/snapshots/rollback', { json: { id: b.dataset.roll } });
      sysMsg('Откат: ' + (rr.ok ? 'успешно' : 'ошибка') + '\n' + (rr.log || ''));
      refreshTreeNow();
    };
  });
  w.querySelectorAll('[data-forget]').forEach((b) => {
    b.onclick = async () => { await api('/api/snapshots/forget', { json: { id: b.dataset.forget } }); pollSnaps(); };
  });
}

async function refreshTreeNow() { await pollState(); }

// ------------------------- кнопки -------------------------
function bind() {
  $('tabFiles').onclick = () => setSide('files');
  $('tabSessions').onclick = () => setSide('sessions');
  $('btnSessionsRefresh').onclick = pollSessions;

  $('btnNewTab').onclick = async () => {
    const r = await api('/api/chat/newtab', { method: 'POST', json: {} });
    if (r.error) sysMsg('+ Вкладка: ' + r.error);
    else sysMsg('+ Вкладка чата открыта в отладочном браузере');
    pollState();
  };
  $('btnDebug').onclick = async () => {
    sysMsg('Запрашиваю диагностику…');
    const r = await api('/api/chat/debug');
    if (r.error) sysMsg('Диагностика: ' + r.error);
    else addFeedText('sys', 'диагностика', r.info || '(пусто)');
  };

  $('btnOpenFolder').onclick = async () => {
    const path = prompt('Полный путь к папке проекта:\n(например C:\\Projects\\mygame)', lastProjRoot || '');
    if (!path) return;
    const r = await api('/api/project/open', { json: { path } });
    if (r.error) sysMsg('Не открыть: ' + r.error);
    pollState();
  };

  $('btnConnect').onclick = async () => {
    $('btnConnect').disabled = true;
    $('btnConnect').textContent = '…подключаю';
    const r = await api('/api/chat/connect', { method: 'POST', json: {} });
    $('btnConnect').disabled = false;
    if (r.error) sysMsg('Подключение чата: ' + r.error);
    pollState();
  };

  $('btnNewChat').onclick = () => api('/api/chat/newchat', { method: 'POST', json: {} });
  $('btnStop').onclick = async () => { await api('/api/chat/cancel', { method: 'POST', json: {} }); pollState(); };

  async function send(kind) {
    const el = $('taskText');
    const text = el.value.trim();
    if (!text) return;
    const r = await api('/api/chat/send', { json: { text, kind } });
    if (r.error) { sysMsg('Отправка: ' + r.error); return; }
    addFeedText('me', 'вы', text);
    el.value = '';
  }
  $('btnSendTask').onclick = () => send('task');
  $('btnSendNote').onclick = () => send('note');
  $('taskText').addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); send('task'); }
  });

  $('btnApply').onclick = () => applyPending(true);
  $('btnDiscard').onclick = () => { pending = null; $('pendingReply').classList.add('hidden'); };

  $('btnSnapsRefresh').onclick = pollSnaps;

  $('logHead').onclick = (e) => {
    if (e.target.id === 'btnLogClear') { $('log').innerHTML = ''; return; }
    $('logbar').classList.toggle('collapsed');
  };

  $('btnShutdown').onclick = async () => {
    if (!confirm('Выключить DeepSeekIDE (сервер)? Вкладку можно будет просто закрыть.')) return;
    await api('/api/shutdown', { json: {} });
    document.body.innerHTML = '<div id="bye">DeepSeekIDE выключена.<br>Вкладку можно закрыть.</div>';
  };

  window.addEventListener('beforeunload', (e) => {
    if (tabs.some((t) => t.dirty)) { e.preventDefault(); e.returnValue = ''; }
  });
}

// ------------------------- боковые вкладки (Проводник/Сессии) -------------------------
function setSide(name) {
  $('tabFiles').classList.toggle('active', name === 'files');
  $('tabSessions').classList.toggle('active', name === 'sessions');
  $('pageFiles').classList.toggle('hidden', name !== 'files');
  $('pageSessions').classList.toggle('hidden', name !== 'sessions');
}

// ------------------------- сессии диалогов -------------------------
let activeSessionId = null;
async function pollSessions() {
  const r = await api('/api/sessions');
  if (!r.sessions) return;
  const w = $('sessions');
  if (!r.sessions.length) {
    if (!activeSessionId)
      w.innerHTML = '<div class="hint">Пока пусто — они появятся сами, когда агент ответит</div>';
    return;
  }
  w.innerHTML = '';
  r.sessions.slice().reverse().forEach((s) => {
    const d = new Date(s.at);
    const hh = String(d.getHours()).padStart(2, '0');
    const mm = String(d.getMinutes()).padStart(2, '0');
    const div = document.createElement('div');
    div.className = 'sess';
    div.innerHTML =
      '<div class="title">' + esc(s.title) +
      '  <span class="badge">' + (s.opsCount || 0) + ' оп</span></div>' +
      '<div class="meta">' + hh + ':' + mm + ' · ' + d.toLocaleDateString('ru-RU') + '</div>';
    div.onclick = () => openSession(s.id);
    w.appendChild(div);
  });
}

async function openSession(id) {
  activeSessionId = id;
  const s = await api('/api/session?id=' + encodeURIComponent(id));
  if (s.error) { sysMsg('Сессия: ' + s.error); return; }
  const ops = (s.ops || []).map((o) => ({
    name: o.name, args: o.args,
    describe: opDescribeFallback(o),
  }));
  pending = { text: s.replyText, ops, errs: s.errs || [] };
  addFeedText('sys', 'сессия', 'Открыта сессия: «' + (s.title || id) + '» — можно применить операции ещё раз');
  showPending(pending);
  setSide('files');
}

// describe для операций из сессии (сервер them хранит без describe)
function opDescribeFallback(op) {
  const p = (op.args && (op.args.path || op.args.to || op.args.from)) || '';
  switch (op.name) {
    case 'write_file': return 'записать файл ' + p;
    case 'edit_file': return 'правка в ' + p + ` («${(op.args.old_string || '').slice(0, 40)}…»)`;
    case 'append_file': return 'дописать в ' + p;
    case 'insert_lines': return 'вставить строки в ' + p;
    case 'replace_lines': return 'заменить строки ' + p;
    case 'make_dir': return 'создать папку ' + p;
    case 'delete_path': return 'удалить ' + p;
    case 'copy_file': return 'копировать ' + (op.args.from || '') + ' → ' + (op.args.to || '');
    case 'move_file': return 'переместить ' + (op.args.from || '') + ' → ' + (op.args.to || '');
    default: return op.name + ' ' + p;
  }
}

// ------------------------- boot -------------------------
(async function boot() {
  bind();
  const ok = await loadMonaco();
  if (!ok) makeFallback();
  pollState();
  pollSnaps();
  pollSessions();
  pollLog();
  pollEventsSmart();
  setInterval(pollState, 2000);
  setInterval(pollReply, 2200);
  setInterval(pollLog, 1500);
  setInterval(pollEventsSmart, 2600);
  setInterval(pollSnaps, 12000);
  setInterval(pollSessions, 5000);
  sysMsg('Добро пожаловать в DeepSeekIDE! Откройте папку проекта слева, затем нажмите «Подключить чат».');
})();
