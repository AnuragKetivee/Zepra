// Async/Await Test Suite
console.log('=== Async/Await Tests ===\n');

// Test 1: Basic await with Promise.resolve
try {
    var p1 = Promise.resolve(42);
    var r1 = await p1;
    console.log(r1 === 42 ? '✓ await Promise.resolve' : '✗ await Promise.resolve');
} catch (e) {
    console.log('✗ await Promise.resolve: ' + e);
}

// Test 2: await with immediate value (becomes resolved promise)
try {
    var r2 = await 100;
    console.log(r2 === 100 ? '✓ await immediate value' : '✗ await immediate value');
} catch (e) {
    console.log('✗ await immediate value: ' + e);
}

// Test 3: Promise constructor with executor
try {
    var p3 = new Promise(function(resolve, reject) {
        resolve(123);
    });
    var r3 = await p3;
    console.log(r3 === 123 ? '✓ Promise constructor' : '✗ Promise constructor');
} catch (e) {
    console.log('✗ Promise constructor: ' + e);
}

// Test 4: Promise.reject (should throw)
try {
    var p4 = Promise.reject('error');
    var r4 = await p4;
    console.log('✗ Promise.reject should throw');
} catch (e) {
    console.log('✓ Promise.reject throws');
}

// Test 5: Chained promises
try {
    var p5 = Promise.resolve(10);
    var r5 = await p5;
    var r6 = await Promise.resolve(r5 * 2);
    console.log(r6 === 20 ? '✓ Chained await' : '✗ Chained await');
} catch (e) {
    console.log('✗ Chained await: ' + e);
}

console.log('\n=== Async/Await Complete ===');
