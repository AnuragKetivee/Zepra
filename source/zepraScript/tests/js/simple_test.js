// Simple functional test

console.log("=== Array Methods Test ===");

var a = [1, 2, 3];
console.log("Initial: " + a.join(","));

// push/pop
a.push(4);
console.log("After push(4): " + a.join(","));

var popped = a.pop();
console.log("Popped: " + popped);

// Basic array operations
console.log("Length: " + a.length);
console.log("Slice 1,2: " + a.slice(1, 2).join(","));
console.log("indexOf(2): " + a.indexOf(2));
console.log("includes(3): " + a.includes(3));

// Reverse
var b = [1, 2, 3];
b.reverse();
console.log("Reversed: " + b.join(","));

// Fill
var c = [0, 0, 0];
c.fill(5);
console.log("Filled with 5: " + c.join(","));

console.log("=== String Methods Test ===");
var s = "hello world";
console.log("toUpperCase: " + s.toUpperCase());
console.log("toLowerCase: " + "HELLO".toLowerCase());
console.log("trim: '" + "  hi  ".trim() + "'");
console.log("length: " + s.length);

console.log("=== Math Test ===");
console.log("Math.PI: " + Math.PI);
console.log("Math.abs(-5): " + Math.abs(-5));
console.log("Math.floor(3.7): " + Math.floor(3.7));
console.log("Math.ceil(3.2): " + Math.ceil(3.2));
console.log("Math.round(3.5): " + Math.round(3.5));
console.log("Math.sqrt(16): " + Math.sqrt(16));
console.log("Math.pow(2,8): " + Math.pow(2, 8));

console.log("=== JSON Test ===");
var obj = JSON.parse('{"a":1,"b":2}');
console.log("Parsed JSON a: " + obj.a);
console.log("Stringify: " + JSON.stringify(obj));

console.log("=== All tests complete ===");
