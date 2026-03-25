window.registerTest('Matrix Math JIT Validation', async (log) => {
  return new Promise((resolve) => {
    const SIZE = 150;
    let A = new Float64Array(SIZE * SIZE);
    let B = new Float64Array(SIZE * SIZE);
    let C = new Float64Array(SIZE * SIZE);

    for (let i = 0; i < SIZE * SIZE; i++) {
      A[i] = Math.random();
      B[i] = Math.random();
    }

    const startTime = performance.now();
    let ops = 0;

    function multiply() {
      for (let i = 0; i < SIZE; i++) {
        for (let j = 0; j < SIZE; j++) {
          let sum = 0;
          for (let k = 0; k < SIZE; k++) {
            sum += A[i * SIZE + k] * B[k * SIZE + j];
          }
          C[i * SIZE + j] = sum;
        }
      }
      ops++;
    }

    function chunk() {
      multiply();
      const now = performance.now();
      if (now - startTime > 3000) { 
        log(`Performed ${ops} matrix multiplications of size ${SIZE}x${SIZE} in 3s.`);
        resolve();
      } else {
        setTimeout(chunk, 0);
      }
    }
    chunk();
  });
});
