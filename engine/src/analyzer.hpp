#pragma once

#include "move_check.hpp"
#include "proposer.hpp"

struct Fold {
    FoldKind kind;
    std::vector<size_t> hunks;
    std::vector<RenameEntry> renames;
    std::vector<std::string> moved;
};

struct Refusal {
    FoldKind proposed;
    std::vector<size_t> hunks;
    std::string reason;
    unsigned new_line;
};

struct Analysis {
    std::string path;
    std::vector<Hunk> hunks;
    std::vector<int> fold_of_hunk;
    std::vector<Fold> folds;
    std::vector<Refusal> refusals;
    size_t changed_lines = 0;
    size_t residual_lines = 0;
    std::string fallback;
};

Analysis analyze(const std::string& path, const std::string& old_text, const std::string& new_text,
                 const ParseConfig& config);
std::string analysis_json(const Analysis& analysis);
