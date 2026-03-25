window.registerTest('DOM API Micro-benchmarks', async (log) => {
  return new Promise((resolve, reject) => {
    const sandbox = document.createElement('div');
    sandbox.style.display = 'none';
    document.body.appendChild(sandbox);

    const NUM_NODES = 2000;
    const startTime = performance.now();
    let iterations = 0;

    function runIteration() {
      try {
        const fragment = document.createDocumentFragment();
        for (let i = 0; i < NUM_NODES; i++) {
          let div = document.createElement('div');
          div.textContent = i;
          fragment.appendChild(div);
        }
        sandbox.appendChild(fragment);

        const nodes = sandbox.childNodes;
        for (let i = 0; i < nodes.length; i++) {
          nodes[i].setAttribute('data-index', i);
        }

        sandbox.innerHTML = '';
        iterations++;

        const now = performance.now();
        if (now - startTime > 3000) {
          log(`Completed ${iterations} DOM mutation cycles for ${NUM_NODES} nodes.`);
          document.body.removeChild(sandbox);
          resolve();
        } else {
          setTimeout(runIteration, 0);
        }
      } catch(e) {
        reject(e);
      }
    }
    runIteration();
  });
});
