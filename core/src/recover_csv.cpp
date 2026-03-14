// Enable 64-bit file offsets on glibc for large-image (USB) support
#ifndef _FILE_OFFSET_BITS
#  define _FILE_OFFSET_BITS 64
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

#if defined(__linux__)
#  include <fcntl.h>
#  include <linux/fs.h>
#  include <sys/ioctl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

struct PlanItem {
    std::string global_id;
    std::string fs;
    std::string path;
    std::string size;
    std::string uid;
    std::string gid;
    std::string mode;
    std::string extents; // start:length;start:length
};

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " --image <path> --plan <metadata.csv> --out <dir>" << std::endl;
}

static bool parse_csv_line(const std::string& line, PlanItem& out) {
    // metadata.csv: global_id,fs,path,size,uid,gid,mode,extents
    std::vector<std::string> parts;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) parts.push_back(cur);
    if (parts.size() < 8) return false;
    out.global_id = parts[0];
    out.fs = parts[1];
    out.path = parts[2];
    out.size = parts[3];
    out.uid = parts[4];
    out.gid = parts[5];
    out.mode = parts[6];
    out.extents = parts[7];
    return true;
}

int main(int argc, char** argv) {
    std::string image; std::string plan_path; std::string out_dir = "recovered";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--image" && i + 1 < argc) image = argv[++i];
        else if (a == "--plan" && i + 1 < argc) plan_path = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
    }
    if (image.empty() || plan_path.empty()) { usage(argv[0]); return 2; }

    // Accept both regular files and block devices as the image source
    bool is_blkdev = false;
#if defined(__linux__)
    {
        struct stat st{};
        if (::stat(image.c_str(), &st) == 0 && S_ISBLK(st.st_mode))
            is_blkdev = true;
    }
#endif
    if (!is_blkdev) {
        std::filesystem::path ip(image);
        if (!std::filesystem::exists(ip) || !std::filesystem::is_regular_file(ip)) {
            std::cerr << "error: image not found: " << image << "\n"; return 1;
        }
    }

    std::filesystem::create_directories(out_dir);

    std::ifstream pf(plan_path);
    if (!pf) { std::cerr << "error: cannot open plan: " << plan_path << "\n"; return 1; }

    std::vector<PlanItem> items;
    std::string line; bool first = true;
    while (std::getline(pf, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        PlanItem it; if (parse_csv_line(line, it)) items.push_back(it);
    }

    std::FILE* fin = std::fopen(image.c_str(), "rb");
    if (!fin) { std::perror("fopen image"); return 1; }

    size_t recovered = 0;
    size_t fallback_n = 0;
    for (const auto& it : items) {
        // ── Determine output filename ──────────────────────────────────────────
        // Prefer the basename from the recovered path; fallback to file_NNNN.bin
        std::string outname;
        if (!it.path.empty()) {
            auto sep = it.path.rfind('/');
            outname = (sep != std::string::npos) ? it.path.substr(sep + 1) : it.path;
            // Sanitise: strip any embedded path separators or null bytes
            for (char& c : outname) { if (c == '/' || c == '\0') c = '_'; }
        }
        if (outname.empty()) {
            ++fallback_n;
            char seq[24];
            std::snprintf(seq, sizeof(seq), "file_%04zu.bin", fallback_n);
            outname = seq;
        }

        std::filesystem::path outp = std::filesystem::path(out_dir) / outname;
        std::FILE* fout = std::fopen(outp.string().c_str(), "wb");
        if (!fout) { std::perror("fopen out"); continue; }
        std::istringstream es(it.extents);
        std::string pair;
        while (std::getline(es, pair, ';')) {
            if (pair.empty()) continue;
            auto pos = pair.find(':'); if (pos == std::string::npos) continue;
            long long start = std::atoll(pair.substr(0, pos).c_str());
            long long length = std::atoll(pair.substr(pos+1).c_str());
            if (start < 0 || length <= 0) continue;
            // fseeko uses off_t (64-bit when _FILE_OFFSET_BITS=64) — safe for >2GB images
            if (::fseeko(fin, static_cast<off_t>(start), SEEK_SET) != 0) continue;
            const size_t buf_sz = 1<<20; // 1MiB
            std::vector<char> buf(buf_sz);
            long long remaining = length;
            while (remaining > 0) {
                size_t to_read = static_cast<size_t>(remaining < (long long)buf_sz ? remaining : buf_sz);
                size_t n = std::fread(buf.data(), 1, to_read, fin);
                if (n == 0) break;
                std::fwrite(buf.data(), 1, n, fout);
                remaining -= static_cast<long long>(n);
            }
        }
        std::fclose(fout);
        recovered++;
    }

    std::fclose(fin);
    std::cout << "recovered " << recovered << " file(s)\n";
    return 0;
}
