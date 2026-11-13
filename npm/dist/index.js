import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "xylem");
let wasmInstance = null;

export function getXylemWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./xylem.wasm", import.meta.url));
  return wasmInstance;
}

export class XylemBus {
  constructor() {
    this._hooks = new Map();
  }

  register(name, listener, options = {}) {
    if (!options.wasm && nativeBinding && nativeBinding.registerHook) {
      nativeBinding.registerHook(name);
    } else {
      const wasm = getXylemWasm();
      if (wasm && wasm.exports.xy_areg) {
        const substrate = getSubstrate();
        substrate.withString(name, (ptr) => {
          const adapterPtr = substrate.calloc(1, 32);
          try {
            wasm.exports.xy_areg(ptr, adapterPtr);
          } finally {
            substrate.free(adapterPtr);
          }
        });
      }
    }
    if (!this._hooks.has(name)) {
      this._hooks.set(name, []);
    }
    this._hooks.get(name).push(listener);
    return () => this.unregister(name, listener);
  }

  call(name, ...args) {
    if (nativeBinding && nativeBinding.callHook) {
      nativeBinding.callHook(name, "");
    }
    const listeners = this._hooks.get(name);
    if (!listeners || listeners.length === 0) return [];
    return listeners.map((fn) => fn(...args));
  }

  has(name) {
    return this._hooks.has(name) && this._hooks.get(name).length > 0;
  }

  unregister(name, listener) {
    const list = this._hooks.get(name);
    if (!list) return false;
    const idx = list.indexOf(listener);
    if (idx !== -1) {
      list.splice(idx, 1);
      return true;
    }
    return false;
  }

  list() {
    return Array.from(this._hooks.keys());
  }
}

export const defaultBus = new XylemBus();

export function areg(name, listener) {
  return defaultBus.register(name, listener);
}

export function call(name, ...args) {
  return defaultBus.call(name, ...args);
}

export function version() {
  if (nativeBinding && nativeBinding.version) {
    return nativeBinding.version();
  }
  return "1.7.1";
}

export { createXylemBridge } from "./bridge.js";

