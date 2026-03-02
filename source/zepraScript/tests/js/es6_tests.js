// ===========================================================================
// ZepraScript ES6+ Feature Tests
// ===========================================================================

let passed = 0, failed = 0;
function assertEqual(a, b, msg) {
    if (a === b) { passed++; console.log("✓ " + msg); }
    else { failed++; console.error("✗ " + msg); }
}
function assert(c, msg) { assertEqual(c, true, msg); }

console.log("\n=== ES6+ Feature Tests ===\n");

// ----- let/const -----
console.log("--- let/const ---");
let x = 1;
const y = 2;
assertEqual(x, 1, "let declaration");
assertEqual(y, 2, "const declaration");

// ----- Arrow Functions -----
console.log("\n--- Arrow Functions ---");
const add = (a, b) => a + b;
const square = x => x * x;
assertEqual(add(2, 3), 5, "arrow with params");
assertEqual(square(4), 16, "arrow single param");

// ----- Template Literals -----
console.log("\n--- Template Literals ---");
let name = "World";
assertEqual(`Hello ${name}!`, "Hello World!", "template literal");
assertEqual(`1 + 1 = ${1 + 1}`, "1 + 1 = 2", "template expression");

// ----- Destructuring -----
console.log("\n--- Destructuring ---");
let [a, b] = [1, 2];
assertEqual(a, 1, "array destructuring [0]");
assertEqual(b, 2, "array destructuring [1]");

let {foo, bar} = {foo: 10, bar: 20};
assertEqual(foo, 10, "object destructuring foo");
assertEqual(bar, 20, "object destructuring bar");

// ----- Spread -----
console.log("\n--- Spread ---");
let arr1 = [1, 2];
let arr2 = [...arr1, 3, 4];
assertEqual(arr2.length, 4, "spread in array");
assertEqual(arr2[2], 3, "spread result");

let obj1 = {a: 1};
let obj2 = {...obj1, b: 2};
assertEqual(obj2.a, 1, "spread in object");
assertEqual(obj2.b, 2, "spread add property");

// ----- Rest Parameters -----
console.log("\n--- Rest Parameters ---");
function sum(...nums) {
    return nums.reduce((a, b) => a + b, 0);
}
assertEqual(sum(1, 2, 3), 6, "rest parameters");

// ----- Default Parameters -----
console.log("\n--- Default Parameters ---");
function greet(name = "Guest") {
    return "Hello " + name;
}
assertEqual(greet(), "Hello Guest", "default param used");
assertEqual(greet("Bob"), "Hello Bob", "default param overridden");

// ----- Classes -----
console.log("\n--- Classes ---");
class Animal {
    constructor(name) {
        this.name = name;
    }
    speak() {
        return this.name + " makes a sound";
    }
}
class Dog extends Animal {
    speak() {
        return this.name + " barks";
    }
}
let dog = new Dog("Rex");
assertEqual(dog.name, "Rex", "class property");
assertEqual(dog.speak(), "Rex barks", "class inheritance");

// ----- Symbol -----
console.log("\n--- Symbol ---");
let sym = Symbol("test");
assertEqual(typeof sym, "symbol", "typeof Symbol");

// ----- Map -----
console.log("\n--- Map ---");
let map = new Map();
map.set("key", "value");
assertEqual(map.get("key"), "value", "Map get/set");
assertEqual(map.has("key"), true, "Map has");
assertEqual(map.size, 1, "Map size");

// ----- Set -----
console.log("\n--- Set ---");
let set = new Set([1, 2, 2, 3]);
assertEqual(set.size, 3, "Set removes duplicates");
assertEqual(set.has(2), true, "Set has");

// ----- Promise -----
console.log("\n--- Promise ---");
assertEqual(typeof Promise, "function", "Promise exists");
let p = new Promise((resolve) => resolve(42));
assertEqual(p instanceof Promise, true, "Promise instance");

// ----- Optional Chaining -----
console.log("\n--- Optional Chaining ---");
let obj3 = { nested: { value: 5 } };
assertEqual(obj3?.nested?.value, 5, "optional chaining exists");
assertEqual(obj3?.missing?.value, undefined, "optional chaining undefined");

// ----- Nullish Coalescing -----
console.log("\n--- Nullish Coalescing ---");
let val1 = null ?? "default";
let val2 = undefined ?? "default";
let val3 = 0 ?? "default";
assertEqual(val1, "default", "?? with null");
assertEqual(val2, "default", "?? with undefined");
assertEqual(val3, 0, "?? with 0");

// ----- Summary -----
console.log("\n=== Summary ===");
console.log("Passed: " + passed + ", Failed: " + failed);
