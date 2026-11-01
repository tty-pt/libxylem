import { createRequire } from "node:module";
import path from "node:path";

let nativeBinding = null;
try {
  const req = createRequire(import.meta.url);
  try {
    nativeBinding = req("../build/Release/xylem_node.node");
  } catch {}
  if (!nativeBinding) {
    try {
      nativeBinding = req(path.join(process.cwd(), "build/Release/xylem_node.node"));
    } catch {}
  }
} catch {
  // In-memory fallback
}

export class XylemBus {
  constructor() {
    this._hooks = new Map();
  }

  register(name, listener) {
    if (nativeBinding && nativeBinding.registerHook) {
      nativeBinding.registerHook(name);
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
  return "0.1.0";
}
