// =====================================================================
// host.cs — ЕДИНЫЙ native messaging host DeepSeek Extended (Windows).
// Chrome/Edge НЕ умеют запускать .bat/.ps1 как нативных хостов — только
// настоящий .exe. Четыре режима:
//   (Chrome spawn, stdin piped)  → протокол native messaging: stdin host
//     запрос:  4 байта little-endian длины + UTF-8 JSON { id, cmd }
//     ответ:   тот же фрейминг  { ok, stdout, stderr, exitCode, killed, cwd }
//   host.exe (двойной клик)      → активная консоль: «OK, я жив», пеком можно
//     вручную гонять команды — они выполняются ка к в проектной папке
//   host.exe selftest            → install.bat: прогон echo+cd, SELFTEST OK
// Рабочая папка: project.cwd рядом с exe (пишет install.bat) или папка exe.
// =====================================================================
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

class Host {
    static Stream In;
    static Stream Out;
    static string WorkingDir;

    // ---------- протокол ----------
    static byte[] ReadFrame() {
        var lenBuf = new byte[4];
        int got = 0;
        while (got < 4) {
            int r = In.Read(lenBuf, got, 4 - got);
            if (r <= 0) return null;
            got += r;
        }
        int len = BitConverter.ToInt32(lenBuf, 0);
        if (len <= 0 || len > 1048576) return null;
        var buf = new byte[len];
        int off = 0;
        while (off < len) {
            int r = In.Read(buf, off, len - off);
            if (r <= 0) return null;
            off += r;
        }
        return buf;
    }

    static void WriteFrame(string s) {
        var bytes = Encoding.UTF8.GetBytes(s);
        var lenBuf = BitConverter.GetBytes(bytes.Length);
        Out.Write(lenBuf, 0, 4);
        Out.Write(bytes, 0, bytes.Length);
        Out.Flush();
    }

    static string J(string s) {
        if (s == null) return "null";
        var sb = new StringBuilder("\"");
        foreach (var c in s) {
            switch (c) {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                default:
                    if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                    else sb.Append(c);
                    break;
            }
        }
        sb.Append('"');
        return sb.ToString();
    }

    // минимальный JSON-парсер строк вида { "id": "...", "cmd": "..." }
    static string GetJsonStr(string json, string key) {
        var needle = "\"" + key + "\"";
        int i = json.IndexOf(needle);
        if (i < 0) return null;
        i = json.IndexOf(':', i + needle.Length);
        if (i < 0) return null;
        i++;
        while (i < json.Length && char.IsWhiteSpace(json[i])) i++;
        if (i >= json.Length || json[i] != '"') return null;
        i++;
        var sb = new StringBuilder();
        while (i < json.Length) {
            char c = json[i];
            if (c == '"') break;
            if (c == '\\' && i + 1 < json.Length) {
                char n = json[i + 1];
                switch (n) {
                    case '"': sb.Append('"'); i += 2; break;
                    case '\\': sb.Append('\\'); i += 2; break;
                    case '/': sb.Append('/'); i += 2; break;
                    case 'n': sb.Append('\n'); i += 2; break;
                    case 'r': sb.Append('\r'); i += 2; break;
                    case 't': sb.Append('\t'); i += 2; break;
                    case 'u':
                        if (i + 5 < json.Length) {
                            try { sb.Append((char)Convert.ToInt32(json.Substring(i + 2, 4), 16)); i += 6; }
                            catch { sb.Append(n); i += 2; }
                            break;
                        }
                        sb.Append(n); i += 2; break;
                    default: sb.Append(n); i += 2; break;
                }
            } else {
                sb.Append(c); i++;
            }
        }
        return sb.ToString();
    }

    // декод вывода: строгий UTF-8, при битых байтах — OEM (cp866 и т.п.)
    static string Decode(byte[] b) {
        if (b == null || b.Length == 0) return "";
        try {
            var strict = new UTF8Encoding(false, true);
            return strict.GetString(b);
        } catch {
            try {
                int oem = NativeMethods.GetOEMCP();
                if (oem <= 0) oem = 866;
                return Encoding.GetEncoding(oem).GetString(b);
            } catch {
                try { return Encoding.GetEncoding(866).GetString(b); }
                catch { return Encoding.Default.GetString(b); }
            }
        }
    }

    static byte[] ReadAllBytes(Stream s) {
        using (var ms = new MemoryStream()) {
            var buf = new byte[8192];
            int r;
            while ((r = s.Read(buf, 0, buf.Length)) > 0) ms.Write(buf, 0, r);
            return ms.ToArray();
        }
    }

    // ---------- выполнение одной команды (общий путь) ----------
    static string Run(string cmd, out string stdout, out string stderr, out int code, out bool killed) {
        stdout = ""; stderr = ""; code = -1;
        var psi = new ProcessStartInfo("cmd.exe", "/c " + cmd);
        psi.WorkingDirectory = WorkingDir;
        psi.UseShellExecute = false;
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        psi.RedirectStandardInput = false;
        psi.CreateNoWindow = true;
        // Без StandardOutputEncoding: читаем RAW и декодируем сами.
        var p = Process.Start(psi);
        var outTask = Task.Run(() => ReadAllBytes(p.StandardOutput.BaseStream));
        var errTask = Task.Run(() => ReadAllBytes(p.StandardError.BaseStream));
        bool k = false;
        if (!p.WaitForExit(120000)) {
            try { p.Kill(); } catch { }
            k = true;
        }
        byte[] outBytes = new byte[0], errBytes = new byte[0];
        try { Task.WaitAll(new Task[] { outTask, errTask }, 5000); } catch { }
        try { outBytes = outTask.Result ?? outBytes; } catch { }
        try { errBytes = errTask.Result ?? errBytes; } catch { }
        stdout = Decode(outBytes);
        stderr = Decode(errBytes);
        code = k ? -1 : p.ExitCode;
        killed = k;
        if (stdout.Length > 65536) stdout = stdout.Substring(0, 65536) + "\n...(truncated)";
        if (stderr.Length > 16384) stderr = stderr.Substring(0, 16384) + "\n...(truncated)";
        return null;
    }

    static void Log(string msg) {
        try {
            var logPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "host.log");
            File.AppendAllText(logPath, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + msg + "\n");
        } catch { }
    }

    static void LoadWorkingDir() {
        try {
            var cwdFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "project.cwd");
            if (File.Exists(cwdFile)) {
                var w = File.ReadAllText(cwdFile).Trim();
                if (w.Length > 0 && Directory.Exists(w)) WorkingDir = w;
            }
        } catch { }
    }

    // ---------- режимы ----------
    static int SelfTest() {
        // прогон настоящей команды тем же пайплайном, что и нативный режим
        string so, se; int code; bool k;
        Run("echo selftest-ok & cd", out so, out se, out code, out k);
        Console.WriteLine("cmd: echo selftest-ok & cd");
        Console.WriteLine("exitCode=" + code + " killed=" + k);
        Console.WriteLine("stdout: " + so.Trim().Replace("\r\n", " | "));
        if (!string.IsNullOrEmpty(se))
            Console.WriteLine("stderr: " + se.Trim().Replace("\r\n", " | "));
        Console.WriteLine("cwd: " + WorkingDir);
        if (code == 0 && !k && so.IndexOf("selftest-ok") >= 0) {
            Console.WriteLine("SELFTEST OK");
            return 0;
        }
        Console.WriteLine("SELFTEST FAIL");
        return 1;
    }

    static void Interactive() {
        try { Console.OutputEncoding = Encoding.GetEncoding(NativeMethods.GetOEMCP()); } catch { }
        Console.WriteLine("== DeepSeek Extended shell host ==");
        Console.WriteLine("host.exe OK, alive. cwd = " + WorkingDir);
        Console.WriteLine("This console is for manual checks; Chrome spawns its own silent instance.");
        Console.WriteLine("Type a command - it runs via cmd /c in the project folder. Empty line / 'exit' to quit.");
        string line;
        while ((line = Console.ReadLine()) != null) {
            line = line.Trim();
            if (line.Length == 0 || line.Equals("exit", StringComparison.OrdinalIgnoreCase)) break;
            try {
                string so, se; int code; bool k;
                Console.Write("$ " + line + Environment.NewLine);
                Run(line, out so, out se, out code, out k);
                if (so.Length > 0) Console.WriteLine(so.TrimEnd('\r', '\n'));
                if (se.Length > 0) Console.WriteLine("[stderr] " + se.TrimEnd('\r', '\n'));
                Console.WriteLine("[exit " + code + (k ? ", killed" : "") + "]");
            } catch (Exception e) {
                Console.WriteLine("ERR: " + e.Message);
            }
        }
    }

    static void NativeLoop() {
        Log("host started (native mode), cwd=" + WorkingDir);
        while (true) {
            var frame = ReadFrame();
            if (frame == null) { Log("stdin EOF, exit"); break; }
            string resp;
            try {
                var json = Encoding.UTF8.GetString(frame);
                var cmd = GetJsonStr(json, "cmd");
                if (string.IsNullOrEmpty(cmd) || cmd.Trim().Length == 0)
                    throw new Exception("empty command");
                Log("cmd: " + cmd.Substring(0, Math.Min(cmd.Length, 120)));
                string so, se; int code; bool k;
                Run(cmd, out so, out se, out code, out k);
                resp = "{\"ok\":true,\"stdout\":" + J(so) +
                       ",\"stderr\":" + J(se) +
                       ",\"exitCode\":" + code +
                       ",\"killed\":" + (k ? "true" : "false") +
                       ",\"cwd\":" + J(WorkingDir) + "}";
            } catch (Exception e) {
                Log("ERR: " + e.Message);
                resp = "{\"ok\":false,\"error\":" + J(e.Message) + "}";
            }
            WriteFrame(resp);
        }
    }

    static int Main(string[] args) {
        WorkingDir = AppDomain.CurrentDomain.BaseDirectory;
        LoadWorkingDir();
        // 1) явный селфтест (install.bat)
        if (args.Length > 0 && args[0] == "selftest") return SelfTest();
        // 2) двойной клик: консоль «говорит ок» и даёт погонять команды
        if (!Console.IsInputRedirected) { Interactive(); return 0; }
        // 3) Chrome/Edge: настоящий нативный режим
        In = Console.OpenStandardInput();
        Out = Console.OpenStandardOutput();
        try { NativeLoop(); }
        catch (Exception e) { Log("FATAL: " + e.Message); return 1; }
        return 0;
    }
}

internal static class NativeMethods {
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    internal static extern int GetOEMCP();
}
