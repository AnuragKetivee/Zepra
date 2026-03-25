window.registerTest('JSON Parsing & Serialization', async (log) => {
  return new Promise((resolve) => {
    let obj = { tree: [] };
    for(let i=0; i<100000; i++) obj.tree.push({id: i, v: Math.random()});
    let timings = [];
    for(let i=0; i<5; i++) {
        const sStart = performance.now();
        const str = JSON.stringify(obj);
        const pStart = performance.now();
        JSON.parse(str);
        timings.push(`C${i}: Stringify=${(pStart-sStart).toFixed(1)}ms Parse=${(performance.now()-pStart).toFixed(1)}ms`);
    }
    log(`Processed massive JSON trees.\n` + timings.join('\n'));
    resolve();
  });
});
