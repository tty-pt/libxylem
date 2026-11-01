import test from "node:test";
import assert from "node:assert/strict";
import { XylemBus, areg, call, version } from "../dist/index.js";

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
