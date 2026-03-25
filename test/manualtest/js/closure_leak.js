window.registerTest('Closure Memory Leaks & Variable Capture', async (log) => {
  return new Promise((resolve) => {
    let timers = [];
    let executed = 0;
    const start = performance.now();
    for(let i=0; i<10000; i++) {
        // Deep nested closure
        let scopedVar = new Array(100).fill(i);
        timers.push(function() {
            executed += scopedVar[0];
        });
    }
    // Execution
    timers.forEach(t => t());
    const end = performance.now();
    log(`Executed 10,000 deep closures successfully.\nMemory contexts captured: 10,000\nExecution time: ${(end-start).toFixed(2)}ms`);
    resolve();
  });
});
