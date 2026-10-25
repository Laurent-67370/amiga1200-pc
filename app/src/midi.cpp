#include "midi.h"
#include "bridge.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif

namespace {
std::atomic<bool> running{false};
std::thread pumpThread, inThread;

// Messages complets à partir des octets joués par l'Amiga (statut courant, SysEx)
struct Parser {
    uint8_t status = 0, msg[3] = {0};
    int need = 0, have = 0;
    std::vector<uint8_t> sysex;
    bool inSysex = false;
    static int dataBytes(uint8_t s) {
        if (s < 0xF0) { uint8_t h = s & 0xF0; return h == 0xC0 || h == 0xD0 ? 1 : 2; }
        if (s == 0xF1 || s == 0xF3) return 1;
        if (s == 0xF2) return 2;
        return 0;
    }
    template <class Short, class Long> void feed(uint8_t b, Short shortMsg, Long longMsg) {
        if (b >= 0xF8) { shortMsg(&b, 1); return; }             // temps réel : à part
        if (inSysex) {
            if (b == 0xF7 || b < 0x80) { sysex.push_back(b); if (b == 0xF7) { longMsg(sysex.data(), sysex.size()); inSysex = false; } return; }
            inSysex = false;
        }
        if (b == 0xF0) { inSysex = true; sysex.assign(1, b); return; }
        if (b >= 0x80) {
            status = b; need = dataBytes(b); have = 0; msg[0] = b;
            if (need == 0) { shortMsg(msg, 1); if (b >= 0xF0) status = 0; }
            return;
        }
        if (!status) return;
        if (have == 0) msg[0] = status;
        msg[1 + have++] = b;
        if (have == need) { shortMsg(msg, 1 + need); have = 0; if (status >= 0xF0) status = 0; }
    }
};

#ifdef _WIN32
HMIDIOUT hout;
HMIDIIN hin;
std::string narrow(const wchar_t *w) {
    char b[256];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, nullptr, nullptr);
    return b;
}
void CALLBACK inProc(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    uint8_t b[3] = {(uint8_t)(p1 & 0xFF), (uint8_t)((p1 >> 8) & 0xFF), (uint8_t)((p1 >> 16) & 0xFF)};
    amb_midi_write(b, 1 + (unsigned)Parser::dataBytes(b[0]));
}
#else
// Séquenceur ALSA par dlopen : l'AppImage ne dépend pas de libasound
struct Alsa {
    void *lib = nullptr, *seq = nullptr, *enc = nullptr, *dec = nullptr;
    int port = -1;
    int (*seq_open)(void **, const char *, int, int);
    int (*seq_close)(void *);
    int (*set_client_name)(void *, const char *);
    int (*create_simple_port)(void *, const char *, unsigned, unsigned);
    int (*connect_to)(void *, int, int, int);
    int (*connect_from)(void *, int, int, int);
    int (*client_info_malloc)(void **);
    void (*client_info_free)(void *);
    void (*client_info_set_client)(void *, int);
    int (*client_info_get_client)(const void *);
    const char *(*client_info_get_name)(void *);
    int (*query_next_client)(void *, void *);
    int (*port_info_malloc)(void **);
    void (*port_info_free)(void *);
    void (*port_info_set_client)(void *, int);
    void (*port_info_set_port)(void *, int);
    int (*port_info_get_port)(const void *);
    unsigned (*port_info_get_capability)(const void *);
    unsigned (*port_info_get_type)(const void *);
    const char *(*port_info_get_name)(const void *);
    int (*query_next_port)(void *, void *);
    int (*midi_event_new)(size_t, void **);
    void (*midi_event_free)(void *);
    long (*midi_event_encode_byte)(void *, int, void *);
    long (*midi_event_decode)(void *, unsigned char *, long, const void *);
    void (*midi_event_no_status)(void *, int);
    int (*event_output_direct)(void *, void *);
    int (*event_input)(void *, void **);
    bool load() {
        if (lib) return true;
        lib = dlopen("libasound.so.2", RTLD_NOW);
        if (!lib) return false;
#define F(n, s) if (!(*(void **)&n = dlsym(lib, s))) { dlclose(lib); lib = nullptr; return false; }
        F(seq_open, "snd_seq_open") F(seq_close, "snd_seq_close") F(set_client_name, "snd_seq_set_client_name")
        F(create_simple_port, "snd_seq_create_simple_port") F(connect_to, "snd_seq_connect_to") F(connect_from, "snd_seq_connect_from")
        F(client_info_malloc, "snd_seq_client_info_malloc") F(client_info_free, "snd_seq_client_info_free")
        F(client_info_set_client, "snd_seq_client_info_set_client") F(client_info_get_client, "snd_seq_client_info_get_client")
        F(client_info_get_name, "snd_seq_client_info_get_name") F(query_next_client, "snd_seq_query_next_client")
        F(port_info_malloc, "snd_seq_port_info_malloc") F(port_info_free, "snd_seq_port_info_free")
        F(port_info_set_client, "snd_seq_port_info_set_client") F(port_info_set_port, "snd_seq_port_info_set_port")
        F(port_info_get_port, "snd_seq_port_info_get_port") F(port_info_get_capability, "snd_seq_port_info_get_capability")
        F(port_info_get_type, "snd_seq_port_info_get_type")
        F(port_info_get_name, "snd_seq_port_info_get_name") F(query_next_port, "snd_seq_query_next_port")
        F(midi_event_new, "snd_midi_event_new") F(midi_event_free, "snd_midi_event_free")
        F(midi_event_encode_byte, "snd_midi_event_encode_byte") F(midi_event_decode, "snd_midi_event_decode")
        F(midi_event_no_status, "snd_midi_event_no_status")
        F(event_output_direct, "snd_seq_event_output_direct") F(event_input, "snd_seq_event_input")
#undef F
        return true;
    }
    // Ports des autres clients : (nom, client, port), qui reçoivent (output) ou qui envoient
    struct P { std::string name; int client, port; };
    std::vector<P> ports(void *s, bool output) {
        std::vector<P> r;
        void *ci, *pi;
        client_info_malloc(&ci); port_info_malloc(&pi);
        client_info_set_client(ci, -1);
        while (query_next_client(s, ci) >= 0) {
            int c = client_info_get_client(ci);
            if (c == 0) continue;   // système
            port_info_set_client(pi, c);
            port_info_set_port(pi, -1);
            while (query_next_port(s, pi) >= 0) {
                unsigned cap = port_info_get_capability(pi);
                // SND_SEQ_PORT_CAP_WRITE|SUBS_WRITE = 0x02|0x40, READ|SUBS_READ = 0x01|0x20 ; NO_EXPORT = 0x80
                unsigned want = output ? 0x42 : 0x21;
                if ((cap & want) != want || (cap & 0x80)) continue;
                r.push_back({std::string(client_info_get_name(ci)) + " : " + port_info_get_name(pi), c, port_info_get_port(pi)});
            }
        }
        port_info_free(pi); client_info_free(ci);
        return r;
    }
} alsa;
// Événement du séquenceur (snd_seq_event_t, 28 octets) : place réservée, rempli par snd_midi_event_encode_byte
struct SeqEvent { uint8_t type, flags, tag, queue; uint32_t time[2]; uint8_t src[2], dst[2]; uint8_t data[12]; };
#endif
}  // namespace

namespace midi {
std::vector<std::string> names(bool output) {
    std::vector<std::string> r;
#ifdef _WIN32
    if (output) {
        UINT n = midiOutGetNumDevs();
        for (UINT i = 0; i < n; i++) { MIDIOUTCAPSW c; if (midiOutGetDevCapsW(i, &c, sizeof c) == MMSYSERR_NOERROR) r.push_back(narrow(c.szPname)); }
    } else {
        UINT n = midiInGetNumDevs();
        for (UINT i = 0; i < n; i++) { MIDIINCAPSW c; if (midiInGetDevCapsW(i, &c, sizeof c) == MMSYSERR_NOERROR) r.push_back(narrow(c.szPname)); }
    }
#else
    void *s = nullptr;
    if (alsa.load() && alsa.seq_open(&s, "default", 0, 0) >= 0) {
        for (auto &p : alsa.ports(s, output)) r.push_back(p.name);
        alsa.seq_close(s);
    }
#endif
    return r;
}

std::string start(const std::string &outName, const std::string &inName) {
    stop();
    std::string msg;
    bool haveOut = false, haveIn = false;
#ifdef _WIN32
    UINT n = midiOutGetNumDevs(), pick = n;
    for (UINT i = 0; i < n; i++) { MIDIOUTCAPSW c; if (midiOutGetDevCapsW(i, &c, sizeof c) == MMSYSERR_NOERROR && narrow(c.szPname) == outName) pick = i; }
    if (pick == n && n > 0) pick = 0;
    if (pick < n && midiOutOpen(&hout, pick, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        MIDIOUTCAPSW c; midiOutGetDevCapsW(pick, &c, sizeof c);
        msg = "MIDI → " + narrow(c.szPname); haveOut = true;
    } else msg = "Aucun appareil MIDI pour la sortie";
    if (!inName.empty()) {
        UINT ni = midiInGetNumDevs();
        for (UINT i = 0; i < ni; i++) {
            MIDIINCAPSW c;
            if (midiInGetDevCapsW(i, &c, sizeof c) == MMSYSERR_NOERROR && narrow(c.szPname) == inName &&
                midiInOpen(&hin, i, (DWORD_PTR)inProc, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                midiInStart(hin); haveIn = true; msg += " · " + inName + " → Amiga"; break;
            }
        }
    }
    amb_midi_enable(haveOut ? 1 : 0, haveIn ? 1 : 0);
    running = true;
    if (haveOut) pumpThread = std::thread([] {
        Parser p;
        uint8_t b[1024];
        while (running) {
            unsigned n = amb_midi_read(b, sizeof b);
            for (unsigned i = 0; i < n; i++)
                p.feed(b[i], [](const uint8_t *m, size_t len) { DWORD v = 0; for (size_t k = 0; k < len; k++) v |= (DWORD)m[k] << (8 * k); midiOutShortMsg(hout, v); },
                       [](const uint8_t *m, size_t len) {
                           std::vector<char> d(m, m + len);
                           MIDIHDR h{}; h.lpData = d.data(); h.dwBufferLength = (DWORD)len;
                           if (midiOutPrepareHeader(hout, &h, sizeof h) != MMSYSERR_NOERROR) return;
                           midiOutLongMsg(hout, &h, sizeof h);
                           for (int t = 0; t < 500 && !(h.dwFlags & MHDR_DONE); t++) Sleep(1);
                           midiOutUnprepareHeader(hout, &h, sizeof h);
                       });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
#else
    if (!alsa.load()) return "MIDI indisponible (ALSA absent)";
    // SND_SEQ_OPEN_DUPLEX = 3, SND_SEQ_NONBLOCK = 1 (la lecture n'attend pas : le fil s'arrête proprement) ;
    // port : lecture et écriture, abonnements (0x63), type application (1 << 20)
    if (alsa.seq_open(&alsa.seq, "default", 3, 1) < 0) return "MIDI indisponible (séquenceur ALSA)";
    alsa.set_client_name(alsa.seq, "Amiga 1200");
    alsa.port = alsa.create_simple_port(alsa.seq, "Port série Amiga", 0x63, (1u << 1) | (1u << 20));
    auto outs = alsa.ports(alsa.seq, true);
    const Alsa::P *dst = nullptr;
    for (auto &p : outs) if (p.name == outName) dst = &p;
    // Sans choix : le premier synthétiseur trouvé, en évitant « Midi Through »
    if (!dst) for (auto &p : outs) if (p.name.find("Through") == std::string::npos) { dst = &p; break; }
    if (dst && alsa.connect_to(alsa.seq, alsa.port, dst->client, dst->port) >= 0) { msg = "MIDI → " + dst->name; haveOut = true; }
    else msg = "Aucun appareil MIDI pour la sortie";
    if (!inName.empty()) for (auto &p : alsa.ports(alsa.seq, false))
        if (p.name == inName && alsa.connect_from(alsa.seq, alsa.port, p.client, p.port) >= 0) { haveIn = true; msg += " · " + inName + " → Amiga"; break; }
    alsa.midi_event_new(1024, &alsa.enc);
    alsa.midi_event_new(1024, &alsa.dec);
    alsa.midi_event_no_status(alsa.dec, 1);
    amb_midi_enable(haveOut ? 1 : 0, haveIn ? 1 : 0);
    running = true;
    if (haveOut) pumpThread = std::thread([] {
        uint8_t b[1024];
        while (running) {
            unsigned n = amb_midi_read(b, sizeof b);
            for (unsigned i = 0; i < n; i++) {
                SeqEvent ev{};
                if (alsa.midi_event_encode_byte(alsa.enc, b[i], &ev) == 1) {
                    // Envoi direct à tous les abonnés du port : source = notre port, destination « abonnés » (254)
                    ev.queue = 253;                  // SND_SEQ_QUEUE_DIRECT
                    ev.src[1] = (uint8_t)alsa.port;
                    ev.dst[0] = 254; ev.dst[1] = 0;   // SND_SEQ_ADDRESS_SUBSCRIBERS
                    alsa.event_output_direct(alsa.seq, &ev);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    if (haveIn) inThread = std::thread([] {
        while (running) {
            void *ev = nullptr;
            if (alsa.event_input(alsa.seq, &ev) >= 0 && ev) {
                unsigned char buf[256];
                long n = alsa.midi_event_decode(alsa.dec, buf, sizeof buf, ev);
                if (n > 0) amb_midi_write(buf, (unsigned)n);
            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
#endif
    return msg;
}

void stop() {
    if (!running) return;
    running = false;
    amb_midi_enable(0, 0);
    if (pumpThread.joinable()) pumpThread.join();
#ifdef _WIN32
    if (hin) { midiInStop(hin); midiInClose(hin); hin = nullptr; }
    if (hout) { midiOutReset(hout); midiOutClose(hout); hout = nullptr; }
#else
    if (inThread.joinable()) inThread.join();
    if (alsa.seq) alsa.seq_close(alsa.seq);
    alsa.seq = nullptr;
    if (alsa.enc) { alsa.midi_event_free(alsa.enc); alsa.enc = nullptr; }
    if (alsa.dec) { alsa.midi_event_free(alsa.dec); alsa.dec = nullptr; }
#endif
}
}  // namespace midi
