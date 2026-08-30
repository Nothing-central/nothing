#include "FingerprintSpoofer.h"
#include "profile_to_json.h"
#include <QJsonDocument>

QString FingerprintSpoofer::injectionScript(const std::string& origin) const {
    Key32 perSiteKey = keys_.PerSiteKey(origin);
    QString perOriginKeyHex = KeyToHex(perSiteKey);

    QJsonDocument navDoc(NavigatorProfileToJson(nav_));
    QJsonDocument scrDoc(ScreenProfileToJson(scr_));
    QJsonDocument glDoc(WebGLProfileToJson(gl_));
    QJsonDocument audioDoc(AudioProfileToJson(audio_));

    QString script;
    script += "(function() {\n'use strict';\n";
    script += "if (window.__NB_FP_INIT__) return;\nwindow.__NB_FP_INIT__ = true;\n";
    script += "const NAV = " + navDoc.toJson(QJsonDocument::Compact) + ";\n";
    script += "const SCR = " + scrDoc.toJson(QJsonDocument::Compact) + ";\n";
    script += "const GL = " + glDoc.toJson(QJsonDocument::Compact) + ";\n";
    script += "const AUDIO = " + audioDoc.toJson(QJsonDocument::Compact) + ";\n";
    script += "const PER_ORIGIN_KEY_HEX = '" + perOriginKeyHex + "';\n";

    // JS crypto (HMAC-SHA256 + XorShift128Plus) still has to be reimplemented here —
    // it runs inside the page, your C++ SessionKeyStore/site_key.cpp can't reach in.
    // That's the one unavoidable duplication: same algorithm, two runtimes.
    script += R"JS(
function hexToBytes(hex){ const o=new Uint8Array(hex.length/2); for(let i=0;i<o.length;i++) o[i]=parseInt(hex.substr(i*2,2),16); return o; }
function rrot(n,x){ return (x>>>n)|(x<<(32-n)); }
const K=new Uint32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
function sha256(bytes){
    let h=new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
    const bitLen=bytes.length*8, padLen=((bytes.length+8)>>6<<6)+64;
    const buf=new Uint8Array(padLen); buf.set(bytes); buf[bytes.length]=0x80;
    const dv=new DataView(buf.buffer);
    dv.setUint32(padLen-4,bitLen>>>0,false); dv.setUint32(padLen-8,Math.floor(bitLen/0x100000000),false);
    const w=new Uint32Array(64);
    for(let off=0; off<padLen; off+=64){
        for(let i=0;i<16;i++) w[i]=dv.getUint32(off+i*4,false);
        for(let i=16;i<64;i++){
            const s0=rrot(7,w[i-15])^rrot(18,w[i-15])^(w[i-15]>>>3);
            const s1=rrot(17,w[i-2])^rrot(19,w[i-2])^(w[i-2]>>>10);
            w[i]=(w[i-16]+s0+w[i-7]+s1)>>>0;
        }
        let [a,b,c,d,e,f,g,hh]=h;
        for(let i=0;i<64;i++){
            const S1=rrot(6,e)^rrot(11,e)^rrot(25,e); const ch=(e&f)^(~e&g);
            const t1=(hh+S1+ch+K[i]+w[i])>>>0;
            const S0=rrot(2,a)^rrot(13,a)^rrot(22,a); const maj=(a&b)^(a&c)^(b&c);
            const t2=(S0+maj)>>>0;
            hh=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;
        }
        h[0]=(h[0]+a)>>>0;h[1]=(h[1]+b)>>>0;h[2]=(h[2]+c)>>>0;h[3]=(h[3]+d)>>>0;
        h[4]=(h[4]+e)>>>0;h[5]=(h[5]+f)>>>0;h[6]=(h[6]+g)>>>0;h[7]=(h[7]+hh)>>>0;
    }
    const out=new Uint8Array(32); const odv=new DataView(out.buffer);
    for(let i=0;i<8;i++) odv.setUint32(i*4,h[i],false);
    return out;
}
function concatBytes(a,b){ const o=new Uint8Array(a.length+b.length); o.set(a,0); o.set(b,a.length); return o; }
function hmacSha256(keyBytes, msgBytes){
    const bs=64; let key=keyBytes; if(key.length>bs) key=sha256(key);
    const k0=new Uint8Array(bs); k0.set(key);
    const ipad=new Uint8Array(bs), opad=new Uint8Array(bs);
    for(let i=0;i<bs;i++){ ipad[i]=k0[i]^0x36; opad[i]=k0[i]^0x5c; }
    return sha256(concatBytes(opad, sha256(concatBytes(ipad, msgBytes))));
}
function XorShift128Plus(bytes){
    const dv=new DataView(bytes.buffer, bytes.byteOffset, 16);
    let s0=dv.getBigUint64(0,true), s1=dv.getBigUint64(8,true);
    if(s0===0n) s0=1n; if(s1===0n) s1=2n;
    const MASK=(1n<<64n)-1n;
    this.next=function(){ let x=s0; const y=s1; s0=y; x=(x^(x<<23n))&MASK; x=x^(x>>17n); x=x^y^(y>>26n); s1=x; return (x+y)&MASK; };
}
const PER_ORIGIN_KEY = hexToBytes(PER_ORIGIN_KEY_HEX);

try {
    const _origToString = Function.prototype.toString;
    const _nativeSet = new WeakSet();
    Object.defineProperty(Function.prototype, 'toString', {
        value: function() {
            if (this === Function.prototype.toString) return 'function toString() { [native code] }';
            if (_nativeSet.has(this)) return 'function ' + (this.name || '') + '() { [native code] }';
            return _origToString.call(this);
        }, writable: true, configurable: true
    });
    _nativeSet.add(Function.prototype.toString);
    window.__NB_NATIVE_SET__ = _nativeSet;
} catch(e) {}
function makeNative(fn) { if (window.__NB_NATIVE_SET__) window.__NB_NATIVE_SET__.add(fn); return fn; }

let _navProto; try { _navProto = Object.getPrototypeOf(navigator); } catch(e) {}
function defNav(prop, getter) {
    try { Object.defineProperty(_navProto, prop, { get: makeNative(getter), configurable: true }); }
    catch(e) { try { Object.defineProperty(navigator, prop, { get: makeNative(getter), configurable: true }); } catch(e2) {} }
}

defNav('userAgent', () => NAV.userAgent);
defNav('platform', () => NAV.platform);
defNav('appVersion', () => NAV.appVersion);
defNav('vendor', () => NAV.vendor);
defNav('productSub', () => NAV.productSub);
defNav('hardwareConcurrency', () => NAV.hardwareConcurrency);
defNav('deviceMemory', () => NAV.deviceMemory);
defNav('maxTouchPoints', () => NAV.maxTouchPoints);
defNav('language', () => NAV.languages[0]);
defNav('languages', () => Object.freeze(NAV.languages.slice()));
defNav('webdriver', () => NAV.webdriver);
defNav('doNotTrack', () => NAV.doNotTrack);
defNav('onLine', () => NAV.online);
defNav('cookieEnabled', () => NAV.cookieEnabled);
defNav('pdfViewerEnabled', () => NAV.pdfViewerEnabled);

if (NAV.userAgentData) {
    Object.defineProperty(navigator, 'userAgentData', {
        get: makeNative(() => ({
            brands: NAV.userAgentData.brands.map(b => ({ brand: b.brand, version: b.version })),
            mobile: NAV.userAgentData.mobile,
            platform: NAV.userAgentData.platform,
            getHighEntropyValues: makeNative(() => Promise.resolve({
                platform: NAV.userAgentData.platform, platformVersion: '10.0.0',
                architecture: 'x86', bitness: '64',
                fullVersionList: NAV.userAgentData.brands.map(b => ({ brand: b.brand, version: b.version + '.0.0.0' })),
            })),
        })), configurable: true,
    });
}

try {
    const _scrProto = Object.getPrototypeOf(screen);
    Object.defineProperty(_scrProto, 'width', { get: makeNative(() => SCR.width), configurable: true });
    Object.defineProperty(_scrProto, 'height', { get: makeNative(() => SCR.height), configurable: true });
    Object.defineProperty(_scrProto, 'availWidth', { get: makeNative(() => SCR.width), configurable: true });
    Object.defineProperty(_scrProto, 'availHeight', { get: makeNative(() => SCR.height), configurable: true });
    Object.defineProperty(_scrProto, 'colorDepth', { get: makeNative(() => SCR.colorDepth), configurable: true });
    Object.defineProperty(_scrProto, 'pixelDepth', { get: makeNative(() => SCR.pixelDepth), configurable: true });
    Object.defineProperty(window, 'devicePixelRatio', { get: makeNative(() => SCR.devicePixelRatio), configurable: true });
} catch(e) {}

try {
    function perturbImageData(data) {
        let uniform = true;
        for (let i = 4; i < data.length; i += 4) {
            if (data[i]!==data[0]||data[i+1]!==data[1]||data[i+2]!==data[2]||data[i+3]!==data[3]) { uniform=false; break; }
        }
        if (uniform) return;
        const callKey = hmacSha256(PER_ORIGIN_KEY, data);
        const rng1 = new XorShift128Plus(callKey.slice(0,16));
        const rng2 = new XorShift128Plus(callKey.slice(16,32));
        const pixelCount = data.length / 4;
        let numNoises = callKey[31]; if (numNoises < 20) numNoises = 20;
        for (let i = 0; i < numNoises; i++) {
            const pixel = Number(rng1.next() % BigInt(pixelCount));
            const channel = Number(rng1.next() % 4n);
            const bit = Number(rng2.next() & 1n);
            data[pixel*4+channel] ^= (0x2 >> bit);
        }
    }
    const _getImageData = CanvasRenderingContext2D.prototype.getImageData;
    CanvasRenderingContext2D.prototype.getImageData = makeNative(function(x, y, w, h) {
        const d = _getImageData.call(this, x, y, w, h); perturbImageData(d.data); return d;
    });
    const _toDataURL = HTMLCanvasElement.prototype.toDataURL;
    HTMLCanvasElement.prototype.toDataURL = makeNative(function(type, quality) {
        const ctx = this.getContext('2d');
        if (ctx && this.width > 0 && this.height > 0) {
            const d = ctx.getImageData(0, 0, this.width, this.height);
            ctx.putImageData(d, 0, 0);
        }
        return _toDataURL.call(this, type, quality);
    });
} catch(e) {}

try {
    function perturbFloat32(arr) {
        const bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
        const callKey = hmacSha256(PER_ORIGIN_KEY, bytes);
        const rng1 = new XorShift128Plus(callKey.slice(0,16));
        const rng2 = new XorShift128Plus(callKey.slice(16,32));
        const numNoises = (callKey[31] % 16) + 5;
        const view = new DataView(arr.buffer, arr.byteOffset, arr.byteLength);
        for (let i = 0; i < numNoises; i++) {
            const idx = Number(rng1.next() % BigInt(arr.length));
            const bit = Number(rng2.next() % 23n);
            let bits = view.getUint32(idx*4, true); bits ^= (1 << bit); view.setUint32(idx*4, bits, true);
        }
    }
    const _getChannelData = AudioBuffer.prototype.getChannelData;
    AudioBuffer.prototype.getChannelData = makeNative(function() { const arr = _getChannelData.apply(this, arguments); perturbFloat32(arr); return arr; });
    Object.defineProperty(AudioContext.prototype, 'sampleRate', { get: () => AUDIO.sampleRate, configurable: true });
    Object.defineProperty(AudioContext.prototype, 'baseLatency', { get: () => AUDIO.baseLatency, configurable: true });
} catch(e) {}

try {
    function patchGL(proto) {
        const orig = proto.getParameter;
        proto.getParameter = makeNative(function(param) {
            switch (param) {
                case 37445: case 0x1F00: return GL.vendor;
                case 37446: case 0x1F01: return GL.renderer;
                case 0x0D33: return GL.maxTextureSize;
                default: return orig.call(this, param);
            }
        });
        const origExt = proto.getExtension;
        proto.getExtension = makeNative(function(name) { if (!GL.extensions.includes(name)) return null; return origExt.call(this, name); });
        proto.getSupportedExtensions = makeNative(function() { return GL.extensions.slice(); });
    }
    if (window.WebGLRenderingContext) patchGL(WebGLRenderingContext.prototype);
    if (window.WebGL2RenderingContext) patchGL(WebGL2RenderingContext.prototype);
} catch(e) {}

try {
    const _DTF = Intl.DateTimeFormat;
    Intl.DateTimeFormat = new Proxy(_DTF, {
        construct(target, args) { if (!args[1]) args[1] = {}; if (!args[1].timeZone) args[1].timeZone = NAV.timezone; return new target(...args); }
    });
} catch(e) {}

})();
)JS";

    return script;
}