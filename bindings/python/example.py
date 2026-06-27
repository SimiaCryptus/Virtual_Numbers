# bindings/example.py
#
# Ergonomic, mpmath/Decimal-flavoured usage of the Number user layer.
# Build with: cmake -S . -B build -DNAM_BUILD_PYTHON=ON && cmake --build build
# Then: PYTHONPATH=build/bindings python bindings/example.py
import nam

# Rationals: 1/7 = 0.(142857)
seventh = nam.rational(1, 7, 10)
print("1/7 ->", seventh.digits(12))

# Base is a codec, not baked into the number.
quarter = nam.rational(1, 4, 10)
print("1/4 base 2 ->", quarter.in_base(2).digits(4))
print("1/4 base 4 ->", quarter.in_base(4).digits(4))
print("original base unchanged:", quarter.base())

# Scoped precision context (RAII / context manager).
with nam.precision_context(digits=8):
    print("precision in scope:", nam.current_precision())
    print("1/7 at ctx precision ->", nam.rational(1, 7, 10).digits())
print("precision restored:", nam.current_precision())

# Fork is honest about cost and value-semantic.
e = nam.e(10)
print("e fork cost:", e.fork_cost())
a, b = e.fork()
print("forked prefixes agree:", a.digits(5) == b.digits(5))

# Interval-honest comparison: True | False | None.
x = nam.rational(1, 7, 10)
y = nam.rational(1, 3, 10)
print("1/7 < 1/3 ?", x.definitely_less_than(y, 5))  # True
z = nam.rational(2, 14, 10)  # == 1/7
print("1/7 vs 2/14 compare:", x.compare(z, 30))  # None (pending)

# Skip-ahead (periodic automata only).
print("skip 1000 digits of 1/7 ->", nam.rational(1, 7, 10).skip(1000).digits(6))
print("series skip is None:", nam.e(10).skip(10))  # None

# Rendering.
print("1/4 ->", nam.rational(1, 4, 10).to_string(4))
# Catalan's constant (slowly-converging series, honest digit commitment).
print("Catalan ->", nam.catalan(10).digits(8))