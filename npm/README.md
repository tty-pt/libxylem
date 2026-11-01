<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/libxylem/main/assets/logo.png" alt="@ttypt/xylem logo" width="220" />
</p>

# @ttypt/xylem

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-Reactor-654FF0?logo=webassembly)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`libxylem`**, the extensible C module bus and dynamic hook dispatch runtime.

---

## 1. Quickstart

```bash
npm install @ttypt/xylem
```

```typescript
import { XylemBus, areg, call } from "@ttypt/xylem";

// 1. Register a dynamic hook listener
const unregister = areg("on_event", (payload) => {
  console.log("Received event payload:", payload);
  return { status: "processed" };
});

// 2. Dispatch event across registered listeners
const results = call("on_event", { userId: 101, action: "login" });
console.log("Hook return values:", results);

unregister();
```

---

## 2. C Development Headers via NPM

```makefile
CFLAGS += -I./node_modules/@ttypt/xylem/include
LDLIBS += -lxylem
```

---

## 3. Building from Source

```bash
make npm-build    # Compiles native addon and WASM reactor
make npm-test     # Runs verification test suite
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
