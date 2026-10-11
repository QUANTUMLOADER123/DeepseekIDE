/* =====================================================================
 * bg.js — фоновый service worker DeepSeek Extended.
 * Единственная задача: WEB FETCH — cont-скрипт не может дергать чужие
 * сайты из-за CORS, фон с host_permissions — может. Принимает сообщение
 * { dsx: 'webfetch', url } и возвращает { ok, status, type, text }.
 * ===================================================================== */
'use strict';

chrome.runtime.onMessage.addListener(function (m, sender, sendResponse) {
  if (!m || m.dsx !== 'webfetch' || typeof m.url !== 'string') return;
  (async function () {
    var timer = 0;
    try {
      var ctrl = new AbortController();
      timer = setTimeout(function () { ctrl.abort(); }, 20000);
      var r = await fetch(m.url, {
        signal: ctrl.signal,
        redirect: 'follow',
        credentials: 'omit',
        cache: 'no-store'
      });
      clearTimeout(timer);
      var type = r.headers.get('content-type') || '';
      var text = await r.text();
      sendResponse({ ok: true, status: r.status, type: type, text: text.slice(0, 300000) });
    } catch (e) {
      clearTimeout(timer);
      var msg = String((e && e.message) || e || 'fetch failed');
      if (msg.indexOf('abort') >= 0) msg = 'страница не ответила за 20 сек';
      sendResponse({ ok: false, error: msg });
    }
  })();
  return true; // ответ асинхронный
});
