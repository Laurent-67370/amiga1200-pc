#include "zip.h"
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace {
FILE *openFile(const std::string &p, const char *mode) {
#ifdef _WIN32
    std::wstring w = fs::u8path(p).wstring(), m(mode, mode + strlen(mode));
    return _wfopen(w.c_str(), m.c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}
bool seek(FILE *f, uint64_t pos) {
#ifdef _WIN32
    return _fseeki64(f, (long long)pos, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)pos, SEEK_SET) == 0;
#endif
}
uint64_t tell(FILE *f) {
#ifdef _WIN32
    return (uint64_t)_ftelli64(f);
#else
    return (uint64_t)ftello(f);
#endif
}
uint64_t fileSize(FILE *f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
#else
    fseeko(f, 0, SEEK_END);
#endif
    return tell(f);
}
uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint64_t r64(const uint8_t *p) { return (uint64_t)r32(p) | (uint64_t)r32(p + 4) << 32; }
struct Closer { void operator()(FILE *f) const { if (f) fclose(f); } };
using File = std::unique_ptr<FILE, Closer>;

// Données d'un fichier de l'archive, passées une fois décompressées à sink ; faux si erreur ou somme CRC fausse
template <class Sink> bool inflateEntry(FILE *f, const zip::Entry &e, Sink sink) {
    uint8_t lh[30];
    if (!seek(f, e.offset) || fread(lh, 1, 30, f) != 30 || r32(lh) != 0x04034b50) return false;
    if (!seek(f, e.offset + 30 + r16(lh + 26) + r16(lh + 28))) return false;
    std::vector<uint8_t> in(1 << 16), out(1 << 16);
    uint64_t left = e.csize;
    uLong crc = crc32(0, nullptr, 0);
    if (e.method == 0) {
        while (left) {
            size_t n = fread(in.data(), 1, (size_t)std::min<uint64_t>(left, in.size()), f);
            if (!n) return false;
            crc = crc32(crc, in.data(), (uInt)n);
            if (!sink(in.data(), n)) return false;
            left -= n;
        }
        return (uint32_t)crc == e.crc;
    }
    if (e.method != 8) return false;
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    int r = Z_OK;
    while (r != Z_STREAM_END) {
        if (z.avail_in == 0) {
            if (!left) break;
            size_t n = fread(in.data(), 1, (size_t)std::min<uint64_t>(left, in.size()), f);
            if (!n) break;
            left -= n;
            z.next_in = in.data(); z.avail_in = (uInt)n;
        }
        z.next_out = out.data(); z.avail_out = (uInt)out.size();
        r = inflate(&z, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) break;
        size_t got = out.size() - z.avail_out;
        crc = crc32(crc, out.data(), (uInt)got);
        if (got && !sink(out.data(), got)) { r = Z_DATA_ERROR; break; }
    }
    inflateEnd(&z);
    return r == Z_STREAM_END && (uint32_t)crc == e.crc;
}
}  // namespace

namespace zip {
bool list(const std::string &zip, std::vector<Entry> &out) {
    out.clear();
    File f(openFile(zip, "rb"));
    if (!f) return false;
    uint64_t size = fileSize(f.get());
    if (size < 22) return false;
    // Fin du répertoire central : dans les 64 derniers Ko (commentaire compris)
    uint64_t start = size > 65557 ? size - 65557 : 0;
    std::vector<uint8_t> tail((size_t)(size - start));
    if (!seek(f.get(), start) || fread(tail.data(), 1, tail.size(), f.get()) != tail.size()) return false;
    long eocd = -1;
    for (long i = (long)tail.size() - 22; i >= 0; i--) if (r32(&tail[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return false;
    uint64_t count = r16(&tail[eocd + 10]), cdSize = r32(&tail[eocd + 12]), cdOff = r32(&tail[eocd + 16]);
    // zip64 : localisateur juste avant
    if ((cdOff == 0xFFFFFFFF || count == 0xFFFF) && eocd >= 20 && r32(&tail[eocd - 20]) == 0x07064b50) {
        uint64_t z64 = r64(&tail[eocd - 20 + 8]);
        uint8_t b[56];
        if (!seek(f.get(), z64) || fread(b, 1, 56, f.get()) != 56 || r32(b) != 0x06064b50) return false;
        count = r64(b + 32); cdSize = r64(b + 40); cdOff = r64(b + 48);
    }
    if (cdOff + cdSize > size || cdSize > (512u << 20)) return false;
    std::vector<uint8_t> cd((size_t)cdSize);
    if (!seek(f.get(), cdOff) || fread(cd.data(), 1, cd.size(), f.get()) != cd.size()) return false;
    size_t p = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (p + 46 > cd.size() || r32(&cd[p]) != 0x02014b50) return false;
        Entry e;
        e.method = r16(&cd[p + 10]); e.crc = r32(&cd[p + 16]);
        e.csize = r32(&cd[p + 20]); e.usize = r32(&cd[p + 24]);
        uint16_t nl = r16(&cd[p + 28]), xl = r16(&cd[p + 30]), cl = r16(&cd[p + 32]);
        e.offset = r32(&cd[p + 42]);
        if (p + 46 + nl + xl + cl > cd.size()) return false;
        e.name.assign((const char *)&cd[p + 46], nl);
        // Champ zip64 : les valeurs à 0xFFFFFFFF y sont, dans l'ordre
        for (size_t x = p + 46 + nl; x + 4 <= p + 46 + nl + xl;) {
            uint16_t id = r16(&cd[x]), len = r16(&cd[x + 2]);
            if (id == 0x0001) {
                size_t q = x + 4;
                if (e.usize == 0xFFFFFFFF && q + 8 <= x + 4 + len) { e.usize = r64(&cd[q]); q += 8; }
                if (e.csize == 0xFFFFFFFF && q + 8 <= x + 4 + len) { e.csize = r64(&cd[q]); q += 8; }
                if (e.offset == 0xFFFFFFFF && q + 8 <= x + 4 + len) e.offset = r64(&cd[q]);
            }
            x += 4 + len;
        }
        for (auto &c : e.name) if (c == '\\') c = '/';
        out.push_back(e);
        p += 46 + nl + xl + cl;
    }
    return true;
}

bool extract(const std::string &zip, const Entry &e, const std::string &dest) {
    File f(openFile(zip, "rb"));
    if (!f) return false;
    std::error_code ec;
    fs::create_directories(fs::u8path(dest).parent_path(), ec);
    File o(openFile(dest, "wb"));
    if (!o) return false;
    bool ok = inflateEntry(f.get(), e, [&](const uint8_t *d, size_t n) { return fwrite(d, 1, n, o.get()) == n; });
    o.reset();
    if (!ok) fs::remove(fs::u8path(dest), ec);
    return ok;
}

bool test(const std::string &zip) {
    std::vector<Entry> es;
    if (!list(zip, es) || es.empty()) return false;
    File f(openFile(zip, "rb"));
    if (!f) return false;
    for (auto &e : es) {
        if (e.dir()) continue;
        uint64_t n = 0;
        if (!inflateEntry(f.get(), e, [&](const uint8_t *, size_t k) { n += k; return true; }) || n != e.usize) return false;
    }
    return true;
}

// ---------- écriture ----------
namespace {
void w16(std::string &s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); }
void w32(std::string &s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); }
void w64(std::string &s, uint64_t v) { for (int i = 0; i < 8; i++) s += (char)((v >> (8 * i)) & 0xFF); }
}

Writer::Writer(const std::string &path) { f = openFile(path, "wb"); }
Writer::~Writer() { if (f) fclose((FILE *)f); }

bool Writer::addFile(const std::string &src, const std::string &name) { return add(name, nullptr, &src); }
bool Writer::addData(const std::string &data, const std::string &name) { return add(name, &data, nullptr); }

// En-tête local toujours en zip64 (tailles connues à la fin seulement) : réécrit une fois le fichier compressé
bool Writer::add(const std::string &name, const std::string *data, const std::string *src) {
    FILE *o = (FILE *)f;
    if (!o) return false;
    File in;
    if (src) { in.reset(openFile(*src, "rb")); if (!in) return false; }
    Entry e;
    e.name = name; e.method = 8; e.offset = tell(o);
    std::string h;
    w32(h, 0x04034b50); w16(h, 45); w16(h, 0x0800); w16(h, 8); w16(h, 0); w16(h, 0x21);   // UTF-8, deflate, date 1980
    w32(h, 0); w32(h, 0xFFFFFFFF); w32(h, 0xFFFFFFFF);
    w16(h, (uint16_t)name.size()); w16(h, 20);
    h += name;
    w16(h, 0x0001); w16(h, 16); w64(h, 0); w64(h, 0);
    if (fwrite(h.data(), 1, h.size(), o) != h.size()) return false;
    z_stream z{};
    if (deflateInit2(&z, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    std::vector<uint8_t> buf(1 << 16), out(1 << 16);
    uLong crc = crc32(0, nullptr, 0);
    size_t dpos = 0;
    bool okw = true;
    for (;;) {
        size_t n;
        if (data) { n = std::min(buf.size(), data->size() - dpos); memcpy(buf.data(), data->data() + dpos, n); dpos += n; }
        else n = fread(buf.data(), 1, buf.size(), in.get());
        bool last = n < buf.size();
        crc = crc32(crc, buf.data(), (uInt)n);
        e.usize += n;
        z.next_in = buf.data(); z.avail_in = (uInt)n;
        do {
            z.next_out = out.data(); z.avail_out = (uInt)out.size();
            deflate(&z, last ? Z_FINISH : Z_NO_FLUSH);
            size_t got = out.size() - z.avail_out;
            if (fwrite(out.data(), 1, got, o) != got) okw = false;
            e.csize += got;
        } while (z.avail_out == 0);
        if (last) break;
    }
    deflateEnd(&z);
    e.crc = (uint32_t)crc;
    uint64_t end = tell(o);
    // Somme CRC et tailles dans l'en-tête local
    std::string fix;
    w32(fix, e.crc);
    seek(o, e.offset + 14); fwrite(fix.data(), 1, 4, o);
    std::string sizes;
    w64(sizes, e.usize); w64(sizes, e.csize);
    seek(o, e.offset + 30 + name.size() + 4); fwrite(sizes.data(), 1, 16, o);
    seek(o, end);
    entries.push_back(e);
    return okw;
}

bool Writer::close() {
    FILE *o = (FILE *)f;
    if (!o) return false;
    uint64_t cdOff = tell(o);
    std::string cd;
    for (auto &e : entries) {
        w32(cd, 0x02014b50); w16(cd, 45); w16(cd, 45); w16(cd, 0x0800); w16(cd, 8); w16(cd, 0); w16(cd, 0x21);
        w32(cd, e.crc); w32(cd, 0xFFFFFFFF); w32(cd, 0xFFFFFFFF);
        w16(cd, (uint16_t)e.name.size()); w16(cd, 28); w16(cd, 0); w16(cd, 0); w16(cd, 0); w32(cd, 0);
        w32(cd, 0xFFFFFFFF);
        cd += e.name;
        w16(cd, 0x0001); w16(cd, 24); w64(cd, e.usize); w64(cd, e.csize); w64(cd, e.offset);
    }
    uint64_t z64 = cdOff + cd.size();
    w32(cd, 0x06064b50); w64(cd, 44); w16(cd, 45); w16(cd, 45); w32(cd, 0); w32(cd, 0);
    w64(cd, entries.size()); w64(cd, entries.size()); w64(cd, z64 - cdOff); w64(cd, cdOff);
    w32(cd, 0x07064b50); w32(cd, 0); w64(cd, z64); w32(cd, 1);
    w32(cd, 0x06054b50); w16(cd, 0); w16(cd, 0); w16(cd, 0xFFFF); w16(cd, 0xFFFF); w32(cd, 0xFFFFFFFF); w32(cd, 0xFFFFFFFF); w16(cd, 0);
    bool ok = fwrite(cd.data(), 1, cd.size(), o) == cd.size();
    ok = fclose(o) == 0 && ok;
    f = nullptr;
    return ok;
}
}  // namespace zip
