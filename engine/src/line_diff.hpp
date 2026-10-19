#pragma once

#include <string>
#include <vector>

struct Hunk {
    size_t old_begin;
    size_t old_end;
    size_t new_begin;
    size_t new_end;
};

std::vector<std::string> split_lines(const std::string& text);
bool line_diff(const std::vector<std::string>& old_lines, const std::vector<std::string>& new_lines,
               std::vector<Hunk>& hunks, size_t max_edits = 4000);
std::string merge_hunks(const std::vector<std::string>& old_lines, const std::vector<std::string>& new_lines,
                        const std::vector<Hunk>& hunks, const std::vector<bool>& keep_old);
