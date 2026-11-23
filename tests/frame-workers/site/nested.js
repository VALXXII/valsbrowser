// Answers through a worker of its own.
const inner = new Worker('worker.js');
onmessage = e => { inner.onmessage = r => postMessage(r.data); inner.postMessage(e.data); };
