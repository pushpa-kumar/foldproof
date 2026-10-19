#include "clang_tu.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

std::string cx_string(CXString str) {
    const char* raw = clang_getCString(str);
    std::string out = raw ? raw : "";
    clang_disposeString(str);
    return out;
}

unsigned cursor_offset(CXCursor cursor) {
    unsigned offset = 0;
    clang_getSpellingLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
    return offset;
}

static std::string detect_sysroot() {
    if (const char* env = std::getenv("FOLDPROOF_SYSROOT")) return env;
    std::string out;
    if (FILE* pipe = popen("xcrun --show-sdk-path 2>/dev/null", "r")) {
        std::array<char, 512> buf{};
        while (fgets(buf.data(), buf.size(), pipe)) out += buf.data();
        pclose(pipe);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

ParseConfig ParseConfig::defaults(const std::vector<std::string>& include_dirs) {
    static const std::string sysroot = detect_sysroot();
    ParseConfig config;
    config.args = {"-x", "c++", "-std=c++20", "-fsyntax-only", "-Wno-everything"};
    const char* resource_dir = std::getenv("FOLDPROOF_RESOURCE_DIR");
    config.args.push_back("-resource-dir");
    config.args.push_back(resource_dir ? resource_dir : FOLDPROOF_CLANG_RESOURCE_DIR);
    if (!sysroot.empty()) {
        config.args.push_back("-isysroot");
        config.args.push_back(sysroot);
    }
    for (const auto& dir : include_dirs) config.args.push_back("-I" + dir);
    return config;
}

TranslationUnit::TranslationUnit(const std::string& path, const std::string& text, const ParseConfig& config)
    : path_(path), text_(text) {
    line_starts_.push_back(0);
    for (unsigned i = 0; i < text_.size(); ++i) {
        bool bare_cr = text_[i] == '\r' && (i + 1 == text_.size() || text_[i + 1] != '\n');
        if (text_[i] == '\n' || bare_cr) line_starts_.push_back(i + 1);
    }

    index_ = clang_createIndex(0, 0);
    std::vector<const char*> argv;
    for (const auto& arg : config.args) argv.push_back(arg.c_str());
    CXUnsavedFile unsaved{path_.c_str(), text_.c_str(), static_cast<unsigned long>(text_.size())};
    unsigned options = CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_KeepGoing;
    CXErrorCode code = clang_parseTranslationUnit2(index_, path_.c_str(), argv.data(), static_cast<int>(argv.size()),
                                                   &unsaved, 1, options, &tu_);
    if (code != CXError_Success) {
        tu_ = nullptr;
        return;
    }
    file_ = clang_getFile(tu_, path_.c_str());
    if (!file_) {
        clang_disposeTranslationUnit(tu_);
        tu_ = nullptr;
        return;
    }
    collect_errors();
    lex();
}

TranslationUnit::~TranslationUnit() {
    if (tu_) clang_disposeTranslationUnit(tu_);
    if (index_) clang_disposeIndex(index_);
}

unsigned TranslationUnit::line_of(unsigned offset) const {
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    return static_cast<unsigned>(it - line_starts_.begin());
}

bool TranslationUnit::has_error_in(unsigned begin, unsigned end) const {
    return std::any_of(errors_.begin(), errors_.end(),
                       [&](const Diag& diag) { return diag.offset >= begin && diag.offset < end; });
}

void TranslationUnit::collect_errors() {
    unsigned count = clang_getNumDiagnostics(tu_);
    for (unsigned i = 0; i < count; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(tu_, i);
        if (clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error) {
            CXFile file = nullptr;
            unsigned line = 0, offset = 0;
            clang_getSpellingLocation(clang_getDiagnosticLocation(diag), &file, &line, nullptr, &offset);
            bool in_main = file && clang_File_isEqual(file, file_);
            errors_.push_back({in_main ? line : 0, in_main ? offset : 0, cx_string(clang_getDiagnosticSpelling(diag))});
        }
        clang_disposeDiagnostic(diag);
    }
}

static bool is_newline_at(const std::string& text, unsigned i) {
    if (text[i] == '\n') return true;
    return text[i] == '\r' && (i + 1 == text.size() || text[i + 1] != '\n');
}

static bool gap_has_newline(const std::string& text, unsigned from, unsigned to) {
    for (unsigned i = from; i < to; ++i) {
        if (!is_newline_at(text, i)) continue;
        unsigned j = i;
        if (text[i] == '\n' && j > from && text[j - 1] == '\r') --j;
        if (j > from && text[j - 1] == '\\') continue;
        return true;
    }
    return false;
}

static bool has_newline(const std::string& text) {
    return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
}

static bool is_hash(const Tok& tok) { return tok.kind == TokKind::Punct && (tok.spelling == "#" || tok.spelling == "%:"); }

static TokKind map_kind(CXTokenKind kind) {
    switch (kind) {
        case CXToken_Punctuation: return TokKind::Punct;
        case CXToken_Keyword: return TokKind::Keyword;
        case CXToken_Identifier: return TokKind::Ident;
        case CXToken_Literal: return TokKind::Literal;
        default: return TokKind::Comment;
    }
}

static bool macro_like_callee(const Tok& tok) {
    CXCursor cursor = tok.cursor;
    if (clang_getCursorKind(cursor) == CXCursor_MacroExpansion) return true;
    if (tok.kind != TokKind::Ident) return false;
    CXCursor ref = clang_getCursorReferenced(cursor);
    if (clang_Cursor_isNull(ref)) return true;
    CXCursorKind kind = clang_getCursorKind(ref);
    if (kind == CXCursor_OverloadedDeclRef) return clang_getNumOverloadedDecls(ref) == 0;
    return !clang_isDeclaration(kind);
}

static int macro_paren_balance(CXTranslationUnit tu, CXCursor expansion) {
    CXCursor def = clang_getCursorReferenced(expansion);
    if (clang_Cursor_isNull(def)) return 1;
    CXToken* raw = nullptr;
    unsigned count = 0;
    clang_tokenize(tu, clang_getCursorExtent(def), &raw, &count);
    int balance = 0;
    bool in_params = count > 1 && cx_string(clang_getTokenSpelling(tu, raw[1])) == "(" &&
                     clang_getRangeEnd(clang_getTokenExtent(tu, raw[0])).int_data ==
                         clang_getRangeStart(clang_getTokenExtent(tu, raw[1])).int_data;
    for (unsigned i = 1; i < count; ++i) {
        std::string spelling = cx_string(clang_getTokenSpelling(tu, raw[i]));
        if (in_params) {
            if (spelling == ")") in_params = false;
            continue;
        }
        if (spelling == "(") ++balance;
        if (spelling == ")") --balance;
    }
    clang_disposeTokens(tu, raw, count);
    return balance;
}

void TranslationUnit::lex() {
    CXSourceLocation begin = clang_getLocationForOffset(tu_, file_, 0);
    CXSourceLocation end = clang_getLocationForOffset(tu_, file_, static_cast<unsigned>(text_.size()));
    CXToken* raw = nullptr;
    unsigned count = 0;
    clang_tokenize(tu_, clang_getRange(begin, end), &raw, &count);
    std::vector<CXCursor> cursors(count);
    if (count) clang_annotateTokens(tu_, raw, count, cursors.data());

    std::vector<std::pair<unsigned, unsigned>> skipped;
    if (CXSourceRangeList* ranges = clang_getSkippedRanges(tu_, file_)) {
        for (unsigned i = 0; i < ranges->count; ++i) {
            unsigned range_begin = 0, range_end = 0;
            clang_getSpellingLocation(clang_getRangeStart(ranges->ranges[i]), nullptr, nullptr, nullptr, &range_begin);
            clang_getSpellingLocation(clang_getRangeEnd(ranges->ranges[i]), nullptr, nullptr, nullptr, &range_end);
            skipped.emplace_back(range_begin, range_end);
        }
        clang_disposeSourceRangeList(ranges);
    }

    tokens_.reserve(count);
    unsigned prev_end = 0;
    bool in_directive = false;
    bool line_has_code = false;
    bool line_comment_newline = false;
    for (unsigned i = 0; i < count; ++i) {
        CXSourceRange extent = clang_getTokenExtent(tu_, raw[i]);
        unsigned tok_begin = 0, tok_end = 0;
        clang_getSpellingLocation(clang_getRangeStart(extent), nullptr, nullptr, nullptr, &tok_begin);
        clang_getSpellingLocation(clang_getRangeEnd(extent), nullptr, nullptr, nullptr, &tok_end);
        Tok tok{};
        tok.kind = map_kind(clang_getTokenKind(raw[i]));
        tok.spelling = cx_string(clang_getTokenSpelling(tu_, raw[i]));
        tok.begin = tok_begin;
        tok.end = tok_end;
        tok.line = line_of(tok_begin);
        tok.space_before = i > 0 && tok_begin > prev_end;
        tok.line_start = i == 0 || gap_has_newline(text_, prev_end, tok_begin);
        tok.cursor = cursors[i];
        tok.skipped = std::any_of(skipped.begin(), skipped.end(),
                                  [&](const auto& range) { return tok_begin >= range.first && tok_begin < range.second; });
        if (tok.line_start) {
            if (in_directive && !tokens_.empty()) tokens_.back().directive_end = true;
            in_directive = false;
            line_has_code = false;
            line_comment_newline = false;
        }
        if (tok.kind == TokKind::Comment) {
            if (has_newline(tok.spelling)) line_comment_newline = true;
        } else if (!line_has_code) {
            line_has_code = true;
            if (is_hash(tok) && !line_comment_newline) {
                in_directive = true;
                tok.directive_start = true;
            }
        }
        tok.in_directive = in_directive;
        tokens_.push_back(std::move(tok));
        prev_end = tok_end;
    }
    if (in_directive && !tokens_.empty()) tokens_.back().directive_end = true;
    clang_disposeTokens(tu_, raw, count);

    std::vector<int> open_groups;
    int depth = 0;
    bool group_just_opened = false;
    const Tok* prev = nullptr;
    bool prev_closed_macro_group = false;
    for (Tok& tok : tokens_) {
        if (tok.kind == TokKind::Comment || tok.in_directive) continue;
        tok.macro_arg_sensitive = !open_groups.empty() && !group_just_opened;
        group_just_opened = false;
        bool closed_macro_group = false;
        if (clang_getCursorKind(tok.cursor) == CXCursor_MacroExpansion) {
            for (int extra = macro_paren_balance(tu_, tok.cursor); extra > 0; --extra) {
                open_groups.push_back(++depth);
                group_just_opened = true;
            }
        }
        if (tok.kind == TokKind::Punct && tok.spelling == "(") {
            ++depth;
            if (prev && (prev_closed_macro_group || macro_like_callee(*prev))) {
                open_groups.push_back(depth);
                group_just_opened = true;
            }
        } else if (tok.kind == TokKind::Punct && tok.spelling == ")") {
            if (!open_groups.empty() && open_groups.back() == depth) {
                open_groups.pop_back();
                tok.macro_arg_sensitive = false;
                closed_macro_group = true;
            }
            if (depth > 0) --depth;
        }
        prev = &tok;
        prev_closed_macro_group = closed_macro_group;
    }
}
