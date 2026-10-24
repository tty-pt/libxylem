/*
 * xylem-bridge.js — Browser capability bridge for libxylem dynamic modules.
 * Implements the "xylem" WASM import namespace with dynamic WebAssembly.instantiate.
 */

function readStr(memory, ptr, len) {
	if (!memory || !ptr || len <= 0) return '';
	return new TextDecoder().decode(new Uint8Array(memory.buffer, ptr, len));
}

export function createXylemBridge(root, getWasm) {
	const loadedModules = new Map();

	return {
		mount() {},
		destroy() {
			loadedModules.clear();
		},
		makeImports() {
			return {
				async wasm_xy_load_module(namePtr, nameLen) {
					const exports = getWasm();
					if (!exports || !exports.memory) return -1;
					const name = readStr(exports.memory, namePtr, nameLen);
					if (loadedModules.has(name)) return 0;

					try {
						const resp = await fetch(`/${name}.wasm`);
						if (!resp.ok) return -1;
						const bytes = await resp.arrayBuffer();
						const childModule = await WebAssembly.compile(bytes);
						const imports = {
							env: {
								memory: exports.memory,
								table: exports.__indirect_function_table || exports.table
							},
							xylem: {
								wasm_xy_load_module: () => 0,
								wasm_xy_unload_module: () => 0
							}
						};
						const childInstance = await WebAssembly.instantiate(childModule, imports);
						loadedModules.set(name, childInstance);
						if (childInstance.exports.xy_install) {
							childInstance.exports.xy_install();
						}
						return 0;
					} catch (e) {
						console.error(`Failed to load WASM module ${name}:`, e);
						return -1;
					}
				},
				wasm_xy_unload_module(namePtr, nameLen) {
					const exports = getWasm();
					if (!exports || !exports.memory) return -1;
					const name = readStr(exports.memory, namePtr, nameLen);
					const mod = loadedModules.get(name);
					if (mod && mod.exports.xy_uninstall) {
						try { mod.exports.xy_uninstall(); } catch (_) {}
					}
					loadedModules.delete(name);
					return 0;
				}
			};
		}
	};
}

if (typeof window !== 'undefined' && window.registerWasmCapability) {
	window.registerWasmCapability('xylem', createXylemBridge);
}
