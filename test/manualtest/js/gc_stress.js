window.registerTest('Garbage Collection Memory Stress', async (log) => {
  return new Promise((resolve, reject) => {
    const startTime = performance.now();
    let cycles = 0;
    let holder = [];

    function stressGC() {
      try {
        let batch = [];
        for (let i = 0; i < 50000; i++) {
          batch.push({ id: i, data: "some_string_data_to_consume_memory_" + Math.random(), ts: new Date() });
        }
        holder = batch; 
        cycles++;

        const now = performance.now();
        if (now - startTime > 3000) { 
          holder = null; 
          log(`Generated and discarded ${cycles} large object batches (~5MB each).`);
          resolve();
        } else {
          setTimeout(stressGC, 10);
        }
      } catch (e) {
        reject(new Error("OOM or GC Failure: " + e.message));
      }
    }
    stressGC();
  });
});
