#include "format_check.hpp"

#include <cctype>

std::string normalize_comment(const std::string& text) {
    std::string body = text;
    if (body.rfind("//", 0) == 0) {
        body = body.substr(2);
    } else if (body.rfind("/*", 0) == 0) {
        body = body.substr(2, body.size() >= 4 ? body.size() - 4 : 0);
    }
    std::string out;
    bool at_line_start = true;
    bool pending_space = false;
    for (char ch : body) {
        if (ch == '\n') {
            at_line_start = true;
            pending_space = !out.empty();
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pending_space = !out.empty();
            continue;
        }
        if (at_line_start && ch == '*') {
            at_line_start = false;
            continue;
        }
        at_line_start = false;
        if (pending_space) out.push_back(' ');
        pending_space = false;
        out.push_back(ch);
    }
    return out;
}

std::set<std::string> line_sensitive_names(const TranslationUnit& tu) {
    std::set<std::string> names{"__LINE__", "__builtin_LINE", "source_location"};
    std::map<std::string, std::vector<std::string>> bodies;
    const auto& toks = tu.tokens();
    for (size_t i = 0; i + 2 < toks.size(); ++i) {
        if (!toks[i].directive_start || toks[i + 1].spelling != "define") continue;
        auto& body = bodies[toks[i + 2].spelling];
        for (size_t j = i + 3; j < toks.size() && toks[j].in_directive && !toks[j].directive_start; ++j) {
            body.push_back(toks[j].spelling);
            if (toks[j].directive_end) break;
        }
    }
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [name, body] : bodies)
            if (!names.count(name))
                for (const auto& spelling : body)
                    if (names.count(spelling)) {
                        names.insert(name);
                        grew = true;
                        break;
                    }
    }
    return names;
}

CheckResult compare_line_sensitive(const Tok& old_tok, const Tok& new_tok, const std::set<std::string>& names) {
    if (old_tok.in_directive || !names.count(old_tok.spelling) || old_tok.line == new_tok.line)
        return {true, "", 0, 0};
    return {false, "'" + old_tok.spelling + "' would report a different line number", old_tok.line, new_tok.line};
}

static CheckResult fail(const std::string& reason, const Tok* old_tok, const Tok* new_tok) {
    return {false, reason, old_tok ? old_tok->line : 0, new_tok ? new_tok->line : 0};
}

CheckResult compare_tokens(const Tok& old_tok, const Tok& new_tok, bool ignore_ident_spelling) {
    if (old_tok.kind != new_tok.kind) return fail("token kind differs", &old_tok, &new_tok);
    if (old_tok.kind == TokKind::Comment) {
        if (normalize_comment(old_tok.spelling) != normalize_comment(new_tok.spelling))
            return fail("comment text changed", &old_tok, &new_tok);
        return {true, "", 0, 0};
    }
    bool skip_spelling = ignore_ident_spelling && old_tok.kind == TokKind::Ident;
    if (!skip_spelling && old_tok.spelling != new_tok.spelling)
        return fail("token '" + old_tok.spelling + "' became '" + new_tok.spelling + "'", &old_tok, &new_tok);
    if (old_tok.in_directive != new_tok.in_directive || old_tok.directive_end != new_tok.directive_end)
        return fail("preprocessor line structure changed", &old_tok, &new_tok);
    if (old_tok.directive_start != new_tok.directive_start)
        return fail("preprocessor line structure changed", &old_tok, &new_tok);
    if (old_tok.skipped || new_tok.skipped) {
        if (old_tok.skipped != new_tok.skipped || old_tok.space_before != new_tok.space_before ||
            old_tok.line_start != new_tok.line_start)
            return fail("layout changed inside an inactive #if block (may be active in another build)", &old_tok, &new_tok);
    }
    bool directive_spacing = (old_tok.directive_spacing || new_tok.directive_spacing) && !old_tok.directive_start;
    if (directive_spacing && old_tok.space_before != new_tok.space_before)
        return fail("whitespace changed inside a preprocessor directive", &old_tok, &new_tok);
    bool macro_spacing = old_tok.macro_arg_sensitive || new_tok.macro_arg_sensitive;
    if (macro_spacing && old_tok.space_before != new_tok.space_before)
        return fail("whitespace changed inside a macro argument (may be stringified)", &old_tok, &new_tok);
    return {true, "", 0, 0};
}

CheckResult format_equivalent(const TranslationUnit& old_tu, const TranslationUnit& new_tu) {
    if (!old_tu.ok() || !new_tu.ok()) return {false, "clang could not parse the file", 0, 0};
    const auto& old_toks = old_tu.tokens();
    const auto& new_toks = new_tu.tokens();
    size_t count = std::min(old_toks.size(), new_toks.size());
    std::set<std::string> sensitive = line_sensitive_names(new_tu);
    for (size_t i = 0; i < count; ++i) {
        CheckResult result = compare_tokens(old_toks[i], new_toks[i], false);
        if (result.ok) result = compare_line_sensitive(old_toks[i], new_toks[i], sensitive);
        if (!result.ok) return result;
    }
    if (old_toks.size() != new_toks.size()) {
        const Tok* old_tok = count < old_toks.size() ? &old_toks[count] : nullptr;
        const Tok* new_tok = count < new_toks.size() ? &new_toks[count] : nullptr;
        return fail("token count differs", old_tok, new_tok);
    }
    return {true, "", 0, 0};
}
