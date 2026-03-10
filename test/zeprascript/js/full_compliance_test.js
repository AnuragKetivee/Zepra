// ===========================================================================
// ZepraScript Full ECMAScript Compliance Test
// Tests ALL essential JS APIs for production sites
// ===========================================================================

let passed = 0, failed = 0, missing = [];

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log("✓ " + name);
    } catch (e) {
        failed++;
        missing.push(name);
        console.log("✗ " + name + " - " + e.message);
    }
}

function exists(obj, prop) {
    if (typeof obj[prop] === 'undefined') throw new Error('missing');
}

console.log("=== Full ECMAScript Compliance Test ===\n");

// ---------------------------------------------------------------------------
// CORE TYPES
// ---------------------------------------------------------------------------
console.log("--- Core Types ---");
test("undefined", () => { if (typeof undefined !== 'undefined') throw new Error('wrong type'); });
test("null", () => { if (null !== null) throw new Error(); });
test("Boolean", () => exists(Boolean, 'prototype'); });
test("Number", () => exists(Number, 'prototype'); });
test("String", () => exists(String, 'prototype'); });
test("Symbol", () => exists(Symbol, 'iterator'); });
test("BigInt", () => { BigInt(1); });
test("Object", () => exists(Object, 'prototype'); });
test("Array", () => exists(Array, 'prototype'); });
test("Function", () => exists(Function, 'prototype'); });

// ---------------------------------------------------------------------------
// OBJECT METHODS
// ---------------------------------------------------------------------------
console.log("\n--- Object ---");
test("Object.keys", () => exists(Object, 'keys'); });
test("Object.values", () => exists(Object, 'values'); });
test("Object.entries", () => exists(Object, 'entries'); });
test("Object.assign", () => exists(Object, 'assign'); });
test("Object.freeze", () => exists(Object, 'freeze'); });
test("Object.seal", () => exists(Object, 'seal'); });
test("Object.create", () => exists(Object, 'create'); });
test("Object.defineProperty", () => exists(Object, 'defineProperty'); });
test("Object.getOwnPropertyDescriptor", () => exists(Object, 'getOwnPropertyDescriptor'); });
test("Object.getPrototypeOf", () => exists(Object, 'getPrototypeOf'); });
test("Object.setPrototypeOf", () => exists(Object, 'setPrototypeOf'); });
test("Object.fromEntries", () => exists(Object, 'fromEntries'); });
test("Object.hasOwn", () => exists(Object, 'hasOwn'); });

// ---------------------------------------------------------------------------
// ARRAY METHODS (CRITICAL FOR SITES)
// ---------------------------------------------------------------------------
console.log("\n--- Array ---");
test("Array.isArray", () => exists(Array, 'isArray'); });
test("Array.from", () => exists(Array, 'from'); });
test("Array.of", () => exists(Array, 'of'); });
test("Array.prototype.push", () => exists(Array.prototype, 'push'); });
test("Array.prototype.pop", () => exists(Array.prototype, 'pop'); });
test("Array.prototype.shift", () => exists(Array.prototype, 'shift'); });
test("Array.prototype.unshift", () => exists(Array.prototype, 'unshift'); });
test("Array.prototype.slice", () => exists(Array.prototype, 'slice'); });
test("Array.prototype.splice", () => exists(Array.prototype, 'splice'); });
test("Array.prototype.concat", () => exists(Array.prototype, 'concat'); });
test("Array.prototype.join", () => exists(Array.prototype, 'join'); });
test("Array.prototype.reverse", () => exists(Array.prototype, 'reverse'); });
test("Array.prototype.sort", () => exists(Array.prototype, 'sort'); });
test("Array.prototype.indexOf", () => exists(Array.prototype, 'indexOf'); });
test("Array.prototype.lastIndexOf", () => exists(Array.prototype, 'lastIndexOf'); });
test("Array.prototype.includes", () => exists(Array.prototype, 'includes'); });
test("Array.prototype.find", () => exists(Array.prototype, 'find'); });
test("Array.prototype.findIndex", () => exists(Array.prototype, 'findIndex'); });
test("Array.prototype.findLast", () => exists(Array.prototype, 'findLast'); });
test("Array.prototype.map", () => exists(Array.prototype, 'map'); });
test("Array.prototype.filter", () => exists(Array.prototype, 'filter'); });
test("Array.prototype.reduce", () => exists(Array.prototype, 'reduce'); });
test("Array.prototype.reduceRight", () => exists(Array.prototype, 'reduceRight'); });
test("Array.prototype.forEach", () => exists(Array.prototype, 'forEach'); });
test("Array.prototype.every", () => exists(Array.prototype, 'every'); });
test("Array.prototype.some", () => exists(Array.prototype, 'some'); });
test("Array.prototype.flat", () => exists(Array.prototype, 'flat'); });
test("Array.prototype.flatMap", () => exists(Array.prototype, 'flatMap'); });
test("Array.prototype.fill", () => exists(Array.prototype, 'fill'); });
test("Array.prototype.copyWithin", () => exists(Array.prototype, 'copyWithin'); });
test("Array.prototype.at", () => exists(Array.prototype, 'at'); });
test("Array.prototype.toReversed", () => exists(Array.prototype, 'toReversed'); });
test("Array.prototype.toSorted", () => exists(Array.prototype, 'toSorted'); });
test("Array.prototype.toSpliced", () => exists(Array.prototype, 'toSpliced'); });
test("Array.prototype.with", () => exists(Array.prototype, 'with'); });

// ---------------------------------------------------------------------------
// STRING METHODS
// ---------------------------------------------------------------------------
console.log("\n--- String ---");
test("String.prototype.charAt", () => exists(String.prototype, 'charAt'); });
test("String.prototype.charCodeAt", () => exists(String.prototype, 'charCodeAt'); });
test("String.prototype.codePointAt", () => exists(String.prototype, 'codePointAt'); });
test("String.prototype.concat", () => exists(String.prototype, 'concat'); });
test("String.prototype.includes", () => exists(String.prototype, 'includes'); });
test("String.prototype.indexOf", () => exists(String.prototype, 'indexOf'); });
test("String.prototype.lastIndexOf", () => exists(String.prototype, 'lastIndexOf'); });
test("String.prototype.match", () => exists(String.prototype, 'match'); });
test("String.prototype.matchAll", () => exists(String.prototype, 'matchAll'); });
test("String.prototype.padStart", () => exists(String.prototype, 'padStart'); });
test("String.prototype.padEnd", () => exists(String.prototype, 'padEnd'); });
test("String.prototype.repeat", () => exists(String.prototype, 'repeat'); });
test("String.prototype.replace", () => exists(String.prototype, 'replace'); });
test("String.prototype.replaceAll", () => exists(String.prototype, 'replaceAll'); });
test("String.prototype.search", () => exists(String.prototype, 'search'); });
test("String.prototype.slice", () => exists(String.prototype, 'slice'); });
test("String.prototype.split", () => exists(String.prototype, 'split'); });
test("String.prototype.startsWith", () => exists(String.prototype, 'startsWith'); });
test("String.prototype.endsWith", () => exists(String.prototype, 'endsWith'); });
test("String.prototype.substring", () => exists(String.prototype, 'substring'); });
test("String.prototype.toLowerCase", () => exists(String.prototype, 'toLowerCase'); });
test("String.prototype.toUpperCase", () => exists(String.prototype, 'toUpperCase'); });
test("String.prototype.trim", () => exists(String.prototype, 'trim'); });
test("String.prototype.trimStart", () => exists(String.prototype, 'trimStart'); });
test("String.prototype.trimEnd", () => exists(String.prototype, 'trimEnd'); });
test("String.prototype.at", () => exists(String.prototype, 'at'); });

// ---------------------------------------------------------------------------
// MATH
// ---------------------------------------------------------------------------
console.log("\n--- Math ---");
test("Math.abs", () => exists(Math, 'abs'); });
test("Math.ceil", () => exists(Math, 'ceil'); });
test("Math.floor", () => exists(Math, 'floor'); });
test("Math.round", () => exists(Math, 'round'); });
test("Math.max", () => exists(Math, 'max'); });
test("Math.min", () => exists(Math, 'min'); });
test("Math.pow", () => exists(Math, 'pow'); });
test("Math.sqrt", () => exists(Math, 'sqrt'); });
test("Math.random", () => exists(Math, 'random'); });
test("Math.sin", () => exists(Math, 'sin'); });
test("Math.cos", () => exists(Math, 'cos'); });
test("Math.tan", () => exists(Math, 'tan'); });
test("Math.log", () => exists(Math, 'log'); });
test("Math.exp", () => exists(Math, 'exp'); });
test("Math.trunc", () => exists(Math, 'trunc'); });
test("Math.sign", () => exists(Math, 'sign'); });

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
console.log("\n--- JSON ---");
test("JSON.parse", () => exists(JSON, 'parse'); });
test("JSON.stringify", () => exists(JSON, 'stringify'); });

// ---------------------------------------------------------------------------
// DATE
// ---------------------------------------------------------------------------
console.log("\n--- Date ---");
test("Date constructor", () => { new Date(); });
test("Date.now", () => exists(Date, 'now'); });
test("Date.parse", () => exists(Date, 'parse'); });
test("Date.prototype.getTime", () => exists(Date.prototype, 'getTime'); });
test("Date.prototype.toISOString", () => exists(Date.prototype, 'toISOString'); });

// ---------------------------------------------------------------------------
// REGEXP
// ---------------------------------------------------------------------------
console.log("\n--- RegExp ---");
test("RegExp constructor", () => { new RegExp('a'); });
test("RegExp.prototype.test", () => exists(RegExp.prototype, 'test'); });
test("RegExp.prototype.exec", () => exists(RegExp.prototype, 'exec'); });

// ---------------------------------------------------------------------------
// PROMISE
// ---------------------------------------------------------------------------
console.log("\n--- Promise ---");
test("Promise constructor", () => { new Promise(() => {}); });
test("Promise.resolve", () => exists(Promise, 'resolve'); });
test("Promise.reject", () => exists(Promise, 'reject'); });
test("Promise.all", () => exists(Promise, 'all'); });
test("Promise.allSettled", () => exists(Promise, 'allSettled'); });
test("Promise.any", () => exists(Promise, 'any'); });
test("Promise.race", () => exists(Promise, 'race'); });
test("Promise.prototype.then", () => exists(Promise.prototype, 'then'); });
test("Promise.prototype.catch", () => exists(Promise.prototype, 'catch'); });
test("Promise.prototype.finally", () => exists(Promise.prototype, 'finally'); });

// ---------------------------------------------------------------------------
// MAP / SET
// ---------------------------------------------------------------------------
console.log("\n--- Map/Set ---");
test("Map constructor", () => { new Map(); });
test("Map.prototype.set", () => exists(Map.prototype, 'set'); });
test("Map.prototype.get", () => exists(Map.prototype, 'get'); });
test("Map.prototype.has", () => exists(Map.prototype, 'has'); });
test("Map.prototype.delete", () => exists(Map.prototype, 'delete'); });
test("Map.prototype.clear", () => exists(Map.prototype, 'clear'); });
test("Map.prototype.forEach", () => exists(Map.prototype, 'forEach'); });
test("Set constructor", () => { new Set(); });
test("Set.prototype.add", () => exists(Set.prototype, 'add'); });
test("Set.prototype.has", () => exists(Set.prototype, 'has'); });
test("Set.prototype.delete", () => exists(Set.prototype, 'delete'); });
test("WeakMap constructor", () => { new WeakMap(); });
test("WeakSet constructor", () => { new WeakSet(); });

// ---------------------------------------------------------------------------
// TYPED ARRAYS
// ---------------------------------------------------------------------------
console.log("\n--- TypedArrays ---");
test("ArrayBuffer", () => { new ArrayBuffer(8); });
test("DataView", () => { new DataView(new ArrayBuffer(8)); });
test("Uint8Array", () => { new Uint8Array(8); });
test("Uint16Array", () => { new Uint16Array(8); });
test("Uint32Array", () => { new Uint32Array(8); });
test("Int8Array", () => { new Int8Array(8); });
test("Int16Array", () => { new Int16Array(8); });
test("Int32Array", () => { new Int32Array(8); });
test("Float32Array", () => { new Float32Array(8); });
test("Float64Array", () => { new Float64Array(8); });
test("BigInt64Array", () => { new BigInt64Array(8); });
test("BigUint64Array", () => { new BigUint64Array(8); });

// ---------------------------------------------------------------------------
// PROXY / REFLECT
// ---------------------------------------------------------------------------
console.log("\n--- Proxy/Reflect ---");
test("Proxy constructor", () => { new Proxy({}, {}); });
test("Reflect.get", () => exists(Reflect, 'get'); });
test("Reflect.set", () => exists(Reflect, 'set'); });
test("Reflect.has", () => exists(Reflect, 'has'); });
test("Reflect.ownKeys", () => exists(Reflect, 'ownKeys'); });

// ---------------------------------------------------------------------------
// ERROR TYPES
// ---------------------------------------------------------------------------
console.log("\n--- Errors ---");
test("Error", () => { new Error(); });
test("TypeError", () => { new TypeError(); });
test("ReferenceError", () => { new ReferenceError(); });
test("SyntaxError", () => { new SyntaxError(); });
test("RangeError", () => { new RangeError(); });
test("URIError", () => { new URIError(); });
test("EvalError", () => { new EvalError(); });
test("AggregateError", () => { new AggregateError([]); });

// ---------------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------------
console.log("\n--- Globals ---");
test("globalThis", () => { if (typeof globalThis === 'undefined') throw new Error(); });
test("console", () => { if (typeof console === 'undefined') throw new Error(); });
test("setTimeout", () => { if (typeof setTimeout !== 'function') throw new Error(); });
test("setInterval", () => { if (typeof setInterval !== 'function') throw new Error(); });
test("clearTimeout", () => { if (typeof clearTimeout !== 'function') throw new Error(); });
test("clearInterval", () => { if (typeof clearInterval !== 'function') throw new Error(); });
test("queueMicrotask", () => { if (typeof queueMicrotask !== 'function') throw new Error(); });
test("atob", () => { if (typeof atob !== 'function') throw new Error(); });
test("btoa", () => { if (typeof btoa !== 'function') throw new Error(); });
test("encodeURI", () => { if (typeof encodeURI !== 'function') throw new Error(); });
test("decodeURI", () => { if (typeof decodeURI !== 'function') throw new Error(); });
test("encodeURIComponent", () => { if (typeof encodeURIComponent !== 'function') throw new Error(); });
test("decodeURIComponent", () => { if (typeof decodeURIComponent !== 'function') throw new Error(); });
test("isNaN", () => { if (typeof isNaN !== 'function') throw new Error(); });
test("isFinite", () => { if (typeof isFinite !== 'function') throw new Error(); });
test("parseInt", () => { if (typeof parseInt !== 'function') throw new Error(); });
test("parseFloat", () => { if (typeof parseFloat !== 'function') throw new Error(); });
test("eval", () => { if (typeof eval !== 'function') throw new Error(); });

// ---------------------------------------------------------------------------
// BROWSER APIS (optional but common)
// ---------------------------------------------------------------------------
console.log("\n--- Browser APIs ---");
test("fetch", () => { if (typeof fetch !== 'function') throw new Error(); });
test("URL", () => { new URL('http://example.com'); });
test("URLSearchParams", () => { new URLSearchParams(); });
test("TextEncoder", () => { new TextEncoder(); });
test("TextDecoder", () => { new TextDecoder(); });
test("Blob", () => { new Blob([]); });
test("File", () => { new File([], 'test'); });
test("FormData", () => { new FormData(); });
test("AbortController", () => { new AbortController(); });
test("Event", () => { new Event('test'); });
test("CustomEvent", () => { new CustomEvent('test'); });
test("EventTarget", () => { new EventTarget(); });

// ---------------------------------------------------------------------------
// SUMMARY
// ---------------------------------------------------------------------------
console.log("\n=== SUMMARY ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
console.log("Total:  " + (passed + failed));
console.log("Rate:   " + (passed / (passed + failed) * 100).toFixed(1) + "%");

if (missing.length > 0) {
    console.log("\n=== MISSING APIS ===");
    for (let m of missing) {
        console.log("  - " + m);
    }
}
