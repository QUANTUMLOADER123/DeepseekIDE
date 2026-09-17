/* =====================================================================
 * content.js — DeepSeek Extended (isolated world): автопилот чата.
 * Внедряет MAIN-world скрипты (opp/fsbridge/ui), ловит готовые ответы
 * DeepSeek, применяет ops в папку проекта через File System Access мост,
 * отвечает модели системными заметками, кормит UI событиями.
 * ===================================================================== */
(function () {
  'use strict';
  if (window.__dsxContent) return; window.__dsxContent = true;

  // ---------------- внедрение MAIN-world скриптов (File System Access + UI)
  function inject(tag, attrs, where) {
    var el = document.createElement(tag);
    for (var k in attrs) el[k] = attrs[k];
    (where || document.documentElement).appendChild(el);
    return el;
  }
  var url = chrome.runtime.getURL;
  inject('link', { rel: 'stylesheet', href: url('ui.css') });
  inject('script', { src: url('opp.js') });
  inject('script', { src: url('fsbridge.js') });
  inject('script', { src: url('ui.js') });

  // ---------------- RPC к MAIN-мосту
  var fsSeq = 0, fsWait = {};
  window.addEventListener('message', function (ev) {
    var m = ev.data;
    if (m && m.__dsidefs === 'resp' && fsWait[m.id]) {
      fsWait[m.id](m); delete fsWait[m.id];
    }
    if (m && m.__dsideui === 'ctl') onUiCtl(m.name, m.payload);
  });
  function fsCall(op, args) {
    return new Promise(function (res) {
      var id = ++fsSeq;
      fsWait[id] = res;
      window.postMessage({ __dsidefs: 'req', id: id, op: op, args: args }, '*');
      setTimeout(function () {
        if (fsWait[id]) { delete fsWait[id]; res({ ok: false, error: 'timeout' }); }
      }, 30000);
    });
  }
  function ui(name, payload) {
    window.postMessage({ __dsideui: 'cmd', name: name, payload: payload }, '*');
  }

  // ---------------- настройки
  var cfg = { pilot: true, note: true, anim: true };
  try {
    chrome.storage.local.get(['pilot', 'note', 'anim'], function (v) {
      if (v.pilot !== undefined) cfg.pilot = v.pilot;
      if (v.note !== undefined) cfg.note = v.note;
      if (v.anim !== undefined) cfg.anim = v.anim;
      ui('conf', cfg); ui('pilot', { on: cfg.pilot });
    });
  } catch (e) { /* storage недоступен — работаем на дефолтах */ }
  function saveCfg() {
    try { chrome.storage.local.set(cfg); } catch (e) {}
  }

  // ---------------- состояние автопилота
  var st = {
    lastAnswer: '', autoCount: Number(sessionStorage.getItem('dsx:auto') || 0),
    sentFiles: {}, sentSearches: {}, busy: false
  };
  function status(text, state) { ui('status', { text: text, state: state }); }

  // ---------------- UI → команды
  async function cmdPick() {
    var r = await fsCall('pick');
    if (r.ok) {
      ui('folder', { name: r.data.name });
      ui('folder2', { name: r.data.name });
      status('проект: ' + r.data.name, 'on');
      cmdFiles();
    } else {
      status('папку не выбрали', '');
    }
  }
  async function cmdFiles() {
    var en = await fsCall('ensure');
    if (en.ok && en.data.ok) {
      var t = await fsCall('tree', { max_entries: 420 });
      if (t.ok) ui('refreshFiles', { text: t.data.text });
      ui('folder', { name: en.data.name });
      ui('folder2', { name: en.data.name });
    } else {
      ui('refreshFiles', { text: '(папка проекта не выбрана)' });
    }
  }
  async function cmdPrime() {
    var en = await fsCall('ensure');
    if (!en.ok || !en.data.ok) { status('сначала выберите папку (Панель ▣ → Файлы)', ''); return; }
    var t = await fsCall('tree', { max_entries: 350 });
    var tree = t.ok ? t.data.text : '';
    if (await injectMessage(DsideOps.buildPrompt(tree), 'системный промпт'))
      status('промпт отправлен ✓', 'on');
  }
  async function cmdRollback(key) {
    var r = await fsCall('rollback', { key: key });
    if (r.ok) { ui('feed', { text: r.data.report }); cmdSnaps(); status('откат применён', 'on'); }
  }
  async function cmdSnaps() {
    var r = await fsCall('snapList');
    ui('refreshSnaps', { list: r.ok ? r.data.list : [] });
  }
  function onUiCtl(name, payload) {
    if (name === 'pick') cmdPick();
    else if (name === 'prime') cmdPrime();
    else if (name === 'pilot') {
      cfg.pilot = !cfg.pilot; saveCfg();
      ui('pilot', { on: cfg.pilot });
      status(cfg.pilot ? 'автопилот включён' : 'пауза автопилота', cfg.pilot ? 'on' : '');
    } else if (name === 'conf') { cfg = payload; saveCfg(); }
    else if (name === 'needFiles') cmdFiles();
    else if (name === 'needSnaps') cmdSnaps();
    else if (name === 'rollback') cmdRollback(payload);
    else if (name === 'preview') {
      fsCall('readNumbered', { path: payload, end_line: 120 }).then(function (r) {
        ui('preview', { text: r.ok ? r.data.text : 'не прочиталось: ' + r.error });
      });
    }
  }

  // ---------------- чтение ответа DeepSeek (эвристики — логи в консоль)
  var SEL = ['div[class*="ds-markdown"]', 'div[class*="markdown"]'];
  function lastAnswerBlock() {
    var els = [];
    SEL.forEach(function (s) { document.querySelectorAll(s).forEach(function (e) { els.push(e); }); });
    return els.length ? els[els.length - 1] : null;
  }
  function answerPayload(block) {
    var obs = [];
    block.querySelectorAll('pre').forEach(function (pre) {
      var p = pre.parentElement, hops = 0;
      while (p && p !== block && hops < 4) {
        var lab = (p.textContent || '').slice(0, 90).toLowerCase();
        if (lab.indexOf('deepseekide-ops') >= 0) { obs.push(pre.innerText || pre.textContent || ''); break; }
        p = p.parentElement; hops++;
      }
    });
    return { text: block.innerText || block.textContent || '', obs: obs };
  }

  // ---------------- самообслуживание NEED FILE / NEED SEARCH (порции 3+2)
  async function serviceNote(raw) {
    var files = DsideOps.findFileRequests(raw), searches = DsideOps.findSearchRequests(raw);
    if (!files.length && !searches.length) return '';
    var todoF = files.filter(function (p) { return !st.sentFiles[p]; }),
        todoS = searches.filter(function (q) { return !st.sentSearches[q]; });
    todoF.forEach(function (p) { st.sentFiles[p] = 1; });
    todoS.forEach(function (q) { st.sentSearches[q] = 1; });
    if (!todoF.length && !todoS.length) return '';
    var out = ['SYSTEM: auto-reply from DeepSeek Extended.'];
    var i;
    for (i = 0; i < Math.min(todoF.length, 3); ++i) {
      var p = todoF[i];
      out.push('You requested file content of «' + p + '» (lines are numbered; use those numbers with insert_lines/replace_lines).');
      var r = await fsCall('readNumbered', { path: p });
      if (r.ok) {
        var bodyTxt = (r.data.lines > 4000 ? '(первые 4000 строк)\n' : '') + r.data.text;
        if (bodyTxt.length > 120000) bodyTxt = bodyTxt.slice(0, 120000) + '\n…(truncated)';
        out.push('FILE "' + p + '"\n```\n' + bodyTxt + '\n```');
      } else {
        out.push('FILE "' + p + '" — READ ERROR: ' + (r.error || 'не читается') +
                 '\n(используйте NEED SEARCH или попросите другое имя)');
      }
    }
    if (todoF.length > 3) out.push('(more files pending — repeat NEED FILE for them to continue)');
    for (i = 0; i < Math.min(todoS.length, 2); ++i) {
      var q = todoS[i];
      out.push('You searched the project for «' + q + '». Matches (file:line: text):');
      var s = await fsCall('search', { query: q });
      var sb = s.ok ? s.data.text : ('ERROR: ' + (s.error || ''));
      if (sb.length > 60000) sb = sb.slice(0, 60000) + '\n…(truncated)';
      out.push('SEARCH "' + q + '"\n```\n' + sb + '\n```');
    }
    return out.join('\n\n');
  }

  // ---------------- главный цикл: ответ устоялся → применить → ответить
  async function settleAndHandle() {
    if (st.busy || !cfg.pilot) return;
    var block = lastAnswerBlock();
    if (!block) return;
    var pay = answerPayload(block);
    var text = (pay.text || '').trim();
    if (text === st.lastAnswer || text.length < 3) return;
    st.busy = true; st.lastAnswer = text;
    status('разбираю ответ…', 'work');
    try {
      var outp = { ops: [], errors: [] };
      if (pay.obs.length) pay.obs.forEach(function (b) { DsideOps.parseOpsBody(b, outp); });
      else DsideOps.extractOps(text, outp);

      var note = '';
      if (outp.ops.length) {
        // валидация путей до исполнения
        outp.ops = outp.ops.filter(function (o) {
          var p = o.args && (o.args.path || o.args.from);
          return !p || !!DsideOps.sanitizeRel(p);
        });
        status('применяю ' + outp.ops.length + ' операций…', 'work');
        var r = await fsCall('mutateBatch', {
          label: 'DeepSeek Extended правки',
          ops: outp.ops.map(function (o) { return { name: o.name, args: o.args }; })
        });
        if (r.ok) {
          ui('feed', { text: r.data.report });
          ui('row', { cls: r.data.done === r.data.total ? 'ok' : 'bad',
                      text: 'пакет: ' + r.data.done + '/' + r.data.total });
          cmdFiles(); cmdSnaps();
          if (cfg.note) {
            note = 'SYSTEM: ops applied automatically by DeepSeek Extended:\n\n```\n' +
                   r.data.report + '\n```\n';
          }
        } else {
          status('ошибка применения: ' + (r.error || '?'), '');
        }
      }
      var svc = await serviceNote(text);
      if (svc) note = note ? note + '\n\n' + svc : svc;

      if (typeof debugStrip === 'function') { /* noop */ }
      if (note) {
        if (st.autoCount >= 12) {
          status('потолок авто-сообщений (12) — ваш ход', '');
        } else if (await injectMessage(note, 'SYSTEM-note')) {
          st.autoCount++;
          sessionStorage.setItem('dsx:auto', String(st.autoCount));
          status('ответ-заметка отправлена', 'on');
        }
      } else {
        status(outp.ops.length ? 'готово ✓' : 'ожидание', outp.ops.length ? 'on' : 'on');
      }
      if (outp.errors.length) console.warn('[DSX] ops warnings:', outp.errors);
    } finally { st.busy = false; }
  }

  // ---------------- ввод в чат
  function findEditor() {
    return document.querySelector('textarea') ||
           document.querySelector('div[contenteditable="true"]');
  }
  async function injectMessage(text, reason) {
    var el = findEditor();
    if (!el) { status('не нашёл поле ввода чата', ''); return false; }
    el.focus();
    if (el.tagName === 'TEXTAREA') {
      Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value').set.call(el, text);
      el.dispatchEvent(new Event('input', { bubbles: true }));
    } else {
      el.textContent = text;
      el.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText' }));
    }
    await new Promise(function (r) { setTimeout(r, 260); });
    var box = el, clicked = false;
    for (var i = 0; i < 6 && box && !clicked; ++i) {
      var btn = box.querySelector('button:not([disabled])');
      if (btn && btn.offsetParent !== null) { btn.click(); clicked = true; }
      else box = box.parentElement;
    }
    if (!clicked) {
      ['keydown', 'keyup'].forEach(function (t) {
        el.dispatchEvent(new KeyboardEvent(t, { key: 'Enter', bubbles: true }));
      });
    }
    console.log('[DSX] inject:', reason, text.length, 'chars');
    return true;
  }

  // ---------------- наблюдатель
  var debounce = 0;
  new MutationObserver(function () {
    clearTimeout(debounce);
    debounce = setTimeout(settleAndHandle, 1600);
  }).observe(document.body, { childList: true, subtree: true, characterData: true });

  document.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' && !e.shiftKey) {
      st.autoCount = 0; st.sentFiles = {}; st.sentSearches = {};
      sessionStorage.setItem('dsx:auto', '0');
    }
  }, true);

  // ---------------- старт
  setTimeout(async function () {
    var en = await fsCall('ensure');
    if (en.ok && en.data.ok) {
      ui('folder', { name: en.data.name });
      ui('folder2', { name: en.data.name });
      status('проект: ' + en.data.name, 'on');
      cmdFiles(); cmdSnaps();
    } else {
      status('выберите папку проекта (Панель ▣ → Файлы)', '');
    }
  }, 800);
})();
