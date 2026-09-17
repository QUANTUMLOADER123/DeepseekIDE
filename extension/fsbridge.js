/* =====================================================================
 * fsbridge.js — МОСТ К ФАЙЛОВОЙ СИСТЕМЕ (MAIN world страницы).
 * Content script не вызывает File System Access пикеры из своего мира —
 * поэтому этот скрипт инжектится в страницу и слушает window.postMessage:
 *   { __dsidefs: 'req', id, op, args }  →  { __dsidefs: 'resp', id, ok, data, error }
 * Хэндл папки проекта хранится в IndexedDB (origin chat.deepseek.com) и
 * перевыпрашивается ОДНИМ кликом после перезапуска браузера.
 * ===================================================================== */
(function () {
  'use strict';
  if (window.__dsidefsReady) return;
  window.__dsidefsReady = true;

  var dirHandle = null;
  var dirName = '';

  // ---------- IndexedDB (крошечная обёртка)
  function idb(mode, fn) {
    return new Promise(function (resolve, reject) {
      var rq = indexedDB.open('dside-extended', 1);
      rq.onupgradeneeded = function () {
        rq.result.createObjectStore('kv');
        rq.result.createObjectStore('snaps');
      };
      rq.onsuccess = function () {
        var tx = rq.result.transaction(['kv', 'snaps'], mode);
        fn(tx, resolve, reject);
      };
      rq.onerror = function () { reject(rq.error); };
    });
  }
  function idbSet(store, key, val) {
    return idb('readwrite', function (tx, res) {
      tx.objectStore(store).put(val, key);
      tx.oncomplete = res;
    });
  }
  function idbGet(store, key) {
    return idb('readonly', function (tx, res, rej) {
      var g = tx.objectStore(store).get(key);
      g.onsuccess = function () { res(g.result); };
      g.onerror = function () { rej(g.error); };
    });
  }
  function idbAll(store) {
    return idb('readonly', function (tx, res) {
      var g = tx.objectStore(store).getAll();
      var k = tx.objectStore(store).getAllKeys();
      var out = [];
      g.onsuccess = function () {
        var keys = k.result, vals = g.result;
        for (var i = 0; i < vals.length; ++i) out.push({ key: keys[i], val: vals[i] });
        res(out);
      };
    });
  }

  // ---------- хэндл папки
  async function pick() {
    dirHandle = await window.showDirectoryPicker({ mode: 'readwrite' });
    dirName = dirHandle.name;
    await idbSet('kv', 'dirHandle', dirHandle);
    return { name: dirName };
  }
  async function ensure() {
    if (!dirHandle) {
      try {
        var saved = await idbGet('kv', 'dirHandle');
        if (saved) {
          var perm = await saved.queryPermission({ mode: 'readwrite' });
          if (perm !== 'granted') perm = await saved.requestPermission({ mode: 'readwrite' });
          if (perm === 'granted') { dirHandle = saved; dirName = saved.name; }
        }
      } catch (e) { /* проигнорировано: попросим заново */ }
    }
    return dirHandle ? { ok: true, name: dirName } : { ok: false, needPick: true };
  }

  // ---------- утилиты путей
  function partsOf(rel) {
    return String(rel).replace(/\\/g, '/').replace(/^\/+/, '').split('/').filter(Boolean);
  }
  async function dirAt(parts, create) {
    var h = dirHandle;
    for (var i = 0; i < parts.length; ++i) {
      h = await h.getDirectoryHandle(parts[i], { create: !!create });
    }
    return h;
  }
  async function parentOf(rel, create) {
    var parts = partsOf(rel);
    if (!parts.length) throw new Error('пустой путь');
    parts.pop();
    return parts.length ? dirAt(parts, create) : dirHandle;
  }
  function leaf(rel) {
    var p = partsOf(rel);
    return p[p.length - 1];
  }
  async function readText(rel) {
    var d = await parentOf(rel, false);
    var fh = await d.getFileHandle(leaf(rel));
    var f = await fh.getFile();
    var buf = await f.arrayBuffer();
    return new TextDecoder('utf-8', { fatal: false }).decode(buf);
  }
  async function writeText(rel, text) {
    var d = await parentOf(rel, true);
    var fh = await d.getFileHandle(leaf(rel), { create: true });
    var w = await fh.createWritable();
    await w.write(new Blob([text], { type: 'text/plain;charset=utf-8' }));
    await w.close();
  }
  function pad5(n) { var s = String(n); while (s.length < 5) s = ' ' + s; return s; }

  // ---------- файловые операции (отвечаем TeM же текстом, что C++ Describe)
  var OPS = {
    readNumbered: async function (args, tools) {
      var text = await readText(args.path);
      var lines = text.split('\n'), out = [];
      var start = args.start_line || 1, end = args.end_line || 1 << 30, shown = 0;
      for (var i = 1; i <= lines.length && shown < 4000; ++i) {
        if (i < start || i > end) continue;
        out.push(pad5(i) + ' | ' + lines[i - 1].replace(/\r$/, ''));
        ++shown;
      }
      return { text: out.join('\n'), lines: lines.length, bytes: text.length };
    },

    tree: async function (args) {
      var max = (args && args.max_entries) || 400, out = [], count = 0;
      var SKIP = { '.git': 1, 'node_modules': 1, '.deepseekide': 1 };
      async function walk(d, prefix, depth) {
        if (count >= max || depth > 8) return;
        for await (var e of d.values()) {
          if (count >= max) return;
          if (SKIP[e.name]) continue;
          ++count;
          out.push(prefix + (e.kind === 'directory' ? '📁 ' : '📄 ') + e.name);
          if (e.kind === 'directory') await walk(e, prefix + '  ', depth + 1);
        }
      }
      await walk(dirHandle, '', 0);
      return { text: '(проект: ' + dirName + ', показано ' + count + ' записей)\n' + out.join('\n') };
    },

    search: async function (args) {
      var needle = args.case_sensitive ? args.query : String(args.query).toLowerCase();
      var out = [], matches = 0, scanned = 0;
      var SKIP = { '.git': 1, 'node_modules': 1, '.deepseekide': 1 };
      var TEXT_EXT = /\.(txt|md|js|ts|jsx|tsx|json|html|css|py|cpp|c|h|hpp|java|go|rs|bat|sh|yaml|yml|xml|ini|cfg|sql)$/i;
      async function walk(d, prefix) {
        if (matches >= 200 || scanned > 400) return;
        for await (var e of d.values()) {
          if (matches >= 200 || scanned > 400) return;
          if (SKIP[e.name]) continue;
          if (e.kind === 'directory') { await walk(e, prefix + e.name + '/'); continue; }
          if (!TEXT_EXT.test(e.name)) continue;
          ++scanned;
          var f = await e.getFile();
          if (f.size > 1024 * 512) continue;
          var text = await f.text();
          var lines = text.split('\n');
          for (var i = 0; i < lines.length && matches < 200; ++i) {
            var hay = args.case_sensitive ? lines[i] : lines[i].toLowerCase();
            if (hay.indexOf(needle) >= 0) {
              out.push(prefix + e.name + ':' + (i + 1) + ': ' + lines[i].slice(0, 200));
              ++matches;
            }
          }
        }
      }
      await walk(dirHandle, '');
      return { text: out.join('\n') || '(совпадений нет)', matches: matches, scanned: scanned };
    },

    exists: async function (args) {
      try { await readText(args.path); return { exists: true }; } catch (e) { return { exists: false }; }
    }
  };

  // ---------- снапшоты: копия старого содержимого ДО мутации (IDB, 25 штук)
  async function snapBegin(label) {
    var id = Date.now() + '-' + Math.floor(Math.random() * 1e6);
    var entry = { ts: Date.now(), label: label || 'agент правки', items: [] };
    window.__dsideCurSnap = entry;
    return id;
  }
  async function snapRecordBefore(rel, kind, oldText) {
    var cur = window.__dsideCurSnap;
    if (!cur) return;
    cur.items.push({ path: rel, kind: kind, oldText: oldText });
  }
  async function snapCommit() {
    var cur = window.__dsideCurSnap;
    window.__dsideCurSnap = null;
    if (!cur || !cur.items.length) return;
    await idbSet('snaps', cur.ts + '-' + Math.floor(Math.random() * 1e6), cur);
    // стрижка до 25
    var all = await idbAll('snaps');
    if (all.length > 25) {
      all.sort(function (a, b) { return a.val.ts - b.val.ts; });
      var extra = all.length - 25;
      for (var i = 0; i < extra; ++i) {
        await idb('readwrite', function (tx, res) {
          tx.objectStore('snaps').delete(all[i].key);
          tx.oncomplete = res;
        });
      }
    }
  }

  // ---------- одна мутация (имена как у C++ ToolRegistry)
  async function mutate(name, args) {
    var rel = window.DsideOps.sanitizeRel(args.path || args.from || '');
    if (!rel) throw new Error('недопустимый путь');
    if (name === 'write_file' || name === 'append_file') {
      var prev = '', had = true;
      try { prev = await readText(rel); } catch (e) { had = false; }
      await snapRecordBefore(rel, had ? 'modified' : 'created', had ? prev : null);
      await writeText(rel, had && name === 'append_file' ? prev + String(args.content) : String(args.content));
      return (had ? 'write ' : 'create ') + rel;
    }
    if (name === 'edit_file') {
      var text = await readText(rel);
      var oldS = String(args.old_string), newS = String(args.new_string);
      if (!oldS) throw new Error('old_string пуст');
      var idx = text.indexOf(oldS);
      if (idx < 0) throw new Error('фрагмент не найден (прочитайте файл и пришлите точный текст)');
      await snapRecordBefore(rel, 'modified', text);
      text = args.replace_all ? text.split(oldS).join(newS) : text.slice(0, idx) + newS + text.slice(idx + oldS.length);
      await writeText(rel, text);
      return 'edit ' + rel;
    }
    if (name === 'insert_lines' || name === 'replace_lines') {
      var t3 = await readText(rel);
      var out = name === 'insert_lines'
        ? window.DsideOps.insertLines(t3, args.line | 0, String(args.content))
        : window.DsideOps.replaceLines(t3, args.start_line | 0, args.end_line | 0, String(args.content));
      if (out === null) throw new Error('диапазон строк вне файла');
      await snapRecordBefore(rel, 'modified', t3);
      await writeText(rel, out);
      return (name === 'insert_lines' ? 'insert ' : 'replace ') + rel;
    }
    if (name === 'make_dir') {
      await dirAt(partsOf(rel), true);
      await snapRecordBefore(rel, 'created-dir', null);
      return 'mkdir ' + rel;
    }
    if (name === 'delete_path') {
      var d = await parentOf(rel, false);
      var wantFile = true, old = null;
      try { old = await readText(rel); } catch (e) { wantFile = false; }
      if (!wantFile && !args.recursive) throw new Error('это папка — нужно recursive:true');
      await snapRecordBefore(rel, wantFile ? (old !== null ? 'deleted' : 'missing') : 'deleted-dir', old);
      await d.removeEntry(leaf(rel), { recursive: !!args.recursive });
      return 'delete ' + rel;
    }
    if (name === 'copy_file' || name === 'move_file') {
      var from = window.DsideOps.sanitizeRel(args.from);
      var to = window.DsideOps.sanitizeRel(args.to);
      if (!from || !to) throw new Error('from/to некорректны');
      var src = await readText(from);
      try { await readText(to); if (name === 'copy_file') throw new Error('цель уже существует: ' + to); }
      catch (e) { if (String(e.message).indexOf('цель') === 0) throw e; }
      await snapRecordBefore(to, 'created', null);
      await writeText(to, src);
      if (name === 'move_file') {
        var pd = await parentOf(from, false);
        await snapRecordBefore(from, 'deleted', src);
        await pd.removeEntry(leaf(from));
      }
      return name === 'copy_file' ? 'copy ' + from + ' → ' + to : 'move ' + from + ' → ' + to;
    }
    throw new Error('неизвестная операция: ' + name);
  }

  OPS.mutateBatch = async function (args) {
    // args: { label, ops: [{name,args}] } — всё под ОДНИМ снапшотом.
    await snapBegin(args.label);
    var report = [], okCount = 0;
    for (var i = 0; i < args.ops.length; ++i) {
      var op = args.ops[i];
      try {
        var msg = await mutate(op.name, op.args || {});
        report.push('✓ ' + msg);
        ++okCount;
      } catch (e) {
        report.push('✗ ' + op.name + ' ' + (op.args && (op.args.path || op.args.from) || '') +
                    ' — ' + (e && e.message ? e.message : e));
      }
    }
    await snapCommit();
    report.push('Готово: ' + okCount + ' из ' + args.ops.length +
                ' операций успешно.' + (okCount < args.ops.length ? ' Остальные пропущены.' : ''));
    return { done: okCount, total: args.ops.length, report: report.join('\n') };
  };

  OPS.snapList = async function () {
    var all = await idbAll('snaps');
    all.sort(function (a, b) { return b.val.ts - a.val.ts; });
    return { list: all.slice(0, 25).map(function (x) {
      var d = new Date(x.val.ts);
      return { key: x.key, ts: x.val.ts, label: x.val.label, count: x.val.items.length,
               when: d.toLocaleDateString() + ' ' + d.toLocaleTimeString().slice(0, 5) };
    }) };
  };

  OPS.rollback = async function (args) {
    var all = await idbAll('snaps');
    var hit = null;
    for (var i = 0; i < all.length; ++i) if (all[i].key === args.key) hit = all[i];
    if (!hit) throw new Error('снапшот не найден');
    var res = [];
    var items = hit.val.items.slice().reverse();  // обратный порядок
    for (var j = 0; j < items.length; ++j) {
      var it = items[j];
      try {
        if (it.kind === 'created' || it.kind === 'created-dir') {
          var d = await parentOf(it.path, false);
          await d.removeEntry(leaf(it.path), { recursive: it.kind === 'created-dir' });
        } else if (it.oldText !== null && it.oldText !== undefined) {
          await writeText(it.path, it.oldText);
        }
        res.push('✓ ' + it.path);
      } catch (e) { res.push('✗ ' + it.path + ' — ' + (e && e.message ? e.message : e)); }
    }
    await idb('readwrite', function (tx, r) {
      tx.objectStore('snaps').delete(args.key); tx.oncomplete = r;
    });
    return { report: res.join('\n') };
  };

  // ---------- диспетчер сообщений
  window.addEventListener('message', async function (ev) {
    var m = ev.data;
    if (!m || m.__dsidefs !== 'req') return;
    function reply(payload) {
      payload.__dsidefs = 'resp'; payload.id = m.id;
      window.postMessage(payload, '*');
    }
    try {
      if (m.op === 'pick') { reply({ ok: true, data: await pick() }); return; }
      if (m.op === 'ensure') { reply({ ok: true, data: await ensure() }); return; }
      var st = await ensure();
      if (!st.ok) { reply({ ok: false, error: 'needPick' }); return; }
      if (OPS[m.op]) { reply({ ok: true, data: await OPS[m.op](m.args || {}) }); return; }
      reply({ ok: false, error: 'unknown op: ' + m.op });
    } catch (e) {
      reply({ ok: false, error: String(e && e.message ? e.message : e) });
    }
  });
})();
