window.registerTest('Array Prototype Engines (Map/Filter/Reduce)', async (log) => {
  return new Promise((resolve) => {
    const arr = Array.from({length: 1000000}, (_, i) => i);
    const start = performance.now();
    const mapped = arr.map(x => x * 2);
    const filtered = mapped.filter(x => x % 3 === 0);
    const sum = filtered.reduce((a, b) => a + b, 0);
    const end = performance.now();
    log(`Chained map/filter/reduce over 1,000,000 items in ${(end-start).toFixed(2)}ms.\nFinal sum: ${sum}`);
    resolve();
  });
});
