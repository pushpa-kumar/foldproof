#include "line_diff.hpp"

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t newline = text.find('\n', start);
        size_t stop = newline == std::string::npos ? text.size() : newline + 1;
        lines.push_back(text.substr(start, stop - start));
        start = stop;
    }
    return lines;
}

bool line_diff(const std::vector<std::string>& old_lines, const std::vector<std::string>& new_lines,
               std::vector<Hunk>& hunks, size_t max_edits) {
    long n = static_cast<long>(old_lines.size());
    long m = static_cast<long>(new_lines.size());
    long limit = std::min<long>(n + m, static_cast<long>(max_edits));
    long offset = limit + 1;
    std::vector<long> frontier(2 * limit + 3, 0);
    std::vector<std::vector<long>> trace;
    long found = -1;
    for (long d = 0; d <= limit && found < 0; ++d) {
        trace.emplace_back(frontier.begin() + (offset - d), frontier.begin() + (offset + d + 1));
        for (long k = -d; k <= d; k += 2) {
            long x = (k == -d || (k != d && frontier[offset + k - 1] < frontier[offset + k + 1]))
                         ? frontier[offset + k + 1]
                         : frontier[offset + k - 1] + 1;
            long y = x - k;
            while (x < n && y < m && old_lines[x] == new_lines[y]) ++x, ++y;
            frontier[offset + k] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
    }
    if (found < 0) return false;

    std::vector<std::pair<long, long>> matches;
    long x = n, y = m;
    for (long d = found; d > 0; --d) {
        const auto& prev = trace[d];
        auto at = [&](long diag) { return prev[diag + d]; };
        long k = x - y;
        long prev_k = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        long prev_x = at(prev_k);
        long prev_y = prev_x - prev_k;
        while (x > prev_x && y > prev_y && x > 0 && y > 0) {
            --x, --y;
            matches.emplace_back(x, y);
        }
        x = prev_x;
        y = prev_y;
    }
    while (x > 0 && y > 0) {
        --x, --y;
        matches.emplace_back(x, y);
    }

    hunks.clear();
    long old_pos = 0, new_pos = 0;
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        if (it->first > old_pos || it->second > new_pos)
            hunks.push_back({static_cast<size_t>(old_pos), static_cast<size_t>(it->first), static_cast<size_t>(new_pos),
                             static_cast<size_t>(it->second)});
        old_pos = it->first + 1;
        new_pos = it->second + 1;
    }
    if (old_pos < n || new_pos < m)
        hunks.push_back({static_cast<size_t>(old_pos), static_cast<size_t>(n), static_cast<size_t>(new_pos),
                         static_cast<size_t>(m)});
    return true;
}

std::string merge_hunks(const std::vector<std::string>& old_lines, const std::vector<std::string>& new_lines,
                        const std::vector<Hunk>& hunks, const std::vector<bool>& keep_old) {
    std::string out;
    size_t old_pos = 0;
    for (size_t i = 0; i < hunks.size(); ++i) {
        const Hunk& hunk = hunks[i];
        for (; old_pos < hunk.old_begin; ++old_pos) out += old_lines[old_pos];
        if (keep_old[i]) {
            for (size_t line = hunk.old_begin; line < hunk.old_end; ++line) out += old_lines[line];
        } else {
            for (size_t line = hunk.new_begin; line < hunk.new_end; ++line) out += new_lines[line];
        }
        old_pos = hunk.old_end;
    }
    for (; old_pos < old_lines.size(); ++old_pos) out += old_lines[old_pos];
    return out;
}
