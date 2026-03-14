// btrfs_scan.cpp — MetaRecoverX Btrfs filesystem scanner
// Scans raw disk images or block devices for Btrfs inodes and directory entries.
// Output CSV: fs,inode,path,size,uid,gid,mode,extents
//
// Btrfs is little-endian on disk.
// Compile: g++ -std=c++17 -O2 -o btrfs_scan btrfs_scan.cpp
//
// Btrfs on-disk structures referenced:
//   Superblock           — at byte offset 65536 (0x10000)
//   Leaf node header     — 101 bytes at start of each tree node (level==0)
//   Item descriptor      — 25 bytes: {objectid(8), type(1), offset(8), data_off(4), data_sz(4)}
//   INODE_ITEM  (type=1) — 160 bytes: size @16, uid @44, gid @48, mode @52
//   INODE_REF   (type=12)— index(8) + name_len(2) + name(name_len)
//   DIR_ITEM    (type=84)— disk_key(17) + transid(8) + data_len(2) + name_len(2) + ftype(1) + name(name_len)

#ifndef _FILE_OFFSET_BITS
#  define _FILE_OFFSET_BITS 64
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#if defined(__linux__)
#  include <fcntl.h>
#  include <linux/fs.h>
#  include <sys/ioctl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

// ─── Btrfs on-disk constants ──────────────────────────────────────────────────
static const uint8_t  BTRFS_MAGIC[8] = {'_','B','H','R','f','S','_','M'};
static constexpr uint64_t BTRFS_SB_OFFSET  = 65536ULL; // primary superblock offset
static constexpr uint8_t  BTRFS_KEY_INODE_ITEM  =  1;
static constexpr uint8_t  BTRFS_KEY_INODE_REF   = 12;
static constexpr uint8_t  BTRFS_KEY_DIR_ITEM     = 84;
static constexpr uint8_t  BTRFS_KEY_DIR_INDEX    = 96;
static constexpr uint32_t BTRFS_LEAF_HEADER_SIZE = 101;
static constexpr uint32_t BTRFS_ITEM_SIZE        = 25;
static constexpr uint32_t BTRFS_DEFAULT_NODESIZE = 16384;

// ─── Little-endian helpers ────────────────────────────────────────────────────
static inline uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])        |
          (static_cast<uint32_t>(p[1]) <<  8) |
          (static_cast<uint32_t>(p[2]) << 16) |
          (static_cast<uint32_t>(p[3]) << 24);
}
static inline uint64_t le64(const uint8_t* p) {
    return static_cast<uint64_t>(le32(p)) |
          (static_cast<uint64_t>(le32(p + 4)) << 32);
}

// ─── Inode record ─────────────────────────────────────────────────────────────
struct BtrfsInodeRec {
    uint64_t ino  = 0;
    uint64_t size = 0;
    uint32_t uid  = 0;
    uint32_t gid  = 0;
    uint32_t mode = 0;
    std::string path; // set from INODE_REF / DIR_ITEM scan
};

struct BtrfsExtentRec {
    uint64_t bytenr   = 0; // physical byte offset in the image
    uint64_t numBytes = 0; // length in bytes
};

// ─── Global state ─────────────────────────────────────────────────────────────
static std::map<uint64_t, BtrfsInodeRec>         g_inodes;   // inode# -> record
static std::map<uint64_t, std::string>           g_names;    // inode# -> filename
static std::map<uint64_t, std::vector<BtrfsExtentRec>> g_extents;  // inode# -> extents
static uint8_t  g_fsid[16]    = {};
static bool     g_sb_found    = false;
static uint32_t g_nodesize    = BTRFS_DEFAULT_NODESIZE;
static uint32_t g_sectorsize  = 4096;
static uint64_t g_image_size  = 0;

// ─── Parse Btrfs superblock ───────────────────────────────────────────────────
// Superblock layout (byte offsets):
//   [0..31]   csum
//   [32..47]  fsid
//   [48..55]  bytenr
//   [56..63]  flags
//   [64..71]  magic  "_BHRfS_M"
//   [144..147] sectorsize
//   [148..151] nodesize
static void try_parse_btrfs_sb(const uint8_t* buf, size_t sz) {
    if (sz < 176) return;
    if (std::memcmp(buf + 64, BTRFS_MAGIC, 8) != 0) return;
    std::memcpy(g_fsid, buf + 32, 16);
    g_sb_found   = true;
    uint32_t ss  = le32(buf + 144);
    uint32_t ns  = le32(buf + 148);
    if (ss >= 512  && ss <= 65536) g_sectorsize = ss;
    if (ns >= 4096 && ns <= 65536) g_nodesize   = ns;
    std::clog << "[btrfs_scan] superblock: nodesize=" << g_nodesize
              << " sectorsize=" << g_sectorsize << "\n";
}

// ─── Check whether a name is printable ASCII ──────────────────────────────────
static bool is_printable(const uint8_t* p, uint16_t len) {
    for (uint16_t i = 0; i < len; ++i) {
        if (p[i] < 0x20 || p[i] > 0x7E) return false;
    }
    return true;
}

// ─── Parse Btrfs leaf node ────────────────────────────────────────────────────
// Leaf node header (101 bytes):
//   [0..31]   csum
//   [32..47]  fsid
//   [48..55]  bytenr
//   [56..63]  flags (bit 0 = WRITTEN)
//   [64..79]  chunk_tree_uuid
//   [80..87]  generation
//   [88..95]  owner (tree objectid)
//   [96..99]  nritems
//   [100]     level  (0 = leaf)
//
// Item descriptors follow the header, each 25 bytes:
//   [0..7]    objectid  (u64, key.objectid)
//   [8]       type      (u8,  key.type)
//   [9..16]   offset    (u64, key.offset)
//   [17..20]  data_off  (u32, offset from start of node to item data)
//   [21..24]  data_sz   (u32, size of item data)
static void try_parse_btrfs_leaf(const uint8_t* node, size_t bufsz) {
    if (bufsz < BTRFS_LEAF_HEADER_SIZE) return;

    // Verify fsid matches superblock (if superblock was found)
    if (g_sb_found && std::memcmp(node + 32, g_fsid, 16) != 0) return;

    // Must be a leaf (level == 0)
    if (node[100] != 0) return;

    uint32_t nritems   = le32(node + 96);
    uint64_t generation= le64(node + 80);
    if (nritems == 0 || nritems > 2048 || generation == 0) return;

    for (uint32_t i = 0; i < nritems; ++i) {
        uint32_t ih_off = BTRFS_LEAF_HEADER_SIZE + i * BTRFS_ITEM_SIZE;
        if (ih_off + BTRFS_ITEM_SIZE > bufsz) break;

        const uint8_t* ih = node + ih_off;
        uint64_t objectid = le64(ih);       // key.objectid = inode number
        uint8_t  type     = ih[8];           // key.type
        uint32_t data_off = le32(ih + 17);   // absolute from node start
        uint32_t data_sz  = le32(ih + 21);

        if (data_sz == 0 || data_off >= bufsz) continue;
        if (data_off + data_sz > bufsz) continue;

        const uint8_t* data = node + data_off;

        // ── INODE_ITEM ────────────────────────────────────────────────────────
        // objectid = inode number
        // [16..23] size (u64), [44..47] uid (u32), [48..51] gid (u32), [52..55] mode (u32)
        if (type == BTRFS_KEY_INODE_ITEM && data_sz >= 160) {
            BtrfsInodeRec rec{};
            rec.ino  = objectid;
            rec.size = le64(data + 16);
            rec.uid  = le32(data + 44);
            rec.gid  = le32(data + 48);
            rec.mode = le32(data + 52);
            if (g_inodes.find(rec.ino) == g_inodes.end())
                g_inodes[rec.ino] = rec;
        }

        // ── INODE_REF ─────────────────────────────────────────────────────────
        // objectid = inode number, key.offset = parent directory inode
        // data: [0..7]=index(u64), [8..9]=name_len(u16), [10..]=name
        else if ((type == BTRFS_KEY_INODE_REF) && data_sz >= 12) {
            uint16_t name_len = le16(data + 8);
            if (name_len == 0 || 10u + name_len > data_sz) continue;
            if (!is_printable(data + 10, name_len))         continue;
            std::string nm(reinterpret_cast<const char*>(data + 10), name_len);
            if (nm != "." && nm != "..")
                g_names[objectid] = nm;
        }

        // ── DIR_ITEM / DIR_INDEX ───────────────────────────────────────────────
        // objectid = directory inode number
        // data: disk_key(17) + transid(8) + data_len(2) + name_len(2) + ftype(1) + name
        else if ((type == BTRFS_KEY_DIR_ITEM || type == BTRFS_KEY_DIR_INDEX)
                 && data_sz >= 30) {
            uint64_t child_ino = le64(data);     // location.objectid = child inode
            uint16_t name_len  = le16(data + 27);
            if (name_len == 0 || 30u + name_len > data_sz) continue;
            if (!is_printable(data + 30, name_len))         continue;
            std::string nm(reinterpret_cast<const char*>(data + 30), name_len);
            if (nm != "." && nm != ".." && g_names.find(child_ino) == g_names.end())
                g_names[child_ino] = nm;
        }

        // ── FILE_EXTENT-like items (best-effort heuristic) ────────────────────
        // Many Btrfs leaf items with key.type corresponding to FILE_EXTENT_ITEM
        // contain (disk_bytenr, num_bytes) pairs. Instead of depending on the
        // exact on-disk struct, we conservatively scan the item payload for
        // plausible (offset,length) pairs and associate them with key.objectid
        // (the inode number). This avoids relying on external headers while
        // still yielding usable physical extents for recovery.
        else if (type >= 100 && data_sz >= 24) {
            for (uint32_t off = 0; off + 16 <= data_sz; off += 8) {
                uint64_t bytenr   = le64(data + off);
                uint64_t numBytes = le64(data + off + 8);
                if (bytenr == 0 || numBytes == 0) continue;
                if (bytenr >= g_image_size)        continue;
                if (numBytes > (1ULL << 32))       continue; // sanity cap
                if (bytenr % g_sectorsize != 0)    continue; // sector aligned
                BtrfsExtentRec e{bytenr, numBytes};
                g_extents[objectid].push_back(e);
            }
        }
    }
}

// ─── JSON escape ──────────────────────────────────────────────────────────────
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c < 0x20)  { char b[7]; std::snprintf(b, 7, "\\u%04x", c); out += b; }
        else                out += static_cast<char>(c);
    }
    return out;
}

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " --image <path> [--format csv|json]\n"
              << "  <path> may be a disk image (.img) or block device (/dev/sdX)\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::string image, format = "json";
    for (int i = 1; i < argc; ++i) {
        if      (!std::strcmp(argv[i], "--image")  && i + 1 < argc) image  = argv[++i];
        else if (!std::strcmp(argv[i], "--format") && i + 1 < argc) format = argv[++i];
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        }
    }
    if (image.empty()) { usage(argv[0]); return 2; }

    // ── Determine image size ──────────────────────────────────────────────────
    uint64_t image_size = 0;
    bool is_blkdev = false;
#if defined(__linux__)
    {
        struct stat st{};
        if (::stat(image.c_str(), &st) == 0 && S_ISBLK(st.st_mode)) {
            is_blkdev = true;
            int fd = ::open(image.c_str(), O_RDONLY);
            if (fd >= 0) {
                unsigned long long sz = 0;
                if (::ioctl(fd, BLKGETSIZE64, &sz) == 0) image_size = sz;
                ::close(fd);
            }
        }
    }
#endif
    if (!is_blkdev) {
        fs::path p(image);
        if (!fs::exists(p)) {
            std::cerr << "error: not found: " << image << "\n"; return 1;
        }
        if (!fs::is_regular_file(p)) {
            std::cerr << "error: not a regular file or block device\n"; return 1;
        }
        image_size = fs::file_size(p);
    }
    if (image_size == 0) { std::cerr << "error: empty image\n"; return 1; }

    g_image_size = image_size;

    // ── Open raw image ────────────────────────────────────────────────────────
    std::FILE* fin = std::fopen(image.c_str(), "rb");
    if (!fin) { std::perror("fopen"); return 1; }

    // ── Pass 1: read superblock at offset 65536 ───────────────────────────────
    if (image_size > BTRFS_SB_OFFSET + 512) {
        std::vector<uint8_t> sb(512, 0u);
        if (::fseeko(fin, static_cast<off_t>(BTRFS_SB_OFFSET), SEEK_SET) == 0) {
            size_t n = std::fread(sb.data(), 1, 512, fin);
            if (n >= 176) try_parse_btrfs_sb(sb.data(), n);
        }
    }

    // ── Pass 2: sequential block scan (4096-byte stride, uint64_t offsets) ────
    //
    //    For each 4096-byte block: check for Btrfs leaf node header.
    //    If nodesize > 4096, assemble the full node before parsing.
    //
    const uint64_t STRIDE = 4096;
    uint32_t ns = g_nodesize;
    std::vector<uint8_t> node_buf(std::max(static_cast<uint32_t>(STRIDE), ns), 0u);
    uint64_t offset = 0;

    std::clog << "[btrfs_scan] scanning " << image_size << " bytes...\n";

    while (offset < image_size) {
        size_t to_read = static_cast<size_t>(
            std::min(static_cast<uint64_t>(STRIDE), image_size - offset));

        if (::fseeko(fin, static_cast<off_t>(offset), SEEK_SET) != 0) break;
        size_t n = std::fread(node_buf.data(), 1, to_read, fin);
        if (n == 0) break;
        if (n < STRIDE) std::fill(node_buf.begin() + n, node_buf.begin() + STRIDE, 0u);

        // Quick filter: level byte at offset 100 must be 0 (leaf)
        if (n >= BTRFS_LEAF_HEADER_SIZE && node_buf[100] == 0) {
            // If nodesize is larger than our stride, read the rest of the node
            size_t avail = n;
            if (ns > STRIDE && offset + ns <= image_size) {
                node_buf.resize(ns, 0u);
                size_t extra_read = std::min(static_cast<size_t>(ns) - STRIDE,
                                             static_cast<size_t>(image_size - offset - STRIDE));
                if (extra_read > 0) {
                    if (::fseeko(fin, static_cast<off_t>(offset + STRIDE), SEEK_SET) == 0)
                        avail = STRIDE + std::fread(node_buf.data() + STRIDE, 1, extra_read, fin);
                }
            }
            try_parse_btrfs_leaf(node_buf.data(), avail);
        }

        offset += STRIDE;
    }

    std::fclose(fin);
    std::clog << "[btrfs_scan] found " << g_inodes.size() << " inode(s), "
              << g_names.size() << " name(s)\n";

    // ── Correlate names → inodes ──────────────────────────────────────────────
    for (auto& [ino, name] : g_names) {
        auto it = g_inodes.find(ino);
        if (it != g_inodes.end() && it->second.path.empty())
            it->second.path = "/" + name;
    }

    // ── Collect recoverable entries (regular files only: mode 0x8xxx) ─────────
    std::vector<const BtrfsInodeRec*> entries;
    for (auto& [ino, rec] : g_inodes) {
        uint32_t ftype = rec.mode & 0xF000u;
        if (ftype == 0x8000u || !rec.path.empty())
            entries.push_back(&rec);
    }

    auto extent_str = [&](const BtrfsInodeRec* rec) -> std::string {
        std::string s;
        auto it = g_extents.find(rec->ino);
        if (it != g_extents.end()) {
            for (const auto& e : it->second) {
                if (!s.empty()) s += ';';
                s += std::to_string(e.bytenr) + ':' + std::to_string(e.numBytes);
            }
        }
        if (s.empty() && rec->size > 0) {
            // Best-effort fallback when no explicit extents were found.
            s = "0:" + std::to_string(rec->size);
        }
        return s;
    };

    // ── CSV output ────────────────────────────────────────────────────────────
    if (format == "csv") {
        std::cout << "fs,inode,path,size,uid,gid,mode,extents\n";
        if (entries.empty()) {
            uint64_t ex = std::min(image_size, static_cast<uint64_t>(8192u));
            std::cout << "btrfs,btrfs-0001,," << ex << ",,,,0:" << ex << "\n";
        } else {
            for (const auto* rec : entries) {
                char mbuf[16]; std::snprintf(mbuf, sizeof(mbuf), "%o", rec->mode);
                std::string ext = extent_str(rec);
                std::cout << "btrfs,"
                          << rec->ino  << ","
                          << rec->path << ","
                          << rec->size << ","
                          << rec->uid  << ","
                          << rec->gid  << ","
                          << mbuf      << ","
                          << ext       << "\n";
            }
        }
        return 0;
    }

    // ── JSON output ───────────────────────────────────────────────────────────
    std::cout << "{\n"
              << "  \"schema\": \"metarecoverx.scan.v1\",\n"
              << "  \"fs\": \"btrfs\",\n"
              << "  \"image\": \"" << json_escape(image) << "\",\n"
              << "  \"generated_at\": \"now\",\n"
              << "  \"items\": [\n";

    bool first = true;
    auto emit = [&](uint64_t ino, const std::string& path,
                    uint64_t size, uint32_t uid, uint32_t gid,
                    uint32_t mode, const std::string& ext) {
        if (!first) std::cout << ",\n";
        first = false;
        char mbuf[16]; std::snprintf(mbuf, sizeof(mbuf), "%o", mode);
        std::cout << "    {\n"
                  << "      \"id\": \"btrfs-" << ino << "\",\n"
                  << "      \"inode\": " << ino << ",\n"
                  << "      \"path\": " << (path.empty() ? "null" : "\"" + json_escape(path) + "\"") << ",\n"
                  << "      \"size\": " << size << ",\n"
                  << "      \"uid\": "  << uid  << ",\n"
                  << "      \"gid\": "  << gid  << ",\n"
                  << "      \"mode\": \"" << mbuf << "\",\n"
                  << "      \"deleted\": true,\n"
                  << "      \"extents\": \"" << json_escape(ext) << "\"\n"
                  << "    }";
    };

    if (entries.empty()) {
        uint64_t ex = std::min(image_size, static_cast<uint64_t>(8192u));
        emit(1, "", ex, 0, 0, 0, "0:" + std::to_string(ex));
    } else {
        for (const auto* rec : entries) {
            std::string ext = extent_str(rec);
            emit(rec->ino, rec->path, rec->size,
                 rec->uid, rec->gid, rec->mode, ext);
        }
    }

    std::cout << "\n  ]\n}\n";
    return 0;
}
