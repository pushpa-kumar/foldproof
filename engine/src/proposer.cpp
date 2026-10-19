#include "proposer.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <numeric>
#include <set>

extern "C" const TSLanguage* tree_sitter_cpp();

const char* fold_kind_name(FoldKind kind) {
    switch (kind) {
        case FoldKind::Format: return "format";
        case FoldKind::Rename: return "rename";
        case FoldKind::Move: return "move";
    }
    return "?";
}

struct SyntaxLeaf {
    std::string type;
    std::string text;
    size_t row;
    std::string function;
};

class SyntaxTree {
public:
    explicit SyntaxTree(const std::string& text) : text_(text) {
        parser_ = ts_parser_new();
        ts_parser_set_language(parser_, tree_sitter_cpp());
        tree_ = ts_parser_parse_string(parser_, nullptr, text_.c_str(), static_cast<uint32_t>(text_.size()));
        collect(ts_tree_root_node(tree_), "");
    }
    ~SyntaxTree() {
        ts_tree_delete(tree_);
        ts_parser_delete(parser_);
    }
    SyntaxTree(const SyntaxTree&) = delete;
    SyntaxTree& operator=(const SyntaxTree&) = delete;

    std::vector<const SyntaxLeaf*> leaves_in(size_t row_begin, size_t row_end) const {
        std::vector<const SyntaxLeaf*> out;
        for (const SyntaxLeaf& leaf : leaves_)
            if (leaf.row >= row_begin && leaf.row < row_end) out.push_back(&leaf);
        return out;
    }
    std::vector<std::string> functions_in(size_t row_begin, size_t row_end) const {
        std::vector<std::string> out;
        for (const auto& [name, rows] : functions_)
            if (rows.first >= row_begin && rows.second < row_end) out.push_back(name);
        return out;
    }
    std::vector<size_t> unit_boundaries() const {
        std::vector<size_t> cuts;
        for (const auto& [name, rows] : functions_) {
            size_t start = rows.first;
            while (start > 0 && comment_rows_.count(start - 1)) --start;
            cuts.push_back(start);
            cuts.push_back(rows.second + 1);
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        return cuts;
    }

private:
    std::string node_text(TSNode node) const {
        return text_.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
    }

    std::string function_name(TSNode node) const {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        while (!ts_node_is_null(declarator)) {
            TSNode inner = ts_node_child_by_field_name(declarator, "declarator", 10);
            if (ts_node_is_null(inner)) break;
            declarator = inner;
        }
        return ts_node_is_null(declarator) ? "" : node_text(declarator);
    }

    void collect(TSNode node, const std::string& function) {
        std::string type = ts_node_type(node);
        std::string scope = function;
        if (type == "function_definition" && function.empty()) {
            scope = function_name(node);
            functions_.emplace_back(scope, std::make_pair(ts_node_start_point(node).row, ts_node_end_point(node).row));
        }
        if (type == "comment") {
            size_t first = ts_node_start_point(node).row, last = ts_node_end_point(node).row;
            bool alone = true;
            for (const SyntaxLeaf& leaf : leaves_)
                if (leaf.row == first && leaf.type != "comment") alone = false;
            if (alone)
                for (size_t row = first; row <= last; ++row) comment_rows_.insert(row);
        }
        uint32_t count = ts_node_child_count(node);
        if (count == 0 || type == "string_literal" || type == "raw_string_literal" || type == "char_literal" ||
            type == "comment") {
            std::string text = node_text(node);
            if (!text.empty()) leaves_.push_back({type, text, ts_node_start_point(node).row, scope});
            return;
        }
        for (uint32_t i = 0; i < count; ++i) collect(ts_node_child(node, i), scope);
    }

    std::string text_;
    TSParser* parser_ = nullptr;
    TSTree* tree_ = nullptr;
    std::vector<SyntaxLeaf> leaves_;
    std::vector<std::pair<std::string, std::pair<size_t, size_t>>> functions_;
    std::set<size_t> comment_rows_;
};

static std::string old_lines_of(const std::string& text, size_t row) {
    size_t start = 0;
    for (size_t i = 0; i < row && start != std::string::npos; ++i) {
        start = text.find('\n', start);
        if (start != std::string::npos) ++start;
    }
    if (start == std::string::npos) return "";
    size_t end = text.find('\n', start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
}

std::vector<Hunk> refine_hunks(const std::string& old_text, const std::string& new_text, const std::vector<Hunk>& hunks) {
    SyntaxTree old_tree(old_text), new_tree(new_text);
    auto old_cuts = old_tree.unit_boundaries();
    auto new_cuts = new_tree.unit_boundaries();
    std::vector<Hunk> refined;
    for (const Hunk& hunk : hunks) {
        bool deletion = hunk.new_begin == hunk.new_end;
        bool insertion = hunk.old_begin == hunk.old_end;
        if (!deletion && !insertion) {
            std::vector<size_t> old_points{hunk.old_begin}, new_points{hunk.new_begin};
            for (size_t cut : old_cuts)
                if (cut > hunk.old_begin && cut < hunk.old_end) old_points.push_back(cut);
            for (size_t cut : new_cuts)
                if (cut > hunk.new_begin && cut < hunk.new_end) new_points.push_back(cut);
            old_points.push_back(hunk.old_end);
            new_points.push_back(hunk.new_end);
            if (old_points.size() != new_points.size() && hunk.old_end - hunk.old_begin == hunk.new_end - hunk.new_begin) {
                old_points.clear();
                new_points.clear();
                for (size_t i = 0; i <= hunk.old_end - hunk.old_begin; ++i) {
                    old_points.push_back(hunk.old_begin + i);
                    new_points.push_back(hunk.new_begin + i);
                }
            }
            if (old_points.size() != new_points.size()) {
                refined.push_back(hunk);
                continue;
            }
            bool per_line = old_points.size() == hunk.old_end - hunk.old_begin + 1 &&
                            old_points.size() > 2;
            for (size_t i = 0; i + 1 < old_points.size(); ++i) {
                if (per_line && old_points[i + 1] - old_points[i] == 1 &&
                    old_lines_of(old_text, old_points[i]) == old_lines_of(new_text, new_points[i]))
                    continue;
                refined.push_back({old_points[i], old_points[i + 1], new_points[i], new_points[i + 1]});
            }
            continue;
        }
        size_t begin = deletion ? hunk.old_begin : hunk.new_begin;
        size_t end = deletion ? hunk.old_end : hunk.new_end;
        std::vector<size_t> points{begin};
        for (size_t cut : deletion ? old_cuts : new_cuts)
            if (cut > begin && cut < end) points.push_back(cut);
        points.push_back(end);
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            if (deletion)
                refined.push_back({points[i], points[i + 1], hunk.new_begin, hunk.new_end});
            else
                refined.push_back({hunk.old_begin, hunk.old_end, points[i], points[i + 1]});
        }
    }
    return refined;
}

static std::string squash(const std::string& text) {
    std::string out;
    for (char ch : text)
        if (!std::isspace(static_cast<unsigned char>(ch))) out.push_back(ch);
    return out;
}

static bool same_leaf(const SyntaxLeaf* a, const SyntaxLeaf* b) {
    if (a->type != b->type) return false;
    if (a->type == "comment") return squash(a->text) == squash(b->text);
    return a->text == b->text;
}

static size_t find_root(std::vector<size_t>& parent, size_t i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

std::vector<Proposal> propose_folds(const std::string& old_text, const std::string& new_text,
                                    const std::vector<Hunk>& hunks) {
    SyntaxTree old_tree(old_text), new_tree(new_text);
    std::vector<Proposal> proposals;
    std::vector<std::vector<RenamePair>> hunk_renames(hunks.size());
    std::vector<bool> rename_only(hunks.size(), false);
    std::map<std::string, std::vector<size_t>> deletions_by_shape, insertions_by_shape;

    for (size_t h = 0; h < hunks.size(); ++h) {
        const Hunk& hunk = hunks[h];
        auto old_leaves = old_tree.leaves_in(hunk.old_begin, hunk.old_end);
        auto new_leaves = new_tree.leaves_in(hunk.new_begin, hunk.new_end);
        if (hunk.new_begin == hunk.new_end || hunk.old_begin == hunk.old_end) {
            bool deletion = hunk.new_begin == hunk.new_end;
            const auto& leaves = deletion ? old_leaves : new_leaves;
            if (leaves.empty()) {
                proposals.push_back({FoldKind::Format, {h}, {}, {}});
                continue;
            }
            auto names = deletion ? old_tree.functions_in(hunk.old_begin, hunk.old_end)
                                  : new_tree.functions_in(hunk.new_begin, hunk.new_end);
            if (names.size() != 1) continue;
            std::string shape = names[0] + "\x1d";
            for (const SyntaxLeaf* leaf : leaves) shape += leaf->type + "\x1e";
            (deletion ? deletions_by_shape : insertions_by_shape)[shape].push_back(h);
            continue;
        }
        if (old_leaves.size() != new_leaves.size()) continue;
        bool format_only = true, renames_ok = true;
        std::set<std::pair<std::string, std::string>> seen;
        for (size_t i = 0; i < old_leaves.size(); ++i) {
            if (same_leaf(old_leaves[i], new_leaves[i])) continue;
            format_only = false;
            if (old_leaves[i]->type != "identifier" || new_leaves[i]->type != "identifier" ||
                new_leaves[i]->function.empty()) {
                renames_ok = false;
                break;
            }
            if (seen.insert({old_leaves[i]->text, new_leaves[i]->text}).second)
                hunk_renames[h].push_back({new_leaves[i]->function, old_leaves[i]->text, new_leaves[i]->text});
        }
        if (format_only) {
            proposals.push_back({FoldKind::Format, {h}, {}, {}});
        } else if (renames_ok) {
            rename_only[h] = true;
        }
    }

    std::vector<size_t> parent(hunks.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::map<std::string, size_t> owner;
    for (size_t h = 0; h < hunks.size(); ++h) {
        if (!rename_only[h]) continue;
        for (const auto& pair : hunk_renames[h]) {
            std::string key = pair.function + "\x1f" + pair.from + "\x1f" + pair.to;
            auto [it, inserted] = owner.emplace(key, h);
            if (!inserted) parent[find_root(parent, h)] = find_root(parent, it->second);
        }
    }
    std::map<size_t, Proposal> groups;
    for (size_t h = 0; h < hunks.size(); ++h) {
        if (!rename_only[h]) continue;
        Proposal& group = groups.try_emplace(find_root(parent, h), Proposal{FoldKind::Rename, {}, {}, {}}).first->second;
        group.hunks.push_back(h);
        for (const auto& pair : hunk_renames[h]) {
            bool known = std::any_of(group.renames.begin(), group.renames.end(), [&](const RenamePair& existing) {
                return existing.function == pair.function && existing.from == pair.from && existing.to == pair.to;
            });
            if (!known) group.renames.push_back(pair);
        }
    }
    for (auto& [root, group] : groups) proposals.push_back(std::move(group));

    for (auto& [shape, deletions] : deletions_by_shape) {
        auto it = insertions_by_shape.find(shape);
        if (it == insertions_by_shape.end()) continue;
        size_t pairs = std::min(deletions.size(), it->second.size());
        for (size_t i = 0; i < pairs; ++i) {
            std::string name = shape.substr(0, shape.find('\x1d'));
            proposals.push_back({FoldKind::Move, {deletions[i], it->second[i]}, {}, {name}});
        }
    }
    return proposals;
}
