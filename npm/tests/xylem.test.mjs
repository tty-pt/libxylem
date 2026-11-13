import test from "node:test";
import assert from "node:assert/strict";
import { XylemBus, areg, call, version, createXylemBridge } from "../dist/index.js";
import { createXylemBridge as createXylemBridgeDirect } from "../dist/bridge.js";

test("XylemBus hook registration and dispatch", () => {
  const bus = new XylemBus();
  let received = null;

  const unreg = bus.register("on_test_event", (data) => {
    received = data;
    return 42;
  });

  assert.equal(bus.has("on_test_event"), true);
  const returns = bus.call("on_test_event", { message: "hello" });

  assert.deepEqual(received, { message: "hello" });
  assert.deepEqual(returns, [42]);

  unreg();
  assert.equal(bus.has("on_test_event"), false);
});

test("defaultBus areg and call helper functions", () => {
  let counter = 0;
  const unreg = areg("increment", (val) => {
    counter += val;
  });

  call("increment", 5);
  call("increment", 10);
  assert.equal(counter, 15);

  unreg();
  assert.ok(version().length > 0);
});

test("createXylemBridge creates capability bridge and imports", async () => {
  assert.equal(typeof createXylemBridge, "function");
  assert.equal(typeof createXylemBridgeDirect, "function");

  const mockExports = {
    memory: new WebAssembly.Memory({ initial: 1 }),
    table: null
  };
  const bridge = createXylemBridge(null, () => mockExports);
  assert.ok(bridge);
  assert.equal(typeof bridge.mount, "function");
  assert.equal(typeof bridge.destroy, "function");
  assert.equal(typeof bridge.makeImports, "function");

  const imports = bridge.makeImports();
  assert.equal(typeof imports.wasm_xy_load_module, "function");
  assert.equal(typeof imports.wasm_xy_unload_module, "function");

  // Unload when not loaded should return 0 safely
  const res = imports.wasm_xy_unload_module(0, 0);
  assert.equal(res, 0);

  bridge.destroy();
});

