#include <string>
#include <vector>

static int clamp_index(int i, int n) { if (i<0) return 0; if (i>=n) return n-1; return i; }

int sum_window(const std::vector<int>& v, int lo, int hi) {
    int acc = 0;
    for (int k = clamp_index(lo, (int)v.size()); k < clamp_index(hi, (int)v.size()); ++k) acc += v[k];
    return acc;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

int count_positive(const std::vector<int>& v) {
    int c = 0;
    for (int x : v) if (x > 0) ++c;
    return c;
}
