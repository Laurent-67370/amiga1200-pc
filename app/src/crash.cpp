#include "app.h"
#include "platform.h"
#include "store.h"
#include <SDL.h>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static std::string marker() { return paths::support() + ".emulation"; }
std::string crashFile() { return paths::support() + "plantage.txt"; }
static std::string systemFile() { return paths::support() + "plantage-systeme.txt"; }

// Chemins préparés d'avance : au moment du plantage, on n'alloue plus rien
static char sysPath[1024], dumpPath[1024];

#ifdef _WIN32
static LONG WINAPI onCrash(EXCEPTION_POINTERS *ep) {
    HANDLE f = CreateFileA(sysPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char b[512];
        DWORD w;
        auto put = [&](const char *t) { WriteFile(f, t, (DWORD)strlen(t), &w, nullptr); };
        EXCEPTION_RECORD *r = ep->ExceptionRecord;
        snprintf(b, sizeof b, "Exception 0x%08lX à l'adresse %p\n", r->ExceptionCode, r->ExceptionAddress);
        put(b);
        // Pile d'appels : module et décalage de chaque adresse (l'exécutable n'a pas de symboles)
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 sf{};
        sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
        HANDLE proc = GetCurrentProcess(), thr = GetCurrentThread();
        SymInitialize(proc, nullptr, TRUE);
        for (int i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thr, &sf, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++) {
            DWORD64 base = SymGetModuleBase64(proc, sf.AddrPC.Offset);
            char mod[MAX_PATH] = "?";
            if (base) GetModuleFileNameA((HMODULE)base, mod, sizeof mod);
            const char *name = strrchr(mod, '\\') ? strrchr(mod, '\\') + 1 : mod;
            snprintf(b, sizeof b, "  #%02d %s+0x%llx\n", i, name, (unsigned long long)(sf.AddrPC.Offset - base));
            put(b);
        }
        CloseHandle(f);
    }
    // Minidump : à joindre au signalement pour une analyse plus poussée
    HANDLE d = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (d != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mi{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), d, MiniDumpNormal, &mi, nullptr, nullptr);
        CloseHandle(d);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void onCrash(int sig) {
    int f = open(sysPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f >= 0) {
        char b[96];
        int n = snprintf(b, sizeof b, "Signal %d (%s)\n", sig, strsignal(sig));
        if (write(f, b, (size_t)n) < 0) {}
        void *frames[64];
        int k = backtrace(frames, 64);
        backtrace_symbols_fd(frames, k, f);
        close(f);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

void crashInstallHandler() {
    snprintf(sysPath, sizeof sysPath, "%s", systemFile().c_str());
    snprintf(dumpPath, sizeof dumpPath, "%s", (paths::support() + "plantage.dmp").c_str());
    std::error_code ec;
    fs::remove(fs::u8path(systemFile()), ec);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onCrash);
#else
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) signal(sig, onCrash);
#endif
}

void crashStarted() { std::ofstream(fs::u8path(marker()), std::ios::trunc) << platform::selfPid(); }
void crashEnded() {
    std::ifstream f(fs::u8path(marker()));
    long pid = 0;
    if (f >> pid && pid == platform::selfPid()) { f.close(); std::error_code ec; fs::remove(fs::u8path(marker()), ec); }
}

// Une marque restée alors que le processus n'existe plus, c'est un arrêt brutal
bool crashCheck(std::string &report) {
    long pid = 0;
    {
        std::ifstream f(fs::u8path(marker()));
        if (!(f >> pid)) return false;
    }
    if (platform::processAlive(pid)) return false;
    std::error_code ec;
    fs::remove(fs::u8path(marker()), ec);
    char date[64];
    time_t t = time(nullptr);
    strftime(date, sizeof date, "%d/%m/%Y %H:%M:%S", localtime(&t));
    report = std::string("Plantage de l'émulation le ") + date + " (amiga1200-pc " AMIGA1200_VERSION ", " + SDL_GetPlatform() + ")\n\n";
    std::deque<std::string> last;
    std::ifstream log(fs::u8path(paths::log()));
    std::string line;
    while (std::getline(log, line)) { last.push_back(line); if (last.size() > 150) last.pop_front(); }
    report += "— Fin du journal de la séance —\n";
    for (auto &l : last) report += l + "\n";
    // Trace du système, si le plantage l'a laissée
    std::ifstream sys(fs::u8path(systemFile()));
    if (sys) {
        std::string t((std::istreambuf_iterator<char>(sys)), std::istreambuf_iterator<char>());
        report += "\n— Trace du système —\n" + t;
#ifdef _WIN32
        if (paths::exists(paths::support() + "plantage.dmp")) report += "\n(minidump : " + paths::support() + "plantage.dmp, à joindre au signalement)\n";
#endif
    } else report += "\n(pas de trace du système : l'émulation a peut-être été arrêtée de force)\n";
    std::ofstream(fs::u8path(crashFile()), std::ios::trunc) << report;
    return true;
}
