# bindings/test_nam_py.py
#
# Minimal pytest-free smoke test for the Python bindings. Run with:
#   PYTHONPATH=build/bindings python bindings/test_nam_py.py
# Mirrors the C++ honesty commitments at the language boundary.
import nam


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


def test_rational_digits():
    n = nam.rational(1, 7, 10)
    check(n.digits(12) == [1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7],
          "1/7 digits")


def test_codec_base_projection():
    q = nam.rational(1, 4, 10)
    check(q.in_base(2).digits(4) == [0, 1, 0, 0], "1/4 base 2")
    check(q.in_base(4).digits(4) == [1, 0, 0, 0], "1/4 base 4")
    check(q.base() == 10, "original base unchanged")


def test_precision_context_scoped():
    check(nam.current_precision() == 30, "default precision")
    with nam.precision_context(digits=8):
        check(nam.current_precision() == 8, "scoped precision")
        check(len(nam.rational(1, 7, 10).digits()) == 8, "ctx digit count")
    check(nam.current_precision() == 30, "restored precision")


def test_fork_value_semantics():
    n = nam.rational(1, 7, 10)
    check(n.fork_cost() == "O(1)", "automaton fork cost")
    a, b = n.fork()
    check(a.digits(20) == b.digits(20), "fork determinism exact")
    check(nam.e(10).fork_cost() == "O(log n)", "series fork cost")


def test_comparison_tri_state():
    x = nam.rational(1, 7, 10)
    y = nam.rational(1, 3, 10)
    check(x.definitely_less_than(y, 5) is True, "1/7 < 1/3")
    z = nam.rational(2, 14, 10)
    check(x.compare(z, 30) is None, "indistinguishable -> None")
    check(x.agrees_with(z, 30), "1/7 agrees with 2/14")


def test_skip():
    s = nam.rational(1, 7, 10).skip(1000)
    check(s.digits(6) == [5, 7, 1, 4, 2, 8], "skip 1000 of 1/7")
    check(nam.e(10).skip(10) is None, "series skip is None")


def test_render():
    check(nam.rational(1, 4, 10).to_string(4) == "0.2500", "render 1/4")


def test_pi_quarter():
    n = nam.pi_quarter(10)
    check(n.digits(6) == [7, 8, 5, 3, 9, 8], "pi/4 digits")
    check(n.fork_cost() == "O(log n)", "pi/4 series fork cost")


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failures = 0
    for t in tests:
        try:
            t()
            print(f"[ OK  ] {t.__name__}")
        except AssertionError as exc:
            failures += 1
            print(f"[FAIL ] {t.__name__}: {exc}")
    print(f"\n{len(tests)} tests, {failures} failures")
    raise SystemExit(1 if failures else 0)