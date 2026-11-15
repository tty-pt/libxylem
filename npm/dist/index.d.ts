export type XylemListener = (...args: any[]) => any;

export declare class XylemBus {
  constructor();
  register(name: string, listener: XylemListener): () => boolean;
  call(name: string, ...args: any[]): any[];
  has(name: string): boolean;
  unregister(name: string, listener: XylemListener): boolean;
  list(): string[];
}

export declare const defaultBus: XylemBus;
export declare function areg(name: string, listener: XylemListener): () => boolean;
export declare function call(name: string, ...args: any[]): any[];
export declare function version(): string;

export { createXylemBridge, XylemBridge } from "./bridge.js";

