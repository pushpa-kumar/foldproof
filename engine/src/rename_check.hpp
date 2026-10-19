#pragma once

#include "format_check.hpp"

#include <map>
#include <set>

struct MacroInfo {
    std::vector<std::string> identifiers;
    bool pastes_or_stringizes = false;
};

struct MacroTable {
    std::map<std::string, MacroInfo> defined;
    std::set<std::string> conditional;
};

MacroTable collect_macros(const TranslationUnit& tu);

struct RenameEntry {
    std::string function;
    std::string old_name;
    std::string new_name;
    unsigned sites;
};

struct RenameResult {
    CheckResult check;
    std::vector<RenameEntry> renames;
};

RenameResult rename_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu,
                               const std::vector<size_t>& old_order = {});
