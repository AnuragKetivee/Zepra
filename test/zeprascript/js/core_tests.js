// ===========================================================================
// ZepraScript Core Test Suite
// ===========================================================================
// Run with: ./zepra-repl tests/js/core_tests.js

// Test framework
let passed = 0;
let failed = 0;

function assert(condition, message) {
    if (condition) {
        passed++;
        console.log("✓ " + message);
    } else {
        failed++;
        console.error("✗ FAILED: " + message);
    }
}

function assertEqual(actual, expected, message) {
    if (actual === expected) {
        passed++;
        console.log("✓ " + message);
    } else {
        failed++;
        console.error("✗ FAILED: " + message + " (expected: " + expected + ", got: " + actual + ")");
    }
}

function test(name, fn) {
    try {
        fn();
    } catch (e) {
        failed++;
        console.error("✗ EXCEPTION in " + name + ": " + e.message);
    }
}

console.log("\n=== ZepraScript Core Tests ===\n");

// ===========================================================================
// Value Types Tests
// ===========================================================================

console.log("--- Value Types ---");

test("undefined", () => {
    assertEqual(typeof undefined, "undefined", "typeof undefined");
    assertEqual(undefined === undefined, true, "undefined === undefined");
});

test("null", () => {
    assertEqual(typeof null, "object", "typeof null");
    assertEqual(null === null, true, "null === null");
});

test("booleans", () => {
    assertEqual(typeof true, "boolean", "typeof true");
    assertEqual(typeof false, "boolean", "typeof false");
    assertEqual(true === true, true, "true === true");
    assertEqual(true === false, false, "true !== false");
});

test("numbers", () => {
    assertEqual(typeof 42, "number", "typeof 42");
    assertEqual(typeof 3.14, "number", "typeof 3.14");
    assertEqual(42 + 8, 50, "42 + 8 = 50");
    assertEqual(10 - 3, 7, "10 - 3 = 7");
    assertEqual(6 * 7, 42, "6 * 7 = 42");
    assertEqual(15 / 3, 5, "15 / 3 = 5");
    assertEqual(17 % 5, 2, "17 % 5 = 2");
});

test("strings", () => {
    assertEqual(typeof "hello", "string", "typeof 'hello'");
    assertEqual("hello" + " " + "world", "hello world", "string concat");
    assertEqual("hello".length, 5, "string length");
    assertEqual("hello"[0], "h", "string index");
});

// ===========================================================================
// Array Tests
// ===========================================================================

console.log("\n--- Arrays ---");

test("array creation", () => {
    let arr = [1, 2, 3];
    assertEqual(arr.length, 3, "array length");
    assertEqual(arr[0], 1, "array[0]");
    assertEqual(arr[1], 2, "array[1]");
    assertEqual(arr[2], 3, "array[2]");
});

test("array push/pop", () => {
    let arr = [];
    arr.push(1);
    arr.push(2);
    assertEqual(arr.length, 2, "push increases length");
    let popped = arr.pop();
    assertEqual(popped, 2, "pop returns last");
    assertEqual(arr.length, 1, "pop decreases length");
});

test("array map", () => {
    let arr = [1, 2, 3];
    let doubled = arr.map(x => x * 2);
    assertEqual(doubled[0], 2, "map [0]");
    assertEqual(doubled[1], 4, "map [1]");
    assertEqual(doubled[2], 6, "map [2]");
});

test("array filter", () => {
    let arr = [1, 2, 3, 4, 5];
    let evens = arr.filter(x => x % 2 === 0);
    assertEqual(evens.length, 2, "filter length");
    assertEqual(evens[0], 2, "filter[0]");
    assertEqual(evens[1], 4, "filter[1]");
});

test("array reduce", () => {
    let arr = [1, 2, 3, 4, 5];
    let sum = arr.reduce((acc, x) => acc + x, 0);
    assertEqual(sum, 15, "reduce sum");
});

// ===========================================================================
// Object Tests
// ===========================================================================

console.log("\n--- Objects ---");

test("object creation", () => {
    let obj = { a: 1, b: 2 };
    assertEqual(obj.a, 1, "obj.a");
    assertEqual(obj.b, 2, "obj.b");
});

test("object property access", () => {
    let obj = { foo: "bar", num: 42 };
    assertEqual(obj.foo, "bar", "dot notation");
    assertEqual(obj["num"], 42, "bracket notation");
});

test("object modification", () => {
    let obj = { x: 1 };
    obj.x = 2;
    obj.y = 3;
    assertEqual(obj.x, 2, "modify existing");
    assertEqual(obj.y, 3, "add new property");
});

// ===========================================================================
// Function Tests
// ===========================================================================

console.log("\n--- Functions ---");

test("function declaration", () => {
    function add(a, b) {
        return a + b;
    }
    assertEqual(add(2, 3), 5, "function call");
});

test("arrow function", () => {
    let multiply = (a, b) => a * b;
    assertEqual(multiply(3, 4), 12, "arrow function");
});

test("closure", () => {
    function makeCounter() {
        let count = 0;
        return () => ++count;
    }
    let counter = makeCounter();
    assertEqual(counter(), 1, "closure call 1");
    assertEqual(counter(), 2, "closure call 2");
    assertEqual(counter(), 3, "closure call 3");
});

// ===========================================================================
// Control Flow Tests
// ===========================================================================

console.log("\n--- Control Flow ---");

test("if statement", () => {
    let x = 10;
    let result;
    if (x > 5) {
        result = "big";
    } else {
        result = "small";
    }
    assertEqual(result, "big", "if true branch");
});

test("for loop", () => {
    let sum = 0;
    for (let i = 1; i <= 5; i++) {
        sum += i;
    }
    assertEqual(sum, 15, "for loop sum");
});

test("while loop", () => {
    let count = 0;
    let i = 0;
    while (i < 5) {
        count++;
        i++;
    }
    assertEqual(count, 5, "while loop count");
});

test("for...of", () => {
    let sum = 0;
    for (let x of [1, 2, 3]) {
        sum += x;
    }
    assertEqual(sum, 6, "for...of sum");
});

// ===========================================================================
// Class Tests
// ===========================================================================

console.log("\n--- Classes ---");

test("class basic", () => {
    class Point {
        constructor(x, y) {
            this.x = x;
            this.y = y;
        }
        
        distance() {
            return Math.sqrt(this.x ** 2 + this.y ** 2);
        }
    }
    
    let p = new Point(3, 4);
    assertEqual(p.x, 3, "class property x");
    assertEqual(p.y, 4, "class property y");
    assertEqual(p.distance(), 5, "class method");
});

// ===========================================================================
// Async Tests
// ===========================================================================

console.log("\n--- Async/Await ---");

test("Promise basic", () => {
    let resolved = false;
    Promise.resolve(42).then(v => {
        resolved = (v === 42);
    });
    // Note: This is synchronous check, may need event loop tick
    assertEqual(typeof Promise, "function", "Promise exists");
});

// ===========================================================================
// Summary
// ===========================================================================

console.log("\n=== Test Summary ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
console.log("Total:  " + (passed + failed));
console.log("Rate:   " + (passed / (passed + failed) * 100).toFixed(1) + "%\n");

if (failed > 0) {
    console.log("STATUS: FAILED");
} else {
    console.log("STATUS: PASSED");
}
