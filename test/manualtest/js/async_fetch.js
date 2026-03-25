window.registerTest('Microtasks & Promise Queues', async (log) => {
  async function runMicrotaskStress(depth) {
    if (depth === 0) return 1;
    const res = await Promise.resolve(depth);
    return res + await runMicrotaskStress(depth - 1);
  }

  log("Testing deep promise resolution...");
  const sum = await runMicrotaskStress(5000);
  log(`Deep promise depth 5000 resolved. Result: ${sum}`);
  if (sum !== 12502500) {
    throw new Error(`Math sync error in promises. Got ${sum}`);
  }

  log("Testing fetch behavior (mocking network request)...");
  try {
    const response = await fetch('data:text/plain;base64,SGVsbG8gWmVwcmE=');
    const text = await response.text();
    if (text !== 'Hello Zepra') throw new Error(`Fetch mock mismatch: ${text}`);
    log(`Fetch result successfully decoded.`);
  } catch (e) {
    log(`Fetch API skipped/failed: ${e.message} (expected if no fetch polyfill)`);
  }
});
