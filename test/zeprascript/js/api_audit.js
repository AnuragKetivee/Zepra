// ===========================================================================
// ZepraScript Minimal API Check
// ===========================================================================

var passed = 0, failed = 0;

function check(name, condition) {
    if (condition) {
        passed++;
        console.log("✓ " + name);
    } else {
        failed++;
        console.log("✗ " + name);
    }
}

console.log("=== Core Types ===");
check("Boolean", typeof Boolean === 'function');
check("Number", typeof Number === 'function');
check("String", typeof String === 'function');
check("Object", typeof Object === 'function');
check("Array", typeof Array === 'function');
check("Function", typeof Function === 'function');
check("Symbol", typeof Symbol === 'function');
check("BigInt", typeof BigInt === 'function');
check("Map", typeof Map === 'function');
check("Set", typeof Set === 'function');
check("WeakMap", typeof WeakMap === 'function');
check("WeakSet", typeof WeakSet === 'function');
check("Promise", typeof Promise === 'function');
check("Proxy", typeof Proxy === 'function');
check("Reflect", typeof Reflect === 'object');
check("Date", typeof Date === 'function');
check("RegExp", typeof RegExp === 'function');
check("Error", typeof Error === 'function');
check("TypeError", typeof TypeError === 'function');
check("ArrayBuffer", typeof ArrayBuffer === 'function');
check("DataView", typeof DataView === 'function');
check("Uint8Array", typeof Uint8Array === 'function');
check("Int32Array", typeof Int32Array === 'function');
check("Float64Array", typeof Float64Array === 'function');

console.log("\n=== Object Methods ===");
check("Object.keys", typeof Object.keys === 'function');
check("Object.values", typeof Object.values === 'function');
check("Object.entries", typeof Object.entries === 'function');
check("Object.assign", typeof Object.assign === 'function');
check("Object.freeze", typeof Object.freeze === 'function');
check("Object.create", typeof Object.create === 'function');

console.log("\n=== Static Methods ===");
check("Array.isArray", typeof Array.isArray === 'function');
check("Array.from", typeof Array.from === 'function');
check("Array.of", typeof Array.of === 'function');
check("JSON.parse", typeof JSON.parse === 'function');
check("JSON.stringify", typeof JSON.stringify === 'function');
check("Math.abs", typeof Math.abs === 'function');
check("Math.floor", typeof Math.floor === 'function');
check("Math.ceil", typeof Math.ceil === 'function');
check("Math.random", typeof Math.random === 'function');

console.log("\n=== Globals ===");
check("console", typeof console !== 'undefined');
check("setTimeout", typeof setTimeout === 'function');
check("setInterval", typeof setInterval === 'function');
check("isNaN", typeof isNaN === 'function');
check("isFinite", typeof isFinite === 'function');
check("parseInt", typeof parseInt === 'function');
check("parseFloat", typeof parseFloat === 'function');
check("encodeURI", typeof encodeURI === 'function');
check("decodeURI", typeof decodeURI === 'function');

console.log("\n=== Browser APIs ===");
check("fetch", typeof fetch === 'function');
check("URL", typeof URL === 'function');
check("TextEncoder", typeof TextEncoder === 'function');
check("TextDecoder", typeof TextDecoder === 'function');
check("Blob", typeof Blob === 'function');
check("FormData", typeof FormData === 'function');
check("AbortController", typeof AbortController === 'function');
check("Event", typeof Event === 'function');

console.log("\n=== Prototype Methods ===");
var a = [1, 2, 3];
check("Array.push works", a.push(4) === 4);
check("Array.pop works", a.pop() === 4);
check("Array.length", a.length === 3);

var s = "hello";
check("String.length", s.length === 5);
check("String.toUpperCase", s.toUpperCase() === "HELLO");
check("String.trim", "  hi  ".trim() === "hi");

console.log("\n=== Summary ===");
console.log("Passed: " + passed + ", Failed: " + failed);
console.log("Rate: " + Math.floor(passed / (passed + failed) * 100) + "%");
