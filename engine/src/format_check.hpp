#pragma once

#include "clang_tu.hpp"

#include <map>
#include <set>

struct CheckResult {
    bool ok;
    std::string reason;
    unsigned old_line;
    unsigned new_line;
};

std::set<std::string> line_sensitive_names(const TranslationUnit& tu);
CheckResult compare_line_sensitive(const Tok& old_tok, const Tok& new_tok, const std::set<std::string>& names);
CheckResult compare_tokens(const Tok& old_tok, const Tok& new_tok, bool ignore_ident_spelling);
std::string normalize_comment(const std::string& text);
CheckResult format_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu);
