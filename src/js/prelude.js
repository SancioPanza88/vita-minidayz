/*
 * Minimal browser environment for running a Construct 2 export in "Ejecta" mode.
 * Everything here is backed by the native functions on the global __n object.
 */
'use strict';
var window = globalThis;
window.window = window;
window.self = window;
window.top = window;
window.parent = window;
window.c2ejecta = true; // makes Construct 2 use its DOM-free code paths

(function () {
  var N = __n;

  // ---------------------------------------------------------------- console
  function fmt(args) {
    var out = [];
    for (var i = 0; i < args.length; i++) {
      var a = args[i];
      if (a instanceof Error) out.push(a.message + '\n' + a.stack);
      else if (typeof a === 'object' && a !== null) { try { out.push(JSON.stringify(a)); } catch (e) { out.push(String(a)); } }
      else out.push(String(a));
    }
    return out.join(' ');
  }
  window.console = {
    log: function () { N.log(fmt(arguments)); },
    info: function () { N.log(fmt(arguments)); },
    debug: function () {},
    warn: function () { N.log('[warn] ' + fmt(arguments)); },
    error: function () { N.log('[error] ' + fmt(arguments)); },
    trace: function () {}, time: function () {}, timeEnd: function () {}, assert: function () {}
  };

  // ---------------------------------------------------------------- events
  function Event(type, init) {
    this.type = type;
    this.bubbles = !!(init && init.bubbles);
    this.defaultPrevented = false;
    this.timeStamp = N.now();
    this._stop = false;
  }
  Event.prototype.preventDefault = function () { this.defaultPrevented = true; };
  Event.prototype.stopPropagation = function () { this._stop = true; };
  Event.prototype.stopImmediatePropagation = function () { this._stop = true; };
  window.Event = Event;

  function EventTarget() {}
  EventTarget.prototype.addEventListener = function (type, fn) {
    if (!fn) return;
    var m = this._listeners || (this._listeners = {});
    (m[type] || (m[type] = [])).push(fn);
  };
  EventTarget.prototype.removeEventListener = function (type, fn) {
    var l = this._listeners && this._listeners[type];
    if (!l) return;
    var i = l.indexOf(fn);
    if (i >= 0) l.splice(i, 1);
  };
  EventTarget.prototype._fire = function (ev) {
    ev.currentTarget = this;
    var l = this._listeners && this._listeners[ev.type];
    if (l) {
      l = l.slice();
      for (var i = 0; i < l.length; i++) {
        try {
          if (typeof l[i] === 'function') l[i].call(this, ev); else if (l[i].handleEvent) l[i].handleEvent(ev);
        } catch (e) { console.error('event ' + ev.type + ': ', e); }
      }
    }
    var h = this['on' + ev.type];
    if (typeof h === 'function') { try { h.call(this, ev); } catch (e) { console.error('on' + ev.type + ': ', e); } }
  };
  EventTarget.prototype.dispatchEvent = function (ev) {
    if (!ev.target) ev.target = this;
    var node = this;
    while (node) {
      node._fire(ev);
      if (ev._stop || !ev.bubbles) break;
      node = node.parentNode || (node === document ? window : null);
    }
    return !ev.defaultPrevented;
  };
  window.EventTarget = EventTarget;
  function mixin(proto) { for (var k in EventTarget.prototype) proto[k] = EventTarget.prototype[k]; }
  mixin(window);
  window._listeners = {};

  // ---------------------------------------------------------------- timers
  var timers = [], timerSeq = 1, rafs = [], rafSeq = 1, clock = N.now();
  function addTimer(fn, ms, args, repeat) {
    var id = timerSeq++;
    ms = +ms || 0;
    timers.push({ id: id, fn: fn, at: clock + ms, ms: ms, args: args, repeat: repeat });
    return id;
  }
  window.setTimeout = function (fn, ms) { return addTimer(fn, ms, Array.prototype.slice.call(arguments, 2), false); };
  window.setInterval = function (fn, ms) { return addTimer(fn, Math.max(ms, 1), Array.prototype.slice.call(arguments, 2), true); };
  window.clearTimeout = window.clearInterval = function (id) {
    for (var i = 0; i < timers.length; i++) if (timers[i].id === id) { timers.splice(i, 1); return; }
  };
  window.requestAnimationFrame = function (fn) { var id = rafSeq++; rafs.push({ id: id, fn: fn }); return id; };
  window.cancelAnimationFrame = function (id) {
    for (var i = 0; i < rafs.length; i++) if (rafs[i].id === id) { rafs.splice(i, 1); return; }
  };
  window.setImmediate = function (fn) { return setTimeout(fn, 0); };
  window.performance = { now: function () { return N.now(); }, timing: { navigationStart: Date.now() } };

  function runTimers() {
    var due = [];
    for (var i = 0; i < timers.length; i++) if (timers[i].at <= clock) due.push(timers[i]);
    due.sort(function (a, b) { return a.at - b.at || a.id - b.id; });
    for (var j = 0; j < due.length; j++) {
      var t = due[j];
      if (timers.indexOf(t) < 0) continue;
      if (t.repeat) t.at = clock + t.ms; else timers.splice(timers.indexOf(t), 1);
      try { typeof t.fn === 'function' ? t.fn.apply(window, t.args) : (0, eval)(String(t.fn)); }
      catch (e) { console.error('timer: ', e); }
    }
  }

  // ---------------------------------------------------------------- DOM
  function Element(tag) {
    this.tagName = this.nodeName = (tag || 'div').toUpperCase();
    this.style = {};
    this.childNodes = this.children = [];
    this.parentNode = null;
    this.attributes = {};
    this.classList = { add: function () {}, remove: function () {}, contains: function () { return false; }, toggle: function () {} };
    this.dataset = {};
  }
  mixin(Element.prototype);
  Element.prototype.appendChild = function (c) { if (c) { this.children.push(c); c.parentNode = this; } return c; };
  Element.prototype.insertBefore = Element.prototype.appendChild;
  Element.prototype.removeChild = function (c) { var i = this.children.indexOf(c); if (i >= 0) this.children.splice(i, 1); if (c) c.parentNode = null; return c; };
  Element.prototype.remove = function () { if (this.parentNode) this.parentNode.removeChild(this); };
  Element.prototype.setAttribute = function (k, v) { this.attributes[k] = String(v); };
  Element.prototype.getAttribute = function (k) { return k in this.attributes ? this.attributes[k] : null; };
  Element.prototype.removeAttribute = function (k) { delete this.attributes[k]; };
  Element.prototype.getBoundingClientRect = function () {
    var w = this.width || window.innerWidth, h = this.height || window.innerHeight;
    return { left: 0, top: 0, x: 0, y: 0, right: w, bottom: h, width: w, height: h };
  };
  Element.prototype.focus = function () {};
  Element.prototype.blur = function () {};
  Element.prototype.click = function () {};
  Element.prototype.getElementsByTagName = function () { return []; };
  Element.prototype.querySelector = function () { return null; };
  Element.prototype.querySelectorAll = function () { return []; };
  Object.defineProperty(Element.prototype, 'offsetWidth', { get: function () { return this.width || window.innerWidth; } });
  Object.defineProperty(Element.prototype, 'offsetHeight', { get: function () { return this.height || window.innerHeight; } });
  Element.prototype.offsetLeft = 0;
  Element.prototype.offsetTop = 0;
  Element.prototype.clientLeft = 0;
  Element.prototype.clientTop = 0;
  window.HTMLElement = window.Element = Element;
  window.Node = Element;

  var document = new Element('#document');
  window.document = document;
  document.nodeType = 9;
  document.readyState = 'complete';
  document.hidden = false;
  document.visibilityState = 'visible';
  document.documentElement = new Element('html');
  document.head = new Element('head');
  document.body = new Element('body');
  document.documentElement.parentNode = document;
  document.documentElement.appendChild(document.head);
  document.documentElement.appendChild(document.body);
  document.location = null;
  document.title = 'Mini DAYZ';
  document.cookie = '';
  document.fonts = { load: function () { return Promise.resolve([]); }, ready: Promise.resolve() };
  var byId = {};
  document.getElementById = function (id) { return byId[id] || null; };
  document.getElementsByTagName = function (t) {
    t = String(t).toLowerCase();
    if (t === 'body') return [document.body];
    if (t === 'head') return [document.head];
    if (t === 'canvas') return mainCanvas ? [mainCanvas] : [];
    return [];
  };
  document.getElementsByClassName = function () { return []; };
  document.querySelector = function (s) {
    if (s === 'body') return document.body;
    if (s && s[0] === '#') return byId[s.slice(1)] || null;
    if (s === 'canvas') return mainCanvas;
    return null;
  };
  document.querySelectorAll = function (s) { var r = document.querySelector(s); return r ? [r] : []; };
  document.createElement = function (tag) {
    tag = String(tag).toLowerCase();
    if (tag === 'canvas') return new HTMLCanvasElement();
    if (tag === 'img') return new Image();
    if (tag === 'audio') return new Audio();
    var el = new Element(tag);
    if (tag === 'input' || tag === 'textarea' || tag === 'select') {
      el.value = ''; el.placeholder = ''; el.title = ''; el.disabled = false; el.readOnly = false; el.type = 'text';
      el.select = function () {}; el.setSelectionRange = function () {};
    }
    if (tag === 'select') {
      el.options = []; el.selectedIndex = -1; el.multiple = false; el.size = 0;
      el.add = function (o, before) { if (typeof before === 'number') this.options.splice(before, 0, o); else this.options.push(o); this.options.length && this.selectedIndex < 0 && (this.selectedIndex = 0); };
      el.remove = function (i) { if (typeof i === 'number') this.options.splice(i, 1); };
    }
    return el;
  };
  window.Option = function (text, value) { this.text = text || ''; this.value = value === undefined ? this.text : value; this.selected = false; };
  document.createElementNS = function (ns, tag) { return document.createElement(tag); };
  document.createTextNode = function (t) { var e = new Element('#text'); e.textContent = t; return e; };
  document.createEvent = function () { var e = new Event(''); e.initEvent = function (t, b) { this.type = t; this.bubbles = b; }; return e; };
  document.hasFocus = function () { return true; };
  document.activeElement = document.body;
  document.parentNode = null;

  window.location = { href: 'file:///index.html', protocol: 'file:', host: '', hostname: '', pathname: '/index.html', search: '', hash: '', origin: 'file://', reload: function () {}, replace: function () {} };
  document.location = window.location;
  window.history = { pushState: function () {}, replaceState: function () {}, back: function () {} };
  window.innerWidth = window.outerWidth = N.screenW;
  window.innerHeight = window.outerHeight = N.screenH;
  window.screen = { width: N.screenW, height: N.screenH, availWidth: N.screenW, availHeight: N.screenH, orientation: { angle: 0, type: 'landscape-primary', lock: function () { return Promise.resolve(); } } };
  window.devicePixelRatio = 1;
  window.scrollTo = function () {};
  window.focus = function () {};
  window.blur = function () {};
  window.open = function () { return null; };
  window.close = function () {};
  window.alert = function (m) { console.log('[alert] ' + m); };
  window.confirm = function () { return true; };
  window.prompt = function () { return null; };
  window.getComputedStyle = function (e) { return e && e.style ? e.style : {}; };
  window.matchMedia = function () { return { matches: false, addListener: function () {}, removeListener: function () {} }; };
  window.navigator = {
    userAgent: 'Mozilla/5.0 (PlayStation Vita) MiniDayZNative/1.0',
    platform: 'PSVita', vendor: '', appName: 'Netscape', appVersion: '5.0',
    language: N.lang || 'en-US', languages: [N.lang || 'en-US'], onLine: false, cookieEnabled: false,
    maxTouchPoints: 6, hardwareConcurrency: 1, standalone: true
  };

  // ---------------------------------------------------------------- storage
  var store = {};
  try { var s = N.storageLoad(); if (s) store = JSON.parse(s) || {}; } catch (e) { console.error('storage: ', e); }
  var storeDirty = false, storeLastSave = 0;
  window.localStorage = {
    getItem: function (k) { k = String(k); return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },
    setItem: function (k, v) { store[String(k)] = String(v); storeDirty = true; },
    removeItem: function (k) { delete store[String(k)]; storeDirty = true; },
    clear: function () { store = {}; storeDirty = true; },
    key: function (i) { return Object.keys(store)[i] || null; },
    get length() { return Object.keys(store).length; }
  };
  window.sessionStorage = {
    _s: {}, getItem: function (k) { return k in this._s ? this._s[k] : null; },
    setItem: function (k, v) { this._s[k] = String(v); }, removeItem: function (k) { delete this._s[k]; },
    clear: function () { this._s = {}; }, key: function (i) { return Object.keys(this._s)[i] || null; },
    get length() { return Object.keys(this._s).length; }
  };
  function flushStorage(force) {
    if (!storeDirty) return;
    var now = N.now();
    if (!force && now - storeLastSave < 1000) return;
    storeDirty = false; storeLastSave = now;
    N.storageSave(JSON.stringify(store));
  }
  window.__flushStorage = flushStorage;

  // Minimal IndexedDB (open/put/get/delete/deleteDatabase) on top of the same store.
  // Used by Construct's save states ("_C2SaveStates") and the game's reset script.
  // IDBKeyRange is intentionally absent so localforage keeps using localStorage.
  function IDBRequest() { this.readyState = 'pending'; this.result = undefined; this.error = null; }
  mixin(IDBRequest.prototype);
  function idbFire(req, type, result) {
    setTimeout(function () {
      req.readyState = 'done';
      if (type === 'success') req.result = result;
      var e = new Event(type); e.target = req; req._fire(e);
    }, 0);
  }
  function idbKey(db, st, k) { return '__idb:' + db + '/' + st + '/' + JSON.stringify(k); }
  function IDBObjectStore(db, name) { this._db = db; this.name = name; this.keyPath = (db._meta.stores[name] || {}).keyPath || null; }
  IDBObjectStore.prototype.put = function (v, k) {
    var key = this.keyPath ? v[this.keyPath] : k, r = new IDBRequest();
    store[idbKey(this._db.name, this.name, key)] = JSON.stringify(v); storeDirty = true;
    idbFire(r, 'success', key); return r;
  };
  IDBObjectStore.prototype.add = IDBObjectStore.prototype.put;
  IDBObjectStore.prototype.get = function (k) {
    var s = store[idbKey(this._db.name, this.name, k)], r = new IDBRequest();
    idbFire(r, 'success', s === undefined ? undefined : JSON.parse(s)); return r;
  };
  IDBObjectStore.prototype['delete'] = function (k) {
    delete store[idbKey(this._db.name, this.name, k)]; storeDirty = true;
    var r = new IDBRequest(); idbFire(r, 'success', undefined); return r;
  };
  IDBObjectStore.prototype.clear = function () {
    var p = '__idb:' + this._db.name + '/' + this.name + '/';
    for (var k in store) if (k.indexOf(p) === 0) delete store[k];
    storeDirty = true;
    var r = new IDBRequest(); idbFire(r, 'success', undefined); return r;
  };
  function IDBDatabase(name, meta) { this.name = name; this._meta = meta; this.version = meta.version; }
  mixin(IDBDatabase.prototype);
  Object.defineProperty(IDBDatabase.prototype, 'objectStoreNames', { get: function () {
    var n = Object.keys(this._meta.stores); n.contains = function (x) { return n.indexOf(x) >= 0; }; return n; } });
  IDBDatabase.prototype.createObjectStore = function (name, opts) {
    this._meta.stores[name] = { keyPath: opts && opts.keyPath || null };
    store['__idbmeta:' + this.name] = JSON.stringify(this._meta); storeDirty = true;
    return new IDBObjectStore(this, name);
  };
  IDBDatabase.prototype.transaction = function (names, mode) {
    var db = this, tx = { db: db, mode: mode || 'readonly', objectStore: function (n) { return new IDBObjectStore(db, n); } };
    mixin(tx);
    setTimeout(function () { var e = new Event('complete'); e.target = tx; EventTarget.prototype._fire.call(tx, e); }, 1);
    return tx;
  };
  IDBDatabase.prototype.close = function () {};
  window.indexedDB = {
    open: function (name, version) {
      var r = new IDBRequest(), mk = '__idbmeta:' + name;
      var meta = store[mk] ? JSON.parse(store[mk]) : null, upgrade = !meta;
      if (!meta) meta = { version: version || 1, stores: {} };
      var db = new IDBDatabase(name, meta);
      setTimeout(function () {
        r.result = db;
        if (upgrade) { var u = new Event('upgradeneeded'); u.target = r; u.oldVersion = 0; u.newVersion = meta.version; r._fire(u); }
        store[mk] = JSON.stringify(meta); storeDirty = true;
        idbFire(r, 'success', db);
      }, 0);
      return r;
    },
    deleteDatabase: function (name) {
      for (var k in store) if (k.indexOf('__idb:' + name + '/') === 0) delete store[k];
      delete store['__idbmeta:' + name]; storeDirty = true;
      var r = new IDBRequest(); idbFire(r, 'success', undefined); return r;
    }
  };

  // ---------------------------------------------------------------- XHR
  function XMLHttpRequest() {
    this.readyState = 0; this.status = 0; this.statusText = ''; this.responseType = '';
    this.response = null; this.responseText = ''; this.timeout = 0; this._headers = {};
  }
  mixin(XMLHttpRequest.prototype);
  XMLHttpRequest.prototype.open = function (method, url, async) {
    this._url = String(url); this._async = async !== false; this.readyState = 1;
  };
  XMLHttpRequest.prototype.setRequestHeader = function () {};
  XMLHttpRequest.prototype.overrideMimeType = function () {};
  XMLHttpRequest.prototype.getAllResponseHeaders = function () { return ''; };
  XMLHttpRequest.prototype.getResponseHeader = function () { return null; };
  XMLHttpRequest.prototype.abort = function () { this._aborted = true; };
  XMLHttpRequest.prototype.send = function () {
    var x = this;
    function done() {
      if (x._aborted) return;
      var url = x._url.replace(/^\.\//, '').replace(/^file:\/\/\//, '');
      var t0 = N.now();
      var data = null;
      if (url === 'data.js' && x.responseType !== 'arraybuffer') data = dataJsFromCache();
      if (data === null) data = (x.responseType === 'arraybuffer' || x.responseType === 'blob') ? N.readBinary(url) : N.readText(url);
      if (url === 'data.js' && data !== null && data !== DATA_MARKER) dataJsText = data;
      if (/\.js$|\.json$|\.xml$/.test(url)) console.log('[xhr] ' + url + ' read in ' + Math.round(N.now() - t0) + ' ms');
      x.readyState = 4;
      if (data === null) {
        x.status = 404; x.statusText = 'Not Found';
        console.warn('XHR 404: ' + url);
      } else {
        x.status = 200; x.statusText = 'OK';
        if (x.responseType === 'json') { try { x.response = JSON.parse(data); } catch (e) { x.response = null; } }
        else if (x.responseType === 'document') { x.response = null; x.responseText = data; }
        else { x.response = data; if (typeof data === 'string') x.responseText = data; }
      }
      var e = new Event('readystatechange'); e.target = x; x._fire(e);
      var l = new Event(x.status === 200 ? 'load' : 'error'); l.target = x;
      l.loaded = l.total = data ? (data.byteLength || data.length) : 0; l.lengthComputable = true;
      x._fire(l);
      var le = new Event('loadend'); le.target = x; x._fire(le);
      if (/data\.js$/.test(url)) console.log('[xhr] ' + url + ' handled in ' + Math.round(N.now() - t0) + ' ms total');
    }
    if (this._async) setTimeout(done, 0); else done();
  };
  window.XMLHttpRequest = XMLHttpRequest;

  // data.js (8.6 MB of JSON) is parsed once and kept as a QuickJS binary object:
  // much faster to load on the next start.
  var DATA_MARKER = '{"__mdz_cached_project":1}', dataJsText = null, dataJsCached = null;
  function dataKey() { return 'v1:' + N.fileSize('data.js'); }
  function dataJsFromCache() {
    var t = N.now(), o = N.objCacheRead('data', dataKey());
    if (!o) return null;
    console.log('[cache] data.js loaded from cache in ' + Math.round(N.now() - t) + ' ms');
    dataJsCached = o;
    return DATA_MARKER;
  }
  var origParse = JSON.parse;
  JSON.parse = function (text, reviver) {
    if (text === DATA_MARKER && dataJsCached) { var o = dataJsCached; dataJsCached = null; return o; }
    if (dataJsText !== null && typeof text === 'string' && text.length > 1000000 && dataJsText.indexOf(text.slice(0, 64)) >= 0) {
      dataJsText = null;
      var t = N.now(), r = origParse(text, reviver);
      console.log('[cache] data.js parsed in ' + Math.round(N.now() - t) + ' ms, writing cache');
      N.objCacheWrite('data', dataKey(), r);
      return r;
    }
    return origParse(text, reviver);
  };

  // ---------------------------------------------------------------- images
  var imageIndex = {};
  try { var ii = N.readText('mdz_images.json'); if (ii) imageIndex = JSON.parse(ii); } catch (e) { console.warn('image index: ' + e); }

  function Image(w, h) {
    this.tagName = this.nodeName = 'IMG';
    this.style = {};
    this.width = w || 0; this.height = h || 0;
    this.naturalWidth = 0; this.naturalHeight = 0;
    this.complete = false; this._src = ''; this.crossOrigin = null;
  }
  mixin(Image.prototype);
  Object.defineProperty(Image.prototype, 'src', {
    get: function () { return this._src; },
    set: function (v) {
      var img = this;
      img._src = String(v);
      img._path = img._src.replace(/^\.\//, '').replace(/^file:\/\/\//, '');
      img.complete = false;
      if (/^data:|^blob:/.test(img._src)) {
        img._path = null;
        setTimeout(function () { img.complete = true; img.width = img.naturalWidth = 1; img.height = img.naturalHeight = 1; img._fire(new Event('load')); }, 0);
        return;
      }
      var info = imageIndex[img._path] || N.imageInfo(img._path);
      setTimeout(function () {
        if (!info) { console.warn('image missing: ' + img._path); var e = new Event('error'); e.target = img; img._fire(e); return; }
        img.naturalWidth = img.width = info[0];
        img.naturalHeight = img.height = info[1];
        img.complete = true;
        nImgLoaded++;
        var l = new Event('load'); l.target = img; img._fire(l);
      }, 0);
    }
  });
  Image.prototype.decode = function () { return Promise.resolve(); };
  Image.prototype.setAttribute = function () {};
  window.Image = Image;
  window.HTMLImageElement = Image;

  // ---------------------------------------------------------------- canvas
  var mainCanvas = null;
  function HTMLCanvasElement() {
    Element.call(this, 'canvas');
    this._w = 300; this._h = 150; this._ctx = null; this._surf = 0;
  }
  HTMLCanvasElement.prototype = Object.create(Element.prototype);
  HTMLCanvasElement.prototype.constructor = HTMLCanvasElement;
  Object.defineProperty(HTMLCanvasElement.prototype, 'width', {
    get: function () { return this._w; },
    set: function (v) { v = Math.max(0, v | 0); this._w = v; if (this._surf) N.surfResize(this._surf, v, this._h); else if (this._gl) this._gl._resize(); }
  });
  Object.defineProperty(HTMLCanvasElement.prototype, 'height', {
    get: function () { return this._h; },
    set: function (v) { v = Math.max(0, v | 0); this._h = v; if (this._surf) N.surfResize(this._surf, this._w, v); else if (this._gl) this._gl._resize(); }
  });
  HTMLCanvasElement.prototype.getContext = function (type, attrs) {
    type = String(type);
    if (type === '2d') {
      if (this._gl) return null;
      if (!this._ctx) { this._surf = N.surfCreate(this._w, this._h); this._ctx = new CanvasRenderingContext2D(this); }
      return this._ctx;
    }
    if (type === 'webgl' || type === 'experimental-webgl') {
      if (this._ctx) return null;
      if (this !== mainCanvas) return null; // only the screen gets a GL context
      if (!this._gl) this._gl = new WebGLRenderingContext(this);
      return this._gl;
    }
    return null;
  };
  HTMLCanvasElement.prototype.toDataURL = function () { return 'data:,'; };
  HTMLCanvasElement.prototype.toBlob = function (cb) { setTimeout(function () { cb(null); }, 0); };
  window.HTMLCanvasElement = HTMLCanvasElement;
  window.__releaseCanvas = function (c) { if (c && c._surf) { N.surfFree(c._surf); c._surf = 0; c._ctx = null; } };

  // --- 2D context (software, used for text and small helpers)
  var NAMED = { black: '#000000', white: '#ffffff', red: '#ff0000', green: '#008000', blue: '#0000ff', yellow: '#ffff00',
    gray: '#808080', grey: '#808080', transparent: 'rgba(0,0,0,0)', dodgerblue: '#1e90ff', orange: '#ffa500', cyan: '#00ffff', magenta: '#ff00ff', lime: '#00ff00' };
  var colorCache = {};
  function parseColor(c) {
    if (typeof c !== 'string') return [0, 0, 0, 1];
    var r = colorCache[c];
    if (r) return r;
    var s = c.trim().toLowerCase();
    if (NAMED[s]) s = NAMED[s];
    var m;
    if (s[0] === '#') {
      if (s.length === 4) r = [parseInt(s[1] + s[1], 16), parseInt(s[2] + s[2], 16), parseInt(s[3] + s[3], 16), 1];
      else r = [parseInt(s.substr(1, 2), 16), parseInt(s.substr(3, 2), 16), parseInt(s.substr(5, 2), 16), s.length >= 9 ? parseInt(s.substr(7, 2), 16) / 255 : 1];
    } else if ((m = /rgba?\(([^)]*)\)/.exec(s))) {
      var p = m[1].split(',').map(function (x) { return parseFloat(x); });
      r = [p[0] | 0, p[1] | 0, p[2] | 0, p.length > 3 ? p[3] : 1];
    } else r = [0, 0, 0, 1];
    colorCache[c] = r;
    return r;
  }
  function parseFont(f) {
    var m = /(\d+(?:\.\d+)?)(px|pt|em)/.exec(f || '');
    var px = 10;
    if (m) { px = parseFloat(m[1]); if (m[2] === 'pt') px *= 4 / 3; else if (m[2] === 'em') px *= 16; }
    return { px: px, bold: /bold|[6-9]00/.test(f || '') };
  }
  function CanvasRenderingContext2D(canvas) {
    this.canvas = canvas;
    this._st = { fillStyle: '#000', strokeStyle: '#000', font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', globalAlpha: 1, tx: 0, ty: 0, sx: 1, sy: 1, lineWidth: 1, globalCompositeOperation: 'source-over' };
    this._stack = [];
    this.imageSmoothingEnabled = true;
    this._path = [];
  }
  var C2D = CanvasRenderingContext2D.prototype;
  ['fillStyle', 'strokeStyle', 'font', 'textAlign', 'textBaseline', 'globalAlpha', 'lineWidth', 'globalCompositeOperation'].forEach(function (k) {
    Object.defineProperty(C2D, k, { get: function () { return this._st[k]; }, set: function (v) { this._st[k] = v; } });
  });
  C2D.save = function () { var c = {}; for (var k in this._st) c[k] = this._st[k]; this._stack.push(c); };
  C2D.restore = function () { if (this._stack.length) this._st = this._stack.pop(); };
  C2D.translate = function (x, y) { this._st.tx += x * this._st.sx; this._st.ty += y * this._st.sy; };
  C2D.scale = function (x, y) { this._st.sx *= x; this._st.sy *= y; };
  C2D.rotate = function () {};
  C2D.setTransform = function (a, b, c, d, e, f) { var s = this._st; s.sx = a === undefined ? 1 : a; s.sy = d === undefined ? 1 : d; s.tx = e || 0; s.ty = f || 0; };
  C2D.resetTransform = function () { this.setTransform(1, 0, 0, 1, 0, 0); };
  C2D.transform = function (a, b, c, d, e, f) { this.translate(e, f); this.scale(a, d); };
  C2D._x = function (x) { return this._st.tx + x * this._st.sx; };
  C2D._y = function (y) { return this._st.ty + y * this._st.sy; };
  C2D._fill = function (x, y, w, h, style) {
    var c = parseColor(style), a = Math.round(c[3] * this._st.globalAlpha * 255);
    N.surfFill(this.canvas._surf, this._x(x), this._y(y), w * this._st.sx, h * this._st.sy, c[0], c[1], c[2], a);
  };
  C2D.fillRect = function (x, y, w, h) { this._fill(x, y, w, h, this._st.fillStyle); };
  C2D.clearRect = function (x, y, w, h) { N.surfClear(this.canvas._surf, this._x(x), this._y(y), w * this._st.sx, h * this._st.sy); };
  C2D.strokeRect = function (x, y, w, h) {
    var lw = this._st.lineWidth || 1, s = this._st.strokeStyle;
    this._fill(x - lw / 2, y - lw / 2, w + lw, lw, s); this._fill(x - lw / 2, y + h - lw / 2, w + lw, lw, s);
    this._fill(x - lw / 2, y - lw / 2, lw, h + lw, s); this._fill(x + w - lw / 2, y - lw / 2, lw, h + lw, s);
  };
  C2D._text = function (t, x, y, style) {
    var f = parseFont(this._st.font), c = parseColor(style), a = Math.round(c[3] * this._st.globalAlpha * 255);
    var al = this._st.textAlign, align = (al === 'center') ? 1 : (al === 'right' || al === 'end') ? 2 : 0;
    var bl = this._st.textBaseline, base = (bl === 'top' || bl === 'hanging') ? 1 : bl === 'middle' ? 2 : (bl === 'bottom' || bl === 'ideographic') ? 3 : 0;
    N.surfText(this.canvas._surf, String(t), this._x(x), this._y(y), f.px * this._st.sy, c[0], c[1], c[2], a, align, base, f.bold ? 1 : 0);
  };
  C2D.fillText = function (t, x, y) { this._text(t, x, y, this._st.fillStyle); };
  C2D.strokeText = function () {};
  C2D.measureText = function (t) {
    var f = parseFont(this._st.font);
    return { width: N.textWidth(String(t), f.px) + (f.bold ? 1 : 0), actualBoundingBoxAscent: f.px * 0.8, actualBoundingBoxDescent: f.px * 0.2 };
  };
  C2D.drawImage = function (img, a, b, c, d, e, f, g, h) {
    var sx, sy, sw, sh, dx, dy, dw, dh, iw = img.width, ih = img.height;
    if (arguments.length <= 3) { sx = 0; sy = 0; sw = iw; sh = ih; dx = a; dy = b; dw = iw; dh = ih; }
    else if (arguments.length <= 5) { sx = 0; sy = 0; sw = iw; sh = ih; dx = a; dy = b; dw = c; dh = d; }
    else { sx = a; sy = b; sw = c; sh = d; dx = e; dy = f; dw = g; dh = h; }
    var kind, src;
    if (img instanceof HTMLCanvasElement) { if (!img._surf) return; kind = 1; src = img._surf; }
    else if (img && img._path) { kind = 0; src = img._path; }
    else return;
    N.surfDraw(this.canvas._surf, kind, src, sx, sy, sw, sh, this._x(dx), this._y(dy), dw * this._st.sx, dh * this._st.sy, this._st.globalAlpha);
  };
  C2D.getImageData = function (x, y, w, h) {
    var buf = N.surfGet(this.canvas._surf, x | 0, y | 0, w | 0, h | 0);
    return { width: w, height: h, data: new Uint8ClampedArray(buf) };
  };
  C2D.createImageData = function (w, h) { if (typeof w === 'object') { h = w.height; w = w.width; } return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; };
  C2D.putImageData = function () {};
  C2D.beginPath = function () { this._path = []; };
  C2D.closePath = function () {};
  C2D.rect = function (x, y, w, h) { this._path.push([x, y, w, h]); };
  C2D.fill = function () { for (var i = 0; i < this._path.length; i++) { var r = this._path[i]; this._fill(r[0], r[1], r[2], r[3], this._st.fillStyle); } };
  C2D.stroke = function () {};
  C2D.clip = function () {};
  C2D.moveTo = C2D.lineTo = C2D.arc = C2D.arcTo = C2D.quadraticCurveTo = C2D.bezierCurveTo = C2D.ellipse = function () {};
  C2D.setLineDash = function () {};
  C2D.createLinearGradient = C2D.createRadialGradient = function () { return { addColorStop: function () {} }; };
  C2D.createPattern = function () { return '#000'; };
  C2D.isPointInPath = function () { return false; };
  window.CanvasRenderingContext2D = CanvasRenderingContext2D;

  // --- WebGL
  var GL = N.gl;
  function Obj(id) { this.__id = id; }
  function WebGLTexture(id) { this.__id = id; }
  function WebGLBuffer(id) { this.__id = id; }
  function WebGLProgram(id) { this.__id = id; }
  function WebGLShader(id) { this.__id = id; }
  function WebGLFramebuffer(id) { this.__id = id; }
  function WebGLRenderbuffer(id) { this.__id = id; }
  function WebGLUniformLocation(id) { this.__id = id; }
  window.WebGLTexture = WebGLTexture; window.WebGLBuffer = WebGLBuffer; window.WebGLProgram = WebGLProgram;
  window.WebGLShader = WebGLShader; window.WebGLFramebuffer = WebGLFramebuffer; window.WebGLUniformLocation = WebGLUniformLocation;
  function id(o) { return o ? o.__id : 0; }

  function WebGLRenderingContext(canvas) {
    this.canvas = canvas;
    this.drawingBufferWidth = N.screenW;
    this.drawingBufferHeight = N.screenH;
  }
  var W = WebGLRenderingContext.prototype;
  for (var k in __GL_CONSTS) { W[k] = __GL_CONSTS[k]; WebGLRenderingContext[k] = __GL_CONSTS[k]; }
  W._resize = function () {};
  W.getContextAttributes = function () { return { alpha: true, depth: false, stencil: false, antialias: false, premultipliedAlpha: true, preserveDrawingBuffer: false }; };
  W.isContextLost = function () { return false; };
  W.getSupportedExtensions = function () { return ['WEBGL_debug_renderer_info']; };
  W.getExtension = function (n) {
    if (n === 'WEBGL_debug_renderer_info') return { UNMASKED_VENDOR_WEBGL: 0x9245, UNMASKED_RENDERER_WEBGL: 0x9246 };
    return null;
  };
  W.getParameter = function (p) {
    var r = GL.getParameter(p);
    if (p === this.ALIASED_POINT_SIZE_RANGE || p === this.ALIASED_LINE_WIDTH_RANGE) return new Float32Array(r);
    if (p === this.VIEWPORT || p === this.MAX_VIEWPORT_DIMS) return new Int32Array(r);
    return r;
  };
  W.getShaderPrecisionFormat = function () { return { rangeMin: 127, rangeMax: 127, precision: 23 }; };
  W.activeTexture = GL.activeTexture;
  W.attachShader = function (p, s) { GL.attachShader(id(p), id(s)); };
  W.bindBuffer = function (t, b) { GL.bindBuffer(t, id(b)); };
  W.bindFramebuffer = function (t, b) { GL.bindFramebuffer(t, id(b)); };
  W.bindRenderbuffer = function (t, b) { GL.bindRenderbuffer(t, id(b)); };
  W.bindTexture = function (t, x) { GL.bindTexture(t, id(x)); };
  W.blendFunc = GL.blendFunc; W.blendFuncSeparate = GL.blendFuncSeparate; W.blendEquation = GL.blendEquation;
  W.blendEquationSeparate = function (a) { GL.blendEquation(a); };
  W.blendColor = function () {};
  W.clear = GL.clear; W.clearColor = GL.clearColor; W.clearDepth = GL.clearDepth; W.clearStencil = function () {};
  W.colorMask = function (r, g, b, a) { GL.colorMask(r ? 1 : 0, g ? 1 : 0, b ? 1 : 0, a ? 1 : 0); };
  W.compileShader = function (s) { GL.compileShader(id(s)); };
  W.createBuffer = function () { return new WebGLBuffer(GL.createBuffer()); };
  W.createTexture = function () { return new WebGLTexture(GL.createTexture()); };
  W.createFramebuffer = function () { return new WebGLFramebuffer(GL.createFramebuffer()); };
  W.createRenderbuffer = function () { return new WebGLRenderbuffer(GL.createRenderbuffer()); };
  W.createProgram = function () { return new WebGLProgram(GL.createProgram()); };
  W.createShader = function (t) { return new WebGLShader(GL.createShader(t)); };
  W.deleteBuffer = function (b) { GL.deleteBuffer(id(b)); };
  W.deleteTexture = function (t) { GL.deleteTexture(id(t)); };
  W.deleteFramebuffer = function (b) { GL.deleteFramebuffer(id(b)); };
  W.deleteRenderbuffer = function (b) { GL.deleteRenderbuffer(id(b)); };
  W.deleteProgram = function (p) { GL.deleteProgram(id(p)); };
  W.deleteShader = function (s) { GL.deleteShader(id(s)); };
  W.depthFunc = GL.depthFunc; W.depthMask = function (f) { GL.depthMask(f ? 1 : 0); };
  W.disable = GL.disable; W.enable = GL.enable;
  W.disableVertexAttribArray = GL.disableVertexAttribArray; W.enableVertexAttribArray = GL.enableVertexAttribArray;
  W.drawArrays = GL.drawArrays; W.drawElements = GL.drawElements;
  W.framebufferTexture2D = function (t, a, tt, tex, l) { GL.framebufferTexture2D(t, a, tt, id(tex), l); };
  W.framebufferRenderbuffer = function (t, a, rt, rb) { GL.framebufferRenderbuffer(t, a, rt, id(rb)); };
  W.renderbufferStorage = GL.renderbufferStorage;
  W.checkFramebufferStatus = GL.checkFramebufferStatus;
  W.generateMipmap = GL.generateMipmap;
  W.getError = GL.getError;
  W.linkProgram = function (p) { GL.linkProgram(id(p)); };
  W.pixelStorei = function (p, v) { GL.pixelStorei(p, typeof v === 'boolean' ? (v ? 1 : 0) : v); };
  W.scissor = GL.scissor; W.viewport = GL.viewport;
  W.shaderSource = function (s, src) { GL.shaderSource(id(s), src); };
  W.getShaderParameter = function (s, p) { var r = GL.getShaderParameter(id(s), p); return (p === this.COMPILE_STATUS || p === this.DELETE_STATUS) ? !!r : r; };
  W.getProgramParameter = function (pr, p) { var r = GL.getProgramParameter(id(pr), p); return (p === this.LINK_STATUS || p === this.DELETE_STATUS || p === this.VALIDATE_STATUS) ? !!r : r; };
  W.getShaderInfoLog = function (s) { return GL.getShaderInfoLog(id(s)); };
  W.getProgramInfoLog = function (p) { return GL.getProgramInfoLog(id(p)); };
  W.getAttribLocation = function (p, n) { return GL.getAttribLocation(id(p), n); };
  W.getUniformLocation = function (p, n) { var l = GL.getUniformLocation(id(p), n); return l < 0 ? null : new WebGLUniformLocation(l); };
  W.texParameteri = GL.texParameteri; W.texParameterf = GL.texParameterf;
  W.uniform1f = function (l, x) { if (l) GL.uniform1f(l.__id, x); };
  W.uniform2f = function (l, x, y) { if (l) GL.uniform2f(l.__id, x, y); };
  W.uniform3f = function (l, x, y, z) { if (l) GL.uniform3f(l.__id, x, y, z); };
  W.uniform4f = function (l, x, y, z, w) { if (l) GL.uniform4f(l.__id, x, y, z, w); };
  W.uniform1i = function (l, x) { if (l) GL.uniform1i(l.__id, x); };
  W.uniform1fv = function (l, v) { if (l) GL.uniform1fv(l.__id, v instanceof Float32Array ? v : new Float32Array(v)); };
  W.uniformMatrix4fv = function (l, t, v) { if (l) GL.uniformMatrix4fv(l.__id, t ? 1 : 0, v instanceof Float32Array ? v : new Float32Array(v)); };
  W.useProgram = function (p) { GL.useProgram(id(p)); };
  W.vertexAttribPointer = function (i, s, t, n, st, o) { GL.vertexAttribPointer(i, s, t, n ? 1 : 0, st, o); };
  W.bufferData = function (t, d, u) { GL.bufferData(t, d, u); };
  W.bufferSubData = function (t, o, d) { GL.bufferSubData(t, o, d); };
  W.lineWidth = GL.lineWidth; W.flush = GL.flush; W.finish = GL.finish;
  W.hint = function () {}; W.stencilFunc = W.stencilOp = W.stencilMask = W.frontFace = W.cullFace = W.polygonOffset = W.sampleCoverage = function () {};
  W.isEnabled = function () { return false; };
  W.readPixels = function (x, y, w, h, f, t, buf) { GL.readPixels(x, y, w, h, f, t, buf); };
  function srcOf(s) {
    if (s instanceof HTMLCanvasElement) { if (!s._surf) s.getContext('2d'); return [1, s._surf]; }
    if (s && s._path) return [0, s._path];
    return null;
  }
  W.texImage2D = function (t, l, ifmt, a, b, c, d, e, f) {
    if (arguments.length === 6) {
      var s = srcOf(c);
      if (s) GL.texImage2DSource(t, l, ifmt, a, b, s[0], s[1]);
      else if (c && c.data && c.width) GL.texImage2DData(t, l, ifmt, c.width, c.height, 0, a, b, c.data);
      else GL.texImage2DData(t, l, ifmt, 1, 1, 0, a, b, new Uint8Array(4));
    } else {
      GL.texImage2DData(t, l, ifmt, a, b, c, d, e, f === undefined ? null : f);
    }
  };
  W.texSubImage2D = function (t, l, x, y, a, b, c, d, e) {
    if (arguments.length === 7) {
      var s = srcOf(c);
      if (s) GL.texSubImage2DSource(t, l, x, y, a, b, s[0], s[1]);
    }
  };
  window.WebGLRenderingContext = WebGLRenderingContext;

  // ---------------------------------------------------------------- audio
  var voices = {}; // native voice handle -> owner (source node or media element)
  function AudioParam(v) { this.value = v; this.defaultValue = v; }
  AudioParam.prototype.setValueAtTime = function (v) { this.value = v; return this; };
  AudioParam.prototype.linearRampToValueAtTime = function (v) { this.value = v; return this; };
  AudioParam.prototype.exponentialRampToValueAtTime = function (v) { this.value = v; return this; };
  AudioParam.prototype.setTargetAtTime = function (v) { this.value = v; return this; };
  AudioParam.prototype.setValueCurveAtTime = function () { return this; };
  AudioParam.prototype.cancelScheduledValues = function () { return this; };
  AudioParam.prototype.cancelAndHoldAtTime = function () { return this; };

  function AudioNode(ctx) { this.context = ctx; this._out = []; this.channelCount = 2; this.numberOfInputs = 1; this.numberOfOutputs = 1; }
  mixin(AudioNode.prototype);
  AudioNode.prototype.connect = function (n) { if (n && this._out.indexOf(n) < 0) this._out.push(n); return n; };
  AudioNode.prototype.disconnect = function (n) { if (n === undefined || typeof n === 'number') this._out = []; else { var i = this._out.indexOf(n); if (i >= 0) this._out.splice(i, 1); } };
  AudioNode.prototype._gain = function () { return 1; };
  AudioNode.prototype._pan = function () { return 0; };
  // gain/pan along the path to the destination (first route found)
  function routeOf(node, depth) {
    if (!node || depth > 16) return null;
    if (node._isDest) return [1, 0];
    for (var i = 0; i < node._out.length; i++) {
      var r = routeOf(node._out[i], depth + 1);
      if (r) return [r[0] * node._gain(), r[1] || node._pan()];
    }
    return null;
  }

  function GainNode(ctx) { AudioNode.call(this, ctx); this.gain = new AudioParam(1); }
  GainNode.prototype = Object.create(AudioNode.prototype);
  GainNode.prototype._gain = function () { var g = this.gain.value; return g > 0 ? g : 0; };
  function PannerNode(ctx) {
    AudioNode.call(this, ctx);
    this.panningModel = 'equalpower'; this.distanceModel = 'inverse'; this.refDistance = 1; this.maxDistance = 10000; this.rolloffFactor = 1;
    this.coneInnerAngle = 360; this.coneOuterAngle = 0; this.coneOuterGain = 0;
    this._x = 0; this._y = 0; this._z = 0;
    this.positionX = new AudioParam(0); this.positionY = new AudioParam(0); this.positionZ = new AudioParam(0);
    this.orientationX = new AudioParam(1); this.orientationY = new AudioParam(0); this.orientationZ = new AudioParam(0);
  }
  PannerNode.prototype = Object.create(AudioNode.prototype);
  PannerNode.prototype.setPosition = function (x, y, z) { this.positionX.value = x; this.positionY.value = y; this.positionZ.value = z; };
  PannerNode.prototype.setOrientation = function () {};
  PannerNode.prototype.setVelocity = function () {};
  PannerNode.prototype._rel = function () {
    var L = this.context.listener;
    return [this.positionX.value - L.positionX.value, this.positionY.value - L.positionY.value, this.positionZ.value - L.positionZ.value];
  };
  PannerNode.prototype._gain = function () {
    var r = this._rel(), d = Math.sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    var ref = this.refDistance || 1, roll = this.rolloffFactor;
    d = Math.max(d, ref);
    if (this.maxDistance) d = Math.min(d, this.maxDistance);
    if (this.distanceModel === 'linear') return Math.max(0, 1 - roll * (d - ref) / Math.max(1, (this.maxDistance - ref)));
    if (this.distanceModel === 'exponential') return Math.pow(d / ref, -roll);
    return ref / (ref + roll * (d - ref));
  };
  PannerNode.prototype._pan = function () {
    var r = this._rel(), d = Math.sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    return d > 0.0001 ? Math.max(-1, Math.min(1, r[0] / d)) * 0.8 : 0;
  };
  function StereoPannerNode(ctx) { AudioNode.call(this, ctx); this.pan = new AudioParam(0); }
  StereoPannerNode.prototype = Object.create(AudioNode.prototype);
  StereoPannerNode.prototype._pan = function () { return this.pan.value; };
  function PassNode(ctx) {
    AudioNode.call(this, ctx);
    this.gain = new AudioParam(1); this.frequency = new AudioParam(350); this.Q = new AudioParam(1); this.detune = new AudioParam(0);
    this.delayTime = new AudioParam(0); this.threshold = new AudioParam(-24); this.knee = new AudioParam(30); this.ratio = new AudioParam(12);
    this.attack = new AudioParam(0.003); this.release = new AudioParam(0.25); this.reduction = 0; this.buffer = null; this.normalize = true;
    this.fftSize = 2048; this.frequencyBinCount = 1024; this.type = 'lowpass'; this.curve = null; this.oversample = 'none';
  }
  PassNode.prototype = Object.create(AudioNode.prototype);
  PassNode.prototype.getByteFrequencyData = PassNode.prototype.getFloatFrequencyData = PassNode.prototype.getByteTimeDomainData = PassNode.prototype.getFloatTimeDomainData = function () {};
  PassNode.prototype.setPeriodicWave = function () {};

  function AudioBuffer(bufId) {
    var info = N.audInfo(bufId);
    this._id = bufId;
    this.length = info[0]; this.sampleRate = info[1]; this.numberOfChannels = info[2];
    this.duration = info[0] / info[1];
  }
  AudioBuffer.prototype.getChannelData = function () { return new Float32Array(0); };

  function AudioBufferSourceNode(ctx) {
    AudioNode.call(this, ctx);
    this.buffer = null; this.loop = false; this.loopStart = 0; this.loopEnd = 0;
    this.playbackRate = new AudioParam(1); this.detune = new AudioParam(0);
    this._v = -1; this._started = false; this._ended = false;
  }
  AudioBufferSourceNode.prototype = Object.create(AudioNode.prototype);
  AudioBufferSourceNode.prototype.start = function (when, offset) {
    if (this._started) return;
    this._started = true;
    var self = this;
    var delay = Math.max(0, ((when || 0) - this.context.currentTime) * 1000);
    var go = function () {
      if (self._stopped || !self.buffer) { self._end(); return; }
      var r = routeOf(self, 0) || [0, 0];
      self._v = N.audPlay(self.buffer._id, self.loop ? 1 : 0, r[0], self.playbackRate.value, offset || 0, r[1]);
      if (self._v < 0) { self._end(); return; }
      voices[self._v] = self; self._lg = r[0]; self._lp = r[1]; self._lr = self.playbackRate.value; self._ll = self.loop;
    };
    if (delay > 5) setTimeout(go, delay); else go();
  };
  AudioBufferSourceNode.prototype.noteOn = AudioBufferSourceNode.prototype.start;
  AudioBufferSourceNode.prototype.noteGrainOn = function (w, o) { this.start(w, o); };
  AudioBufferSourceNode.prototype.stop = function (when) {
    var self = this;
    var delay = Math.max(0, ((when || 0) - this.context.currentTime) * 1000);
    var go = function () {
      self._stopped = true;
      if (self._v >= 0) { N.audStop(self._v); delete voices[self._v]; self._v = -1; self._end(); }
    };
    if (delay > 5) setTimeout(go, delay); else go();
  };
  AudioBufferSourceNode.prototype.noteOff = AudioBufferSourceNode.prototype.stop;
  AudioBufferSourceNode.prototype._end = function () {
    if (this._ended) return;
    this._ended = true;
    var e = new Event('ended'); e.target = this; this._fire(e);
  };
  AudioBufferSourceNode.prototype._update = function () {
    var r = routeOf(this, 0) || [0, 0];
    var rate = this.playbackRate.value;
    if (r[0] !== this._lg || r[1] !== this._lp || rate !== this._lr || this.loop !== this._ll) {
      this._lg = r[0]; this._lp = r[1]; this._lr = rate; this._ll = this.loop;
      N.audSet(this._v, r[0], rate, r[1], 0, this.loop ? 1 : 0);
    }
  };

  function MediaElementSourceNode(ctx, el) { AudioNode.call(this, ctx); this.mediaElement = el; el._node = this; }
  MediaElementSourceNode.prototype = Object.create(AudioNode.prototype);

  var audioStart = N.now();
  function AudioContext() {
    this.sampleRate = 48000;
    this.state = 'running';
    this.destination = new AudioNode(this);
    this.destination._isDest = true;
    this.destination.maxChannelCount = 2;
    this.listener = {
      positionX: new AudioParam(0), positionY: new AudioParam(0), positionZ: new AudioParam(0),
      forwardX: new AudioParam(0), forwardY: new AudioParam(0), forwardZ: new AudioParam(-1),
      upX: new AudioParam(0), upY: new AudioParam(1), upZ: new AudioParam(0),
      setPosition: function (x, y, z) { this.positionX.value = x; this.positionY.value = y; this.positionZ.value = z; },
      setOrientation: function () {}
    };
  }
  mixin(AudioContext.prototype);
  Object.defineProperty(AudioContext.prototype, 'currentTime', { get: function () { return (N.now() - audioStart) / 1000; } });
  AudioContext.prototype.createGain = AudioContext.prototype.createGainNode = function () { return new GainNode(this); };
  AudioContext.prototype.createPanner = function () { return new PannerNode(this); };
  AudioContext.prototype.createStereoPanner = function () { return new StereoPannerNode(this); };
  AudioContext.prototype.createBufferSource = function () { return new AudioBufferSourceNode(this); };
  AudioContext.prototype.createMediaElementSource = function (el) { return new MediaElementSourceNode(this, el); };
  ['createBiquadFilter', 'createDelay', 'createDelayNode', 'createConvolver', 'createDynamicsCompressor', 'createAnalyser', 'createWaveShaper', 'createOscillator', 'createChannelSplitter', 'createChannelMerger'].forEach(function (n) {
    AudioContext.prototype[n] = function () { var p = new PassNode(this); p.start = p.stop = function () {}; return p; };
  });
  AudioContext.prototype.createBuffer = function (ch, len, rate) { return { numberOfChannels: ch, length: len, sampleRate: rate, duration: len / rate, _id: -1, getChannelData: function () { return new Float32Array(len); } }; };
  AudioContext.prototype.createPeriodicWave = function () { return {}; };
  AudioContext.prototype.decodeAudioData = function (ab, ok, fail) {
    var self = this;
    return new Promise(function (resolve, reject) {
      setTimeout(function () {
        var idb = N.audDecode(ab);
        if (idb < 0) { var err = new Error('decode failed'); if (fail) fail(err); reject(err); return; }
        var b = new AudioBuffer(idb);
        if (ok) ok(b);
        resolve(b);
      }, 0);
    });
  };
  AudioContext.prototype.resume = function () { this.state = 'running'; return Promise.resolve(); };
  AudioContext.prototype.suspend = function () { return Promise.resolve(); };
  AudioContext.prototype.close = function () { return Promise.resolve(); };
  window.AudioContext = window.webkitAudioContext = AudioContext;

  // HTMLAudioElement backed by the same native voices
  function Audio(src) {
    Element.call(this, 'audio');
    this._src = ''; this._buf = -1; this._v = -1; this.paused = true; this.ended = false; this.loop = false;
    this.volume = 1; this.muted = false; this.playbackRate = 1; this.autoplay = false; this.preload = 'auto';
    this.readyState = 0; this.duration = NaN; this._offset = 0; this._node = null;
    if (src) this.src = src;
  }
  Audio.prototype = Object.create(Element.prototype);
  Audio.prototype.constructor = Audio;
  Audio.prototype.canPlayType = function (t) { return /ogg/.test(t) ? 'probably' : ''; };
  Object.defineProperty(Audio.prototype, 'src', {
    get: function () { return this._src; },
    set: function (v) {
      var self = this;
      this._src = String(v);
      this._stopVoice();
      var path = this._src.replace(/^\.\//, '').replace(/^file:\/\/\//, '');
      setTimeout(function () {
        var ab = N.readBinary(path);
        var idb = ab ? N.audDecode(ab) : -1;
        if (idb < 0) { console.warn('audio missing: ' + path); var e = new Event('error'); e.target = self; self._fire(e); return; }
        self._buf = idb;
        var info = N.audInfo(idb);
        self.duration = info[0] / info[1];
        self.readyState = 4;
        ['loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough'].forEach(function (n) { var e = new Event(n); e.target = self; self._fire(e); });
        if (self.autoplay || self._wantPlay) self.play();
      }, 0);
    }
  });
  Object.defineProperty(Audio.prototype, 'currentTime', {
    get: function () { return this._v >= 0 ? N.audPos(this._v) : this._offset; },
    set: function (t) { this._offset = +t || 0; if (this._v >= 0) { this._stopVoice(); this._start(); } }
  });
  Audio.prototype._gain = function () {
    var g = this.muted ? 0 : this.volume, p = 0;
    if (this._node) { var r = routeOf(this._node, 0); if (!r) return [0, 0]; g *= r[0]; p = r[1]; }
    return [g, p];
  };
  Audio.prototype._start = function () {
    var r = this._gain();
    this._v = N.audPlay(this._buf, this.loop ? 1 : 0, r[0], this.playbackRate || 1, this._offset, r[1]);
    if (this._v >= 0) { voices[this._v] = this; this._lg = r[0]; this._lp = r[1]; this._lr = this.playbackRate; this._ll = this.loop; this._lpaused = false; }
  };
  Audio.prototype._stopVoice = function () { if (this._v >= 0) { this._offset = N.audPos(this._v); N.audStop(this._v); delete voices[this._v]; this._v = -1; } };
  Audio.prototype.play = function () {
    this.paused = false; this.ended = false;
    if (this._buf < 0) { this._wantPlay = true; return Promise.resolve(); }
    if (this._v < 0) this._start();
    var e = new Event('play'); e.target = this; this._fire(e);
    e = new Event('playing'); e.target = this; this._fire(e);
    return Promise.resolve();
  };
  Audio.prototype.pause = function () {
    this._wantPlay = false;
    if (!this.paused) { this.paused = true; this._stopVoice(); var e = new Event('pause'); e.target = this; this._fire(e); }
  };
  Audio.prototype.load = function () {};
  Audio.prototype._end = function () {
    this._v = -1; this._offset = 0; this.paused = true; this.ended = true;
    var e = new Event('ended'); e.target = this; this._fire(e);
  };
  Audio.prototype._update = function () {
    var r = this._gain();
    if (r[0] !== this._lg || r[1] !== this._lp || this.playbackRate !== this._lr || this.loop !== this._ll) {
      this._lg = r[0]; this._lp = r[1]; this._lr = this.playbackRate; this._ll = this.loop;
      N.audSet(this._v, r[0], this.playbackRate || 1, r[1], 0, this.loop ? 1 : 0);
    }
  };
  window.Audio = Audio;
  window.HTMLAudioElement = Audio;
  window.HTMLMediaElement = Audio;

  function updateAudio() {
    var ended = N.audEnded();
    for (var i = 0; i < ended.length; i++) {
      var o = voices[ended[i]];
      delete voices[ended[i]];
      if (o) { o._v = -1; o._end(); }
    }
    for (var k in voices) { var v = voices[k]; if (v._v >= 0) v._update(); }
  }

  // ---------------------------------------------------------------- jQuery stub
  function JQ(sel) {
    if (typeof sel === 'function') { setTimeout(sel, 0); return this; }
    this.el = sel; this.length = sel ? 1 : 0; this[0] = typeof sel === 'string' ? document.querySelector(sel) : sel;
  }
  var noop = function () { return this; };
  ['css', 'appendTo', 'append', 'show', 'hide', 'remove', 'focus', 'blur', 'keydown', 'keyup', 'mousedown', 'mouseup', 'mousemove', 'dblclick', 'click', 'on', 'off', 'bind', 'unbind', 'attr', 'addClass', 'removeClass', 'prop', 'val', 'text', 'html'].forEach(function (n) { JQ.prototype[n] = noop; });
  JQ.prototype.ready = function (fn) { setTimeout(fn, 0); return this; };
  JQ.prototype.parents = function () { return this; };
  JQ.prototype.offset = function () { return { left: 0, top: 0 }; };
  JQ.prototype.position = JQ.prototype.offset;
  JQ.prototype.width = function () { return window.innerWidth; };
  JQ.prototype.height = function () { return window.innerHeight; };
  JQ.prototype.each = function (fn) { if (this[0]) fn.call(this[0], 0, this[0]); return this; };
  window.jQuery = window.$ = function (s) { return new JQ(s); };
  window.jQuery.fn = JQ.prototype;
  window.jQuery.extend = function (a, b) { for (var k in b) a[k] = b[k]; return a; };
  window.jQuery.ajax = function () {};

  // ---------------------------------------------------------------- input
  var touches = {}; // identifier -> touch
  function mkTouch(id, x, y) {
    return { identifier: id, pageX: x, pageY: y, clientX: x, clientY: y, screenX: x, screenY: y, target: mainCanvas, force: 1, radiusX: 4, radiusY: 4 };
  }
  window.__touch = function (type, id, x, y) {
    var t;
    if (type === 2) { t = touches[id]; if (!t) return; delete touches[id]; }
    else { t = mkTouch(id, x, y); touches[id] = t; }
    var name = type === 0 ? 'touchstart' : type === 1 ? 'touchmove' : 'touchend';
    var ev = new Event(name, { bubbles: true });
    ev.changedTouches = [t];
    ev.touches = Object.keys(touches).map(function (k) { return touches[k]; });
    ev.targetTouches = ev.touches;
    ev.target = mainCanvas;
    mainCanvas.dispatchEvent(ev);
  };
  window.TouchEvent = Event;

  // ---------------------------------------------------------------- boot + frame
  // Construct 2 disables its XML plugin in DOM-free mode, but the game needs it
  // (localised text). We provide DOMParser/XPath (xml.js), so re-enable it.
  function patchRuntime(src) {
    var ej = /this\.(\w+)\s*=\s*!!window(?:\.c2ejecta|\["c2ejecta"\])/.exec(src);
    var df = ej && new RegExp('this\\.(\\w+)\\s*=\\s*\\(?\\s*this\\.\\w+\\s*\\|\\|\\s*this\\.\\w+\\s*\\|\\|\\s*this\\.' + ej[1] + '\\s*\\)?\\s*;').exec(src);
    var anchor = src.indexOf('The XML object is not supported');
    if (!df || anchor < 0) { console.warn('runtime patch: XML plugin not found'); return src; }
    var start = src.lastIndexOf('function', src.lastIndexOf('XPathResult', anchor));
    var dp = src.indexOf('DOMParser', anchor);
    var end = dp < 0 ? -1 : src.indexOf('})()', dp);
    if (start < 0 || end < 0) { console.warn('runtime patch: XML block bounds not found'); return src; }
    var block = src.slice(start, end);
    var n = 0;
    block = block.replace(new RegExp('(this\\.\\w+)\\.' + df[1] + '\\b', 'g'), function (m, a) { n++; return a + '.__xmlDomFree'; });
    console.log('runtime patch: XML plugin enabled (' + n + ' checks, flag "' + df[1] + '")');
    src = src.slice(0, start) + block + src.slice(end);
    // Textbox/List objects are skipped in DOM-free mode, but the game's save states
    // serialise them; create them on top of our (invisible) dummy DOM instead.
    src = src.replace(/if\s*\(\s*this\.[\w.]+\s*\)\s*(\{\s*)?([\w.]+\(\s*"\[Construct 2\] (?:Textbox|List) plugin not supported)/g,
      function (m, brace, rest) { return 'if(false)' + (brace || '') + rest; });
    return src;
  }

  window.__boot = function () {
    var t = N.now();
    var src = N.readText('c2runtime.js');
    if (src === null) throw new Error('c2runtime.js not found in the game folder');
    N.evalCached(patchRuntime(src), 'c2runtime');
    console.log('[boot] runtime evaluated in ' + Math.round(N.now() - t) + ' ms');
    var div = new Element('div');
    div.id = 'c2canvasdiv';
    document.body.appendChild(div);
    mainCanvas = new HTMLCanvasElement();
    mainCanvas.id = 'c2canvas';
    mainCanvas.width = N.screenW;
    mainCanvas.height = N.screenH;
    div.appendChild(mainCanvas);
    byId.c2canvasdiv = div;
    byId.c2canvas = mainCanvas;
    t = N.now();
    window.cr_createRuntime('c2canvas');
    console.log('[boot] runtime created in ' + Math.round(N.now() - t) + ' ms');
  };

  var frameNo = 0, beatT = 0, beatN = 0, beatW = 0, nImgLoaded = 0;
  window.__imgLoaded = function () { nImgLoaded++; };
  window.__frame = function (now) {
    clock = now;
    frameNo++;
    var fs = N.now();
    runTimers();
    var list = rafs; rafs = [];
    for (var i = 0; i < list.length; i++) {
      try { list[i].fn(now); } catch (e) { console.error('frame: ', e); }
    }
    updateAudio();
    flushStorage(false);
    var fd = N.now() - fs;
    beatN++; beatT += fd; if (fd > beatW) beatW = fd;
    if (frameNo <= 5 || fd > 2000 || beatN >= 300) {
      console.log('[frame ' + frameNo + '] t=' + Math.round(now / 1000) + 's js avg ' + (beatT / beatN).toFixed(1) + ' ms, max ' + Math.round(beatW) + ' ms, images ' + nImgLoaded);
      beatN = 0; beatT = 0; beatW = 0;
    }
  };
})();
