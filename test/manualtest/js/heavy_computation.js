window.registerTest('Heavy Computation (Synchronous Primes)', async (log) => {
  return new Promise((resolve) => {
    function isPrime(num) {
      for (let i = 2, s = Math.sqrt(num); i <= s; i++) {
          if (num % i === 0) return false;
      }
      return num > 1;
    }
    
    const startTime = performance.now();
    let limit = 10000;
    let ops = 0;

    function chunk() {
      let count = 0;
      for (let i = 2; i <= limit; i++) {
        if (isPrime(i)) count++;
      }
      ops += limit;
      limit += 10000;

      const now = performance.now();
      if (now - startTime > 3000) { 
        log(`Computed primes incrementally. Processed bounds dynamically.`);
        log(`Total operations: ${ops}`);
        resolve();
      } else {
        setTimeout(chunk, 0); // Yield to event loop to avoid locking the UI runner
      }
    }
    chunk();
  });
});
