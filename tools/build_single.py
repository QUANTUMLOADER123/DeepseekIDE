#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собирает DeepSeekIDE.py (один файл) из tools/app_template.py + extension/*.js.
Порядок бандла: qt-клей -> opp.js -> fsbridge.js -> content.js.
qwebchannel.js подставляется ВО ВРЕМЯ инжекции из установленного PyQt6.
Запуск: python tools/build_single.py
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / 'tools' / 'app_template.py'
OUT = ROOT / 'DeepSeekIDE.py'
SENTINEL = '@@AGENT_JS@@'

GLUE_JS = r"""/* DeepSeekIDE Qt-клей: единый вызов в Python через QWebChannel (асинхронно).
 * window.dsxCall(kind, data) -> Promise(object)  — его дергают content.js
 * (webfetch/shell) и fsbridge.js (все fs-опы). */
window.__DSX_QT = true;
(function () {
  var pend = {}, tok = 0, ready = false, queue = [], bridge = null;
  window.dsxCall = function (kind, data) {
    return new Promise(function (res, rej) {
      var t = 'qt' + (++tok) + '_' + Date.now();
      pend[t] = { res: res, rej: rej };
      var go = function () {
        try { bridge.invokeOp(t, kind, JSON.stringify(data || {})); }
        catch (e) { delete pend[t]; rej(e); }
      };
      if (ready) go(); else queue.push(go);
      setTimeout(function () {
        if (pend[t]) { delete pend[t]; rej(new Error('qt bridge timeout')); }
      }, 185000);
    });
  };
  try {
    new QWebChannel(qt.webChannelTransport, function (ch) {
      bridge = ch.objects.dsx;
      bridge.reply.connect(function (t, json) {
        var p = pend[t]; if (!p) return; delete pend[t];
        try { p.res(JSON.parse(json)); } catch (e) { p.rej(e); }
      });
      ready = true;
      queue.forEach(function (f) { f(); });
      queue = [];
      console.log('[DSX] qt-bridge ready');
    });
  } catch (e) { console.warn('[DSX] qt-bridge init failed', e); }
})();"""


def main():
    tpl = TEMPLATE.read_text(encoding='utf-8')
    if SENTINEL not in tpl:
        raise SystemExit('sentinel missing in template')

    parts = [GLUE_JS]
    for name in ('opp.js', 'fsbridge.js', 'content.js'):
        src = (ROOT / 'extension' / name).read_text(encoding='utf-8')
        parts.append(src)
    agent = '\n;\n'.join(parts)

    if '"""' in agent or agent.endswith('\\'):
        raise SystemExit('agent contains banned python-string sequence')

    out = tpl.replace(SENTINEL, agent)
    OUT.write_text(out, encoding='utf-8')
    print('built %s: %d bytes (agent = %d bytes)' % (OUT.name, len(out), len(agent)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
