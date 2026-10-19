#include <string>
#include <vector>

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

static int clamp_index(int i, int n) {
    if (i < 0) return 0;
    if (i >= n) return n - 1;
    return i;
}

int sum_window(const std::vector<int>& values, int lo, int hi) {
    int total = 0;
    for (int k = clamp_index(lo, (int)values.size()); k < clamp_index(hi, (int)values.size()); ++k) total += values[k];
    return total;
}

int count_positive(const std::vector<int>& v) {
    int c = 0;
    for (int x : v) if (x >= 0) ++c;
    return c;
}
