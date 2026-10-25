#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ======================================================================
# DeepSeekIDE.py — DeepSeek как десктоп-IDE в ОДНОМ файле (PyQt6 WebEngine).
# БЕЗ браузерных расширений, БЕЗ native messaging host: весь агент
# (пилот, чтение/правка файлов, память, интернет, SHELL) встроен:
#   JS-агент инжектится в страницу DeepSeek; «транспорт» (файлы, шелл,
#   интернет, диалог выбора папки) делает Python через QWebChannel.
# Запуск:  pip install PyQt6 PyQt6-WebEngine  &&  python DeepSeekIDE.py
# Проверка без GUI:  python DeepSeekIDE.py --selftest
# ======================================================================
import base64
import ctypes
import html.parser
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--disable-logging"

APP_NAME = "DeepSeekIDE"
START_URL = "https://chat.deepseek.com/"
TIMEOUT_SHELL = 120          # сек
TIMEOUT_FETCH = 20           # сек (dl = 60)
CAP_STDOUT, CAP_STDERR = 65536, 16384
CAP_PAGE_TEXT = 300000
CAP_DOWNLOAD = 16 * 1024 * 1024


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
UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36')


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


FS_OPS = {
    'ensure': fs_ensure, 'forget': fs_forget, 'read': fs_read, 'write': fs_write,
    'mkdir': fs_mkdir, 'remove': fs_remove, 'stat': fs_stat, 'tree': fs_tree,
    'search': fs_search, 'write_b64': fs_write_b64,
}


# ======================================================================
# JS-агент (сборка из extension/ через tools/build_single.py):
#   qt-клей + opp.js + fsbridge.js + content.js, qwebchannel.js грузится
#   из установленного PyQt6 в момент инжекции.
# ======================================================================
AGENT_JS = r"""@@AGENT_JS@@"""


def load_qwebchannel_js():
    """qwebchannel.js лежит в установленном PyQt6 — поднимаем оттуда."""
    import pathlib
    import PyQt6
    base = pathlib.Path(PyQt6.__file__).resolve().parent
    hits = list(base.glob('**/qtwebchannel/qwebchannel.js'))
    if not hits:
        raise RuntimeError('qwebchannel.js не найден в PyQt6 — переустанови: pip install -U PyQt6-WebEngine')
    return hits[0].read_text(encoding='utf-8')


# ======================================================================
# Самотест без GUI: FS-пайплайн + shell + webfetch (сеть опциональна)
# ======================================================================
def selftest():
    import tempfile
    root = tempfile.mkdtemp(prefix='dsx_selftest_')
    cfg = load_cfg(); cfg['_selftest_folder'] = root
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
# Qt: окно + WebChannel-мост
# ======================================================================
def run_app():
    from PyQt6.QtCore import QUrl, Qt
    from PyQt6.QtGui import QMouseEvent, QRegion
    from PyQt6.QtWebChannel import QWebChannel
    from PyQt6.QtWebEngineCore import QWebEngineProfile, QWebEngineSettings, QWebEnginePage
    from PyQt6.QtWebEngineWidgets import QWebEngineView
    from PyQt6.QtWidgets import QApplication, QMainWindow, QPushButton, QHBoxLayout, QVBoxLayout, QWidget, QLabel, QFileDialog
    from PyQt6.QtCore import QObject, pyqtSlot, pyqtSignal

    class Bridge(QObject):
        reply = pyqtSignal(str, str)

        def __init__(self_obj, win):
            super().__init__()
            self_obj.win = win

        @pyqtSlot(str, str, str)
        def invokeOp(self_obj, token, kind, payload):
            try:
                args = json.loads(payload or '{}')
            except Exception:
                args = {}
            # выбор папки — только из GUI-потока (модальный диалог), отвечаем сразу
            if kind == 'fs' and args.get('op') == 'pick':
                path = QFileDialog.getExistingDirectory(self_obj.win, 'Папка проекта') or ''
                if path:
                    cfg = load_cfg(); cfg['folder'] = path; save_cfg(cfg)
                    out = {'ok': True, 'path': path, 'name': os.path.basename(os.path.normpath(path))}
                else:
                    out = {'ok': False, 'error': 'папка не выбрана'}
                self_obj.reply.emit(token, json.dumps(out, ensure_ascii=False))
                return

            def work():
                try:
                    if kind == 'fs':
                        op = args.get('op')
                        if op not in FS_OPS:
                            out = {'ok': False, 'error': 'unknown fs op: %s' % op}
                        else:
                            out = FS_OPS[op](args)
                    elif kind == 'shell':
                        out = shell_run(args)
                    elif kind == 'webfetch':
                        out = web_fetch(args)
                    else:
                        out = {'ok': False, 'error': 'unknown kind: %s' % kind}
                except Exception as e:
                    out = {'ok': False, 'error': str(e)}
                self_obj.reply.emit(token, json.dumps(out, ensure_ascii=False))
            threading.Thread(target=work, daemon=True).start()

    class DeepSeekApp(QMainWindow):
        def __init__(self):
            super().__init__()
            self.setWindowTitle("DeepSeek IDE")
            self.setGeometry(0, 0, 1400, 700)
            self.setMinimumSize(1000, 500)
            self.setWindowFlags(Qt.WindowType.FramelessWindowHint)
            self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
            self.drag_position = None
            self.normal_geometry = None
            self._agent_js = None

            self.setStyleSheet("""
                QMainWindow { background: transparent; }
                #header { background: #1a1a1a; }
                #title { color: rgb(71, 107, 197); font-size: 13px; font-weight: bold; }
                QPushButton {
                    background: transparent; color: white; border: none; font-size: 16px; padding: 4px 8px;
                }
                QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 6px; }
                QWebEngineView { background: #0d0d0d; }
            """)

            self.profile = QWebEngineProfile("DeepSeekStorage", self)
            self.profile.setPersistentStoragePath(os.path.join(app_data_dir(), 'webprofile'))
            self.profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies)
            self.profile.setHttpUserAgent(UA)

            self.page = QWebEnginePage(self.profile, self)
            settings = self.page.settings()
            settings.setAttribute(QWebEngineSettings.WebAttribute.JavascriptEnabled, True)
            settings.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)

            # WebChannel: объект 'dsx' виден странице
            self.bridge = Bridge(self)
            self.channel = QWebChannel(self.page)
            self.channel.registerObject('dsx', self.bridge)
            self.page.setWebChannel(self.channel)

            self.view = QWebEngineView()
            self.view.setPage(self.page)
            self.page.loadFinished.connect(self.inject_agent)
            self.view.load(QUrl(START_URL))

            header = QWidget(objectName="header")
            header.setFixedHeight(40)
            header_layout = QHBoxLayout(header)
            header_layout.setContentsMargins(16, 0, 16, 0)
            header_layout.setSpacing(0)

            title = QLabel("deepseek ide")
            title.setObjectName("title")

            minimize_btn = QPushButton("−")
            minimize_btn.clicked.connect(self.showMinimized)
            fullscreen_btn = QPushButton("□")
            fullscreen_btn.clicked.connect(self.toggle_fullscreen)
            close_btn = QPushButton("✕")
            close_btn.clicked.connect(self.close)

            btn_layout = QHBoxLayout()
            btn_layout.setSpacing(8)
            btn_layout.addWidget(minimize_btn)
            btn_layout.addWidget(fullscreen_btn)
            btn_layout.addWidget(close_btn)

            header_layout.addWidget(title)
            header_layout.addStretch()
            header_layout.addLayout(btn_layout)

            main_layout = QVBoxLayout()
            main_layout.setContentsMargins(0, 0, 0, 0)
            main_layout.setSpacing(0)
            main_layout.addWidget(header)
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
            """Полный бандл агента: qwebchannel.js + qt-клей + opp/fsbridge/content."""
            if self._agent_js is None:
                try:
                    qweb_js = load_qwebchannel_js()
                except Exception as e:
                    print('[DSX] ' + str(e))
                    return
                self._agent_js = qweb_js + '\n;\n' + AGENT_JS
            self.page.runJavaScript(self._agent_js)

        def center_window(self):
            screen = QApplication.primaryScreen().availableGeometry()
            window_rect = self.frameGeometry()
            window_rect.moveCenter(screen.center())
            self.move(window_rect.topLeft())

        def rounded_rect_region(self):
            rect = self.rect()
            radius = 24
            region = QRegion()
            region += QRegion(rect.left() + radius, rect.top(), rect.width() - 2*radius, 40, QRegion.RegionType.Rectangle)
            region += QRegion(rect.left(), rect.top() + radius, rect.width(), 40 - radius, QRegion.RegionType.Rectangle)
            region += QRegion(rect.left(), rect.top(), radius*2, radius*2, QRegion.RegionType.Ellipse)
            region += QRegion(rect.right() - radius*2, rect.top(), radius*2, radius*2, QRegion.RegionType.Ellipse)
            region += QRegion(rect.left(), rect.top() + 40, rect.width(), rect.height() - 40 - radius, QRegion.RegionType.Rectangle)
            region += QRegion(rect.left() + radius, rect.bottom() - radius, rect.width() - 2*radius, radius, QRegion.RegionType.Rectangle)
            region += QRegion(rect.left(), rect.bottom() - radius*2, radius*2, radius*2, QRegion.RegionType.Ellipse)
            region += QRegion(rect.right() - radius*2, rect.bottom() - radius*2, radius*2, radius*2, QRegion.RegionType.Ellipse)
            return region

        def resizeEvent(self, event):
            self.setMask(self.rounded_rect_region())
            if event:
                super().resizeEvent(event)

        def toggle_fullscreen(self):
            if self.isFullScreen():
                self.showNormal()
                if self.normal_geometry:
                    self.setGeometry(self.normal_geometry)
                self.setMask(self.rounded_rect_region())
            else:
                self.normal_geometry = self.geometry()
                self.showFullScreen()
                self.setMask(QRegion())

        def keyPressEvent(self, event):
            if event.key() == Qt.Key.Key_Escape and self.isFullScreen():
                self.toggle_fullscreen()
            super().keyPressEvent(event)

        def mousePressEvent(self, event: QMouseEvent):
            if event.button() == Qt.MouseButton.LeftButton:
                clicked = self.childAt(event.position().toPoint())
                if clicked and clicked.objectName() == "header":
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
    window = DeepSeekApp()
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    run_app()
