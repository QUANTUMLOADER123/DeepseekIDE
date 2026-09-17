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
  var BRACKET_RE = /\[<\{\s*(NEED FILE|NEED SEARCH|FILE INFO|READ RANGE)\s*:\s*([^<>]*?)\s*\}>\]/gi;
  function findBracketRequests(answer) {
    var out = [], inFence = false;
    var lines = String(answer).split('\n');
    for (var li = 0; li < lines.length && out.length < 12; ++li) {
      var line = lines[li];
      if (line.slice(0, 3) === '```') { inFence = !inFence; continue; }
      if (inFence) continue;
      var m; BRACKET_RE.lastIndex = 0;
      while ((m = BRACKET_RE.exec(line)) !== null && out.length < 12) {
        var kind = m[1].toUpperCase(), key;
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
        } else { // NEED SEARCH
          val = val.slice(0, 200);
          if (!val) continue;
        }
        key = kind + '|' + val.toLowerCase();
        var dup = false;
        for (var k = 0; k < out.length; ++k) if (out[k].key === key) { dup = true; break; }
        if (!dup) out.push({ kind: kind, arg: val, key: key, bracket: true });
      }
    }
    return out;
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
"(There is NO terminal in this edition — the browser sandbox cannot run commands. Deliver build/run instructions in text instead.)",
"",
"# GETTING CONTEXT (self-service, automatic — never ask the human for file contents)",
"Missing information? Emit requests INSIDE the special bracket marker, ONE per line, OUTSIDE ops blocks and code fences. The extension answers them automatically within seconds and shows the user an inline status chip; the answer arrives as a hidden SYSTEM message and your flow continues automatically.",
"Syntax (copy EXACTLY, including the brackets):",
"[<{FILE INFO: <project-relative path>}>]          -> file size in bytes + total line count.",
"[<{READ RANGE: <project-relative path>:<from>-<to>}>] -> numbered lines from..to (1-based, inclusive, up to 1200 lines at once).",
"[<{NEED FILE: <project-relative path>}>]         -> WHOLE file with line numbers (hard cap ~4000 lines / 120k chars; the rest is TRUNCATED).",
"[<{NEED SEARCH: <substring>}>]                   -> matches across ALL text files of the project in the form file:line: text.",
"Example:",
"[<{FILE INFO: src/main.cpp}>]",
"[<{READ RANGE: src/main.cpp:1-300}>]",
"[<{NEED SEARCH: handleLogin}>]",
"STRICT RULES:",
"- If unsure about a file's size, ask FILE INFO FIRST. For files over ~600 lines ALWAYS read them chunk-by-chunk with READ RANGE (e.g. :1-400, then :401-800) instead of NEED FILE — full dumps get truncated and waste context.",
"- NEED SEARCH scans the ENTIRE project, not only files you have already received. Use it to locate symbols before asking for files.",
"- Batch all requests you need into ONE message, then wait — do not narrate them.",
"- NEVER repeat a request that was already answered in this chat: re-use what you already received. Repeating wastes a whole round-trip and is treated as an error.",
"# PROJECT STRUCTURE",
tree || '(структура недоступна — спросите через NEED SEARCH/NEED FILE)',
"",
"# WORK STYLE",
"- ACT, DON'T ASK. Choose sensible defaults; mention them instead of asking.",
"- Stay strictly inside the project root above. Never aim outside it.",
"- After the ops blocks add a short human summary (plain text, few lines).",
"- Prefer one well-planned change over three sloppy ones."
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
    sanitizeRel: sanitizeRel,
    insertLines: insertLines,
    replaceLines: replaceLines,
    buildPrompt: buildPrompt,
    looksLikePath: looksLikePath
  };
});
