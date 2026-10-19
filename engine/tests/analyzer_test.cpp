#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "analyzer.hpp"

static std::string read_text(const std::string& path) {
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

static Analysis run(const std::string& old_src, const std::string& new_src) {
    return analyze("t.cpp", old_src, new_src, ParseConfig::defaults());
}

TEST(Analyzer, IdenticalFilesHaveNothingToReview) {
    Analysis analysis = run("int f() { return 1; }\n", "int f() { return 1; }\n");
    EXPECT_EQ(analysis.changed_lines, 0u);
    EXPECT_TRUE(analysis.hunks.empty());
}

TEST(Analyzer, ReformatIsFoldedAndRealChangeStays) {
    std::string old_src = "int f(int a){return a+1;}\nint g(int b) { return b * 2; }\n";
    std::string new_src = "int f(int a) { return a + 1; }\nint g(int b) { return b * 3; }\n";
    Analysis analysis = run(old_src, new_src);
    ASSERT_EQ(analysis.folds.size(), 1u);
    EXPECT_EQ(analysis.folds[0].kind, FoldKind::Format);
    EXPECT_EQ(analysis.changed_lines, 4u);
    EXPECT_EQ(analysis.residual_lines, 2u);
}

TEST(Analyzer, ProposerIsNeverTrustedWithoutChecker) {
    std::string old_src = "#define STR(x) #x\nconst char* s = STR(a+b);\n";
    std::string new_src = "#define STR(x) #x\nconst char* s = STR(a + b);\n";
    Analysis analysis = run(old_src, new_src);
    EXPECT_TRUE(analysis.folds.empty());
    ASSERT_EQ(analysis.refusals.size(), 1u);
    EXPECT_EQ(analysis.refusals[0].proposed, FoldKind::Format);
    EXPECT_NE(analysis.refusals[0].reason.find("stringified"), std::string::npos) << analysis.refusals[0].reason;
    EXPECT_EQ(analysis.residual_lines, analysis.changed_lines);
}

TEST(Analyzer, RenameAcrossSeveralHunksIsOneFold) {
    std::string old_src =
        "int f(int n) {\n  int tmp = n;\n  int other = 1;\n  other += 2;\n  other += 3;\n  return tmp + other;\n}\n";
    std::string new_src =
        "int f(int n) {\n  int value = n;\n  int other = 1;\n  other += 2;\n  other += 3;\n  return value + other;\n}\n";
    Analysis analysis = run(old_src, new_src);
    ASSERT_EQ(analysis.folds.size(), 1u);
    EXPECT_EQ(analysis.folds[0].kind, FoldKind::Rename);
    EXPECT_EQ(analysis.folds[0].hunks.size(), 2u);
    ASSERT_EQ(analysis.folds[0].renames.size(), 1u);
    EXPECT_EQ(analysis.folds[0].renames[0].old_name, "tmp");
    EXPECT_EQ(analysis.residual_lines, 0u);
}

TEST(Analyzer, PartialRenameIsRefused) {
    std::string old_src = "int n = 9;\nint f() {\n  int n = 1;\n  int k = 2;\n  k += 1;\n  return n;\n}\n";
    std::string new_src = "int n = 9;\nint f() {\n  int m = 1;\n  int k = 2;\n  k += 1;\n  return n;\n}\n";
    Analysis analysis = run(old_src, new_src);
    EXPECT_TRUE(analysis.folds.empty());
    EXPECT_EQ(analysis.residual_lines, analysis.changed_lines);
}

TEST(Analyzer, FoldsAndResidualPartitionTheDiff) {
    std::string old_src = read_text(FOLDPROOF_FIXTURES "/agent_refactor/old.cpp");
    std::string new_src = read_text(FOLDPROOF_FIXTURES "/agent_refactor/new.cpp");
    Analysis analysis = run(old_src, new_src);
    size_t folded = 0;
    for (size_t h = 0; h < analysis.hunks.size(); ++h) {
        const Hunk& hunk = analysis.hunks[h];
        size_t lines = (hunk.old_end - hunk.old_begin) + (hunk.new_end - hunk.new_begin);
        if (analysis.fold_of_hunk[h] >= 0) folded += lines;
    }
    EXPECT_EQ(folded + analysis.residual_lines, analysis.changed_lines);
    EXPECT_EQ(analysis.residual_lines, 2u);
}

TEST(Analyzer, GoldenJson) {
    std::string old_src = read_text(FOLDPROOF_FIXTURES "/agent_refactor/old.cpp");
    std::string new_src = read_text(FOLDPROOF_FIXTURES "/agent_refactor/new.cpp");
    std::string expected = read_text(FOLDPROOF_FIXTURES "/agent_refactor/expected.json");
    while (!expected.empty() && expected.back() == '\n') expected.pop_back();
    EXPECT_EQ(analysis_json(run(old_src, new_src)), expected);
}

TEST(Analyzer, UnparsableNewFileFallsBack) {
    Analysis analysis = run("int f();\n", "int f(;\n");
    EXPECT_TRUE(analysis.folds.empty());
    EXPECT_EQ(analysis.residual_lines, analysis.changed_lines);
}

TEST(Analyzer, RenameCertificateAttachesOnlyToItsFold) {
    std::string old_src =
        "int f() {\n  int a = 1;\n  int pad = 0;\n  pad += 1;\n  return a + pad;\n}\nint g(int count) { return count+1; }\n";
    std::string new_src =
        "int f() {\n  int count = 1;\n  int pad = 0;\n  pad += 1;\n  return count + pad;\n}\nint g(int count) { return count + 1; }\n";
    Analysis analysis = run(old_src, new_src);
    int renames = 0, formats = 0;
    for (const Fold& fold : analysis.folds) {
        if (fold.kind == FoldKind::Rename) {
            ++renames;
            ASSERT_EQ(fold.renames.size(), 1u);
            EXPECT_EQ(fold.renames[0].function, "f");
        }
        if (fold.kind == FoldKind::Format) {
            ++formats;
            EXPECT_TRUE(fold.renames.empty());
        }
    }
    EXPECT_EQ(renames, 1);
    EXPECT_EQ(formats, 1);
    EXPECT_EQ(analysis.residual_lines, 0u);
}

TEST(Analyzer, JsonEscapesInvalidUtf8) {
    Analysis analysis;
    analysis.path = std::string("x\xff") + "\xc3\xa9.cpp";
    std::string json = analysis_json(analysis);
    EXPECT_NE(json.find("x\\ufffd\xc3\xa9.cpp"), std::string::npos) << json;
}
