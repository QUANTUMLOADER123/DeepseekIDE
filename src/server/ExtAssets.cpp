// ExtAssets — исходники браузерного расширения DeepSeekIDE.
//
// ВАЖНО: JS живёт здесь raw-строками. Править аккуратно: внутри нельзя
// последовательности )DS" (делимитер). Весь JS плотно прокомментирован.

#include "server/ExtAssets.h"

#include <fstream>
#include <vector>

namespace {

const char* kManifest = R"DS({
  "manifest_version": 3,
  "name": "DeepSeekIDE Autopilot",
  "version": "0.1.0",
  "description": "Автопилот DeepSeekIDE: применяет правки из ответов DeepSeek прямо в ваш проект.",
  "icons": {},
  "permissions": ["storage"],
  "host_permissions": ["http://127.0.0.1/*", "https://chat.deepseek.com/*"],
  "background": { "service_worker": "sw.js" },
  "content_scripts": [
    {
      "matches": ["https://chat.deepseek.com/*"],
      "js": ["config.js", "content.js"],
      "run_at": "document_idle",
      "all_frames": false
    }
  ]
}
)DS";

// sw.js — единственная точка выхода в локальный API: у service worker'а
// host_permissions http://127.0.0.1/* снимают CORS-ограничения целиком,
// страница chat.deepseek.com к localhost напрямую не обращается.
const char* kSw = R"DS(/* DeepSeekIDE Autopilot — service worker (мост к локальному серверу). */
importScripts('config.js');

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || msg.kind !== 'api') return false;
  (async () => {
    try {
      const init = {
        method: msg.method || 'GET',
        headers: { 'X-DeepSeekIDE-Token': self.__DSIDE.token }
      };
      if (msg.body !== undefined) {
        init.headers['Content-Type'] = 'application/json';
        init.body = JSON.stringify(msg.body);
      }
      const r = await fetch('http://127.0.0.1:' + self.__DSIDE.port + msg.path, init);
      const text = await r.text();
      let data;
      try { data = JSON.parse(text); } catch (e) { data = { raw: text }; }
      sendResponse({ ok: r.ok, status: r.status, data });
    } catch (e) {
      sendResponse({ ok: false, error: String(e && e.message ? e.message : e) });
    }
  })();
  return true; // асинхронный ответ — держим канал открытым
});
)DS";

// content.js — автопилот и виджет. Держится на эвристиках DOM, поэтому:
//  - каждый важный шаг логируется на сервер (видно в журнале IDE / issue);
//  - селекторы собраны в одном месте (SEL), чтобы править за 5 секунд.
const char* kContent = R"DS(/* DeepSeekIDE Autopilot — content script (запускается на chat.deepseek.com). */
(() => {
  if (window.__dsideInjected) return;
  window.__dsideInjected = true;

  // ------------------------------- мост к серверу
  function api(method, path, body) {
    return new Promise((resolve) => {
      try {
        chrome.runtime.sendMessage({ kind: 'api', method, path, body }, (r) => {
          if (chrome.runtime.lastError) return resolve({ ok: false, error: chrome.runtime.lastError.message });
          resolve(r || { ok: false, error: 'no response' });
        });
      } catch (e) { resolve({ ok: false, error: String(e) }); }
    });
  }
  function slog(level, msg) {           // журнал расширения -> сервер IDE
    api('POST', '/api/ext/log', { level, msg: String(msg).slice(0, 500) });
  }
  slog('info', 'DeepSeekIDE Autopilot: content-script активен (v0.1.0)');

  // ------------------------------- эвристики DOM (правим здесь)
  const SEL = {
    // markdown-тело ответа ассистента (кандидаты по убыванию приоритета)
    answer: ['div[class*="ds-markdown"]', 'div[class*="markdown"]', '[data-testid*="markdown"]'],
    editor: ['textarea[placeholder]', 'textarea', 'div[contenteditable="true"]'],
    codePre: 'pre'
  };
  function qOne(list, root) {
    for (const sel of list) { const el = (root || document).querySelector(sel); if (el) return el; }
    return null;
  }
  function qAll(list, root) {
    const out = [];
    for (const sel of list) out.push(...(root || document).querySelectorAll(sel));
    return out;
  }

  // ------------------------------- состояние автопилота
  const st = {
    autopilot: true,       // при false — следим, но ничего не шлём в чат
    lastAnswerText: '',    // что уже обработали (анти-дубль)
    lastChangeMs: 0,
    pendingInject: 0,      // защита от повторной вставки, пока шлётся предыдущая
    autoCount: 0,          // авто-сообщений подряд (потолок 12)
    lastUserMs: 0,         // когда человек слал сообщение сам
    statusText: 'ожидание',
    log: []
  };

  function miniLog(line) {
    st.log.unshift(line);
    if (st.log.length > 3) st.log.length = 3;
    renderWidget();
  }

  // ------------------------------- виджет (стеклянная таблетка)
  const w = document.createElement('div');
  w.id = 'dside-widget';
  const style = document.createElement('style');
  style.textContent = `
#dside-widget{position:fixed;right:16px;bottom:16px;z-index:2147483000;font-family:ui-sans-serif,system-ui,"Segoe UI",Roboto,sans-serif;
  background:rgba(17,24,39,.72);backdrop-filter:blur(14px) saturate(140%);-webkit-backdrop-filter:blur(14px) saturate(140%);
  border:1px solid rgba(255,255,255,.08);border-radius:14px;color:#e5e9f0;padding:10px 12px;min-width:230px;max-width:320px;
  box-shadow:0 12px 32px rgba(0,0,0,.45);transition:transform .18s ease}
#dside-widget:hover{transform:translateY(-2px)}
#dside-widget .hd{display:flex;align-items:center;gap:8px;font-size:12.5px;font-weight:600;letter-spacing:.2px}
#dside-widget .dot{width:8px;height:8px;border-radius:50%;background:#8b93a7;transition:background .2s}
#dside-widget .dot.on{background:#35d49a;box-shadow:0 0 8px #35d49a88}
#dside-widget .dot.work{background:#4d6bfe;box-shadow:0 0 8px #4d6bfeaa;animation:ds-pulse 1.1s infinite}
@keyframes ds-pulse{50%{opacity:.45}}
#dside-widget .st{font-size:11.5px;color:#a7b0c2;margin-top:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#dside-widget .lg{font-size:10.5px;color:#7c869c;margin-top:6px;line-height:1.45;display:none}
#dside-widget:hover .lg{display:block}
#dside-widget .btns{display:flex;gap:6px;margin-top:8px}
#dside-widget button{font:inherit;font-size:11px;color:#cfe0ff;background:rgba(77,107,254,.16);border:1px solid rgba(77,107,254,.4);
  border-radius:8px;padding:4px 8px;cursor:pointer;transition:background .15s}
#dside-widget button:hover{background:rgba(77,107,254,.32)}
#dside-widget button.warn{color:#ffd7d7;border-color:rgba(255,120,120,.45);background:rgba(255,90,90,.12)}
  `;
  document.documentElement.appendChild(style);
  document.documentElement.appendChild(w);

  function renderWidget() {
    w.innerHTML = '';
    const hd = document.createElement('div'); hd.className = 'hd';
    const dot = document.createElement('span');
    dot.className = 'dot ' + (st.autopilot ? (st.statusText === 'работаю…' ? 'work' : 'on') : '');
    hd.appendChild(dot);
    const name = document.createElement('span'); name.textContent = 'DeepSeekIDE · автопилот';
    hd.appendChild(name); w.appendChild(hd);
    const stl = document.createElement('div'); stl.className = 'st';
    stl.textContent = st.statusText + (st.autoCount ? ' · авто ' + st.autoCount : '');
    w.appendChild(stl);
    if (st.log.length) {
      const lg = document.createElement('div'); lg.className = 'lg'; lg.textContent = st.log.join('\n');
      w.appendChild(lg);
    }
    const btns = document.createElement('div'); btns.className = 'btns';
    const bPrime = mkBtn('⟡ Прайм', () => prime());
    const bAuto = mkBtn(st.autopilot ? '⏸ Пауза' : '▶ Пуск', async () => {
      st.autopilot = !st.autopilot;
      await api('POST', '/api/ext/autopilot', { enabled: st.autopilot });
      renderWidget();
      miniLog(st.autopilot ? 'автопилот включён' : 'автопилот на паузе');
    });
    const bIde = mkBtn('IDE ↗', async () => {
      const r = await api('GET', '/api/ext/home'); window.open((r.data && r.data.url) || '/', '_blank');
    });
    btns.append(bPrime, bAuto, bIde); w.appendChild(btns);
    function mkBtn(t, fn) { const b = document.createElement('button'); b.textContent = t; b.onclick = fn; return b; }
  }
  renderWidget();

  // ------------------------------- ввод в редактор чата
  function findEditor() { return qOne(SEL.editor) || qOne(SEL.editor.slice(1)); }
  function setEditorText(el, text) {
    el.focus();
    if (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT') {
      const proto = el.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
      Object.getOwnPropertyDescriptor(proto, 'value').set.call(el, text);
      el.dispatchEvent(new Event('input', { bubbles: true }));
    } else {
      el.textContent = text;
      el.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText' }));
    }
  }
  function clickSend(el) {
    // 1) кнопка рядом с редактором: enabled-кнопка в общем ближайшем контейнере
    let box = el;
    for (let i = 0; i < 6 && box; ++i) {
      const btn = box.querySelector('button:not([disabled])');
      if (btn && btn.offsetParent !== null) { btn.click(); return true; }
      box = box.parentElement;
    }
    // 2) запасной план: Enter
    for (const type of ['keydown', 'keypress', 'keyup']) {
      el.dispatchEvent(new KeyboardEvent(type, { key: 'Enter', code: 'Enter', bubbles: true }));
    }
    return true;
  }
  async function injectMessage(text, reason) {
    const el = findEditor();
    if (!el) { miniLog('не нашёл поле ввода чата!'); slog('warn', 'editor not found'); return false; }
    setEditorText(el, text);
    await new Promise((r) => setTimeout(r, 250));
    clickSend(el);
    miniLog('в чат отправлено: ' + reason);
    slog('info', 'inject: ' + reason + ' (' + text.length + ' chars)');
    return true;
  }

  async function prime() {
    miniLog('запрашиваю промпт…');
    const r = await api('GET', '/api/ext/prompt');
    if (!r.ok || !r.data || !r.data.prompt) { miniLog('сервер не дал промпт'); return; }
    if (await injectMessage(r.data.prompt, 'системный промпт')) miniLog('промпт отправлен ✓');
  }

  // ------------------------------- чтение ответа
  function lastAnswerBlock() {
    const els = qAll(SEL.answer);
    return els.length ? els[els.length - 1] : null;
  }
  function answerPayload(block) {
    // obs: тела code-блоков, у которых заголовок/контейнер говорит «deepseekide-ops».
    const obs = [];
    for (const pre of block.querySelectorAll(SEL.codePre)) {
      let label = '';
      let p = pre.parentElement;
      for (let i = 0; i < 4 && p && p !== block; ++i) {
        label = (p.textContent || '').slice(0, 80);
        if (label.toLowerCase().includes('deepseekide-ops')) {
          obs.push(pre.innerText || pre.textContent || '');
          break;
        }
        p = p.parentElement;
      }
    }
    return { text: block.innerText || block.textContent || '', obs };
  }

  let observerBusy = false;
  async function settleAndHandle() {
    if (observerBusy || !st.autopilot || st.pendingInject > 0) return;
    const block = lastAnswerBlock();
    if (!block) return;
    const { text, obs } = answerPayload(block);
    const trimmed = (text || '').trim();
    if (trimmed === st.lastAnswerText || trimmed.length < 3) return;
    observerBusy = true;
    try {
      st.lastAnswerText = trimmed;
      st.statusText = 'работаю…';
      renderWidget();
      const r = await api('POST', '/api/ext/answer', { text: trimmed, obs });
      if (!r.ok) {
        miniLog('сервер недоступен: ' + (r.error || r.status));
        st.statusText = 'IDE не отвечает';
      } else {
        const d = r.data || {};
        const rep = (d.report || '').trim();
        if (d.ops) miniLog('операций: ' + d.applied + '/' + d.ops + (rep ? ' ✓' : ''));
        if (d.note && d.note.length > 0) {
          if (st.autoCount >= 12) {
            miniLog('потолок авто-сообщений (12) — жду человека');
            st.statusText = 'нужен ввод человека';
          } else {
            st.pendingInject++;
            try { if (await injectMessage(d.note, 'SYSTEM-note')) st.autoCount++; } finally { st.pendingInject--; }
          }
        } else {
          st.statusText = 'готово';
        }
      }
    } finally {
      observerBusy = false;
      renderWidget();
    }
  }

  // ------------------------------- наблюдатель: тишина 1.6с по странице
  let debounce = 0;
  const mo = new MutationObserver(() => {
    st.lastChangeMs = Date.now();
    clearTimeout(debounce);
    debounce = setTimeout(settleAndHandle, 1600);
  });
  mo.observe(document.body, { childList: true, subtree: true, characterData: true });

  // ------------------------------- ручной ввод человека: сбрасываем счётчик и onServer-сеты
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { st.lastUserMs = Date.now(); st.autoCount = 0; api('POST', '/api/ext/reset'); }
  }, true);
  document.addEventListener('click', (e) => {
    const t = e.target;
    if (t && t.closest && t.closest('button')) { st.autoCount = 0; }
  }, true);

  // ------------------------------- пульс: статус и конфиг с сервера
  setInterval(async () => {
    const r = await api('GET', '/api/ext/state');
    if (r.ok && r.data) {
      st.autopilot = !!r.data.autopilot;
      if (st.statusText !== 'работаю…') st.statusText = st.autopilot ? st.statusText === 'IDE не отвечает' ? 'ожидание' : st.statusText : 'пауза';
    } else {
      st.statusText = 'IDE не отвечает';
    }
    renderWidget();
  }, 5000);
})();
)DS";

bool WriteOne(const std::filesystem::path& dir, const std::string& name, const std::string& body,
              std::string& err) {
  std::ofstream f(dir / name, std::ios::binary | std::ios::trunc);
  if (!f) {
    err = "не могу создать " + name;
    return false;
  }
  f << body;
  return true;
}

}  // namespace

bool WriteExtensionAssets(const std::filesystem::path& dir, int port, const std::string& token,
                          std::string& err) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    err = "create_directories: " + ec.message();
    return false;
  }
  if (!WriteOne(dir, "manifest.json", kManifest, err)) return false;
  if (!WriteOne(dir, "sw.js", kSw, err)) return false;
  if (!WriteOne(dir, "content.js", kContent, err)) return false;
  const std::string config = "window.__DSIDE = { port: " + std::to_string(port) + ", token: " +
                             "\"" + token + "\" };\n";
  if (!WriteOne(dir, "config.js", config, err)) return false;
  return true;
}
