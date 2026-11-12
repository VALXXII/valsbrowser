// Runs the injected per-site audio script (written by audio_test.exe from
// src/audio-script.h with gains {a.example: .05, b.example: 0, youtube.com: .35}) in a
// fake browser: media elements with a native-like `volume`, a Web Audio graph, frames,
// Media Session and the WebView2 message channel.
// Usage: node tests/audio/script_test.js build/audio-qa/audio-script.js
'use strict';
const fs = require('fs'), vm = require('vm');
const source = fs.readFileSync(process.argv[2], 'utf8');
let failures = 0;
const expect = (ok, what) => { if (!ok) { console.error('FAIL ' + what); failures++; } };
const near = (a, b) => Math.abs(a - b) < 1e-9;
const tick = () => new Promise(r => setTimeout(r, 0));
const wait = ms => new Promise(r => setTimeout(r, ms));

function World({ hostname = 'www.youtube.com', top = null, parent = null, ancestors = [] } = {}) {
  const w = {};
  class Ev { constructor(type) { this.type = type; this.stopped = false; } stopImmediatePropagation() { this.stopped = true; } }
  class Target {
    constructor() { this._l = {}; }
    addEventListener(type, f, capture) { (this._l[type] = this._l[type] || []).push({ f, capture: !!capture }); }
    removeEventListener() {}
    _fire(e, capture) { for (const l of (this._l[e.type] || []).slice()) { if (e.stopped) return; if (l.capture === capture || this === e.target) l.f.call(this, e); } }
    dispatchEvent(e) { e.target = this; if (this.isConnected) w._fire(e, true); if (!e.stopped) this._fire(e, false); return true; }
  }
  // Native-like volume: validated, event queued only when it changes.
  class HTMLMediaElement extends Target {
    constructor() { super(); this._v = 1; this.paused = true; this.ended = false; this.muted = false; this.duration = NaN; this.isConnected = true; this.videoWidth = 0; }
    play() { this.paused = false; queueMicrotask(() => this.dispatchEvent(new Ev('play'))); return Promise.resolve(); }
    pause() { this.paused = true; queueMicrotask(() => this.dispatchEvent(new Ev('pause'))); }
  }
  class Audio extends HTMLMediaElement { constructor(src) { super(); this.isConnected = false; this.src = src; } }
  class Document { createElement(name) { const el = /^(audio|video)$/i.test(name) ? new HTMLMediaElement() : { tagName: name }; if (el.isConnected !== undefined) el.isConnected = false; return el; } }
  Object.defineProperty(HTMLMediaElement.prototype, 'volume', { configurable: true, enumerable: true,
    get() { return this._v; },
    set(v) { v = +v; if (!(v >= 0 && v <= 1)) throw new RangeError('IndexSizeError'); if (v !== this._v) { this._v = v; setTimeout(() => this.dispatchEvent(new Ev('volumechange')), 0); } } });
  class AudioNode { constructor(ctx) { this.context = ctx; this.out = []; }
    connect(d) { this.out.push(d); return d && d instanceof AudioNode ? d : undefined; }
    disconnect(d) { this.out = d === undefined ? [] : this.out.filter(x => x !== d); } }
  class AudioDestinationNode extends AudioNode {}
  class GainNode extends AudioNode { constructor(ctx) { super(ctx); const g = { value: 1, setTargetAtTime(v) { g.value = v; }, cancelScheduledValues() {} }; this.gain = g; } }
  class BaseAudioContext extends Target { constructor() { super(); this.destination = new AudioDestinationNode(this); this.currentTime = 0; this.state = 'running'; } }
  BaseAudioContext.prototype.createGain = function () { return new GainNode(this); };
  class AudioContext extends BaseAudioContext {}
  AudioContext.prototype.createMediaElementSource = function (el) { const n = new AudioNode(this); n.element = el; return n; };
  class OfflineAudioContext extends BaseAudioContext {}
  class MediaSession { constructor() { this._m = null; this._s = 'none'; } }
  Object.defineProperty(MediaSession.prototype, 'metadata', { configurable: true, enumerable: true, get() { return this._m; }, set(v) { this._m = v; } });
  Object.defineProperty(MediaSession.prototype, 'playbackState', { configurable: true, enumerable: true, get() { return this._s; }, set(v) { this._s = v; } });
  const posted = [], webviewListeners = [];
  w._l = {}; Object.setPrototypeOf(w, Target.prototype);
  Object.assign(w, { HTMLMediaElement, Audio, Document, Reflect, AudioNode, AudioDestinationNode, GainNode, BaseAudioContext, AudioContext, OfflineAudioContext, MediaSession, Event: Ev, URL,
    WeakRef, Date, Math, JSON, Object, Array, Map, Set, WeakMap, WeakSet, String, Number, RegExp, Promise, Error, isFinite,
    setTimeout, clearTimeout, queueMicrotask, frames: [], posted, webviewListeners,
    location: { hostname, ancestorOrigins: ancestors }, navigator: { mediaSession: new MediaSession() },
    document: Object.assign(new Document(), { querySelectorAll: () => w._elements || [] }) });
  w.window = w; w.top = top || w; w.parent = parent || w;
  if (!top) w.chrome = { webview: { postMessage: m => posted.push(JSON.parse(JSON.stringify(m))), addEventListener: (t, f) => webviewListeners.push(f) } };
  w.postMessage = (data, origin, from) => setTimeout(() => { const e = new Ev('message'); e.data = data; e.source = from; e.target = w; w._fire(e, true); if (!e.stopped) w._fire(e, false); }, 0);
  w.host = data => webviewListeners.forEach(f => f({ data }));
  vm.createContext(w); vm.runInContext(source, w);
  return w;
}

(async () => {
  // ---- Media elements -------------------------------------------------------------------
  {
    const w = World();
    const el = new w.HTMLMediaElement(); let seen = 0; el.addEventListener('volumechange', () => seen++);
    el.play(); await tick(); await tick();
    expect(near(el._v, .35), 'starting gain applied from the baked table (youtube.com 35 %)');
    expect(el.volume === 1, 'the site still reads its own volume');
    await wait(5); expect(seen === 0, 'our adjustment fires no volumechange the page can see');
    el.volume = .8; await wait(5);
    expect(near(el._v, .28) && el.volume === .8 && seen === 1, 'site sets 80 %: plays at 80 x 35 %, sees its 80 % and its own event');
    w.host({ lisiereAudioGain: .5 }); await wait(5);
    expect(near(el._v, .4) && el.volume === .8 && seen === 1, 'browser gain 50 %: plays at 40 %, the site slider never moves, no event');
    let threw = false; try { el.volume = 1.5; } catch (e) { threw = true; } expect(threw && el.volume === .8, 'out-of-range values still throw like the native setter');
    w.host({ lisiereAudioGain: 0 }); await wait(5); expect(el._v === 0 && el.volume === .8, 'muted site: silent element, site value intact');
    el.volume = .3; await wait(5); expect(seen === 2 && el.volume === .3, 'site change while muted by the browser still notifies the site');
    w.host({ lisiereAudioGain: 7 }); w.host({ lisiereAudioGain: 'x' }); await wait(5); expect(el._v === 0, 'invalid gains are ignored');
    // Detached new Audio(): found through play(); its own listener never hears our changes.
    w.host({ lisiereAudioGain: .5 }); await wait(5);
    const a = new w.Audio('x.mp3'); let heard = 0; a.addEventListener('volumechange', () => heard++); a.volume = .8; await wait(5);
    expect(heard === 1 && a instanceof w.HTMLMediaElement, 'new Audio() still constructs a media element; its own change is heard');
    a.play(); await tick(); expect(near(a._v, .4) && a.volume === .8, 'detached Audio() gets the gain on play()');
    w.host({ lisiereAudioGain: .25 }); await wait(5); expect(near(a._v, .2) && heard === 1, 'detached element: our gain changes stay invisible to its listeners');
    let threwCall = false; try { w.Audio(); } catch (e) { threwCall = true; } expect(threwCall, 'Audio() without new still throws');
    const v = w.document.createElement('video'); let vh = 0; v.addEventListener('volumechange', () => vh++); v.play(); await tick(); w.host({ lisiereAudioGain: .6 }); await wait(5);
    expect(near(v._v, .6) && vh === 0, 'createElement(video): gain applied silently');
  }
  // ---- Web Audio ---------------------------------------------------------------------------
  {
    const w = World({ hostname: 'a.example' });
    const ctx = new w.AudioContext(), osc = new w.AudioNode(ctx);
    const r = osc.connect(ctx.destination);
    const master = osc.out[0];
    expect(r === ctx.destination, 'connect() still returns the destination');
    expect(master instanceof w.GainNode && master.out[0] === ctx.destination && near(master.gain.value, .05), 'graph goes through one hidden gain (a.example 5 %)');
    const osc2 = new w.AudioNode(ctx); osc2.connect(ctx.destination); expect(osc2.out[0] === master, 'one master per context');
    w.host({ lisiereAudioGain: .6 }); await tick(); expect(near(master.gain.value, .6), 'gain change reaches the graph');
    osc.disconnect(ctx.destination); expect(osc.out.length === 0, 'disconnect(destination) removes the hidden hop');
    const off = new w.OfflineAudioContext(), n = new w.AudioNode(off); n.connect(off.destination); expect(n.out[0] === off.destination, 'offline renders untouched');
    const el = new w.HTMLMediaElement(); el.play(); await tick(); expect(near(el._v, .6), 'element at the gain');
    ctx.createMediaElementSource(el); expect(el._v === 1 && el.volume === 1, 'an element routed into Web Audio is not attenuated twice');
  }
  // ---- Reports -------------------------------------------------------------------------------
  {
    const w = World({ hostname: 'music.example' });
    w.navigator.mediaSession.metadata = { title: 'Clair de lune', artist: 'Debussy', album: 'Suite', artwork: [{ src: 'https://img/512.jpg', sizes: '512x512' }, { src: 'https://img/96.jpg', sizes: '96x96' }, { src: 'http://img/128.jpg', sizes: '128x128' }] };
    const el = new w.HTMLMediaElement(); el.duration = 300; el.play(); await wait(200);
    const m = w.posted[w.posted.length - 1];
    expect(m && m.lisiereAudio === 1 && m.p === 1 && m.d === 300 && m.s === 2 && m.t === 'Clair de lune' && m.a === 'Debussy' && m.b === 'Suite', 'report: playing element, duration, session, metadata');
    expect(m && m.i === 'https://img/96.jpg', 'artwork: https only, size closest to the 40 DIP tile');
    expect(w.posted.length >= 1 && w.posted[0].h === 1, 'first report of a document is flagged');
    const count = w.posted.length; el.pause(); await wait(200);
    const last = w.posted[w.posted.length - 1]; expect(w.posted.length === count + 1 && last.p === 0 && last.s === 1, 'pause reported once, session paused');
    const s = new w.HTMLMediaElement(); s.duration = 1.2; s.play(); await wait(200); expect(w.posted[w.posted.length - 1].d === 1.2, 'short clip duration reported');
    const muted = new w.HTMLMediaElement(); muted.muted = true; muted.duration = 900; muted.play(); await wait(200); expect(w.posted[w.posted.length - 1].d !== 900, 'muted autoplay is not counted as playing');
  }
  // ---- Frames --------------------------------------------------------------------------------
  {
    const top = World({ hostname: 'www.youtube.com' });
    const child = World({ hostname: 'cdn.player.example', top, parent: top, ancestors: ['https://www.youtube.com'] });
    const el = new child.HTMLMediaElement(); el.play(); await tick(); await tick();
    expect(near(el._v, .35), "a frame starts at its top site's gain (ancestorOrigins)");
    // Gain from the parent only.
    child.postMessage({ __lisiereAudioGain: .2 }, '*', top); await wait(5); expect(near(el._v, .2), 'frame takes the gain from its parent');
    child.postMessage({ __lisiereAudioGain: .9 }, '*', {}); await wait(5); expect(near(el._v, .2), 'gain from anyone else is ignored');
    // Relay: the top document forwards its gain to child frames.
    let relayed = null; top.frames.push({ postMessage: d => { relayed = d; } });
    top.host({ lisiereAudioGain: .7 }); expect(relayed && relayed.__lisiereAudioGain === .7, 'top relays the gain into its frames');
    // Report from a frame reaches the browser through the top document.
    top.postMessage({ __lisiereAudioReport: { p: 1, d: 240, l: 0, w: 0, v: 1, s: 2, t: 'Embedded video', a: '', b: '', i: '' } }, '*', child); await wait(200);
    const m = top.posted[top.posted.length - 1]; expect(m && m.p === 1 && m.d === 240 && m.t === 'Embedded video' && m.v === 1, 'frame report merged into the top report');
    let pageSaw = false; top.addEventListener('message', () => { pageSaw = true; }); top.postMessage({ __lisiereAudioGain: 1 }, '*', child); await wait(5); expect(!pageSaw, 'our frame messages never reach page listeners');
  }
  if (failures) { console.error(failures + ' script check(s) failed'); process.exit(1); }
  console.log('audio script checks passed');
  process.exit(0); // pages keep a 2 s heartbeat while media plays
})();
