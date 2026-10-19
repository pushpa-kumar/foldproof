#include "rename_check.hpp"

#include <cctype>


struct Binding {
    bool resolved = false;
    bool local = false;
    bool main_file = false;
    unsigned decl_offset = 0;
    std::string usr;
    CXCursor function{};
};

static bool is_function_kind(CXCursorKind kind) {
    switch (kind) {
        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_Constructor:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
        case CXCursor_FunctionTemplate:
            return true;
        default:
            return false;
    }
}

static Binding classify(const TranslationUnit& tu, const Tok& tok) {
    Binding binding;
    CXCursor ref = clang_getCursorReferenced(tok.cursor);
    if (clang_Cursor_isNull(ref) || clang_isInvalid(clang_getCursorKind(ref))) return binding;
    binding.resolved = true;
    binding.usr = cx_string(clang_getCursorUSR(ref));
    CXFile file = nullptr;
    unsigned offset = 0;
    clang_getSpellingLocation(clang_getCursorLocation(ref), &file, nullptr, nullptr, &offset);
    binding.main_file = file && clang_File_isEqual(file, tu.file());
    binding.decl_offset = offset;
    CXCursorKind kind = clang_getCursorKind(ref);
    if (!binding.main_file || (kind != CXCursor_VarDecl && kind != CXCursor_ParmDecl)) return binding;
    CXCursor parent = clang_getCursorSemanticParent(ref);
    if (!is_function_kind(clang_getCursorKind(parent))) return binding;
    binding.local = true;
    binding.function = parent;
    return binding;
}

static std::pair<unsigned, unsigned> extent_offsets(CXCursor cursor) {
    CXSourceRange range = clang_getCursorExtent(cursor);
    unsigned begin = 0, end = 0;
    clang_getSpellingLocation(clang_getRangeStart(range), nullptr, nullptr, nullptr, &begin);
    clang_getSpellingLocation(clang_getRangeEnd(range), nullptr, nullptr, nullptr, &end);
    return {begin, end};
}

static bool is_hash_spelling(const std::string& spelling) {
    return spelling == "#" || spelling == "##" || spelling == "%:" || spelling == "%:%:";
}

MacroTable collect_macros(const TranslationUnit& tu) {
    MacroTable table;
    clang_visitChildren(
        clang_getTranslationUnitCursor(tu.raw()),
        [](CXCursor cursor, CXCursor, CXClientData data) {
            if (clang_getCursorKind(cursor) != CXCursor_MacroDefinition) return CXChildVisit_Continue;
            auto* table = static_cast<MacroTable*>(data);
            CXTranslationUnit raw_tu = clang_Cursor_getTranslationUnit(cursor);
            MacroInfo& info = table->defined[cx_string(clang_getCursorSpelling(cursor))];
            CXToken* raw = nullptr;
            unsigned count = 0;
            clang_tokenize(raw_tu, clang_getCursorExtent(cursor), &raw, &count);
            for (unsigned i = 1; i < count; ++i) {
                std::string spelling = cx_string(clang_getTokenSpelling(raw_tu, raw[i]));
                if (is_hash_spelling(spelling)) info.pastes_or_stringizes = true;
                if (clang_getTokenKind(raw[i]) == CXToken_Identifier)
                    info.identifiers.push_back(spelling);
                if (clang_getTokenKind(raw[i]) == CXToken_Keyword) info.keywords.push_back(spelling);
            }
            clang_disposeTokens(raw_tu, raw, count);
            return CXChildVisit_Continue;
        },
        &table);
    if (CXSourceRangeList* ranges = clang_getAllSkippedRanges(tu.raw())) {
        for (unsigned r = 0; r < ranges->count; ++r) {
            CXToken* raw = nullptr;
            unsigned count = 0;
            clang_tokenize(tu.raw(), ranges->ranges[r], &raw, &count);
            for (unsigned i = 0; i + 2 < count; ++i) {
                if (!is_hash_spelling(cx_string(clang_getTokenSpelling(tu.raw(), raw[i])))) continue;
                if (cx_string(clang_getTokenSpelling(tu.raw(), raw[i + 1])) != "define") continue;
                table.conditional.insert(cx_string(clang_getTokenSpelling(tu.raw(), raw[i + 2])));
            }
            clang_disposeTokens(tu.raw(), raw, count);
        }
        clang_disposeSourceRangeList(ranges);
    }
    return table;
}

static std::string unsafe_macro_reason(const MacroTable& table, const std::string& macro,
                                       const std::set<std::string>& names) {
    std::set<std::string> seen{macro};
    std::vector<std::string> stack{macro};
    while (!stack.empty()) {
        std::string current = stack.back();
        stack.pop_back();
        if (table.conditional.count(current))
            return "macro '" + current + "' has another definition under a different #if branch";
        auto it = table.defined.find(current);
        if (it == table.defined.end()) return "macro '" + current + "' has no visible definition";
        if (it->second.pastes_or_stringizes)
            return "macro '" + current + "' stringizes or pastes tokens, so renaming its arguments changes behaviour";
        for (const auto& ident : it->second.identifiers) {
            if (names.count(ident)) return "macro '" + current + "' mentions '" + ident + "'";
            if ((table.defined.count(ident) || table.conditional.count(ident)) && seen.insert(ident).second)
                stack.push_back(ident);
        }
        for (const auto& keyword : it->second.keywords)
            if (table.defined.count(keyword) && seen.insert(keyword).second) stack.push_back(keyword);
    }
    return "";
}

static CheckResult check_function_body(const TranslationUnit& tu, CXCursor function, const std::set<std::string>& names,
                                       const MacroTable& macros) {
    auto [begin, end] = extent_offsets(function);
    std::string fn_name = cx_string(clang_getCursorSpelling(function));
    unsigned line = tu.line_of(begin);
    if (tu.has_error_in(begin, end))
        return {false, "clang reported an error inside '" + fn_name + "'", line, line};
    for (const Tok& tok : tu.tokens()) {
        if (tok.begin < begin || tok.begin >= end) continue;
        if (tok.in_directive || tok.skipped)
            return {false, "preprocessor directive inside '" + fn_name + "'", tok.line, tok.line};
        bool expansion = clang_getCursorKind(tok.cursor) == CXCursor_MacroExpansion;
        bool macro_name = (tok.kind == TokKind::Ident &&
                           (macros.defined.count(tok.spelling) || macros.conditional.count(tok.spelling))) ||
                          (tok.kind == TokKind::Keyword && macros.defined.count(tok.spelling));
        if (!expansion && !macro_name) continue;
        std::string reason = unsafe_macro_reason(macros, tok.spelling, names);
        if (!reason.empty()) return {false, reason, tok.line, tok.line};
    }
    return {true, "", 0, 0};
}

static std::string strip_offsets(const std::string& usr) {
    std::string out;
    for (size_t i = 0; i < usr.size(); ++i) {
        if (usr[i] == '@' && i + 1 < usr.size() && std::isdigit(static_cast<unsigned char>(usr[i + 1]))) {
            size_t j = i + 1;
            while (j < usr.size() && std::isdigit(static_cast<unsigned char>(usr[j]))) ++j;
            if (j == usr.size() || usr[j] == '@') {
                i = j - 1;
                continue;
            }
        }
        out.push_back(usr[i]);
    }
    return out;
}

static bool same_declaration(const Binding& old_bind, const Binding& new_bind, std::map<unsigned, size_t>& old_decls,
                             std::map<unsigned, size_t>& new_decls) {
    if (old_bind.resolved != new_bind.resolved) return false;
    if (!old_bind.resolved) return true;
    if (old_bind.main_file != new_bind.main_file) return false;
    if (!old_bind.main_file) return old_bind.usr == new_bind.usr;
    size_t old_id = old_decls.emplace(old_bind.decl_offset, old_decls.size()).first->second;
    size_t new_id = new_decls.emplace(new_bind.decl_offset, new_decls.size()).first->second;
    return old_id == new_id && strip_offsets(old_bind.usr) == strip_offsets(new_bind.usr);
}

RenameResult rename_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu,
                               const std::vector<size_t>& old_order) {
    RenameResult result{{true, "", 0, 0}, {}};
    auto fail = [&](const std::string& reason, const Tok* old_tok, const Tok* new_tok) {
        result.check = {false, reason, old_tok ? old_tok->line : 0, new_tok ? new_tok->line : 0};
        result.renames.clear();
        return result;
    };
    if (!old_tu.ok() || !new_tu.ok()) return fail("clang could not parse the file", nullptr, nullptr);
    const auto& old_toks = old_tu.tokens();
    const auto& new_toks = new_tu.tokens();
    if (old_toks.size() != new_toks.size()) return fail("token count differs", nullptr, nullptr);
    if (!old_order.empty() && old_order.size() != old_toks.size())
        return fail("token order does not cover the file", nullptr, nullptr);

    std::map<unsigned, size_t> old_ids, new_ids, old_decls, new_decls;
    std::map<size_t, size_t> entry_of_id;
    std::map<unsigned, std::pair<CXCursor, CXCursor>> touched;
    std::map<unsigned, std::set<std::string>> touched_names;
    for (size_t i = 0; i < old_toks.size(); ++i) {
        const Tok& old_tok = old_toks[old_order.empty() ? i : old_order[i]];
        const Tok& new_tok = new_toks[i];
        if (old_tok.kind == TokKind::Ident && new_tok.kind == TokKind::Ident) {
            Binding old_bind = classify(old_tu, old_tok);
            Binding new_bind = classify(new_tu, new_tok);
            if (old_bind.local != new_bind.local)
                return fail("'" + new_tok.spelling + "' now refers to a different entity", &old_tok, &new_tok);
            if (old_bind.local) {
                size_t old_id = old_ids.emplace(old_bind.decl_offset, old_ids.size()).first->second;
                size_t new_id = new_ids.emplace(new_bind.decl_offset, new_ids.size()).first->second;
                if (old_id != new_id)
                    return fail("'" + new_tok.spelling + "' would capture or shadow another variable", &old_tok, &new_tok);
                if (old_tok.spelling != new_tok.spelling) {
                    auto [it, inserted] = entry_of_id.emplace(old_id, result.renames.size());
                    if (inserted)
                        result.renames.push_back(
                            {cx_string(clang_getCursorSpelling(old_bind.function)), old_tok.spelling, new_tok.spelling, 0, {}});
                    RenameEntry& entry = result.renames[it->second];
                    if (entry.old_name != old_tok.spelling || entry.new_name != new_tok.spelling)
                        return fail("inconsistent rename of '" + old_tok.spelling + "'", &old_tok, &new_tok);
                    ++entry.sites;
                    entry.new_offsets.push_back(new_tok.begin);
                    unsigned key = extent_offsets(old_bind.function).first;
                    touched.emplace(key, std::make_pair(old_bind.function, new_bind.function));
                    touched_names[key].insert(old_tok.spelling);
                    touched_names[key].insert(new_tok.spelling);
                }
            } else {
                if (old_tok.spelling != new_tok.spelling)
                    return fail("'" + old_tok.spelling + "' became '" + new_tok.spelling + "' (not a local variable)",
                                &old_tok, &new_tok);
                if (!same_declaration(old_bind, new_bind, old_decls, new_decls))
                    return fail("'" + new_tok.spelling + "' now refers to a different declaration", &old_tok, &new_tok);
            }
        } else if (old_tok.kind != TokKind::Comment && new_tok.kind != TokKind::Comment &&
                   (clang_isExpression(clang_getCursorKind(old_tok.cursor)) ||
                    clang_isExpression(clang_getCursorKind(new_tok.cursor)))) {
            Binding old_bind = classify(old_tu, old_tok);
            Binding new_bind = classify(new_tu, new_tok);
            bool same = old_bind.local == new_bind.local;
            if (same && old_bind.local)
                same = old_ids.emplace(old_bind.decl_offset, old_ids.size()).first->second ==
                       new_ids.emplace(new_bind.decl_offset, new_ids.size()).first->second;
            else if (same)
                same = same_declaration(old_bind, new_bind, old_decls, new_decls);
            if (!same)
                return fail("'" + new_tok.spelling + "' now resolves to a different operator or function", &old_tok, &new_tok);
        }
        CheckResult layout = compare_tokens(old_tok, new_tok, true);
        if (!layout.ok) {
            result.check = layout;
            result.renames.clear();
            return result;
        }
    }
    MacroTable old_macros, new_macros;
    if (!touched.empty()) {
        old_macros = collect_macros(old_tu);
        new_macros = collect_macros(new_tu);
    }
    for (const auto& [key, functions] : touched) {
        const auto& names = touched_names[key];
        CheckResult body = check_function_body(old_tu, functions.first, names, old_macros);
        if (body.ok) body = check_function_body(new_tu, functions.second, names, new_macros);
        if (!body.ok) {
            result.check = body;
            result.renames.clear();
            return result;
        }
    }
    return result;
}
