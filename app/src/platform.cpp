#include "platform.h"
#include <SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#else
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/types.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace platform {

#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
static std::string narrow(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

std::string exePath() {
#ifdef _WIN32
    wchar_t b[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, b, sizeof b / sizeof b[0]);
    return narrow(std::wstring(b, n));
#else
    // Dans une AppImage, l'exécutable est dans un montage temporaire : on le relance par lui-même
    char b[4096];
    ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    return n > 0 ? std::string(b, n) : std::string("amiga1200");
#endif
}

std::string exeDir() {
    char *p = SDL_GetBasePath();
    std::string s = p ? p : "./";
    SDL_free(p);
    return s;
}

std::string documentsDir() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string r;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p) == S_OK) r = narrow(p);
    CoTaskMemFree(p);
    if (r.empty()) r = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".";
    return r;
#else
    // Dossier Documents de xdg-user-dirs (en français : ~/Documents aussi), sinon le dossier personnel
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    if (FILE *f = popen("xdg-user-dir DOCUMENTS 2>/dev/null", "r")) {
        char b[2048] = {0};
        if (fgets(b, sizeof b, f)) {
            std::string d(b);
            while (!d.empty() && (d.back() == '\n' || d.back() == '\r')) d.pop_back();
            pclose(f);
            if (!d.empty() && d != home && fs::is_directory(d)) return d;
        } else pclose(f);
    }
    return fs::is_directory(home + "/Documents") ? home + "/Documents" : home;
#endif
}

long selfPid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

bool processAlive(long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    // Processus enfant terminé : il est récupéré (sinon il resterait « zombie », vivant pour kill)
    int st;
    pid_t r = waitpid((pid_t)pid, &st, WNOHANG);
    if (r == (pid_t)pid) return false;
    return kill((pid_t)pid, 0) == 0;
#endif
}

long spawnSelf(const std::vector<std::string> &args, const std::map<std::string, std::string> &env) {
#ifdef _WIN32
    std::wstring cmd = L"\"" + wide(exePath()) + L"\"";
    for (auto &a : args) {
        std::wstring w = wide(a), q;
        for (wchar_t c : w) { if (c == L'"') q += L'\\'; q += c; }
        cmd += L" \"" + q + L"\"";
    }
    for (auto &[k, v] : env) SetEnvironmentVariableW(wide(k).c_str(), v.empty() ? nullptr : wide(v).c_str());
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    for (auto &[k, v] : env) SetEnvironmentVariableW(wide(k).c_str(), nullptr);
    if (!ok) return 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (long)pi.dwProcessId;
#else
    std::string self = std::getenv("APPIMAGE") ? std::getenv("APPIMAGE") : exePath();
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(self.c_str()));
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> envs;
    for (char **e = environ; *e; e++) {
        std::string s(*e);
        auto k = s.substr(0, s.find('='));
        if (!env.count(k)) envs.push_back(s);
    }
    for (auto &[k, v] : env) if (!v.empty()) envs.push_back(k + "=" + v);
    std::vector<char *> envp;
    for (auto &s : envs) envp.push_back(const_cast<char *>(s.c_str()));
    envp.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, self.c_str(), nullptr, nullptr, argv.data(), envp.data()) != 0) return 0;
    return (long)pid;
#endif
}

#ifndef _WIN32
// Linux : zenity (GNOME, la plupart des bureaux) ou kdialog (KDE)
static std::string shq(const std::string &s) {
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}
static bool has(const char *tool) {
    std::string c = std::string("command -v ") + tool + " >/dev/null 2>&1";
    return std::system(c.c_str()) == 0;
}
static std::vector<std::string> runLines(const std::string &cmd, char sep) {
    std::vector<std::string> out;
    FILE *f = popen(cmd.c_str(), "r");
    if (!f) return out;
    std::string all;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) all.append(b, n);
    pclose(f);
    std::string cur;
    for (char c : all) {
        if (c == sep || c == '\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
#endif

std::vector<std::string> openFiles(const std::string &title, bool multiple, const std::vector<std::string> &filters) {
#ifdef _WIN32
    std::wstring filter;
    for (auto &f : filters) {
        auto bar = f.find('|');
        filter += wide(f.substr(0, bar)); filter += L'\0';
        filter += wide(bar == std::string::npos ? "*.*" : f.substr(bar + 1)); filter += L'\0';
    }
    filter += L"Tous les fichiers"; filter += L'\0'; filter += L"*.*"; filter += L'\0'; filter += L'\0';
    std::vector<wchar_t> buf(65536, 0);
    std::wstring wt = wide(title);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o;
    o.lpstrFilter = filter.c_str(); o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size(); o.lpstrTitle = wt.c_str();
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | (multiple ? OFN_ALLOWMULTISELECT : 0);
    if (!GetOpenFileNameW(&o)) return {};
    // Plusieurs fichiers : le dossier, puis les noms, séparés par des zéros
    std::vector<std::wstring> parts;
    for (wchar_t *p = buf.data(); *p; p += wcslen(p) + 1) parts.push_back(p);
    std::vector<std::string> r;
    if (parts.size() == 1) r.push_back(narrow(parts[0]));
    else for (size_t i = 1; i < parts.size(); i++) r.push_back(narrow(parts[0] + L"\\" + parts[i]));
    return r;
#else
    if (has("zenity")) {
        std::string c = "zenity --file-selection --title=" + shq(title) + (multiple ? " --multiple --separator='\x1f'" : "");
        for (auto &f : filters) {
            auto bar = f.find('|');
            std::string pat = bar == std::string::npos ? "*" : f.substr(bar + 1);
            for (auto &ch : pat) if (ch == ';') ch = ' ';
            // zenity ne distingue pas les majuscules : on ajoute les deux écritures
            std::string up;
            for (char ch : pat) up += (char)toupper((unsigned char)ch);
            c += " --file-filter=" + shq(f.substr(0, bar) + " | " + pat + " " + up);
        }
        c += " --file-filter=" + shq("Tous les fichiers | *");
        return runLines(c + " 2>/dev/null", '\x1f');
    }
    if (has("kdialog")) {
        std::string c = "kdialog --title " + shq(title) + " --getopenfilename . " + (multiple ? "--multiple --separate-output" : "");
        return runLines(c + " 2>/dev/null", '\n');
    }
    message("Choix de fichiers impossible", "Installez zenity (ou kdialog) pour choisir des fichiers, ou glissez-les sur la fenêtre.");
    return {};
#endif
}

std::string saveFile(const std::string &title, const std::string &name, const std::string &ext) {
#ifdef _WIN32
    std::vector<wchar_t> buf(4096, 0);
    std::wstring n = wide(name);
    wcsncpy(buf.data(), n.c_str(), buf.size() - 1);
    std::wstring filter = wide(ext) + L'\0' + L"*." + wide(ext) + L'\0' + L'\0';
    std::wstring wt = wide(title), we = wide(ext);
    OPENFILENAMEW o{}; o.lStructSize = sizeof o;
    o.lpstrFilter = filter.c_str(); o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size(); o.lpstrTitle = wt.c_str();
    o.lpstrDefExt = we.c_str(); o.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&o) ? narrow(buf.data()) : std::string();
#else
    std::vector<std::string> r;
    if (has("zenity")) r = runLines("zenity --file-selection --save --confirm-overwrite --title=" + shq(title) + " --filename=" + shq(name) + " 2>/dev/null", '\n');
    else if (has("kdialog")) r = runLines("kdialog --title " + shq(title) + " --getsavefilename " + shq(name) + " 2>/dev/null", '\n');
    else message("Enregistrement impossible", "Installez zenity (ou kdialog) pour choisir où enregistrer.");
    if (r.empty()) return {};
    std::string p = r[0];
    if (fs::path(p).extension().empty()) p += "." + ext;
    return p;
#endif
}

void open(const std::string &target) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", wide(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string c = "xdg-open " + shq(target) + " >/dev/null 2>&1 &";
    if (std::system(c.c_str())) {}
#endif
}

void initTimers() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

void message(const std::string &title, const std::string &text) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), nullptr);
}

}  // namespace platform
