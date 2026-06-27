// bindings/wasm/test_nam_wasm.js
//
// Minimal dependency-free smoke test for the WASM bindings. Run with:
//   node bindings/wasm/test_nam_wasm.js   (from the build output dir)
// Mirrors the C++/Python honesty commitments at the JS boundary.
'use strict';

const createNam = require('./nam_wasm.js');
const loadNam = require('./nam.js');

function check(cond, msg) {
    if (!cond) throw new Error(msg);
}

function eq(a, b) {
    return JSON.stringify(a) === JSON.stringify(b);
}

(async () => {
    const nam = await loadNam(createNam);
    const tests = {
        test_rational_digits() {
            const n = nam.rational(1, 7, 10);
            check(eq(n.digits(12), [1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7]),
                '1/7 digits');
        },
        test_codec_base_projection() {
            const q = nam.rational(1, 4, 10);
            check(eq(q.in_base(2).digits(4), [0, 1, 0, 0]), '1/4 base 2');
            check(eq(q.in_base(4).digits(4), [1, 0, 0, 0]), '1/4 base 4');
            check(q.base() === 10, 'original base unchanged');
        },
        test_precision_context_scoped() {
            check(nam.current_precision() === 30, 'default precision');
            nam.precision_context(8, () => {
                check(nam.current_precision() === 8, 'scoped precision');
                check(nam.rational(1, 7, 10).digits().length === 8,
                    'ctx digit count');
            });
            check(nam.current_precision() === 30, 'restored precision');
        },
        test_fork_value_semantics() {
            const n = nam.rational(1, 7, 10);
            check(n.fork_cost() === 'O(1)', 'automaton fork cost');
            const [a, b] = n.fork();
            check(eq(a.digits(20), b.digits(20)), 'fork determinism exact');
            check(nam.e(10).fork_cost() === 'O(log n)', 'series fork cost');
        },
        test_comparison_tri_state() {
            const x = nam.rational(1, 7, 10);
            const y = nam.rational(1, 3, 10);
            check(x.definitely_less_than(y, 5) === true, '1/7 < 1/3');
            const z = nam.rational(2, 14, 10);
            check(x.compare(z, 30) === null, 'indistinguishable -> null');
            check(x.agrees_with(z, 30), '1/7 agrees with 2/14');
        },
        test_skip() {
            const s = nam.rational(1, 7, 10).skip(1000);
            check(eq(s.digits(6), [5, 7, 1, 4, 2, 8]), 'skip 1000 of 1/7');
            check(nam.e(10).skip(10) === null, 'series skip is null');
        },
        test_render() {
            check(nam.rational(1, 4, 10).to_string(4) === '0.2500',
                'render 1/4');
        },
    };

    let failures = 0;
    for (const name of Object.keys(tests).sort()) {
        try {
            tests[name]();
            console.log(`[ OK  ] ${name}`);
        } catch (exc) {
            failures += 1;
            console.log(`[FAIL ] ${name}: ${exc.message}`);
        }
    }
    const total = Object.keys(tests).length;
    console.log(`\n${total} tests, ${failures} failures`);
    process.exit(failures ? 1 : 0);
})();