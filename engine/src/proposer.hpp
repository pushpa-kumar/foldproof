#pragma once

#include "line_diff.hpp"

enum class FoldKind { Format, Rename, Move };

struct RenamePair {
    std::string function;
    std::string from;
    std::string to;
};

struct Proposal {
    FoldKind kind;
    std::vector<size_t> hunks;
    std::vector<RenamePair> renames;
    std::vector<std::string> moved;
};

std::vector<Hunk> refine_hunks(const std::string& old_text, const std::string& new_text, const std::vector<Hunk>& hunks);
std::vector<Proposal> propose_folds(const std::string& old_text, const std::string& new_text,
                                    const std::vector<Hunk>& hunks);
const char* fold_kind_name(FoldKind kind);
