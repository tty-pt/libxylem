export interface XylemBridge {
  mount(): void;
  destroy(): void;
  makeImports(): {
    wasm_xy_load_module(namePtr: number, nameLen: number): Promise<number>;
    wasm_xy_unload_module(namePtr: number, nameLen: number): number;
  };
}

export declare function createXylemBridge(root?: any, getWasm?: () => any): XylemBridge;
