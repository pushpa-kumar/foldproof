#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "analyzer.hpp"
#include "version.hpp"

static bool read_file(const std::string& path, std::string& out) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    if (info.st_size > 0 && !(buf << in.rdbuf())) return false;
    out = buf.str();
    return true;
}

static int usage() {
    std::fprintf(stderr, "usage: foldproof analyze --old <file> --new <file> [--path <repo path>] [-I<dir>...]\n");
    return 2;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("foldproof %s\n", foldproof_version());
        return 0;
    }
    if (argc < 2 || std::strcmp(argv[1], "analyze") != 0) return usage();
    std::string old_path, new_path, virtual_path;
    std::vector<std::string> include_dirs;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--old" && i + 1 < argc) {
            old_path = argv[++i];
        } else if (arg == "--new" && i + 1 < argc) {
            new_path = argv[++i];
        } else if (arg == "--path" && i + 1 < argc) {
            virtual_path = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            include_dirs.push_back(argv[++i]);
        } else if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            include_dirs.push_back(arg.substr(2));
        } else {
            return usage();
        }
    }
    if (old_path.empty() || new_path.empty()) return usage();
    std::string old_text, new_text;
    if (!read_file(old_path, old_text) || !read_file(new_path, new_text)) {
        std::fprintf(stderr, "foldproof: cannot read input files\n");
        return 1;
    }
    if (virtual_path.empty()) virtual_path = new_path;
    Analysis analysis = analyze(virtual_path, old_text, new_text, ParseConfig::defaults(include_dirs));
    std::printf("%s\n", analysis_json(analysis).c_str());
    return 0;
}
