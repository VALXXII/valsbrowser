#pragma once
// Page side of the per-site volume (audio-mixer.h), injected into every document and
// frame with AddScriptToExecuteOnDocumentCreated, before any page script runs.
//
//  * <audio>/<video>: the element really plays at (site volume x browser gain). The
//    `volume` property keeps returning what the site set, and the volumechange events
//    our own adjustments cause are swallowed before page listeners see them, so the
//    site's own slider never moves when ours does.
//  * Web Audio: every connection to a live AudioContext's destination goes through one
//    hidden GainNode per context (OfflineAudioContext renders are untouched). Gain
//    changes ramp over ~15 ms (no zipper noise). An element routed into Web Audio
//    (createMediaElementSource) gets the gain only once, from the graph.
//  * Frames: each document hears its gain from its parent (postMessage), the top
//    document from the browser (WebView2 web message), and relays it to its children;
//    reports travel the other way. The starting gain of a new document comes from the
//    table baked into this script (re-injected when a volume changes), so a page that
//    autoplays starts at its site's volume, before any message arrives.
//  * Reports (top document -> browser, JSON object, see Audio::ParseReport): playing
//    elements, longest duration, live streams, running AudioContexts, Media Session
//    state, title/artist/album and the best artwork URL. Sent on media events, debounced
//    150 ms, plus a 2 s heartbeat only while something plays.
// Limits: media inside closed/open shadow roots that start without play() are
// discovered at their first report only; sound produced outside these APIs (plugins,
// <embed>) is listed (the engine's audible flag) but cannot be attenuated.
#include <string>
#include <sstream>
#include "audio-mixer.h"

namespace NativeChrome { namespace Audio {
inline const wchar_t* ScriptBody() {
    return LR"JS((()=>{'use strict';
const W=window;try{if(W.__lisiereAudio)return;Object.defineProperty(W,'__lisiereAudio',{value:1});}catch(e){return;}
const TOP=W===W.top,setT=W.setTimeout.bind(W),clearT=W.clearTimeout.bind(W),now=Date.now.bind(Date),abs=Math.abs;
const siteOf=h=>String(h||'').toLowerCase().replace(/^www\./,'');
let topHost='';try{if(TOP)topHost=location.hostname;else{const a=location.ancestorOrigins;if(a&&a.length)topHost=new URL(a[a.length-1]).hostname;}}catch(e){}
const START=__GAINS__;let gain=1;{const g=START[siteOf(topHost)];if(typeof g==='number'&&g>=0&&g<=1)gain=g;}
const MP=W.HTMLMediaElement&&HTMLMediaElement.prototype,vol=MP&&Object.getOwnPropertyDescriptor(MP,'volume');
const own=new WeakMap(),pending=new WeakMap(),routed=new WeakSet(),known=new Set(),guarded=new WeakSet();
const real=el=>vol.get.call(el),siteVol=el=>own.has(el)?own.get(el):real(el);
function apply(el){if(!own.has(el))own.set(el,real(el));const want=routed.has(el)?own.get(el):own.get(el)*gain;if(abs(real(el)-want)>1e-9){if(el.isConnected||guarded.has(el))pending.set(el,(pending.get(el)||0)+1);vol.set.call(el,want);}}
function swallow(e){const t=e.target,n=pending.get(t);if(n){if(n>1)pending.set(t,n-1);else pending.delete(t);e.stopImmediatePropagation();}}
function guard(el){try{if(!guarded.has(el)){guarded.add(el);el.addEventListener('volumechange',swallow);}}catch(e){}return el;}
function track(el){known.add(el);try{apply(el);}catch(e){}}
let timer=0,beat=0,first=1,last='';
function schedule(){if(!timer)timer=setT(()=>{timer=0;report();},150);}
if(MP&&vol&&vol.get&&vol.set){
 Object.defineProperty(MP,'volume',{configurable:true,enumerable:vol.enumerable,get:function volume(){return siteVol(this);},set:function volume(v){const n=+v;if(!(n>=0&&n<=1)){vol.set.call(this,v);return;}const before=siteVol(this);own.set(this,n);known.add(this);const want=routed.has(this)?n:n*gain;if(abs(real(this)-want)>1e-9)vol.set.call(this,want);else if(n!==before){const el=this;setT(()=>el.dispatchEvent(new Event('volumechange')),0);}}});
 W.addEventListener('volumechange',swallow,true);
 /* Elements that may never join the document (new Audio(), createElement) get our listener first, before any page listener. */
 const NA=W.Audio;if(NA){const Audio=function Audio(){if(!new.target)return NA.apply(this,arguments);return guard(Reflect.construct(NA,arguments,new.target===Audio?NA:new.target));};Audio.prototype=NA.prototype;try{Object.defineProperty(W,'Audio',{value:Audio,writable:true,configurable:true});}catch(e){}}
 const DP=W.Document&&Document.prototype,ce=DP&&DP.createElement;if(ce)DP.createElement=function createElement(){const el=ce.apply(this,arguments);if(el instanceof HTMLMediaElement)guard(el);return el;};
 const nativePlay=MP.play;MP.play=function play(){try{track(this);schedule();}catch(e){}return nativePlay.apply(this,arguments);};
 for(const type of['play','playing','pause','ended','emptied','durationchange','loadedmetadata'])W.addEventListener(type,e=>{const t=e.target;if(t instanceof HTMLMediaElement){if(type==='play'||type==='playing'||type==='loadedmetadata')track(t);schedule();}},true);
}
const AC=W.AudioContext,AN=W.AudioNode&&AudioNode.prototype,ctxs=[],masters=new WeakMap();
if(AC&&AN&&W.AudioDestinationNode){
 const connect=AN.connect,disconnect=AN.disconnect,BAC=W.BaseAudioContext?BaseAudioContext.prototype:AC.prototype,createGain=BAC.createGain;
 const live=d=>d instanceof AudioDestinationNode&&d.context instanceof AC;
 const master=ctx=>{let g=masters.get(ctx);if(!g){g=createGain.call(ctx);g.gain.value=gain;connect.call(g,ctx.destination);masters.set(ctx,g);ctxs.push(W.WeakRef?new WeakRef(ctx):{deref:()=>ctx});try{ctx.addEventListener('statechange',schedule);}catch(e){}schedule();}return g;};
 AN.connect=function connect_(d){if(live(d)&&this!==masters.get(d.context)){const a=Array.prototype.slice.call(arguments);a[0]=master(d.context);connect.apply(this,a);return d;}return connect.apply(this,arguments);};
 AN.disconnect=function disconnect_(d){if(live(d)&&masters.has(d.context)&&this!==masters.get(d.context)){const a=Array.prototype.slice.call(arguments);a[0]=masters.get(d.context);return disconnect.apply(this,a);}return disconnect.apply(this,arguments);};
 const mes=AC.prototype.createMediaElementSource;
 if(mes&&vol)AC.prototype.createMediaElementSource=function createMediaElementSource(el){const node=mes.apply(this,arguments);try{if(!own.has(el))own.set(el,real(el));routed.add(el);known.add(el);vol.set.call(el,own.get(el));}catch(e){}return node;};
}
function setGain(g){if(typeof g!=='number'||!(g>=0&&g<=1))return;gain=g;
 if(vol){for(const el of known)try{apply(el);}catch(e){}try{for(const el of document.querySelectorAll('audio,video'))track(el);}catch(e){}}
 for(const r of ctxs){const c=r.deref(),m=c&&masters.get(c);if(m)try{m.gain.cancelScheduledValues(c.currentTime);m.gain.setTargetAtTime(g,c.currentTime,.015);}catch(e){try{m.gain.value=g;}catch(x){}}}
 relay();}
function relay(){try{for(let i=0;i<W.frames.length;i++)try{W.frames[i].postMessage({__lisiereAudioGain:gain},'*');}catch(e){}}catch(e){}}
const kids=new Map();
W.addEventListener('message',e=>{const d=e.data;if(!d||typeof d!=='object')return;
 if('__lisiereAudioGain' in d){if(!TOP&&e.source===W.parent)setGain(d.__lisiereAudioGain);e.stopImmediatePropagation();return;}
 if(d.__lisiereAudioHello){try{e.source&&e.source.postMessage({__lisiereAudioGain:gain},'*');}catch(x){}e.stopImmediatePropagation();return;}
 if(d.__lisiereAudioReport&&typeof d.__lisiereAudioReport==='object'){if(e.source)kids.set(e.source,{r:d.__lisiereAudioReport,at:now()});schedule();e.stopImmediatePropagation();}},true);
if(TOP&&W.chrome&&chrome.webview)chrome.webview.addEventListener('message',e=>{const d=e.data;if(d&&typeof d==='object'){if('lisiereAudioGain' in d)setGain(d.lisiereAudioGain);if(d.lisiereAudioPing){last='';report();}}});
if(!TOP)try{W.parent.postMessage({__lisiereAudioHello:1},'*');}catch(e){}
function art(list){try{let best='',score=1e9;for(const a of list||[]){const src=String(a&&a.src||'');if(!/^https:\/\//.test(src))continue;let s=0;for(const m of String(a.sizes||'').matchAll(/(\d+)x(\d+)/g))s=Math.max(s,+m[1]);const d=s?abs(s-128)+(s<96?200:0):150;if(d<score){score=d;best=src;}}return best;}catch(e){return'';}}
function local(){const r={p:0,d:-1,l:0,w:0,v:0,s:0,t:'',a:'',b:'',i:''};
 for(const el of Array.from(known)){let paused=true;try{paused=el.paused||el.ended;}catch(e){}if(paused){if(!el.isConnected)known.delete(el);continue;}
  if(el.muted||siteVol(el)<=0)continue;r.p++;const d=el.duration;if(d===Infinity)r.l=1;else if(d>r.d)r.d=d;try{if(el.videoWidth>0)r.v++;}catch(e){}}
 for(const x of ctxs){const c=x.deref();if(c&&c.state==='running')r.w++;}
 try{const ms=navigator.mediaSession,md=ms&&ms.metadata;if(md&&(md.title||md.artist)){const st=ms.playbackState;r.s=st==='playing'||(st!=='paused'&&r.p>0)?2:1;r.t=String(md.title||'').slice(0,300);r.a=String(md.artist||'').slice(0,300);r.b=String(md.album||'').slice(0,300);r.i=art(md.artwork).slice(0,2048);}}catch(e){}
 return r;}
function merged(){const r=local(),t=now();for(const[src,k]of kids){const c=k.r;if(t-k.at>5000&&(c.p||c.w)){kids.delete(src);continue;}
  r.p+=c.p|0;r.w+=c.w|0;r.v+=c.v|0;if(c.l)r.l=1;if(typeof c.d==='number'&&c.d>r.d)r.d=c.d;
  if(c.t&&(!r.t||(c.s>r.s))){r.s=Math.max(r.s,c.s|0);r.t=String(c.t);r.a=String(c.a||'');r.b=String(c.b||'');r.i=String(c.i||'');}}
 return r;}
function report(){if(timer){clearT(timer);timer=0;}if(beat){clearT(beat);beat=0;}
 const r=merged();
 if(TOP){const msg={lisiereAudio:1,p:r.p,d:isFinite(r.d)?Math.round(r.d*10)/10:-1,l:r.l,w:r.w,v:r.v,s:r.s,t:r.t,a:r.a,b:r.b,i:r.i};if(first){msg.h=1;}const key=JSON.stringify(msg);
  if(key!==last||first){first=0;last=key;try{W.chrome.webview.postMessage(msg);}catch(e){}}}
 else{try{W.parent.postMessage({__lisiereAudioReport:r},'*');}catch(e){}}
 if(r.p||r.w)beat=setT(report,2000);}
try{const MS=W.MediaSession&&MediaSession.prototype;for(const k of['metadata','playbackState']){const dsc=MS&&Object.getOwnPropertyDescriptor(MS,k);if(dsc&&dsc.set)Object.defineProperty(MS,k,{configurable:true,enumerable:dsc.enumerable,get:dsc.get,set:function(v){dsc.set.call(this,v);schedule();}});}}catch(e){}
})();)JS";
}
// The script with the starting gains of every site that is not at 100 % (sites are
// validated host names: no quote, backslash or control character can reach the JSON).
inline std::wstring Script(const Volumes& volumes) {
    std::wstring gains = L"{";
    for (auto& [site, percent] : volumes.All()) {
        if (site.find_first_of(L"\"\\'<>\r\n\t ") != std::wstring::npos) continue;
        if (gains.size() > 1) gains += L',';
        gains += L'"' + site + L"\":" + (percent >= 100 ? L"1" : percent <= 0 ? L"0" : L"0." + std::wstring(percent < 10 ? L"0" : L"") + std::to_wstring(percent));
    }
    gains += L'}';
    std::wstring body = ScriptBody();
    const auto at = body.find(L"__GAINS__");
    if (at != std::wstring::npos) body.replace(at, 9, gains);
    return body;
}
// Web message that sets a live page's gain (0..1).
inline std::wstring GainMessage(int percent) {
    percent = ClampPercent(percent);
    return L"{\"lisiereAudioGain\":" + std::wstring(percent >= 100 ? L"1" : percent <= 0 ? L"0" : L"0." + std::wstring(percent < 10 ? L"0" : L"") + std::to_wstring(percent)) + L"}";
}
}} // namespace NativeChrome::Audio
