// Minimal read-only ZIM archive reader (no libzim / xapian / ICU dependency).
// Supports ZIM v5/v6, old and new namespace schemes, zstd + xz + uncompressed
// clusters, extended clusters and split archives (.zimaa, .zimab, ...).
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zim {

struct Entry {
    uint32_t index = 0;
    char ns = 0;
    uint16_t mime = 0;
    bool redirect = false;
    bool special = false; // link target / deleted entry: no content
    uint32_t redirectIndex = 0;
    uint32_t cluster = 0;
    uint32_t blob = 0;
    std::string url;
    std::string title; // never empty: falls back to url
};

class Archive {
public:
    Archive();
    ~Archive();
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    bool open(const std::string &path, std::string &err);
    const std::string &path() const { return path_; }

    bool newNamespaces() const { return newNs_; }
    char articleNs() const { return newNs_ ? 'C' : 'A'; }
    uint32_t entryCount() const { return entryCount_; }

    bool entryAt(uint32_t index, Entry &e);
    bool find(char ns, const std::string &url, Entry &e);
    // Follows redirects (bounded). Returns false on broken chains.
    bool resolve(Entry &e);
    bool read(const Entry &e, std::string &out, std::string &err);
    std::string mimeType(const Entry &e) const;
    bool isHtml(const Entry &e) const;

    bool mainPage(Entry &e);
    std::string metadata(const std::string &name);

    // Title-ordered listing of articles.
    uint32_t articleCount() const { return listCount_; }
    bool articleAt(uint32_t pos, Entry &e);
    // Entries whose title starts with `prefix` (byte-wise), at most `max`.
    void titlesWithPrefix(const std::string &prefix, size_t max, std::vector<Entry> &out);

private:
    struct Part {
        int fd;
        uint64_t start, size;
    };
    struct Cluster {
        uint32_t number = 0;
        uint64_t stamp = 0;
        bool extended = false;
        std::string data;
    };

    bool pread(uint64_t off, void *buf, size_t len);
    bool loadCluster(uint32_t n, Cluster *&out, std::string &err);
    bool listIndex(uint32_t pos, uint32_t &index);
    void close();

    std::string path_;
    std::vector<Part> parts_;
    uint64_t totalSize_ = 0;
    uint16_t major_ = 0, minor_ = 0;
    bool newNs_ = false;
    uint32_t entryCount_ = 0, clusterCount_ = 0;
    uint64_t urlPtrPos_ = 0, titlePtrPos_ = 0, clusterPtrPos_ = 0, mimeListPos_ = 0;
    uint32_t mainPage_ = 0xffffffffu;
    std::vector<std::string> mimes_;

    // title listing: either a region of the file or an in-memory blob
    uint32_t listCount_ = 0;
    uint64_t listFilePos_ = 0;
    std::string listBlob_;
    bool listInMemory_ = false;
    bool listNeedsHtmlFilter_ = false;

    std::unordered_map<uint32_t, Entry> entryCache_;
    std::vector<Cluster> clusters_;
    uint64_t stamp_ = 0;
};

} // namespace zim
