#include "analyzer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>

static size_t hunk_lines(const Hunk& hunk) { return (hunk.old_end - hunk.old_begin) + (hunk.new_end - hunk.new_begin); }

static bool entry_in_hunk(const RenameEntry& entry, const Hunk& hunk, const std::vector<size_t>& line_starts) {
    for (unsigned offset : entry.new_offsets) {
        size_t line = static_cast<size_t>(std::upper_bound(line_starts.begin(), line_starts.end(), offset) -
                                          line_starts.begin()) - 1;
        if (line >= hunk.new_begin && line < hunk.new_end) return true;
    }
    return false;
}

static MoveResult check_folds(const std::string& path, const std::vector<std::string>& old_lines,
                              const std::vector<std::string>& new_lines, const std::vector<Hunk>& hunks,
                              const std::vector<const Proposal*>& proposals, const TranslationUnit& new_tu,
                              const ParseConfig& config) {
    std::vector<bool> keep_old(hunks.size(), false);
    std::vector<std::string> moved;
    for (const Proposal* proposal : proposals) {
        for (size_t h : proposal->hunks) keep_old[h] = true;
        moved.insert(moved.end(), proposal->moved.begin(), proposal->moved.end());
    }
    TranslationUnit reviewed(path, merge_hunks(old_lines, new_lines, hunks, keep_old), config);
    return move_equivalent(reviewed, new_tu, moved);
}

Analysis analyze(const std::string& path, const std::string& old_text, const std::string& new_text,
                 const ParseConfig& config) {
    Analysis analysis;
    analysis.path = path;
    auto old_lines = split_lines(old_text);
    auto new_lines = split_lines(new_text);
    bool diffed = whitespace_aware_diff(old_lines, new_lines, analysis.hunks);
    if (!diffed) {
        analysis.hunks = {{0, old_lines.size(), 0, new_lines.size()}};
        analysis.fallback = "diff too large to analyse";
    }
    analysis.fold_of_hunk.assign(analysis.hunks.size(), -1);
    for (const Hunk& hunk : analysis.hunks) analysis.changed_lines += hunk_lines(hunk);
    analysis.residual_lines = analysis.changed_lines;
    if (!analysis.fallback.empty() || analysis.hunks.empty()) return analysis;
    if (old_lines.size() > 5000 || new_lines.size() > 5000) {
        analysis.fallback = "file larger than 5000 lines";
        return analysis;
    }

    TranslationUnit new_tu(path, new_text, config);
    if (!new_tu.ok()) {
        analysis.fallback = "clang could not parse the new file";
        return analysis;
    }
    analysis.hunks = refine_hunks(old_text, new_text, analysis.hunks);
    analysis.fold_of_hunk.assign(analysis.hunks.size(), -1);
    std::vector<Proposal> proposals = propose_folds(old_text, new_text, analysis.hunks);
    std::vector<const Proposal*> chosen;
    std::vector<size_t> accepted;
    std::vector<bool> taken(proposals.size(), false);
    std::vector<Refusal> refusal_of(proposals.size());
    for (size_t i = 0; i < proposals.size(); ++i)
        refusal_of[i] = {proposals[i].kind, proposals[i].hunks, "time budget ran out before this fold was checked", 0};
    MoveResult final_result{{true, "", 0, 0}, {}};
    auto started = std::chrono::steady_clock::now();
    auto out_of_time = [&] { return std::chrono::steady_clock::now() - started > std::chrono::seconds(30); };
    std::function<bool(const std::vector<size_t>&)> attempt = [&](const std::vector<size_t>& group) {
        if (group.empty() || out_of_time()) return false;
        std::vector<const Proposal*> trial = chosen;
        for (size_t i : group) trial.push_back(&proposals[i]);
        MoveResult result = check_folds(path, old_lines, new_lines, analysis.hunks, trial, new_tu, config);
        if (result.check.ok) {
            chosen = trial;
            final_result = result;
            for (size_t i : group) {
                taken[i] = true;
                accepted.push_back(i);
            }
            return true;
        }
        if (group.size() == 1) {
            refusal_of[group[0]].reason = result.check.reason;
            refusal_of[group[0]].new_line = result.check.new_line;
            return false;
        }
        std::vector<size_t> left(group.begin(), group.begin() + group.size() / 2);
        std::vector<size_t> right(group.begin() + group.size() / 2, group.end());
        bool any = attempt(left);
        return attempt(right) || any;
    };
    std::vector<size_t> all(proposals.size());
    std::iota(all.begin(), all.end(), 0);
    attempt(all);
    for (bool progress = !accepted.empty(); progress && !out_of_time();) {
        progress = false;
        for (size_t i = 0; i < proposals.size(); ++i)
            if (!taken[i] && attempt({i})) progress = true;
    }
    for (size_t i = 0; i < proposals.size(); ++i)
        if (!taken[i]) analysis.refusals.push_back(refusal_of[i]);
    std::vector<size_t> line_starts{0};
    for (size_t i = 0; i < new_text.size(); ++i)
        if (new_text[i] == '\n') line_starts.push_back(i + 1);

    for (size_t i : accepted) {
        const Proposal& proposal = proposals[i];
        std::vector<RenameEntry> renames;
        for (const RenameEntry& entry : final_result.renames)
            for (size_t h : proposal.hunks)
                if (entry_in_hunk(entry, analysis.hunks[h], line_starts)) {
                    renames.push_back(entry);
                    break;
                }
        FoldKind kind = !proposal.moved.empty() ? FoldKind::Move : !renames.empty() ? FoldKind::Rename : FoldKind::Format;
        int id = static_cast<int>(analysis.folds.size());
        std::vector<size_t> hunks = proposal.hunks;
        std::sort(hunks.begin(), hunks.end());
        analysis.folds.push_back({kind, hunks, renames, proposal.moved});
        for (size_t h : proposal.hunks) {
            analysis.fold_of_hunk[h] = id;
            analysis.residual_lines -= hunk_lines(analysis.hunks[h]);
        }
    }
    return analysis;
}

static std::string quote(const std::string& text) {
    std::string out = "\"";
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 0x80) {
            size_t len = (ch & 0xE0) == 0xC0 ? 2 : (ch & 0xF0) == 0xE0 ? 3 : (ch & 0xF8) == 0xF0 ? 4 : 0;
            bool valid = len > 0 && i + len <= text.size();
            for (size_t j = 1; valid && j < len; ++j)
                valid = (static_cast<unsigned char>(text[i + j]) & 0xC0) == 0x80;
            if (valid) {
                out.append(text, i, len);
                i += len - 1;
            } else {
                out += "\\ufffd";
            }
            continue;
        }
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                    out += buf;
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    return out + "\"";
}

static std::string index_list(const std::vector<size_t>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); ++i) out += (i ? "," : "") + std::to_string(values[i]);
    return out + "]";
}

std::string analysis_json(const Analysis& analysis) {
    std::string out = "{\"path\":" + quote(analysis.path);
    out += ",\"changed_lines\":" + std::to_string(analysis.changed_lines);
    out += ",\"residual_lines\":" + std::to_string(analysis.residual_lines);
    out += ",\"fallback\":" + (analysis.fallback.empty() ? std::string("null") : quote(analysis.fallback));
    out += ",\"hunks\":[";
    for (size_t i = 0; i < analysis.hunks.size(); ++i) {
        const Hunk& hunk = analysis.hunks[i];
        out += std::string(i ? "," : "") + "{\"old_begin\":" + std::to_string(hunk.old_begin) +
               ",\"old_end\":" + std::to_string(hunk.old_end) + ",\"new_begin\":" + std::to_string(hunk.new_begin) +
               ",\"new_end\":" + std::to_string(hunk.new_end) + ",\"fold\":" +
               (analysis.fold_of_hunk[i] < 0 ? std::string("null") : std::to_string(analysis.fold_of_hunk[i])) + "}";
    }
    out += "],\"folds\":[";
    for (size_t i = 0; i < analysis.folds.size(); ++i) {
        const Fold& fold = analysis.folds[i];
        out += std::string(i ? "," : "") + "{\"kind\":" + quote(fold_kind_name(fold.kind)) +
               ",\"hunks\":" + index_list(fold.hunks) + ",\"renames\":[";
        for (size_t r = 0; r < fold.renames.size(); ++r)
            out += std::string(r ? "," : "") + "{\"function\":" + quote(fold.renames[r].function) +
                   ",\"from\":" + quote(fold.renames[r].old_name) + ",\"to\":" + quote(fold.renames[r].new_name) +
                   ",\"sites\":" + std::to_string(fold.renames[r].sites) + "}";
        out += "],\"moved\":[";
        for (size_t m = 0; m < fold.moved.size(); ++m) out += std::string(m ? "," : "") + quote(fold.moved[m]);
        out += "]}";
    }
    out += "],\"refusals\":[";
    for (size_t i = 0; i < analysis.refusals.size(); ++i) {
        const Refusal& refusal = analysis.refusals[i];
        out += std::string(i ? "," : "") + "{\"proposed\":" + quote(fold_kind_name(refusal.proposed)) +
               ",\"hunks\":" + index_list(refusal.hunks) + ",\"reason\":" + quote(refusal.reason) +
               ",\"new_line\":" + std::to_string(refusal.new_line) + "}";
    }
    return out + "]}";
}
