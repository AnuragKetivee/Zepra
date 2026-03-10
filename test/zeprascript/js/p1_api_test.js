// P1 API Test Suite for ZepraScript
// Tests String/Array prototype methods, TypedArray operations, etc.

console.log("=== P1 API Test Suite ===\n");

var passed = 0;
var failed = 0;

function test(name, result) {
    if (result) {
        console.log("✓ " + name);
        passed = passed + 1;
    } else {
        console.log("✗ " + name);
        failed = failed + 1;
    }
}

// String Prototype Tests
console.log("\n=== String Prototype ===");
test("split", "a,b,c".split(",").length === 3);
test("replace", "hello".replace("l", "x") === "hexlo");
test("includes", "hello".includes("ell"));
test("startsWith", "hello".startsWith("hel"));
test("endsWith", "hello".endsWith("lo"));
test("trim", "  hello  ".trim() === "hello");
test("toLowerCase", "HELLO".toLowerCase() === "hello");
test("toUpperCase", "hello".toUpperCase() === "HELLO");
test("slice", "hello".slice(1, 4) === "ell");
test("substring", "hello".substring(1, 4) === "ell");
test("indexOf", "hello".indexOf("l") === 2);
test("charAt", "hello".charAt(1) === "e");
test("repeat", "ab".repeat(3) === "ababab");
test("padStart", "5".padStart(3, "0") === "005");
test("padEnd", "5".padEnd(3, "0") === "500");

// Array Prototype Tests
console.log("\n=== Array Prototype ===");
var arr = [1, 2, 3, 4, 5];
test("map", arr.map(function(x) { return x * 2; }).join(",") === "2,4,6,8,10");
test("filter", arr.filter(function(x) { return x > 2; }).length === 3);
test("reduce", arr.reduce(function(a, b) { return a + b; }, 0) === 15);
test("find", arr.find(function(x) { return x > 3; }) === 4);
test("findIndex", arr.findIndex(function(x) { return x > 3; }) === 3);
test("some", arr.some(function(x) { return x > 4; }));
test("every", arr.every(function(x) { return x > 0; }));
test("includes", arr.includes(3));
test("indexOf", arr.indexOf(3) === 2);
test("join", arr.join("-") === "1-2-3-4-5");
test("slice", arr.slice(1, 3).length === 2);
test("concat", arr.concat([6, 7]).length === 7);
test("push/pop", (function() {
    var a = [1, 2];
    a.push(3);
    return a.pop() === 3 && a.length === 2;
})());

// TypedArray Tests
console.log("\n=== TypedArray ===");
test("Uint8Array constructor", typeof Uint8Array === "function");
test("Uint8Array length", new Uint8Array(10).length === 10);
test("Int32Array constructor", typeof Int32Array === "function");
test("Float64Array constructor", typeof Float64Array === "function");
test("ArrayBuffer constructor", typeof ArrayBuffer === "function");
test("DataView constructor", typeof DataView === "function");

// ES6+ Features
console.log("\n=== ES6+ Features ===");
test("Map exists", typeof Map === "function");
test("Set exists", typeof Set === "function");
test("Symbol exists", typeof Symbol === "function");
test("Promise exists", typeof Promise === "function");
test("Array.isArray", Array.isArray([1, 2, 3]));
test("Array.from", Array.from([1, 2, 3]).length === 3);
test("Object.keys", Object.keys({a: 1, b: 2}).length === 2);
test("Object.values", Object.values({a: 1, b: 2}).length === 2);
test("Object.entries", Object.entries({a: 1}).length === 1);

// Number Methods
console.log("\n=== Number Methods ===");
test("Number.isNaN", Number.isNaN(NaN));
test("Number.isFinite", Number.isFinite(42));
test("Number.isInteger", Number.isInteger(42));
test("Number.MAX_VALUE", Number.MAX_VALUE > 0);

// JSON
console.log("\n=== JSON ===");
test("JSON.stringify object", JSON.stringify({a: 1}) === '{"a":1}');
test("JSON.parse object", JSON.parse('{"a":1}').a === 1);
test("JSON.stringify array", JSON.stringify([1, 2]) === "[1,2]");

// Math
console.log("\n=== Math ===");
test("Math.PI", Math.PI > 3.14);
test("Math.abs", Math.abs(-5) === 5);
test("Math.floor", Math.floor(3.7) === 3);
test("Math.ceil", Math.ceil(3.2) === 4);
test("Math.round", Math.round(3.5) === 4);
test("Math.max", Math.max(1, 2, 3) === 3);
test("Math.min", Math.min(1, 2, 3) === 1);

// Summary
console.log("\n=== Summary ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
console.log("Total: " + (passed + failed));
