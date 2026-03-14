// xfs_scan.cpp — MetaRecoverX XFS filesystem scanner
// Scans raw disk images or block devices for XFS inodes and directory entries.
// Output CSV: fs,inode,path,size,uid,gid,mode,extents
//
// XFS is big-endian on disk.
// Compile: g++ -std=c++17 -O2 -o xfs_scan xfs_scan.cpp

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

// ─── XFS on-disk magic constants (big-endian) ──────────────────────────────
static constexpr uint32_t XFS_SB_MAGIC        = 0x58465342u; // "XFSB"
static constexpr uint16_t XFS_DINODE_MAGIC    = 0x494Eu;     // "IN"
static constexpr uint32_t XFS_DIR2_DATA_MAGIC = 0x58443244u; // "XD2D" (v4)
static constexpr uint32_t XFS_DIR3_DATA_MAGIC = 0x58443342u; // "XD3B" (v5)
static constexpr uint8_t  XFS_DINODE_FMT_EXTENTS = 2;

// ─── Big-endian helpers ─────────────────────────────────────────────────────
static inline uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static inline uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
static inline uint64_t be64(const uint8_t* p) {
    return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4);
}

// ─── XFS packed 128-bit extent record ───────────────────────────────────────
// Layout (bit 127..0):
//   [127]     = unwritten flag
//   [126..73] = startoff  (logical block offset, 54 bits)
//   [72..21]  = startblock (physical disk block, 52 bits)
//   [20..0]   = blockcount (21 bits)
struct XfsExtent {
    uint64_t startblock = 0;
    uint32_t blockcount = 0;
    bool     unwritten  = false;
};

static XfsExtent decode_extent(const uint8_t* p) {
    uint64_t hi = be64(p);
    uint64_t lo = be64(p + 8);
    XfsExtent e;
    e.unwritten  = static_cast<bool>((hi >> 63) & 1u);
    e.startblock = ((hi & 0x1FFull) << 43) | (lo >> 21);
    e.blockcount = static_cast<uint32_t>(lo & ((1u << 21) - 1u));
    return e;
}

// ─── Inode record ────────────────────────────────────────────────────────────
struct InodeRec {
    uint64_t ino         = 0;
    uint16_t mode        = 0; // POSIX mode (including file type)
    uint32_t uid         = 0;
    uint32_t gid         = 0;
    uint64_t size        = 0; // file size in bytes
    uint8_t  version     = 0;
    uint8_t  format      = 0; // data fork format
    uint32_t nextents    = 0;
    uint64_t disk_offset = 0; // where this inode was found on disk
    std::vector<XfsExtent> extents;
    std::string path;         // set from directory scan
};

// ─── Global scan state ───────────────────────────────────────────────────────
static std::map<uint64_t, InodeRec>    g_inodes;     // inode# -> record
static std::map<uint64_t, std::string> g_names;      // inode# -> filename
static uint32_t g_block_size = 4096;
static uint32_t g_inode_size = 512;

// ─── Parse XFS superblock (at byte offset 0 of the filesystem) ───────────────
static void try_parse_superblock(const uint8_t* b) {
    if (be32(b) != XFS_SB_MAGIC) return;
    uint32_t bs = be32(b + 4);          // sb_blocksize
    uint16_t is = be16(b + 104);        // sb_inodesize
    if (bs >= 512  && bs <= 65536 && (bs & (bs - 1)) == 0) g_block_size = bs;
    if (is >= 256  && is <= 2048  && (is & (is - 1)) == 0) g_inode_size = is;
    std::clog << "[xfs_scan] superblock: block_size=" << g_block_size
              << " inode_size=" << g_inode_size << "\n";
}

// ─── Parse one candidate inode slot ──────────────────────────────────────────
// XFS dinode_core layout (big-endian):
//   [0..1]   di_magic (0x494E = "IN")
//   [2..3]   di_mode  (POSIX mode)
//   [4]      di_version
//   [5]      di_format (1=local,2=extents,3=btree)
//   [8..11]  di_uid
//   [12..15] di_gid
//   [56..63] di_size  (file size in bytes)
//   [76..79] di_nextents
//   v3 only:
//   [152..159] di_ino (embedded inode number)
static void try_parse_inode(const uint8_t* p, uint64_t disk_off) {
    if (be16(p) != XFS_DINODE_MAGIC) return;

    uint16_t mode     = be16(p + 2);
    uint8_t  version  = p[4];
    uint8_t  format   = p[5];
    uint32_t uid      = be32(p + 8);
    uint32_t gid      = be32(p + 12);
    uint64_t size     = be64(p + 56);
    uint32_t nextents = be32(p + 76);

    // Sanity: file type nibble must be valid
    uint16_t ftype = mode & 0xF000u;
    if (ftype == 0) return;

    InodeRec rec;
    rec.mode        = mode;
    rec.uid         = uid;
    rec.gid         = gid;
    rec.size        = size;
    rec.version     = version;
    rec.format      = format;
    rec.nextents    = nextents;
    rec.disk_offset = disk_off;

    // v3 inodes embed the inode number at byte offset 152
    if (version >= 3 && g_inode_size >= 176) {
        uint64_t emb = be64(p + 152);
        if (emb > 0) rec.ino = emb;
    }
    if (rec.ino == 0) rec.ino = disk_off / static_cast<uint64_t>(g_inode_size);

    // Parse extent records from the data fork (format == 2)
    uint32_t fork_off = (version >= 3) ? 176u : 96u;
    if (format == XFS_DINODE_FMT_EXTENTS && nextents > 0 && nextents < 65536u) {
        for (uint32_t i = 0; i < nextents && i < 128u; ++i) {
            uint32_t eoff = fork_off + i * 16u;
            if (eoff + 16u > static_cast<uint32_t>(g_inode_size)) break;
            rec.extents.push_back(decode_extent(p + eoff));
        }
    }

    g_inodes[rec.ino] = std::move(rec);
}

// ─── Parse XFS directory data block ──────────────────────────────────────────
// XFS dir2_data_entry layout:
//   [0..7]               inumber  (be64)
//   [8]                  namelen  (u8)
//   [9..9+namelen-1]     name     (char[namelen], not null-terminated)
//   [9+namelen]          ftype    (u8, v5 only)
//   (8-byte align)
//   [last 2]             tag      (be16)
static void try_parse_dir_block(const uint8_t* blk, size_t sz) {
    uint32_t magic = be32(blk);
    uint32_t hdr;
    bool     v5;
    if      (magic == XFS_DIR2_DATA_MAGIC) { hdr = 16u; v5 = false; }
    else if (magic == XFS_DIR3_DATA_MAGIC) { hdr = 64u; v5 = true;  }
    else return;

    size_t pos = hdr;
    while (pos + 10 < sz) {
        const uint8_t* ep = blk + pos;

        // Unused entry: marked by freetag 0xFFFF at start
        if (ep[0] == 0xFF && ep[1] == 0xFF) {
            if (pos + 4 > sz) break;
            uint16_t len = be16(ep + 2);
            if (len < 8 || pos + len > sz) break;
            pos += len;
            continue;
        }

        uint64_t ino     = be64(ep);
        uint8_t  namelen = ep[8];

        if (namelen == 0 || namelen > 255)      { pos++; continue; }
        if (pos + 9u + namelen > sz)            { pos++; continue; }

        // Validate: name must be printable ASCII
        const char* np = reinterpret_cast<const char*>(ep + 9);
        bool valid = true;
        for (uint8_t k = 0; k < namelen; ++k) {
            unsigned char c = static_cast<unsigned char>(np[k]);
            if (c < 0x20 || c > 0x7E) { valid = false; break; }
        }
        if (!valid) { pos++; continue; }

        std::string nm(np, namelen);
        if (nm != "." && nm != "..") {
            if (g_names.find(ino) == g_names.end())
                g_names[ino] = nm;
        }

        // Advance: 8(ino)+1(namelen)+namelen+1(ftype if v5), 8-byte aligned, +2(tag)
        size_t raw = 8u + 1u + namelen + (v5 ? 1u : 0u);
        size_t step = ((raw + 7u) & ~7u) + 2u;
        if (step < 10u) step = 10u;
        pos += step;
    }
}

// ─── JSON escape ────────────────────────────────────────────────────────────
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
              << "  <path> may be a disk image or block device (/dev/sdX)\n";
}

// ─── Main ────────────────────────────────────────────────────────────────────
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

    // ── Open raw image ────────────────────────────────────────────────────────
    std::FILE* fin = std::fopen(image.c_str(), "rb");
    if (!fin) { std::perror("fopen"); return 1; }

    // ── Sequential block scan (4096-byte steps, uint64_t offsets) ─────────────
    //
    //    Reads every 4096-byte block; within each block checks inode-sized
    //    sub-slots for XFS inode magic and checks the full block for XFS
    //    directory data block magic.
    //
    const uint64_t STRIDE = 4096;
    std::vector<uint8_t> buf(STRIDE, 0u);
    uint64_t offset = 0;

    std::clog << "[xfs_scan] scanning " << image_size << " bytes...\n";

    while (offset < image_size) {
        size_t to_read = static_cast<size_t>(
            std::min(STRIDE, image_size - offset));

        if (::fseeko(fin, static_cast<off_t>(offset), SEEK_SET) != 0) break;
        size_t n = std::fread(buf.data(), 1, to_read, fin);
        if (n == 0) break;
        if (n < STRIDE) std::fill(buf.begin() + n, buf.end(), 0u);

        // Parse superblock at image start
        if (offset == 0) try_parse_superblock(buf.data());

        // Scan inode-sized sub-slots
        uint32_t islot = (g_inode_size >= 64) ? g_inode_size : 512u;
        for (uint32_t ioff = 0; ioff + islot <= STRIDE; ioff += islot)
            try_parse_inode(buf.data() + ioff, offset + ioff);

        // Check for directory data block
        try_parse_dir_block(buf.data(), n);

        offset += STRIDE;
    }

    std::fclose(fin);
    std::clog << "[xfs_scan] found " << g_inodes.size() << " inode(s), "
              << g_names.size() << " name(s)\n";

    // ── Correlate names → inodes ──────────────────────────────────────────────
    for (auto& [ino, name] : g_names) {
        auto it = g_inodes.find(ino);
        if (it != g_inodes.end() && it->second.path.empty())
            it->second.path = "/" + name;
    }

    // ── Collect recoverable entries ───────────────────────────────────────────
    // Keep regular files (mode 0x8000) and anything with a filename
    std::vector<const InodeRec*> entries;
    for (auto& [ino, rec] : g_inodes) {
        uint16_t ftype = rec.mode & 0xF000u;
        if (ftype == 0x8000u || !rec.path.empty())
            entries.push_back(&rec);
    }

    // ── Build extent string ───────────────────────────────────────────────────
    auto extent_str = [&](const InodeRec* rec) -> std::string {
        std::string s;
        for (const auto& e : rec->extents) {
            uint64_t bstart = e.startblock * static_cast<uint64_t>(g_block_size);
            uint64_t blen   = static_cast<uint64_t>(e.blockcount) * g_block_size;
            if (!s.empty()) s += ';';
            s += std::to_string(bstart) + ':' + std::to_string(blen);
        }
        // Fallback: use the inode's disk offset as extent hint
        if (s.empty() && rec->size > 0)
            s = std::to_string(rec->disk_offset) + ':' + std::to_string(rec->size);
        return s;
    };

    // ── CSV output ────────────────────────────────────────────────────────────
    if (format == "csv") {
        std::cout << "fs,inode,path,size,uid,gid,mode,extents\n";
        if (entries.empty()) {
            uint64_t ex = std::min(image_size, static_cast<uint64_t>(4096u));
            std::cout << "xfs,xfs-0001,," << ex << ",,,,0:" << ex << "\n";
        } else {
            for (const auto* rec : entries) {
                char mbuf[16]; std::snprintf(mbuf, sizeof(mbuf), "%o", rec->mode);
                std::cout << "xfs,"
                          << rec->ino  << ","
                          << rec->path << ","
                          << rec->size << ","
                          << rec->uid  << ","
                          << rec->gid  << ","
                          << mbuf      << ","
                          << extent_str(rec) << "\n";
            }
        }
        return 0;
    }

    // ── JSON output ───────────────────────────────────────────────────────────
    std::cout << "{\n"
              << "  \"schema\": \"metarecoverx.scan.v1\",\n"
              << "  \"fs\": \"xfs\",\n"
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
                  << "      \"id\": \"xfs-" << ino << "\",\n"
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
        uint64_t ex = std::min(image_size, static_cast<uint64_t>(4096u));
        emit(1, "", ex, 0, 0, 0, "0:" + std::to_string(ex));
    } else {
        for (const auto* rec : entries)
            emit(rec->ino, rec->path, rec->size,
                 rec->uid, rec->gid, rec->mode, extent_str(rec));
    }

    std::cout << "\n  ]\n}\n";
    return 0;
}
