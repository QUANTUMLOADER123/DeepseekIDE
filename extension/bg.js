/* =====================================================================
 * bg.js — фоновый service worker DeepSeek Extended.
 * Единственная задача: WEB FETCH — cont-скрипт не может дергать чужие
 * сайты из-за CORS, фон с host_permissions — может. Принимает сообщение
 * { dsx: 'webfetch', url } и возвращает { ok, status, type, text }.
 * ===================================================================== */
'use strict';

// runtime-сообщения идут JSON-сериализацией — бинарь носим base64-строкой
function bufToBase64(buf) {
  var bytes = new Uint8Array(buf), out = '', CH = 0x8000;
  for (var i = 0; i < bytes.length; i += CH) {
    out += String.fromCharCode.apply(null, bytes.subarray(i, i + CH));
  }
  return btoa(out);
}

chrome.runtime.onMessage.addListener(function (m, sender, sendResponse) {
  if (m && m.dsx === 'shell' && typeof m.cmd === 'string') {
    // SHELL-мост: команда уезжает в нативный хост, ответ — обратно в контент
    (async function () {
      try {
        var resp = await chrome.runtime.sendNativeMessage('ai.deepseekide.shell', { id: m.id || '', cmd: m.cmd });
        sendResponse(resp && typeof resp === 'object' ? resp : { ok: false, error: 'странный ответ хоста' });
      } catch (e) {
        var msg = String((e && e.message) || e || 'native messaging failed');
        if (msg.indexOf('not found') >= 0 || msg.indexOf('not.installed') >= 0)
          msg = 'хост шелла не установлен — запусти shell/install.bat из репозитория и перезапусти Chrome';
        else if (msg.indexOf('forbidden') >= 0 || msg.indexOf('access') >= 0)
          msg = 'ID расширения не в allowed_origins хоста — переустанови хост (install.bat) с новой версией';
        sendResponse({ ok: false, error: msg });
      }
    })();
    return true;
  }
  if (!m || m.dsx !== 'webfetch' || typeof m.url !== 'string') return;
  (async function () {
    var timer = 0;
    try {
      var ctrl = new AbortController();
      timer = setTimeout(function () { ctrl.abort(); }, m.dl ? 60000 : 20000);
      var r = await fetch(m.url, {
        signal: ctrl.signal,
        redirect: 'follow',
        credentials: 'omit',
        cache: 'no-store'
      });
      clearTimeout(timer);
      var type = r.headers.get('content-type') || '';
      if (m.dl) { // режим скачивания файла (кап 16 МБ)
        var buf = await r.arrayBuffer();
        if (buf.byteLength > 16 * 1024 * 1024) {
          sendResponse({ ok: false, error: 'файл больше 16 МБ (' + Math.round(buf.byteLength / 1048576) + ')' });
          return;
        }
        sendResponse({ ok: true, status: r.status, type: type, size: buf.byteLength, b64: bufToBase64(buf) });
        return;
      }
      var text = await r.text();
      sendResponse({ ok: true, status: r.status, type: type, text: text.slice(0, 300000) });
    } catch (e) {
      clearTimeout(timer);
      var msg = String((e && e.message) || e || 'fetch failed');
      if (msg.indexOf('abort') >= 0) msg = 'страница не ответила за ' + (m.dl ? 60 : 20) + ' сек';
      sendResponse({ ok: false, error: msg });
    }
  })();
  return true; // ответ асинхронный
});
