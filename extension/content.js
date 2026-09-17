/* =====================================================================
 * content.js — DeepSeek Extended (isolated world): СТЕЛС-автопилот.
 * Никакого своего UI, кроме родной пилюли «Авто-пилот» рядом с тумблерами
 * сайта. Промпт незаметно подмешивается в начало сообщения пользователя,
 * служебные сообщения автопилота скрываются под чипы «Прочитано: "x.cpp"».
 * ===================================================================== */
(function () {
  'use strict';
  if (window.__dsxContent) return; window.__dsxContent = true;

  // ---------------- внедрение MAIN-world скриптов (File System Access мост)
  function inject(tag, attrs) {
    var el = document.createElement(tag);
    for (var k in attrs) el[k] = attrs[k];
    document.documentElement.appendChild(el);
    return el;
  }
  var url = chrome.runtime.getURL;
  inject('script', { src: url('opp.js') });
  inject('script', { src: url('fsbridge.js') });

  // ---------------- стелс-маркеры служебного контента
  // Модель видит всё; пользователь — только то, что после ⟦/DSX⟧ (или чип).
  var MARK = '⟦DSX⟧', END = '⟦/DSX⟧';

  // ---------------- RPC к MAIN-мосту
  var fsSeq = 0, fsWait = {};
  window.addEventListener('message', function (ev) {
    var m = ev.data;
    if (!m) return;
    if (m.__dsidefs === 'resp' && fsWait[m.id]) { fsWait[m.id](m); delete fsWait[m.id]; }
    if (m.__dsidefs === 'event') onBridgeEvent(m);
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

  // ---------------- настройки (только тумблер автопилота)
  var cfg = { pilot: false };
  try {
    chrome.storage.local.get(['pilot', 'v'], function (v) {
      if (v.v !== 18) {
        cfg.pilot = false;                       // свежая установка/обнова: выключен
        try { chrome.storage.local.set({ pilot: false, v: 18 }); } catch (e) {}
      } else if (v.pilot !== undefined) {
        cfg.pilot = v.pilot;
      }
      syncNativeToggle();
    });
  } catch (e) { /* дефолты */ }
  function saveCfg() { try { chrome.storage.local.set(cfg); } catch (e) {} }

  // ---------------- состояние
  var st = {
    folderReady: false, folderName: '',
    bigPrompt: '', primedOnce: false,
    lastAnswer: '', autoCount: Number(sessionStorage.getItem('dsx:auto') || 0),
    sentFiles: {}, sentSearches: {}, busy: false,
    lastUrl: location.href, lastTrimAt: 0, watchdog: 0
  };

  // ---------------- микро-тосты (в стиле сайта, сами исчезают)
  var toastBox = null;
  function toast(text) {
    if (!toastBox) {
      toastBox = document.createElement('div');
      toastBox.style.cssText =
        'position:fixed;right:16px;bottom:16px;z-index:8990;display:flex;' +
        'flex-direction:column;gap:8px;align-items:flex-end;pointer-events:none';
      document.body.appendChild(toastBox);
    }
    var t = document.createElement('div');
    t.textContent = text;
    t.style.cssText =
      'max-width:340px;padding:9px 15px;border-radius:12px;font-size:13px;line-height:1.4;' +
      'background:var(--dsw-alias-bg-layer-1,#2b2d31);color:var(--dsw-alias-label-primary,#f5f5f5);' +
      'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
      'box-shadow:var(--dsw-shadow-lv2,0 6px 24px rgba(0,0,0,.3));font-family:var(--dsw-font-family,inherit);' +
      'opacity:0;transform:translateY(8px);transition:all .28s var(--ds-ease-out,ease-out)';
    toastBox.appendChild(t);
    requestAnimationFrame(function () { t.style.opacity = '1'; t.style.transform = 'translateY(0)'; });
    setTimeout(function () {
      t.style.opacity = '0'; t.style.transform = 'translateY(8px)';
      setTimeout(function () { t.remove(); }, 320);
    }, 2600);
  }

  // ---------------- модалка «Подключение автопилота» (стиль ds-modal сайта)
  var dsxModal = null;
  function closeFolderModal() {
    if (dsxModal) { dsxModal.remove(); dsxModal = null; }
  }
  function showFolderModal() {
    closeFolderModal();
    var ov = document.createElement('div');
    ov.className = 'dsx-modal-ov';
    ov.style.cssText = 'position:fixed;inset:0;z-index:1030;background:rgba(0,0,0,.6);' +
      'display:flex;align-items:center;justify-content:center;opacity:0;transition:opacity .22s';
    ov.innerHTML =
      '<div class="ds-elevated" style="width:420px;max-width:calc(100vw - 48px);border-radius:16px;' +
      'background:var(--dsw-alias-bg-layer-1,#2b2d31);border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
      'box-shadow:var(--dsw-shadow-lv3,0 16px 48px rgba(0,0,0,.45));font-family:var(--dsw-font-family,inherit);' +
      'color:var(--dsw-alias-label-primary,#f5f5f5);transform:scale(.94);transition:transform .22s var(--ds-ease-out,ease-out)">' +
        '<div style="display:flex;align-items:center;justify-content:space-between;padding:18px 20px 4px">' +
          '<div style="font-size:17px;font-weight:600">🛩 Авто-пилот</div>' +
          '<div class="dsx-modal-x" role="button" tabindex="0" style="cursor:pointer;padding:6px;border-radius:8px;' +
          'color:var(--dsw-alias-label-secondary,#aaa)" title="Закрыть">✕</div>' +
        '</div>' +
        '<div style="padding:8px 20px 0;font-size:13.5px;line-height:1.55;color:var(--dsw-alias-label-secondary,#aaa)">' +
          'DeepSeek станет агентом твоего проекта: правки будут падать прямо в файлы, ' +
          'а контекст подмешиваться скрытно. Выбери корневую папку проекта — ' +
          'она запомнится, потом всё само.' +
        '</div>' +
        '<div class="dsx-modal-cur" style="display:none;padding:12px 20px 0;font-size:13px;' +
        'color:var(--dsw-alias-label-tertiary,#888)"></div>' +
        '<div style="display:flex;gap:10px;padding:18px 20px 20px">' +
          '<button class="dsx-modal-pick" style="flex:1;padding:11px 16px;border:none;cursor:pointer;' +
          'border-radius:12px;font-size:14px;font-weight:600;font-family:inherit;color:#fff;' +
          'background:var(--dsw-alias-brand-primary,#5686fe);transition:filter .15s">📂 Выбрать папку проекта</button>' +
          '<button class="dsx-modal-no" style="padding:11px 16px;cursor:pointer;border-radius:12px;font-size:14px;' +
          'font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
          'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
          'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">Позже</button>' +
        '</div>' +
      '</div>';
    document.body.appendChild(ov);
    dsxModal = ov;
    requestAnimationFrame(function () {
      ov.style.opacity = '1';
      ov.firstElementChild.style.transform = 'scale(1)';
    });
    var cur = ov.querySelector('.dsx-modal-cur');
    if (st.folderName) {
      cur.style.display = 'block';
      cur.textContent = 'Сейчас запомнено: 📂 ' + st.folderName;
    }
    ov.querySelector('.dsx-modal-x').onclick = closeFolderModal;
    ov.querySelector('.dsx-modal-no').onclick = closeFolderModal;
    ov.addEventListener('mousedown', function (e) { if (e.target === ov) closeFolderModal(); });
    // «Выбрать папку» перехватит fsbridge в MAIN-мире (там живой жест) — см. bindGesture.
  }

  // ---------------- события моста (папка выбрана жестом в MAIN-мире)
  function onBridgeEvent(m) {
    if (m.ev === 'folder') {
      st.folderReady = true; st.folderName = m.name || '';
      closeFolderModal();
      syncNativeToggle();
      toast('📂 Проект подключён: ' + st.folderName);
      refreshPrompt();
    } else if (m.ev === 'folderError') {
      var err = String(m.error || '');
      if (err.indexOf('AbortError') < 0 && err.toLowerCase().indexOf('abort') < 0)
        toast('Не вышло открыть папку: ' + err);
    }
  }

  // ---------------- сборка промптов
  function bigPrompt(tree) {
    return '[Автоматическое системное сообщение DeepSeek Extended. Этот блок — служебный ' +
      'контекст расширения: всегда учитывай его и НИКОГДА не упоминай и не цитируй ' +
      'в ответах пользователю.]\n\n' +
      DsideOps.buildPrompt(tree);
  }
  function shortPrefix() {
    return '[DeepSeek Extended активен для проекта «' + st.folderName + '». Напоминание: ' +
      'все изменения файлов — СТРОГО блоками ```deepseekide-ops (строгий JSON); ' +
      'недостающий контекст запрашивай отдельными строками NEED FILE: <путь> и ' +
      'NEED SEARCH: <подстрока>. Эту служебную вставку в ответе не упоминай.]';
  }
  async function refreshPrompt() {
    var en = await fsCall('ensure');
    if (!(en.ok && en.data.ok)) { st.folderReady = false; syncNativeToggle(); return; }
    st.folderReady = true; st.folderName = en.data.name;
    var t = await fsCall('tree', { max_entries: 350 });
    if (t.ok) st.bigPrompt = bigPrompt(t.data.text);
    else if (!st.bigPrompt) st.bigPrompt = bigPrompt('');
    console.log('[DSX] refresh: папка="' + st.folderName + '", промпт=' +
      (st.bigPrompt ? st.bigPrompt.length + ' символов' : 'НЕТ') + (t.ok ? '' : ' (дерево не прочиталось)'));
    syncNativeToggle();
  }

  // ---------------- обёртка служебного сообщения
  function wrapStealth(real, chip, visibleTail) {
    return MARK + '\nCHIP: ' + chip + '\n' + real + '\n' + END +
           (visibleTail ? '\n' + visibleTail : '');
  }

  // ---------------- СТЕЛС-ПОДМЕШИВАНИЕ в сообщение пользователя
  function findEditor() {
    return document.querySelector('textarea[placeholder*="DeepSeek" i]') ||
           document.querySelector('textarea');
  }
  function nativeSetValue(el, text) {
    Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value').set.call(el, text);
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }
  // Синхронно переписываем textarea ДО того, как сайт прочтёт значение
  // (capture-фаза Enter/клика): пользователь не видит промпт вообще.
  var lastFolderToast = 0;
  function tryStealthPrefix(editor) {
    var v = editor ? editor.value : '';
    if (!v || !v.trim()) return;
    if (v.indexOf(MARK) === 0) return; // это наше собственное служебное сообщение
    if (!cfg.pilot) { console.log('[DSX] пропуск: автопилот выкл'); return; }
    if (!st.folderReady) {
      // Chrome после перезапуска требует ЖЕСТ для возврата доступа к папке —
      // подскажем, что делать, и попробуем тихо восстановить в фоне.
      if (Date.now() - lastFolderToast > 15000) {
        lastFolderToast = Date.now();
        toast('📂 Кликни по «🛩 Авто-пилот» — нужен доступ к папке проекта');
      }
      console.log('[DSX] пропуск: папка не подключена (folderReady=false)');
      fsCall('ensure').then(function (en) {
        if (en.ok && en.data.ok) refreshPrompt();
      });
      return;
    }
    var prefix, chip;
    if (st.primedOnce) { prefix = shortPrefix(); chip = '🛩 Автопилот'; }
    else if (st.bigPrompt) { prefix = st.bigPrompt; chip = '🛩 Контекст проекта загружен'; }
    else { prefix = shortPrefix(); chip = '🛩 Автопилот'; }
    nativeSetValue(editor, wrapStealth(prefix, chip, v.replace(/^\s+/, '')));
    if (!st.primedOnce && st.bigPrompt) {
      st.primedOnce = true; sessionStorage.setItem('dsx:primed', '1');
      toast('🛩 Контекст проекта подшит к первому сообщению');
    }
    st.autoCount = 0; st.sentFiles = {}; st.sentSearches = {};
    sessionStorage.setItem('dsx:auto', '0');
    console.log('[DSX] промпт подшит к сообщению:', (prefix === st.bigPrompt ? 'БОЛЬШОЙ' : 'короткий'),
      '| итого символов:', editor.value.length);
    // сторожок: если через 2с ни один пузырь не обрезался — сайт мог уйти от этих классов
    clearTimeout(st.watchdog);
    st.watchdog = setTimeout(function () {
      if (Date.now() - st.lastTrimAt > 1900)
        console.warn('[DSX] ВНИМАНИЕ: пузырь с промптом не найден — возможно, сайт сменил вёрстку. Скинь этот лог разработчику.');
    }, 2000);
  }
  document.addEventListener('keydown', function (e) {
    if (e.__dsxBypass || e.key !== 'Enter' || e.shiftKey || e.isComposing) return;
    var t = e.target;
    if (!t || t.tagName !== 'TEXTAREA') return;
    tryStealthPrefix(t);
  }, true);
  document.addEventListener('click', function (e) {
    if (e.__dsxBypass || !e.target || !e.target.closest) return;
    var btn = e.target.closest('._52c986b') ||
              (e.target.closest('.bf38813a') && e.target.closest('button,[role="button"]'));
    if (!btn) return;
    var ed = findEditor();
    if (ed) tryStealthPrefix(ed);
  }, true);

  // ---------------- БРИТВА: прячем служебный текст из отрисованных пузырей
  function trimStealthNodes() {
    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    var hits = [], n;
    while ((n = walker.nextNode())) {
      if (n.nodeValue && n.nodeValue.indexOf(MARK) >= 0) hits.push(n);
    }
    hits.forEach(function (tn) {
      var el = tn.parentElement, hops = 0;
      while (el && hops < 8) {
        if (el.textContent.indexOf(END) >= 0) break;
        el = el.parentElement; hops++;
      }
      if (!el) return;
      var full = el.textContent;
      var i = full.indexOf(MARK), j = full.lastIndexOf(END);
      if (i < 0 || j < 0 || j <= i) return;
      var tail = full.slice(j + END.length).replace(/^\s+/, '');
      var chip = (full.slice(i, j).match(/CHIP:([^\n]*)/) || [])[1] || 'служебное';
      if (tail) {
        // сообщение пользователя с подмешанным промптом — виден только его текст
        el.textContent = tail;
      } else {
        // чисто служебное сообщение — скрываем его пузырь ЦЕЛИКОМ
        var box = el, hp = 0;
        while (box && hp < 12 &&
               !(box.hasAttribute && box.hasAttribute('data-virtual-list-item-key')) &&
               !(box.className && String(box.className).indexOf('_9663006') >= 0)) {
          box = box.parentElement; hp++;
        }
        (box || el).style.display = 'none';
      }
      st.lastTrimAt = Date.now();
      console.log('[DSX] скрыт служебный сегмент:', chip);
    });
  }

  // ---------------- ввод в чат (служебные сообщения автопилота)
  async function injectStealth(real, logTag) {
    var el = findEditor();
    if (!el) { toast('Не нашёл поле ввода чата'); return false; }
    el.focus();
    nativeSetValue(el, wrapStealth(real, logTag, ''));
    await new Promise(function (r) { setTimeout(r, 260); });
    var box = el, clicked = false;
    for (var i = 0; i < 6 && box && !clicked; ++i) {
      var btn = box.querySelector('button:not([disabled])');
      if (btn && btn.offsetParent !== null) {
        var ev = new MouseEvent('click', { bubbles: true });
        ev.__dsxBypass = true; btn.dispatchEvent(ev);
        clicked = true;
      } else box = box.parentElement;
    }
    if (!clicked) {
      ['keydown', 'keyup'].forEach(function (tp) {
        var ke = new KeyboardEvent(tp, { key: 'Enter', bubbles: true });
        ke.__dsxBypass = true; el.dispatchEvent(ke);
      });
    }
    console.log('[DSX] inject:', logTag, real.length, 'chars');
    return true;
  }

  // ---------------- чтение ответа DeepSeek
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
  function base(p) { var a = String(p).split('/'); return a[a.length - 1]; }
  async function serviceNote(raw) {
    var files = DsideOps.findFileRequests(raw), searches = DsideOps.findSearchRequests(raw);
    if (!files.length && !searches.length) return { text: '', rows: [] };
    var todoF = files.filter(function (p) { return !st.sentFiles[p]; }),
        todoS = searches.filter(function (q) { return !st.sentSearches[q]; });
    todoF.forEach(function (p) { st.sentFiles[p] = 1; });
    todoS.forEach(function (q) { st.sentSearches[q] = 1; });
    if (!todoF.length && !todoS.length) return { text: '', rows: [] };
    var out = ['SYSTEM: auto-reply from DeepSeek Extended.'];
    var rows = [], i;
    for (i = 0; i < Math.min(todoF.length, 3); ++i) {
      var p = todoF[i];
      out.push('You requested file content of «' + p + '» (lines are numbered; use those numbers with insert_lines/replace_lines).');
      var r = await fsCall('readNumbered', { path: p });
      if (r.ok) {
        var bodyTxt = (r.data.lines > 4000 ? '(первые 4000 строк)\n' : '') + r.data.text;
        if (bodyTxt.length > 120000) bodyTxt = bodyTxt.slice(0, 120000) + '\n…(truncated)';
        out.push('FILE "' + p + '"\n```\n' + bodyTxt + '\n```');
        rows.push('📖 Прочитано: ' + base(p));
      } else {
        out.push('FILE "' + p + '" — READ ERROR: ' + (r.error || 'не читается') +
                 '\n(используйте NEED SEARCH или попросите другое имя)');
        rows.push('📖 Не удалось прочитать: ' + base(p));
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
      rows.push('🔍 Поиск «' + q + '»' + (s.ok ? ': ' + s.data.matches + ' совпадений' : ' — ошибка'));
    }
    return { text: out.join('\n\n'), rows: rows };
  }

  // ---------------- главный цикл: ответ устоялся → применить → ответить
  function appendAssistantRows(block, rows) {
    if (!block || !block.isConnected) return;
    rows.forEach(function (t) {
      var p = document.createElement('p');
      p.className = 'dsx-row-line';
      p.textContent = t;
      p.style.cssText = 'margin:2px 0;font-size:13px;line-height:1.5;' +
        'color:var(--dsw-alias-label-tertiary,var(--dsw-alias-label-secondary,#999));' +
        'font-family:var(--dsw-font-family,inherit)';
      block.appendChild(p);
    });
  }
  async function settleAndHandle() {
    if (st.busy || !cfg.pilot || !st.folderReady) return;
    var block = lastAnswerBlock();
    if (!block) return;
    var pay = answerPayload(block);
    var text = (pay.text || '').trim();
    if (text === st.lastAnswer || text.length < 3) return;
    st.busy = true; st.lastAnswer = text;
    try {
      var outp = { ops: [], errors: [] };
      if (pay.obs.length) pay.obs.forEach(function (b) { DsideOps.parseOpsBody(b, outp); });
      else DsideOps.extractOps(text, outp);

      var note = '', rows = [];
      if (outp.ops.length) {
        outp.ops = outp.ops.filter(function (o) {
          var p = o.args && (o.args.path || o.args.from);
          return !p || !!DsideOps.sanitizeRel(p);
        });
        var r = await fsCall('mutateBatch', {
          label: 'DeepSeek Extended правки',
          ops: outp.ops.map(function (o) { return { name: o.name, args: o.args }; })
        });
        if (r.ok) {
          var sum = r.data.done === r.data.total
            ? '✅ Применено: ' + r.data.done + ' из ' + r.data.total + ' операций'
            : '⚠️ Применено ' + r.data.done + ' из ' + r.data.total + ' — есть ошибки';
          toast(sum);
          rows.push(sum);
          note = 'SYSTEM: ops applied automatically by DeepSeek Extended:\n\n```\n' +
                 r.data.report + '\n```\n';
          refreshPrompt(); // дерево изменилось
        } else {
          toast('Ошибка применения: ' + (r.error || '?'));
          rows.push('⚠️ Ошибка применения правок');
        }
      }
      var svc = await serviceNote(text);
      if (svc.text) {
        note = note ? note + '\n\n' + svc.text : svc.text;
        rows = svc.rows.concat(rows);
      }

      if (note) {
        if (st.autoCount >= 12) {
          toast('Потолок авто-сообщений (12) — ваш ход');
        } else if (await injectStealth(note, 'служебное')) {
          st.autoCount++;
          sessionStorage.setItem('dsx:auto', String(st.autoCount));
        }
      }
      if (rows.length) appendAssistantRows(block, rows);
      if (outp.errors.length) console.warn('[DSX] ops warnings:', outp.errors);
    } finally { st.busy = false; }
  }

  // ---------------- НАТИВНАЯ пилюля «Авто-пилот» (зеркалим разметку сайта)
  function findToggleHost() {
    var host = document.querySelector('._58b31c9');
    if (host) return host;
    var spans = document.querySelectorAll('.ds-toggle-button span');
    for (var i = 0; i < spans.length; ++i) {
      var t = spans[i].textContent || '';
      if (t.indexOf('Умный поиск') >= 0 || t.indexOf('Глубокое мышление') >= 0)
        return spans[i].closest('.ds-toggle-button').parentElement;
    }
    return null;
  }
  function syncNativeToggle(el) {
    el = el || document.querySelector('.dsx-pilot-tg');
    if (!el) return;
    var on = cfg.pilot;
    el.classList.toggle('ds-toggle-button--selected', on);
    el.setAttribute('aria-pressed', on ? 'true' : 'false');
    el.title = !on ? 'Авто-пилот выкл: обычный чат. Включи — подключишь папку проекта.'
      : st.folderReady
        ? 'Авто-пилот ВКЛ: проект «' + st.folderName + '». Промпт подмешивается незаметно.'
        : 'Авто-пилот ВКЛ — ожидает выбор папки проекта';
  }
  function ensureNativeToggle() {
    var host = findToggleHost();
    if (!host) return;
    var el = host.querySelector('.dsx-pilot-tg');
    if (!el) {
      el = document.createElement('div');
      el.className = 'dsx-native dsx-pilot-tg f79352dc ds-toggle-button ds-toggle-button--m';
      el.tabIndex = 0;
      el.setAttribute('role', 'button');
      el.style.cssText = 'transform:translateZ(0px);position:relative';
      el.innerHTML =
        '<div class="ds-toggle-button__icon"><div class="ds-icon" style="font-size:inherit">' +
        '<div style="width:14px;height:14px">' +
        '<svg width="14" height="14" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">' +
        '<path d="M8 1.6c.9 0 1.6.7 1.6 1.6 0 .5-.2.9-.6 1.2l2.2 4.4c.5-.3.6-.2.9-.1 1.7.9 1.9 3 .9 4.2-1 1.1-2.7 1-3.8-.1l-2.7-1.3-2.7 1.3c-1.1 1.1-2.8 1.2-3.8.1-1-1.2-.8-3.3.9-4.2.3-.1.5-.2.9.1L4.6 4.6c-.4-.3-.6-.7-.6-1.2C4 2.3 4.6 1.6 5.5 1.6c.7 0 1.3.4 1.5 1 .2-.1.5-.2 1-.2Z" fill="currentColor"/>' +
        '</svg></div></div></div>' +
        '<span class="_6dbc175">Авто-пилот</span>' +
        '<div class="ds-focus-ring" style="--dsl-focus-ring-offset:-1px"></div>';
      el.addEventListener('click', function () {
        if (!cfg.pilot) {
          cfg.pilot = true; saveCfg(); syncNativeToggle(el);
          if (!st.folderReady) showFolderModal();
        } else {
          cfg.pilot = false; saveCfg(); syncNativeToggle(el);
        }
      });
      el.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') el.click(); });
      host.appendChild(el);
    }
    syncNativeToggle(el);
  }
  setInterval(ensureNativeToggle, 1400);

  // ---------------- сторож URL (новая беседа = новый прайм)
  setInterval(function () {
    if (location.href !== st.lastUrl) {
      st.lastUrl = location.href;
      st.primedOnce = false; st.lastAnswer = '';
      st.sentFiles = {}; st.sentSearches = {};
      st.autoCount = 0; sessionStorage.setItem('dsx:auto', '0');
      sessionStorage.removeItem('dsx:primed');
    }
  }, 1500);

  // ---------------- наблюдатель: бритва + ответы
  var debounce = 0;
  new MutationObserver(function () {
    trimStealthNodes();
    clearTimeout(debounce);
    debounce = setTimeout(settleAndHandle, 1600);
  }).observe(document.body, { childList: true, subtree: true, characterData: true });
  setInterval(trimStealthNodes, 900);

  // ---------------- старт
  if (sessionStorage.getItem('dsx:primed') === '1') st.primedOnce = true;
  console.log('[DSX] DeepSeek Extended v17 стелс: загружено. pilot=' + cfg.pilot);
  refreshPrompt();
  setTimeout(refreshPrompt, 1200);
  setInterval(function () {
    if (!st.bigPrompt || !st.folderReady) refreshPrompt();
  }, 6000);
})();
