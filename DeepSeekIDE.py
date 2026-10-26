#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ======================================================================
# DeepSeekIDE.py — DeepSeek как десктоп-IDE в ОДНОМ ФАЙЛЕ (PyQt6 WebEngine).
# НИЧЕГО БОЛЬШЕ НЕ НУЖНО: ни браузерных расширений, ни native messaging,
# ни внешних js/qwebchannel-файлов:
#   * окно = frameless PyQt6 + QWebEngineView со страницей DeepSeek;
#   * агент (пилот, правки файлов, память, интернет, SHELL) = JS-код,
#     встроенный ниже строкой AGENT_JS и заинжекченный в страницу;
#   * транспорт JS<->Python — polling: JS кладёт вызовы в __dsxOutbox,
#     Python каждые 150 мс забирает их runJavaScript'ом, ответы шлёт
#     вызовом window.dsxReplyFromPython(...) (файлы на диск, шелл —
#     subprocess cmd /c, интернет — urllib; никакого CORS-кошмара);
#   * папка проекта — нативный Qt-диалог.
# Запуск:  pip install PyQt6 PyQt6-WebEngine  &&  python DeepSeekIDE.py
# Проверка без GUI:  python DeepSeekIDE.py --selftest
# ======================================================================
import base64
import ctypes
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--enable-gpu-rasterization --enable-zero-copy"

APP_NAME = "DeepSeekIDE"
START_URL = "https://chat.deepseek.com/sign_in"
POLL_MS = 150
TIMEOUT_SHELL = 120          # сек
TIMEOUT_FETCH = 20           # сек (dl = 60)
CAP_STDOUT, CAP_STDERR = 65536, 16384
CAP_PAGE_TEXT = 300000
CAP_DOWNLOAD = 16 * 1024 * 1024

UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36')


def app_data_dir():
    """Папка настроек приложения (win: %LOCALAPPDATA%, нix: ~/.local/share)."""
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        p = os.path.join(base, 'DeepSeekExtended', 'qt-app')
    else:
        p = os.path.join(os.path.expanduser('~'), '.local', 'share', 'deepseekide')
    os.makedirs(p, exist_ok=True)
    return p


CFG_PATH = os.path.join(app_data_dir(), 'config.json')


def load_cfg():
    try:
        with open(CFG_PATH, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return {}


def save_cfg(cfg):
    with open(CFG_PATH, 'w', encoding='utf-8') as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


# ======================================================================
# FS: одна «папка проекта», относительные пути, защита от выхода из неё
# ======================================================================
def project_root():
    p = load_cfg().get('folder')
    return p if p and os.path.isdir(p) else None


def resolve_rel(rel):
    """Относительный путь модели -> абсолютный ВНУТРИ корня проекта."""
    rel = str(rel or '').replace('\\', '/').lstrip('/').strip()
    if not rel:
        raise ValueError('пустой путь')
    if '..' in rel.split('/'):
        raise ValueError('выход за папку проекта запрещён: ' + rel)
    root = project_root()
    if not root:
        raise RuntimeError('не выбрана папка проекта (needPick)')
    full = os.path.abspath(os.path.join(root, *rel.split('/')))
    if not full.startswith(os.path.abspath(root) + os.sep):
        raise ValueError('выход за папку проекта запрещён: ' + rel)
    return full


SKIP_DIRS = {'.git', 'node_modules', '.deepseekide', '__pycache__'}
TEXT_EXT = re.compile(r'\.(txt|md|js|ts|jsx|tsx|json|html|css|py|cpp|c|h|hpp|java|go|rs|bat|sh|ps1|cs|yaml|yml|xml|ini|cfg|sql)$', re.I)


def fs_ensure(_args):
    root = project_root()
    if root:
        return {'ok': True, 'name': os.path.basename(os.path.normpath(root))}
    return {'ok': False, 'needPick': True}


def fs_forget(_args):
    cfg = load_cfg(); cfg.pop('folder', None); save_cfg(cfg)
    return {'ok': True}


def fs_read(args):
    p = resolve_rel(args.get('path'))
    with open(p, 'rb') as f:
        data = f.read(3 * 1024 * 1024)
    return {'ok': True, 'text': data.decode('utf-8', errors='replace')}


def fs_write(args):
    p = resolve_rel(args.get('path'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        f.write(str(args.get('text', '')))
    return {'ok': True}


def fs_mkdir(args):
    p = resolve_rel(args.get('path'))
    os.makedirs(p, exist_ok=True)
    return {'ok': True}


def fs_remove(args):
    p = resolve_rel(args.get('path'))
    if os.path.isdir(p):
        if not args.get('recursive'):
            raise ValueError('это папка — нужно recursive:true')
        import shutil
        shutil.rmtree(p)
    elif os.path.isfile(p):
        os.remove(p)
    else:
        raise FileNotFoundError(str(args.get('path')))
    return {'ok': True}


def fs_stat(args):
    p = resolve_rel(args.get('path'))
    st = os.stat(p)
    lines = -1
    if st.st_size <= 3 * 1024 * 1024:
        try:
            with open(p, 'r', encoding='utf-8', errors='replace') as f:
                lines = len(f.read().split('\n'))
        except Exception:
            lines = -1
    return {'ok': True, 'bytes': st.st_size, 'lines': lines,
            'modified': time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime(st.st_mtime))}


def fs_write_b64(args):
    p = resolve_rel(args.get('path'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    data = base64.b64decode(str(args.get('b64', '')))
    with open(p, 'wb') as f:
        f.write(data)
    return {'ok': True, 'bytes': len(data)}


def fs_tree(args):
    root = project_root()
    if not root:
        raise RuntimeError('не выбрана папка проекта (needPick)')
    max_entries = int(args.get('max_entries') or 400)
    out, count = [], 0

    def walk(path, prefix, depth):
        nonlocal count
        if count >= max_entries or depth > 8:
            return
        try:
            entries = sorted(os.scandir(path), key=lambda e: (not e.is_dir(follow_symlinks=False), e.name.lower()))
        except OSError:
            return
        for e in entries:
            if count >= max_entries:
                return
            if e.name in SKIP_DIRS:
                continue
            is_dir = e.is_dir(follow_symlinks=False)
            count += 1
            out.append(prefix + ('[папка] ' if is_dir else '        ') + e.name)
            if is_dir:
                walk(os.path.join(path, e.name), prefix + '  ', depth + 1)

    walk(root, '', 0)
    name = os.path.basename(os.path.normpath(root))
    head = '(проект: %s, показано %d записей)' % (name, count)
    return {'ok': True, 'text': head + '\n' + '\n'.join(out)}


def fs_search(args):
    root = project_root()
    if not root:
        raise RuntimeError('не выбрана папка проекта (needPick)')
    query = str(args.get('query', ''))
    needle = query if args.get('case_sensitive') else query.lower()
    out, matches, scanned = [], 0, 0

    def walk(path, prefix):
        nonlocal matches, scanned
        if matches >= 200 or scanned > 400:
            return
        try:
            entries = sorted(os.scandir(path), key=lambda e: e.name.lower())
        except OSError:
            return
        for e in entries:
            if matches >= 200 or scanned > 400:
                return
            if e.name in SKIP_DIRS:
                continue
            full = os.path.join(path, e.name)
            if e.is_dir(follow_symlinks=False):
                walk(full, prefix + e.name + '/')
                continue
            if not TEXT_EXT.search(e.name):
                continue
            try:
                if os.path.getsize(full) > 512 * 1024:
                    continue
                with open(full, 'r', encoding='utf-8', errors='replace') as f:
                    text = f.read()
            except OSError:
                continue
            scanned += 1
            for i, line in enumerate(text.split('\n')):
                if matches >= 200:
                    break
                hay = line if args.get('case_sensitive') else line.lower()
                if needle in hay:
                    out.append('%s%s:%d: %s' % (prefix, e.name, i + 1, line[:200]))
                    matches += 1

    walk(root, '')
    return {'ok': True, 'text': '\n'.join(out) or '(совпадений нет)',
            'matches': matches, 'scanned': scanned}


# ======================================================================
# Шелл: cmd /c в корне проекта, таймаут, капы, декод UTF-8 -> OEM
# ======================================================================
def _decode(raw):
    if not raw:
        return ''
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        pass
    cp = 866
    if os.name == 'nt':
        try:
            cp = ctypes.windll.kernel32.GetOEMCP() or 866
        except Exception:
            cp = 866
    try:
        return raw.decode('cp%d' % cp, errors='replace')
    except Exception:
        return raw.decode('latin-1', errors='replace')


def shell_run(args):
    cmd = str(args.get('cmd', '')).replace('\n', ' ').strip()
    if not cmd:
        return {'ok': False, 'error': 'пустая команда'}
    cwd = project_root() or app_data_dir()
    argv = ['cmd.exe', '/c', cmd] if os.name == 'nt' else ['/bin/sh', '-c', cmd]
    killed = False
    try:
        p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            out_b, err_b = p.communicate(timeout=TIMEOUT_SHELL)
            code = p.returncode
        except subprocess.TimeoutExpired:
            p.kill()
            out_b, err_b = p.communicate()
            killed = True
            code = -1
    except Exception as e:
        return {'ok': False, 'error': 'spawn: %s' % e}
    stdout = _decode(out_b)
    stderr = _decode(err_b)
    if len(stdout) > CAP_STDOUT:
        stdout = stdout[:CAP_STDOUT] + '\n...(truncated)'
    if len(stderr) > CAP_STDERR:
        stderr = stderr[:CAP_STDERR] + '\n...(truncated)'
    return {'ok': True, 'stdout': stdout, 'stderr': stderr,
            'exitCode': code, 'killed': killed, 'cwd': cwd}


# ======================================================================
# Интернет: WEB FETCH / WEB DOWNLOAD (без CORS-ограничений браузера)
# ======================================================================
def web_fetch(args):
    url = str(args.get('url', '')).strip()
    dl = bool(args.get('dl'))
    if not re.match(r'https?://', url, re.I):
        return {'ok': False, 'error': 'not http(s): ' + url[:80]}
    req = urllib.request.Request(url, headers={
        'User-Agent': UA,
        'Cache-Control': 'no-cache',
    })
    timeout = 60 if dl else TIMEOUT_FETCH
    try:
        resp = urllib.request.urlopen(req, timeout=timeout)
        ctype = resp.headers.get('Content-Type', '')
        status = getattr(resp, 'status', 200)
        if dl:
            data = resp.read(CAP_DOWNLOAD + 1)
            if len(data) > CAP_DOWNLOAD:
                return {'ok': False, 'error': 'файл больше 16 МБ (%d)' % (len(data) // 1048576)}
            return {'ok': True, 'status': status, 'type': ctype,
                    'size': len(data), 'b64': base64.b64encode(data).decode('ascii')}
        data = resp.read(CAP_PAGE_TEXT + 1)
        text = data[:CAP_PAGE_TEXT].decode('utf-8', errors='replace')
        return {'ok': True, 'status': status, 'type': ctype, 'text': text}
    except Exception as e:
        msg = str(e)
        if 'timed out' in msg or 'Timeout' in msg:
            msg = 'страница не ответила за %d сек' % timeout
        return {'ok': False, 'error': msg}


def img_fetch(args):
    prompt = str(args.get('prompt', '')).strip()
    model = str(args.get('model', '')).strip() or 'black-forest-labs/flux.1-schnell'
    key = str(args.get('key', '')).strip()
    if not prompt:
        return {'ok': False, 'error': 'empty prompt'}
    url = ('https://gen.pollinations.ai/image/' + urllib.parse.quote(prompt, safe='') +
           '?model=' + urllib.parse.quote(model, safe='') + '&width=1024&height=1024&nologo=true')
    dlog('IMG →', model, '|', prompt[:70] + ('…' if len(prompt) > 70 else ''), '| ключ:', 'да' if key else 'нет (аноним)')
    hdrs = {'User-Agent': UA}
    if key:
        hdrs['Authorization'] = 'Bearer ' + key
    req = urllib.request.Request(url, headers=hdrs)
    try:
        resp = urllib.request.urlopen(req, timeout=150)
        data = resp.read()
        dlog('IMG OK:', getattr(resp, 'status', 200), len(data), 'bytes,', resp.headers.get('Content-Type', '?'))
        return {'ok': True, 'status': getattr(resp, 'status', 200),
                'type': resp.headers.get('Content-Type', 'image/jpeg'),
                'size': len(data), 'b64': base64.b64encode(data).decode('ascii')}
    except Exception as e:
        msg = str(e)
        try:
            msg += ' :: ' + e.read(300).decode('utf-8', errors='replace')  # API отвечает текстом ошибки
        except Exception:
            pass
        dlog('IMG FAIL:', msg[:200])
        return {'ok': False, 'error': msg}


def dlog(*a):
    # единый лог в консоль: метка времени + префикс [DSX]
    try:
        print('[DSX %s]' % time.strftime('%H:%M:%S'), *a, flush=True)
    except Exception:
        pass


def brief_out(out):
    # однострочная сводка результата хендлера моста для консоли
    if not isinstance(out, dict):
        return str(out)[:140]
    if not out.get('ok'):
        return 'FAIL: ' + str(out.get('error'))[:200]
    parts = []
    for k in ('exitCode', 'status', 'size', 'lines', 'bytes'):
        if k in out:
            parts.append('%s=%s' % (k, out[k]))
    d = out.get('data')
    if isinstance(d, dict):
        if 'done' in d:
            parts.append('ops=%s/%s' % (d.get('done'), d.get('total')))
        if 'text' in d:
            parts.append('text=%d зн' % len(str(d.get('text') or '')))
    return 'OK ' + ' '.join(parts)


FS_OPS = {
    'ensure': fs_ensure, 'forget': fs_forget, 'read': fs_read, 'write': fs_write,
    'mkdir': fs_mkdir, 'remove': fs_remove, 'stat': fs_stat, 'tree': fs_tree,
    'search': fs_search, 'write_b64': fs_write_b64,
}


# ======================================================================
# JS-агент (встроен): qt-клей (polling outbox) + opp.js + fsbridge.js +
# content.js из старого расширения. Инжектится одним runJavaScript.
# ======================================================================
AGENT_JS = r"""/* DeepSeekIDE Qt-клей без внешних файлов: outbox-поллинг.
 * window.dsxCall(kind, data) -> Promise(object); Python каждые ~150ms
 * сливает window.__dsxOutbox, отвечает вызовом dsxReplyFromPython. */
window.__DSX_QT = true;
window.__dsxOutbox = window.__dsxOutbox || [];
(function () {
  var pend = {}, tok = 0;
  window.dsxCall = function (kind, data) {
    return new Promise(function (res, rej) {
      var t = 'q' + (++tok) + '_' + Date.now();
      pend[t] = { res: res, rej: rej };
      window.__dsxOutbox.push({ t: t, kind: kind, payload: data || {} });
      setTimeout(function () {
        if (pend[t]) { delete pend[t]; rej(new Error('host timeout')); }
      }, 185000);
    });
  };
  window.dsxReplyFromPython = function (m) {
    if (!m) return;
    var p = pend[m.t]; if (!p) return; delete pend[m.t];
    p.res(m.out);
  };
  console.log('[DSX] qt-bridge ready (polling)');
})();

;
/* =====================================================================
 * opp.js — «мозг» DeepSeek Extended: парсер deepseekide-ops, математика
 * строк и системный промпт. Тот самый код, что живёт в C++ (OpsParser/
 * Prompt/AutoNote), переписанный 1-в-1 — ПРАВИТЬ СИНХРОННО с ними!
 * Работает и в content script, и в node (module.exports — для тестов).
 * ===================================================================== */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.DsideOps = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var MUT_OPS = ['write_file', 'edit_file', 'append_file', 'insert_lines', 'replace_lines',
                 'make_dir', 'delete_path', 'copy_file', 'move_file'];
  // В чистом расширении run_command НЕТ (браузер не запускает процессы ОС).

  function trimOp(s) { return s.replace(/^\s+|\s+$/g, ''); }

  // ----- сканер сбалансированного JSON-спана (как ExtractJsonSpan в C++)
  function extractJsonSpan(body) {
    var start = -1;
    for (var i = 0; i < body.length; ++i) {
      var c0 = body[i];
      if (c0 === '{' || c0 === '[') {
        var depth = 0, inStr = false, esc = false, j = i;
        var open = c0;
        for (; j < body.length; ++j) {
          var c = body[j];
          if (inStr) {
            if (esc) esc = false;
            else if (c === '\\') esc = true;
            else if (c === '"') inStr = false;
          } else {
            if (c === '"') inStr = true;
            else if (c === open) depth++;
            else if ((open === '{' && c === '}') || (open === '[' && c === ']')) {
              depth--;
              if (depth === 0) break;
            }
          }
        }
        if (j < body.length && depth === 0) return body.slice(i, j + 1);
        i = j; // дальше первого сканчика валидного спана не ищем сразу — но продолжаем цикл
      }
    }
    return '';
  }

  function tryParse(str) { try { return JSON.parse(str); } catch (e) { return null; } }

  function validateOp(op, idx, errors) {
    if (!op || typeof op !== 'object' || Array.isArray(op)) { errors.push('#' + idx + ': не объект'); return null; }
    var name = op.name, args = op.args;
    if (typeof name !== 'string' || MUT_OPS.indexOf(name) < 0) {
      errors.push('#' + idx + ': операция «' + name + '» не разрешена'); return null;
    }
    if (!args || typeof args !== 'object' || Array.isArray(args)) {
      errors.push('#' + idx + ': нет объекта args'); return null;
    }
    return { name: name, args: args };
  }

  // Тело одного блока: строгий JSON → фолбэк на вырезание сбалансированного спана.
  function parseOpsBody(body, out) {
    body = body.trim();
    if (!body) return;
    var data = tryParse(body);
    var src = data == null ? tryParse(extractJsonSpan(body)) : data;
    if (src == null) {
      out.errors.push('блок deepseekide-ops: нечитаемый JSON (' + body.slice(0, 120) + '…)');
      return;
    }
    var arr = Array.isArray(src) ? src : [src];
    arr.forEach(function (op, i) {
      var v = validateOp(op, out.ops.length + 1, out.errors);
      if (v) out.ops.push({ name: v.name, args: v.args, json: op });
    });
  }

  // Fence-путь: ```deepseekide-ops ... ```
  function extractOps(answer, out) {
    var re = /```deepseekide-ops[ \t]*\r?\n([\s\S]*?)```/g, m;
    var found = false;
    while ((m = re.exec(answer)) !== null) { found = true; parseOpsBody(m[1], out); }
    if (!found) {
      // DOM-путь зовётся отдельно; здесь же — попытка вырезать голый спан после
      // слова-маркера (на случай съехавшей разметки)
    }
    return found;
  }

  // ----- директивы NEED FILE / NEED SEARCH: строгое начало строки + валидация
  var FILE_MARKERS = ['НУЖЕН ФАЙЛ:', 'Нужен файл:', 'нужен файл:', 'NEED FILE:', 'Need file:', 'need file:'];
  var SEARCH_MARKERS = ['НУЖЕН ПОИСК:', 'Нужен поиск:', 'нужен поиск:', 'NEED SEARCH:', 'Need search:', 'need search:'];

  function unescapeHtml(p) {
    return p.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"')
            .replace(/&#39;/g, "'").replace(/&amp;/g, '&');
  }
  function markerAtLineStart(line) {
    var pos = 0, sawBullet = false;
    while (pos < line.length && /\s/.test(line[pos])) pos++;
    if (pos < line.length && '-*•❯>·>'.indexOf(line[pos]) >= 0) {
      var p2 = pos + 1;
      while (p2 < line.length && /\s/.test(line[p2])) p2++;
      if (p2 > pos + 1) { sawBullet = true; pos = p2; }
    }
    return { pos: pos, bullet: sawBullet };
  }
  function looksLikePath(p) {
    if (!p || p.length === 0 || p.length > 200) return false;
    if (/[<>«»&;|*?]/.test(p)) return false;
    var hasSep = p.indexOf('/') >= 0 || p.indexOf('\\') >= 0;
    var hasDot = p.indexOf('.') >= 0;
    return hasSep || hasDot;
  }
  function cleanPath(p) {
    p = unescapeHtml(p);
    var junk = '`"\'«»<>.,;:!';
    var changed = true;
    while (changed && p.length) {
      var before = p.length;
      while (p.length && (junk.indexOf(p[0]) >= 0 || /\s/.test(p[0]))) p = p.slice(1);
      while (p.length && (junk.indexOf(p[p.length - 1]) >= 0 || /\s/.test(p[p.length - 1]))) p = p.slice(0, -1);
      while (p.length && (p[p.length - 1] === '.' || p[p.length - 1] === ',')) p = p.slice(0, -1);
      changed = p.length !== before;
    }
    return p.slice(0, 200);
  }

  function scanDirectives(answer, markers, clean, valid, maxCount) {
    var out = [], inFence = false;
    var lines = answer.split('\n');
    for (var li = 0; li < lines.length && out.length < (maxCount || 5); ++li) {
      var line = lines[li];
      if (line.slice(0, 3) === '```') { inFence = !inFence; continue; }
      if (inFence) continue;
      var st = markerAtLineStart(line);
      for (var mi = 0; mi < markers.length; ++mi) {
        var mk = markers[mi];
        if (line.substr(st.pos, mk.length) !== mk) continue;
        var val = clean(unescapeHtml(line.slice(st.pos + mk.length)));
        if (valid(val) && out.indexOf(val) < 0) out.push(val);
        break;
      }
    }
    return out;
  }
  function findFileRequests(answer) {
    return scanDirectives(answer, FILE_MARKERS, cleanPath, looksLikePath, 8);
  }
  function findSearchRequests(answer) {
    return scanDirectives(answer, SEARCH_MARKERS, function (v) {
      var t = v.trim();
      while (t.length && '`"<'.indexOf(t[0]) >= 0) t = t.slice(1);
      while (t.length && '`">.'.indexOf(t[t.length - 1]) >= 0) t = t.slice(0, -1);
      return t.trim();
    }, function (v) { return v.length > 0 && v.length <= 200; }, 4);
  }

  // ----- СКОБОЧНЫЕ запросы v22: [<{NEED FILE: путь}>] и др.
  // «<{» не трактуется HTML-токенайзером как тег (после < идёт {), поэтому
  // разметка доживает до DOM-текста нетронутой и видна пользователю.
  // Маркеры внутри ```-ограждённого кода игнорируются (это могут быть примеры).
  // модель иногда рисует HTML-сущности (&lt;&gt;&amp;), вложенные кавычки в аргументе,
  // а закрытие портит: «}]» вместо «}>]» — всё это жуём.
  var BRACKET_RE = /\[<\{\s*(NEED FILE|NEED SEARCH|FILE INFO|READ RANGE|MEMORY SEARCH|MEMORY ADD|ASK MULTI|ASK|REVERT FILE|WEB FETCH|WEB DOWNLOAD|WEB GREP|SHELL|IMAGE FILE|IMAGE SHOW)\s*[:：]\s*([\s\S]*?)(?:\s*\}>\]|\s*\}\])/gi;
  function findBracketRequests(answer) {
    answer = unescapeHtml(String(answer)); // &lt;{SHELL…2&gt;&amp;1}] -> [<{SHELL…2>&1}]
    var out = [], inFence = false;
    var lines = answer.split('\n');
    for (var li = 0; li < lines.length && out.length < 12; ++li) {
      var line = lines[li];
      if (line.slice(0, 3) === '```') { inFence = !inFence; continue; }
      if (inFence) continue;
      var m; BRACKET_RE.lastIndex = 0;
      while ((m = BRACKET_RE.exec(line)) !== null && out.length < 12) {
        var kind = m[1].toUpperCase(), key, opts = [], extra = '';
        var val = unescapeHtml(String(m[2])).trim();
        if (kind === 'NEED FILE' || kind === 'FILE INFO') {
          val = cleanPath(val);
          if (!looksLikePath(val)) continue;
        } else if (kind === 'READ RANGE') {
          var rm = val.match(/^(.+?)\s*[:\u003a]\s*(\d+)\s*[-–—]\s*(\d+)$/);
          if (!rm) continue;
          var pth = cleanPath(rm[1]);
          if (!looksLikePath(pth)) continue;
          var f = parseInt(rm[2], 10) || 1, t = parseInt(rm[3], 10) || f;
          if (f < 1) f = 1;
          if (t < f) t = f;
          if (t - f > 1199) t = f + 1199; // потолок фрагмента: 1200 строк
          val = pth + ':' + f + '-' + t;
        } else if (kind === 'WEB FETCH') {
          if (!/^https?:\/\/\S+$/.test(val) || val.length > 500) continue;
        } else if (kind === 'WEB DOWNLOAD') {
          var dm = /^(.+?)::([\s\S]*)$/.exec(val);
          if (!dm) continue;
          var durl = dm[1].trim(), dpath = cleanPath(dm[2]);
          if (!/^https?:\/\/\S+$/.test(durl) || durl.length > 500) continue;
          if (!looksLikePath(dpath)) continue;
          val = durl; extra = dpath;
        } else if (kind === 'WEB GREP') {
          var gm = /^(.+?)::([\s\S]*)$/.exec(val);
          if (!gm) continue;
          var gurl = gm[1].trim(), gq = gm[2].trim().slice(0, 200);
          if (!/^https?:\/\/\S+$/.test(gurl) || gurl.length > 500) continue;
          if (!gq) continue;
          val = gurl; extra = gq;
        } else if (kind === 'MEMORY ADD') {
          var am = /^(.+?)::([\s\S]*)$/.exec(val);
          if (!am || !am[1].trim() || !am[2].trim()) continue;
          extra = am[2].trim().slice(0, 1500);
          val = am[1].trim().slice(0, 120);
        } else if (kind === 'REVERT FILE') {
          val = cleanPath(val);
          if (!looksLikePath(val)) continue;
        } else if (kind === 'ASK' || kind === 'ASK MULTI') {
          var pm = /^(.+?)::([\s\S]*)$/.exec(val);
          var opts = [];
          if (pm) {
            opts = pm[2].split(';').map(function (s) { return s.trim(); })
                        .filter(function (s) { return s.length > 0; }).slice(0, 8);
            val = pm[1].trim();
          }
          if (!val) continue;
          if (val.length > 300) val = val.slice(0, 300);
        } else if (kind === 'SHELL') {
          val = val.replace(/[\r\n]+/g, ' ').trim();
          if (!val || val.length > 500) continue;
        } else if (kind === 'IMAGE FILE') {
          var im = /^(.+?)::([\s\S]*)$/.exec(val);
          if (!im) continue;
          var ipr = im[1].trim(), ipath = cleanPath(im[2]);
          if (!ipr || ipr.length > 900) continue;
          if (!looksLikePath(ipath)) continue;
          if (!/\.(png|jpe?g|webp)$/i.test(ipath)) continue;
          val = ipr; extra = ipath;
        } else if (kind === 'IMAGE SHOW') {
          if (!val || val.length > 900) continue;
        } else { // NEED SEARCH / MEMORY SEARCH
          val = val.slice(0, 200);
          if (!val) continue;
        }
        key = kind + '|' + val.toLowerCase();
        var dup = false;
        for (var k = 0; k < out.length; ++k) if (out[k].key === key) { dup = true; break; }
        if (!dup) out.push({ kind: kind, arg: val, opts: opts, extra: extra, key: key, bracket: true });
      }
    }
    return out;
  }


  // ----- ПАМЯТЬ владельца: поиск по заметкам (простой скоринг по термам)
  // Вход: notes = [{t: title, x: text}], query — строка запроса.
  // Выход: до 2 лучших карточек {title, snippet} (snippet с «…»-склейкой).
  function searchMemory(notes, query) {
    if (!Array.isArray(notes) || !notes.length) return [];
    var q = String(query || '').toLowerCase().trim();
    if (!q) return [];
    var terms = q.split(/[^\p{L}\p{N}_]+/u).filter(function (s) { return s.length > 1; });
    if (!terms.length) terms = [q];
    var scored = [];
    notes.forEach(function (n, idx) {
      var title = String(n.t || ('заметка ' + (idx + 1)));
      var text = String(n.x || '');
      if (!text) return;
      var lines = text.split('\n'), hits = [], score = 0, i;
      var low = text.toLowerCase();
      if (low.indexOf(q) >= 0) score += 10; // точная фраза — сильный буст
      for (i = 0; i < lines.length; ++i) {
        var ll = lines[i].toLowerCase(), lineScore = 0;
        terms.forEach(function (t) {
          var p = 0, c = 0;
          while ((p = ll.indexOf(t, p)) >= 0 && c < 8) { lineScore++; p += t.length; c++; }
        });
        if (lineScore > 0) { score += lineScore; hits.push(lines[i].trim()); }
      }
      var tl = title.toLowerCase();
      terms.forEach(function (t) { if (tl.indexOf(t) >= 0) score += 3; });
      if (score <= 0) return;
      var snippet = hits.slice(0, 18).join('\n…\n');
      if (!snippet) {
        // совпал только заголовок — отдаём начало заметки как выдержку
        snippet = lines.map(function (l) { return l.trim(); }).filter(Boolean).slice(0, 5).join('\n');
        if (snippet.length > 800) snippet = snippet.slice(0, 800) + ' …';
      }
      if (snippet.length > 1600) snippet = snippet.slice(0, 1600) + ' …';
      scored.push({ title: title, snippet: snippet, score: score });
    });
    scored.sort(function (a, b) { return b.score - a.score; });
    return scored.slice(0, 2);
  }

  // ----- безопасный относительный путь (sandbox — против ../ и абсолюта)
  var PROTECTED = ['.git', '.deepseekide'];
  function sanitizeRel(rel) {
    if (typeof rel !== 'string') return null;
    var p = rel.replace(/\\/g, '/').replace(/^\/+/, '').replace(/\/+$|\/+/g, function (m) { return m === '' ? m : m.length > 1 ? '/' : m; });
    if (!p) return null;
    var parts = p.split('/'), out = [];
    for (var i = 0; i < parts.length; ++i) {
      var seg = parts[i];
      if (seg === '' || seg === '.') continue;
      if (seg === '..') return null;
      out.push(seg);
    }
    if (!out.length) return null;
    if (PROTECTED.indexOf(out[0]) >= 0) return null;
    return out.join('/');
  }

  // ----- математика строк (1-based, включительно) — как в ToolRegistry
  function insertLines(text, line, content) {
    var lines = text.split('\n');
    if (line < 1 || line > lines.length + 1) return null;
    var ins = content.split('\n');
    lines.splice.apply(lines, [line - 1, 0].concat(ins));
    return lines.join('\n');
  }
  function replaceLines(text, startLine, endLine, content) {
    var lines = text.split('\n');
    if (startLine < 1 || endLine < startLine || endLine > lines.length) return null;
    var rep = content === '' ? [] : content.split('\n');
    lines.splice.apply(lines, [startLine - 1, endLine - startLine + 1].concat(rep));
    return lines.join('\n');
  }

  // ----- системный промпт (Extended-редакция: без терминала)
  function buildPrompt(tree) {
    return [
"You are DeepSeek Extended Agent — an autonomous senior software engineer. Your edits land DIRECTLY in the user's project folder on their machine via a browser extension. You ACT; you do not merely chat. Answer in Russian by default unless the user writes English.",
"",
"# HOW TO CHANGE FILES (mandatory format)",
"Emit every change ONLY inside fenced blocks:",
"```deepseekide-ops",
"[{\"name\":\"write_file\",\"args\":{\"path\":\"src/main.py\",\"content\":\"full file text\"}}]",
"```",
"- Multiple blocks per reply allowed; they execute top-to-bottom AUTOMATICALLY and instantly, with no user confirmation.",
"- Inside a block: STRICT JSON ONLY — one object {\"name\": ..., \"args\": {...}} or an array. No comments, no prose, no trailing commas.",
"- Escape newlines inside JSON strings as \\n. Triple backticks are fine INSIDE string values.",
"- Code shown OUTSIDE an ops block is NEVER applied.",
"",
"# TOOLBOX",
"- write_file {path, content} — create / FULLY rewrite a file, always complete files.",
"- edit_file {path, old_string, new_string, replace_all?} — replace an EXACT fragment (byte-for-byte incl. indentation).",
"- append_file {path, content}.",
"- insert_lines {path, line, content} — insert BEFORE 1-based line (line = lineCount+1 appends).",
"- replace_lines {path, start_line, end_line, content} — replace range (empty content deletes it).",
"- make_dir {path}. delete_path {path, recursive?}. copy_file {from, to}. move_file {from, to}.",
"",
"# GETTING CONTEXT (self-service, automatic — never ask the human for file contents)",
"Missing information? Emit requests INSIDE the special bracket marker, ONE per line, OUTSIDE ops blocks and code fences. The extension answers them automatically within seconds and shows the user an inline status chip; the answer arrives as a hidden SYSTEM message and your flow continues automatically.",
"Syntax (copy EXACTLY, including the brackets):",
"# MARKER SYNTAX — STRICT CONTRACT (the automation regex is unforgiving about these):",
"- Write markers EXACTLY as [<{NAME: argument}>] — canonically closed }>]. The tail }] also parses, but }>] is the rule.",
"- One marker per LINE, plain text. NEVER inside ``` fences (fenced = human example = ignored).",
"- NEVER HTML-escape: write < > & literally, never &lt; &gt; &amp;. NEVER use <｜DSML｜>, <function_calls> or any XML tool syntax — invalid, never executes.",
"- Quotes, >, &, && INSIDE the argument are fine: [<{SHELL: cmd /c \"dir \"C:\\Program Files\" && echo ok\" 2>&1}>] is a VALID marker.",
"- Paths in markers are project-relative with forward slashes. drive:// absolute paths only inside SHELL commands.",
"- Markers fire automatically; results arrive as SYSTEM messages. Do not announce or narrate them (\"now I will request...\"), just emit the marker line and keep writing your reply normally.",
"",
"[<{FILE INFO: <project-relative path>}>]          -> file size in bytes + total line count.",
"[<{READ RANGE: <project-relative path>:<from>-<to>}>] -> numbered lines from..to (1-based, inclusive, up to 1200 lines at once).",
"[<{NEED FILE: <project-relative path>}>]         -> WHOLE file with line numbers (hard cap ~4000 lines / 120k chars; the rest is TRUNCATED).",
"[<{NEED SEARCH: <substring>}>]                   -> matches across ALL text files of the project in the form file:line: text.",
"[<{MEMORY SEARCH: <keywords>}>]                  -> owner\u2019s knowledge base (architecture, fields, structures, protocols, how things work): returns matching note fragments.",
"[<{WEB FETCH: <https-url>}>]                              -> fetches a web page and returns its readable TEXT (docs, references). JS-only SPA pages may come back empty.",
"[<{MEMORY ADD: <title> :: <one-line fact>}>]               -> permanently saves a durable fact into the owner knowledge base (verified offsets, decisions, conventions — never code dumps, one line per fact).",
"[<{REVERT FILE: <project-relative path>}>]                 -> restores the file to the snapshot taken right before its last edit by you (if your edit created it, the file is removed). The undo is permanent.",
"[<{WEB GREP: <url> :: <substring or from-to>}>]               -> downloads the page RAW/CLEANED and returns FULL matching lines (e.g. \u0027m_modelState\u0027 -> whole \u0027constexpr ... = 0x140;\u0027 lines with numbers). Use \u0027144-150\u0027 instead of a substring to get that line range.",
"[<{WEB DOWNLOAD: <url> :: <project-relative path>}>]          -> downloads a file (binary ok, ≤16 MB) straight into the project folder. Use for dumps/SDK snapshots the task needs; confirm the path in your summary.",
"[<{IMAGE SHOW: <image prompt>}>]              -> generates an image with the owner's image API and shows it INLINE right where the marker stands (the skeleton/shimmer is replaced by the picture). Write rich visual descriptions (style, light, mood, composition); text-on-image is unreliable, avoid it. If generation is off or the key is missing, the SYSTEM reply says so — continue in text then.",
"[<{IMAGE FILE: <image prompt> :: <project-relative path.png>}>] -> generates the same way but SAVES the bytes into the project file instead of showing it in chat (assets, mockups, textures).",
"[<{SHELL: <one-line command>}>] -> runs the command in the project folder via the native shell bridge: the HUMAN sees it in a confirm card (or has auto-allow on); you receive stdout+stderr+exit code. For builds, tests, scripts, package managers, compilers. One line only (chain with && or &), non-interactive, 120s timeout. HARD LIMIT: at most ONE SHELL command per reply — the human must confirm each card (or has auto-allow), a flood of cards jams the loop. Need several steps? Chain them in the one line (cmd1 && cmd2), or take this result and send the next command in the NEXT reply. If the reply says the bridge is offline, do NOT retry — give the human run instructions in text instead.",

"[<{ASK: <dilemma> :: <option1> ; <option2> ; <option3>}>]      -> asks the HUMAN through a clickable card: single choice (one option).",
"[<{ASK MULTI: <dilemma> :: <option1> ; <option2> ; <option3>}>] -> same but the human may tick SEVERAL options.",
"  (2-8 concrete options after \u0027::\u0027, separated by \u0027;\u0027; the UI always adds a \u0027custom answer\u0027 field itself. You get the picked answer(s) as a SYSTEM message and continue.)",
"Example:",
"[<{FILE INFO: src/main.cpp}>]",
"[<{READ RANGE: src/main.cpp:1-300}>]",
"[<{NEED SEARCH: handleLogin}>]",
"STRICT RULES:",
"- If unsure about a file's size, ask FILE INFO FIRST. For files over ~600 lines ALWAYS read them chunk-by-chunk with READ RANGE (e.g. :1-400, then :401-800) instead of NEED FILE — full dumps get truncated and waste context.",
"- NEED SEARCH scans the ENTIRE project, not only files you have already received. Use it to locate symbols before asking for files.",
"- Never invent project internals (field names, struct layouts, packet formats, config keys...). MEMORY SEARCH is your first stop for \u201chow does X work here\u201d; NEED SEARCH / READ RANGE confirm it in code.",
"- Teach the future you: every time you verify a durable fact (working offset, tricky API call, naming convention...), immediately MEMORY ADD it. Next sessions will reuse it instead of re-digging.",
"- Broke something with your own edit and the fix is not obvious? REVERT FILE it and redo cleanly \u2014 that is cheaper than patching over damage.",
"- External docs available: when the task involves an API/format you do not know well, WEB FETCH the authoritative page instead of guessing.",
"- Working with open dumps (offsets, schemas, SDKs)? Do NOT guess fields: WEB GREP the raw dump page for the member name to get its exact line, then MEMORY ADD the verified result.",
"- Prove your work: after code edits prefer VERIFYING with SHELL (compiler check, tests, lint) when a suitable command exists. Destructive/irreversible actions (mass deletes, force-push...) must go through the ASK card instead.",
"- Prefer acting on your own defaults; but when a fork genuinely depends on the HUMAN\u2019s intent (which framework, keep-or-delete, which of two APIs...), ask via [<{ASK: ...}>] / [<{ASK MULTI: ...}>] instead of plain-text questions or stalling. Plain-text questions are invisible to the automation and dead-end the loop.",
"- NEVER emit XML/framework tool syntax like <｜DSML｜>, <function_calls>, <antml:...>, ```tool_code or similar — those formats are INVALID here and never execute. The ONLY working tool syntax is the [<{...}>] markers above, closed EXACTLY as }] or }>] on the same intent.",
"- Batch all requests you need into ONE message, then wait — do not narrate them.",
"- NEVER repeat a request that was already answered in this chat: re-use what you already received. Repeating wastes a whole round-trip and is treated as an error.",
"# PRIVATE SCRATCHPAD (hidden thinking — RARE, extreme cases only)",
"For a genuinely hard task (multi-file refactor plan, deep debugging, weighing real alternatives) you may put your reasoning into a fenced block that is COMPLETELY hidden from the human:",
"```deepseekide-think",
"<very complex reasoning>",
"```",
"STRICT RULES:",
"- Default is NO think block: if the answer or the next step is straightforward/obvious, write it directly — a think block is overkill and a waste.",
"- The human NEVER sees the block's contents: nothing important belongs inside (no conclusions to deliver, no ops blocks, no [<{...}>] markers — they would vanish or be ignored).",
"- NEVER use triple backticks inside the block — they would close the fence early and flood the user's view. Describe code with single `quotes` or words instead.",
"- Do not restate think-block contents in visible prose.",
"",
"# PROJECT STRUCTURE",
tree || '(структура недоступна — спросите через NEED SEARCH/NEED FILE)',
"",
"# WORK STYLE",
"- ACT, DON'T ASK. Choose sensible defaults; mention them instead of asking.",
"- Stay strictly inside the project root above. Never aim outside it.",
"- Prefer one well-planned change over three sloppy ones.",
"",
"# TURN DISCIPLINE (critical)",
"- A message containing ops blocks or [<{...}>] requests is a WORK message: end it right after the last block. NO summary, NO conclusion, NO 'Готово' in it.",
"- The automatic SYSTEM reply (ops report or requested data) comes right after. Only your NEXT message — after reading that reply — may contain the short human summary (few lines, plain text).",
"- If the SYSTEM reply reports an ERROR (e.g. old_string not found): briefly diagnose, re-read the exact fragment with READ RANGE if needed, and retry with a CORRECTED payload ONCE. Never resend an identical payload — that creates an infinite error loop.",
"- Never claim the result before the SYSTEM reply arrived: you have NOT applied anything until the ops report confirms it."
    ].join('\n');
  }

  return {
    MUT_OPS: MUT_OPS,
    extractJsonSpan: extractJsonSpan,
    parseOpsBody: parseOpsBody,
    extractOps: extractOps,
    findFileRequests: findFileRequests,
    findSearchRequests: findSearchRequests,
    findBracketRequests: findBracketRequests,
    searchMemory: searchMemory,
    sanitizeRel: sanitizeRel,
    insertLines: insertLines,
    replaceLines: replaceLines,
    buildPrompt: buildPrompt,
    looksLikePath: looksLikePath
  };
});

;
/* =====================================================================
 * fsbridge.js — МОСТ К ФАЙЛОВОЙ СИСТЕМЕ (MAIN world страницы).
 * Content script не вызывает File System Access пикеры из своего мира —
 * поэтому этот скрипт инжектится в страницу и слушает window.postMessage:
 *   { __dsidefs: 'req', id, op, args }  →  { __dsidefs: 'resp', id, ok, data, error }
 * Хэндл папки проекта хранится в IndexedDB (origin chat.deepseek.com) и
 * перевыпрашивается ОДНИМ кликом после перезапуска браузера.
 * ===================================================================== */
(function () {
  // ---------- Qt-режим (DeepSeekIDE.py): файловые примитивы уходят в Python через WebChannel
  var QT = function () { return window.__DSX_QT === true && typeof window.dsxCall === 'function'; };
  function qfs(op, args) {
    var a = {}; for (var k in (args || {})) a[k] = args[k]; a.op = op;
    return window.dsxCall('fs', a).then(function (r) {
      if (!r) throw new Error('qt fs: пустой ответ (' + op + ')');
      if (typeof r.error === 'string' && r.error) throw new Error(r.error);
      return r;  // {ok:false, needPick:true} — нормальный ответ, НЕ исключение
    });
  }

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
    return idb('readonly', function (tx, res, rej) {
      var g = tx.objectStore(store).getAll();
      var k = tx.objectStore(store).getAllKeys();
      var done = 0, out = [];      // ждём УСПЕХ ОБОИХ запросов
      function fin() {
        if (++done < 2) return;
        var keys = k.result, vals = g.result;
        for (var i = 0; i < vals.length; ++i) out.push({ key: keys[i], val: vals[i] });
        res(out);
      }
      g.onsuccess = fin; k.onsuccess = fin;
      g.onerror = function () { rej(g.error); };
      k.onerror = function () { rej(k.error); };
    });
  }

  // ---------- хэндл папки
  async function pick() {
    if (QT()) {
      var r = await qfs('pick', {}); // нативный диалог Qt, гестур не нужен
      dirHandle = null; dirName = (r && (r.name || r.path)) || 'папка';
      return { name: dirName };
    }
    dirHandle = await window.showDirectoryPicker({ mode: 'readwrite' });
    dirName = dirHandle.name;
    await idbSet('kv', 'dirHandle', dirHandle);
    return { name: dirName };
  }
  async function ensure() {
    if (QT()) {
      var r = await qfs('ensure', {});
      if (r && r.ok) { dirName = r.name || dirName; return { ok: true, name: dirName }; }
      return { ok: false, needPick: true };
    }
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
    if (QT()) { var rr = await qfs('read', { path: rel }); return String(rr.text != null ? rr.text : ''); }
    var d = await parentOf(rel, false);
    var fh = await d.getFileHandle(leaf(rel));
    var f = await fh.getFile();
    var buf = await f.arrayBuffer();
    return new TextDecoder('utf-8', { fatal: false }).decode(buf);
  }
  async function writeText(rel, text) {
    if (QT()) { await qfs('write', { path: rel, text: String(text) }); return; }
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

    // бинарная запись (WEB DOWNLOAD): base64 → файл, под снапшот-одиночкой
    writeB64: async function (args) {
      var bin = atob(String(args.b64 || ''));
      var bytes = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
      var rel = String(args.path || '');
      var prev = null, had = true;
      try { prev = await readText(rel); } catch (e) { had = false; }
      await snapBegin('web download');
      await snapRecordBefore(rel, had ? 'modified' : 'created', had ? prev : null);
      if (QT()) {
        var r = await qfs('write_b64', { path: rel, b64: String(args.b64 || '') });
        await snapCommit();
        return { ok: true, bytes: r.bytes != null ? r.bytes : bytes.length };
      }
      var d = await parentOf(rel, true);
      var fh = await d.getFileHandle(leaf(rel), { create: true });
      var w = await fh.createWritable();
      await w.write(new Blob([bytes], { type: 'application/octet-stream' }));
      await w.close();
      await snapCommit();
      return { ok: true, bytes: bytes.length };
    },

    fileInfo: async function (args) {
      if (QT()) {
        var r = await qfs('stat', { path: args.path });
        return { bytes: r.bytes, lines: r.lines != null ? r.lines : -1, modified: r.modified || '' };
      }
      var d = await parentOf(args.path, false);
      var fh = await d.getFileHandle(leaf(args.path));
      var f = await fh.getFile();
      var lines = -1;
      if (f.size <= 3 * 1024 * 1024) { // до ~3 МБ честно считаем строки
        var text = await readText(args.path);
        lines = text.split('\n').length;
      }
      return { bytes: f.size, lines: lines, modified: new Date(f.lastModified).toISOString() };
    },

    tree: async function (args) {
      if (QT()) { var r = await qfs('tree', { max_entries: (args && args.max_entries) || 400 }); return { text: r.text }; }
      var max = (args && args.max_entries) || 400, out = [], count = 0;
      var SKIP = { '.git': 1, 'node_modules': 1, '.deepseekide': 1 };
      async function walk(d, prefix, depth) {
        if (count >= max || depth > 8) return;
        for await (var e of d.values()) {
          if (count >= max) return;
          if (SKIP[e.name]) continue;
          ++count;
          out.push(prefix + (e.kind === 'directory' ? '[папка] ' : '        ') + e.name);
          if (e.kind === 'directory') await walk(e, prefix + '  ', depth + 1);
        }
      }
      await walk(dirHandle, '', 0);
      return { text: '(проект: ' + dirName + ', показано ' + count + ' записей)\n' + out.join('\n') };
    },

    search: async function (args) {
      if (QT()) {
        var r = await qfs('search', { query: args.query, case_sensitive: !!args.case_sensitive });
        return { text: r.text, matches: r.matches || 0, scanned: r.scanned || 0 };
      }
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
      if (QT()) await qfs('mkdir', { path: rel });
      else await dirAt(partsOf(rel), true);
      await snapRecordBefore(rel, 'created-dir', null);
      return 'mkdir ' + rel;
    }
    if (name === 'delete_path') {
      var oldT = null;
      try { oldT = await readText(rel); } catch (e) { /* это папка */ }
      if (oldT === null && !args.recursive) throw new Error('это папка — нужно recursive:true');
      await snapRecordBefore(rel, oldT !== null ? 'deleted' : 'deleted-dir', oldT);
      if (QT()) await qfs('remove', { path: rel, recursive: !!args.recursive });
      else {
        var d0 = await parentOf(rel, false);
        await d0.removeEntry(leaf(rel), { recursive: !!args.recursive });
      }
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
        await snapRecordBefore(from, 'deleted', src);
        if (QT()) await qfs('remove', { path: from, recursive: false });
        else { var pd = await parentOf(from, false); await pd.removeEntry(leaf(from)); }
      }
      return name === 'copy_file' ? 'copy ' + from + ' → ' + to : 'move ' + from + ' → ' + to;
    }
    throw new Error('неизвестная операция: ' + name);
  }

  function dsxCountLines(s) { return s ? s.split('\n').length : 0; }
  function dsxDiffLines(a, b) {
    var map = {}, i, la = a.split('\n'), lb = b.split('\n');
    for (i = 0; i < la.length; ++i) map[la[i]] = (map[la[i]] || 0) + 1;
    var add = 0;
    for (i = 0; i < lb.length; ++i) { var k = lb[i]; if (map[k]) map[k]--; else add++; }
    var del = 0;
    for (var k2 in map) { if (Object.prototype.hasOwnProperty.call(map, k2)) del += map[k2]; }
    return [add, del];
  }
  async function dsxOpStat(name, args) {
    try {
      if (name === 'write_file') {
        var old = '';
        try { old = await readText(args.path); } catch (e0) {}
        var d1 = dsxDiffLines(old, String(args.content || args.text || ''));
        return ' [+' + d1[0] + ' -' + d1[1] + ']';
      }
      if (name === 'edit_file') {
        var d2 = dsxDiffLines(String(args.old_string || ''), String(args.new_string || ''));
        return ' [+' + d2[0] + ' -' + d2[1] + ']';
      }
      if (name === 'append_file') return ' [+' + dsxCountLines(String(args.content || args.text || '')) + ' -0]';
      if (name === 'insert_lines') return ' [+' + dsxCountLines(String(args.content || '')) + ' -0]';
      if (name === 'replace_lines') {
        var rm = (args.end_line | 0) - (args.start_line | 0) + 1; if (rm < 0) rm = 0;
        return ' [+' + dsxCountLines(String(args.content || '')) + ' -' + rm + ']';
      }
    } catch (e3) {}
    return '';
  }

  function dsxSplit(s) { return String(s == null ? '' : s).split('\n'); }
  function dsxCapDet(arr, capN) {
    if (arr.length > capN) return arr.slice(0, capN).concat([{ t: '@', s: '…(' + (arr.length - capN) + ' more lines)' }]);
    return arr;
  }
  async function dsxOpDetail(name, args) {
    var out = [];
    try {
      if (name === 'edit_file') {
        dsxSplit(args.old_string).forEach(function (l) { out.push({ t: '-', s: l }); });
        dsxSplit(args.new_string).forEach(function (l) { out.push({ t: '+', s: l }); });
      } else if (name === 'insert_lines') {
        out.push({ t: '@', s: '@ line ' + (args.line | 0) });
        dsxSplit(args.content).forEach(function (l) { out.push({ t: '+', s: l }); });
      } else if (name === 'replace_lines') {
        out.push({ t: '@', s: '@ lines ' + (args.start_line | 0) + '–' + (args.end_line | 0) });
        dsxSplit(args.content).forEach(function (l) { out.push({ t: '+', s: l }); });
      } else if (name === 'append_file') {
        out.push({ t: '@', s: '@ end of file' });
        dsxSplit(args.content || args.text || '').forEach(function (l) { out.push({ t: '+', s: l }); });
      } else if (name === 'write_file') {
        var oldT = '';
        try { oldT = await readText(args.path); } catch (e0) {}
        var om = {}, i, lo = oldT ? oldT.split('\n') : [], ln = dsxSplit(args.content || '');
        for (i = 0; i < lo.length; ++i) om[lo[i]] = (om[lo[i]] || 0) + 1;
        var addLs = [];
        for (i = 0; i < ln.length; ++i) { var k = ln[i]; if (om[k]) om[k]--; else addLs.push(k); }
        var delLs = [];
        for (var kk in om) {
          if (!Object.prototype.hasOwnProperty.call(om, kk) || om[kk] <= 0) continue;
          for (i = 0; i < om[kk]; ++i) delLs.push(kk);
        }
        delLs.forEach(function (l) { out.push({ t: '-', s: l }); });
        addLs.forEach(function (l) { out.push({ t: '+', s: l }); });
        if (!out.length) out.push({ t: '@', s: '(content identical)' });
      }
    } catch (e3) {}
    return dsxCapDet(out, 120);
  }

  OPS.mutateBatch = async function (args) {
    // args: { label, ops: [{name,args}] } — всё под ОДНИМ снапшотом.
    await snapBegin(args.label);
    var report = [], okCount = 0, items = [];
    for (var i = 0; i < args.ops.length; ++i) {
      var op = args.ops[i];
      try {
        var sfx = await dsxOpStat(op.name, op.args || {});
        var det = await dsxOpDetail(op.name, op.args || {}); // ДО mutate (читает старое содержимое)
        var t0 = Date.now();
        var msg = await mutate(op.name, op.args || {});
        var ms = Date.now() - t0;
        report.push('✓ ' + msg + sfx);
        items.push({ name: op.name, msg: msg, stat: sfx.trim(), ok: true, ms: ms, detail: det });
        ++okCount;
      } catch (e) {
        var em = e && e.message ? e.message : String(e);
        report.push('✗ ' + op.name + ' ' + (op.args && (op.args.path || op.args.from) || '') + ' — ' + em);
        items.push({ name: op.name, msg: String(op.name) + ' ' + (op.args && (op.args.path || op.args.from) || ''),
                     stat: '', ok: false, ms: 0, detail: [], err: em });
      }
    }
    await snapCommit();
    report.push('Готово: ' + okCount + ' из ' + args.ops.length +
                ' операций успешно.' + (okCount < args.ops.length ? ' Остальные пропущены.' : ''));
    return { done: okCount, total: args.ops.length, report: report.join('\n'), items: items };
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

  // Откат ОДНОГО файла: самое свежее до-состояние из снапшотов (новейшее → старое).
  // Найденный item консумируется, чтобы последующий rollback не перезаписал поверх.
  OPS.revertFile = async function (args) {
    var rel = String(args.path || '');
    var all = await idbAll('snaps');
    all.sort(function (a, b) { return b.val.ts - a.val.ts; });
    for (var i = 0; i < all.length; ++i) {
      var items = (all[i].val && all[i].val.items) || [];
      for (var j = items.length - 1; j >= 0; --j) {
        var it = items[j];
        if (it.path !== rel) continue;
        if (it.kind === 'created' || it.kind === 'created-dir') {
          var d = await parentOf(rel, false);
          await d.removeEntry(leaf(rel), { recursive: it.kind === 'created-dir' });
        } else if (it.oldText !== null && it.oldText !== undefined) {
          await writeText(rel, it.oldText);
        }
        items.splice(j, 1);
        if (!items.length) {
          await idb('readwrite', function (tx, r) { tx.objectStore('snaps').delete(all[i].key); tx.oncomplete = r; });
        } else {
          await idbSet('snaps', all[i].key, all[i].val);
        }
        return { reverted: true, path: rel, kind: it.kind, label: all[i].val.label || '', ts: all[i].val.ts };
      }
    }
    return { reverted: false, error: 'нет снапшота по этому файлу (он ещё не правился или история стёрта)' };
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

  // ---------- жест: кнопка «Выбрать папку» в оверлейной модалке.
  // Клик по ней ловим ЗДЕСЬ, в MAIN-мире — user activation живой,
  // showDirectoryPicker/requestPermission откроются без проблем.
  function bindGesture() {
    var btns = document.querySelectorAll('.dsx-modal-pick');
    for (var i = 0; i < btns.length; ++i) {
      var el = btns[i];
      if (el.__dsxBound) continue;
      el.__dsxBound = true;
      el.addEventListener('click', function () {
        var origText = el.textContent;
        el.textContent = (window.DSX_T || function (a) { return a; })('Открываю выбор папки…', 'Opening the folder picker…');
        (async function () {
          try {
            if (QT()) {
              await pick();              // Qt: нативный диалог по клику всегда; иначе «Сменить…» обходит диалог
            } else {
              var stt = await ensure();
              if (!stt.ok) await pick();
            }
            window.postMessage({ __dsidefs: 'event', ev: 'folder', name: dirName }, '*');
          } catch (e) {
            window.postMessage({ __dsidefs: 'event', ev: 'folderError',
              error: String((e && e.name) || '') + ': ' + (e && e.message ? e.message : e) }, '*');
            el.textContent = origText;
          }
        })();
      }, true);
    }
  }
  setInterval(bindGesture, 700);

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
      if (m.op === 'forget') {          // «Забыть папку» из настроек — без нужды в папке
        dirHandle = null; dirName = '';
        if (QT()) { try { await qfs('forget', {}); } catch (e2) {} }
        try {
          await idb('readwrite', function (tx, res) {
            tx.objectStore('kv').delete('dirHandle');
            tx.oncomplete = res;
          });
        } catch (e) {}
        reply({ ok: true, data: { forgotten: true } });
        return;
      }
      var st = await ensure();
      if (!st.ok) { reply({ ok: false, error: 'needPick' }); return; }
      if (OPS[m.op]) { reply({ ok: true, data: await OPS[m.op](m.args || {}) }); return; }
      reply({ ok: false, error: 'unknown op: ' + m.op });
    } catch (e) {
      reply({ ok: false, error: String(e && e.message ? e.message : e) });
    }
  });
})();

;
/* =====================================================================
 * content.js — DeepSeek Extended (isolated world): СТЕЛС-автопилот v34.
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

  // ---------------- i18n: язык UI = язык сайта
  var DSX_LANG = (String(document.documentElement.lang || '').toLowerCase().indexOf('ru') === 0) ? 'ru' : 'en';
  var T = function (ru, en) { return DSX_LANG === 'ru' ? ru : en; };

  // ---------------- внедрение MAIN-world скриптов (File System Access мост)
  function inject(tag, attrs) {
    var el = document.createElement(tag);
    for (var k in attrs) el[k] = attrs[k];
    document.documentElement.appendChild(el);
    return el;
  }
  if (!window.__DSX_QT && typeof chrome !== 'undefined' && chrome.runtime && chrome.runtime.getURL) {
    var url = chrome.runtime.getURL;
    inject('script', { src: url('opp.js') });
    inject('script', { src: url('fsbridge.js') });
  }
  // ---------------- транспорт: Qt-приложение (window.dsxCall) ИЛИ chrome-расширение
  // ---------------- лог в консоль приложения
  // python-сторона печатает console.log страницы (QuietPage), JS-ошибки ловим сами.
  function dlog() {
    try {
      var a = ['[DSX]'];
      for (var li = 0; li < arguments.length; ++li) a.push(String(arguments[li]));
      console.log(a.join(' '));
    } catch (e) {}
  }
  window.addEventListener('error', function (e) {
    try { console.log('[DSX-ERR]', e.message, '@', (String(e.filename || '').split('/').pop() || '?') + ':' + (e.lineno || 0)); } catch (_) {}
  });
  window.addEventListener('unhandledrejection', function (e) {
    try { console.log('[DSX-ERR] promise:', String(e.reason && (e.reason.stack || e.reason.message || e.reason)).slice(0, 300)); } catch (_) {}
  });

  function dsxSend(kind, data) {
    return new Promise(function (resolve) {
      try {
        if (typeof window.dsxCall === 'function') {
          window.dsxCall(kind, data).then(resolve, function (e) {
            resolve({ ok: false, error: String(e && e.message || e) });
          });
          return;
        }
        if (kind === 'webfetch') {
          chrome.runtime.sendMessage({ dsx: 'webfetch', url: data.url, dl: !!data.dl }, function (r) {
            if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
            else resolve(r || { ok: false, error: 'пустой ответ фона' });
          });
        } else if (kind === 'shell') {
          chrome.runtime.sendMessage({ dsx: 'shell', cmd: data.cmd }, function (r) {
            if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
            else resolve(r || { ok: false, error: 'пустой ответ фона' });
          });
        } else resolve({ ok: false, error: 'dsxSend: unknown kind ' + kind });
      } catch (e) { resolve({ ok: false, error: String(e && e.message || e) }); }
    });
  }

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
  var CFG_VERSION = 34;
  var cfg = { pilot: false, rules: '', statusRows: true, jbTest: false, jbText: '', memory: [], autolimit: undefined };
  try {
    if (window.__DSX_QT) {
      var rawc = localStorage.getItem('dsx:cfg');
      if (rawc) {
        var vc = JSON.parse(rawc);
        if (vc.pilot !== undefined) cfg.pilot = !!vc.pilot;
        if (typeof vc.rules === 'string') cfg.rules = vc.rules;
        if (vc.statusRows !== undefined) cfg.statusRows = !!vc.statusRows;
        if (vc.jbTest !== undefined) cfg.jbTest = !!vc.jbTest;
        if (typeof vc.jbText === 'string') cfg.jbText = vc.jbText;
        if (Array.isArray(vc.memory)) cfg.memory = clampMemory(vc.memory);
        if (vc.autolimit !== undefined) cfg.autolimit = vc.autolimit | 0;
      }
    } else {
      chrome.storage.local.get(['pilot', 'rules', 'statusRows', 'jbTest', 'jbText', 'memory', 'autolimit'], function (v) {
        if (!v) return;
        if (v.pilot !== undefined) cfg.pilot = !!v.pilot;
        if (typeof v.rules === 'string') cfg.rules = v.rules;
        if (v.statusRows !== undefined) cfg.statusRows = !!v.statusRows;
        if (v.jbTest !== undefined) cfg.jbTest = !!v.jbTest;
        if (typeof v.jbText === 'string') cfg.jbText = v.jbText;
        if (Array.isArray(v.memory)) cfg.memory = clampMemory(v.memory);
        if (v.autolimit !== undefined) cfg.autolimit = v.autolimit | 0;
        syncNativeToggle(); syncFolderLabels();
      });
    }
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
    try {
      if (window.__DSX_QT) { localStorage.setItem('dsx:cfg', JSON.stringify(cfg)); return; }
      chrome.storage.local.set({ v: CFG_VERSION, pilot: cfg.pilot, rules: cfg.rules, statusRows: cfg.statusRows, jbTest: cfg.jbTest, jbText: cfg.jbText, memory: cfg.memory, autolimit: cfg.autolimit, shellAuto: cfg.shellAuto, autoContinue: cfg.autoContinue });
    } catch (e) {}
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
      'color:var(--dsw-alias-label-secondary,#aaa)" title="' + T('Закрыть', 'Close') + '">✕</div></div>';
  }
  function bindModalChrome(card) {
    card.querySelector('.dsx-modal-x').onclick = closeModals;
  }
  function closeModals() {
    document.querySelectorAll('.dsx-modal-ov').forEach(function (el) { el.remove(); });
  }
  function syncFolderLabels() {
    document.querySelectorAll('.dsx-cur-folder').forEach(function (el) {
      el.textContent = st.folderName ? st.folderName : T('— папка не выбрана —', '— no folder selected —');
    });
  }

  // ---------------- модалка «Подключение автопилота»
  function showFolderModal() {
    var card = overlayBase(430);
    card.innerHTML =
      modalHeader(T('Авто-пилот', 'Auto-pilot')) +
      '<div style="padding:14px 20px 20px">' +
        '<div style="font-size:14px;line-height:1.6;color:var(--dsw-alias-label-secondary,#ccc)">' +
          T('Авто-пилот будет редактировать прямо в папке проекта. Выбери её один раз — дальше агент сам читает файлы, пишет правки, запускает тесты и возвращает результат. В чате это выглядит как обычный диалог.',
            'Auto-pilot edits right inside your project folder. Pick it once — the agent then reads files, applies edits, runs tests and reports back. In chat it still looks like a normal conversation.') +
        '</div>' +
        '<div style="display:flex;gap:10px;margin-top:16px;justify-content:flex-end">' +
          '<button class="dsx-modal-later" style="padding:9px 16px;cursor:pointer;border-radius:10px;' +
          'font-size:13px;font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
          'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
          'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))">' + T('Позже', 'Later') + '</button>' +
          '<button class="dsx-modal-pick" style="padding:9px 16px;border:none;cursor:pointer;border-radius:10px;' +
          'font-size:13px;font-weight:600;font-family:inherit;color:#fff;' +
          'background:var(--dsw-alias-brand-primary,#5686fe)">' + T('Выбрать папку проекта', 'Choose project folder') + '</button>' +
        '</div>' +
      '</div>';
    bindModalChrome(card);
    card.querySelector('.dsx-modal-later').onclick = function () {
      cfg.pilot = false; saveCfg(); syncNativeToggle(); closeModals();
    };
  }

  // ---------------- нативный ds-switch сайта для наших переключателей
  function setDsSwitch(sw, on) {
    if (!sw) return;
    sw.classList.toggle('ds-switch--checked', on);
    sw.setAttribute('aria-checked', on ? 'true' : 'false');
  }

  function syncNativeToggle() {
    document.querySelectorAll('.dsx-set-pilot').forEach(function (sw) { setDsSwitch(sw, !!cfg.pilot); });
  }

  // ---------------- настройки: строки как у нативных Settings сайта, автосейв
  var SET_ROW = 'display:flex;align-items:center;justify-content:space-between;gap:14px;padding:12px 0;min-height:52px;border-bottom:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.06))';
  var SET_TIT = 'font-size:14px;font-weight:500;color:var(--dsw-alias-label-primary,#eee)';
  var SET_INP = 'background:var(--dsw-alias-bg-layer-2,#1d1f24);color:var(--dsw-alias-label-primary,#f5f5f5);border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));outline:none;border-radius:10px;font-family:inherit;font-size:13px';
  var SET_BTN = 'padding:8px 14px;border:none;cursor:pointer;border-radius:10px;font-size:13px;font-weight:600;font-family:inherit;color:#fff;flex-shrink:0;background:var(--dsw-alias-brand-primary,#5686fe)';
  var SET_GHOST = 'padding:8px 12px;cursor:pointer;border-radius:10px;font-size:13px;font-family:inherit;flex-shrink:0;color:var(--dsw-alias-label-primary,#eee);background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))';

  function sw(cls) {
    return '<button type="button" role="switch" class="ds-switch ' + cls + '"><div class="ds-switch-thumb"></div></button>';
  }
  function bindSwitch(root, cls, get, set) {
    var s = root.querySelector(cls);
    if (!s) return null;
    setDsSwitch(s, get());
    s.onclick = function () { set(!get()); setDsSwitch(s, get()); saveCfg(); };
    return s;
  }
  function dsxDebounce(fn, ms) { var t; return function () { clearTimeout(t); t = setTimeout(fn, ms); }; }

  // ====== вкладка «Агент»
  function agentPaneHtml() {
    return '' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('Авто-пилот', 'Auto-pilot') + '</div>' + sw('dsx-set-pilot') + '</div>' +
      '<div style="' + SET_ROW + '">' +
        '<div style="' + SET_TIT + ';flex:1;min-width:0">' + T('Папка проекта', 'Project folder') +
          '<div class="dsx-cur-folder" style="font-size:12px;font-weight:400;color:var(--dsw-alias-label-tertiary,#888);overflow:hidden;text-overflow:ellipsis;white-space:nowrap"></div>' +
        '</div>' +
        '<button class="dsx-modal-pick" style="' + SET_BTN + '">' + T('Сменить…', 'Change…') + '</button>' +
        '<button class="dsx-set-forget" style="' + SET_GHOST + '">' + T('Забыть', 'Forget') + '</button>' +
      '</div>' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('Шелл без подтверждения', 'Run shell without asking') + '</div>' + sw('dsx-set-shellauto') + '</div>' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('Авто-нажатие «Continue»', 'Auto-press “Continue”') + '</div>' + sw('dsx-set-cont') + '</div>' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('Строки о ходе работы', 'Work-status lines') + '</div>' + sw('dsx-set-rows') + '</div>' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('Лимит авто-ходов (0 — без лимита)', 'Auto-turn limit (0 — unlimited)') + '</div>' +
        '<input class="dsx-set-limit" type="number" min="0" max="999" step="1" style="width:84px;padding:7px 10px;text-align:center;flex-shrink:0;' + SET_INP + '">' +
      '</div>';
  }
  function bindAgentPane(root) {
    syncFolderLabels();
    bindSwitch(root, '.dsx-set-pilot', function () { return cfg.pilot; }, function (v) {
      cfg.pilot = v;
      if (v && !st.folderReady) showFolderModal();
    });
    bindSwitch(root, '.dsx-set-shellauto', function () { return cfg.shellAuto === true; }, function (v) { cfg.shellAuto = v; });
    bindSwitch(root, '.dsx-set-cont', function () { return cfg.autoContinue !== false; }, function (v) { cfg.autoContinue = v; });
    bindSwitch(root, '.dsx-set-rows', function () { return cfg.statusRows !== false; }, function (v) { cfg.statusRows = v; });
    var limIn = root.querySelector('.dsx-set-limit');
    limIn.value = (cfg.autolimit === 0) ? '0' : String(cfg.autolimit || 12);
    limIn.onchange = function () {
      var lv = parseInt(limIn.value, 10);
      cfg.autolimit = (isNaN(lv) || lv < 0) ? 12 : (lv > 999 ? 999 : lv);
      limIn.value = String(cfg.autolimit);
      saveCfg();
    };
    root.querySelector('.dsx-set-forget').onclick = function () {
      fsCall('forget').then(function () {
        st.folderReady = false; st.folderName = ''; st.bigPrompt = ''; st.primedOnce = false;
        sessionStorage.removeItem('dsx:primed');
        syncNativeToggle(); syncFolderLabels();
      });
    };
  }

  // ====== вкладка «Изображения» (эксперимент)
  function imagesPaneHtml() {
    return '' +
      '<div style="' + SET_ROW + '"><div style="' + SET_TIT + '">' + T('[EXP] Генерация изображений', '[EXPERIMENTAL] Image generation') + '</div>' + sw('dsx-set-imgon') + '</div>' +
      '<div style="' + SET_ROW + ';min-height:52px">' +
        '<div style="' + SET_TIT + '">' + T('Модель', 'Model') + '</div>' +
        '<div style="display:flex;gap:8px;flex-shrink:0">' +
          '<button type="button" class="dsx-imgmodel" data-v="default" style="' + SET_GHOST + '">Default</button>' +
          '<button type="button" class="dsx-imgmodel" data-v="text" style="' + SET_GHOST + '">Text</button>' +
        '</div>' +
      '</div>' +
      '<div style="padding:12px 0 2px">' +
        '<div style="' + SET_TIT + ';margin-bottom:8px">' + T('API-ключ (pollinations)', 'API key (pollinations)') + '</div>' +
        '<input class="dsx-set-imgkey" type="password" placeholder="sk_…" autocomplete="off" spellcheck="false" ' +
        'style="width:100%;box-sizing:border-box;padding:9px 12px;' + SET_INP + '">' +
      '</div>';
  }
  function paintImgModels(root) {
    var cur = cfg.imgModel || 'default';
    root.querySelectorAll('.dsx-imgmodel').forEach(function (b) {
      var on = b.getAttribute('data-v') === cur;
      b.style.background = on ? 'var(--dsw-alias-interactive-bg-hover,rgba(255,255,255,.14))'
                              : 'var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07))';
      b.style.fontWeight = on ? '600' : '400';
      b.style.borderColor = on ? 'var(--dsw-alias-brand-primary,#5686fe)' : 'var(--dsw-alias-border-l2,rgba(255,255,255,.1))';
    });
  }
  function bindImagesPane(root) {
    bindSwitch(root, '.dsx-set-imgon', function () { return !!cfg.imgOn; }, function (v) { cfg.imgOn = v; });
    var key = root.querySelector('.dsx-set-imgkey');
    key.value = cfg.imgKey || '';
    key.addEventListener('input', dsxDebounce(function () { cfg.imgKey = key.value.trim(); saveCfg(); }, 450));
    root.querySelectorAll('.dsx-imgmodel').forEach(function (b) {
      b.onclick = function () { cfg.imgModel = b.getAttribute('data-v'); saveCfg(); paintImgModels(root); };
    });
    paintImgModels(root);
  }

  // ====== вкладка «Контекст»
  function contextPaneHtml() {
    return '' +
      '<div style="padding:12px 0;border-bottom:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.06))">' +
        '<div style="' + SET_TIT + ';margin-bottom:8px">' + T('Начальный промпт', 'Initial prompt') + '</div>' +
        '<textarea class="dsx-set-rules" rows="6" placeholder="' + T('Правила для DeepSeek… (можно пусто)', 'Rules for DeepSeek… (can be empty)') + '" style="width:100%;box-sizing:border-box;padding:10px 12px;resize:vertical;line-height:1.5;' + SET_INP + '"></textarea>' +
      '</div>' +
      '<div style="padding:12px 0;border-bottom:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.06))">' +
        '<div style="' + SET_TIT + ';margin-bottom:8px">' + T('Память проекта', 'Project memory') + '</div>' +
        '<div class="dsx-mem-list" style="max-height:240px;overflow-y:auto;display:flex;flex-direction:column;gap:8px"></div>' +
        '<button class="dsx-mem-add" style="margin-top:8px;' + SET_GHOST + '">' + T('+ Добавить заметку', '+ Add note') + '</button>' +
      '</div>' +
      '<div style="padding:12px 0">' +
        '<div style="' + SET_ROW + ';border-bottom:none;min-height:0;padding:0 0 10px">' +
          '<div style="' + SET_TIT + '">' + T('Тест устойчивости (jailbreak-check)', 'Robustness test (jailbreak-check)') + '</div>' + sw('dsx-set-jb') +
        '</div>' +
        '<textarea class="dsx-set-jbtext" rows="4" placeholder="' + T('Тестовый промпт…', 'Test prompt…') + '" style="width:100%;box-sizing:border-box;padding:10px 12px;resize:vertical;line-height:1.5;' + SET_INP + '"></textarea>' +
      '</div>';
  }
  function bindContextPane(root) {
    var ta = root.querySelector('.dsx-set-rules');
    var jt = root.querySelector('.dsx-set-jbtext');
    ta.value = cfg.rules || ''; jt.value = cfg.jbText || '';
    var saveText = dsxDebounce(function () {
      cfg.rules = ta.value;        // без лимита символов
      cfg.jbText = jt.value;       // без лимита символов
      saveCfg(); refreshPrompt();
    }, 450);
    ta.addEventListener('input', saveText);
    jt.addEventListener('input', saveText);
    bindSwitch(root, '.dsx-set-jb', function () { return !!cfg.jbTest; }, function (v) { cfg.jbTest = v; refreshPrompt(); });
    var memList = root.querySelector('.dsx-mem-list');
    var saveMem = dsxDebounce(function () {
      var notes = [];
      memList.querySelectorAll('.dsx-mem-card').forEach(function (w) {
        var t = w.querySelector('.dsx-mem-t').value.trim();
        var x = w.querySelector('.dsx-mem-x').value.trim();
        if (t || x) notes.push({ t: t.slice(0, 120), x: x.slice(0, 12000) });
      });
      cfg.memory = clampMemory(notes);
      saveCfg(); refreshPrompt();
    }, 450);
    function addMemCard(n) {
      var wrap = document.createElement('div');
      wrap.className = 'dsx-mem-card';
      wrap.style.cssText = 'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));' +
        'border-radius:10px;padding:8px;background:var(--dsw-alias-bg-layer-2,#1d1f24)';
      var ti = document.createElement('input');
      ti.className = 'dsx-mem-t'; ti.type = 'text';
      ti.placeholder = T('Заголовок', 'Title');
      ti.value = (n && n.t) || '';
      ti.style.cssText = 'width:100%;box-sizing:border-box;padding:7px 10px;border-radius:8px;' +
        'font-size:12.5px;font-family:inherit;background:transparent;' +
        'color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      var xa = document.createElement('textarea');
      xa.className = 'dsx-mem-x'; xa.rows = 3;
      xa.placeholder = T('Суть…', 'The core…');
      xa.value = (n && n.x) || '';
      xa.style.cssText = 'width:100%;box-sizing:border-box;margin-top:6px;padding:7px 10px;' +
        'resize:vertical;border-radius:8px;font-size:12.5px;line-height:1.45;font-family:inherit;' +
        'background:transparent;color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      var del = document.createElement('button');
      del.textContent = T('Удалить', 'Delete');
      del.style.cssText = 'margin-top:6px;padding:5px 10px;cursor:pointer;border-radius:8px;' +
        'font-size:12px;font-family:inherit;color:#ff8f8f;background:transparent;' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12))';
      del.onclick = function () { wrap.remove(); saveMem(); };
      ti.addEventListener('input', saveMem); xa.addEventListener('input', saveMem);
      wrap.appendChild(ti); wrap.appendChild(xa); wrap.appendChild(del);
      memList.appendChild(wrap);
    }
    (Array.isArray(cfg.memory) ? cfg.memory : []).forEach(addMemCard);
    root.querySelector('.dsx-mem-add').onclick = function () { addMemCard({ t: '', x: '' }); saveMem(); };
  }

  // ---------------- наша модалка настроек (резерв; сайт-табы — основной путь)
  function showSettingsModal() {
    var card = overlayBase(560);
    card.innerHTML = modalHeader(T('Настройки агента', 'Agent settings')) +
      '<div style="padding:0 20px"><div style="font-size:16px;font-weight:600;padding:12px 0 2px">' + T('Агент', 'Agent') + '</div></div>' +
      '<div class="dsx-pane-agent" style="padding:0 20px"></div>' +
      '<div style="padding:0 20px"><div style="font-size:16px;font-weight:600;padding:12px 0 2px">' + T('Изображения', 'Images') + '</div></div>' +
      '<div class="dsx-pane-images" style="padding:0 20px"></div>' +
      '<div style="padding:0 20px"><div style="font-size:16px;font-weight:600;padding:12px 0 2px">' + T('Контекст', 'Context') + '</div></div>' +
      '<div class="dsx-pane-context" style="padding:0 20px 18px"></div>';
    bindModalChrome(card);
    var pa = card.querySelector('.dsx-pane-agent'); pa.innerHTML = agentPaneHtml(); bindAgentPane(pa);
    var pi = card.querySelector('.dsx-pane-images'); pi.innerHTML = imagesPaneHtml(); bindImagesPane(pi);
    var pc = card.querySelector('.dsx-pane-context'); pc.innerHTML = contextPaneHtml(); bindContextPane(pc);
  }

  // ---------------- две вкладки ВНУТРИ нативного Settings сайта
  function findSiteSettingsModal() {
    var title = document.querySelector('.ds-modal-content__title');
    if (!title) return null;
    var tt = (title.textContent || '').trim();
    if (tt !== 'Settings' && tt !== 'Настройки') return null;
    return title.closest('.ds-modal-content');
  }
  function openDsxPane(modal, content, tab, kind) {
    var pane = content.querySelector('.dsx-agent-pane[data-kind="' + kind + '"]');
    if (!pane) {
      pane = document.createElement('div');
      pane.className = 'dsx-agent-pane';
      pane.setAttribute('data-kind', kind);
      pane.style.cssText = 'font-size:14px;color:var(--dsw-alias-label-primary,#eee);padding:4px 2px 16px';
      if (kind === 'agent') { pane.innerHTML = agentPaneHtml(); bindAgentPane(pane); }
      else if (kind === 'images') { pane.innerHTML = imagesPaneHtml(); bindImagesPane(pane); }
      else { pane.innerHTML = contextPaneHtml(); bindContextPane(pane); }
      content.appendChild(pane);
    }
    Array.prototype.forEach.call(content.children, function (c) {
      if (c === pane) { c.style.display = ''; }
      else if (c.classList.contains('dsx-agent-pane')) { c.style.display = 'none'; }
      else if (c.style.display !== 'none') { c.dataset.dsxHidden = '1'; c.style.display = 'none'; }
    });
    var rail = modal.querySelector('.d316d158');
    if (rail) {
      rail.querySelectorAll('.ds-button').forEach(function (b) {
        b.classList.remove('_699d482');
        b.style.removeProperty('--dsl-button-color'); // нативный актив держится ещё и на css-var
      });
    }
    tab.classList.add('_699d482');
    tab.style.setProperty('--dsl-button-color', 'var(--dsw-alias-interactive-bg-hover)');
  }
  function addSiteTab(modal, rail, content, label, kind) {
    var srcBtn = rail.querySelector('.ds-button');
    var tab = srcBtn.cloneNode(true);
    tab.classList.add('dsx-site-tab');
    tab.classList.remove('_699d482');
    var lbl = tab.querySelector('.ds-button__content');
    if (lbl) lbl.textContent = label;
    tab.addEventListener('click', function () { openDsxPane(modal, content, tab, kind); });
    rail.appendChild(tab);
  }
  function ensureSiteSettingsTab() {
    var modal = findSiteSettingsModal();
    if (!modal) return;
    var rail = modal.querySelector('.d316d158');
    var content = modal.querySelector('.ds-scroll-area__content');
    if (!rail || !content) return;
    // миграция: снести старую пилюлю у композера и шестерёнку, если остались
    document.querySelectorAll('.dsx-pilot-tg,.dsx-gear-btn').forEach(function (n) { n.remove(); });
    if (!rail.dataset.dsxTabs) {
      rail.dataset.dsxTabs = '1';
      addSiteTab(modal, rail, content, T('Агент', 'Agent'), 'agent');
      addSiteTab(modal, rail, content, T('Изображения', 'Images'), 'images');
      addSiteTab(modal, rail, content, T('Контекст', 'Context'), 'context');
      rail.addEventListener('click', function (ev) {
        var b = ev.target && ev.target.closest ? ev.target.closest('.ds-button') : null;
        if (!b || b.classList.contains('dsx-site-tab')) return;
        // клик по родному табу: вернуть сайтовый контент
        Array.prototype.forEach.call(content.children, function (c) {
          if (c.classList.contains('dsx-agent-pane')) { c.style.display = 'none'; return; }
          if (c.dataset.dsxHidden === '1') { c.style.display = ''; delete c.dataset.dsxHidden; }
        });
        rail.querySelectorAll('.dsx-site-tab').forEach(function (t2) {
          t2.classList.remove('_699d482');
          t2.style.removeProperty('--dsl-button-color');
        });
      }, true);
    }
  }
  try {
    new MutationObserver(function () { try { ensureSiteSettingsTab(); } catch (e) {} })
      .observe(document.documentElement, { childList: true, subtree: true });
  } catch (e) {}
  setInterval(ensureSiteSettingsTab, 1500); // страховка

  // ---------------- события моста (папка выбрана жестом в MAIN-мире)
  function onBridgeEvent(m) {
    if (m.ev === 'folder') {
      st.folderReady = true; st.folderName = m.name || '';
      closeModals(); // модалка «Выбрать папку» уходит сразу после Qt-диалога
      syncNativeToggle(); syncFolderLabels();
      toast(T('Проект подключён: ', 'Project connected: ') + st.folderName);
      refreshPrompt();
    } else if (m.ev === 'folderError') {
      var err = String(m.error || '');
      if (err.indexOf('AbortError') < 0 && err.toLowerCase().indexOf('abort') < 0)
        toast(T('Не вышло открыть папку: ', 'Could not open the folder: ') + err);
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
  // сайт автоматически раздувает textarea по input — при служебных инжектах лочим высоту
  function clampComposer(el) {
    var h = el.clientHeight || 46;
    el.style.height = h + 'px';
    el.style.maxHeight = h + 'px';
    el.style.overflow = 'hidden';
    return function () {
      el.style.height = ''; el.style.maxHeight = ''; el.style.overflow = '';
    };
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
        toast(T('Включи автопилот: Settings → «Агент» (нужна папка проекта)', 'Enable auto-pilot: Settings → “Agent” (a project folder is required)'));
      }
      console.log('[DSX] пропуск: папка не подключена (folderReady=false)');
      fsCall('ensure').then(function (en) {
        if (en.ok && en.data.ok) refreshPrompt();
      });
      return;
    }
    var prefix, chip;
    if (st.primedOnce) { prefix = shortPrefix(); chip = T('Автопилот', 'Autopilot'); }
    else if (st.bigPrompt) { prefix = st.bigPrompt; chip = T('Контекст проекта загружен', 'Project context loaded'); }
    else { prefix = shortPrefix(); chip = T('Автопилот', 'Autopilot'); }
    var unclampU = clampComposer(editor); // прайминг пользовательского мессаджа тоже не раздувает
    nativeSetValue(editor, wrapStealth(prefix, chip, v.replace(/^\s+/, '')));
    setTimeout(unclampU, 600);
    if (!st.primedOnce && st.bigPrompt) {
      st.primedOnce = true; sessionStorage.setItem('dsx:primed', '1');
      // без тоста: юзеру тихо
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

  // ---------------- защита от повторов (скролл/перерисовка списка не должна
  // перезапускать выполненное: ops, shell, ask — всё по ключу ответа)
  function actRead() {
    try { var a = JSON.parse(sessionStorage.getItem('dsx:acts') || 'null'); if (a) return a; } catch (e) {}
    return { ops: [], sh: [], ask: [] };
  }
  function actSeen(bucket, key) { return actRead()[bucket].indexOf(key) >= 0; }
  function actMark(bucket, key) {
    var a = actRead();
    if (a[bucket].indexOf(key) >= 0) return;
    a[bucket].push(key);
    if (a[bucket].length > 500) a[bucket] = a[bucket].slice(-500);
    try { sessionStorage.setItem('dsx:acts', JSON.stringify(a)); } catch (e) {}
  }
  function actReset() { try { sessionStorage.removeItem('dsx:acts'); } catch (e) {} }

  // ---------------- групповая отправка: несколько карточек = ОДИН ответ модели
  async function groupOrSend(block, card, text, logTag) {
    card.setAttribute('data-done', '1');
    block.__dsxOut = block.__dsxOut || [];
    block.__dsxOut.push(text);
    var host = block.closest('._4f9bf79') || block.parentElement;
    var pend = host ? host.querySelectorAll('[data-dsx-card]:not([data-done])').length : 0;
    if (pend > 0) return 'wait';
    var parts = block.__dsxOut; block.__dsxOut = [];
    var ok = await injectStealth(parts.join('\n\n'), logTag);
    if (!ok) return 'fail';
    st.autoCount++;
    st.waitingAnswer = true; sessionStorage.setItem('dsx:wait', '1');
    sessionStorage.setItem('dsx:auto', String(st.autoCount));
    return 'sent';
  }

  // ---------------- автоклик «Continue/Продолжить» (оборванная генерация)
  var lastContinueAt = 0;
  function clickContinueIfAny() {
    if (!cfg.pilot || cfg.autoContinue === false) return;
    if (Date.now() - lastContinueAt < 8000) return; // один клик в 8 сек максимум
    // родной чип «Continue» сайта — точный селектор из разметки
    var btn = document.querySelector('._8e85838 [role="button"]');
    if (!btn || !btn.isConnected || btn.offsetParent === null) {
      btn = null; // fallback: ищем подпись в хвосте чата
      var cands = document.querySelectorAll('div[role="button"], button');
      for (var i = cands.length - 1; i >= 0; --i) {
        var el = cands[i];
        if (!el || !el.isConnected || el.offsetParent === null) continue;
        if (el.closest('._871cbca')) continue; // наш индикатор «Думаю…» — не то
        var t = (el.textContent || '').trim();
        if (t !== 'Continue' && t !== 'Продолжить' && t !== 'Повторить' && t !== 'Continue generating') continue;
        btn = el; break;
      }
    }
    if (!btn) return;
    var ev = new MouseEvent('click', { bubbles: true });
    ev.__dsxBypass = true;
    btn.dispatchEvent(ev);
    lastContinueAt = Date.now();
    console.log('[DSX] нажал «Continue» — генерация продолжается');
  }
  setInterval(function () { try { clickContinueIfAny(); } catch (e) {} }, 1500);

  // ---------------- ввод в чат (служебные сообщения автопилота — настоящие
  // отправки на сервер, чтобы после перезагрузки модель их видела)
  async function injectStealth(real, logTag) {
    var el = findEditor();
    if (!el) { toast(T('Не нашёл поле ввода чата', 'Chat input not found')); return false; }
    el.focus();
    el.style.visibility = 'hidden'; // юзер не видит вливаемый служебный текст
    dlog('inject →', real.length, 'зн', logTag ? '(' + logTag + ')' : '');
    var unclamp = clampComposer(el); // и textarea не раздувается от большого текста
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
    setTimeout(function () { el.style.visibility = ''; unclamp(); }, 450);
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
  function webFetchCall(url, dl) { return dsxSend('webfetch', { url: url, dl: !!dl }); }
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
    if (uniq.length) dlog('serviceNote: распознано', uniq.length, 'запр:', uniq.map(function (r) { return r.kind + (r.extra ? ' → ' + r.extra : ''); }).join(' | '));
    if (!uniq.length) return { text: '', rows: [] };

    var out = ['SYSTEM: auto-reply from DeepSeek Extended.'], rows = [], asks = [], shells = [], imgs = [];
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
          if (!rq.bracket) rows.push(T('Прочитано: ', 'Read: ') + base(p));
        } else {
          out.push('FILE "' + p + '" — READ ERROR: ' + (r.error || 'не читается') +
                   '\n(проверьте имя через [<{NEED SEARCH: часть имени}>])');
          if (!rq.bracket) rows.push(T('Не удалось прочитать: ', 'Failed to read: ') + base(p));
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
          if (!rq.bracket) rows.push(T('Размер: ', 'Size: ') + base(ip) + ' — ' + lns + ' строк');
        } else {
          out.push('INFO "' + ip + '" — ERROR: ' + (inf.error || 'не читается'));
          if (!rq.bracket) rows.push(T('Не удалось осмотреть: ', 'Failed to inspect: ') + base(ip));
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
          if (!rq.bracket) rows.push(T('Прочитано: ', 'Read: ') + base(rp) + ' (' + rf + '–' + rt + ')');
        } else {
          out.push('RANGE "' + rp + '" — READ ERROR: ' + (rr.error || 'не читается'));
          if (!rq.bracket) rows.push(T('Не удалось прочитать: ', 'Failed to read: ') + base(rp));
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
        if (!rq.bracket) rows.push(T('Память «', 'Memory “') + mq + '»' + (hits.length ? ': найдено' : ' — пусто'));
        done++;
      } else if (rq.kind === 'WEB FETCH') {
        var wu = rq.arg;
        if (st.sentWeb[wu]) continue;
        st.sentWeb[wu] = 1;
        var fin1 = progress ? progress(T('Загрузка: ', 'Fetching: ') + hostOf(wu) + '…') : null;
        var wf = await webFetchCall(wu);
        dlog('WEB FETCH:', wu, '→', (wf && wf.ok) ? ('HTTP ' + wf.status + ', ' + (wf.type || '?')) : ('ERR ' + ((wf && wf.error) || '?')));
        if (fin1) fin1(wf.ok ? T('Загружено: ', 'Fetched: ') + hostOf(wu) : T('Не загрузилось: ', 'Fetch failed: ') + hostOf(wu));
        if (wf.ok) {
          var wtxt = htmlToText(wf.text || '', wf.type || '');
          if (!wtxt.trim()) {
            out.push('PAGE "' + wu + '": fetched (HTTP ' + wf.status + ') but no readable text — ' +
                     (String(wf.type || '').indexOf('html') >= 0 ? 'JS-heavy SPA?' : 'empty body') + '. Try documentation mirrors or another URL.');
          } else {
            if (wtxt.length > 25000) wtxt = wtxt.slice(0, 25000) + '\n…(truncated at 25k chars; fetch a more specific page if needed)';
            out.push('PAGE "' + wu + '" (HTTP ' + wf.status + ', ' + (wf.type || '?') + '):\n```\n' + wtxt + '\n```');
          }
          if (!rq.bracket) rows.push(T('Загружено: ', 'Fetched: ') + hostOf(wu));
        } else {
          out.push('PAGE "' + wu + '": FETCH FAILED — ' + (wf.error || 'сеть недоступна'));
          if (!rq.bracket) rows.push(T('Не загрузилось: ', 'Fetch failed: ') + hostOf(wu));
        }
        done++;
      } else if (rq.kind === 'WEB DOWNLOAD') {
        var durl = rq.arg, dpath = String(rq.extra || '');
        var dkey = durl + '|' + dpath;
        if (st.sentDl[dkey]) continue;
        st.sentDl[dkey] = 1;
        var finD = progress ? progress(T('Скачивает: ', 'Downloading: ') + base(dpath) + ' ← ' + hostOf(durl) + '…') : null;
        var dwn = await webFetchCall(durl, true);
        if (dwn.ok && dwn.b64) {
          var wb = await fsCall('writeB64', { path: dpath, b64: dwn.b64 });
          if (wb.ok) {
            var kb = dwn.size > 1048576 ? (dwn.size / 1048576).toFixed(1) + ' MB' : Math.round(dwn.size / 1024) + ' KB';
            out.push('DOWNLOAD saved: «' + dpath + '» — ' + dwn.size + ' bytes (' + (dwn.type || '?') + ') from ' + durl + '. The file is in the project now.');
            if (!rq.bracket) rows.push(T('Скачано: ', 'Downloaded: ') + base(dpath) + ' (' + kb + ')');
            if (finD) finD(T('Скачано: ', 'Downloaded: ') + base(dpath) + ' (' + kb + ')');
            refreshPrompt();
          } else {
            out.push('DOWNLOAD «' + dpath + '»: write FAILED — ' + (wb.error || '?'));
            if (!rq.bracket) rows.push(T('Не сохранилось: ', 'Save failed: ') + base(dpath));
            if (finD) finD(T('Не сохранилось: ', 'Save failed: ') + base(dpath));
          }
        } else {
          out.push('DOWNLOAD from ' + durl + ': FETCH FAILED — ' + (dwn.error || '?'));
          if (!rq.bracket) rows.push(T('Не скачалось: ', 'Download failed: ') + hostOf(durl));
          if (finD) finD(T('Не скачалось: ', 'Download failed: ') + hostOf(durl));
        }
        done++;
      } else if (rq.kind === 'WEB GREP') {
        var gurl = rq.arg, gq = String(rq.extra || '');
        var gkey = gurl + '|' + gq.toLowerCase();
        if (st.sentGrep[gkey]) continue;
        st.sentGrep[gkey] = 1;
        var finG = progress ? progress(T('Ищет в инете: «', 'Searching the web: “') + gq + '» на ' + hostOf(gurl) + '…') : null;
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
            if (finG) finG(T('Прочитано в инете: строки ', 'Read on the web: lines ') + gf + '–' + gt + ' (' + hostOf(gurl) + ')');
            if (!rq.bracket) rows.push(T('Строки ', 'Lines ') + gf + '–' + gt + ': ' + hostOf(gurl));
          } else { // подстрока → ПОЛНЫЕ строки с номерами
            for (gi = 0; gi < glines.length && gout.length < 40; ++gi) {
              if (glines[gi].indexOf(gq) >= 0) gout.push('L' + (gi + 1) + ': ' + glines[gi].trim().slice(0, 300));
            }
            if (gout.length) {
              out.push('GREP "' + gurl + '" for «' + gq + '» — ' + gout.length + ' hit(s), FULL lines:\n```\n' + gout.join('\n').slice(0, 20000) + '\n```');
              if (finG) finG(T('Найдено в инете: «', 'Found on the web: “') + gq + '» — ' + gout.length + ' совпад.');
            } else {
              out.push('GREP "' + gurl + '" for «' + gq + '» — no hits on the page (' + glines.length + ' lines scanned). Try another spelling or fetch the page first.');
              if (finG) finG(T('Не найдено в инете: «', 'Nothing found on the web: “') + gq + '»');
            }
            if (!rq.bracket) rows.push(T('Искал в инете: «', 'Searched the web: “') + gq + '»' + (gout.length ? ' — ' + gout.length + ' совпад.' : ' — пусто'));
          }
        } else {
          out.push('GREP "' + gurl + '": FETCH FAILED — ' + (gp.error || '?'));
          if (finG) finG(T('Не открылось: ', 'Couldn’t open: ') + hostOf(gurl));
          if (!rq.bracket) rows.push(T('Не открылось: ', 'Couldn’t open: ') + hostOf(gurl));
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
          if (!rq.bracket) rows.push(T('Память переполнена: ', 'Memory is full: ') + mt);
        } else {
          if (!merged) cfg.memory.push({ t: mt.slice(0, 120), x: mb.slice(0, 2000) });
          cfg.memory = clampMemory(cfg.memory);
          saveCfg();
          out.push('MEMORY SAVED «' + mt + '»' + (merged ? ' (appended to the existing note)' : ' (new note)') +
                   '. Total: ' + cfg.memory.length + ' note(s). It is now searchable via MEMORY SEARCH.');
          if (!rq.bracket) rows.push(T('Запомнил: ', 'Memorized: ') + mt);
        }
        done++;
      } else if (rq.kind === 'REVERT FILE') {
        var vp = rq.arg;
        var rv = await fsCall('revertFile', { path: vp });
        if (rv.ok && rv.data && rv.data.reverted) {
          out.push('REVERT "' + vp + '": done — restored its pre-edit state from snapshot «' + (rv.data.label || 'без имени') + '»' +
                   (rv.data.kind === 'created' ? ' (your edit had created it, so the file was REMOVED)' : ' (old content written back)') +
                   '. The file now differs from your last edit — re-read it before editing again.');
          if (!rq.bracket) rows.push(T('Откачено: ', 'Reverted: ') + base(vp));
          refreshPrompt(); // содержимое изменилось
        } else {
          out.push('REVERT "' + vp + '": FAILED — ' + ((rv.data && rv.data.error) || rv.error || 'нет снапшота'));
          if (!rq.bracket) rows.push(T('Откат не удался: ', 'Revert failed: ') + base(vp));
        }
        done++;
      } else if (rq.kind === 'SHELL') {
        shells.push({ cmd: rq.arg });
        if (!rq.bracket) rows.push(T('Команда: ', 'Command: ') + rq.arg.slice(0, 80));
      } else if (rq.kind === 'IMAGE FILE' || rq.kind === 'IMAGE SHOW') {
        var ik = (rq.kind === 'IMAGE FILE' ? 'if|' + (rq.extra || '') : 'is|' + rq.arg).toLowerCase();
        st.sentImgs = st.sentImgs || {};
        if (st.sentImgs[ik]) continue;
        st.sentImgs[ik] = 1;
        if (!cfg.imgOn) {
          done++;
          out.push('IMAGE request NOT executed: image generation is DISABLED (owner panel: Settings → Images). Briefly tell the user and continue without the image.');
          dlog('IMG пропуск: генерация выключена (Settings → Изображения)');
          rows.push(T('Картинка пропущена (генерация выкл.)', 'Image skipped (generation off)'));
          continue;
        }
        var imodel = cfg.imgModel === 'text' ? 'microsoft/mai-image-2.6-flash' : 'black-forest-labs/flux.1-schnell';
        imgs.push({ kindg: rq.kind === 'IMAGE FILE' ? 'file' : 'show', prompt: rq.arg, path: rq.extra || '', model: imodel });
        dlog('IMG в очередь:', rq.kind, '|', imodel, '|', String(rq.arg).slice(0, 70));
        if (!rq.bracket) rows.push(T('Рисует картинку…', 'Painting an image…'));
      } else if (rq.kind === 'ASK' || rq.kind === 'ASK MULTI') {
        asks.push({ q: rq.arg, opts: rq.opts || [], multi: rq.kind === 'ASK MULTI' });
        if (!rq.bracket) rows.push(T('Ждёт ответа: ', 'Waiting for your reply: ') + rq.arg);
      } else { // NEED SEARCH
        var q = rq.arg;
        if (st.sentSearches[q]) continue;
        st.sentSearches[q] = 1;
        var s = await fsCall('search', { query: q });
        var sb = s.ok ? s.data.text : ('ERROR: ' + (s.error || ''));
        if (sb.length > 60000) sb = sb.slice(0, 60000) + '\n…(truncated)';
        out.push('SEARCH "' + q + '" — substring matches over ALL text files of the project (file:line: text):\n```\n' + sb + '\n```');
        if (!rq.bracket) rows.push(T('Поиск «', 'Search “') + q + '»' + (s.ok ? ': ' + s.data.matches + ' совпадений' : ' — ошибка'));
        done++;
      }
    }
    sentSave();
    if ((asks.length || shells.length || imgs.length) && !done) return { text: '', rows: rows, asks: asks, shells: shells, imgs: imgs }; // вопросы/команды/картинки — ждут
    if (!done) {
      // ВСЁ запрошенное уже отправлялось раньше: молчать НЕЛЬЗЯ (модель ждёт
      // ответ и встаёт навсегда) — шлём короткое напоминание.
      out.push('REMINDER: everything you just requested was ALREADY sent earlier in this chat (look above in the hidden SYSTEM messages).');
      out.push('Do NOT repeat the same requests — re-use that content. If you need a DIFFERENT slice of a big file, ask ' +
               '[<{READ RANGE: <path>:<other-from>-<other-to>}>]; for size, [<{FILE INFO: <path>}>].');
      return { text: out.join('\n\n'), rows: [] };
    }
    return { text: out.join('\n\n'), rows: rows, asks: asks, shells: shells, imgs: imgs };
  }

  // ---------------- бейджи: [<{…}>] в ответе модели превращаем в строки-статусы
  // Меняем ТОЛЬКО nodeValue текстовых узлов (React не заметит — дерево то же).
  var BADGE_RE = /\[<\{\s*(NEED FILE|NEED SEARCH|FILE INFO|READ RANGE|MEMORY SEARCH|MEMORY ADD|ASK MULTI|ASK|REVERT FILE|WEB FETCH|WEB DOWNLOAD|WEB GREP|SHELL|IMAGE FILE|IMAGE SHOW)\s*:\s*([^<>]*?)\s*\}>\]/gi;
  function badgeText(kind, arg, pending) {
    var K = kind.toUpperCase(), tail = pending ? '…' : '';
    if (K === 'NEED FILE')    return (pending ? T('Чтение: ', 'Reading: ')       : T('Прочитано: ', 'Read: '))      + base(arg) + tail;
    if (K === 'FILE INFO')    return (pending ? T('Осмотр: ', 'Inspecting: ')     : T('Размер получен: ', 'Size: ')) + base(arg) + tail;
    if (K === 'READ RANGE') {
      var m = String(arg).match(/^(.+?)\s*:\s*(\d+)\s*[-–—]\s*(\d+)$/);
      var nm = m ? base(m[1]) + ' (' + m[2] + '–' + m[3] + ')' : base(arg);
      return (pending ? T('Чтение: ', 'Reading: ') : T('Прочитано: ', 'Read: ')) + nm + tail;
    }
    if (K === 'MEMORY SEARCH') return (pending ? T('Память: ', 'Memory: ') : T('Найдено в памяти: ', 'Found in memory: ')) + String(arg).trim() + tail;
    if (K === 'ASK' || K === 'ASK MULTI') return (pending ? T('Спрашивает: ', 'Asking: ') : T('Отвечено: ', 'Answered: ')) + String(arg).trim() + tail;
    if (K === 'WEB FETCH') return (pending ? T('Загрузка: ', 'Fetching: ') : T('Загружено: ', 'Fetched: ')) + hostOf(String(arg).trim()) + tail;
    if (K === 'WEB DOWNLOAD') {
      var dp = String(arg).split('::'); var dn = dp.length > 1 ? base(dp[1].trim()) : hostOf(dp[0].trim());
      return (pending ? T('Скачивает: ', 'Downloading: ') : T('Скачано: ', 'Downloaded: ')) + dn + tail;
    }
    if (K === 'WEB GREP') {
      var gp2 = String(arg).split('::'); var gq2 = (gp2.length > 1 ? gp2[1] : gp2[0]).trim();
      return (pending ? T('Ищет в инете: ', 'Searching the web: ') : T('Найдено в инете: ', 'Found on the web: ')) + gq2 + tail;
    }
    if (K === 'IMAGE FILE') return (pending ? T('Рисует в файл: ', 'Painting to file: ') : T('Картинка сохранена: ', 'Image saved: ')) + base(String(arg).trim()) + tail;
    if (K === 'IMAGE SHOW') return (pending ? T('Рисует картинку: «', 'Painting an image: “') : T('Картинка: «', 'Image: “')) + String(arg).trim().slice(0, 60) + '»' + tail;
    if (K === 'MEMORY ADD') return (pending ? T('Запоминает: ', 'Memorizing: ') : T('Запомнил: ', 'Memorized: ')) + String(arg).split('::')[0].trim() + tail;
    if (K === 'REVERT FILE') return (pending ? T('Откатываю: ', 'Reverting: ') : T('Откачено: ', 'Reverted: ')) + base(String(arg).trim()) + tail;
    if (K === 'SHELL') return (pending ? T('Команда: ', 'Command: ') : T('Выполнено: ', 'Executed: ')) + String(arg).trim().slice(0, 90) + tail;
    return (pending ? T('Поиск: ', 'Search: ') : T('Найдено: ', 'Found: ')) + String(arg).trim() + tail; // NEED SEARCH
  }
  // убрать из видимого ответа: нераспарсенные маркеры (в т.ч. экранированные),
  // DSML-mусор вида <｜｜DSML｜｜ calls> — ничего из этого юзеру не нужно.
  function stripDanglingMarkers(block) {
    try {
      eachTextNode(block, function (n) {
        var p = n.parentElement;
        if (p && p.closest && p.closest('pre, code')) return; // примеры в коде не трогаем
        var v = n.nodeValue;
        if (!v) return;
        if (v.indexOf('<{') < 0 && v.indexOf('&lt;{') < 0 && v.indexOf('DSML') < 0 &&
            v.indexOf('｜') < 0) return;
        var w = v.replace(/&lt;/g, '<').replace(/&gt;/g, '>');
        w = w.replace(/\[<\{[\s\S]{0,900}?\}>\]/g, '');   // нормальное закрытие
        w = w.replace(/\[<\{[\s\S]{0,900}?\}\]/g, '');     // сломанное «}]»
        w = w.replace(/\[<\{[\s\S]{0,900}$/g, '');         // оборванный хвост (стрим)
        w = w.replace(/<[|｜]{1,3}\s*DSML[\s\S]{0,200}?(?:>|$)/g, ''); // <｜DSML｜ … >
        w = w.replace(/[|｜]{1,3}\s*(?:DSML|dsml|calls|invoke|parameters?)\s*[|｜]{0,3}/g, '');
        if (w !== n.nodeValue) n.nodeValue = w;
      });
    } catch (e) {}
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
  function resumeBadges(block) { try { renderBadges(block); stripDanglingMarkers(block); } catch (e) {} }
  // после того, как служебное ушло: «Чтение: x…» → «Прочитано: x»
  function finalizeBadges(block) {
    var swaps = DSX_LANG === 'ru' ? [
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
      [/Команда: ([^…\n]+)…/g,        'Выполнено: $1'],
      [/Рисует в файл: ([^…\n]+)…/g,   'Картинка сохранена: $1'],
      [/Рисует картинку: «([^…\n]+)…/g, 'Картинка: «$1']
    ] : [
      [/Reading: ([^…\n]+)…/g,              'Read: $1'],
      [/Inspecting: ([^…\n]+)…/g,           'Size: $1'],
      [/Search: ([^…\n]+)…/g,               'Found: $1'],
      [/Memory: ([^…\n]+)…/g,               'Found in memory: $1'],
      [/Asking: ([^…\n]+)…/g,               'Answered: $1'],
      [/Fetching: ([^…\n]+)…/g,             'Fetched: $1'],
      [/Downloading: ([^…\n]+)…/g,          'Downloaded: $1'],
      [/Searching the web: ([^…\n]+)…/g,    'Found on the web: $1'],
      [/Memorizing: ([^…\n]+)…/g,           'Memorized: $1'],
      [/Reverting: ([^…\n]+)…/g,            'Reverted: $1'],
      [/Command: ([^…\n]+)…/g,              'Executed: $1'],
      [/Painting to file: ([^…\n]+)…/g,       'Image saved: $1'],
      [/Painting an image: “([^…\n]+)…/g,    'Image: “$1']
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
  async function execShell(block, cmd, statusEl, defer) {
    if (statusEl) statusEl.textContent = T('Выполняет: ', 'Running: ') + cmd.slice(0, 90) + '…';
    var hrow = toolRowStart(block.closest('._4f9bf79') || block.parentElement, 'Bash', cmd, { expandable: true });
    var sok = false, detail = '';
    var r = await dsxSend('shell', { cmd: cmd });
    dlog('SHELL:', cmd.slice(0, 90), '→', (r && r.ok) ? ('exit ' + r.exitCode + (r.killed ? ' (timeout 120s)' : '')) : ('ERR ' + ((r && r.error) || 'нет связи')));
    var report, short;
    if (r && r.ok) {
      var out = String(r.stdout || '').replace(/\n$/, '');
      var err = String(r.stderr || '').replace(/\n$/, '');
      report = 'CHDIR: ' + (r.cwd || '?') + '\nEXIT CODE: ' + r.exitCode +
               (r.killed ? ' (TIMEOUT 120s — убито)' : '') +
               '\n\nSTDOUT:\n```\n' + (out || '(пусто)') + '\n```' +
               (err ? '\n\nSTDERR:\n```\n' + err + '\n```' : '');
      sok = (r.exitCode === 0) && !r.killed;
      detail = '<span class="dsx-dl-at">$ ' + esc(cmd) + '</span>' +
               esc('\n' + (out || '(no output)')) +
               (err ? '\n\n<span class="dsx-tool-err">' + esc('STDERR:\n' + err) + '</span>' : '');
      short = T('Выполнено (код ', 'Done (exit ') + r.exitCode + '): ' + cmd.slice(0, 70) +
              (out ? ' → ' + out.split('\n')[0].slice(0, 60) : '');
    } else {
      var er = (r && r.error) || T('нет связи с хостом', 'no connection to the host');
      report = 'SHELL ERROR: ' + er + '\n(Если хост не установлен/не запущен — подскажи юзеру: shell\\install.bat из репозитория, потом перезапуск Chrome. Не повторяй ту же команду впрок.)';
      short = T('Ошибка шелла: ', 'Shell error: ') + er.slice(0, 80);
      detail = '<span class="dsx-tool-err">' + esc(report) + '</span>';
    }
    hrow.finish(sok ? 'ok' : 'bad', detail);
    if (statusEl) statusEl.textContent = short;
    if (defer) return 'SYSTEM: shell result for ' + cmd + '\n' + report;
    await shellSend(cmd, report); shellBookkeep();
    finalizeBadges(block); hideActionBar(block);
    return '';
  }
  function runShellCmd(block, cmd) {
    var shKey = hashStr((block.textContent || '').slice(0, 500) + '|sh|' + cmd);
    if (actSeen('sh', shKey)) return; // уже выполняли — не второй раз
    // авто-режим: настройка «Шелл без подтверждения» или галка сессии
    if (st.shellAuto || cfg.shellAuto === true) { actMark('sh', shKey); execShell(block, cmd, null); return; }
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var key = 'dsx-sh-' + hashStr(cmd).slice(0, 24);
      if (host.querySelector('[data-key="' + key + '"]')) return;

      var card = document.createElement('div');
      card.setAttribute('data-key', key);
      card.setAttribute('data-dsx-card', '1');
      card.style.cssText = 'margin-top:10px;padding:12px 14px;border-radius:12px;max-width:560px;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))';

      var cap = document.createElement('div');
      cap.textContent = T('Модель просит выполнить команду:', 'The model asks to run a command:');
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
      run.textContent = T('Выполнить', 'Run');
      run.style.cssText = 'padding:8px 16px;border:none;cursor:pointer;border-radius:10px;' +
        'font-size:13px;font-weight:600;font-family:inherit;color:#fff;' +
        'background:var(--dsw-alias-brand-primary,#5686fe)';
      var no = document.createElement('button');
      no.textContent = T('Отклонить', 'Reject');
      no.style.cssText = 'padding:8px 14px;cursor:pointer;border-radius:10px;font-size:13px;' +
        'font-family:inherit;color:var(--dsw-alias-label-primary,#eee);' +
        'background:var(--dsw-alias-button-ghost-active-fill,rgba(255,255,255,.07));' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1))';
      var autoLab = document.createElement('label');
      autoLab.style.cssText = 'display:flex;align-items:center;gap:6px;cursor:pointer;font-size:12px;' +
        'color:var(--dsw-alias-label-tertiary,#888)';
      var autoCb = document.createElement('input');
      autoCb.type = 'checkbox'; autoCb.checked = cfg.shellAuto === true;
      autoCb.style.cssText = 'width:14px;height:14px;accent-color:#5686fe;margin:0';
      var autoTx = document.createElement('span');
      autoTx.textContent = T('авто-разрешать до конца сессии', 'auto-allow for this session');
      autoLab.appendChild(autoCb); autoLab.appendChild(autoTx);
      row.appendChild(run); row.appendChild(no); row.appendChild(autoLab);
      card.appendChild(row);

      var statusEl = document.createElement('div');
      statusEl.style.cssText = 'margin-top:8px;font-size:12.5px;color:var(--dsw-alias-label-tertiary,#888)';
      card.appendChild(statusEl);
      host.appendChild(card);

      run.onclick = function () {
        cfg.shellAuto = !!autoCb.checked; saveCfg();
        run.disabled = true; no.disabled = true;
        code.style.opacity = '.6';
        actMark('sh', shKey);
        execShell(block, cmd, statusEl, true).then(function (payload) {
          card.removeChild(code); card.removeChild(row);
          groupOrSend(block, card, payload, 'шелл').then(function (res) {
            if (res === 'sent') { finalizeBadges(block); hideActionBar(block); }
            else if (res === 'wait') statusEl.textContent = T('Выполнено — жду остальные карточки…', 'Done — waiting for the other cards…');
          });
        });
      };
      no.onclick = async function () {
        run.disabled = true; no.disabled = true;
        card.removeChild(code); card.removeChild(row);
        statusEl.textContent = T('Отклонено: ', 'Rejected: ') + cmd.slice(0, 90);
        toolRowStart(block.closest('._4f9bf79') || block.parentElement, 'Bash', cmd, { expandable: false })
          .finish('rej', null, null, 0);
        var txt = 'SYSTEM: shell command was REJECTED by the user in the confirm card. ' +
          'Do NOT retry it; continue without it, or suggest a manual alternative in text.\nCOMMAND: ' + cmd;
        var res = await groupOrSend(block, card, txt, 'шелл-отказ');
        if (res === 'sent') { finalizeBadges(block); hideActionBar(block); }
      };
      console.log('[DSX] shell-карточка: «' + cmd.slice(0, 60) + '»');
    } catch (e) { console.warn('[DSX] shell card error', e); }
  }

  // ---------------- служебные user-бабблы автопилота — юзер от них чист
  // Сообщение юзера со скрытыми правилами (маркер ВНАЧАЛЕ + его текст после) НЕ трогаем;
  // прячем только пузыри, где после ⟦/DSX⟧ ничего нет (= целиком служебные).
  function hideSvcUserMsgs() {
    try {
      document.querySelectorAll('._9663006').forEach(function (r) {
        if (r.__dsxUserSeen || r.style.display === 'none') return;
        var t = (r.textContent || '');
        if (t.indexOf(MARK) < 0) { r.__dsxUserSeen = 1; return; }
        if (t.trim().indexOf(MARK) !== 0) { r.__dsxUserSeen = 1; return; }
        var after = t.split(END).slice(1).join(END).replace(/\s+/g, '');
        if (!after) {
          r.style.display = 'none';
          r.__dsxUserSeen = 1;
          return;
        }
        r.__dsxUserSeen = 1; // юзерское сообщение + скрытые правила — остаётся видимым
        // панель реакций под сообщением юзера — убираем (место жрёт)
        var par = r.parentElement;
        if (par) {
          var bar2 = par.querySelector('.ds-flex._0a3d93b');
          if (bar2) bar2.style.display = 'none';
        }
      });
    } catch (e) {}
  }
  setInterval(function () { hideSvcUserMsgs(); }, 900);

  // ---------------- карточка-опрос (ASK / ASK MULTI): варианты → клик → ответ
  // сам уезжает модели служебным сообщением; последний вариант всегда «Свой ответ»
  function renderAskCard(block, ask) {
    try {
      var host = block.closest('._4f9bf79') || block.parentElement;
      if (!host) return;
      var key = 'dsx-ask-' + hashStr(ask.q + '|' + (ask.opts || []).join(';')).slice(0, 24);
      if (host.querySelector('[data-key="' + key + '"]')) return;
      var aKey = hashStr((block.textContent || '').slice(0, 300) + '|ask|' + ask.q);
      if (actSeen('ask', aKey)) return; // отвечали — не дублим карточку после перерисовки

      var card = document.createElement('div');
      card.setAttribute('data-key', key);
      card.setAttribute('data-dsx-card', '1');
      card.style.cssText = 'margin-top:10px;padding:12px 14px;border-radius:12px;' +
        'background:var(--dsw-alias-bg-layer-2,#1d1f24);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));max-width:520px';

      var title = document.createElement('div');
      title.textContent = ask.q;
      title.style.cssText = 'font-size:13.5px;font-weight:600;line-height:1.45;' +
        'color:var(--dsw-alias-label-primary,#eee)';
      card.appendChild(title);

      var hint = document.createElement('div');
      hint.textContent = ask.multi ? T('отметь один или несколько вариантов', 'check one or more options') : T('выбери один вариант', 'pick one option');
      hint.style.cssText = 'margin-top:2px;font-size:11.5px;color:var(--dsw-alias-label-tertiary,#888)';
      card.appendChild(hint);

      var list = document.createElement('div');
      list.style.cssText = 'margin-top:8px;display:flex;flex-direction:column;gap:6px';
      var options = (ask.opts || []).concat([T('Свой ответ…', 'Custom answer…')]);
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
      customTa.rows = 2; customTa.placeholder = T('Напиши свой вариант ответа…', 'Type your own answer…');
      customTa.style.cssText = 'display:none;width:100%;box-sizing:border-box;margin-top:8px;' +
        'padding:8px 10px;resize:vertical;border-radius:9px;font-size:13px;line-height:1.45;' +
        'font-family:inherit;background:transparent;color:var(--dsw-alias-label-primary,#f5f5f5);' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.12));outline:none';
      card.appendChild(customTa);

      var btn = document.createElement('button');
      btn.textContent = T('Отправить ответ', 'Send answer');
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
        if (!picked.length) { toast(T('Выбери вариант или напиши свой ответ', 'Pick an option or write your own')); return; }
        btn.disabled = true; btn.style.opacity = '.5'; btn.textContent = T('Отправляю…', 'Sending…');
        var answer = 'SYSTEM: the user answered your question (via DeepSeek Extended UI).\n' +
          'QUESTION: «' + ask.q + '»\n' +
          'ANSWER' + (ask.multi ? 'S (several picked)' : '') + ': ' +
          (ask.multi ? '\n- ' + picked.join('\n- ') : picked.join('; '));
        actMark('ask', aKey);
        var res = await groupOrSend(block, card, answer, 'ответ на вопрос');
        if (res === 'fail') { btn.disabled = false; btn.style.opacity = '1'; btn.textContent = T('Отправить ответ', 'Send answer'); return; }
        card.innerHTML = '';
        var doneRow = document.createElement('div');
        doneRow.textContent = (res === 'wait'
          ? T('Ответ принят (жду остальные карточки): ', 'Answer accepted (waiting for the other cards): ')
          : T('Ответ отправлен: ', 'Answer sent: ')) + picked.join(', ');
        doneRow.style.cssText = 'font-size:12.5px;color:var(--dsw-alias-label-tertiary,#888)';
        card.appendChild(doneRow);
        if (res === 'sent') { finalizeBadges(block); hideActionBar(block); }
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

  // ---------------- ТУЛ-СТРОКИ как в IDE: ▸ Bash ● executing 0.8s → ✓ 1.2s; клик — детали
  function dsxToolCss() {
    if (document.getElementById('dsx-tool-css')) return;
    var s = document.createElement('style');
    s.id = 'dsx-tool-css';
    s.textContent = '' +
      '.dsx-tool{margin:6px 0;border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
        'border-radius:10px;background:var(--dsw-alias-bg-layer-2,#1d1f24);overflow:hidden;' +
        'font-size:12.5px;font-family:var(--dsw-font-family,inherit);max-width:660px}' +
      '.dsx-tool-h{display:flex;align-items:center;gap:8px;padding:7px 10px;user-select:none}' +
      '.dsx-tool-x{cursor:pointer}' +
      '.dsx-tool-che{width:12px;flex-shrink:0;color:var(--dsw-alias-label-tertiary,#888);font-size:11px;transition:transform .15s}' +
      '.dsx-tool-open .dsx-tool-che{transform:rotate(90deg)}' +
      '.dsx-tool-nm{font-family:Consolas,Menlo,monospace;font-weight:600;font-size:12px;color:var(--dsw-alias-label-primary,#eee);white-space:nowrap}' +
      '.dsx-tool-sub{font-family:Consolas,Menlo,monospace;font-size:11.5px;overflow:hidden;text-overflow:ellipsis;' +
        'white-space:nowrap;color:var(--dsw-alias-label-tertiary,#999);flex:1;min-width:0}' +
      '.dsx-tool-stat{margin-left:auto;display:flex;align-items:center;gap:8px;flex-shrink:0;font-family:Consolas,monospace;font-size:11px}' +
      '.dsx-tool-diff{font-variant-numeric:tabular-nums;white-space:nowrap}' +
      '.dsx-tool-dur{color:var(--dsw-alias-label-tertiary,#999);font-variant-numeric:tabular-nums;min-width:44px;text-align:right}' +
      '.dsx-tool-dot{width:8px;height:8px;border-radius:50%;background:#8a9199;flex-shrink:0}' +
      '.dsx-tool-run{background:#e8b339;animation:dsxPulse 1s infinite}' +
      '.dsx-tool-std-ok{background:#3fb950}' +
      '.dsx-tool-std-bad{background:#f85149}' +
      '.dsx-tool-std-rej{background:#8a9199}' +
      '@keyframes dsxPulse{0%,100%{opacity:1}55%{opacity:.3}}' +
      '.dsx-tool-b{display:none;border-top:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.08));' +
        'padding:8px 10px;max-height:260px;overflow:auto;background:rgba(0,0,0,.18)}' +
      '.dsx-tool-open .dsx-tool-b{display:block}' +
      '.dsx-tool-pre{margin:0;font-family:Consolas,Menlo,monospace;font-size:11.5px;line-height:1.55;' +
        'white-space:pre-wrap;word-break:break-word;color:var(--dsw-alias-label-secondary,#ccc)}' +
      '.dsx-dl-add{color:#3fb950;background:rgba(63,185,80,.08);display:block}' +
      '.dsx-dl-del{color:#f85149;background:rgba(248,81,73,.08);display:block}' +
      '.dsx-dl-at{color:#8a9199;display:block}' +
      '.dsx-tool-err{color:#f85149}' +
      '.dsx-tool-x:hover{background:rgba(255,255,255,.035)}' +
      '.dsx-tool{transition:border-color .2s,background .2s}' +
      '.dsx-tool:has(.dsx-tool-run){border-color:rgba(232,179,57,.35);' +
        'background:linear-gradient(110deg,var(--dsw-alias-bg-layer-2,#1d1f24) 35%,rgba(232,179,57,.09) 50%,var(--dsw-alias-bg-layer-2,#1d1f24) 65%);' +
        'background-size:220% 100%;animation:dsxSweep 1.5s linear infinite}' +
      '@keyframes dsxSweep{0%{background-position:190% 0}100%{background-position:-20% 0}}' +
      '.dsx-tool:has(.dsx-tool-std-ok){border-color:rgba(63,185,80,.28)}' +
      '.dsx-tool:has(.dsx-tool-std-bad){border-color:rgba(248,81,73,.35)}';
    document.head.appendChild(s);
  }
  function fmtDur(ms) { return ms < 1000 ? (ms | 0) + 'ms' : (ms / 1000).toFixed(1) + 's'; }
  var OP_LABEL = { write_file: 'Write', edit_file: 'Edit', append_file: 'Append',
                   insert_lines: 'Insert', replace_lines: 'Replace', make_dir: 'Mkdir',
                   delete_path: 'Delete', copy_file: 'Copy', move_file: 'Move', revert: 'Revert' };
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }

  // старт тул-строки: name — вид (Bash/Edit…), sub — команда / путь файла
  function toolRowStart(host, name, sub, opts) {
    dsxToolCss();
    if (!host) return { el: null, finish: function () {} };
    var box = host.querySelector('.dsx-tool-box');
    if (!box) { box = document.createElement('div'); box.className = 'dsx-tool-box'; host.appendChild(box); }
    if (!box.isConnected) host.appendChild(box);
    var expandable = !!(opts && opts.expandable);
    var el = document.createElement('div');
    el.className = 'dsx-tool';
    el.innerHTML =
      '<div class="dsx-tool-h' + (expandable ? ' dsx-tool-x' : '') + '">' +
        '<span class="dsx-tool-che">' + (expandable ? '\u25B8' : '') + '</span>' +
        '<span class="dsx-tool-nm">' + esc(name) + '</span>' +
        '<span class="dsx-tool-sub">' + esc(sub || '') + '</span>' +
        '<span class="dsx-tool-stat">' +
          '<span class="dsx-tool-diff" data-diff></span>' +
          '<span class="dsx-tool-dot dsx-tool-run"></span>' +
          '<span class="dsx-tool-dur" data-dur>0.0s</span>' +
        '</span>' +
      '</div>' +
      (expandable ? '<div class="dsx-tool-b"><pre class="dsx-tool-pre"></pre></div>' : '');
    var t0 = Date.now(), durEl = el.querySelector('[data-dur]');
    el.__dsxTimer = setInterval(function () {
      if (!el.isConnected) { clearInterval(el.__dsxTimer); return; }
      durEl.textContent = fmtDur(Date.now() - t0);
    }, 200);
    if (expandable) {
      el.querySelector('.dsx-tool-h').addEventListener('click', function () { el.classList.toggle('dsx-tool-open'); });
    }
    box.appendChild(el);
    return {
      el: el,
      finish: function (status, detailHtml, diffTxt, fixedMs) {
        clearInterval(el.__dsxTimer);
        durEl.textContent = fmtDur(typeof fixedMs === 'number' ? fixedMs : (Date.now() - t0));
        var dot = el.querySelector('.dsx-tool-dot');
        dot.classList.remove('dsx-tool-run');
        dot.classList.add(status === 'ok' ? 'dsx-tool-std-ok' : status === 'rej' ? 'dsx-tool-std-rej' : 'dsx-tool-std-bad');
        if (diffTxt) {
          var dd = el.querySelector('[data-diff]');
          dd.innerHTML = esc(diffTxt).replace(/\+\d+/g, '<span style="color:#3fb950">$&</span>')
                                     .replace(/-\d+/g, '<span style="color:#f85149">$&</span>');
        }
        if (detailHtml) {
          var b = el.querySelector('.dsx-tool-b pre');
          if (b) b.innerHTML = detailHtml;
        }
      }
    };
  }

  // вставить элемент СРАЗУ после текстового фрагмента (позиция бейджа в ответе)
  function insertAfterTextNode(block, needle, el) {
    var done = false;
    try {
      eachTextNode(block, function (n) {
        if (done) return;
        var i = n.nodeValue.indexOf(needle);
        if (i < 0) return;
        var after = n.splitText(i + needle.length);
        n.parentNode.insertBefore(el, after);
        done = true;
      });
    } catch (e) {}
    return done;
  }

  // ---------------- генерация изображений (pollinations, ключ юзера) ----------
  function dsxImgCss() {
    if (document.getElementById('dsx-img-css')) return;
    var s = document.createElement('style');
    s.id = 'dsx-img-css';
    s.textContent = '' +
      '.dsx-imgbox{display:block;margin:8px 0;max-width:min(560px,100%);border-radius:14px;overflow:hidden;' +
        'border:1px solid var(--dsw-alias-border-l2,rgba(255,255,255,.1));background:var(--dsw-alias-bg-layer-2,#1d1f24)}' +
      '.dsx-img-skel{height:340px;position:relative;overflow:hidden;display:flex;align-items:center;justify-content:center;' +
        'color:var(--dsw-alias-label-tertiary,#999);font-size:12.5px}' +
      '.dsx-img-skel:before{content:"";position:absolute;inset:0;' +
        'background:linear-gradient(110deg,transparent 30%,rgba(255,255,255,.07) 50%,transparent 70%);' +
        'background-size:220% 100%;animation:dsxSweep 1.4s linear infinite}' +
      '.dsx-imgbox img{display:block;width:100%;height:auto;opacity:0;transition:opacity .35s}' +
      '.dsx-imgbox img.dsx-img-ready{opacity:1}';
    document.head.appendChild(s);
  }
  function dsxBlobToB64(blob) {
    return new Promise(function (res, rej) {
      var fr = new FileReader();
      fr.onload = function () { res(String(fr.result).split(',')[1] || ''); };
      fr.onerror = rej;
      fr.readAsDataURL(blob);
    });
  }
  async function dsxGenImage(prompt, model, key) {
    var url = 'https://gen.pollinations.ai/image/' + encodeURIComponent(prompt) +
      '?model=' + encodeURIComponent(model) + '&width=1024&height=1024&nologo=true';
    dlog('IMG gen:', model, '|', String(prompt).slice(0, 70));
    try {
      var hdrs = {}; if (key) hdrs['Authorization'] = 'Bearer ' + key;
      var r = await fetch(url, { headers: hdrs });
      if (!r.ok) throw new Error('HTTP ' + r.status + ': ' + (await r.text()).slice(0, 160));
      var blob = await r.blob();
      dlog('IMG браузер OK:', blob.size, 'bytes');
      return { ok: true, blobUrl: URL.createObjectURL(blob), b64: await dsxBlobToB64(blob), size: blob.size };
    } catch (e) {
      dlog('IMG браузер FAIL:', String(e && e.message || e).slice(0, 120), '— пробую python-мост…');
      var r2 = await dsxSend('imgfetch', { prompt: prompt, model: model, key: key });
      dlog('IMG мост:', (r2 && r2.ok) ? ('OK ' + (r2.size || 0) + ' bytes') : ('ERR ' + ((r2 && r2.error) || '?')));
      if (r2 && r2.ok && r2.b64) return { ok: true, b64: r2.b64, type: r2.type, size: r2.size };
      return { ok: false, error: (r2 && r2.error) || String(e && e.message || e) };
    }
  }
  async function runImageJob(block, im) {
    var host = block.closest('._4f9bf79') || block.parentElement;
    var skel = null;
    if (im.kindg === 'show') {
      dsxImgCss();
      skel = document.createElement('div');
      skel.className = 'dsx-imgbox';
      skel.innerHTML = '<div class="dsx-img-skel">' + esc(T('Генерирую картинку…', 'Generating image…')) + '</div>';
      // точно В тексте ответа: сразу за бейджем маркера; запасной — перед тул-строками
      var needle = T('Рисует картинку: «', 'Painting an image: “');
      var inlineOk = insertAfterTextNode(block, needle, skel);
      if (!inlineOk && host) {
        dlog('IMG скелет: бейдж не найден в тексте — встаю перед тул-строками');
        host.insertBefore(skel, host.querySelector('.dsx-tool-box') || null);
      }
    }
    var hr = toolRowStart(host, 'Image', im.prompt.slice(0, 80), { expandable: false });
    var res = await dsxGenImage(im.prompt, im.model, cfg.imgKey);
    if (!res.ok) {
      dlog('IMG FAIL:', res.error);
      hr.finish('bad', null, null, 0);
      if (skel) {
        var sk = skel.querySelector('.dsx-img-skel');
        sk.classList.add('dsx-tool-err');
        sk.textContent = T('Не удалось сгенерировать', 'Generation failed');
      }
      return 'ERROR: image generation failed — ' + (res.error || 'unknown');
    }
    var statKB = res.size ? Math.round(res.size / 1024) + ' KB' : '';
    if (im.kindg === 'file') {
      var w = await fsCall('write_b64', { path: im.path, b64: res.b64 });
      dlog('IMG файл:', im.path, '→', (w && w.ok) ? ((res.size || 0) + ' bytes записаны') : ('ERR ' + (w && w.error)));
      hr.finish(w && w.ok ? 'ok' : 'bad', null, statKB, 0);
      if (w && w.ok) refreshPrompt();
      return w && w.ok
        ? 'IMAGE saved into the project: ' + im.path + ' (' + (res.size || 0) + ' bytes). NOT shown in chat, as requested.'
        : 'ERROR saving image to ' + im.path + ': ' + (w && w.error);
    }
    var imEl = new Image();
    imEl.onload = function () { imEl.classList.add('dsx-img-ready'); };
    imEl.src = res.blobUrl || ('data:' + (res.type || 'image/jpeg') + ';base64,' + res.b64);
    imEl.alt = im.prompt;
    skel.innerHTML = ''; skel.appendChild(imEl);
    dlog('IMG показана инлайн:', String(im.prompt).slice(0, 60), '|', statKB);
    hr.finish('ok', null, statKB, 0);
    return 'IMAGE generated and shown INLINE in your message (the user sees it). Continue the reply naturally around it.';
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
        var safe = String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        safe = safe
          .replace(/^([✓✗]) /, function (m, ch) {
            return '<span style="color:' + (ch === '✓' ? '#3fb950' : '#f85149') + ';font-weight:600">' + ch + '</span> ';
          })
          .replace(/\[(\+\d+)(?: -(\d+))?\]/g, function (m, a2, d2) {
            var s2 = ' <span style="color:#3fb950;font-weight:600">' + a2 + '</span>';
            if (d2) s2 += ' <span style="color:#f85149;font-weight:600">-' + d2 + '</span>';
            return s2;
          });
        p.innerHTML = safe;
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
    write_file:   [T('Пишет: ', 'Writing: '), T('Записано: ', 'Written: ')],
    edit_file:    [T('Правит: ', 'Editing: '), T('Изменено: ', 'Edited: ')],
    append_file:  [T('Дописывает: ', 'Appending: '), T('Дописано: ', 'Appended: ')],
    insert_lines: [T('Вставляет в ', 'Inserting into '), T('Вставлено в ', 'Inserted into ')],
    replace_lines:[T('Заменяет в ', 'Replacing in '), T('Заменено в ', 'Replaced in ')],
    make_dir:     [T('Создаёт папку ', 'Creating folder '), T('Папка создана: ', 'Folder created: ')],
    delete_path:  [T('Удаляет ', 'Deleting '), T('Удалено: ', 'Deleted: ')],
    copy_file:    [T('Копирует ', 'Copying '), T('Скопировано: ', 'Copied: ')],
    move_file:    [T('Перемещает ', 'Moving '), T('Перемещено: ', 'Moved: ')]
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
      try {
        var fig = pre.closest('figure') || pre; // шапка с «deepseekide-ops» и «Копировать»
        if (fig.style.display !== 'none') fig.style.display = 'none';
      } catch (e) {}
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
        var key = op + '|' + pth + (to ? '|' + to : '');
        if (st.liveSeen[key]) continue;
        st.liveSeen[key] = 1;
        // живая ТУЛ-СТРОКА вместо серого текста: жёлтая пульсация + тикающее время
        var hr = toolRowStart(host, OP_LABEL[op] || op, base(pth) + (to ? ' → ' + base(to) : ''), { expandable: false });
        if (hr.el) {
          hr.el.classList.add('dsx-live-tool');
          hr.el.__dsxFin = function (ok2) { hr.finish(ok2 ? 'ok' : 'bad'); };
        }
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
        var t0r = +row.getAttribute('data-t0') || 0;
        var dur = t0r ? ' · ' + fmtDur(Date.now() - t0r) : '';
        row.textContent = (ok ? fin : (t.slice(0, -1) + T(' (ОШИБКА)', ' (ERROR)'))) + dur;
      });
      host.querySelectorAll('.dsx-live-tool').forEach(function (el) {
        if (el.__dsxDone) return; el.__dsxDone = 1;
        if (el.__dsxFin) el.__dsxFin(ok);
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
    hideSvcUserMsgs();      // служебные user-пузыри — тоже
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

        dlog('settle: новый ответ,', text.length, 'зн; tail=', tail);
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
        if (outp.ops.length && actSeen('ops', answHash)) {
          // скролл вверх/перерисовка вирт-листа: эти правки УЖЕ применены — пропуск
          rows.push(T('Правки этого ответа уже применены — пропуск', 'Edits of this reply were applied already — skipped'));
          finalizeLiveOps(block, true);
        } else if (outp.ops.length && (answHash !== st.opsMeta.h || vKey !== st.opsMeta.k)) {
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
          dlog('OPS batch:', r.ok ? ((r.data.done || 0) + '/' + (r.data.total || 0) + ' применено') : ('ERR ' + (r.error || '?')), '|', (outp.ops.length || 0), 'оп.');
          if (r.ok) {
            var sum = r.data.done === r.data.total
              ? T('Применено: ', 'Applied: ') + r.data.done + ' / ' + r.data.total + T(' операций', ' ops')
              : T('Применено ', 'Applied ') + r.data.done + ' / ' + r.data.total + T(' — есть ошибки', ' — with errors');
            toast(sum);
            rows.push(sum);
            var hostBox = block.closest('._4f9bf79') || block.parentElement;
            var items = (r.data && r.data.items) || [];
            if (items.length) {
              if (hostBox) hostBox.querySelectorAll('.dsx-live-tool').forEach(function (n) { n.remove(); });
              var OPLBL = OP_LABEL;
              items.forEach(function (it) {
                var dh = '';
                if (!it.ok) {
                  dh = '<span class="dsx-tool-err">' + esc(it.err || 'error') + '</span>';
                } else if (it.detail && it.detail.length) {
                  it.detail.forEach(function (d) {
                    var cls = d.t === '+' ? 'dsx-dl-add' : d.t === '-' ? 'dsx-dl-del' : 'dsx-dl-at';
                    dh += '<span class="' + cls + '">' + esc((d.t === '@' ? '' : d.t + ' ') + d.s) + '</span>';
                  });
                }
                var hr2 = toolRowStart(hostBox, OPLBL[it.name] || it.name, it.msg || '', { expandable: !!dh });
                hr2.finish(it.ok ? 'ok' : 'bad', dh || null, it.stat ? it.stat : '', it.ms || 0);
              });
            } else { // старый MAIN без items — плоские строки
              String(r.data.report || '').split('\n').forEach(function (ln) {
                if (/^[✓✗] /.test(ln)) rows.push(ln);
              });
            }
            finalizeLiveOps(block, true);
            actMark('ops', answHash);
            note = 'SYSTEM: ops applied automatically by DeepSeek Extended:\n\n```\n' +
                   r.data.report + '\n```\n';
            refreshPrompt(); // дерево изменилось
          } else {
            toast(T('Ошибка применения: ', 'Apply error: ') + (r.error || '?'));
            rows.push(T('Ошибка применения правок', 'Failed to apply edits'));
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
              row.setAttribute('data-t0', String(Date.now()));
              row.textContent = pending;
              row.style.cssText = LIVE_ROW_CSS;
              boxL.appendChild(row);
              return function (fin) {
                var t0r = +row.getAttribute('data-t0') || Date.now();
                row.textContent = fin + ' · ' + fmtDur(Date.now() - t0r);
              };
            } catch (e) { return function () {}; }
          });
          dlog('svc: ответ-сервис', (svc.text || '').length, 'зн; asks=' + ((svc.asks || []).length), 'shells=' + ((svc.shells || []).length), 'imgs=' + ((svc.imgs || []).length));
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
          if (svc.imgs && svc.imgs.length) {
            svc.imgs.forEach(function (im) {
              runImageJob(block, im).then(function (report) {
                return injectStealth(report, 'картинка');
              }).then(function (ok) {
                if (ok) {
                  st.autoCount++;
                  st.waitingAnswer = true; sessionStorage.setItem('dsx:wait', '1');
                  sessionStorage.setItem('dsx:auto', String(st.autoCount));
                }
              });
            });
          }
        }

        if (note) {
          var __dsxLim = (cfg.autolimit === 0) ? Infinity : (cfg.autolimit > 0 ? cfg.autolimit : 12);
          if (st.autoCount >= __dsxLim) {
            toast(T('Потолок авто-сообщений (', 'Auto-message cap reached (') + __dsxLim + T(') — ваш ход; 0 в настройках = без лимита', ') — your turn; 0 in settings = unlimited'));
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
  // (пилюля и шестерёнка у композера удалены: настройки — вкладка «Агент» в Settings сайта)

  // ---------------- сторож URL (новая беседа = новый прайм)
  setInterval(function () {
    if (location.href !== st.lastUrl) {
      st.lastUrl = location.href;
      st.primedOnce = false; st.lastAnswer = ''; st.lastBlock = null; st.opsMeta = { h: '', k: '' };
      st.sentFiles = {}; st.sentSearches = {}; st.sentInfos = {}; st.sentRanges = {}; st.sentWeb = {}; st.sentDl = {}; st.sentGrep = {}; sentLoad(); // дедуп новой беседы
      st.liveSeen = {};
      actReset(); // новый чат — выполненное снова доступно
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
          /* тихо: Continue сам жмём */
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
  console.log('[DSX] DeepSeek Extended v34 стелс: загружено. pilot=' + cfg.pilot);
  refreshPrompt();
  setTimeout(refreshPrompt, 1200);
  setInterval(function () {
    if (!st.bigPrompt || !st.folderReady) refreshPrompt();
  }, 6000);
})();
"""


# ======================================================================
# Самотest без GUI: FS-пайплайн + shell (интернет проверяется вживую)
# ======================================================================
def selftest():
    import tempfile
    root = tempfile.mkdtemp(prefix='dsx_selftest_')
    global project_root
    _orig = project_root
    project_root = lambda: root  # noqa: E731 — временно подменяем корень
    try:
        fs_write({'path': 'a/hello.txt', 'text': 'привет\nвторая строка\n'})
        assert 'hello.txt' in fs_tree({})['text']
        assert 'привет' in fs_read({'path': 'a/hello.txt'})['text']
        r = fs_search({'query': 'вторая'})
        assert '(совпадений нет)' not in r['text']
        fs_mkdir({'path': 'b/sub'})
        fs_write_b64({'path': 'b/bin.dat', 'b64': base64.b64encode(b'\x00\x01\x02').decode()})
        fs_stat({'path': 'b/bin.dat'})
        fs_remove({'path': 'b', 'recursive': True})
        sh = shell_run({'cmd': 'echo selftest-ok'})
        assert sh['ok'] and 'selftest-ok' in sh['stdout'], sh
        print('SELFTEST OK (fs + shell прошли; webfetch проверяется вживую в приложении)')
        return 0
    finally:
        project_root = _orig


# ======================================================================
# Qt: окно + polling-мост
# ======================================================================
def run_app():
    from PyQt6.QtCore import QUrl, Qt, QTimer, QObject, pyqtSignal
    from PyQt6.QtGui import QFont, QMouseEvent, QRegion
    from PyQt6.QtWebEngineCore import QWebEngineProfile, QWebEngineSettings, QWebEnginePage
    from PyQt6.QtWebEngineWidgets import QWebEngineView
    from PyQt6.QtWidgets import QApplication, QMainWindow, QPushButton, QHBoxLayout, QVBoxLayout, QWidget, QLabel, QFileDialog

    class Bridge(QObject):
        """Никаких qwebchannel: outbox-поллер сливает вызовы JS -> Python;
        ответы уезжают обратно page.runJavaScript(dsxReplyFromPython(...))."""
        pushResult = pyqtSignal(str)

        def __init__(self, page, win):
            super().__init__()
            self.page = page
            self.win = win
            self.pushResult.connect(self._emit_to_js)

        def _emit_to_js(self, blob):
            self.page.runJavaScript('(window.dsxReplyFromPython||function(){})(%s);' % blob)

        def handle_item(self, item):
            t = item.get('t'); kind = item.get('kind')
            payload = item.get('payload') or {}
            try:
                if kind == 'fs':
                    br = str(payload.get('op', '')) + ' ' + str(payload.get('path') or payload.get('from') or payload.get('url') or '')[:110]
                elif kind == 'shell':
                    br = '$ ' + str(payload.get('cmd', ''))[:110]
                elif kind == 'webfetch':
                    br = str(payload.get('url', ''))[:110]
                elif kind == 'imgfetch':
                    br = '%s | %s' % (payload.get('model', '?'), str(payload.get('prompt', ''))[:70])
                else:
                    br = str(payload)[:110]
                dlog('JS→PY:', kind, br.strip())
            except Exception:
                pass
            # выбор папки — GUI-поток, модальный диалог, отвечаем сразу
            if kind == 'fs' and payload.get('op') == 'pick':
                path = QFileDialog.getExistingDirectory(self.win, 'Папка проекта') or ''
                if path:
                    cfg = load_cfg(); cfg['folder'] = path; save_cfg(cfg)
                    name = os.path.basename(os.path.normpath(path))
                    self.win.update_project_chip(name)
                    out = {'ok': True, 'path': path, 'name': name}
                else:
                    out = {'ok': False, 'error': 'папка не выбрана'}
                self.pushResult.emit(json.dumps({'t': t, 'out': out}, ensure_ascii=False))
                return

            def work():
                try:
                    if kind == 'fs':
                        op = payload.get('op')
                        fn = FS_OPS.get(op)
                        out = fn(payload) if fn else {'ok': False, 'error': 'unknown fs op: %s' % op}
                    elif kind == 'shell':
                        out = shell_run(payload)
                    elif kind == 'webfetch':
                        out = web_fetch(payload)
                    elif kind == 'imgfetch':
                        out = img_fetch(payload)
                    else:
                        out = {'ok': False, 'error': 'unknown kind: %s' % kind}
                except Exception as e:
                    out = {'ok': False, 'error': str(e)}
                try:
                    dlog('PY→JS:', kind, brief_out(out))
                except Exception:
                    pass
                self.pushResult.emit(json.dumps({'t': t, 'out': out}, ensure_ascii=False))
            threading.Thread(target=work, daemon=True).start()

        def poll(self):
            self.page.runJavaScript(
                'window.__dsxOutbox ? JSON.stringify(window.__dsxOutbox.splice(0, window.__dsxOutbox.length)) : "[]"',
                self._got_items)

        def _got_items(self, text):
            if not text or text == '[]':
                return
            try:
                items = json.loads(text)
            except Exception:
                return
            for item in items or []:
                self.handle_item(item)

    class QuietPage(QWebEnginePage):
        def javaScriptConsoleMessage(self, level, msg, line, src_):
            s = str(msg)
            if 'WebGPU' in s or 'WebAssembly' in s or 'MIME type' in s:
                return  # шум движка самого сайта — в консоль не тащим
            try:
                lvl = getattr(level, 'name', str(level))
                srcs = str(src_)
                if len(srcs) > 46:
                    srcs = '…' + srcs[-45:]
                print('[DSX-JS %s %s:%d] %s' % (lvl, srcs, line, s), flush=True)
            except Exception:
                pass

    class DeepSeekApp(QMainWindow):
        def __init__(self):
            super().__init__()
            self.setWindowTitle("DeepSeek IDE")
            self.setGeometry(0, 0, 1400, 700)
            self.setMinimumSize(1000, 500)
            self.setWindowFlags(Qt.WindowType.FramelessWindowHint)
            # БЕЗ setMask: 1-битная маска давала пиксельные «зубцы» на закруглениях.
            # Гладкие углы окна рисует сам DWM (Windows 11).
            self.drag_position = None
            self.normal_geometry = None

            self.setStyleSheet("""
                QMainWindow { background: #0d0d0d; }
                #overlay { background: transparent; border-radius: 10px; }
                /* фон пилюли = фон сайта под ней */
                #projChip { color: #9aa4b2; font-family: "Segoe UI"; font-size: 11px;
                            background: #262b34; border-radius: 8px; padding: 3px 10px; }
                QPushButton { background: transparent; border: none; border-radius: 7px; color: #d5d9e0;
                              min-width: 30px; max-width: 30px; min-height: 24px; max-height: 24px; }
                QPushButton:hover { background: rgba(255,255,255,0.12); }
                QPushButton#closeBtn:hover { background: #e81123; color: #fff; }
                QWebEngineView { background: #0d0d0d; }
            """)

            self.profile = QWebEngineProfile("DeepSeekStorage", self)
            self.profile.setPersistentStoragePath(os.path.join(app_data_dir(), 'webprofile'))
            self.profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies)
            self.profile.setHttpUserAgent(UA)

            self.page = QuietPage(self.profile, self)
            settings = self.page.settings()
            settings.setAttribute(QWebEngineSettings.WebAttribute.JavascriptEnabled, True)
            settings.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)

            self.view = QWebEngineView()
            self.view.setPage(self.page)
            self.page.loadFinished.connect(self.inject_agent)
            self.view.load(QUrl(START_URL))

            self.bridge = Bridge(self.page, self)
            self.poll_timer = QTimer(self)
            self.poll_timer.setInterval(POLL_MS)
            self.poll_timer.timeout.connect(self.bridge.poll)
            self.poll_timer.start()

            icon_font = QFont("Segoe MDL2 Assets", 9)

            self.proj_chip = QLabel("папка не выбрана")
            self.proj_chip.setObjectName("projChip")
            _init_folder = load_cfg().get('folder')
            if _init_folder:
                self.update_project_chip(os.path.basename(os.path.normpath(_init_folder)))

            overlay = QWidget(self.view)          # пилюля ПОВЕРХ страницы (не в HTML)
            overlay.setObjectName("overlay")
            ov = QHBoxLayout(overlay)
            ov.setContentsMargins(10, 6, 8, 6)
            ov.setSpacing(6)
            ov.addWidget(self.proj_chip)

            def mkbtn(glyph, tip, cb, close_=False):
                btn = QPushButton(glyph)
                btn.setFont(icon_font)
                if tip:
                    btn.setToolTip(tip)
                if close_:
                    btn.setObjectName("closeBtn")
                btn.clicked.connect(cb)
                return btn

            ov.addWidget(mkbtn("\uE72C", "Обновить страницу", lambda: self.view.reload()))
            ov.addWidget(mkbtn("\uE921", "Свернуть", self.showMinimized))
            ov.addWidget(mkbtn("\uE922", "Во весь экран / обратно", self.toggle_fullscreen))
            ov.addWidget(mkbtn("\uE8BB", "Закрыть", self.close, close_=True))
            overlay.adjustSize()
            self.overlay = overlay

            main_layout = QVBoxLayout()
            main_layout.setContentsMargins(0, 0, 0, 0)
            main_layout.setSpacing(0)
            main_layout.addWidget(self.view, 1)

            container = QWidget()
            container.setLayout(main_layout)

            central = QWidget()
            central_layout = QHBoxLayout(central)
            central_layout.setContentsMargins(0, 0, 0, 0)
            central_layout.addWidget(container)
            self.setCentralWidget(central)

            self.center_window()
            self.resizeEvent(None)

        def inject_agent(self):
            self.page.runJavaScript(AGENT_JS)
            # диагностика: смотрим, что движок видит сайту (в консоль приложения)
            self.page.runJavaScript('navigator.userAgent', lambda ua: print('[DSX] UA -> ' + str(ua)))

        def update_project_chip(self, name):
            self.proj_chip.setText('')

        def center_window(self):
            screen = QApplication.primaryScreen().availableGeometry()
            window_rect = self.frameGeometry()
            window_rect.moveCenter(screen.center())
            self.move(window_rect.topLeft())

        def resizeEvent(self, event):
            if not self.isFullScreen():
                self.normal_geometry = self.geometry()  # бэкап размера на лету
            self._reposition_overlay()
            if event:
                super().resizeEvent(event)

        def _reposition_overlay(self):
            # пилюля — строго правый верхний угол поверх страницы
            self.overlay.adjustSize()
            self.overlay.move(self.view.width() - self.overlay.width() - 14, 14)
            self.overlay.raise_()

        def showEvent(self, event):
            super().showEvent(event)
            QTimer.singleShot(0, self._reposition_overlay)
            QTimer.singleShot(300, self._reposition_overlay)

        def moveEvent(self, event):
            if not self.isFullScreen():
                self.normal_geometry = self.geometry()
            super().moveEvent(event)

        def toggle_fullscreen(self):
            if self.isFullScreen():
                self.showNormal()
                if self.normal_geometry:
                    self.setGeometry(self.normal_geometry)
            else:
                self.normal_geometry = self.geometry()
                self.showFullScreen()
            self.resizeEvent(None)  # переставить оверлей

        def keyPressEvent(self, event):
            if event.key() == Qt.Key.Key_Escape and self.isFullScreen():
                self.toggle_fullscreen()
            super().keyPressEvent(event)

        def mousePressEvent(self, event: QMouseEvent):
            if event.button() == Qt.MouseButton.LeftButton:
                clicked = self.childAt(event.position().toPoint())
                if clicked is self.overlay or (clicked and clicked.objectName() == "projChip"):
                    # окно тащим за пилюлю-оверлей (кнопки сами проглатывают клик)
                    self.drag_position = event.globalPosition().toPoint() - self.frameGeometry().topLeft()
            super().mousePressEvent(event)

        def mouseMoveEvent(self, event: QMouseEvent):
            if event.buttons() == Qt.MouseButton.LeftButton and self.drag_position is not None:
                self.move(event.globalPosition().toPoint() - self.drag_position)
            super().mouseMoveEvent(event)

        def mouseReleaseEvent(self, event: QMouseEvent):
            self.drag_position = None
            super().mouseReleaseEvent(event)

        def closeEvent(self, event):
            self.view.deleteLater()
            self.page.deleteLater()
            self.profile.deleteLater()
            super().closeEvent(event)

    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    print('[DSX] DeepSeekIDE started (UA: Chrome 140). Если Google отказывается логинить — входи по почте/телефону на той же странице.')
    window = DeepSeekApp()
    window.show()
    if os.name == 'nt':
        try:  # закругления frameless-окна через DWM (33 = corner preference, 2 = ROUND)
            import ctypes
            ctypes.windll.dwmapi.DwmSetWindowAttribute(int(window.winId()), 33, ctypes.byref(ctypes.c_int(2)), 4)
        except Exception as e:
            print('[DSX] DWM corners:', e)
    sys.exit(app.exec())


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    run_app()
