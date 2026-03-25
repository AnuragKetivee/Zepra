window.registerTest('RegExp Backtracking Stress', async (log) => {
  return new Promise((resolve) => {
    const start = performance.now();
    let res = "";
    // Evil Regex testing ReDoS vulnerability / performance
    const evilRegex = /^(a+)+$/;
    try {
      const match = evilRegex.test("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"); // 31 a's
      res = `Regex eval finished cleanly (Match: ${match})`;
    } catch(e) {
      res = `Regex engine boundary exception: ${e.message}`;
    }
    const end = performance.now();
    log(`${res} in ${(end-start).toFixed(2)}ms`);
    resolve();
  });
});
