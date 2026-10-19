#include "move_check.hpp"

#include <algorithm>

struct Segment {
    size_t first = 0;
    size_t last = 0;
    std::string name;
    CXCursorKind kind = CXCursor_UnexposedDecl;
    unsigned decl_begin = 0;
    unsigned decl_end = 0;
};

struct DeclLeaf {
    std::string name;
    CXCursorKind kind;
    unsigned begin;
    unsigned end;
};

static std::pair<unsigned, unsigned> extent_of(CXCursor cursor) {
    CXSourceRange range = clang_getCursorExtent(cursor);
    unsigned begin = 0, end = 0;
    clang_getSpellingLocation(clang_getRangeStart(range), nullptr, nullptr, nullptr, &begin);
    clang_getSpellingLocation(clang_getRangeEnd(range), nullptr, nullptr, nullptr, &end);
    return {begin, end};
}

static void collect_leaves(CXCursor parent, const TranslationUnit& tu, std::vector<DeclLeaf>& leaves) {
    struct Ctx {
        const TranslationUnit* tu;
        std::vector<DeclLeaf>* leaves;
    } ctx{&tu, &leaves};
    clang_visitChildren(
        parent,
        [](CXCursor cursor, CXCursor, CXClientData data) {
            auto* ctx = static_cast<Ctx*>(data);
            CXFile file = nullptr;
            clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, nullptr, nullptr, nullptr);
            if (!file || !clang_File_isEqual(file, ctx->tu->file())) return CXChildVisit_Continue;
            CXCursorKind kind = clang_getCursorKind(cursor);
            if (!clang_isDeclaration(kind)) return CXChildVisit_Continue;
            auto [begin, end] = extent_of(cursor);
            if (kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec) {
                const auto& toks = ctx->tu->tokens();
                auto brace = std::find_if(toks.begin(), toks.end(), [&](const Tok& tok) {
                    return tok.begin >= begin && tok.begin < end && tok.spelling == "{";
                });
                if (brace == toks.end() || end == 0) return CXChildVisit_Continue;
                ctx->leaves->push_back({"", kind, begin, brace->end});
                collect_leaves(cursor, *ctx->tu, *ctx->leaves);
                ctx->leaves->push_back({"", kind, end - 1, end});
                return CXChildVisit_Continue;
            }
            ctx->leaves->push_back({cx_string(clang_getCursorSpelling(cursor)), kind, begin, end});
            return CXChildVisit_Continue;
        },
        &ctx);
}

static std::vector<Segment> segment_tokens(const TranslationUnit& tu) {
    std::vector<DeclLeaf> leaves;
    collect_leaves(clang_getTranslationUnitCursor(tu.raw()), tu, leaves);
    std::sort(leaves.begin(), leaves.end(), [](const DeclLeaf& a, const DeclLeaf& b) { return a.begin < b.begin; });
    const auto& toks = tu.tokens();
    std::vector<Segment> segments;
    size_t next = 0;
    for (const DeclLeaf& leaf : leaves) {
        if (!segments.empty() && leaf.begin < segments.back().decl_end) continue;
        size_t first_own = next;
        while (first_own < toks.size() && toks[first_own].begin < leaf.begin) ++first_own;
        size_t glue_end = next;
        for (size_t i = next; i < first_own; ++i)
            if (toks[i].in_directive || toks[i].skipped) glue_end = i + 1;
        if (glue_end > next) {
            segments.push_back({next, glue_end, "", CXCursor_PreprocessingDirective, 0, 0});
            next = glue_end;
        }
        size_t last = next;
        while (last < toks.size() && toks[last].begin < leaf.end) ++last;
        if (last == next) continue;
        if (last < toks.size() && toks[last].spelling == ";" && !toks[last].in_directive) ++last;
        while (last < toks.size() && toks[last].kind == TokKind::Comment && !toks[last].line_start) ++last;
        segments.push_back({next, last, leaf.name, leaf.kind, leaf.begin, leaf.end});
        next = last;
    }
    if (next < toks.size()) segments.push_back({next, toks.size(), "", CXCursor_UnexposedDecl, 0, 0});
    return segments;
}

static const Segment* find_function(const std::vector<Segment>& segments, const std::string& name, int& matches) {
    const Segment* found = nullptr;
    matches = 0;
    for (const Segment& seg : segments) {
        if (seg.name != name) continue;
        ++matches;
        found = &seg;
    }
    return found;
}

static std::string check_moved_segment(const TranslationUnit& tu, const Segment& seg) {
    if (seg.kind != CXCursor_FunctionDecl) return "only non-template free functions can be folded as moves";
    for (size_t i = seg.first; i < seg.last; ++i) {
        const Tok& tok = tu.tokens()[i];
        if (tok.in_directive || tok.skipped) return "moved code contains a preprocessor directive";
        bool trailing_semicolon = i + 1 == seg.last && tok.spelling == ";";
        if (tok.kind != TokKind::Comment && !trailing_semicolon &&
            (tok.begin < seg.decl_begin || tok.begin >= seg.decl_end))
            return "moved code carries tokens outside the function";
        if (tok.spelling == "__LINE__" || tok.spelling == "__builtin_LINE" || tok.spelling == "source_location")
            return "moved code observes its own line number";
    }
    if (tu.has_error_in(seg.decl_begin, seg.decl_end)) return "clang reported an error inside the moved function";
    return "";
}

static bool uses_counter(const TranslationUnit& tu) {
    for (const Tok& tok : tu.tokens())
        if (tok.spelling == "__COUNTER__") return true;
    for (const auto& [name, info] : collect_macros(tu).defined)
        for (const auto& ident : info.identifiers)
            if (ident == "__COUNTER__") return true;
    return false;
}

static size_t directives_before(const std::vector<Segment>& segments, const Segment* target) {
    size_t count = 0;
    for (const Segment& seg : segments) {
        if (&seg == target) break;
        if (seg.kind == CXCursor_PreprocessingDirective) ++count;
    }
    return count;
}

static std::vector<std::string> error_messages(const TranslationUnit& tu) {
    std::vector<std::string> messages;
    for (const Diag& diag : tu.errors()) messages.push_back(diag.message);
    std::sort(messages.begin(), messages.end());
    return messages;
}

static bool all_resolved(const TranslationUnit& tu, const std::string& name) {
    for (const Tok& tok : tu.tokens()) {
        if (tok.kind != TokKind::Ident || tok.spelling != name) continue;
        CXCursor ref = clang_getCursorReferenced(tok.cursor);
        if (clang_Cursor_isNull(ref) || clang_isInvalid(clang_getCursorKind(ref))) return false;
        if (clang_getCursorKind(ref) == CXCursor_OverloadedDeclRef) return false;
    }
    return true;
}

MoveResult move_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu,
                           const std::vector<std::string>& moved_functions) {
    auto fail = [](const std::string& reason, unsigned old_line, unsigned new_line) {
        return MoveResult{{false, reason, old_line, new_line}, {}};
    };
    if (!old_tu.ok() || !new_tu.ok()) return fail("clang could not parse the file", 0, 0);
    if (error_messages(old_tu) != error_messages(new_tu)) return fail("compiler errors differ between the reviewed and the new file", 0, 0);
    if (!moved_functions.empty() && (uses_counter(old_tu) || uses_counter(new_tu)))
        return fail("__COUNTER__ values depend on declaration order", 0, 0);

    std::vector<Segment> old_segs = segment_tokens(old_tu);
    std::vector<Segment> new_segs = segment_tokens(new_tu);
    std::vector<const Segment*> old_moved, new_moved;
    for (const auto& name : moved_functions) {
        int old_matches = 0, new_matches = 0;
        const Segment* old_seg = find_function(old_segs, name, old_matches);
        const Segment* new_seg = find_function(new_segs, name, new_matches);
        if (old_matches != 1 || new_matches != 1) return fail("'" + name + "' is missing or overloaded", 0, 0);
        unsigned old_line = old_tu.line_of(old_seg->decl_begin);
        unsigned new_line = new_tu.line_of(new_seg->decl_begin);
        std::string reason = check_moved_segment(old_tu, *old_seg);
        if (reason.empty()) reason = check_moved_segment(new_tu, *new_seg);
        if (!reason.empty()) return fail(reason, old_line, new_line);
        if (!all_resolved(old_tu, name) || !all_resolved(new_tu, name))
            return fail("a use of '" + name + "' could not be resolved, so overload choice is unverified", old_line,
                        new_line);
        if (directives_before(old_segs, old_seg) != directives_before(new_segs, new_seg))
            return fail("move crosses a preprocessor directive (#pragma or #define regions may differ)", old_line,
                        new_line);
        old_moved.push_back(old_seg);
        new_moved.push_back(new_seg);
    }

    std::vector<const Segment*> old_rest;
    for (const Segment& seg : old_segs)
        if (std::find(old_moved.begin(), old_moved.end(), &seg) == old_moved.end()) old_rest.push_back(&seg);
    size_t rest_index = 0;
    std::vector<size_t> order;
    for (const Segment& seg : new_segs) {
        const Segment* source = nullptr;
        auto moved = std::find(new_moved.begin(), new_moved.end(), &seg);
        if (moved != new_moved.end()) {
            source = old_moved[moved - new_moved.begin()];
        } else if (rest_index < old_rest.size()) {
            source = old_rest[rest_index++];
        } else {
            return fail("declarations do not line up after the move", 0, new_tu.line_of(seg.decl_begin));
        }
        for (size_t i = source->first; i < source->last; ++i) order.push_back(i);
    }
    if (rest_index != old_rest.size() || order.size() != old_tu.tokens().size())
        return fail("declarations do not line up after the move", 0, 0);

    RenameResult renamed = rename_equivalent(old_tu, new_tu, order);
    return {renamed.check, renamed.renames};
}
