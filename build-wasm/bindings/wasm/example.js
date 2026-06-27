// bindings/wasm/example.js
//
// Ergonomic, mpmath-flavoured usage of the Number user layer from JS/WASM.
// Build:  cmake -S . -B build-wasm -DNAM_BUILD_WASM=ON \
//               -DCMAKE_TOOLCHAIN_FILE=$EMSDK/.../Emscripten.cmake
//         cmake --build build-wasm
// Run:    node bindings/wasm/example.js   (from the build output dir)
'use strict';
// For an in-browser visual demo of the p-adic ultrametric fractal, open
// padic_fractal.html from the build output dir (served over HTTP so the
// .wasm can be fetched), e.g. `python3 -m http.server`.


const createNam = require('./nam_wasm.js');
const loadNam = require('./nam.js');

(async () => {
    const nam = await loadNam(createNam);

    // Rationals: 1/7 = 0.(142857)
    console.log('1/7 ->', nam.rational(1, 7, 10).digits(12));

    // Base is a codec, not baked into the number.
    const quarter = nam.rational(1, 4, 10);
    console.log('1/4 base 2 ->', quarter.in_base(2).digits(4));
    console.log('1/4 base 4 ->', quarter.in_base(4).digits(4));
    console.log('original base unchanged:', quarter.base());

    // Scoped precision context (RAII / callback `with`).
    nam.precision_context(8, () => {
        console.log('precision in scope:', nam.current_precision());
        console.log('1/7 at ctx precision ->', nam.rational(1, 7, 10).digits());
    });
    console.log('precision restored:', nam.current_precision());

    // Fork is honest about cost and value-semantic.
    const e = nam.e(10);
    console.log('e fork cost:', e.fork_cost());
    const [a, b] = e.fork();
    console.log('forked prefixes agree:',
        JSON.stringify(a.digits(5)) === JSON.stringify(b.digits(5)));

    // Interval-honest comparison: true | false | null.
    const x = nam.rational(1, 7, 10);
    const y = nam.rational(1, 3, 10);
    console.log('1/7 < 1/3 ?', x.definitely_less_than(y, 5));   // true
    const z = nam.rational(2, 14, 10);                         // == 1/7
    console.log('1/7 vs 2/14 compare:', x.compare(z, 30));     // null

    // Skip-ahead (periodic automata only).
    console.log('skip 1000 of 1/7 ->',
        nam.rational(1, 7, 10).skip(1000).digits(6));
    console.log('series skip is null:', nam.e(10).skip(10));   // null

    // Rendering.
    console.log('1/4 ->', nam.rational(1, 4, 10).to_string(4));
})();