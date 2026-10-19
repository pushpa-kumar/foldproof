#include <gtest/gtest.h>

#include <random>

#include "line_diff.hpp"

static std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines) out += line;
    return out;
}

TEST(LineDiff, IdenticalHasNoHunks) {
    auto lines = split_lines("a\nb\nc\n");
    std::vector<Hunk> hunks;
    ASSERT_TRUE(line_diff(lines, lines, hunks));
    EXPECT_TRUE(hunks.empty());
}

TEST(LineDiff, SingleReplacement) {
    auto old_lines = split_lines("a\nb\nc\n");
    auto new_lines = split_lines("a\nB\nc\n");
    std::vector<Hunk> hunks;
    ASSERT_TRUE(line_diff(old_lines, new_lines, hunks));
    ASSERT_EQ(hunks.size(), 1u);
    EXPECT_EQ(hunks[0].old_begin, 1u);
    EXPECT_EQ(hunks[0].old_end, 2u);
    EXPECT_EQ(hunks[0].new_begin, 1u);
    EXPECT_EQ(hunks[0].new_end, 2u);
}

TEST(LineDiff, MissingTrailingNewlineIsALineChange) {
    auto old_lines = split_lines("a\nb");
    auto new_lines = split_lines("a\nb\n");
    std::vector<Hunk> hunks;
    ASSERT_TRUE(line_diff(old_lines, new_lines, hunks));
    EXPECT_EQ(hunks.size(), 1u);
}

TEST(LineDiff, RandomizedMergeRoundTrip) {
    std::mt19937 rng(7);
    for (int round = 0; round < 300; ++round) {
        std::vector<std::string> old_lines, new_lines;
        int old_size = rng() % 30, new_size = rng() % 30;
        for (int i = 0; i < old_size; ++i) old_lines.push_back(std::string(1, char('a' + rng() % 4)) + "\n");
        for (int i = 0; i < new_size; ++i) new_lines.push_back(std::string(1, char('a' + rng() % 4)) + "\n");
        std::vector<Hunk> hunks;
        ASSERT_TRUE(line_diff(old_lines, new_lines, hunks));
        std::vector<bool> all_old(hunks.size(), true), all_new(hunks.size(), false);
        EXPECT_EQ(merge_hunks(old_lines, new_lines, hunks, all_old), join(old_lines));
        EXPECT_EQ(merge_hunks(old_lines, new_lines, hunks, all_new), join(new_lines));
        size_t unchanged = old_lines.size();
        for (const auto& hunk : hunks) unchanged -= hunk.old_end - hunk.old_begin;
        size_t new_unchanged = new_lines.size();
        for (const auto& hunk : hunks) new_unchanged -= hunk.new_end - hunk.new_begin;
        EXPECT_EQ(unchanged, new_unchanged);
        std::vector<std::vector<int>> lcs(old_size + 1, std::vector<int>(new_size + 1, 0));
        for (int i = old_size - 1; i >= 0; --i)
            for (int j = new_size - 1; j >= 0; --j)
                lcs[i][j] = old_lines[i] == new_lines[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        EXPECT_EQ(unchanged, static_cast<size_t>(lcs[0][0])) << "diff must be minimal";
    }
}

TEST(LineDiff, EditLimitReportsFailure) {
    std::vector<std::string> old_lines, new_lines;
    for (int i = 0; i < 100; ++i) old_lines.push_back("o" + std::to_string(i) + "\n");
    for (int i = 0; i < 100; ++i) new_lines.push_back("n" + std::to_string(i) + "\n");
    std::vector<Hunk> hunks;
    EXPECT_FALSE(line_diff(old_lines, new_lines, hunks, 50));
}

TEST(LineDiff, WhitespaceAwareDiffPairsReindentedLines) {
    auto old_lines = split_lines("f();\n}\n  g();\n  h();\n}\n");
    auto new_lines = split_lines("f();\n}\ng();\nh();\n}\n");
    std::vector<Hunk> hunks;
    ASSERT_TRUE(whitespace_aware_diff(old_lines, new_lines, hunks));
    ASSERT_EQ(hunks.size(), 2u);
    for (const Hunk& hunk : hunks) {
        EXPECT_EQ(hunk.old_end - hunk.old_begin, 1u);
        EXPECT_EQ(hunk.new_end - hunk.new_begin, 1u);
    }
}

TEST(LineDiff, WhitespaceAwareDiffRoundTrips) {
    std::mt19937 rng(11);
    const char* pool[] = {"a\n", " a\n", "b\n", "  b\n", "}\n", "\n"};
    for (int round = 0; round < 300; ++round) {
        std::vector<std::string> old_lines, new_lines;
        for (int i = 0, n = rng() % 25; i < n; ++i) old_lines.push_back(pool[rng() % 6]);
        for (int i = 0, n = rng() % 25; i < n; ++i) new_lines.push_back(pool[rng() % 6]);
        std::vector<Hunk> hunks;
        ASSERT_TRUE(whitespace_aware_diff(old_lines, new_lines, hunks));
        std::vector<bool> all_old(hunks.size(), true), all_new(hunks.size(), false);
        EXPECT_EQ(merge_hunks(old_lines, new_lines, hunks, all_old), join(old_lines));
        EXPECT_EQ(merge_hunks(old_lines, new_lines, hunks, all_new), join(new_lines));
    }
}
