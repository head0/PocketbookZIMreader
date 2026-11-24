#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "zim.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lzma.h>
#include "zstd.h"

namespace zim {

namespace {

const uint32_t kMagic = 0x044D495A;
const size_t kMaxClusterBytes = 96u << 20; // refuse absurd clusters (RAM is tight)
const size_t kClusterCacheSlots = 3;
const size_t kClusterCacheBytes = 48u << 20;

inline uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t rd64(const unsigned char *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

int cmpKey(char nsA, const std::string &a, char nsB, const std::string &b)
{
    if (nsA != nsB)
        return (unsigned char)nsA < (unsigned char)nsB ? -1 : 1;
    return a.compare(b); // std::string compares as unsigned bytes
}

bool endsWith(const std::string &s, const char *suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

} // namespace

Archive::Archive() {}
Archive::~Archive() { close(); }

void Archive::close()
{
    for (auto &p : parts_)
        if (p.fd >= 0)
            ::close(p.fd);
    parts_.clear();
    totalSize_ = 0;
    entryCache_.clear();
    clusters_.clear();
}

bool Archive::pread(uint64_t off, void *buf, size_t len)
{
    unsigned char *out = static_cast<unsigned char *>(buf);
    if (off > totalSize_ || len > totalSize_ - off)
        return false;
    for (auto &p : parts_) {
        if (len == 0)
            break;
        if (off >= p.start + p.size || off < p.start)
            continue;
        uint64_t local = off - p.start;
        size_t chunk = (size_t)std::min<uint64_t>(len, p.size - local);
        size_t done = 0;
        while (done < chunk) {
            ssize_t r = ::pread(p.fd, out + done, chunk - done, (off_t)(local + done));
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                return false;
            done += (size_t)r;
        }
        out += chunk;
        off += chunk;
        len -= chunk;
    }
    return len == 0;
}

bool Archive::open(const std::string &path, std::string &err)
{
    close();
    path_ = path;

    std::vector<std::string> files;
    if (endsWith(path, ".zimaa")) {
        std::string base = path.substr(0, path.size() - 2);
        for (char a = 'a'; a <= 'z'; ++a)
            for (char b = 'a'; b <= 'z'; ++b) {
                std::string f = base + a + b;
                if (::access(f.c_str(), R_OK) != 0)
                    goto listed;
                files.push_back(f);
            }
    } else {
        files.push_back(path);
    }
listed:
    if (files.empty()) {
        err = "fichier introuvable";
        return false;
    }
    for (auto &f : files) {
        int fd = ::open(f.c_str(), O_RDONLY | O_CLOEXEC);
        struct stat st;
        if (fd < 0 || fstat(fd, &st) != 0) {
            if (fd >= 0)
                ::close(fd);
            err = "impossible d'ouvrir " + f + " : " + strerror(errno);
            close();
            return false;
        }
        parts_.push_back(Part{fd, totalSize_, (uint64_t)st.st_size});
        totalSize_ += (uint64_t)st.st_size;
    }

    unsigned char h[80];
    if (!pread(0, h, sizeof h) || rd32(h) != kMagic) {
        err = "ce n'est pas un fichier ZIM";
        close();
        return false;
    }
    major_ = rd16(h + 4);
    minor_ = rd16(h + 6);
    if (major_ != 5 && major_ != 6) {
        err = "version ZIM non geree (" + std::to_string(major_) + ")";
        close();
        return false;
    }
    newNs_ = major_ == 6 && minor_ >= 1;
    entryCount_ = rd32(h + 24);
    clusterCount_ = rd32(h + 28);
    urlPtrPos_ = rd64(h + 32);
    titlePtrPos_ = rd64(h + 40);
    clusterPtrPos_ = rd64(h + 48);
    mimeListPos_ = rd64(h + 56);
    mainPage_ = rd32(h + 64);

    if (urlPtrPos_ > totalSize_ || (uint64_t)entryCount_ * 8 > totalSize_ - urlPtrPos_ ||
        clusterPtrPos_ > totalSize_ || (uint64_t)clusterCount_ * 8 > totalSize_ - clusterPtrPos_ ||
        mimeListPos_ >= totalSize_) {
        err = "fichier ZIM tronque ou incomplet (copie interrompue ?)";
        close();
        return false;
    }

    // MIME type list: zero terminated strings, ended by an empty one.
    mimes_.clear();
    {
        std::string buf;
        uint64_t pos = mimeListPos_;
        bool done = false;
        while (!done && buf.size() < 65536) {
            char tmp[1024];
            size_t n = (size_t)std::min<uint64_t>(sizeof tmp, totalSize_ - pos);
            if (n == 0 || !pread(pos, tmp, n))
                break;
            buf.append(tmp, n);
            pos += n;
            size_t start = 0;
            mimes_.clear();
            while (true) {
                size_t z = buf.find('\0', start);
                if (z == std::string::npos)
                    break;
                if (z == start) {
                    done = true;
                    break;
                }
                mimes_.push_back(buf.substr(start, z - start));
                start = z + 1;
            }
        }
    }

    // Title listing. Prefer the "front articles" listing of recent archives.
    listCount_ = 0;
    listInMemory_ = false;
    listNeedsHtmlFilter_ = false;
    Entry le;
    std::string lerr;
    if (newNs_ && find('X', "listing/titleOrdered/v1", le) && read(le, listBlob_, lerr) && listBlob_.size() >= 4) {
        listInMemory_ = true;
        listCount_ = (uint32_t)(listBlob_.size() / 4);
    } else if (titlePtrPos_ != 0 && titlePtrPos_ < totalSize_ &&
               (uint64_t)entryCount_ * 4 <= totalSize_ - titlePtrPos_) {
        listFilePos_ = titlePtrPos_;
        listCount_ = entryCount_;
        listNeedsHtmlFilter_ = newNs_;
    } else if (newNs_ && find('X', "listing/titleOrdered/v0", le) && read(le, listBlob_, lerr)) {
        listInMemory_ = true;
        listCount_ = (uint32_t)(listBlob_.size() / 4);
        listNeedsHtmlFilter_ = true;
    }
    return true;
}

bool Archive::entryAt(uint32_t index, Entry &e)
{
    if (index >= entryCount_)
        return false;
    auto it = entryCache_.find(index);
    if (it != entryCache_.end()) {
        e = it->second;
        return true;
    }
    unsigned char p8[8];
    if (!pread(urlPtrPos_ + (uint64_t)index * 8, p8, 8))
        return false;
    uint64_t pos = rd64(p8);
    if (pos >= totalSize_)
        return false;

    std::string buf;
    size_t want = 512;
    size_t fixed = 0;
    while (true) {
        size_t n = (size_t)std::min<uint64_t>(want, totalSize_ - pos);
        buf.resize(n);
        if (n < 12 || !pread(pos, &buf[0], n))
            return false;
        const unsigned char *b = reinterpret_cast<const unsigned char *>(buf.data());
        e = Entry();
        e.index = index;
        e.mime = rd16(b);
        e.ns = (char)b[3];
        if (e.mime == 0xffff) {
            e.redirect = true;
            e.redirectIndex = rd32(b + 8);
            fixed = 12;
        } else if (e.mime == 0xfffe || e.mime == 0xfffd) {
            e.special = true;
            fixed = 8;
        } else {
            if (n < 16)
                return false;
            e.cluster = rd32(b + 8);
            e.blob = rd32(b + 12);
            fixed = 16;
        }
        size_t z1 = buf.find('\0', fixed);
        size_t z2 = z1 == std::string::npos ? z1 : buf.find('\0', z1 + 1);
        if (z2 != std::string::npos) {
            e.url.assign(buf, fixed, z1 - fixed);
            e.title.assign(buf, z1 + 1, z2 - z1 - 1);
            break;
        }
        if (n < want || want >= 65536)
            return false;
        want *= 4;
    }
    if (e.title.empty())
        e.title = e.url;
    if (entryCache_.size() > 8192)
        entryCache_.clear();
    entryCache_[index] = e;
    return true;
}

bool Archive::find(char ns, const std::string &url, Entry &e)
{
    uint32_t lo = 0, hi = entryCount_;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!entryAt(mid, e))
            return false;
        int c = cmpKey(e.ns, e.url, ns, url);
        if (c == 0)
            return true;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

bool Archive::resolve(Entry &e)
{
    for (int i = 0; i < 16 && e.redirect; ++i) {
        Entry next;
        if (!entryAt(e.redirectIndex, next))
            return false;
        e = next;
    }
    return !e.redirect && !e.special;
}

std::string Archive::mimeType(const Entry &e) const
{
    return e.mime < mimes_.size() ? mimes_[e.mime] : std::string();
}

bool Archive::isHtml(const Entry &e) const
{
    return mimeType(e).compare(0, 9, "text/html") == 0;
}

bool Archive::mainPage(Entry &e)
{
    if (mainPage_ != 0xffffffffu && entryAt(mainPage_, e) && resolve(e))
        return true;
    if (newNs_ && find('W', "mainPage", e) && resolve(e))
        return true;
    return false;
}

std::string Archive::metadata(const std::string &name)
{
    Entry e;
    std::string out, err;
    if (find('M', name, e) && resolve(e) && read(e, out, err) && out.size() < 4096)
        return out;
    return std::string();
}

bool Archive::loadCluster(uint32_t n, Cluster *&out, std::string &err)
{
    for (auto &c : clusters_)
        if (c.number == n) {
            c.stamp = ++stamp_;
            out = &c;
            return true;
        }

    unsigned char p8[8];
    if (n >= clusterCount_ || !pread(clusterPtrPos_ + (uint64_t)n * 8, p8, 8)) {
        err = "cluster hors limites";
        return false;
    }
    uint64_t pos = rd64(p8);
    unsigned char info;
    if (!pread(pos, &info, 1)) {
        err = "lecture impossible (fichier tronque ?)";
        return false;
    }
    int comp = info & 0x0f;
    Cluster c;
    c.number = n;
    c.extended = (info & 0x10) != 0;
    pos += 1;

    const size_t kIn = 128 * 1024;
    std::vector<unsigned char> in(kIn);

    if (comp == 5) {
        ZSTD_DStream *ds = ZSTD_createDStream();
        if (!ds) {
            err = "memoire insuffisante";
            return false;
        }
        ZSTD_initDStream(ds);
        std::vector<char> outbuf(256 * 1024);
        bool finished = false, failed = false;
        while (!finished && !failed) {
            size_t avail = (size_t)std::min<uint64_t>(kIn, totalSize_ > pos ? totalSize_ - pos : 0);
            if (avail == 0 || !pread(pos, in.data(), avail)) {
                failed = true;
                break;
            }
            ZSTD_inBuffer ib = {in.data(), avail, 0};
            while (ib.pos < ib.size || true) {
                ZSTD_outBuffer ob = {outbuf.data(), outbuf.size(), 0};
                size_t r = ZSTD_decompressStream(ds, &ob, &ib);
                if (ZSTD_isError(r)) {
                    failed = true;
                    break;
                }
                c.data.append(outbuf.data(), ob.pos);
                if (c.data.size() > kMaxClusterBytes) {
                    failed = true;
                    break;
                }
                if (r == 0) {
                    finished = true;
                    break;
                }
                if (ib.pos == ib.size && ob.pos < ob.size)
                    break; // needs more input
            }
            pos += avail;
        }
        ZSTD_freeDStream(ds);
        if (!finished) {
            err = "decompression zstd impossible";
            return false;
        }
    } else if (comp == 4) {
        lzma_stream strm = LZMA_STREAM_INIT;
        if (lzma_stream_decoder(&strm, UINT64_MAX, 0) != LZMA_OK) {
            err = "memoire insuffisante";
            return false;
        }
        std::vector<unsigned char> outbuf(256 * 1024);
        bool finished = false, failed = false;
        while (!finished && !failed) {
            size_t avail = (size_t)std::min<uint64_t>(kIn, totalSize_ > pos ? totalSize_ - pos : 0);
            if (avail == 0 || !pread(pos, in.data(), avail)) {
                failed = true;
                break;
            }
            strm.next_in = in.data();
            strm.avail_in = avail;
            while (true) {
                strm.next_out = outbuf.data();
                strm.avail_out = outbuf.size();
                lzma_ret r = lzma_code(&strm, LZMA_RUN);
                c.data.append(reinterpret_cast<char *>(outbuf.data()), outbuf.size() - strm.avail_out);
                if (r == LZMA_STREAM_END) {
                    finished = true;
                    break;
                }
                if (r != LZMA_OK || c.data.size() > kMaxClusterBytes) {
                    failed = true;
                    break;
                }
                if (strm.avail_in == 0 && strm.avail_out != 0)
                    break;
            }
            pos += avail;
        }
        lzma_end(&strm);
        if (!finished) {
            err = "decompression xz impossible";
            return false;
        }
    } else {
        err = "compression de cluster non geree (" + std::to_string(comp) + ")";
        return false;
    }

    // Evict: keep few clusters and a bounded number of bytes.
    size_t total = c.data.size();
    for (auto &k : clusters_)
        total += k.data.size();
    while (!clusters_.empty() && (clusters_.size() >= kClusterCacheSlots || total > kClusterCacheBytes)) {
        size_t oldest = 0;
        for (size_t i = 1; i < clusters_.size(); ++i)
            if (clusters_[i].stamp < clusters_[oldest].stamp)
                oldest = i;
        total -= clusters_[oldest].data.size();
        clusters_.erase(clusters_.begin() + (long)oldest);
    }
    c.stamp = ++stamp_;
    clusters_.push_back(std::move(c));
    out = &clusters_.back();
    return true;
}

bool Archive::read(const Entry &e, std::string &out, std::string &err)
{
    out.clear();
    if (e.redirect || e.special) {
        err = "entree sans contenu";
        return false;
    }
    unsigned char p8[8];
    if (e.cluster >= clusterCount_ || !pread(clusterPtrPos_ + (uint64_t)e.cluster * 8, p8, 8)) {
        err = "cluster hors limites";
        return false;
    }
    uint64_t cpos = rd64(p8);
    unsigned char info;
    if (!pread(cpos, &info, 1)) {
        err = "lecture impossible (fichier tronque ?)";
        return false;
    }
    int comp = info & 0x0f;
    bool extended = (info & 0x10) != 0;
    size_t osz = extended ? 8 : 4;

    if (comp == 0 || comp == 1) {
        // Uncompressed: read the blob straight from the file.
        unsigned char o[16];
        uint64_t base = cpos + 1;
        if (!pread(base, o, osz)) {
            err = "lecture impossible";
            return false;
        }
        uint64_t first = extended ? rd64(o) : rd32(o);
        uint64_t count = first / osz;
        if (count < 2 || e.blob >= count - 1 || !pread(base + (uint64_t)e.blob * osz, o, osz * 2)) {
            err = "blob hors limites";
            return false;
        }
        uint64_t a = extended ? rd64(o) : rd32(o);
        uint64_t b = extended ? rd64(o + 8) : rd32(o + 4);
        if (b < a || b - a > kMaxClusterBytes) {
            err = "contenu trop volumineux";
            return false;
        }
        out.resize((size_t)(b - a));
        if (b > a && !pread(base + a, &out[0], out.size())) {
            err = "lecture impossible (fichier tronque ?)";
            return false;
        }
        return true;
    }

    Cluster *c = nullptr;
    if (!loadCluster(e.cluster, c, err))
        return false;
    const unsigned char *d = reinterpret_cast<const unsigned char *>(c->data.data());
    size_t size = c->data.size();
    if (size < osz) {
        err = "cluster corrompu";
        return false;
    }
    uint64_t first = extended ? rd64(d) : rd32(d);
    uint64_t count = first / osz;
    if (count < 2 || e.blob >= count - 1 || first > size) {
        err = "blob hors limites";
        return false;
    }
    const unsigned char *o = d + (size_t)e.blob * osz;
    uint64_t a = extended ? rd64(o) : rd32(o);
    uint64_t b = extended ? rd64(o + 8) : rd32(o + 4);
    if (b < a || b > size) {
        err = "cluster corrompu";
        return false;
    }
    out.assign(c->data, (size_t)a, (size_t)(b - a));
    return true;
}

bool Archive::listIndex(uint32_t pos, uint32_t &index)
{
    if (pos >= listCount_)
        return false;
    unsigned char b[4];
    if (listInMemory_)
        memcpy(b, listBlob_.data() + (size_t)pos * 4, 4);
    else if (!pread(listFilePos_ + (uint64_t)pos * 4, b, 4))
        return false;
    index = rd32(b);
    return true;
}

bool Archive::articleAt(uint32_t pos, Entry &e)
{
    uint32_t idx;
    return listIndex(pos, idx) && entryAt(idx, e);
}

void Archive::titlesWithPrefix(const std::string &prefix, size_t max, std::vector<Entry> &out)
{
    char ns = articleNs();
    uint32_t lo = 0, hi = listCount_;
    Entry e;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!articleAt(mid, e))
            return;
        if (cmpKey(e.ns, e.title, ns, prefix) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t scanned = 0;
    for (uint32_t i = lo; i < listCount_ && out.size() < max && scanned < max * 20 + 200; ++i, ++scanned) {
        if (!articleAt(i, e))
            return;
        if (e.ns != ns || e.title.compare(0, prefix.size(), prefix) != 0)
            break;
        if (listNeedsHtmlFilter_ && !e.redirect && !isHtml(e))
            continue;
        out.push_back(e);
    }
}

} // namespace zim
