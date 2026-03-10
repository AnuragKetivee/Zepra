// ===========================================================================
// ZepraScript Builtin Tests
// ===========================================================================

let passed = 0, failed = 0;

function assertEqual(a, b, msg) {
    if (a === b) { passed++; console.log("✓ " + msg); }
    else { failed++; console.error("✗ " + msg + " (got: " + a + ", expected: " + b + ")"); }
}

console.log("\n=== Builtin Tests ===\n");

// ----- Math -----
console.log("--- Math ---");
assertEqual(Math.abs(-5), 5, "Math.abs(-5)");
assertEqual(Math.floor(3.7), 3, "Math.floor(3.7)");
assertEqual(Math.ceil(3.2), 4, "Math.ceil(3.2)");
assertEqual(Math.round(3.5), 4, "Math.round(3.5)");
assertEqual(Math.max(1, 5, 3), 5, "Math.max(1,5,3)");
assertEqual(Math.min(1, 5, 3), 1, "Math.min(1,5,3)");
assertEqual(Math.pow(2, 3), 8, "Math.pow(2,3)");
assertEqual(Math.sqrt(16), 4, "Math.sqrt(16)");
assertEqual(Math.sign(-10), -1, "Math.sign(-10)");
assertEqual(Math.sign(10), 1, "Math.sign(10)");

// ----- String -----
console.log("\n--- String ---");
assertEqual("hello".toUpperCase(), "HELLO", "toUpperCase");
assertEqual("HELLO".toLowerCase(), "hello", "toLowerCase");
assertEqual("  trim  ".trim(), "trim", "trim");
assertEqual("hello".indexOf("l"), 2, "indexOf");
assertEqual("hello".slice(1, 4), "ell", "slice");
assertEqual("hello".charAt(1), "e", "charAt");
assertEqual("a,b,c".split(",").length, 3, "split");
assertEqual(["a", "b", "c"].join("-"), "a-b-c", "join");
assertEqual("hello".includes("ell"), true, "includes");
assertEqual("hello".startsWith("he"), true, "startsWith");
assertEqual("hello".endsWith("lo"), true, "endsWith");
assertEqual("abc".repeat(3), "abcabcabc", "repeat");

// ----- Array -----
console.log("\n--- Array ---");
assertEqual([1,2,3].indexOf(2), 1, "indexOf");
assertEqual([1,2,3].includes(2), true, "includes");
assertEqual([1,2,3].find(x => x > 1), 2, "find");
assertEqual([1,2,3].findIndex(x => x > 1), 1, "findIndex");
assertEqual([1,2,3].every(x => x > 0), true, "every");
assertEqual([1,2,3].some(x => x > 2), true, "some");
assertEqual([1,2,3].reverse().join(","), "3,2,1", "reverse");
assertEqual([3,1,2].sort().join(","), "1,2,3", "sort");

// ----- Object -----
console.log("\n--- Object ---");
let obj = { a: 1, b: 2 };
assertEqual(Object.keys(obj).length, 2, "Object.keys");
assertEqual(Object.values(obj).includes(1), true, "Object.values");
assertEqual(Object.entries(obj).length, 2, "Object.entries");

// ----- JSON -----
console.log("\n--- JSON ---");
assertEqual(JSON.stringify({a: 1}), '{"a":1}', "JSON.stringify");
let parsed = JSON.parse('{"x":42}');
assertEqual(parsed.x, 42, "JSON.parse");

// ----- Number -----
console.log("\n--- Number ---");
assertEqual(Number.isInteger(5), true, "isInteger(5)");
assertEqual(Number.isInteger(5.5), false, "isInteger(5.5)");
assertEqual(Number.isNaN(NaN), true, "isNaN(NaN)");
assertEqual(Number.isFinite(100), true, "isFinite(100)");
assertEqual(Number.parseFloat("3.14"), 3.14, "parseFloat");
assertEqual(Number.parseInt("42"), 42, "parseInt");

// ----- Summary -----
console.log("\n=== Summary ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
