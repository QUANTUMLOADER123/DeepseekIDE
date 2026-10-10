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

test('MEMORY SEARCH: маркер ловится, поиск по заметкам скорит и режет', () => {
  const r = Ops.findBracketRequests('гляну память:\n[<{MEMORY SEARCH: поля пакета движения}>]');
  assert.equal(r.length, 1);
  assert.equal(r[0].kind, 'MEMORY SEARCH');
  assert.equal(r[0].arg, 'поля пакета движения');

  const notes = [
    { t: 'Пакет движения', x: 'struct MovePacket {\n  float x, y, z;\n  uint8 flags; // бит 0 — прыжок\n};\nОтправляется каждые 33мс.' },
    { t: 'Рецепты зелий', x: 'зелье скорости: сахар + паутина\nзелье силы: огненный порошок' }
  ];
  // совпадение по строкам
  const hits = Ops.searchMemory(notes, 'flags прыжок');
  assert.equal(hits.length, 1, 'одна карточка попала');
  assert.equal(hits[0].title, 'Пакет движения');
  assert.ok(hits[0].snippet.indexOf('uint8 flags') >= 0, 'фрагмент со строкой flags присутствует');
  // совпадение только по заголовку — фолбэк на начало заметки
  const t2 = Ops.searchMemory(notes, 'движения');
  assert.equal(t2.length, 1);
  assert.ok(t2[0].snippet.indexOf('MovePacket') >= 0, 'фолбэк-сниппет с началом заметки');
  assert.equal(Ops.searchMemory(notes, 'несуществующий кверселлум').length, 0, 'нет совпадений — пусто');
  assert.equal(Ops.searchMemory([], 'что-то').length, 0, 'пустая память — пусто');
});

test('ASK / ASK MULTI: вопросы с опциями парсятся, фенсы игнорятся', () => {
  const a = Ops.findBracketRequests('[<{ASK: React или Vue? :: React ; Vue ; Vanilla JS}>]');
  assert.equal(a.length, 1);
  assert.equal(a[0].kind, 'ASK');
  assert.equal(a[0].arg, 'React или Vue?');
  assert.deepEqual(a[0].opts, ['React', 'Vue', 'Vanilla JS']);

  const m = Ops.findBracketRequests('смотри:\n[<{ASK MULTI: Какие файлы трогаем? :: main.cpp ; sdk/entity.inl }>]');
  assert.equal(m[0].kind, 'ASK MULTI');
  assert.deepEqual(m[0].opts, ['main.cpp', 'sdk/entity.inl']);

  // без опций — один вопрос
  const bare = Ops.findBracketRequests('[<{ASK: Какой порт слушаем?}>]');
  assert.equal(bare[0].arg, 'Какой порт слушаем?');
  assert.deepEqual(bare[0].opts, []);

  // в фенсе — игнор
  assert.equal(Ops.findBracketRequests('```\n[<{ASK: x :: y ; z}>]\n```').length, 0);
});
