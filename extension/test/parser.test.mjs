import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const Ops = require('../opp.js');

function fresh() { return { ops: [], errors: [] }; }

test('ops-блок распознаётся и валидируется', () => {
  const out = fresh();
  Ops.extractOps('текст\n```deepseekide-ops\n[{"name":"write_file","args":{"path":"a.py","content":"x = 1"}}]\n```\nещё', out);
  assert.equal(out.ops.length, 1);
  assert.equal(out.ops[0].name, 'write_file');
  assert.equal(out.ops[0].args.path, 'a.py');
  assert.equal(out.errors.length, 0);
});

test('JSON вырезается из мусора виджета («Копировать» + проза)', () => {
  const out = fresh();
  Ops.parseOpsBody('Копировать\nСкачать\n[{"name":"make_dir","args":{"path":"site"}}]\n\nГотово! Всё сделал.', out);
  assert.equal(out.ops.length, 1);
  assert.equal(out.ops[0].name, 'make_dir');
});

test('run_command НЕ входит в белый список расширения', () => {
  const out = fresh();
  Ops.parseOpsBody('[{"name":"run_command","args":{"command":"ls"}}]', out);
  assert.equal(out.ops.length, 0);
  assert.equal(out.errors.length, 1);
});

test('NEED FILE: только в начале строки, мусорные пути отбрасываются', () => {
  assert.deepEqual(Ops.findFileRequests('- ❌ если нужен файл, я пишу «НУЖЕН ФАЙЛ: <путь>», и его присылают'), []);
  assert.deepEqual(Ops.findFileRequests('НУЖЕН ФАЙЛ: &lt;путь&gt;, и ты шлёшь его сам'), []);
  assert.deepEqual(Ops.findFileRequests('- NEED FILE: site/main.py\nГотово'), ['site/main.py']);
  assert.deepEqual(Ops.findFileRequests('```\nНУЖЕН ФАЙЛ: x.h\n```'), []);
});

test('NEED SEARCH: бэктики и дедуп', () => {
  assert.deepEqual(
    Ops.findSearchRequests('НУЖЕН ПОИСК: `replace_lines`\nещё\nNEED SEARCH: replace_lines'),
    ['replace_lines']);
});

test('математика строк: insert/replace с 1-based границами', () => {
  assert.equal(Ops.insertLines('a\nb\nc', 2, 'X'), 'a\nX\nb\nc');
  assert.equal(Ops.insertLines('a\nb', 3, 'tail'), 'a\nb\ntail');
  assert.equal(Ops.insertLines('a', 5, 'x'), null);
  assert.equal(Ops.replaceLines('a\nb\nc', 2, 3, 'Z'), 'a\nZ');
  assert.equal(Ops.replaceLines('a\nb\nc', 2, 2, 'X\nY'), 'a\nX\nY\nc');
  assert.equal(Ops.replaceLines('a\nb', 2, 9, 'x'), null);
});

test('sanitizeRel: отсекает выходы и абсолюты', () => {
  assert.equal(Ops.sanitizeRel('../evil'), null);
  assert.equal(Ops.sanitizeRel('a/../../b'), null);
  assert.equal(Ops.sanitizeRel('.git/hooks/x'), null);
  assert.equal(Ops.sanitizeRel('docs//a.md'), 'docs/a.md');
  assert.equal(Ops.sanitizeRel('/root/main.py'), 'root/main.py');
});

test('промпт Extended: есть toolbox и NEED-протоколы и НЕТ run_command', () => {
  const p = Ops.buildPrompt('(дерево)');
  assert.match(p, /write_file/);
  assert.match(p, /NEED FILE:/);
  assert.match(p, /NEED SEARCH:/);
  assert.ok(p.indexOf('no terminal') < 0 || p.indexOf('NO terminal') >= 0);
  // в тексте описан запрет терминала, но самой операции run_command нет
  assert.equal(/"run_command"/.test(p), false);
});

test('скобочные запросы [<{...}>] v22: виды, диапазон, фенсы, дедуп', () => {
  const ans = [
    'Сейчас посмотрю.',
    '[<{FILE INFO: FTAP ROBLOX.lua}>]',
    '[<{read range: src/main.cpp:10-50}>]',
    '[<{NEED SEARCH: handleLogin}>]',
    '[<{NEED FILE: docs/README.md}>]',
    '[<{NEED FILE: docs/README.md}>]', // дубль — игнор
    '```',
    '[<{NEED FILE: fake/example.js}>]', // в фенсе — пример, игнор
    '```',
    '[<{READ RANGE: big.lua:5000-9999}>]' // кап диапазона 1200
  ].join('\n');
  const r = Ops.findBracketRequests(ans);
  assert.equal(r.length, 5, 'ровно 5 уникальных запроса');
  assert.equal(r[0].kind, 'FILE INFO');
  assert.equal(r[1].kind, 'READ RANGE');
  assert.equal(r[1].arg, 'src/main.cpp:10-50');
  assert.equal(r[2].kind, 'NEED SEARCH');
  assert.equal(r[2].arg, 'handleLogin');
  assert.equal(r[3].kind, 'NEED FILE');
  assert.equal(r[4].arg, 'big.lua:5000-6199', 'диапазон обрезан до 1200 строк');
  // не наша скобка — мимо
  assert.equal(Ops.findBracketRequests('просто текст без маркеров').length, 0);
});
