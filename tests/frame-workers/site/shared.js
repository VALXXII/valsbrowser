onconnect = e => { const port = e.ports[0]; port.onmessage = m => port.postMessage(m.data * 2); };
