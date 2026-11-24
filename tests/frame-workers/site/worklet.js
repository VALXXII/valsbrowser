registerProcessor('silent', class extends AudioWorkletProcessor { process() { return false; } });
