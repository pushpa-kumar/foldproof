#include <gtest/gtest.h>

#include "rename_check.hpp"

static RenameResult check(const std::string& old_src, const std::string& new_src) {
    ParseConfig config = ParseConfig::defaults();
    TranslationUnit old_tu("t.cpp", old_src, config);
    TranslationUnit new_tu("t.cpp", new_src, config);
    return rename_equivalent(old_tu, new_tu);
}

TEST(Rename, LocalRenameIsEquivalent) {
    RenameResult result = check("int f(int n) { int tmp = n * 2; return tmp + 1; }\n",
                                "int f(int n) { int doubled = n * 2; return doubled + 1; }\n");
    ASSERT_TRUE(result.check.ok) << result.check.reason;
    ASSERT_EQ(result.renames.size(), 1u);
    EXPECT_EQ(result.renames[0].function, "f");
    EXPECT_EQ(result.renames[0].old_name, "tmp");
    EXPECT_EQ(result.renames[0].new_name, "doubled");
    EXPECT_EQ(result.renames[0].sites, 2u);
}

TEST(Rename, ParameterRenameIsEquivalent) {
    RenameResult result = check("int f(int n) { return n; }\n", "int f(int count) { return count; }\n");
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Rename, SwappedNamesAreEquivalent) {
    std::string old_src = "int f(int a, int b) { return a - b; }\n";
    std::string new_src = "int f(int b, int a) { return b - a; }\n";
    EXPECT_TRUE(check(old_src, new_src).check.ok);
}

TEST(Rename, SwappingUsesIsRejected) {
    std::string old_src = "int f(int a, int b) { return a - b; }\n";
    std::string new_src = "int f(int a, int b) { return b - a; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_FALSE(result.check.ok);
    EXPECT_NE(result.check.reason.find("capture or shadow"), std::string::npos) << result.check.reason;
}

TEST(Rename, ShadowingOuterLocalIsRejected) {
    std::string old_src = "int f() { int a = 1; { int b = 2; a += b; } return a; }\n";
    std::string new_src = "int f() { int a = 1; { int a = 2; a += a; } return a; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, CapturingGlobalIsRejected) {
    std::string old_src = "int total = 5;\nint f() { int sum = 1; return sum + total; }\n";
    std::string new_src = "int total = 5;\nint f() { int total = 1; return total + total; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_FALSE(result.check.ok);
    EXPECT_NE(result.check.reason.find("different entity"), std::string::npos) << result.check.reason;
}

TEST(Rename, PartialRenameExposingGlobalIsRejected) {
    std::string old_src = "int n = 5;\nint f() { int n = 1; return n; }\n";
    std::string new_src = "int n = 5;\nint f() { int k = 1; return n; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, GlobalRenameIsRejected) {
    std::string old_src = "int count = 0;\nint f() { return count; }\n";
    std::string new_src = "int total = 0;\nint f() { return total; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, MemberRenameIsRejected) {
    std::string old_src = "struct S { int x; int get() { return x; } };\n";
    std::string new_src = "struct S { int y; int get() { return y; } };\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, LambdaCaptureRenameIsEquivalent) {
    std::string old_src = "int f() { int base = 3; auto g = [base](int v) { return base + v; }; return g(1); }\n";
    std::string new_src = "int f() { int offset = 3; auto g = [offset](int v) { return offset + v; }; return g(1); }\n";
    EXPECT_TRUE(check(old_src, new_src).check.ok) << check(old_src, new_src).check.reason;
}

TEST(Rename, LambdaParamShadowingCaptureIsRejected) {
    std::string old_src = "int f() { int base = 3; auto g = [&](int v) { return base + v; }; return g(1); }\n";
    std::string new_src = "int f() { int base = 3; auto g = [&](int base) { return base + base; }; return g(1); }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, MacroMentioningLocalIsRejected) {
    std::string old_src = "#define USE_X (x + 1)\nint f() { int x = 1; return USE_X; }\n";
    std::string new_src = "#define USE_X (x + 1)\nint f() { int y = 1; return USE_X; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, NestedMacroMentioningLocalIsRejected) {
    std::string old_src = "#define INNER (x)\n#define OUTER (INNER + 1)\nint x = 7;\nint f() { int x = 1; return OUTER; }\n";
    std::string new_src = "#define INNER (x)\n#define OUTER (INNER + 1)\nint x = 7;\nint f() { int y = 1; return OUTER; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_FALSE(result.check.ok);
    EXPECT_NE(result.check.reason.find("mentions 'x'"), std::string::npos) << result.check.reason;
}

TEST(Rename, UnrelatedMacroIsAllowed) {
    std::string old_src = "#define TWICE(v) ((v) * 2)\nint f() { int a = 1; return TWICE(a); }\n";
    std::string new_src = "#define TWICE(v) ((v) * 2)\nint f() { int b = 1; return TWICE(b); }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Rename, ErrorInFunctionIsRejected) {
    std::string old_src = "int f() { int a = undeclared(); return a; }\n";
    std::string new_src = "int f() { int b = undeclared(); return b; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_FALSE(result.check.ok);
}

TEST(Rename, StructuredBindingRenameIsRejected) {
    std::string old_src = "struct P { int a, b; };\nint f(P p) { auto [x, y] = p; return x + y; }\n";
    std::string new_src = "struct P { int a, b; };\nint f(P p) { auto [u, v] = p; return u + v; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, RenameWithReformatIsEquivalent) {
    std::string old_src = "int f(int n){int t=n;return t;}\n";
    std::string new_src = "int f(int n) {\n  int value = n;\n  return value;\n}\n";
    EXPECT_TRUE(check(old_src, new_src).check.ok);
}

TEST(Rename, RenameInTemplateIsEquivalent) {
    std::string old_src = "template <class T> T f(T a) { T acc = a; return acc + a; }\n";
    std::string new_src = "template <class T> T f(T a) { T sum = a; return sum + a; }\n";
    EXPECT_TRUE(check(old_src, new_src).check.ok) << check(old_src, new_src).check.reason;
}

TEST(Rename, DependentNameCaptureInTemplateIsRejected) {
    std::string old_src = "template <class T> int f(T t) { int y = 0; return g(t) + y; }\n";
    std::string new_src = "template <class T> int f(T t) { int g = 0; return g(t) + g; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, MacroNotMentioningRenamedNameIsAllowed) {
    std::string old_src = "#define INNER (x)\n#define OUTER (INNER + 1)\nint f() { int x = 1; int z = 0; return OUTER + z; }\n";
    std::string new_src = "#define INNER (x)\n#define OUTER (INNER + 1)\nint f() { int x = 1; int w = 0; return OUTER + w; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Rename, StringizedArgumentIsRejected) {
    std::string prelude = "int printf(const char*, ...);\n#define SHOW(v) printf(\"%s=%d\\n\", #v, v)\n";
    RenameResult result = check(prelude + "void f() { int tmp = 3; SHOW(tmp); }\n", prelude + "void f() { int val = 3; SHOW(val); }\n");
    EXPECT_FALSE(result.check.ok) << "stringized argument text changes";
}

TEST(Rename, AssertWithRenamedLocalIsRejected) {
    std::string old_src = "#include <cassert>\nint f(int x) { assert(x > 0); return x; }\n";
    std::string new_src = "#include <cassert>\nint f(int y) { assert(y > 0); return y; }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, TokenPastingIsRejected) {
    std::string old_src = "int tmp = 100;\n#define CAT(x, y) x##y\nint f() { int tmp = 5; return CAT(t, mp); }\n";
    std::string new_src = "int tmp = 100;\n#define CAT(x, y) x##y\nint f() { int val = 5; return CAT(t, mp); }\n";
    EXPECT_FALSE(check(old_src, new_src).check.ok);
}

TEST(Rename, MacroNamePassedAsArgumentIsChecked) {
    std::string prelude = "int a = 100;\n#define GA() a\n#define CALL(m) m()\n";
    RenameResult result = check(prelude + "int f(int a) { return CALL(GA); }\n", prelude + "int f(int b) { return CALL(GA); }\n");
    EXPECT_FALSE(result.check.ok);
}

TEST(Rename, KeywordNamedMacroIsChecked) {
    std::string prelude = "int x = 100;\n#define return return x +\n";
    EXPECT_FALSE(check(prelude + "int f(int x) { return 1; }\n", prelude + "int f(int y) { return 1; }\n").check.ok);
}

TEST(Rename, MacroWithInactiveDefinitionIsRejected) {
    std::string prelude =
        "int printf(const char*, ...);\nint tmp = 100;\n#ifdef DEBUG\n#define TRACE() printf(\"%d\", tmp)\n#else\n#define TRACE()\n#endif\n";
    RenameResult result =
        check(prelude + "int f() { int tmp = 5; TRACE(); return tmp; }\n", prelude + "int f() { int val = 5; TRACE(); return val; }\n");
    EXPECT_FALSE(result.check.ok);
    EXPECT_NE(result.check.reason.find("#if branch"), std::string::npos) << result.check.reason;
}

TEST(Rename, LengthChangingRenameKeepsLocalClassIdentity) {
    std::string old_src = "int f() { int x = 1; struct P { int v; }; P p{x}; return p.v; }\n";
    std::string new_src = "int f() { int longer_name = 1; struct P { int v; }; P p{longer_name}; return p.v; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}

TEST(Rename, OverloadedOperatorChoiceIsCompared) {
    std::string old_src = "struct A { int v; };\nint operator+(A a, int b) { return a.v + b; }\nint f() { A a{1}; int k = 2; return a + k; }\n";
    std::string new_src = "struct A { int v; };\nint operator+(A a, int b) { return a.v + b; }\nint f() { A a{1}; int n = 2; return a + n; }\n";
    RenameResult result = check(old_src, new_src);
    EXPECT_TRUE(result.check.ok) << result.check.reason;
}
