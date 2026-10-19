#pragma once

#include <clang-c/Index.h>

#include <string>
#include <vector>

struct ParseConfig {
    std::vector<std::string> args;
    static ParseConfig defaults(const std::vector<std::string>& include_dirs = {});
};

struct Diag {
    unsigned line;
    unsigned offset;
    std::string message;
};

enum class TokKind { Punct, Keyword, Ident, Literal, Comment };

struct Tok {
    TokKind kind;
    std::string spelling;
    unsigned begin;
    unsigned end;
    unsigned line;
    bool space_before;
    bool line_start;
    bool in_directive;
    bool directive_start;
    bool directive_spacing;
    bool directive_end;
    bool skipped;
    bool macro_arg_sensitive;
    CXCursor cursor;
};

class TranslationUnit {
public:
    TranslationUnit(const std::string& path, const std::string& text, const ParseConfig& config);
    ~TranslationUnit();
    TranslationUnit(const TranslationUnit&) = delete;
    TranslationUnit& operator=(const TranslationUnit&) = delete;

    bool ok() const { return tu_ != nullptr; }
    CXTranslationUnit raw() const { return tu_; }
    CXFile file() const { return file_; }
    const std::string& text() const { return text_; }
    const std::string& path() const { return path_; }
    const std::vector<Tok>& tokens() const { return tokens_; }
    const std::vector<Diag>& errors() const { return errors_; }
    bool has_error_in(unsigned begin, unsigned end) const;
    unsigned line_of(unsigned offset) const;

private:
    void lex();
    void collect_errors();

    std::string path_;
    std::string text_;
    CXIndex index_ = nullptr;
    CXTranslationUnit tu_ = nullptr;
    CXFile file_ = nullptr;
    std::vector<Tok> tokens_;
    std::vector<Diag> errors_;
    std::vector<unsigned> line_starts_;
};

std::string cx_string(CXString str);
unsigned cursor_offset(CXCursor cursor);
