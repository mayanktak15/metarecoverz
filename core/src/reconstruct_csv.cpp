#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

struct Item {
    std::string fs;      // xfs | btrfs
    std::string inode;   // raw inode number from scanner CSV
    std::string path;    // full path if known
    std::string size;
    std::string uid;
    std::string gid;
    std::string mode;
    std::string extents; // start:length;start:length
};

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " --out metadata.json --csv-out metadata.csv <csv1> [csv2 ...]" << std::endl;
}

static bool parse_csv_line(const std::string& line, Item& out) {
    // Scanner CSV format: fs,inode,path,size,uid,gid,mode,extents
    // (column 1 may be labelled "id" in older scans or "inode" in newer scans;
    //  parsing is positional so both are handled correctly)
    std::vector<std::string> parts;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) parts.push_back(cur);
    if (parts.size() < 8) return false;
    out.fs = parts[0];
    out.inode = parts[1];
    out.path = parts[2];
    out.size = parts[3];
    out.uid = parts[4];
    out.gid = parts[5];
    out.mode = parts[6];
    out.extents = parts[7];
    return true;
}

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    std::string out_json = "artifacts/metadata.json";
    std::string out_csv = "artifacts/metadata.csv";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            out_json = argv[++i];
        } else if (a == "--csv-out" && i + 1 < argc) {
            out_csv = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            usage(argv[0]);
            return 2;
        } else {
            inputs.push_back(a);
        }
    }

    if (inputs.empty()) {
        usage(argv[0]);
        return 2;
    }

    std::vector<Item> items;
    for (const auto& path : inputs) {
        std::ifstream f(path);
        if (!f) { std::cerr << "error: cannot open " << path << "\n"; return 1; }
        std::string line;
        bool first = true;
        while (std::getline(f, line)) {
            if (first) { first = false; continue; } // skip header
            if (line.empty()) continue;
            Item it; if (parse_csv_line(line, it)) items.push_back(it);
        }
    }

    // write CSV summary
    std::filesystem::path(out_csv).parent_path().string();
    std::filesystem::create_directories(std::filesystem::path(out_csv).parent_path());
    {
        std::ofstream oc(out_csv);
        // Required metadata.csv header
        oc << "global_id,fs,path,size,uid,gid,mode,extents\n";
        for (const auto& it : items) {
            // global_id is fs-inode (e.g., xfs-10321)
            std::string gid = it.fs + "-" + it.inode;
            oc << gid << "," << it.fs << "," << it.path << "," << it.size << ","
               << it.uid << "," << it.gid << "," << it.mode << ","
               << it.extents << "\n";
        }
    }

    // write JSON metadata
    std::filesystem::create_directories(std::filesystem::path(out_json).parent_path());
    std::ofstream oj(out_json);
    oj << "{\n";
    oj << "  \"schema\": \"metarecoverx.metadata.v1\",\n";
    oj << "  \"sources\": [";
    for (size_t i = 0; i < inputs.size(); ++i) {
        oj << "\"" << inputs[i] << "\"";
        if (i + 1 < inputs.size()) oj << ",";
    }
    oj << "],\n";
    oj << "  \"generated_at\": \"now\",\n";
    oj << "  \"items\": [\n";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        oj << "    {\n";
        std::string gid = it.fs + "-" + it.inode;
        oj << "      \"global_id\": \"" << gid << "\",\n";
        oj << "      \"fs\": \"" << it.fs << "\",\n";
        oj << "      \"path\": " << (it.path.empty()?"null":"\""+it.path+"\"") << ",\n";
        oj << "      \"size\": " << (it.size.empty()?"0":it.size) << ",\n";
        oj << "      \"timestamps\": {\"ctime\": null, \"mtime\": null, \"atime\": null, \"crtime\": null},\n";
        oj << "      \"uid\": " << (it.uid.empty()?"null":it.uid) << ", \"gid\": " << (it.gid.empty()?"null":it.gid)
           << ", \"mode\": " << (it.mode.empty()?"null":it.mode) << ",\n";
        oj << "      \"extents\": [";
        if (!it.extents.empty()) {
            // extents: start:length;...
            std::istringstream es(it.extents);
            std::string pair; bool first = true;
            while (std::getline(es, pair, ';')) {
                if (pair.empty()) continue;
                auto pos = pair.find(':');
                if (pos == std::string::npos) continue;
                std::string s = pair.substr(0, pos);
                std::string l = pair.substr(pos+1);
                if (!first) oj << ","; first = false;
                oj << "{\"start\": " << s << ", \"length\": " << l << "}";
            }
        }
        oj << "],\n";
        oj << "      \"recovery_plan\": \"" << (it.extents.empty()?"skip":"carve") << "\",\n";
        oj << "      \"notes\": null\n";
        oj << "    }" << (i + 1 < items.size() ? "," : "") << "\n";
    }
    oj << "  ]\n";
    oj << "}\n";
    return 0;
}
