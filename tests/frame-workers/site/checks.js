// Each check starts one kind of worker and reports true once it answered, false on an error. A worker the
// engine holds paused never answers: the host sees the check missing when its time is up.
function runChecks(where, report) {
  const once = (key) => { let sent = false; return ok => { if (!sent) { sent = true; report(where + ':' + key, ok); } }; };
  const ask = (worker, key) => {
    const done = once(key);
    worker.onmessage = e => done(e.data === 42);
    worker.onerror = () => done(false);
    worker.postMessage(21);
  };
  // A blob: worker, the way Cloudflare Turnstile starts its own.
  try { ask(new Worker(URL.createObjectURL(new Blob(['onmessage=e=>postMessage(e.data*2)'], { type: 'text/javascript' }))), 'blob-worker'); }
  catch (e) { once('blob-worker')(false); }
  try { ask(new Worker('worker.js'), 'worker'); } catch (e) { once('worker')(false); }
  // A worker started by a worker.
  try { ask(new Worker('nested.js'), 'nested-worker'); } catch (e) { once('nested-worker')(false); }
  try {
    const done = once('shared-worker');
    const s = new SharedWorker('shared.js?' + where);
    s.port.onmessage = e => done(e.data === 42);
    s.onerror = () => done(false);
    s.port.postMessage(21);
  } catch (e) { once('shared-worker')(false); }
  // An audio worklet (its own thread in the engine).
  try {
    const done = once('audio-worklet');
    new AudioContext().audioWorklet.addModule('worklet.js').then(() => done(true), () => done(false));
  } catch (e) { once('audio-worklet')(false); }
  if (navigator.serviceWorker) {
    const done = once('service-worker');
    navigator.serviceWorker.register('sw.js?' + where).then(r => {
      const w = r.active || r.waiting || r.installing;
      const send = sw => {
        const c = new MessageChannel();
        c.port1.onmessage = e => done(e.data === 42);
        sw.postMessage(21, [c.port2]);
      };
      if (w.state === 'activated') send(w);
      else w.addEventListener('statechange', () => { if (w.state === 'activated') send(w); });
    }, () => done(false));
  }
}
