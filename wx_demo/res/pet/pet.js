// wx_demo 的薄宿主：保留原 DSH 客户端、动作控制器和命中逻辑，只有本地 API 与原生桥在此适配。
(function () {
  'use strict';
  const API = '/api/live2d-pet';
  const nativeFetch = window.fetch.bind(window);
  const NativeEventSource = window.EventSource;
  const NativeAudio = window.Audio;
  const disposers = [];
  const audios = new Set();
  const streams = new Set();
  const state = {
    app: null, model: null, vendor: null, alive: true, visible: true,
    applied: false, loaded: false, painted: false, ready: false,
    phase: 'idle', assistantState: 'idle', renders: 0, errors: [],
    sampleAt: 0, opaquePixels: 0, bootAt: Date.now(), unavailableSent: false,
    applyModelVisibility: null, catalog: null, error: '',
  };
  let watchTimer = 0;
  let deadlineTimer = 0;
  let doneTimer = 0;
  let observer = null;
  let moduleExports = null;
  let shared = readStore('wx-demo.pet.settings.v1', { rev: 1 });
  let customSounds = readStore('wx-demo.pet.sounds.v1', []);
  if (!shared || typeof shared !== 'object' || Array.isArray(shared)) shared = { rev: 1 };
  if (!Array.isArray(customSounds)) customSounds = [];

  function readStore(key, fallback) {
    try { return JSON.parse(localStorage.getItem(key) || 'null') ?? fallback; }
    catch (_) { return fallback; }
  }
  function writeStore(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); }
    catch (_) { throw new Error('本地存储空间不足，未保存此次更改。'); }
  }
  function post(command) {
    if (!state.alive) return;
    try { window.chrome?.webview?.postMessage(String(command)); } catch (_) { /* 独立浏览器预览 */ }
  }
  function recordError(error) {
    const text = String(error?.message || error || '未知错误').slice(0, 700);
    state.error = text;
    if (state.errors[state.errors.length - 1] !== text) state.errors.push(text);
    if (state.errors.length > 12) state.errors.shift();
  }
  function fail(text, detail) {
    if (!state.alive || state.ready) return;
    recordError(detail || text);
    const status = document.getElementById('pet-status');
    const label = document.getElementById('pet-status-text');
    if (status && label) {
      status.hidden = false;
      status.setAttribute('data-error', '');
      // textContent 即使内容相同也会产生 childList mutation；保持幂等，避免错误界面触发观察器自循环。
      if (label.textContent !== text) label.textContent = text;
      document.getElementById('pet-retry').hidden = false;
    }
    if (!state.unavailableSent) { state.unavailableSent = true; post('model-unavailable'); }
  }
  window.addEventListener('error', (event) => {
    const error = event.error || event.message || event.target?.src || '脚本加载失败';
    recordError(error);
    if (!state.ready) fail('Live2D 加载失败', error);
  }, true);
  window.addEventListener('unhandledrejection', (event) => {
    recordError(event.reason);
    if (!state.ready) fail('Live2D 加载失败', event.reason);
  });

  const catalogPromise = nativeFetch('catalog.json', { cache: 'no-store' }).then((response) => {
    if (!response.ok) throw new Error('catalog.json: HTTP ' + response.status);
    return response.json();
  }).then((catalog) => {
    if (!catalog || catalog.ok !== true || !Array.isArray(catalog.pets) || catalog.pets.length !== 1)
      throw new Error('本地模型清单无效');
    state.catalog = catalog;
    // WebView2 虚拟主机直接提供静态文件，不会触发 WebResourceRequested。
    // 宿主在文档创建时提供可直接读取的运行库地址；HTTP 诊断环境保留原路由。
    const runtime = window.__wxDemoPetRuntime;
    if (runtime && typeof runtime.coreUrl === 'string' && typeof runtime.vendorUrl === 'string') {
      return Object.assign({}, catalog, { coreUrl: runtime.coreUrl, vendorUrl: runtime.vendorUrl });
    }
    return catalog;
  });
  // 先接住拒绝；原客户端取清单时仍会收到同一条真实错误。
  catalogPromise.catch((error) => fail('本地模型资源不完整', error));

  function json(value, status = 200) {
    return new Response(JSON.stringify(value), { status, headers: { 'content-type': 'application/json; charset=utf-8' } });
  }
  function localUrl(input) {
    try { return new URL(typeof input === 'string' || input instanceof URL ? input : input.url, location.href); }
    catch (_) { return null; }
  }
  function voiceUrl(input) {
    const url = localUrl(input);
    if (!url || url.origin !== location.origin || !url.pathname.startsWith(API + '/sound-file/')) return input;
    const segments = url.pathname.slice((API + '/sound-file/').length).split('/').map(decodeURIComponent);
    const pet = state.catalog?.pets.find((entry) => entry.id === segments[0]);
    if (!pet || segments.length !== 2) return input;
    const voice = pet.voices?.find((entry) => entry.id === segments[1]);
    if (voice) return new URL('/model/' + voice.file.split('/').map(encodeURIComponent).join('/'), location.href).href;
    const custom = customSounds.find((entry) => entry.id === segments[1]);
    return custom ? 'data:' + custom.mime + ';base64,' + custom.base64 : input;
  }
  function library(pet) {
    return (pet.soundLibrary || []).concat(customSounds.map(({ id, name }) => ({ id, name, builtin: false })));
  }
  async function bodyJson(input, init) {
    let text = init?.body;
    if (text === undefined && input instanceof Request) text = await input.clone().text();
    if (text === undefined || text === '') return {};
    if (typeof text !== 'string' || text.length > 1500000) throw new Error('请求内容过大。');
    return JSON.parse(text);
  }
  function audioMime(base64) {
    if (typeof base64 !== 'string' || !/^[A-Za-z0-9+/]+={0,2}$/.test(base64) || base64.length > 1398104)
      throw new Error('声音文件无效或超过 1 MB。');
    const bytes = atob(base64);
    if (bytes.length > 1048576) throw new Error('声音文件超过 1 MB。');
    if (bytes.startsWith('RIFF') && bytes.slice(8, 12) === 'WAVE') return 'audio/wav';
    if (bytes.startsWith('OggS')) return 'audio/ogg';
    if (bytes.startsWith('ID3') || (bytes.charCodeAt(0) === 255 && (bytes.charCodeAt(1) & 224) === 224)) return 'audio/mpeg';
    throw new Error('请选择 MP3、WAV 或 OGG 文件。');
  }
  window.fetch = async function (input, init) {
    const url = localUrl(input);
    if (!url || url.origin !== location.origin || !url.pathname.startsWith(API + '/')) return nativeFetch(input, init);
    const path = url.pathname.slice(API.length);
    const method = (init?.method || (input instanceof Request ? input.method : 'GET')).toUpperCase();
    try {
      if (path === '/catalog') return json(await catalogPromise);
      if (path === '/settings') {
        if (method === 'POST') {
          const patch = await bodyJson(input, init);
          const next = Object.assign({}, shared, { rev: (Number(shared.rev) || 0) + 1 });
          for (const key of ['tuning', 'overrides', 'outfit']) if (Object.prototype.hasOwnProperty.call(patch, key)) next[key] = patch[key];
          writeStore('wx-demo.pet.settings.v1', next);
          shared = next;
        } else if (method !== 'GET') return json({ ok: false, error: '不支持此操作。' }, 405);
        return json(Object.assign({ ok: true }, shared));
      }
      if (path === '/layer' || path === '/display') {
        return json({ ok: true, mode: 'inline', owner: 'inline', desktopRunning: false,
          binary: { supported: false, found: false }, download: { state: 'idle' } });
      }
      const catalog = await catalogPromise;
      const pet = catalog.pets[0];
      if (path === '/sound/' + encodeURIComponent(pet.id)) return json({ ok: true, sounds: {} });
      if (path.startsWith('/sound-file/')) {
        const mapped = voiceUrl(url.href);
        return mapped === url.href ? json({ ok: false, error: '声音不存在。' }, 404) : nativeFetch(mapped, init);
      }
      const soundPrefix = '/sounds/' + encodeURIComponent(pet.id);
      if (path === soundPrefix || path.startsWith(soundPrefix + '/')) {
        const id = path === soundPrefix ? null : decodeURIComponent(path.slice(soundPrefix.length + 1));
        if (method === 'GET') return json({ ok: true, sounds: library(pet) });
        if (id !== null && pet.soundLibrary?.some((entry) => entry.id === id))
          return json({ ok: false, error: '随包声音保持原样。' }, 400);
        let next = customSounds.slice();
        if (method === 'DELETE') next = next.filter((entry) => entry.id !== id);
        else if (method === 'POST') {
          const patch = await bodyJson(input, init);
          const old = next.find((entry) => entry.id === id);
          if (id !== null && !old) throw new Error('声音不存在。');
          if (!old && next.length >= 12) throw new Error('本地声音库最多 12 段。');
          const entry = Object.assign({}, old || { id: 'wx-' + Date.now().toString(36) + Math.random().toString(36).slice(2, 8), name: '声音' });
          if (patch.base64 !== undefined) {
            entry.mime = audioMime(patch.base64);
            entry.base64 = patch.base64;
          }
          if (patch.name !== undefined) entry.name = String(patch.name).trim().slice(0, 60) || '声音';
          if (!entry.base64) throw new Error('声音内容为空。');
          next = old ? next.map((item) => item.id === id ? entry : item) : next.concat(entry);
        } else return json({ ok: false, error: '不支持此操作。' }, 405);
        writeStore('wx-demo.pet.sounds.v1', next);
        customSounds = next;
        return json({ ok: true, sounds: library(pet) });
      }
      // 模型资源走本地虚拟主机，不把任意 URL 变成文件访问入口。
      const assetPrefix = '/asset/' + encodeURIComponent(pet.id) + '/';
      if (path.startsWith(assetPrefix) && method === 'GET') {
        const relative = path.slice(assetPrefix.length);
        if (relative.split('/').some((segment) => ['.', '..', ''].includes(decodeURIComponent(segment))))
          return json({ ok: false }, 400);
        return nativeFetch('/model/' + relative, init);
      }
      return json({ ok: false, error: '此插件没有该宿主接口。' }, 404);
    } catch (error) { return json({ ok: false, error: String(error.message || error) }, 400); }
  };
  // 原客户端通过 new Audio 直接播放随包语音；将同一组声音 ID 映射到静态文件。
  window.Audio = function (source) {
    const audio = source === undefined ? new NativeAudio() : new NativeAudio(voiceUrl(source));
    audios.add(audio);
    audio.addEventListener('ended', () => audios.delete(audio), { once: true });
    return audio;
  };
  window.Audio.prototype = NativeAudio.prototype;

  // 保留原客户端的 SSE 生命周期与 local-phase 处理分支，不创建网络长连接。
  class LocalEventSource extends EventTarget {
    constructor(url) {
      super(); this.url = String(url); this.readyState = 1; this.withCredentials = false;
      streams.add(this);
      queueMicrotask(() => {
        if (this.readyState !== 1) return;
        this.dispatchEvent(new Event('open'));
        this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify({ phase: state.phase }) }));
      });
    }
    close() { this.readyState = 2; streams.delete(this); }
  }
  function EventSourceAdapter(url, options) {
    const parsed = localUrl(url);
    if (parsed?.origin === location.origin && parsed.pathname === API + '/events') return new LocalEventSource(url);
    if (!NativeEventSource) throw new Error('EventSource unavailable');
    return new NativeEventSource(url, options);
  }
  Object.assign(EventSourceAdapter, { CONNECTING: 0, OPEN: 1, CLOSED: 2 });
  window.EventSource = EventSourceAdapter;

  function isVisible() { return state.alive && state.visible && !document.hidden; }
  function syncVisibility() {
    const visible = isVisible();
    document.body?.toggleAttribute('data-native-hidden', !visible);
    if (state.applyModelVisibility) state.applyModelVisibility(visible);
    if (state.app?.ticker) visible ? state.app.ticker.start() : state.app.ticker.stop();
    if (!visible) for (const audio of audios) audio.pause();
  }
  function captureModel(model) {
    state.model = model;
    const automator = model.automator;
    if (!automator) return;
    let prototype = automator;
    let descriptor;
    while (prototype && !descriptor) { descriptor = Object.getOwnPropertyDescriptor(prototype, 'autoUpdate'); prototype = Object.getPrototypeOf(prototype); }
    if (descriptor?.get && descriptor?.set) {
      let requested = descriptor.get.call(automator);
      Object.defineProperty(automator, 'autoUpdate', { configurable: true,
        get: () => descriptor.get.call(automator),
        set: (value) => { requested = value === true; descriptor.set.call(automator, requested && isVisible()); },
      });
      state.applyModelVisibility = (visible) => descriptor.set.call(automator, requested && visible);
    } else state.applyModelVisibility = (visible) => { automator.autoUpdate = visible; };
    syncVisibility();
  }
  function samplePaint(app) {
    const api = window.__dshLive2dPet;
    const count = Number(api?.releaseDebug?.().updateCalls || 0);
    if (!count || !state.model || state.painted || Date.now() - state.sampleAt < 150) return;
    state.sampleAt = Date.now();
    try {
      const sample = document.createElement('canvas'); sample.width = 48; sample.height = 48;
      const context = sample.getContext('2d', { willReadFrequently: true });
      context.drawImage(app.canvas, 0, 0, 48, 48);
      const pixels = context.getImageData(0, 0, 48, 48).data;
      let opaque = 0;
      for (let i = 3; i < pixels.length; i += 4) if (pixels[i] > 16) opaque += 1;
      state.opaquePixels = opaque;
      if (opaque > 12) state.painted = true;
    } catch (error) { recordError(error); }
  }
  // 原 vendor 文件不改动。接住其导出，仅记录实例与真实 render/Core.update。
  Object.defineProperty(window, '__dshLive2dPetVendor', {
    configurable: true, get: () => state.vendor ?? undefined,
    set: (vendor) => {
      state.vendor = vendor;
      const BaseApplication = vendor.Application;
      vendor.Application = class WxPetApplication extends BaseApplication {
        async init(options) {
          await super.init(options);
          state.app = this;
          const render = this.renderer.render;
          this.renderer.render = function (...args) {
            const result = render.apply(this, args);
            state.renders += 1;
            if (state.alive) samplePaint(state.app);
            return result;
          };
          syncVisibility();
        }
        destroy(...args) {
          if (state.app === this) { state.app = null; state.loaded = false; }
          return super.destroy(...args);
        }
      };
      const from = vendor.Live2DModel.from;
      vendor.Live2DModel.from = async function (...args) {
        const model = await from.apply(this, args);
        if (state.alive) captureModel(model);
        return model;
      };
    },
  });

  window.__petLocale = 'zh';
  window.__ModuleLoader__ = {
    load(registration) {
      try {
        if (registration.id !== 'dsh-pet-live2d') throw new Error('未知插件。');
        moduleExports = registration.factory((name) => {
          if (name === 'react') return window.React;
          if (name === 'react-dom/client' || name === 'react-dom') return window.ReactDOM;
          throw new Error('未知模块：' + name);
        });
      } catch (error) { fail('桌宠脚本未能启动', error); }
    },
  };
  function setState(value) {
    if (!['idle', 'thinking', 'done', 'error', 'cancelled'].includes(value)) return;
    state.assistantState = value;
    state.phase = value === 'error' ? 'failed' : value === 'cancelled' ? 'idle' : value;
    document.getElementById('pet-dock')?.setAttribute('data-state', value);
    window.dispatchEvent(new CustomEvent('dsh-live2d-pet:local-phase', { detail: { phase: state.phase } }));
    clearTimeout(doneTimer);
    if (value === 'done' || value === 'error') doneTimer = setTimeout(() => setState('idle'), 6500);
  }
  function pointer(x, y) { if (isVisible() && Number.isFinite(x) && Number.isFinite(y)) window.__petPointer?.(x, y); }
  function probe(x, y) {
    if (!isVisible() || !Number.isFinite(x) || !Number.isFinite(y) || x < 0 || y < 0 || x >= innerWidth || y >= innerHeight) return false;
    // 按住拖动期间保持原 PointerCapture 的事件通道，松开后恢复轮廓穿透。
    if (document.querySelector('[data-stage][data-dragging]')) return true;
    const element = document.elementFromPoint(x, y);
    return element !== null && element.closest('[data-panel],[data-bubble],[data-dsh-live2d-pet],[data-wx-pet-ui]') !== null;
  }
  function snapshot() {
    const api = window.__dshLive2dPet;
    const core = api?.coreIdentity?.();
    const raw = core?._model;
    const debug = api?.releaseDebug?.();
    const drawn = {};
    for (const id of ['ParamAngleX', 'ParamAngleY', 'ParamEyeLOpen', 'ParamEyeROpen', 'ParamBodyAngleX']) drawn[id] = api?.drawn?.(id) ?? null;
    return {
      ready: state.ready, loaded: state.loaded, phase: document.querySelector('[data-dsh-live2d-pet]')?.getAttribute('data-phase') || state.phase,
      paused: !isVisible(), frameCount: Number(debug?.updateCalls || 0), renderCount: state.renders,
      parameterCount: Number(raw?.parameters?.count || raw?.parameters?.values?.length || 0),
      drawableCount: Number(raw?.drawables?.count || raw?.drawables?.ids?.length || 0),
      drawn, opaquePixels: state.opaquePixels, errors: state.errors.slice(), debugError: state.error,
      width: innerWidth, height: innerHeight, source: 'original-dsh-live2d-pet',
    };
  }
  window.wxDemoPet = Object.freeze({ probe, pointer, snapshot, setState });
  function onNativeMessage(event) {
    const message = event.data;
    if (!message || typeof message !== 'object') return;
    if (message.type === 'state') setState(message.state);
    else if (message.type === 'visibility' && typeof message.visible === 'boolean') { state.visible = message.visible; syncVisibility(); }
  }
  window.chrome?.webview?.addEventListener('message', onNativeMessage);
  document.addEventListener('visibilitychange', syncVisibility);

  function layoutUi() {
    const root = document.querySelector('[data-dsh-live2d-pet]');
    const dock = document.getElementById('pet-dock');
    if (!root || !dock) return;
    const rect = root.getBoundingClientRect();
    const x = rect.right + 6 + 36 < innerWidth - 8 ? rect.right + 6 : rect.left - 42;
    dock.style.left = Math.max(8, Math.min(innerWidth - 44, x)) + 'px';
    dock.style.top = Math.max(8, Math.min(innerHeight - 100, rect.top + rect.height * 0.52)) + 'px';
    dock.hidden = false;
    const panel = root.querySelector('[data-panel]');
    if (panel && !panel.querySelector('[data-wx-pet-credits]')) {
      const credits = document.createElement('div');
      credits.setAttribute('data-wx-pet-credits', '');
      credits.textContent = 'DSH Live2D · A8Chann · MIT\n模型：上善无形 / ZipZipPipe / 氵六青\nCC BY-NC-SA 4.0 · 模型与动作未修改';
      credits.style.whiteSpace = 'pre-line';
      credits.title = 'https://creativecommons.org/licenses/by-nc-sa/4.0/';
      panel.appendChild(credits);
    }
    const api = window.__dshLive2dPet;
    state.loaded = !!(state.model && api?.coreIdentity?.() && state.app?.canvas);
    if (!state.ready && state.loaded && state.painted && Number(api?.releaseDebug?.().updateCalls || 0) > 0) {
      state.ready = true;
      state.error = '';
      clearTimeout(deadlineTimer);
      document.getElementById('pet-status').hidden = true;
      post('model-ready');
      // React 的 ready effect 在本轮提交后才接 local-phase；下一任务再同步当前 AI 状态。
      setTimeout(() => { if (state.alive) setState(state.assistantState); }, 0);
    }
    const hint = root.querySelector('[data-hint]')?.textContent || '';
    if (!state.ready && /Core|加载失败/.test(hint)) fail(/Core/.test(hint) ? 'Live2D 运行时未能加载' : 'Live2D 模型加载失败', hint);
  }
  async function boot() {
    document.getElementById('pet-ai').addEventListener('click', () => post('open-ai'));
    document.getElementById('pet-hide').addEventListener('click', () => post('hide-pet'));
    const reload = () => {
      if (window.chrome?.webview) post('reload-pet');
      else location.reload();
    };
    document.getElementById('pet-reload').addEventListener('click', reload);
    document.getElementById('pet-retry').addEventListener('click', reload);
    try {
      await catalogPromise;
      if (!state.alive) return;
      if (!window.React || !window.ReactDOM || !moduleExports?.apply) throw new Error('本地 React 或桌宠客户端缺失。');
      moduleExports.apply({
        slots: undefined, logger: console,
        effect(fn) { const dispose = fn(); if (typeof dispose === 'function') disposers.push(dispose); return dispose; },
        get() { return undefined; },
      });
      state.applied = true;
      observer = new MutationObserver(layoutUi);
      observer.observe(document.body, { childList: true, subtree: true });
      watchTimer = setInterval(layoutUi, 100);
      deadlineTimer = setTimeout(() => fail('Live2D 尚未绘制，请重试', '30 秒内没有读到真实模型画面。'), 30000);
      layoutUi();
    } catch (error) { fail('Live2D 未能启动', error); }
  }
  window.addEventListener('pagehide', () => {
    if (!state.alive) return;
    state.alive = false;
    clearInterval(watchTimer); clearTimeout(deadlineTimer); clearTimeout(doneTimer);
    observer?.disconnect();
    window.chrome?.webview?.removeEventListener('message', onNativeMessage);
    document.removeEventListener('visibilitychange', syncVisibility);
    for (const source of streams) source.close();
    for (const audio of audios) { audio.pause(); audio.src = ''; }
    for (const dispose of disposers.reverse()) { try { dispose(); } catch (_) { /* 继续释放其余资源 */ } }
    state.applyModelVisibility = null; state.model = null;
    window.fetch = nativeFetch; window.Audio = NativeAudio; window.EventSource = NativeEventSource;
  }, { once: true });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot, { once: true });
  else void boot();
})();
