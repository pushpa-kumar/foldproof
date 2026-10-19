"""Generate (old, new) C++ program pairs: safe mechanical edits and near-miss behaviour changes."""

import random

NAMES = ["acc", "tmp", "val", "count", "total", "idx", "buf", "res", "cur", "sum", "lim", "step", "out", "w", "k"]
PRELUDE = "#include <cstdio>\n#include <cstring>\n"


def pick_names(rng, n):
    return rng.sample(NAMES, n)


def filler(rng):
    a, b = pick_names(rng, 2)
    c = rng.randint(2, 9)
    return f"static int filler_{rng.randint(0, 999)}(int {a}) {{ int {b} = {a} * {c}; return {b} - {a}; }}\n"


def program(body, calls):
    prints = "".join(f'    std::printf("%d\\n", (int)({call}));\n' for call in calls)
    return PRELUDE + body + "int main() {\n" + prints + "    return 0;\n}\n"


def compact_fn(name, params, local, expr, ret):
    return f"int {name}({params}){{int {local}={expr};return {ret};}}\n"


def expanded_fn(name, params, local, expr, ret):
    return f"int {name}({params}) {{\n    int {local} = {expr};\n    return {ret};\n}}\n"


def safe_reformat(rng):
    p, l = pick_names(rng, 2)
    k = rng.randint(2, 9)
    arg = rng.randint(1, 50)
    pre = filler(rng)
    old = program(pre + compact_fn("f", f"int {p}", l, f"{p}*{k}+1", f"{l}-{p}"), [f"f({arg})"])
    new = program(pre + expanded_fn("f", f"int {p}", l, f"{p} * {k} + 1", f"{l} - {p}"), [f"f({arg})"])
    return old, new


def safe_rename(rng):
    p, l, l2 = pick_names(rng, 3)
    k = rng.randint(2, 9)
    old = program(expanded_fn("f", f"int {p}", l, f"{p} * {k}", f"{l} + {l} / 2"), ["f(7)"])
    new = program(expanded_fn("f", f"int {p}", l2, f"{p} * {k}", f"{l2} + {l2} / 2"), ["f(7)"])
    return old, new


def safe_move(rng):
    a = f"int helper(int x) {{ return x * {rng.randint(2, 9)}; }}\n"
    b = f"int other(int y) {{ return y + {rng.randint(1, 9)}; }}\n"
    c = "int run(int v) { return helper(v) + other(v); }\n"
    return program(a + b + c, ["run(5)"]), program(b + a + c, ["run(5)"])


def safe_move_rename(rng):
    l, l2 = pick_names(rng, 2)
    a = f"int helper(int x) {{\n    int {l} = x * 3;\n    return {l} + 1;\n}}\n"
    a2 = f"int helper(int x) {{\n    int {l2} = x * 3;\n    return {l2} + 1;\n}}\n"
    b = f"int other(int y) {{ return y - {rng.randint(1, 9)}; }}\n"
    c = "int run(int v) { return helper(v) * other(v); }\n"
    return program(a + b + c, ["run(4)"]), program(b + a2 + c, ["run(4)"])


def safe_blank_lines(rng):
    body = "int f(int a) { return a + 1; }\n" + filler(rng)
    return program(body, ["f(2)"]), program(body.replace("\n", "\n\n", 1), ["f(2)"])


def nm_rename_capture_global(rng):
    g, l = pick_names(rng, 2)
    pre = f"int {g} = {rng.randint(50, 99)};\n"
    old = pre + f"int f(int k) {{\n    int {l} = k;\n    return {l} + {g};\n}}\n"
    new = pre + f"int f(int k) {{\n    int {g} = k;\n    return {g} + {g};\n}}\n"
    return program(old, ["f(3)"]), program(new, ["f(3)"])


def nm_rename_partial(rng):
    x, y = pick_names(rng, 2)
    pre = f"int {x} = {rng.randint(50, 99)};\n"
    old = pre + f"int f() {{\n    int {x} = 4;\n    return {x} * 2 + {x};\n}}\n"
    new = pre + f"int f() {{\n    int {y} = 4;\n    return {y} * 2 + {x};\n}}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_swap_uses(rng):
    a, b = pick_names(rng, 2)
    old = f"int f(int {a}, int {b}) {{\n    return {a} - {b};\n}}\n"
    new = f"int f(int {a}, int {b}) {{\n    return {b} - {a};\n}}\n"
    return program(old, ["f(9, 2)"]), program(new, ["f(9, 2)"])


def nm_reformat_flip(rng):
    k = rng.randint(3, 9)
    old = f"int f(int n){{if(n<{k})return 1;return 0;}}\n"
    new = f"int f(int n) {{\n    if (n <= {k}) return 1;\n    return 0;\n}}\n"
    return program(old, [f"f({k})"]), program(new, [f"f({k})"])


def nm_reformat_literal(rng):
    k = rng.randint(2, 9)
    old = f"int f(int x){{return x*{k}+1;}}\n"
    new = f"int f(int x) {{\n    return x * {k} + 2;\n}}\n"
    return program(old, ["f(3)"]), program(new, ["f(3)"])


def nm_stringize_spacing(rng):
    a, b = pick_names(rng, 2)
    pre = "#define STR(x) #x\n"
    old = pre + f"int f() {{ return (int)std::strlen(STR({a}+{b})); }}\n"
    new = pre + f"int f() {{\n    return (int)std::strlen(STR({a} + {b}));\n}}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_rename_in_stringize(rng):
    l, l2 = rng.sample([n for n in NAMES if len(n) > 1], 2)
    while len(l) == len(l2):
        l2 = rng.choice([n for n in NAMES if len(n) > 1 and n != l])
    pre = "#define NAMELEN(v) ((int)std::strlen(#v) + (v))\n"
    old = pre + f"int f() {{\n    int {l} = 1;\n    return NAMELEN({l});\n}}\n"
    new = pre + f"int f() {{\n    int {l2} = 1;\n    return NAMELEN({l2});\n}}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_move_past_overload(rng):
    k = rng.randint(2, 9)
    pick_long = "int pick(long) { return 1; }\n"
    use = "int use() { return pick(1); }\n"
    pick_int = f"int pick(int) {{ return {k}; }}\n"
    return program(pick_long + use + pick_int, ["use()"]), program(pick_long + pick_int + use, ["use()"])


def nm_move_across_macro(rng):
    k1, k2 = rng.sample(range(1, 9), 2)
    old = f"#define K {k1}\nint a() {{ return K; }}\n#undef K\n#define K {k2}\nint b() {{ return K; }}\n"
    new = f"#define K {k1}\nint b() {{ return K; }}\n#undef K\n#define K {k2}\nint a() {{ return K; }}\n"
    return program(old, ["a()", "b()"]), program(new, ["a()", "b()"])


def nm_directive_space(rng):
    k = rng.randint(1, 9)
    old = f"#define F(x) (x+{k})\nint f() {{ return F(1); }}\n"
    new = f"#define F (x) (x+{k})\nint f() {{ return F(1); }}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_rename_into_macro_ref(rng):
    pre = f"int x = {rng.randint(50, 99)};\n#define USE_X (x + 1)\n"
    old = pre + "int f() {\n    int y = 1;\n    return USE_X + y;\n}\n"
    new = pre + "int f() {\n    int x = 1;\n    return USE_X + x;\n}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_counter_swap(rng):
    a = "int a() { return __COUNTER__; }\n"
    b = "int b() { return __COUNTER__; }\n"
    return program(a + b, ["a()", "b()"]), program(b + a, ["a()", "b()"])


def nm_shadow_nested(rng):
    a, b = pick_names(rng, 2)
    old = f"int f() {{\n    int {a} = 1;\n    {{ int {b} = 2; {a} += {b}; }}\n    return {a};\n}}\n"
    new = f"int f() {{\n    int {a} = 1;\n    {{ int {a} = 2; {a} += {a}; }}\n    return {a};\n}}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_lambda_shadow(rng):
    old = "int f() {\n    int base = 3;\n    auto g = [&](int v) { return base + v; };\n    return g(1);\n}\n"
    new = "int f() {\n    int base = 3;\n    auto g = [&](int base) { return base + base; };\n    return g(1);\n}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_move_line(rng):
    a = "int a() { return 1; }\n"
    b = "int b() { return __LINE__; }\n"
    return program(a + b, ["b()"]), program(b + a, ["b()"])


def nm_reformat_above_line(rng):
    k = rng.randint(2, 9)
    old = f"int f(int x){{return x*{k};}}\nint where() {{ return __LINE__; }}\n"
    new = f"int f(int x) {{\n    return x * {k};\n}}\nint where() {{ return __LINE__; }}\n"
    return program(old, ["f(2) + where()"]), program(new, ["f(2) + where()"])


def nm_operator_move(rng):
    k = rng.randint(10, 99)
    st = "struct A { operator int() const { return 1; } };\n"
    use = "int use() { A a; return a + 1; }\n"
    op = f"int operator+(A, int) {{ return {k}; }}\n"
    return program(st + use + op, ["use()"]), program(st + op + use, ["use()"])


def nm_token_paste(rng):
    pre = "int tmp = 100;\n#define CAT(x, y) x##y\n"
    old = pre + "int f() {\n    int tmp = 5;\n    return CAT(t, mp);\n}\n"
    new = pre + "int f() {\n    int val = 5;\n    return CAT(t, mp);\n}\n"
    return program(old, ["f()"]), program(new, ["f()"])


def nm_string_space(rng):
    old = 'int f() { return (int)std::strlen("a b"); }\n'
    new = 'int f() {\n    return (int)std::strlen("a  b");\n}\n'
    return program(old, ["f()"]), program(new, ["f()"])


def nm_pragma_region(rng):
    hello = 'void hello() { std::printf("side effect\\n"); }\n'
    push = "#pragma clang attribute push(__attribute__((constructor)), apply_to = function)\nvoid other() {}\n"
    pop = "#pragma clang attribute pop\n"
    return program(hello + push + pop, ["0"]), program(push + hello + pop, ["0"])


SAFE = [safe_reformat, safe_rename, safe_move, safe_move_rename, safe_blank_lines]
NEAR_MISS = [nm_rename_capture_global, nm_rename_partial, nm_swap_uses, nm_reformat_flip, nm_reformat_literal,
             nm_stringize_spacing, nm_rename_in_stringize, nm_move_past_overload, nm_move_across_macro,
             nm_directive_space, nm_rename_into_macro_ref, nm_counter_swap, nm_shadow_nested, nm_lambda_shadow,
             nm_move_line, nm_reformat_above_line, nm_operator_move, nm_token_paste, nm_string_space, nm_pragma_region]


def with_noise(rng, old, new):
    a, b = pick_names(rng, 2)
    k = rng.randint(2, 9)
    name = f"noise_{rng.randint(0, 9999)}"
    compact = compact_fn(name, f"int {a}", b, f"{a}*{k}", f"{b}+{a}")
    expanded = expanded_fn(name, f"int {a}", b, f"{a} * {k}", f"{b} + {a}")
    cut_old, cut_new = old.index("int main()"), new.index("int main()")
    return old[:cut_old] + compact + old[cut_old:], new[:cut_new] + expanded + new[cut_new:]


def generate(variants, seed=7):
    rng = random.Random(seed)
    cases = []
    for category, rules in (("safe", SAFE), ("near_miss", NEAR_MISS)):
        for rule in rules:
            seen = set()
            for i in range(variants):
                old, new = rule(rng)
                if rng.random() < 0.6:
                    old, new = with_noise(rng, old, new)
                if (old, new) in seen:
                    continue
                seen.add((old, new))
                cases.append({"name": f"{rule.__name__}_{i}", "rule": rule.__name__, "category": category,
                              "old": old, "new": new})
    return cases
