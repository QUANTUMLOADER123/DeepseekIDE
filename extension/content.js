/* =====================================================================
 * content.js — DeepSeek Extended (isolated world): СТЕЛС-автопилот v32.
 * Никакого своего UI, кроме: (1) родной пилюли «Авто-пилот» рядом с
 * тумблерами сайта, (2) шестерёнки настроек вплотную слева от скрепки
 * (icon-кнопка без аутлайна).
 * Промпт незаметно подмешивается в сообщение пользователя, служебные
 * сообщения скрываются, статусы («Прочитано», «Применено»)
 * дописываются тихими строками под ответ DeepSeek. БЕЗ эмодзи.
 *
 * ГЛАВНОЕ ПРАВИЛО ВЫЖИВАНИЯ: НИКОГДА не удалять/не заменять DOM-узлы,
 * созданные React (никаких el.textContent=..., remove(), innerHTML=...).
 * React при следующей отрисовке пытается оперировать своими узлами и,
 * не найдя их, роняет весь сайт в белый экран. Можно только:
 *   — менять nodeValue его текстовым узлам (in-place);
 *   — ставить style.display на контейнерах;
 *   — ДОБАВЛЯТЬ свои узлы в КОНЕЦ чужих контейнеров.
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
  // Модель видит всё; пользователь — только то, что после ⟦/DSX⟧.
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

  // ---------------- настройки (тумблер + правила + мелочи)
  var CFG_VERSION = 32;
  var cfg = { pilot: false, rules: '', statusRows: true, jbTest: false, jbText: '', memory: [] };
  try {
    chrome.storage.local.get(['pilot', 'rules', 'statusRows', 'jbTest', 'jbText', 'memory', 'v'], function (v) {
      if (v && v.v !== CFG_VERSION) {
        // свежая установка/обнова: пилот не трогаем (он и так выкл у новых),
        // дефолты новых полей — бережливо дописываем
        cfg.pilot = !!v.pilot;
        cfg.rules = typeof v.rules === 'string' ? v.rules : '';
        cfg.statusRows = v.statusRows !== false;
        cfg.jbTest = !!v.jbTest;
        cfg.jbText = typeof v.jbText === 'string' ? v.jbText : '';
        if (Array.isArray(v.memory)) cfg.memory = clampMemory(v.memory);
        var base = { v: CFG_VERSION, pilot: cfg.pilot, rules: cfg.rules, statusRows: cfg.statusRows, jbTest: cfg.jbTest, jbText: cfg.jbText, memory: cfg.memory };
        try { chrome.storage.local.set(base); } catch (e) {}
      } else if (v) {
        if (v.pilot !== undefined) cfg.pilot = !!v.pilot;
        if (typeof v.rules === 'string') cfg.rules = v.rules;
        if (v.statusRows !== undefined) cfg.statusRows = !!v.statusRows;
        if (v.jbTest !== undefined) cfg.jbTest = !!v.jbTest;
        if (typeof v.jbText === 'string') cfg.jbText = v.jbText;
        if (Array.isArray(v.memory)) cfg.memory = clampMemory(v.memory);
      }
      syncNativeToggle(); syncFolderLabels();
    });
  } catch (e) { /* дефолты */ }
  function clampMemory(arr) {
    var out = [];
    for (var i = 0; i < arr.length && out.length < 40; ++i) {
      var n = arr[i] || {};
      var t = String(n.t || '').slice(0, 120), x = String(n.x || '').slice(0, 12000);
      if (!t.trim() && !x.trim()) continue;
      out.push({ t: t, x: x });
    }
    return out;
  }
  function saveCfg() {
    try { chrome.storage.local.set({ v: CFG_VERSION, pilot: cfg.pilot, rules: cfg.rules, statusRows: cfg.statusRows, jbTest: cfg.jbTest, jbText: cfg.jbText, memory: cfg.memory }); } catch (e) {}
  }

  // ---- ops-дедуп v30: скипаем повтор ТОЛЬКО того же ответа (тот же блок/ключ),
  // а не «того же текста» — юзер вправе попросить повторить правку дословно
  function loadOpsMeta() {
    try {
      var r = sessionStorage.getItem('dsx:opsh');
      if (!r) return { h: '', k: '' };
      var d = JSON.parse(r);
      if (d && typeof d === 'object') return { h: String(d.h || ''), k: String(d.k || '') };
      return { h: String(d), k: '' }; // легаси-строка
    } catch (e) { return { h: '', k: '' }; }
  }
  // ---------------- состояние
  var st = {
    folderReady: false, folderName: '',
    bigPrompt: '', primedOnce: false,
    lastAnswer: '', lastBlock: null, opsMeta: loadOpsMeta(),
    autoCount: Number(sessionStorage.getItem('dsx:auto') || 0),
    sentFiles: {}, sentSearches: {}, sentInfos: {}, sentRanges: {}, sentWeb: {}, sentDl: {}, sentGrep: {}, busy: false,
    waitingAnswer: sessionStorage.getItem('dsx:wait') === '1',
    lastUrl: location.href, lastTrimAt: 0, watchdog: 0, reloadHint: 0
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

  // ---------------- каркас модалок (стиль ds-modal сайта)
  function overlayBase(width) {    var ov = document.createElement('div');
    ov.className = 'dsx-modal-ov';
    ov.style.cssText = 'position:fixed;inset:0;z-index:1030;background:rgba(0,0,0,.6);' +
      'display:flex;align-items:center;justify-content:center;opacity:0;transition:opacity .22s';
    var card = document.createElement('div');
    card.className = 'ds-elevated';
    card.style.cssText = 'width:' + width + 'px;max-width:calc(100vw - 48px);max-height:calc(100vh - 64px);' +
      'overflow:auto;border-radius:16px;' +
      'background:var(--dsw-alias-bg-layer-1,#2b2d31);border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
      'box-shadow:var(--dsw-shadow-lv3,0 16px 48px rgba(0,0,0,.45));font-family:var(--dsw-font-family,inherit);' +
      'color:var(--dsw-alias-label-primary,#f5f5f5);transform:scale(.94);' +
      'transition:transform .22s var(--ds-ease-out,ease-out)';
    ov.appendChild(card);
    document.body.appendChild(ov);
    requestAnimationFrame(function () { ov.style.opacity = '1'; card.style.transform = 'scale(1)'; });
    ov.addEventListener('mousedown', function (e) { if (e.target === ov) closeModals(); });
    return card;
  }
  function modalHeader(title) {
    return '<div style="display:flex;align-items:center;justify-content:space-between;padding:18px 20px 4px">' +
      '<div style="font-size:17px;font-weight:600">' + title + '</div>' +
      '<div class="dsx-modal-x" role="button" tabindex="0" style="cursor:pointer;padding:6px;border-radius:8px;' +
      'color:var(--dsw-alias-label-secondary,#aaa)" title="Закрыть">✕</div></div>';
  }
  function bindModalChrome(card) {
    card.querySelector('.dsx-modal-x').onclick = closeModals;
  }
  function closeModals() {
    document.querySelectorAll('.dsx-modal-ov').forEach(function (el) { el.remove(); });
  }
  function syncFolderLabels() {
    document.querySelectorAll('.dsx-cur-folder').forEach(function (el) {
      el.textContent = st.folderName ? st.folderName : '— папка не выбрана —';
    });
  }

  // ---------------- модалка «Подключение автопилота»
  function showFolderModal() {
    closeModals();
    var card = overlayBase(420);
    card.innerHTML =
      modalHeader('Авто-пилот') +
      '<div style="padding:8px 20px 0;font-size:13.5px;line-height:1.55;color:var(--dsw-alias-label-secondary,#aaa)">' +
        'DeepSeek станет агентом твоего проекта: правки будут падать прямо в файлы, ' +
        'а контекст подмешиваться скрытно. Выбери корневую папку проекта — ' +
        'она запомнится, потом всё само.' +
      '</div>' +
      '<div style="padding:12px 20px 0;font-size:13px;color:var(--dsw-alias-label-tertiary,#888)">' +
        'Сейчас: <span class="dsx-cur-folder"></span>' +
      '</div>' +
      '<div style="display:flex;gap:10px;padding:18px 20px 20px">' +
        '<button class="dsx-modal-pick" style="flex:1;padding:11px 16px;border:none;cursor:pointer;' +
        'border-radius:12px;font-size:14px;font-weight:600;font-family:inherit;color:#fff;' +
        'background:var(--dsw-alias-brand-primary,#5686fe);transition:filter .15s">Выбрать папку проекта</button>' +
        '<button class="dsx-modal-no" style="padding:11px 16px;cursor:pointer;border-radius:12px;font-size:14px;' +
        'font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
        'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">Позже</button>' +
      '</div>';
    bindModalChrome(card);
    card.querySelector('.dsx-modal-no').onclick = closeModals;
    syncFolderLabels();
    // «Выбрать папку» перехватит fsbridge в MAIN-мире (там живой жест) — см. bindGesture.
  }

  // ---------------- модалка НАСТРОЕК (шестерёнка)
  function showSettingsModal() {
    closeModals();
    var card = overlayBase(480);
    card.innerHTML =
      modalHeader('DeepSeek Extended — настройки') +
      // --- раздел: папка проекта
      '<div style="padding:14px 20px 0">' +
        '<div style="font-size:13px;font-weight:600;color:var(--dsw-alias-label-primary,#eee)">Папка проекта</div>' +
        '<div style="display:flex;align-items:center;gap:10px;margin-top:8px">' +
          '<div class="dsx-cur-folder" style="flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;' +
          'font-size:13px;color:var(--dsw-alias-label-secondary,#aaa)"></div>' +
          '<button class="dsx-modal-pick" style="padding:8px 14px;border:none;cursor:pointer;border-radius:10px;' +
          'font-size:13px;font-weight:600;font-family:inherit;color:#fff;flex-shrink:0;' +
          'background:var(--dsw-alias-brand-primary,#5686fe)">Сменить…</button>' +
          '<button class="dsx-set-forget" title="Убрать сохранённый доступ к папке" style="padding:8px 12px;' +
          'cursor:pointer;border-radius:10px;font-size:13px;font-family:inherit;flex-shrink:0;' +
          'color:var(--dsw-alias-label-primary,#eee);background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
          'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">Забыть</button>' +
        '</div>' +
      '</div>' +
      // --- раздел: начальный промпт
      '<div style="padding:18px 20px 0">' +
        '<div style="font-size:13px;font-weight:600;color:var(--dsw-alias-label-primary,#eee)">Начальный промпт (правила для ИИ)</div>' +
        '<div style="margin-top:4px;font-size:12px;line-height:1.5;color:var(--dsw-alias-label-tertiary,#888)">' +
          'Уходит скрытно с каждым сообщением при включённом автопилоте: стиль кода, язык, запреты. ' +
          'Например: «Отвечай коротко. Код — C++17, комментарии на русском, отступы 4 пробела».' +
        '</div>' +
        '<textarea class="dsx-set-rules" rows="5" placeholder="Твои правила для DeepSeek… (можно пусто)" ' +
        'style="width:100%;box-sizing:border-box;margin-top:8px;padding:10px 12px;resize:vertical;' +
        'border-radius:10px;font-size:13px;line-height:1.5;font-family:inherit;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));outline:none"></textarea>' +
      '</div>' +
      // --- раздел: ПАМЯТЬ (заметки для ИИ)
      '<div style="padding:18px 20px 0">' +
        '<div style="font-size:13px;font-weight:600;color:var(--dsw-alias-label-primary,#eee)">Память проекта (заметки для ИИ)</div>' +
        '<div style="margin-top:4px;font-size:12px;line-height:1.5;color:var(--dsw-alias-label-tertiary,#888)">' +
          'Загрузи знания: поля, структуры, протоколы, как устроен проект. Контекст не раздувается — ' +
          'модель сама заглядывает сюда маркером MEMORY SEARCH, когда чего-то не знает; в промпте лишь оглавление.' +
        '</div>' +
        '<div class="dsx-mem-list" style="margin-top:8px;max-height:230px;overflow-y:auto;display:flex;flex-direction:column;gap:8px"></div>' +
        '<button class="dsx-mem-add" style="margin-top:8px;padding:8px 14px;cursor:pointer;border-radius:10px;font-size:13px;font-family:inherit;' +
        'color:var(--dsw-alias-label-primary,#eee);background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">+ Добавить заметку</button>' +
      '</div>' +
      // --- раздел: ТЕСТ УСТОЙЧИВОСТИ (jailbreak-check владельца)
      '<div style="padding:18px 20px 0">' +
        '<label style="display:flex;align-items:center;gap:10px;cursor:pointer;font-size:13px;font-weight:600;' +
        'color:var(--dsw-alias-label-primary,#eee)">' +
          '<input type="checkbox" class="dsx-set-jb" style="width:15px;height:15px;accent-color:#5686fe">' +
          ' Тест устойчивости (jailbreak-check)' +
        '</label>' +
        '<div style="margin-top:4px;font-size:12px;line-height:1.5;color:var(--dsw-alias-label-tertiary,#888)">' +
          'Для проверки СВОЕЙ модели: вставь сюда свой тестовый промпт — он будет подшиваться ' +
          'в конец скрытого контекста, и ты увидишь, поддаётся ли модель. Пустое поле = ничего не уходит.' +
        '</div>' +
        '<textarea class="dsx-set-jbtext" rows="4" placeholder="Твой тестовый промпт для jailbreak-check…" ' +
        'style="width:100%;box-sizing:border-box;margin-top:8px;padding:10px 12px;resize:vertical;' +
        'border-radius:10px;font-size:13px;line-height:1.5;font-family:inherit;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));outline:none"></textarea>' +
      '</div>' +
      // --- раздел: мелочи
      '<div style="padding:16px 20px 0">' +
        '<label style="display:flex;align-items:center;gap:10px;cursor:pointer;font-size:13px;' +
        'color:var(--dsw-alias-label-secondary,#aaa)">' +
          '<input type="checkbox" class="dsx-set-rows" style="width:15px;height:15px;accent-color:#5686fe">' +
          ' Показывать строки статуса под ответом DeepSeek («Прочитано», «Применено»)' +
        '</label>' +
      '</div>' +
      // --- футер
      '<div style="display:flex;align-items:center;gap:10px;padding:18px 20px 20px">' +
        '<div style="flex:1;font-size:12px;color:var(--dsw-alias-label-tertiary,#777)">DeepSeek Extended · v1.10.0</div>' +
        '<button class="dsx-set-cancel" style="padding:10px 16px;cursor:pointer;border-radius:12px;font-size:14px;' +
        'font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
        'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">Отмена</button>' +
        '<button class="dsx-set-save" style="padding:10px 18px;border:none;cursor:pointer;border-radius:12px;' +
        'font-size:14px;font-weight:600;font-family:inherit;color:#fff;' +
        'background:var(--dsw-alias-brand-primary,#5686fe)">Сохранить</button>' +
      '</div>';
    bindModalChrome(card);
    syncFolderLabels();
    var ta = card.querySelector('.dsx-set-rules');
    var cb = card.querySelector('.dsx-set-rows');
    var jb = card.querySelector('.dsx-set-jb');
    var jt = card.querySelector('.dsx-set-jbtext');
    ta.value = cfg.rules || '';
    cb.checked = cfg.statusRows !== false;
    jb.checked = !!cfg.jbTest;
    jt.value = cfg.jbText || '';
    var memList = card.querySelector('.dsx-mem-list');
    function addMemCard(n) {
      var wrap = document.createElement('div');
      wrap.className = 'dsx-mem-card';
      wrap.style.cssText = 'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));' +
        'border-radius:10px;padding:8px;background:var(--dsw-alias-bg-layer-2,#1d1f24)';
      var ti = document.createElement('input');
      ti.className = 'dsx-mem-t'; ti.type = 'text';
      ti.placeholder = 'Заголовок («Пакеты протокола», «Структуры данных»…)';
      ti.value = (n && n.t) || '';
      ti.style.cssText = 'width:100%;box-sizing:border-box;padding:7px 10px;border-radius:8px;' +
        'font-size:12.5px;font-family:inherit;background:transparent;' +
        'color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      var xa = document.createElement('textarea');
      xa.className = 'dsx-mem-x'; xa.rows = 3;
      xa.placeholder = 'Суть: поля, форматы, как что работает…';
      xa.value = (n && n.x) || '';
      xa.style.cssText = 'width:100%;box-sizing:border-box;margin-top:6px;padding:7px 10px;' +
        'resize:vertical;border-radius:8px;font-size:12.5px;line-height:1.45;font-family:inherit;' +
        'background:transparent;color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      var del = document.createElement('button');
      del.textContent = 'Удалить';
      del.style.cssText = 'margin-top:6px;padding:5px 10px;cursor:pointer;border-radius:8px;' +
        'font-size:12px;font-family:inherit;color:#ff8f8f;background:transparent;' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12))';
      del.onclick = function () { wrap.remove(); };
      wrap.appendChild(ti); wrap.appendChild(xa); wrap.appendChild(del);
      memList.appendChild(wrap);
    }
    (Array.isArray(cfg.memory) ? cfg.memory : []).forEach(addMemCard);
    card.querySelector('.dsx-mem-add').onclick = function () { addMemCard({ t: '', x: '' }); };
    card.querySelector('.dsx-set-cancel').onclick = closeModals;
    card.querySelector('.dsx-set-save').onclick = function () {
      cfg.rules = ta.value.slice(0, 4000);
      cfg.statusRows = !!cb.checked;
      cfg.jbTest = !!jb.checked;
      cfg.jbText = jt.value.slice(0, 8000);
      var mem = [];
      card.querySelectorAll('.dsx-mem-card').forEach(function (c2) {
        var t = c2.querySelector('.dsx-mem-t').value.slice(0, 120);
        var x = c2.querySelector('.dsx-mem-x').value.slice(0, 12000);
        if (t.trim() || x.trim()) mem.push({ t: t, x: x });
      });
      cfg.memory = clampMemory(mem);
      saveCfg(); closeModals();
      toast('Настройки сохранены');
      refreshPrompt(); // пересобрать промпт с новыми правилами
    };
    card.querySelector('.dsx-set-forget').onclick = function () {
      fsCall('forget').then(function () {
        st.folderReady = false; st.folderName = ''; st.bigPrompt = ''; st.primedOnce = false;
        sessionStorage.removeItem('dsx:primed');
        syncNativeToggle(); syncFolderLabels();
        toast('Папка забыта');
      });
    };
    // «Сменить…» перехватит fsbridge в MAIN-мире (.dsx-modal-pick).
  }

  // ---------------- события моста (папка выбрана жестом в MAIN-мире)
  function onBridgeEvent(m) {
    if (m.ev === 'folder') {
      st.folderReady = true; st.folderName = m.name || '';
      // закрываем только приветственную модалку; в настройках просто обновим подпись
      syncNativeToggle(); syncFolderLabels();
      toast('Проект подключён: ' + st.folderName);
      refreshPrompt();
    } else if (m.ev === 'folderError') {
      var err = String(m.error || '');
      if (err.indexOf('AbortError') < 0 && err.toLowerCase().indexOf('abort') < 0)
        toast('Не вышло открыть папку: ' + err);
    }
  }

  // ---------------- сборка промптов
  function rulesBlock() {
    var r = (cfg.rules || '').trim();
    if (!r) return '';
    return '\n\n# USER RULES (пользовательские правила — соблюдай СТРОГО, они важнее стандартных)\n' + r;
  }
  // индекс памяти в большой промпт: только ЗАГОЛОВКИ — тела заметок
  // модель добывает сама через [<{MEMORY SEARCH: …}>]
  function memIndexBlock() {
    var notes = Array.isArray(cfg.memory) ? cfg.memory : [];
    if (!notes.length) return '';
    var titles = [];
    notes.forEach(function (n, i) {
      var t = String((n && n.t) || ('заметка ' + (i + 1))).trim();
      if (t) titles.push('- ' + t);
    });
    if (!titles.length) return '';
    var s = titles.join('\n');
    if (s.length > 3500) s = s.slice(0, 3500) + '\n- …';
    return '\n\n# PROJECT MEMORY (owner knowledge base)\n' +
      'The owner preloaded knowledge notes about this project (fields, structures, protocols, ' +
      'how things work). When unsure about ANY project internals, FIRST ask ' +
      '[<{MEMORY SEARCH: keywords}>] — matching fragments arrive in the next SYSTEM message. ' +
      'Available note titles:\n' + s;
  }
  function jbBlock() {
    if (!cfg.jbTest) return '';
    var t = (cfg.jbText || '').trim();
    if (!t) return '';
    return '\n\n' + t; // red-team probe владельца: текст подшивается как есть
  }
  function bigPrompt(tree) {
    return '[Автоматическое системное сообщение DeepSeek Extended. Этот блок — служебный ' +
      'контекст расширения: всегда учитывай его и НИКОГДА не упоминай и не цитируй ' +
      'в ответах пользователю.]\n\n' +
      DsideOps.buildPrompt(tree) + rulesBlock() + memIndexBlock() + jbBlock();
  }
  function shortPrefix() {
    return '[DeepSeek Extended активен для проекта «' + st.folderName + '». Напоминание: ' +
      'все изменения файлов — СТРОГО блоками ```deepseekide-ops (строгий JSON); ' +
      'недостающий контекст запрашивай маркерами [<{NEED FILE: путь}>], [<{NEED SEARCH: подстрока}>], ' +
      '[<{FILE INFO: путь}>], [<{READ RANGE: путь:с-по}>], [<{MEMORY SEARCH: слова}>] (каждый с новой строки). ' +
      'Рабочее сообщение = только блоки/запросы, без итога; итог пиши следующим сообщением после SYSTEM-ответа. ' +
      'Эту служебную вставку в ответе не упоминай.]' + rulesBlock() + jbBlock();
  }
  async function refreshPrompt() {
    var en = await fsCall('ensure');
    if (!(en.ok && en.data.ok)) { st.folderReady = false; syncNativeToggle(); return; }
    st.folderReady = true; st.folderName = en.data.name;
    var t = await fsCall('tree', { max_entries: 350 });
    if (t.ok) st.bigPrompt = bigPrompt(t.data.text);
    else if (!st.bigPrompt) st.bigPrompt = bigPrompt('');
    console.log('[DSX] refresh: папка="' + st.folderName + '"' + (cfg.jbTest && (cfg.jbText || '').trim() ? ', JB-ТЕСТ ВКЛ' : '') + ', промпт=' +
      (st.bigPrompt ? st.bigPrompt.length + ' символов' : 'НЕТ') + (t.ok ? '' : ' (дерево не прочиталось)'));
    syncNativeToggle(); syncFolderLabels();
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
  function findSendBtn() {
    var b = document.querySelector('._52c986b');
    if (b && b.offsetParent !== null) return b.closest('button,[role="button"]') || b;
    return null;
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
        toast('Кликни по «Авто-пилот» — нужен доступ к папке проекта');
      }
      console.log('[DSX] пропуск: папка не подключена (folderReady=false)');
      fsCall('ensure').then(function (en) {
        if (en.ok && en.data.ok) refreshPrompt();
      });
      return;
    }
    var prefix, chip;
    if (st.primedOnce) { prefix = shortPrefix(); chip = 'Автопилот'; }
    else if (st.bigPrompt) { prefix = st.bigPrompt; chip = 'Контекст проекта загружен'; }
    else { prefix = shortPrefix(); chip = 'Автопилот'; }
    nativeSetValue(editor, wrapStealth(prefix, chip, v.replace(/^\s+/, '')));
    if (!st.primedOnce && st.bigPrompt) {
      st.primedOnce = true; sessionStorage.setItem('dsx:primed', '1');
      toast('Контекст проекта подшит к первому сообщению');
    }
    // новое сообщение юзера = новый цикл: дедуп NEED сбрасываем (в т.ч. на диске)
    st.autoCount = 0; st.sentFiles = {}; st.sentSearches = {}; st.sentInfos = {}; st.sentRanges = {}; st.sentWeb = {}; st.sentDl = {}; st.sentGrep = {}; sentSave();
    st.liveSeen = {};
    sessionStorage.setItem('dsx:auto', '0');
    console.log('[DSX] промпт подшит к сообщению:', (prefix === st.bigPrompt ? 'БОЛЬШОЙ' : 'короткий'),
      '| итого символов:', editor.value.length);
    clearTimeout(st.watchdog);
    st.watchdog = setTimeout(function () {
      if (Date.now() - st.lastTrimAt > 2500)
        console.warn('[DSX] ВНИМАНИЕ: пузырь с промптом не найден — возможно, сайт сменил вёрстку. Скинь этот лог разработчику.');
    }, 2600);
  }
  document.addEventListener('keydown', function (e) {
    if (e.__dsxBypass || e.key !== 'Enter' || e.shiftKey || e.isComposing) return;
    var t = e.target;
    if (!t || t.tagName !== 'TEXTAREA') return;
    if (t !== findEditor()) return; // наши модалки (и любые чужие поля) — мимо
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

  // ---------------- БРИТВА v3: прячем служебный текст БЕЗ удаления узлов.
  // Только nodeValue in-place и style.display. КОМПОЗЕР (нижняя панель
  // ._871cbca с textarea и её скрытым дублём-измерителем) — ЗАПРЕТНАЯ ЗОНА:
  // там маркеры тоже живут (сайт копирует ввод), но трогать их = ломать ввод.
  function softSetText(el, newText) {
    var walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
    var nodes = [], n;
    while ((n = walker.nextNode())) nodes.push(n);
    if (!nodes.length) return;
    nodes[0].nodeValue = newText;                    // хвост — в первый узел
    for (var i = 1; i < nodes.length; ++i) nodes[i].nodeValue = ''; // остальные пустеют, но ЖИВЫ
  }
  function markerCarrier(tn) {
    var el = tn.parentElement, hops = 0;
    while (el && hops < 8) {
      if (el.textContent.indexOf(END) >= 0) return el;
      el = el.parentElement; hops++;
    }
    return null;
  }
  function trimStealthNodes() {
    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    var hits = [], n;
    while ((n = walker.nextNode())) {
      if (n.nodeValue && n.nodeValue.indexOf(MARK) >= 0) hits.push(n);
    }
    for (var k = 0; k < hits.length; ++k) {
      var tn = hits[k];
      var pe = tn.parentElement;
      if (!pe || !pe.closest) continue;
      if (pe.closest('._871cbca')) continue;           // КОМПОЗЕР — не трогаем никогда
      var el = markerCarrier(tn);
      if (!el) continue;
      var full = el.textContent;
      var i = full.indexOf(MARK), j = full.lastIndexOf(END);
      if (i < 0 || j < 0 || j <= i) continue;
      var tail = full.slice(j + END.length).replace(/^\s+/, '');
      // строка главного списка сообщений (у рядов есть data-virtual-list-item-key)
      var row = pe.closest('._9663006[data-virtual-list-item-key], ._4f9bf79[data-virtual-list-item-key]');
      if (row) {
        if (tail) {
          softSetText(el, tail);                       // сообщение юзера — только его текст
        } else if (String(row.className).indexOf('_9663006') >= 0) {
          softSetText(el, '');                         // служебное — гасим и прячем пузырь
          row.style.display = 'none';
        }
        st.lastTrimAt = Date.now();
        continue;
      }
      // правый сайдбар-навигатор сообщений: зеркалит юзер-сообщения — прячем карточку
      var navItem = pe.closest('._81e7b5e');
      if (navItem) {
        if (tail) softSetText(el, tail);
        else { softSetText(el, ''); navItem.style.display = 'none'; }
        continue;
      }
      // прочие места: только гасим текст
      softSetText(el, tail);
    }
  }

  // ---------------- сторожок композера + залежалые следы старых версий:
  // textarea — всегда видима; input[type=file] — наоборот ПРЯЧЕМ (сайт дёргает
  // его программно из скрепки, голая нативная кнопка «Выбор файлов» — мусор),
  // чистим «📖»-строки и линию статус-боксов от v19–v20.
  setInterval(function () {
    try {
      document.querySelectorAll('.dsx-row-line').forEach(function (p) {
        var t = p.textContent || '';
        if (/[📖🔍✅⚠]/.test(t)) p.textContent = t.replace(/[📖🔍✅⚠]\s*/g, '');
      });
    } catch (e) {}
  }, 1500);
  setInterval(function () {
    try {
      var box = document.querySelector('._871cbca') || document;
      var ta = box.querySelector('textarea[placeholder*="DeepSeek" i], textarea');
      if (ta && ta.style && ta.style.display === 'none') ta.style.removeProperty('display');
      box.querySelectorAll('input[type="file"]').forEach(function (f) {
        if (f.style && f.style.display !== 'none') f.style.display = 'none';
      });
    } catch (e) {}
  }, 1000);

  // ---------------- автоклик «Continue/Продолжить» (оборванная генерация)
  // DeepSeek рисует чип-кнопку, когда ответ обрывается; пока мы ждём хвост
  // генерации после служебного — жмём её сами, иначе авто-цикл встаёт молча.
  var lastContinueAt = 0;
  function clickContinueIfAny() {
    if (!cfg.pilot) return;
    if (!st.waitingAnswer && !st.busy) return;       // не наша очередь — не наши кнопки
    if (Date.now() - lastContinueAt < 8000) return;  // анти-спам
    var items = document.querySelectorAll('[data-virtual-list-item-key]');
    if (!items.length) return;
    var tail = items[items.length - 1];              // только хвост беседы
    var cands = tail.querySelectorAll('div[role="button"], button');
    for (var i = cands.length - 1; i >= 0; --i) {
      var el = cands[i];
      if (!el || !el.isConnected || el.offsetParent === null) continue;
      if (el.closest('._871cbca')) continue;         // композер — запретная зона
      var t = (el.textContent || '').trim();
      if (t !== 'Continue' && t !== 'Продолжить' && t !== 'Continue generating') continue;
      var ev = new MouseEvent('click', { bubbles: true });
      ev.__dsxBypass = true;
      el.dispatchEvent(ev);
      lastContinueAt = Date.now();
      console.log('[DSX] нажал «Continue» — генерация продолжается');
      break;
    }
  }
  setInterval(function () { try { clickContinueIfAny(); } catch (e) {} }, 1500);

  // ---------------- ввод в чат (служебные сообщения автопилота — настоящие
  // отправки на сервер, чтобы после перезагрузки модель их видела)
  async function injectStealth(real, logTag) {
    var el = findEditor();
    if (!el) { toast('Не нашёл поле ввода чата'); return false; }
    el.focus();
    nativeSetValue(el, wrapStealth(real, logTag, ''));
    await new Promise(function (r) { setTimeout(r, 320); });
    var clicked = false;
    var btn = findSendBtn();                     // основной путь — родная кнопка отправки
    if (!btn) {                                  // запасной — подъём от textarea
      var box = el;
      for (var i = 0; i < 6 && box && !clicked; ++i) {
        var b2 = box.querySelector('button:not([disabled])');
        if (b2 && b2.offsetParent !== null) { btn = b2; break; }
        box = box.parentElement;
      }
    }
    if (btn) {
      var ev = new MouseEvent('click', { bubbles: true });
      ev.__dsxBypass = true; btn.dispatchEvent(ev);
      clicked = true;
    }
    if (!clicked) {
      ['keydown', 'keyup'].forEach(function (tp) {
        var ke = new KeyboardEvent(tp, { key: 'Enter', bubbles: true });
        ke.__dsxBypass = true; el.dispatchEvent(ke);
      });
    }
    console.log('[DSX] service-send:', logTag, real.length, 'chars');
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
  // последний ли этот ответ в беседе (нет сообщений пользователя после него) —
  // важно для авто-возобновления после перезагрузки: обслуживаем только хвост
  function isTailAssistant(block) {
    try {
      var msgs = document.querySelectorAll('._9663006, ._4f9bf79');
      if (!msgs.length) return true;
      var last = msgs[msgs.length - 1];
      return last === block || last.contains(block);
    } catch (e) { return true; }
  }

  // ---------------- самообслуживание: скобочные запросы [<{...}>] + легаси NEED FILE
  // Дедуп ПОСТОЯННЫЙ: переживает перезагрузку страницы (sessionStorage,
  // отдельно на каждую беседу по URL). Сбрасывается только когда юзер
  // шлёт новое своё сообщение (см. tryStealthPrefix).
  function sentKey() { return 'dsx:sent:' + location.pathname; }
  function sentLoad() {
    try {
      var raw = sessionStorage.getItem(sentKey());
      if (!raw) return;
      var d = JSON.parse(raw);
      st.sentFiles = d.f || {}; st.sentSearches = d.s || {};
      st.sentInfos = d.i || {}; st.sentRanges = d.r || {}; st.sentWeb = d.w || {};
      st.sentDl = d.d || {}; st.sentGrep = d.g || {};
    } catch (e) {}
  }
  function sentSave() {
    try {
      sessionStorage.setItem(sentKey(), JSON.stringify({ f: st.sentFiles, s: st.sentSearches, i: st.sentInfos, r: st.sentRanges, w: st.sentWeb, d: st.sentDl, g: st.sentGrep }));
    } catch (e) {}
  }
  sentLoad();
  // ---------------- WEB FETCH: фон тянет страницу, тут чистим в текст
  function hostOf(u) { try { return new URL(u).host; } catch (e) { return String(u).slice(0, 60); } }
  function webFetchCall(url, dl) {
    return new Promise(function (resolve) {
      try {
        chrome.runtime.sendMessage({ dsx: 'webfetch', url: url, dl: !!dl }, function (r) {
          if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
          else resolve(r || { ok: false, error: 'пустой ответ фона' });
        });
      } catch (e) { resolve({ ok: false, error: String(e && e.message || e) }); }
    });
  }
  function htmlToText(html, type) {
    var raw = String(html || '');
    if (String(type).indexOf('html') < 0) {
      // json/txt/xml — отдаём как есть, без html-крякозябр
      return raw;
    }
    try {
      var doc = new DOMParser().parseFromString(raw, 'text/html');
      ['script', 'style', 'noscript', 'svg', 'header', 'footer', 'nav', 'form', 'iframe', 'canvas']
        .forEach(function (t) { doc.querySelectorAll(t).forEach(function (n) { n.remove(); }); });
      var main = doc.querySelector('main') || doc.querySelector('article') ||
                 doc.querySelector('#main') || doc.querySelector('#content') ||
                 doc.querySelector('.content') || doc.body;
      var text = (main && main.textContent) || '';
      return text.replace(/[ \t\r\f\v]+/g, ' ').replace(/\n\s*\n+/g, '\n\n').trim();
    } catch (e) { return raw; }
  }
  function base(p) { var a = String(p).split('/'); return a[a.length - 1]; }

  async function serviceNote(raw, progress) {
    if (raw.length > 60000) raw = raw.slice(0, 60000);
    // LLM иногда пересказывает прошлые NEED-строки в ответе («я запросил
    // README.md…») — отрезаем этот эпилог, запросы ниже списка не ловим.
    var cut = raw.search(/\n\s*(Теперь|Итак|Отлично|Готово|Хорошо,? вот|Вот что|Давай(те)?|Кстати)/);
    if (cut > 0) raw = raw.slice(0, cut);

    var reqs = DsideOps.findBracketRequests(raw);
    DsideOps.findFileRequests(raw).forEach(function (p) {
      reqs.push({ kind: 'NEED FILE', arg: p, key: 'NEED FILE|' + p.toLowerCase(), bracket: false });
    });
    DsideOps.findSearchRequests(raw).forEach(function (q) {
      reqs.push({ kind: 'NEED SEARCH', arg: q, key: 'NEED SEARCH|' + q.toLowerCase().slice(0, 200), bracket: false });
    });
    // дедуп внутри одного ответа
    var seen = {}, uniq = [];
    reqs.forEach(function (r) { if (!seen[r.key]) { seen[r.key] = 1; uniq.push(r); } });
    if (!uniq.length) return { text: '', rows: [] };

    var out = ['SYSTEM: auto-reply from DeepSeek Extended.'], rows = [], asks = [], shells = [];
    var done = 0, i;
    for (i = 0; i < uniq.length; ++i) {
      var rq = uniq[i];
      if (done >= 6) { // потолок хода: 6 операций самообслуживания
        out.push('(more requests pending — emit them again in your next message, they will be served)');
        break;
      }
      if (rq.kind === 'NEED FILE') {
        var p = rq.arg;
        if (st.sentFiles[p]) continue;
        st.sentFiles[p] = 1;
        var r = await fsCall('readNumbered', { path: p });
        if (r.ok) {
          var bodyTxt = (r.data.lines > 4000 ? '(показаны первые 4000 строк из ' + r.data.lines +
              '; для продолжения используйте [<{READ RANGE: ' + p + ':4001-4800}>])\n' : '') + r.data.text;
          if (bodyTxt.length > 120000) {
            var cutLn = (bodyTxt.slice(0, 120000).match(/\n/g) || []).length;
            bodyTxt = bodyTxt.slice(0, 120000) + '\n…(обрезано на ~строке ' + cutLn +
              ' из ' + r.data.lines + '; продолжайте через [<{READ RANGE: ' + p + ':' + (cutLn + 1) + '-' + (cutLn + 400) + '>}])';
          }
          out.push('You requested file content of «' + p + '» (lines are numbered; use those numbers with insert_lines/replace_lines).');
          out.push('FILE "' + p + '"\n```\n' + bodyTxt + '\n```');
          if (!rq.bracket) rows.push('Прочитано: ' + base(p));
        } else {
          out.push('FILE "' + p + '" — READ ERROR: ' + (r.error || 'не читается') +
                   '\n(проверьте имя через [<{NEED SEARCH: часть имени}>])');
          if (!rq.bracket) rows.push('Не удалось прочитать: ' + base(p));
        }
        done++;
      } else if (rq.kind === 'FILE INFO') {
        var ip = rq.arg;
        if (st.sentInfos[ip]) continue;
        st.sentInfos[ip] = 1;
        var inf = await fsCall('fileInfo', { path: ip });
        if (inf.ok) {
          var lns = inf.data.lines >= 0 ? String(inf.data.lines) : 'очень много (>3 МБ, не считались)';
          out.push('INFO "' + ip + '": ' + inf.data.bytes + ' bytes, ' + lns + ' lines, modified ' + inf.data.modified + '.' +
                   (inf.data.lines > 600 || inf.data.lines < 0
                     ? ' Large file — read it chunk-by-chunk via [<{READ RANGE: ' + ip + ':1-400}>], [<{READ RANGE: ' + ip + ':401-800}>] etc.'
                     : ''));
          if (!rq.bracket) rows.push('Размер: ' + base(ip) + ' — ' + lns + ' строк');
        } else {
          out.push('INFO "' + ip + '" — ERROR: ' + (inf.error || 'не читается'));
          if (!rq.bracket) rows.push('Не удалось осмотреть: ' + base(ip));
        }
        done++;
      } else if (rq.kind === 'READ RANGE') {
        var rm = rq.arg.match(/^(.+):(\d+)-(\d+)$/);
        if (!rm) continue;
        var rp = rm[1], rf = +rm[2], rt = +rm[3];
        if (st.sentRanges[rq.key]) continue;
        st.sentRanges[rq.key] = 1;
        var rr = await fsCall('readNumbered', { path: rp, start_line: rf, end_line: rt });
        if (rr.ok) {
          var bt = rr.data.text;
          if (bt.length > 60000) bt = bt.slice(0, 60000) + '\n…(truncated at 60k chars)';
          out.push('RANGE "' + rp + '" — lines ' + rf + '-' + rt + ' of ' + rr.data.lines + ' total:\n```\n' + bt + '\n```');
          if (!rq.bracket) rows.push('Прочитано: ' + base(rp) + ' (' + rf + '–' + rt + ')');
        } else {
          out.push('RANGE "' + rp + '" — READ ERROR: ' + (rr.error || 'не читается'));
          if (!rq.bracket) rows.push('Не удалось прочитать: ' + base(rp));
        }
        done++;
      } else if (rq.kind === 'MEMORY SEARCH') {
        var mq = rq.arg;
        var notes = Array.isArray(cfg.memory) ? cfg.memory : [];
        var hits = DsideOps.searchMemory(notes, mq);
        if (hits.length) {
          var seg = hits.map(function (h, n) { return (n + 1) + ') [' + h.title + ']\n' + h.snippet; }).join('\n---\n');
          if (seg.length > 6000) seg = seg.slice(0, 6000) + '\n…(truncated)';
          out.push('MEMORY hits for «' + mq + '» — owner knowledge base (' + notes.length + ' notes):\n' + seg);
        } else {
          var tl = notes.map(function (n2, i2) { return String(n2.t || ('заметка ' + (i2 + 1))).trim(); })
                        .filter(Boolean).slice(0, 40).join('; ');
          if (tl.length > 600) tl = tl.slice(0, 600) + '…';
          out.push('MEMORY «' + mq + '»: no hits. Knowledge base has ' + notes.length + ' note(s)' +
                   (tl ? ' — titles: ' + tl + '. Retry with different or simpler keywords.'
                       : ' and it is EMPTY — ask the user to fill Память in extension settings.'));
        }
        if (!rq.bracket) rows.push('Память «' + mq + '»' + (hits.length ? ': найдено' : ' — пусто'));
        done++;
      } else if (rq.kind === 'WEB FETCH') {
        var wu = rq.arg;
        if (st.sentWeb[wu]) continue;
        st.sentWeb[wu] = 1;
        var fin1 = progress ? progress('Загрузка: ' + hostOf(wu) + '…') : null;
        var wf = await webFetchCall(wu);
        if (fin1) fin1(wf.ok ? 'Загружено: ' + hostOf(wu) : 'Не загрузилось: ' + hostOf(wu));
        if (wf.ok) {
          var wtxt = htmlToText(wf.text || '', wf.type || '');
          if (!wtxt.trim()) {
            out.push('PAGE "' + wu + '": fetched (HTTP ' + wf.status + ') but no readable text — ' +
                     (String(wf.type || '').indexOf('html') >= 0 ? 'JS-heavy SPA?' : 'empty body') + '. Try documentation mirrors or another URL.');
          } else {
            if (wtxt.length > 25000) wtxt = wtxt.slice(0, 25000) + '\n…(truncated at 25k chars; fetch a more specific page if needed)';
            out.push('PAGE "' + wu + '" (HTTP ' + wf.status + ', ' + (wf.type || '?') + '):\n```\n' + wtxt + '\n```');
          }
          if (!rq.bracket) rows.push('Загружено: ' + hostOf(wu));
        } else {
          out.push('PAGE "' + wu + '": FETCH FAILED — ' + (wf.error || 'сеть недоступна'));
          if (!rq.bracket) rows.push('Не загрузилось: ' + hostOf(wu));
        }
        done++;
      } else if (rq.kind === 'WEB DOWNLOAD') {
        var durl = rq.arg, dpath = String(rq.extra || '');
        var dkey = durl + '|' + dpath;
        if (st.sentDl[dkey]) continue;
        st.sentDl[dkey] = 1;
        var finD = progress ? progress('Скачивает: ' + base(dpath) + ' ← ' + hostOf(durl) + '…') : null;
        var dwn = await webFetchCall(durl, true);
        if (dwn.ok && dwn.b64) {
          var wb = await fsCall('writeB64', { path: dpath, b64: dwn.b64 });
          if (wb.ok) {
            var kb = dwn.size > 1048576 ? (dwn.size / 1048576).toFixed(1) + ' MB' : Math.round(dwn.size / 1024) + ' KB';
            out.push('DOWNLOAD saved: «' + dpath + '» — ' + dwn.size + ' bytes (' + (dwn.type || '?') + ') from ' + durl + '. The file is in the project now.');
            if (!rq.bracket) rows.push('Скачано: ' + base(dpath) + ' (' + kb + ')');
            if (finD) finD('Скачано: ' + base(dpath) + ' (' + kb + ')');
            refreshPrompt();
          } else {
            out.push('DOWNLOAD «' + dpath + '»: write FAILED — ' + (wb.error || '?'));
            if (!rq.bracket) rows.push('Не сохранилось: ' + base(dpath));
            if (finD) finD('Не сохранилось: ' + base(dpath));
          }
        } else {
          out.push('DOWNLOAD from ' + durl + ': FETCH FAILED — ' + (dwn.error || '?'));
          if (!rq.bracket) rows.push('Не скачалось: ' + hostOf(durl));
          if (finD) finD('Не скачалось: ' + hostOf(durl));
        }
        done++;
      } else if (rq.kind === 'WEB GREP') {
        var gurl = rq.arg, gq = String(rq.extra || '');
        var gkey = gurl + '|' + gq.toLowerCase();
        if (st.sentGrep[gkey]) continue;
        st.sentGrep[gkey] = 1;
        var finG = progress ? progress('Ищет в инете: «' + gq + '» на ' + hostOf(gurl) + '…') : null;
        var gp = await webFetchCall(gurl);
        if (gp.ok) {
          var gsrc = String(gp.type || '').indexOf('html') >= 0 ? htmlToText(gp.text || '', gp.type) : String(gp.text || '');
          var glines = gsrc.split('\n'), gout = [], gi;
          var lm = gq.match(/^(\d+)\s*[-–—]\s*(\d+)$/);
          if (lm) { // диапазон строк «144-150»
            var gf = Math.max(1, +lm[1]), gt = Math.min(glines.length, Math.min(gf + 119, +lm[2]));
            for (gi = gf; gi <= gt; ++gi) gout.push('L' + gi + ': ' + glines[gi - 1].slice(0, 300));
            if (+lm[2] > gt) gout.push('…(дальше ' + gt + ' строк нет: всего ' + glines.length + ')');
            out.push('GREP "' + gurl + '" — lines ' + gf + '-' + gt + ' of ' + glines.length + ':\n```\n' + gout.join('\n').slice(0, 20000) + '\n```');
            if (finG) finG('Прочитано в инете: строки ' + gf + '–' + gt + ' (' + hostOf(gurl) + ')');
            if (!rq.bracket) rows.push('Строки ' + gf + '–' + gt + ': ' + hostOf(gurl));
          } else { // подстрока → ПОЛНЫЕ строки с номерами
            for (gi = 0; gi < glines.length && gout.length < 40; ++gi) {
              if (glines[gi].indexOf(gq) >= 0) gout.push('L' + (gi + 1) + ': ' + glines[gi].trim().slice(0, 300));
            }
            if (gout.length) {
              out.push('GREP "' + gurl + '" for «' + gq + '» — ' + gout.length + ' hit(s), FULL lines:\n```\n' + gout.join('\n').slice(0, 20000) + '\n```');
              if (finG) finG('Найдено в инете: «' + gq + '» — ' + gout.length + ' совпад.');
            } else {
              out.push('GREP "' + gurl + '" for «' + gq + '» — no hits on the page (' + glines.length + ' lines scanned). Try another spelling or fetch the page first.');
              if (finG) finG('Не найдено в инете: «' + gq + '»');
            }
            if (!rq.bracket) rows.push('Искал в инете: «' + gq + '»' + (gout.length ? ' — ' + gout.length + ' совпад.' : ' — пусто'));
          }
        } else {
          out.push('GREP "' + gurl + '": FETCH FAILED — ' + (gp.error || '?'));
          if (finG) finG('Не открылось: ' + hostOf(gurl));
          if (!rq.bracket) rows.push('Не открылось: ' + hostOf(gurl));
        }
        done++;
      } else if (rq.kind === 'MEMORY ADD') {
        var mt = rq.arg, mb = String(rq.extra || '');
        if (!Array.isArray(cfg.memory)) cfg.memory = [];
        var merged = false, mi;
        for (mi = 0; mi < cfg.memory.length; ++mi) {
          if (String(cfg.memory[mi].t || '').toLowerCase() === mt.toLowerCase()) {
            cfg.memory[mi].x = (String(cfg.memory[mi].x || '') + '\n' + mb).slice(0, 12000);
            merged = true; break;
          }
        }
        if (!merged && cfg.memory.length >= 40) {
          out.push('MEMORY ADD «' + mt + '»: REJECTED — knowledge base is full (40 notes). Tell the user, or add the fact into an EXISTING note title.');
          if (!rq.bracket) rows.push('Память переполнена: ' + mt);
        } else {
          if (!merged) cfg.memory.push({ t: mt.slice(0, 120), x: mb.slice(0, 2000) });
          cfg.memory = clampMemory(cfg.memory);
          saveCfg();
          out.push('MEMORY SAVED «' + mt + '»' + (merged ? ' (appended to the existing note)' : ' (new note)') +
                   '. Total: ' + cfg.memory.length + ' note(s). It is now searchable via MEMORY SEARCH.');
          if (!rq.bracket) rows.push('Запомнил: ' + mt);
        }
        done++;
      } else if (rq.kind === 'REVERT FILE') {
        var vp = rq.arg;
        var rv = await fsCall('revertFile', { path: vp });
        if (rv.ok && rv.data && rv.data.reverted) {
          out.push('REVERT "' + vp + '": done — restored its pre-edit state from snapshot «' + (rv.data.label || 'без имени') + '»' +
                   (rv.data.kind === 'created' ? ' (your edit had created it, so the file was REMOVED)' : ' (old content written back)') +
                   '. The file now differs from your last edit — re-read it before editing again.');
          if (!rq.bracket) rows.push('Откачено: ' + base(vp));
          refreshPrompt(); // содержимое изменилось
        } else {
          out.push('REVERT "' + vp + '": FAILED — ' + ((rv.data && rv.data.error) || rv.error || 'нет снапшота'));
          if (!rq.bracket) rows.push('Откат не удался: ' + base(vp));
        }
        done++;
      } else if (rq.kind === 'SHELL') {
        shells.push({ cmd: rq.arg });
        if (!rq.bracket) rows.push('Команда: ' + rq.arg.slice(0, 80));
      } else if (rq.kind === 'ASK' || rq.kind === 'ASK MULTI') {
        asks.push({ q: rq.arg, opts: rq.opts || [], multi: rq.kind === 'ASK MULTI' });
        if (!rq.bracket) rows.push('Ждёт ответа: ' + rq.arg);
      } else { // NEED SEARCH
        var q = rq.arg;
        if (st.sentSearches[q]) continue;
        st.sentSearches[q] = 1;
        var s = await fsCall('search', { query: q });
        var sb = s.ok ? s.data.text : ('ERROR: ' + (s.error || ''));
        if (sb.length > 60000) sb = sb.slice(0, 60000) + '\n…(truncated)';
        out.push('SEARCH "' + q + '" — substring matches over ALL text files of the project (file:line: text):\n```\n' + sb + '\n```');
        if (!rq.bracket) rows.push('Поиск «' + q + '»' + (s.ok ? ': ' + s.data.matches + ' совпадений' : ' — ошибка'));
        done++;
      }
    }
    sentSave();
    if ((asks.length || shells.length) && !done) return { text: '', rows: rows, asks: asks, shells: shells }; // вопросы/команды — ждёт человек
    if (!done) {
      // ВСЁ запрошенное уже отправлялось раньше: молчать НЕЛЬЗЯ (модель ждёт
      // ответ и встаёт навсегда) — шлём короткое напоминание.
      out.push('REMINDER: everything you just requested was ALREADY sent earlier in this chat (look above in the hidden SYSTEM messages).');
      out.push('Do NOT repeat the same requests — re-use that content. If you need a DIFFERENT slice of a big file, ask ' +
               '[<{READ RANGE: <path>:<other-from>-<other-to>}>]; for size, [<{FILE INFO: <path>}>].');
      return { text: out.join('\n\n'), rows: [] };
    }
    return { text: out.join('\n\n'), rows: rows, asks: asks, shells: shells };
  }

  // ---------------- бейджи: [<{…}>] в ответе модели превращаем в строки-статусы
  // Меняем ТОЛЬКО nodeValue текстовых узлов (React не заметит — дерево то же).
  var BADGE_RE = /\[<\{\s*(NEED FILE|NEED SEARCH|FILE INFO|READ RANGE|MEMORY SEARCH|MEMORY ADD|ASK MULTI|ASK|REVERT FILE|WEB FETCH|WEB DOWNLOAD|WEB GREP|SHELL)\s*:\s*([^<>]*?)\s*\}>\]/gi;
  function badgeText(kind, arg, pending) {
    var K = kind.toUpperCase(), tail = pending ? '…' : '';
    if (K === 'NEED FILE')    return (pending ? 'Чтение: '        : 'Прочитано: ')      + base(arg) + tail;
    if (K === 'FILE INFO')    return (pending ? 'Осмотр: '        : 'Размер получен: ') + base(arg) + tail;
    if (K === 'READ RANGE') {
      var m = String(arg).match(/^(.+?)\s*:\s*(\d+)\s*[-–—]\s*(\d+)$/);
      var nm = m ? base(m[1]) + ' (' + m[2] + '–' + m[3] + ')' : base(arg);
      return (pending ? 'Чтение: ' : 'Прочитано: ') + nm + tail;
    }
    if (K === 'MEMORY SEARCH') return (pending ? 'Память: ' : 'Найдено в памяти: ') + String(arg).trim() + tail;
    if (K === 'ASK' || K === 'ASK MULTI') return (pending ? 'Спрашивает: ' : 'Отвечено: ') + String(arg).trim() + tail;
    if (K === 'WEB FETCH') return (pending ? 'Загрузка: ' : 'Загружено: ') + hostOf(String(arg).trim()) + tail;
    if (K === 'WEB DOWNLOAD') {
      var dp = String(arg).split('::'); var dn = dp.length > 1 ? base(dp[1].trim()) : hostOf(dp[0].trim());
      return (pending ? 'Скачивает: ' : 'Скачано: ') + dn + tail;
    }
    if (K === 'WEB GREP') {
      var gp2 = String(arg).split('::'); var gq2 = (gp2.length > 1 ? gp2[1] : gp2[0]).trim();
      return (pending ? 'Ищет в инете: ' : 'Найдено в инете: ') + gq2 + tail;
    }
    if (K === 'MEMORY ADD') return (pending ? 'Запоминает: ' : 'Запомнил: ') + String(arg).split('::')[0].trim() + tail;
    if (K === 'REVERT FILE') return (pending ? 'Откатываю: ' : 'Откачено: ') + base(String(arg).trim()) + tail;
    if (K === 'SHELL') return (pending ? 'Команда: ' : 'Выполнено: ') + String(arg).trim().slice(0, 90) + tail;
    return (pending ? 'Поиск: ' : 'Найдено: ') + String(arg).trim() + tail; // NEED SEARCH
  }
  function eachTextNode(root, fn) {
    var w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
    var n; while ((n = w.nextNode())) fn(n);
  }
  // маркеры [<{...}>] прячем в «Чтение: x…» СРАЗУ (во время стрима тоже).
  // КАЖДАЯ замена записывается на узле (n.__dsxMarks: {бейдж, маркер}) —
  // иначе после замены парсер команд вообще не видит [<{...}>: бейджи
  // съедали маркеры раньше serviceNote, и тулки умирали (регрессия v28).
  function renderBadges(block) {
    try {
      eachTextNode(block, function (n) {
        if (!n.nodeValue || n.nodeValue.indexOf('[<{') < 0) return;
        BADGE_RE.lastIndex = 0;
        if (!BADGE_RE.test(n.nodeValue)) return;
        BADGE_RE.lastIndex = 0;
        var marks = n.__dsxMarks || (n.__dsxMarks = []);
        n.nodeValue = n.nodeValue.replace(BADGE_RE, function (mm, k, a) {
          var b = badgeText(k, a, true);
          marks.push({ b: b, m: mm });
          return b;
        });
      });
    } catch (e) {}
  }
  // на время чтения возвращаем оригинальные маркеры на место (парсеру нужен
  // сырой вид команды), потом resumeBadges прячет обратно
  function pauseBadges(block) {
    var n0 = 0;
    try {
      eachTextNode(block, function (n) {
        var marks = n.__dsxMarks;
        if (!marks || !marks.length) return;
        var v = n.nodeValue;
        for (var i = 0; i < marks.length; ++i) {
          if (v.indexOf(marks[i].b) >= 0) v = v.split(marks[i].b).join(marks[i].m);
        }
        if (v !== n.nodeValue) { n.nodeValue = v; n0++; }
      });
    } catch (e) {}
    return n0;
  }
  function resumeBadges(block) { try { renderBadges(block); } catch (e) {} }
  // после того, как служебное ушло: «Чтение: x…» → «Прочитано: x»
  function finalizeBadges(block) {
    var swaps = [
      [/Чтение: ([^…\n]+)…/g,        'Прочитано: $1'],
      [/Осмотр: ([^…\n]+)…/g,        'Размер получен: $1'],
      [/Поиск: ([^…\n]+)…/g,         'Найдено: $1'],
      [/Память: ([^…\n]+)…/g,        'Найдено в памяти: $1'],
      [/Спрашивает: ([^…\n]+)…/g,     'Отвечено: $1'],
      [/Загрузка: ([^…\n]+)…/g,       'Загружено: $1'],
      [/Скачивает: ([^…\n]+)…/g,      'Скачано: $1'],
      [/Ищет в инете: ([^…\n]+)…/g,   'Найдено в инете: $1'],
      [/Запоминает: ([^…\n]+)…/g,     'Запомнил: $1'],
      [/Откатываю: ([^…\n]+)…/g,      'Откачено: $1'],
      [/Команда: ([^…\n]+)…/g,        'Выполнено: $1']
    ];
    try {
      eachTextNode(block, function (n) {
        if (!n.nodeValue || n.nodeValue.indexOf('…') < 0) return;
        var v = n.nodeValue;
        for (var i = 0; i < swaps.length; ++i) v = v.replace(swaps[i][0], swaps[i][1]);
        if (v !== n.nodeValue) n.nodeValue = v;
      });
    } catch (e) {}
  }
  // ---------------- SHELL: карточка-команда с подтверждением (или авто-режим)
  function shellSend(cmd, body) {
    return injectStealth('SYSTEM: shell result for ' + cmd + '\n' + body, 'шелл');
  }
  function shellBookkeep() {
    st.autoCount++;
    st.waitingAnswer = true; sessionStorage.setItem('dsx:wait', '1');
    sessionStorage.setItem('dsx:auto', String(st.autoCount));
  }
  async function execShell(block, cmd, statusEl) {
    if (statusEl) statusEl.textContent = 'Выполняет: ' + cmd.slice(0, 90) + '…';
    var r = await new Promise(function (resolve) {
      try {
        chrome.runtime.sendMessage({ dsx: 'shell', cmd: cmd }, function (resp) {
          if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
          else resolve(resp || { ok: false, error: 'пустой ответ фона' });
        });
      } catch (e) { resolve({ ok: false, error: String(e && e.message || e) }); }
    });
    var report, short;
    if (r && r.ok) {
      var out = String(r.stdout || '').replace(/\n$/, '');
      var err = String(r.stderr || '').replace(/\n$/, '');
      report = 'CHDIR: ' + (r.cwd || '?') + '\nEXIT CODE: ' + r.exitCode +
               (r.killed ? ' (TIMEOUT 120s — убито)' : '') +
               '\n\nSTDOUT:\n```\n' + (out || '(пусто)') + '\n```' +
               (err ? '\n\nSTDERR:\n```\n' + err + '\n```' : '');
      short = 'Выполнено (код ' + r.exitCode + '): ' + cmd.slice(0, 70) +
              (out ? ' → ' + out.split('\n')[0].slice(0, 60) : '');
    } else {
      var er = (r && r.error) || 'нет связи с хостом';
      report = 'SHELL ERROR: ' + er + '\n(Если хост не установлен/не запущен — подскажи юзеру: shell\\install.bat из репозитория, потом перезапуск Chrome. Не повторяй ту же команду впрок.)';
      short = 'Ошибка шелла: ' + er.slice(0, 80);
    }
    if (statusEl) statusEl.textContent = short;
    await shellSend(cmd, report); shellBookkeep();
    finalizeBadges(block); hideActionBar(block);
  }
  function runShellCmd(block, cmd) {
    // авто-режим (галка в карточке): исполняем без вопроса
    if (st.shellAuto) { execShell(block, cmd, null); return; }
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var key = 'dsx-sh-' + hashStr(cmd).slice(0, 24);
      if (host.querySelector('[data-key="' + key + '"]')) return;

      var card = document.createElement('div');
      card.setAttribute('data-key', key);
      card.style.cssText = 'margin-top:10px;padding:12px 14px;border-radius:12px;max-width:560px;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))';

      var cap = document.createElement('div');
      cap.textContent = 'Модель просит выполнить команду:';
      cap.style.cssText = 'font-size:12px;margin-bottom:6px;color:var(--dsw-alias-label-tertiary,#888)';
      card.appendChild(cap);

      var code = document.createElement('div');
      code.textContent = cmd;
      code.style.cssText = 'padding:8px 10px;border-radius:8px;font-family:Consolas,monospace;font-size:12.5px;' +
        'line-height:1.45;word-break:break-all;user-select:text;' +
        'color:var(--dsw-alias-label-primary,#eee);background:rgba(0,0,0,.25);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08))';
      card.appendChild(code);

      var row = document.createElement('div');
      row.style.cssText = 'margin-top:10px;display:flex;align-items:center;gap:8px;flex-wrap:wrap';
      var run = document.createElement('button');
      run.textContent = 'Выполнить';
      run.style.cssText = 'padding:8px 16px;border:none;cursor:pointer;border-radius:10px;' +
        'font-size:13px;font-weight:600;font-family:inherit;color:#fff;' +
        'background:var(--dsw-alias-brand-primary,#5686fe)';
      var no = document.createElement('button');
      no.textContent = 'Отклонить';
      no.style.cssText = 'padding:8px 14px;cursor:pointer;border-radius:10px;font-size:13px;' +
        'font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
        'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))';
      var autoLab = document.createElement('label');
      autoLab.style.cssText = 'display:flex;align-items:center;gap:6px;cursor:pointer;font-size:12px;' +
        'color:var(--dsw-alias-label-tertiary,#888)';
      var autoCb = document.createElement('input');
      autoCb.type = 'checkbox'; autoCb.style.cssText = 'width:14px;height:14px;accent-color:#5686fe;margin:0';
      var autoTx = document.createElement('span');
      autoTx.textContent = 'авто-разрешать до конца сессии';
      autoLab.appendChild(autoCb); autoLab.appendChild(autoTx);
      row.appendChild(run); row.appendChild(no); row.appendChild(autoLab);
      card.appendChild(row);

      var statusEl = document.createElement('div');
      statusEl.style.cssText = 'margin-top:8px;font-size:12.5px;color:var(--dsw-alias-label-tertiary,#888)';
      card.appendChild(statusEl);
      host.appendChild(card);

      run.onclick = function () {
        if (autoCb.checked) st.shellAuto = true;
        run.disabled = true; no.disabled = true;
        code.style.opacity = '.6';
        execShell(block, cmd, statusEl).then(function () {
          card.removeChild(code); card.removeChild(row);
        });
      };
      no.onclick = async function () {
        run.disabled = true; no.disabled = true; card.removeChild(code); card.removeChild(row);
        statusEl.textContent = 'Отклонено: ' + cmd.slice(0, 90);
        await shellSend(cmd, 'The user REJECTED this command in the confirm card. Do NOT retry it; ' +
          'continue without it, or suggest a manual alternative in text.');
        shellBookkeep();
        finalizeBadges(block); hideActionBar(block);
      };
      console.log('[DSX] shell-карточка: «' + cmd.slice(0, 60) + '»');
    } catch (e) { console.warn('[DSX] shell card error', e); }
  }

  // ---------------- карточка-опрос (ASK / ASK MULTI): варианты → клик → ответ
  // сам уезжает модели служебным сообщением; последний вариант всегда «Свой ответ»
  function renderAskCard(block, ask) {
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var key = 'dsx-ask-' + hashStr(ask.q + '|' + (ask.opts || []).join(';')).slice(0, 24);
      if (host.querySelector('[data-key="' + key + '"]')) return;

      var card = document.createElement('div');
      card.setAttribute('data-key', key);
      card.style.cssText = 'margin-top:10px;padding:12px 14px;border-radius:12px;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));max-width:520px';

      var title = document.createElement('div');
      title.textContent = ask.q;
      title.style.cssText = 'font-size:13.5px;font-weight:600;line-height:1.45;' +
        'color:var(--dsw-alias-label-primary,#eee)';
      card.appendChild(title);

      var hint = document.createElement('div');
      hint.textContent = ask.multi ? 'отметь один или несколько вариантов' : 'выбери один вариант';
      hint.style.cssText = 'margin-top:2px;font-size:11.5px;color:var(--dsw-alias-label-tertiary,#888)';
      card.appendChild(hint);

      var list = document.createElement('div');
      list.style.cssText = 'margin-top:8px;display:flex;flex-direction:column;gap:6px';
      var options = (ask.opts || []).concat(['Свой ответ…']);
      var name = key + '-grp';
      options.forEach(function (opt, oi) {
        var lab = document.createElement('label');
        lab.style.cssText = 'display:flex;align-items:center;gap:9px;cursor:pointer;font-size:13px;' +
          'padding:7px 10px;border-radius:9px;color:var(--dsw-alias-label-secondary,#ccc);' +
          'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
          'background:transparent;transition:border-color .15s,background .15s';
        var inp = document.createElement('input');
        inp.type = ask.multi ? 'checkbox' : 'radio';
        inp.name = name;
        inp.style.cssText = 'width:14px;height:14px;accent-color:#5686fe;flex-shrink:0;margin:0';
        var sp = document.createElement('span');
        sp.textContent = opt;
        lab.appendChild(inp); lab.appendChild(sp);
        list.appendChild(lab);
      });
      card.appendChild(list);

      var customTa = document.createElement('textarea');
      customTa.rows = 2; customTa.placeholder = 'Напиши свой вариант ответа…';
      customTa.style.cssText = 'display:none;width:100%;box-sizing:border-box;margin-top:8px;' +
        'padding:8px 10px;resize:vertical;border-radius:9px;font-size:13px;line-height:1.45;' +
        'font-family:inherit;background:transparent;color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      card.appendChild(customTa);

      var btn = document.createElement('button');
      btn.textContent = 'Отправить ответ';
      btn.disabled = true;
      btn.style.cssText = 'margin-top:10px;padding:9px 16px;border:none;cursor:pointer;' +
        'border-radius:10px;font-size:13px;font-weight:600;font-family:inherit;color:#fff;' +
        'opacity:.5;background:var(--dsw-alias-brand-primary,#5686fe)';
      card.appendChild(btn);

      var inps = function () { return list.querySelectorAll('input'); };
      function isCustomChecked() { var l = inps(); return l[l.length - 1].checked; }
      function refreshBtn() {
        var l = inps(), on = false;
        for (var i = 0; i < l.length; ++i) if (l[i].checked) on = true;
        if (isCustomChecked() && !(customTa.value || '').trim()) on = false;
        btn.disabled = !on;
        btn.style.opacity = on ? '1' : '.5';
      }
      function paint() {
        var l = inps();
        for (var i = 0; i < l.length; ++i) {
          var lab = l[i].parentElement;
          if (l[i].checked) { lab.style.borderColor = '#5686fe'; lab.style.background = 'rgba(86,134,254,.12)'; }
          else { lab.style.borderColor = 'var(--dsw-alias-border-l2,rgba(255,255,255,.08))'; lab.style.background = 'transparent'; }
        }
        var cc = isCustomChecked();
        customTa.style.display = cc ? 'block' : 'none';
        if (cc) customTa.focus();
      }
      list.addEventListener('change', function () { paint(); refreshBtn(); });
      customTa.addEventListener('input', refreshBtn);

      btn.onclick = async function () {
        var l = inps(), picked = [];
        for (var i = 0; i < l.length - 1; ++i) if (l[i].checked) picked.push(options[i]);
        var customText = (customTa.value || '').trim();
        if (isCustomChecked() && customText) picked.push(customText);
        if (!picked.length) { toast('Выбери вариант или напиши свой ответ'); return; }
        btn.disabled = true; btn.style.opacity = '.5'; btn.textContent = 'Отправляю…';
        var answer = 'SYSTEM: the user answered your question (via DeepSeek Extended UI).\n' +
          'QUESTION: «' + ask.q + '»\n' +
          'ANSWER' + (ask.multi ? 'S (several picked)' : '') + ': ' +
          (ask.multi ? '\n- ' + picked.join('\n- ') : picked.join('; '));
        var ok = await injectStealth(answer, 'ответ на вопрос');
        if (!ok) { btn.disabled = false; btn.style.opacity = '1'; btn.textContent = 'Отправить ответ'; return; }
        st.autoCount++;
        st.waitingAnswer = true; sessionStorage.setItem('dsx:wait', '1');
        sessionStorage.setItem('dsx:auto', String(st.autoCount));
        card.innerHTML = '';
        var doneRow = document.createElement('div');
        doneRow.textContent = 'Ответ отправлен: ' + picked.join(', ');
        doneRow.style.cssText = 'font-size:12.5px;color:var(--dsw-alias-label-tertiary,#888)';
        card.appendChild(doneRow);
        finalizeBadges(block);
        hideActionBar(block);
      };

      host.appendChild(card);
      console.log('[DSX] ask-карточка: «' + ask.q.slice(0, 60) + '» (' +
        (ask.multi ? 'multi' : 'single') + ', опций ' + (ask.opts || []).length + ')');
    } catch (e) { console.warn('[DSX] ask card error', e); }
  }

  // у сообщения-запроса прячем экшн-бар (копировать/регенер/лайки…) — выглядит
  // как единое служебное сообщение без мусора внизу
  function hideActionBar(block) {
    try {
      var host = block.closest('._4f9bf79');
      if (!host || host.getAttribute('data-dsx-hid')) return;
      host.setAttribute('data-dsx-hid', '1');
      var bar = host.querySelector('.ds-flex._0a3d93b');
      if (bar) bar.style.display = 'none';
    } catch (e) {}
  }

  // ---------------- строки статуса под ответом DeepSeek
  // Добавляем СВОЙ контейнер в конец родного сообщения (append-only — React-safe).
  function appendAssistantRows(block, rows) {
    if (cfg.statusRows === false) return;
    if (!block || !block.isConnected) return;
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var box = host.querySelector('.dsx-status-box');
      if (!box) {
        box = document.createElement('div');
        box.className = 'dsx-status-box';
        host.appendChild(box);
      }
      box.style.cssText = 'margin-top:6px'; // перебиваем стили старых боксов (была линия)
      rows.forEach(function (t) {
        var p = document.createElement('p');
        p.className = 'dsx-row-line';
        p.textContent = t;
        p.style.cssText = 'margin:2px 0;font-size:13px;line-height:1.5;' +
          'color:var(--dsw-alias-label-tertiary,var(--dsw-alias-label-secondary,#999));' +
          'font-family:var(--dsw-font-family,inherit)';
        box.appendChild(p);
      });
    } catch (e) {}
  }

  // ---------------- ЖИВЫЕ статусы: пока модель стримит ops-блок, уже видно,
  // ЧТО она делает («Пишет: index.html…»); сама стена голого JSON сворачивается.
  var OP_VERBS = {
    write_file:   ['Пишет: ', 'Записано: '],
    edit_file:    ['Правит: ', 'Изменено: '],
    append_file:  ['Дописывает: ', 'Дописано: '],
    insert_lines: ['Вставляет в ', 'Вставлено в '],
    replace_lines:['Заменяет в ', 'Заменено в '],
    make_dir:     ['Создаёт папку ', 'Папка создана: '],
    delete_path:  ['Удаляет ', 'Удалено: '],
    copy_file:    ['Копирует ', 'Скопировано: '],
    move_file:    ['Перемещает ', 'Перемещено: ']
  };
  function opsBlockPres(block) {
    var pres = [];
    try {
      block.querySelectorAll('pre').forEach(function (pre) {
        var p = pre.parentElement, hops = 0;
        while (p && p !== block && hops < 4) {
          var t = (p.textContent || '').slice(0, 90).toLowerCase();
          if (t.indexOf('deepseekide-ops') >= 0) { pres.push(pre); break; }
          p = p.parentElement; hops++;
        }
      });
    } catch (e) {}
    return pres;
  }
  // сворачиваем <pre> с голым JSON — статус-строки расскажут всё сами
  function collapseOpsBlocks(block) {
    opsBlockPres(block).forEach(function (pre) {
      try { if (pre.style.display !== 'none') pre.style.display = 'none'; } catch (e) {}
    });
  }
  // think-блоки (```deepseekide-think```) скрываем от юзера целиком
  function hideThinkBlocks(block) {
    try {
      block.querySelectorAll('pre').forEach(function (pre) {
        var p = pre.parentElement, hops = 0;
        while (p && p !== block && hops < 4) {
          var t = (p.textContent || '').slice(0, 120).toLowerCase();
          if (t.indexOf('deepseekide-think') >= 0) {
            if (p.style.display !== 'none') p.style.display = 'none';
            return;
          }
          p = p.parentElement; hops++;
        }
      });
    } catch (e) {}
  }
  function ensureLiveBox(host) {
    var box = host.querySelector('.dsx-live-box');
    if (!box) {
      box = document.createElement('div');
      box.className = 'dsx-live-box';
      box.style.cssText = 'margin-top:8px';
      host.appendChild(box);
    }
    return box;
  }
  var LIVE_ROW_CSS = 'margin:2px 0;font-size:13px;line-height:1.5;' +
    'color:var(--dsw-alias-label-tertiary,var(--dsw-alias-label-secondary,#999));' +
    'font-family:var(--dsw-font-family,inherit)';
  function updateLiveOps(block) {
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var pres = opsBlockPres(block);
      if (!pres.length) return;
      collapseOpsBlocks(block);
      var joined = '';
      pres.forEach(function (pre) { joined += (pre.innerText || pre.textContent || '') + '\n'; });
      if (!st.liveSeen) st.liveSeen = {};
      var re = /"name"\s*:\s*"(\w+)"[\s\S]{0,300}?"(?:path|from)"\s*:\s*"([^"][^"]*?)"/g, m;
      var found = false;
      while ((m = re.exec(joined)) !== null) {
        var op = m[1], pth = m[2].replace(/\\\//g, '/').replace(/\\\\/g, '\\');
        var vb = OP_VERBS[op];
        if (!vb) continue;
        var to = null;
        if (op === 'copy_file' || op === 'move_file') {
          var tm = /"to"\s*:\s*"([^"][^"]*?)"/.exec(joined.slice(m.index, m.index + 400));
          if (tm) to = tm[1].replace(/\\\//g, '/').replace(/\\\\/g, '\\');
        }
        var label = vb[0] + base(pth) + (to ? ' → ' + base(to) : '');
        var key = op + '|' + pth + (to ? '|' + to : '');
        if (st.liveSeen[key]) continue;
        st.liveSeen[key] = 1;
        var box = ensureLiveBox(host);
        if (!box.isConnected) { host.appendChild(box); }
        var row = document.createElement('p');
        row.className = 'dsx-live-line';
        row.setAttribute('data-dsx-final', vb[1] + base(pth) + (to ? ' → ' + base(to) : ''));
        row.textContent = label + '…';
        row.style.cssText = LIVE_ROW_CSS;
        box.appendChild(row);
        found = true;
      }
      if (found) console.log('[DSX] live: правки замечены в стриме');
    } catch (e) {}
  }
  // стрим кончился, правки применены/не применены — «Пишет: x…» → «Записано: x»
  function finalizeLiveOps(block, ok) {
    try {
      var host = block && (block.closest('._4f9bf79') || block.parentElement);
      if (!host) return;
      host.querySelectorAll('.dsx-live-line').forEach(function (row) {
        var t = row.textContent || '';
        if (t.slice(-1) !== '…') return;
        var fin = row.getAttribute('data-dsx-final');
        row.textContent = ok ? fin : (t.slice(0, -1) + ' (ОШИБКА)');
      });
    } catch (e) {}
  }
  // живой тик во время стрима (троттлинг из наблюдателя)
  function liveTick() {
    // NB: waitingAnswer НЕ проверяем — стрим ответа после служебного как раз
    // идёт под этим гейтом, и именно тогда живые статусы нужны сильнее всего
    if (!cfg.pilot || !st.folderReady || st.busy) return;
    var block = lastAnswerBlock();
    if (!block || !isTailAssistant(block)) return;
    renderBadges(block);   // маркеры [<{...}>] мгновенно превращаем в «Ищет в инете: …»
    updateLiveOps(block);  // живые строки «Пишет: файл…»
    hideThinkBlocks(block); // think-блоки юзеру не видны
  }

  // ---------------- главный цикл: ответ устоялся → применить → ответить
  function hashStr(s) {
    var h = 5381;
    for (var i = 0; i < s.length; ++i) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
    return (h >>> 0).toString(36) + '.' + s.length;
  }
  // ожил ответ модели: снимаем гейт
  function noteAnswerArrived(text) {
    if (st.waitingAnswer) {
      st.waitingAnswer = false;
      sessionStorage.removeItem('dsx:wait');
      console.log('[DSX] пришёл ответ модели — гейт снят');
    }
    st.lastAnswer = text;
    try { sessionStorage.setItem('dsx:last', text); } catch (e) {}
  }
  async function settleAndHandle() {
    try {
      if (st.busy || !cfg.pilot || !st.folderReady) return;
      var block = lastAnswerBlock();
      if (!block) return;
      var paused = pauseBadges(block);          // маркеры на место — парсер их видит
      var pay = answerPayload(block);
      if (paused) resumeBadges(block);          // ...и снова в бейджи
      var text = (pay.text || '').trim();
      // дедуп — ТОЛЬКО тот же блок с тем же текстом (перерисовка/скролл).
      // Если юзер попросил «ещё раз» и модель ответила дословно так же — это
      // НОВЫЙ блок: обрабатываем заново, иначе правки никогда не повторятся.
      if (text.length > 80 && st.lastBlock === block && text === (st.lastAnswer || '').trim()) return;
      st.lastBlock = block;
      st.busy = true;
      noteAnswerArrived(text);
      try {
        var answHash = hashStr(text);
        var tail = isTailAssistant(block);

        var outp = { ops: [], errors: [] };
        if (pay.obs.length) pay.obs.forEach(function (b) { DsideOps.parseOpsBody(b, outp); });
        else DsideOps.extractOps(text, outp);

        var rows = [], note = '';
        // --- правки: применяем ОДИН раз на ответ (хэш переживает перезагрузку)
        var vKey = '';
        try {
          var vh = block.closest('[data-virtual-list-item-key]');
          vKey = vh ? (vh.getAttribute('data-virtual-list-item-key') || '') : '';
        } catch (e) {}
        if (outp.ops.length && (answHash !== st.opsMeta.h || vKey !== st.opsMeta.k)) {
          st.opsMeta = { h: answHash, k: vKey };
          sessionStorage.setItem('dsx:opsh', JSON.stringify(st.opsMeta));
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
              ? 'Применено: ' + r.data.done + ' из ' + r.data.total + ' операций'
              : 'Применено ' + r.data.done + ' из ' + r.data.total + ' — есть ошибки';
            toast(sum);
            rows.push(sum);
            finalizeLiveOps(block, true);
            note = 'SYSTEM: ops applied automatically by DeepSeek Extended:\n\n```\n' +
                   r.data.report + '\n```\n';
            refreshPrompt(); // дерево изменилось
          } else {
            toast('Ошибка применения: ' + (r.error || '?'));
            rows.push('Ошибка применения правок');
            finalizeLiveOps(block, false);
          }
        }
        // --- запросы контекста ([<{...}>] и легаси NEED FILE/SEARCH): обслуживаем,
        // только если ответ — ХВОСТ беседы (иначе файлы уже отправлены раньше
        // и лежат в истории сервера)
        var svcReq = false;
        if (tail) {
          renderBadges(block); // маркеры сразу превращаем в «Чтение: x…»
          hideThinkBlocks(block);
          var svc = await serviceNote(text, function (pending) {
            // строка прогресса прямо под сообщением модели, живёт секунды-секунды
            try {
              var host = block.closest('._4f9bf79') || block.parentElement;
              if (!host) return function () {};
              var boxL = ensureLiveBox(host);
              var row = document.createElement('p');
              row.className = 'dsx-live-line dsx-svc-live';
              row.textContent = pending;
              row.style.cssText = LIVE_ROW_CSS;
              boxL.appendChild(row);
              return function (fin) { row.textContent = fin; };
            } catch (e) { return function () {}; }
          });
          if (svc.text) {
            svcReq = true;
            note = note ? note + '\n\n' + svc.text : svc.text;
            rows = svc.rows.concat(rows);
          }
          if (svc.asks && svc.asks.length) {
            svc.asks.forEach(function (a) { renderAskCard(block, a); });
            hideActionBar(block); // у сообщения с вопросом экшн-бар тоже прячем
          }
          if (svc.shells && svc.shells.length) {
            svc.shells.forEach(function (s) { runShellCmd(block, s.cmd); });
            hideActionBar(block); // и у сообщения с командами
          }
        }

        if (note) {
          if (st.autoCount >= 12) {
            toast('Потолок авто-сообщений (12) — ваш ход');
          } else if (await injectStealth(note, 'служебное')) {
            if (svcReq) { finalizeBadges(block); hideActionBar(block); }
            st.autoCount++;
            // служебная ушла на сервер: отсюда и до ответа модели любые
            // копии этого NEED-дождя больше не слушаем (иначе повторная
            // отправка при нашем же пузыре внизу ленты)
            st.waitingAnswer = true;
            sessionStorage.setItem('dsx:wait', '1');
            sessionStorage.setItem('dsx:auto', String(st.autoCount));
            if (!st.reloadHint) { // сторожок: вдруг вкладку заглушили
              st.reloadHint = setTimeout(function () {
                if (st.waitingAnswer) {
                  console.log('[DSX] ответа нет давно — можно «продолжи»');
                }
              }, 300000);
            }
          }
        }
        if (rows.length) appendAssistantRows(block, rows);
        if (outp.errors.length) console.warn('[DSX] ops warnings:', outp.errors);
      } finally { st.busy = false; }
    } catch (e) {
      st.busy = false;
      console.warn('[DSX] settle error (сайт не тронут):', e);
    }
  }

  // ---------------- НАТИВНАЯ пилюля «Авто-пилот» + ШЕСТЕРЁНКА настроек
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
  // шестерёнка — В ПРАВОМ кластере (.bf38813a), непосредственно слева от
  // скрепки (attacher). Точная копия разметки icon-кнопки сайта: Никакого
  // аутлайна-пилюли, обычный значок, как у кнопки аттача.
  function ensureGearButton() {
    var cluster = document.querySelector('.bf38813a');
    // миграция: если старая шестерёнка осталась в левом ряду — сносим
    document.querySelectorAll('._58b31c9 .dsx-gear-btn').forEach(function (n) { n.remove(); });
    if (!cluster) return;
    if (cluster.querySelector('.dsx-gear-btn')) return;
    var g = document.createElement('div');
    g.setAttribute('role', 'button');
    g.className = 'ds-button ds-button--iconLabelPrimary ds-button--icon ds-button--capsule ' +
                  'ds-button--s ds-button--icon-relative-m dsx-gear-btn';
    g.tabIndex = 0;
    g.title = 'DeepSeek Extended — настройки';
    g.style.cssText = '--dsl-button-height: 34px;';
    g.innerHTML =
      '<div class="ds-button__background"></div>' +
      '<div class="ds-button__icon ds-button__icon--last-child">' +
      '<div class="ds-icon" style="font-size: inherit;">' +
      '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">' +
      '<path d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 0 0-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 0 0-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 0 0-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 0 0-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 0 0 1.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<path d="M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>' +
      '</svg></div></div>';
    g.addEventListener('click', showSettingsModal);
    g.addEventListener('keydown', function (e) { if (e.key === 'Enter' || e.key === ' ') g.click(); });
    cluster.insertBefore(g, cluster.firstChild); // левее скрепки
  }
  setInterval(function () { ensureNativeToggle(); ensureGearButton(); }, 1400);

  // ---------------- сторож URL (новая беседа = новый прайм)
  setInterval(function () {
    if (location.href !== st.lastUrl) {
      st.lastUrl = location.href;
      st.primedOnce = false; st.lastAnswer = ''; st.lastBlock = null; st.opsMeta = { h: '', k: '' };
      st.sentFiles = {}; st.sentSearches = {}; st.sentInfos = {}; st.sentRanges = {}; st.sentWeb = {}; st.sentDl = {}; st.sentGrep = {}; sentLoad(); // дедуп новой беседы
      st.liveSeen = {};
      st.autoCount = 0; sessionStorage.setItem('dsx:auto', '0');
      sessionStorage.removeItem('dsx:primed');
      sessionStorage.removeItem('dsx:opsh');
      armReloadResume(); // история новой беседы дорисуется — проверить хвост
    }
  }, 1500);

  // ---------------- наблюдатель: бритва (троттлинг) + ответы (дебаунс)
  var debounce = 0, trimTimer = 0, lastTrimRun = 0;
  function scheduleTrim() {
    if (trimTimer) return;
    var wait = Math.max(50, 120 - (Date.now() - lastTrimRun));
    trimTimer = setTimeout(function () {
      trimTimer = 0; lastTrimRun = Date.now();
      try { trimStealthNodes(); } catch (e) {}
    }, wait);
  }
  var liveTimer = 0;
  new MutationObserver(function () {
    scheduleTrim();
    if (!liveTimer) liveTimer = setTimeout(function () { liveTimer = 0; try { liveTick(); } catch (e) {} }, 650);
    clearTimeout(debounce);
    debounce = setTimeout(settleAndHandle, 1600);
  }).observe(document.body, { childList: true, subtree: true, characterData: true });
  setInterval(function () { try { trimStealthNodes(); } catch (e) {} }, 900);

  // ---------------- авто-возобновление после перезагрузки страницы
  // Если хвост беседы — необслуженный ответ модели (она просила файлы,
  // а страница рухнула/перезагрузилась) — цикл подхватит settleAndHandle.
  // Если хвост — НАШЕ служебное сообщение без ответа — тихая подсказка.
  // ВНИМАНИЕ: запускать ТОЛЬКО когда виртуальный список дорисовал историю
  // (число строк стабильно ~2 сек), иначе хвостом может оказаться СТАРЫЙ
  // ответ с NEED FILE и уедет дубль служебного сообщения.
  function reloadResume() {
    if (!cfg.pilot) return;
    try {
      var msgs = document.querySelectorAll('[data-virtual-list-item-key]');
      if (!msgs.length) return;
      var last = msgs[msgs.length - 1];
      var isUserTail = String(last.className).indexOf('_9663006') >= 0;
      if (!isUserTail) {
        if (last.querySelector('._9663006')) return; // ассистент-блок содержит юзера? страховка
      }
      if (isUserTail) {
        if (!st.reloadHint) {
          st.reloadHint = 1;
          toast('Ответ модели оборвался — напиши «продолжи»');
          console.log('[DSX] хвост беседы — сообщение без ответа (после перезагрузки)');
        }
      } else if (st.folderReady) {
        console.log('[DSX] авто-возобновление: хвост — ответ модели, проверяю NEED/правки…');
        settleAndHandle();
      }
    } catch (e) {}
  }
  // сторожок стабильности истории: ждём, пока ds-virtual-list перестанет
  // догружаться (счётчик строк не меняется две проверки подряд), потом — один раз.
  var rrPrev = -1, rrSame = 0, rrTicks = 0, rrArmed = true;
  function armReloadResume() { rrPrev = -1; rrSame = 0; rrTicks = 0; rrArmed = true; }
  setInterval(function () {
    if (!rrArmed) return;
    var n = document.querySelectorAll('[data-virtual-list-item-key]').length;
    if (n > 0 && n === rrPrev) rrSame++; else { rrSame = 0; rrPrev = n; }
    if (++rrTicks > 25 || rrSame >= 2) { rrArmed = false; reloadResume(); }
  }, 1000);

  // ---------------- старт
  if (sessionStorage.getItem('dsx:primed') === '1') st.primedOnce = true;
  console.log('[DSX] DeepSeek Extended v32 стелс: загружено. pilot=' + cfg.pilot);
  refreshPrompt();
  setTimeout(refreshPrompt, 1200);
  setInterval(function () {
    if (!st.bigPrompt || !st.folderReady) refreshPrompt();
  }, 6000);
})();
