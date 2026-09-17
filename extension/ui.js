/* =====================================================================
 * ui.js — MAIN world: весь оверлей DeepSeek Extended (рельс, панель с
 * вкладками Файлы/Операции/Снапшоты/Настройки, таблетка-статус).
 * Content script (isolated world) складывает файлы на страницу и общается
 * с UI командами через postMessage:
 *   { __dsideui:'cmd', name, payload }
 * Команды: status(text,state), row(cls,text), feed(reportText), log(text),
 *          refreshFiles(treeText), refreshSnaps(list), folder(name),
 *          preview(text), note(text) — показ «послать ли в чат?»
 * ===================================================================== */
(function () {
  'use strict';
  if (window.__DSIDE_UI) return; window.__DSIDE_UI = true;

  // ---------------- DOM-скелет
  var rail = document.createElement('div'); rail.id = 'dsx-rail';
  rail.innerHTML = '<div class="gem">✦</div><div class="lbl">DSX</div><div class="caret">❮</div>';
  document.documentElement.appendChild(rail);

  var panel = document.createElement('div'); panel.id = 'dsx-panel';
  panel.innerHTML =
    '<div class="hd"><span class="ttl">✦ DeepSeek Extended</span>' +
    '<span class="sub" id="dsx-sub">агент-проекты</span>' +
    '<span class="close" title="Скрыть (Ctrl+Shift+X)">✕</span></div>' +
    '<div class="tabs">' +
    '<div class="tb on" data-tab="feed">Операции</div>' +
    '<div class="tb" data-tab="files">Файлы</div>' +
    '<div class="tb" data-tab="snaps">Снапшоты</div>' +
    '<div class="tb" data-tab="conf">Настройки</div></div>' +
    '<div class="body"><div id="dsx-feed"></div><div id="dsx-files" style="display:none"></div>' +
    '<div id="dsx-snaps" style="display:none"></div><div id="dsx-conf" style="display:none"></div></div>';
  document.documentElement.appendChild(panel);

  var pill = document.createElement('div'); pill.id = 'dsx-pill';
  pill.innerHTML =
    '<div class="top"><span class="dot" id="dsx-dot"></span><span>DeepSeek Extended</span></div>' +
    '<div class="st" id="dsx-st">загружается…</div>' +
    '<div class="btns">' +
    '<button class="dsx-btn" id="dsx-b-prime">⟡ Прайм</button>' +
    '<button class="dsx-btn" id="dsx-b-pilot">⏸ Пауза</button>' +
    '<button class="dsx-btn ghost" id="dsx-b-open">Панель ▣</button></div>';
  document.documentElement.appendChild(pill);

  // ---------------- поведение каркаса
  var curTab = 'feed';
  function togglePanel(force) {
    var open = force !== undefined ? force : !panel.classList.contains('open');
    panel.classList.toggle('open', open);
    rail.style.display = open ? 'none' : 'flex';
  }
  rail.onclick = function () { togglePanel(true); };
  panel.querySelector('.close').onclick = function () { togglePanel(false); };
  document.addEventListener('keydown', function (e) {
    if (e.ctrlKey && e.shiftKey && (e.key === 'X' || e.key === 'x' || e.code === 'KeyX')) togglePanel();
  }, true);
  panel.querySelectorAll('.tb').forEach(function (tb) {
    tb.onclick = function () {
      curTab = tb.dataset.tab;
      panel.querySelectorAll('.tb').forEach(function (x) { x.classList.toggle('on', x === tb); });
      ['feed', 'files', 'snaps', 'conf'].forEach(function (n) {
        document.getElementById('dsx-' + n).style.display = n === curTab ? 'block' : 'none';
      });
      if (curTab === 'snaps') emit('needSnaps');
      if (curTab === 'files') emit('needFiles');
    };
  });
  document.getElementById('dsx-b-open').onclick = function () { togglePanel(true); };

  // ---------------- каналы: content → ui (cmd), ui → content (ctl)
  function emit(name, payload) {
    window.postMessage({ __dsideui: 'ctl', name: name, payload: payload }, '*');
  }
  function on(name, fn) { (H[name] = fn); }
  var H = {};
  window.addEventListener('message', function (ev) {
    var m = ev.data;
    if (!m || m.__dsideui !== 'cmd') return;
    if (H[m.name]) H[m.name](m.payload);
  });

  document.getElementById('dsx-b-prime').onclick = function () { emit('prime'); };
  document.getElementById('dsx-b-pilot').onclick = function () { emit('pilot'); };

  // ---------------- handlers
  on('status', function (p) {
    var st = document.getElementById('dsx-st'), dot = document.getElementById('dsx-dot');
    st.textContent = p.text;
    dot.className = 'dot ' + (p.state === 'work' ? 'work' : p.state === 'on' ? 'on' : '');
  });
  on('pilot', function (p) {
    document.getElementById('dsx-b-pilot').textContent = p.on ? '⏸ Пауза' : '▶ Пуск';
  });
  on('folder', function (p) {
    document.getElementById('dsx-sub').textContent = p.name ? 'проект: ' + p.name : 'папка не выбрана';
    var cf = document.getElementById('dsx-conf');
    if (cf.__folderEl) cf.__folderEl.textContent = 'Папка: ' + (p.name || 'не выбрана');
  });

  var feedEl = document.getElementById('dsx-feed');
  on('row', function (p) {
    var d = document.createElement('div');
    d.className = 'op ' + (p.cls || 'ok');
    d.textContent = p.text;
    feedEl.prepend(d);
    while (feedEl.children.length > 60) feedEl.removeChild(feedEl.lastChild);
  });
  on('feed', function (p) {
    // пакетный отчёт из строк '✓/✗ ...' → ряды с анимацией каскадом
    String(p.text || '').split('\n').filter(Boolean).forEach(function (line, i) {
      setTimeout(function () {
        var cls = line[0] === '✓' ? 'ok' : line[0] === '✗' ? 'bad' : 'ok';
        H.row({ cls: cls, text: line.replace(/^[✓✗] ?/, '') });
      }, 60 * i);
    });
  });

  // ---------- вкладка Файлы
  var filesEl = document.getElementById('dsx-files');
  function renderFilesHeading(name) {
    filesEl.innerHTML =
      '<div style="display:flex;gap:8px;align-items:center;margin-bottom:8px">' +
      '<button class="dsx-btn" id="dsx-b-pick">📂 ' + (name ? 'Сменить папку' : 'Выбрать папку проекта…') + '</button>' +
      (name ? '<button class="dsx-btn ghost" id="dsx-b-refresh">↻</button>' : '') + '</div>' +
      '<div class="dsx-note" style="margin-bottom:8px">Клик по файлу — предпросмотр первых строк.</div>' +
      '<div id="dsx-tree"></div><div class="preview" id="dsx-preview" style="display:none"></div>';
    var pickBtn = document.getElementById('dsx-b-pick');
    if (pickBtn) pickBtn.onclick = function () { emit('pick'); };
    var refBtn = document.getElementById('dsx-b-refresh');
    if (refBtn) refBtn.onclick = function () { emit('needFiles'); };
  }
  renderFilesHeading(null);
  on('folder2', function (p) { renderFilesHeading(p.name); });
  on('refreshFiles', function (p) {
    var tree = document.getElementById('dsx-tree');
    if (!tree) return;
    tree.innerHTML = '';
    var lines = String(p.text || '').split('\n');
    if (lines[0] && lines[0].indexOf('(проект:') === 0) lines.shift();
    lines.slice(0, 420).forEach(function (ln, i) {
      var d = document.createElement('div');
      d.className = 'node dsx-row';
      d.style.animationDelay = Math.min(i * 4, 240) + 'ms';
      var m = ln.match(/^(\s*)(📁|📄)\s*(.*)$/);
      d.dataset.rel = (d.__path = m ? m[3] : ln);
      d.dataset.dir = m && m[2] === '📁' ? '1' : '';
      d.textContent = ln;
      d.onclick = function () {
        if (d.dataset.dir === '1') return;
        tree.querySelectorAll('.node.sel').forEach(function (x) { x.classList.remove('sel'); });
        d.classList.add('sel');
        emit('preview', d.__path);
      };
      tree.appendChild(d);
      // склейка относительного пути из отступов дерева
      var depth = m ? Math.floor(m[1].length / 2) : 0;
      var parts = [d.__path], node = d, lvl = depth;
      while (lvl > 0 && node) {
        node = node.previousElementSibling;
        if (node && node.__depth !== undefined && node.__depth === lvl - 1) { parts.unshift(node.__name); lvl--; }
      }
      d.__depth = depth; d.__name = d.__path;
      d.__path = parts.join('/');
    });
  });
  on('preview', function (p) {
    var pv = document.getElementById('dsx-preview');
    if (!pv) return;
    pv.style.display = 'block';
    pv.textContent = p.text;
  });

  // ---------- вкладка Снапшоты
  on('refreshSnaps', function (p) {
    var el = document.getElementById('dsx-snaps');
    el.innerHTML = '<div class="dsx-note" style="margin-bottom:8px">' +
      'Перед каждым пакетом правок агент копирует затрагиваемые файлы. Откат — одним кликом.</div>';
    (p.list || []).forEach(function (s, i) {
      var d = document.createElement('div');
      d.className = 'dsx-snap dsx-row';
      d.style.animationDelay = (i * 40) + 'ms';
      d.innerHTML = '<span>📸</span><span class="meta"><div class="n">' + s.label + '</div>' +
        '<div class="t">' + s.when + ' · файлов: ' + s.count + '</div></span>' +
        '<button class="dsx-btn warn">Откатить</button>';
      d.querySelector('button').onclick = function () {
        if (confirm('Откатить «' + s.label + '» (' + s.when + ')?')) {
          d.style.opacity = '.5';
          emit('rollback', s.key);
        }
      };
      el.appendChild(d);
    });
    if (!(p.list || []).length) {
      var d2 = document.createElement('div'); d2.className = 'dsx-note';
      d2.textContent = 'Снапшотов пока нет — появятся после первых правок агента.';
      el.appendChild(d2);
    }
  });

  // ---------- вкладка Настройки
  (function () {
    var cf = document.getElementById('dsx-conf');
    cf.innerHTML =
      '<label class="dsx-tgl"><input type="checkbox" id="dsx-c-pilot" checked>' +
      '<span class="sw"></span><span>Автопилот (читать ответы, применять ops, отвечать модели)</span></label>' +
      '<label class="dsx-tgl"><input type="checkbox" id="dsx-c-note" checked>' +
      '<span class="sw"></span><span>Автозаметки в чат (SYSTEM: отчёты и содержимое по NEED FILE/SEARCH)</span></label>' +
      '<label class="dsx-tgl"><input type="checkbox" id="dsx-c-anim" checked>' +
      '<span class="sw"></span><span>Анимации интерфейса</span></label>' +
      '<div class="dsx-note" style="margin:10px 0">Папка: <span id="dsx-c-folder">не выбрана</span></div>' +
      '<button class="dsx-btn" id="dsx-b-pick2">📂 Выбрать папку проекта…</button>' +
      '<div class="dsx-note" style="margin-top:14px">' +
      'DeepSeek Extended v1.0. Терминал в браузерной песочнице недоступен — ' +
      'команды сборки модель просто напишет вам текстом, всё остальное делает сама.</div>';
    cf.__folderEl = cf.querySelector('#dsx-c-folder');
    cf.querySelector('#dsx-b-pick2').onclick = function () { emit('pick'); };
    cf.querySelectorAll('input').forEach(function (inp) {
      inp.onchange = function () {
        emit('conf', { pilot: cf.querySelector('#dsx-c-pilot').checked,
                       note: cf.querySelector('#dsx-c-note').checked,
                       anim: cf.querySelector('#dsx-c-anim').checked });
      };
    });
  })();
  on('conf', function (p) {
    document.querySelector('#dsx-c-pilot').checked = !!p.pilot;
    document.querySelector('#dsx-c-note').checked = !!p.note;
    document.querySelector('#dsx-c-anim').checked = !!p.anim;
  });
})();
