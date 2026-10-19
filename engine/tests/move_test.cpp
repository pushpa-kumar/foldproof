#include <gtest/gtest.h>

#include "move_check.hpp"

static MoveResult check(const std::string& old_src, const std::string& new_src, const std::vector<std::string>& moved) {
    ParseConfig config = ParseConfig::defaults();
    TranslationUnit old_tu("t.cpp", old_src, config);
    TranslationUnit new_tu("t.cpp", new_src, config);
    return move_equivalent(old_tu, new_tu, moved);
}

static const char* kOld =
    "int helper(int x) { return x * 2; }\n"
    "// adds one\n"
    "int add_one(int x) { return x + 1; }\n"
    "int run(int v) { return helper(v) + add_one(v); }\n";

TEST(Move, ReorderedFunctionIsEquivalent) {
    std::string new_src =
        "// adds one\n"
        "int add_one(int x) { return x + 1; }\n"
        "int helper(int x) { return x * 2; }\n"
        "int run(int v) { return helper(v) + add_one(v); }\n";
    MoveResult result = check(kOld, new_src, {"add_one"});
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Move, MovedFunctionWithEditIsRejected) {
    std::string new_src =
        "// adds one\n"
        "int add_one(int x) { return x + 2; }\n"
        "int helper(int x) { return x * 2; }\n"
        "int run(int v) { return helper(v) + add_one(v); }\n";
    EXPECT_FALSE(check(kOld, new_src, {"add_one"}).check.ok);
}

TEST(Move, MovePastFirstUseIsRejected) {
    std::string new_src =
        "int helper(int x) { return x * 2; }\n"
        "int run(int v) { return helper(v) + add_one(v); }\n"
        "// adds one\n"
        "int add_one(int x) { return x + 1; }\n";
    MoveResult result = check(kOld, new_src, {"add_one"});
    EXPECT_FALSE(result.check.ok);
}

TEST(Move, OverloadChoiceChangeIsRejected) {
    std::string old_src =
        "int pick(long) { return 1; }\n"
        "int use() { return pick(1); }\n"
        "int pick_int(int) { return 2; }\n";
    std::string new_src =
        "int pick(long) { return 1; }\n"
        "int pick_int(int) { return 2; }\n"
        "int use() { return pick(1); }\n";
    EXPECT_TRUE(check(old_src, new_src, {"pick_int"}).check.ok);

    std::string overload_old =
        "int pick(long) { return 1; }\n"
        "int use() { return pick(1); }\n"
        "int other(int) { return 2; }\n"
        "int pick(int) { return 3; }\n";
    std::string overload_new =
        "int pick(long) { return 1; }\n"
        "int pick(int) { return 3; }\n"
        "int use() { return pick(1); }\n"
        "int other(int) { return 2; }\n";
    MoveResult result = check(overload_old, overload_new, {"pick"});
    EXPECT_FALSE(result.check.ok);
}

TEST(Move, MovingUseAcrossOverloadIsRejected) {
    std::string old_src =
        "int pick(long) { return 1; }\n"
        "int use() { return pick(1); }\n"
        "int pick(int) { return 3; }\n";
    std::string new_src =
        "int pick(long) { return 1; }\n"
        "int pick(int) { return 3; }\n"
        "int use() { return pick(1); }\n";
    MoveResult result = check(old_src, new_src, {"use"});
    EXPECT_FALSE(result.check.ok);
    EXPECT_NE(result.check.reason.find("different declaration"), std::string::npos) << result.check.reason;
}

TEST(Move, TemplateMoveIsRejected) {
    std::string old_src = "template <class T> T id(T t) { return t; }\nint g() { return 0; }\n";
    std::string new_src = "int g() { return 0; }\ntemplate <class T> T id(T t) { return t; }\n";
    EXPECT_FALSE(check(old_src, new_src, {"id"}).check.ok);
}

TEST(Move, MoveCarryingDirectiveIsRejected) {
    std::string old_src = "int a() { return 1; }\n#define K 2\nint b() { return K; }\n";
    std::string new_src = "#define K 2\nint b() { return K; }\nint a() { return 1; }\n";
    EXPECT_FALSE(check(old_src, new_src, {"b"}).check.ok);
}

TEST(Move, MoveInsideNamespaceIsEquivalent) {
    std::string old_src = "namespace n {\nint a() { return 1; }\nint b() { return 2; }\n}\n";
    std::string new_src = "namespace n {\nint b() { return 2; }\nint a() { return 1; }\n}\n";
    MoveResult result = check(old_src, new_src, {"b"});
    EXPECT_TRUE(result.check.ok) << result.check.reason;
    MoveResult second = check(old_src, new_src, {"a"});
    EXPECT_TRUE(second.check.ok) << second.check.reason;
}

TEST(Move, UnlistedReorderIsRejected) {
    std::string new_src =
        "// adds one\n"
        "int add_one(int x) { return x + 1; }\n"
        "int helper(int x) { return x * 2; }\n"
        "int run(int v) { return helper(v) + add_one(v); }\n";
    EXPECT_FALSE(check(kOld, new_src, {}).check.ok);
}

TEST(Move, MoveAcrossNamespaceBoundaryIsRejected) {
    std::string old_src = "namespace n {\nint a() { return 1; }\n}\nint b() { return 2; }\n";
    std::string new_src = "namespace n {\nint a() { return 1; }\nint b() { return 2; }\n}\n";
    EXPECT_FALSE(check(old_src, new_src, {"b"}).check.ok);
}

TEST(Move, CrossingPragmaRegionIsRejected) {
    std::string old_src =
        "int printf(const char*, ...);\nvoid hello() { printf(\"x\"); }\n"
        "#pragma clang attribute push(__attribute__((constructor)), apply_to = function)\nvoid other() {}\n"
        "#pragma clang attribute pop\n";
    std::string new_src =
        "int printf(const char*, ...);\n"
        "#pragma clang attribute push(__attribute__((constructor)), apply_to = function)\nvoid other() {}\n"
        "void hello() { printf(\"x\"); }\n#pragma clang attribute pop\n";
    MoveResult result = check(old_src, new_src, {"hello"});
    EXPECT_FALSE(result.check.ok);
}

TEST(Move, OperatorOverloadChoiceIsCompared) {
    std::string old_src =
        "struct A { operator int() const { return 1; } };\nint use() { A a; return a + 1; }\n"
        "int operator+(A, int) { return 42; }\n";
    std::string new_src =
        "struct A { operator int() const { return 1; } };\nint operator+(A, int) { return 42; }\n"
        "int use() { A a; return a + 1; }\n";
    EXPECT_FALSE(check(old_src, new_src, {"operator+"}).check.ok);
}

TEST(Move, CounterOrderIsRejected) {
    std::string old_src = "int a() { return __COUNTER__; }\nint b() { return __COUNTER__; }\n";
    std::string new_src = "int b() { return __COUNTER__; }\nint a() { return __COUNTER__; }\n";
    EXPECT_FALSE(check(old_src, new_src, {"b"}).check.ok);
}

TEST(Move, LineObservingFunctionIsRejected) {
    std::string old_src = "int a() { return 1; }\nint b() { return __LINE__; }\n";
    std::string new_src = "int b() { return __LINE__; }\nint a() { return 1; }\n";
    EXPECT_FALSE(check(old_src, new_src, {"b"}).check.ok);
}

TEST(Move, FunctionAfterStructCanMove) {
    std::string old_src = "struct S { int v; };\nint f() { return 1; }\nint g() { return 2; }\n";
    std::string new_src = "struct S { int v; };\nint g() { return 2; }\nint f() { return 1; }\n";
    MoveResult result = check(old_src, new_src, {"g"});
    EXPECT_TRUE(result.check.ok) << result.check.reason;
    MoveResult other = check(old_src, new_src, {"f"});
    EXPECT_TRUE(other.check.ok) << other.check.reason;
}

TEST(Move, TrailingCommentTravelsWithFunction) {
    std::string old_src = "int f() { return 1; } // one\nint g() { return 2; }\n";
    std::string new_src = "int g() { return 2; }\nint f() { return 1; } // one\n";
    MoveResult result = check(old_src, new_src, {"f"});
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Move, FirstFunctionAfterIncludesCanMove) {
    std::string old_src = "#include <vector>\nint f() { return 1; }\nint g() { return 2; }\n";
    std::string new_src = "#include <vector>\nint g() { return 2; }\nint f() { return 1; }\n";
    MoveResult result = check(old_src, new_src, {"f"});
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}
