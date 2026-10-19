#include <cstdio>
#include <cstring>

#include "version.hpp"

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("foldproof %s\n", foldproof_version());
        return 0;
    }
    std::fprintf(stderr, "usage: foldproof analyze --old <file> --new <file> [-I<dir>...]\n");
    return 2;
}
