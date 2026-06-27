// bindings/wasm/calculator.js
//
// Interactive calculator / REPL for the nam Number library, exposing the
// ergonomic user layer (constructors, codecs, scoped precision, honest
// comparison, fork, skip, rendering) over a friendly command line.
//
// Run from the WASM build output dir:
//     node bindings/wasm/calculator.js
//
// Design notes
// ------------
//   - Every honesty commitment of the library is preserved verbatim: the
//     calculator NEVER fabricates a digit. Comparison reports null as
//     `pending`, skip on a series tier reports null, and fork annotates
//     its cost tier.
//   - Numbers live in named registers (variables). The prompt lets you
//     build, name, transform and inspect them interactively.
//   - Constants and rationals are first-class; a small expression-free
//     command grammar keeps the parsing honest and predictable.
'use strict';

const readline = require('readline');
const createNam = require('./nam_wasm.js');
const loadNam = require('./nam.js');

// ---- tiny help text -------------------------------------------------------

const HELP = `
nam interactive calculator — commands

Constructors (store into a register with  >name):
  rational P Q [BASE]        rational P/Q (default base 10)
  sqrt D [BASE]              square root of integer D
  padic A B P                p-adic A/B in Z_p
  e [BASE]                   Euler's number
  ln2 [BASE]                 natural log of 2
  one_over_e [BASE]          1/e
  pi_quarter [BASE]          pi/4
  catalan [BASE]             Catalan's constant

Inspection / transforms (operate on a register or the last result):
  digits NAME [N]            first N digits (N defaults to ctx precision)
  string NAME [N]            rendered decimal string with N fractional digits
  base NAME                  the register's codec base
  in_base NAME B [>out]      reproject NAME into base B
  fork NAME                  fork (reports cost tier), stores halves a,b
  fork_cost NAME             annotate the fork cost tier
  skip NAME K [>out]         skip K digits (periodic automata only)
  streaming NAME [>out]      streaming memoization mode
  cached NAME N [>out]       cached(N) memoization mode

Honest comparison (tri-state):
  compare A B [MAXD]         -1 | 1 | pending(null)
  less A B [MAXD]            true | false | pending(null)
  agrees A B [MAXD]          do A and B agree over MAXD digits?

Precision context (scoped / RAII):
  precision                  show current precision
  precision N CMD...         run one command at precision N, then restore

Registers / misc:
  let NAME = CONSTRUCTOR...   alias for  CONSTRUCTOR... >NAME
  vars                        list registers
  drop NAME                   remove a register
  help                        this text
  quit / exit                 leave

The result of any constructor or transform is also kept in the special
register  _  (the "last result"). Reference it like any other name.
`;

// ---- argument helpers -----------------------------------------------------

function isInt(tok) {
    return /^-?\d+$/.test(tok);
}

function asInt(tok, what) {
    if (!isInt(tok)) throw new Error(`expected integer for ${what}, got "${tok}"`);
    return parseInt(tok, 10);
}

// Strip a trailing  >name  store target from a token list. Returns the
// store name (or null) and the remaining tokens.
function extractStore(tokens) {
    const out = [];
    let store = null;
    for (const t of tokens) {
        if (t.startsWith('>')) {
            store = t.slice(1);
            if (!store) throw new Error('empty store target after ">"');
        } else {
            out.push(t);
        }
    }
    return {store, rest: out};
}

(async () => {
    const nam = await loadNam(createNam);

    // Register table. `_` holds the last produced Number.
    const regs = Object.create(null);

    function setLast(num) {
        regs['_'] = num;
        return num;
    }

    // Resolve a name to a wrapped Number, with a friendly error.
    function lookup(name) {
        const n = regs[name];
        if (!n) throw new Error(`unknown register "${name}" (try: vars)`);
        return n;
    }

    // Pretty-print a tri-state comparison value.
    function triStr(v) {
        if (v === null) return 'pending (null)';
        return String(v);
    }

    function boolStr(v) {
        if (v === null) return 'pending (null)';
        return String(v);
    }

    // ---- constructor dispatch --------------------------------------------
    // Returns a wrapped Number or throws.
    function construct(cmd, args) {
        switch (cmd) {
            case 'rational': {
                const p = asInt(args[0], 'P');
                const q = asInt(args[1], 'Q');
                const base = args[2] !== undefined ? asInt(args[2], 'BASE') : 10;
                return nam.rational(p, q, base);
            }
            case 'sqrt': {
                const d = asInt(args[0], 'D');
                const base = args[1] !== undefined ? asInt(args[1], 'BASE') : 10;
                return nam.sqrt(d, base);
            }
            case 'padic': {
                const a = asInt(args[0], 'A');
                const b = asInt(args[1], 'B');
                const p = asInt(args[2], 'P');
                return nam.padic(a, b, p);
            }
            case 'e':
            case 'ln2':
            case 'one_over_e':
            case 'pi_quarter':
            case 'catalan': {
                const base = args[0] !== undefined ? asInt(args[0], 'BASE') : 10;
                return nam[cmd](base);
            }
            default:
                return null; // not a constructor
        }
    }

    const CONSTRUCTORS = new Set([
        'rational', 'sqrt', 'padic', 'e', 'ln2',
        'one_over_e', 'pi_quarter', 'catalan',
    ]);

    // ---- top-level command evaluation ------------------------------------
    // Returns a human-readable string to print (or '' for silence).
    function evaluate(tokens) {
        if (tokens.length === 0) return '';

        // `let NAME = CONSTRUCTOR...`  sugar.
        if (tokens[0] === 'let') {
            const eq = tokens.indexOf('=');
            if (eq !== 2) throw new Error('usage: let NAME = CONSTRUCTOR...');
            const name = tokens[1];
            const num = setLast(dispatchConstruct(tokens.slice(3)));
            regs[name] = num;
            return `${name} := ${num.toString()}`;
        }

        // `precision` and scoped `precision N CMD...`.
        if (tokens[0] === 'precision') {
            if (tokens.length === 1) {
                return `current precision: ${nam.current_precision()}`;
            }
            const n = asInt(tokens[1], 'N');
            let inner = '';
            nam.precision_context(n, () => {
                inner = evaluate(tokens.slice(2));
            });
            return `[at precision ${n}] ${inner}\n` +
                `precision restored: ${nam.current_precision()}`;
        }

        const cmd = tokens[0];
        const rawArgs = tokens.slice(1);

        // Constructor commands (optionally stored with >name).
        if (CONSTRUCTORS.has(cmd)) {
            const {store, rest} = extractStore(rawArgs);
            const num = setLast(construct(cmd, rest));
            if (store) regs[store] = num;
            return (store ? `${store} := ` : '') + num.toString();
        }

        switch (cmd) {
            case 'help':
                return HELP;

            case 'vars': {
                const names = Object.keys(regs).sort();
                if (names.length === 0) return '(no registers)';
                return names
                    .map((k) => `  ${k} = ${regs[k].toString()}`)
                    .join('\n');
            }

            case 'drop': {
                const name = rawArgs[0];
                if (!(name in regs)) throw new Error(`no such register "${name}"`);
                delete regs[name];
                return `dropped ${name}`;
            }

            case 'digits': {
                const num = lookup(rawArgs[0]);
                const n = rawArgs[1] !== undefined
                    ? asInt(rawArgs[1], 'N') : undefined;
                const arr = n === undefined ? num.digits() : num.digits(n);
                return `[${arr.join(', ')}]`;
            }

            case 'string': {
                const num = lookup(rawArgs[0]);
                const n = rawArgs[1] !== undefined ? asInt(rawArgs[1], 'N') : 6;
                return num.to_string(n);
            }

            case 'base': {
                const num = lookup(rawArgs[0]);
                return `base ${num.base()}`;
            }

            case 'in_base': {
                const {store, rest} = extractStore(rawArgs);
                const num = lookup(rest[0]);
                const b = asInt(rest[1], 'B');
                const out = setLast(num.in_base(b));
                if (store) regs[store] = out;
                return (store ? `${store} := ` : '') + out.toString();
            }

            case 'fork': {
                const num = lookup(rawArgs[0]);
                const [a, b] = num.fork();
                regs['a'] = a;
                regs['b'] = b;
                setLast(a);
                return `forked (cost ${num.fork_cost()}) into a, b\n` +
                    `  a = ${a.toString()}\n  b = ${b.toString()}`;
            }

            case 'fork_cost': {
                const num = lookup(rawArgs[0]);
                return `fork cost: ${num.fork_cost()}`;
            }

            case 'skip': {
                const {store, rest} = extractStore(rawArgs);
                const num = lookup(rest[0]);
                const k = asInt(rest[1], 'K');
                const out = num.skip(k);
                if (out === null) {
                    return 'skip pending (null) — no fast-forward path ' +
                        '(non-periodic tier)';
                }
                setLast(out);
                if (store) regs[store] = out;
                return (store ? `${store} := ` : '') + out.toString();
            }

            case 'streaming': {
                const {store, rest} = extractStore(rawArgs);
                const num = lookup(rest[0]);
                const out = setLast(num.streaming());
                if (store) regs[store] = out;
                return (store ? `${store} := ` : '') + out.toString();
            }

            case 'cached': {
                const {store, rest} = extractStore(rawArgs);
                const num = lookup(rest[0]);
                const n = asInt(rest[1], 'N');
                const out = setLast(num.cached(n));
                if (store) regs[store] = out;
                return (store ? `${store} := ` : '') + out.toString();
            }

            case 'compare': {
                const a = lookup(rawArgs[0]);
                const b = lookup(rawArgs[1]);
                const maxd = rawArgs[2] !== undefined
                    ? asInt(rawArgs[2], 'MAXD') : 30;
                return `compare: ${triStr(a.compare(b, maxd))}`;
            }

            case 'less': {
                const a = lookup(rawArgs[0]);
                const b = lookup(rawArgs[1]);
                const maxd = rawArgs[2] !== undefined
                    ? asInt(rawArgs[2], 'MAXD') : 30;
                return `less: ${boolStr(a.definitely_less_than(b, maxd))}`;
            }

            case 'agrees': {
                const a = lookup(rawArgs[0]);
                const b = lookup(rawArgs[1]);
                const maxd = rawArgs[2] !== undefined
                    ? asInt(rawArgs[2], 'MAXD') : 30;
                return `agrees: ${a.agrees_with(b, maxd)}`;
            }

            default:
                throw new Error(`unknown command "${cmd}" (try: help)`);
        }
    }

    // Construct from a bare constructor token list (for `let`).
    function dispatchConstruct(tokens) {
        const cmd = tokens[0];
        if (!CONSTRUCTORS.has(cmd)) {
            throw new Error(`"${cmd}" is not a constructor`);
        }
        const {rest} = extractStore(tokens.slice(1));
        return construct(cmd, rest);
    }

    // ---- REPL loop --------------------------------------------------------
    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        prompt: 'nam> ',
    });

    console.log('nam interactive calculator. type "help" for commands, ' +
        '"quit" to exit.');
    console.log(`precision = ${nam.current_precision()} digits\n`);
    rl.prompt();

    rl.on('line', (line) => {
        const trimmed = line.trim();
        if (trimmed === '') {
            rl.prompt();
            return;
        }
        if (trimmed === 'quit' || trimmed === 'exit') {
            rl.close();
            return;
        }
        try {
            const tokens = trimmed.split(/\s+/);
            const out = evaluate(tokens);
            if (out) console.log(out);
        } catch (exc) {
            console.log(`error: ${exc.message}`);
        }
        rl.prompt();
    });

    rl.on('close', () => {
        console.log('bye.');
        process.exit(0);
    });
})();