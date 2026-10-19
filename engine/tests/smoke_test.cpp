#include <gtest/gtest.h>
#include <clang-c/Index.h>
#include <tree_sitter/api.h>

#include "version.hpp"

extern "C" const TSLanguage* tree_sitter_cpp();

TEST(Smoke, VersionIsSet) { EXPECT_STREQ(foldproof_version(), "0.1.0"); }

TEST(Smoke, LibclangLoads) {
    CXIndex index = clang_createIndex(0, 0);
    ASSERT_NE(index, nullptr);
    clang_disposeIndex(index);
}

TEST(Smoke, TreeSitterParsesCpp) {
    TSParser* parser = ts_parser_new();
    ASSERT_TRUE(ts_parser_set_language(parser, tree_sitter_cpp()));
    const char* src = "int main() { return 0; }";
    TSTree* tree = ts_parser_parse_string(parser, nullptr, src, 24);
    EXPECT_FALSE(ts_node_has_error(ts_tree_root_node(tree)));
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}
