#include <gtest/gtest.h>

#include "format_check.hpp"

static CheckResult check(const std::string& old_src, const std::string& new_src) {
    ParseConfig config = ParseConfig::defaults();
    TranslationUnit old_tu("t.cpp", old_src, config);
    TranslationUnit new_tu("t.cpp", new_src, config);
    return format_equivalent(old_tu, new_tu);
}

TEST(Format, IdenticalFilesAreEquivalent) {
    EXPECT_TRUE(check("int f(int a) { return a + 1; }\n", "int f(int a) { return a + 1; }\n").ok);
}

TEST(Format, ReindentAndRewrapAreEquivalent) {
    std::string old_src = "int f(int a,int b){return a+b;}\n";
    std::string new_src = "int f(int a, int b) {\n    return a + b;\n}\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, CommentWhitespaceIsEquivalent) {
    std::string old_src = "// adds two\n// numbers\nint f();\n";
    std::string new_src = "//   adds two\n//numbers\nint f();\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
    EXPECT_TRUE(check("/* adds\n * two */ int f();\n", "/* adds two */\nint f();\n").ok);
}

TEST(Format, CommentTextChangeIsRejected) {
    CheckResult result = check("// keep lock held\nint f();\n", "// lock released\nint f();\n");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reason, "comment text changed");
}

TEST(Format, OperatorChangeIsRejected) {
    CheckResult result = check("bool f(int n) { return n > 0; }\n", "bool f(int n) { return n >= 0; }\n");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.new_line, 1u);
}

TEST(Format, ClosingAngleSplitIsRejected) {
    std::string old_src = "template <class T> struct V {};\nV<V<int>> x;\n";
    std::string new_src = "template <class T> struct V {};\nV<V<int> > x;\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, StringLiteralWhitespaceIsRejected) {
    EXPECT_FALSE(check("const char* s = \"a b\";\n", "const char* s = \"a  b\";\n").ok);
    EXPECT_FALSE(check("const char* s = \"ab\";\n", "const char* s = \"a\" \"b\";\n").ok);
}

TEST(Format, RawStringWhitespaceIsRejected) {
    EXPECT_FALSE(check("auto s = R\"(x\n y)\";\n", "auto s = R\"(x\n  y)\";\n").ok);
}

TEST(Format, DirectiveJoinedLinesAreRejected) {
    std::string old_src = "#define A 1\n#define B 2\nint x = A + B;\n";
    std::string new_src = "#define A 1 \\\n#define B 2\nint x = A + B;\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, ContinuationInsideDirectiveIsEquivalent) {
    std::string old_src = "#define ADD(a, b) ((a) + (b))\nint x = ADD(1, 2);\n";
    std::string new_src = "#define ADD(a, b) \\\n  ((a) + (b))\nint x = ADD(1, 2);\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, FunctionLikeToObjectLikeMacroIsRejected) {
    std::string old_src = "#define F(x) (x)\nint y = F(1);\n";
    std::string new_src = "#define F (x) (x)\nint y = F(1);\n";
    CheckResult result = check(old_src, new_src);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reason, "whitespace changed inside a preprocessor directive");
}

TEST(Format, WhitespaceInsideStringifiedMacroArgIsRejected) {
    std::string old_src = "#define STR(x) #x\nconst char* s = STR(a+b);\n";
    std::string new_src = "#define STR(x) #x\nconst char* s = STR(a + b);\n";
    CheckResult result = check(old_src, new_src);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reason, "whitespace changed inside a macro argument (may be stringified)");
}

TEST(Format, LeadingWhitespaceInMacroArgIsEquivalent) {
    std::string old_src = "#define STR(x) #x\nconst char* s = STR(ab);\n";
    std::string new_src = "#define STR(x) #x\nconst char* s = STR( ab );\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, UnresolvedCalleeIsTreatedAsMacro) {
    std::string old_src = "void f() { CHECK(a==b); }\n";
    std::string new_src = "void f() { CHECK(a == b); }\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, ResolvedFunctionArgsMayBeReformatted) {
    std::string old_src = "int g(int, int);\nint f(int a) { return g(a+1,a); }\n";
    std::string new_src = "int g(int, int);\nint f(int a) { return g(a + 1, a); }\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, StdHeadersResolve) {
    std::string old_src = "#include <vector>\nint f(){std::vector<int> v;return (int)v.size();}\n";
    std::string new_src = "#include <vector>\nint f() {\n  std::vector<int> v;\n  return (int)v.size();\n}\n";
    ParseConfig config = ParseConfig::defaults();
    TranslationUnit old_tu("t.cpp", old_src, config);
    EXPECT_TRUE(old_tu.errors().empty()) << (old_tu.errors().empty() ? "" : old_tu.errors()[0].message);
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, CommentSplitIntoTwoIsRejected) {
    EXPECT_FALSE(check("// a b\nint f();\n", "// a\n// b\nint f();\n").ok);
}

TEST(Format, DirectiveAfterSameLineCommentIsDetected) {
    std::string old_src = "/**/ #define F(x) (x+1)\nint y = F(2);\n";
    std::string new_src = "/**/ #define F (x) (x+1)\nint y = F(2);\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
    std::string moved_old = "int f() { return 3\n/**/ #define Z\n- 1; }\n";
    std::string moved_new = "int f() { return 3\n/**/ #define Z - 1\n; }\n";
    EXPECT_FALSE(check(moved_old, moved_new).ok);
}

TEST(Format, DigraphDirectiveIsDetected) {
    EXPECT_FALSE(check("%:define F(x) (x+1)\nint y = F(2);\n", "%:define F (x) (x+1)\nint y = F(2);\n").ok);
}

TEST(Format, BareCarriageReturnEndsDirective) {
    std::string old_src = "int y = 3\n#define X\r- 1\n;\n";
    std::string new_src = "int y = 3\n#define X - 1\n;\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, MacroNameProducedByExpansionIsSensitive) {
    std::string prelude = "#define STR(x) #x\n#define CALL(m) m\nunsigned long strlen(const char*);\n";
    std::string old_src = prelude + "unsigned long n = strlen(CALL(STR)(a+b));\n";
    std::string new_src = prelude + "unsigned long n = strlen(CALL(STR)(a + b));\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, KeywordNamedMacroIsSensitive) {
    std::string old_src = "#define auto(x) #x\nconst char* s = auto(a+b);\n";
    std::string new_src = "#define auto(x) #x\nconst char* s = auto(a + b);\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, InactiveBlockLayoutChangeIsRejected) {
    std::string old_src = "#define STR(x) #x\nnamespace n {\n#ifdef DBG\nconst char* s = STR(a+b);\n#endif\n}\n";
    std::string new_src = "#define STR(x) #x\nnamespace n {\n#ifdef DBG\nconst char* s = STR(a + b);\n#endif\n}\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, ParenInsideDirectiveDoesNotCloseGroup) {
    std::string old_src = "#define STR(x) #x\nconst char* s = STR(a\n#define RP )\n+b);\n";
    std::string new_src = "#define STR(x) #x\nconst char* s = STR(a\n#define RP )\n+ b);\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, KeywordParensStayReformattable) {
    std::string old_src = "int f(int a, int b) { if (a+b>0) return sizeof(a)+1; return 0; }\n";
    std::string new_src = "int f(int a, int b) {\n  if (a + b > 0) return sizeof(a) + 1;\n  return 0;\n}\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}

TEST(Format, MacroOpeningParenStartsSensitiveGroup) {
    std::string old_src = "#define STR(x) #x\n#define OPEN STR(\nconst char* s = OPEN a+b);\n";
    std::string new_src = "#define STR(x) #x\n#define OPEN STR(\nconst char* s = OPEN a + b);\n";
    EXPECT_FALSE(check(old_src, new_src).ok);
}

TEST(Format, BalancedMacroDoesNotOverreach) {
    std::string old_src = "#define TWICE(v) ((v) * 2)\nint f(int a) { return TWICE(a)+a*3; }\n";
    std::string new_src = "#define TWICE(v) ((v) * 2)\nint f(int a) { return TWICE(a) + a * 3; }\n";
    EXPECT_TRUE(check(old_src, new_src).ok);
}
