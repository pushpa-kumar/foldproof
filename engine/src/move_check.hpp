#pragma once

#include "rename_check.hpp"

struct MoveResult {
    CheckResult check;
    std::vector<RenameEntry> renames;
};

MoveResult move_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu,
                           const std::vector<std::string>& moved_functions);
