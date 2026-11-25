# ZCSR Runtime

Runtime is the **runtime host**: the single OS-window, surface, input-pump and
main-loop owner for ZCSR products. It links the engine and, when the product
needs it, UIM. A runtime module does not create a second window, surface,
renderer or input pump.

This README uses these words with exactly one meaning:

- **runtime host**: the module that owns the one OS window, one surface, one
input pump and one main loop.
- **runtime module**: a unit hosted by the runtime host. The editor is a runtime
  module; it is not a host.
- **window**: the one OS window owned by the runtime host.
- **surface**: the drawable owned by the runtime host.
- **setting page**: one group of setting rows.
- **setting page container**: the independent owner of setting page instances.
  It stores page identity and visibility. It is not a general page stack and it
  is not a UI document owner.

This README is the usage overview for every runtime module. Architecture and
API details belong in a module's header comments (and, if a repo adds one,
`docs/`); do not put plans, audits, or one-off reports in the repo.

## Modules

| Directory | Responsibility | Public entry |
| --- | --- | --- |
| `zcsr_frame_loop/` | Frame iteration, lazy module init/tick/shutdown, service ordering, optional fixed step (at most 8 substeps per call). | `zcsr_frame_loop.h` |
| `zcsr_task_scheduler/` | Generic worker pool. Non-blocking submit, blocking wait, `parallel_for` with inline fallback if scheduling fails. No render/UI/file knowledge. | `zcsr_task_scheduler.h` |
| `zcsr_runtime_shell/` | The only window/main-loop owner. Hosts a module table, keyboard/pointer/close routing, optional settings overlay, shared surface/renderer and task pool. | `zcsr_runtime_shell.h` |
| `player/` | Standalone `zcsr_player` executable. Loads a UIM project, renders pages through UIM, and never compiles editor chrome or ImGui. | `zcsr_player --play <project.uireproj>` |
| `tests/` | Runtime-level frame-loop and shell tests, plus the player autoquit fixture. | CTest |

### Frame loop

`zcsr_frame_loop_create(capacity)` creates the loop; add descriptors with
`zcsr_frame_loop_add_module()`. A `service` module ticks on every frame; a
non-service module ticks only while it is the active module. Use
`zcsr_frame_loop_set_active(loop, -1)` for the shell home screen.
`zcsr_frame_loop_set_fixed_step(loop, seconds)` enables fixed-step accumulation;
`0` passes the frame delta through. `zcsr_frame_loop_tick()` initializes modules
lazily and returns false if an init/tick callback fails; the host should stop.
`zcsr_frame_loop_shutdown()` is idempotent and `zcsr_frame_loop_destroy()` also
shuts down initialized modules.

### Task scheduler

`zcsr_task_scheduler_create(worker_count)`; `worker_count <= 0` selects the
implementation default. `zcsr_task_scheduler_submit()` is non-blocking and
returns false when the queue is full. `zcsr_task_scheduler_wait()` must not be
called from a worker. `zcsr_task_scheduler_parallel_for()` splits `[begin,end)`
into `grain` chunks, blocks, and runs any remaining chunk inline if scheduling
fails. The shell creates one pool and exposes it through
`zcsr_runtime_shell_tasks()`.

### Runtime shell

`zcsr_runtime_shell_run(modules, count, argc, argv)` and
`zcsr_runtime_window_run(...)` are the same host contract: one window, one loop.
`zcsr_window_game_run(...)` is the legacy compatibility name. `argv` is forwarded
so `autoquit` can run headless self-tests. A module supplies optional `init`,
`frame`, `shutdown`, `key`, `pointer`, `text` and `close_request` callbacks. The
shell owns the shared `zcsr_surface` and renderer; read them with
`zcsr_runtime_shell_surface()` only while the shell is running.
`close_request` is consulted before the shell accepts an OS close request; false
keeps the window open. `zcsr_runtime_shell_inject_key()` queues a key into the
shell's single input ring for tests; platform backends must use the normal pump.
`zcsr_runtime_shell_request_exit()` asks the shell to exit after the module has
handled its own unsaved-work confirmation.

`ZCSR_RUNTIME_WITH_SETTINGS_PAGE=ON` adds the independent setting page container
overlay and its read-only test hooks. The default is `OFF`, so the default build
has no setting-page dependency and no SETTINGS entry. When the option is ON, the
top level forces `ZCSR_UIM_WITH_PAGE_MANAGER=ON` so a reused CMake cache cannot
silently omit the legacy `zcsr_ui_page_manager` target. The canonical target
name is `zcsr_page_manager`; the old name is a compatibility name.

## Build

Requirements: CMake 3.16+, C11, and the engine/UIM repositories. The top level
defaults `ZCSR_ENGINE_DIR` and `ZCSR_UIM_DIR` to sibling checkouts; override them
for another layout.

```bat
cmake -S . -B ..\zcsr-build\zcsr_runtime\main -G "Visual Studio 18 2026" -A x64
cmake --build ..\zcsr-build\zcsr_runtime\main --config Release
ctest --test-dir ..\zcsr-build\zcsr_runtime\main -C Release --output-on-failure
```

Settings page build:

```bat
cmake -S . -B ..\zcsr-build\zcsr_runtime\settings -G "Visual Studio 18 2026" -A x64 ^
  -DZCSR_RUNTIME_WITH_SETTINGS_PAGE=ON
cmake --build ..\zcsr-build\zcsr_runtime\settings --config Release
ctest --test-dir ..\zcsr-build\zcsr_runtime\settings -C Release --output-on-failure
```

Options:

- `ZCSR_RUNTIME_BUILD_PLAYER=ON` (default): builds `zcsr_player` and its autoquit
  test. Requires UIM.
- `ZCSR_RUNTIME_WITH_SETTINGS_PAGE=OFF` (default): no setting page container;
  `ON` adds the settings overlay and forces the legacy UIM
  `zcsr_ui_page_manager` target on. Canonical target alias: `zcsr_page_manager`.
- `ZCSR_ENGINE_DIR`, `ZCSR_UIM_DIR`: source directories for the two dependencies.

## Release 1.0 test boundary

- Default configuration: 6/6 CTest passed on a clean tree.
- Settings page ON: 10/10 CTest passed on a clean tree, including settings key
  routing and the player autoquit fixture.
- The tests exercise the runtime host, frame loop, scheduler, settings key
  routing and player entry. They are not a benchmark and do not establish
  rendering performance.

## Known 1.0 boundaries

- The `text` callback is part of the module descriptor. The 1.0 shell drains
  `zcsr_input_ext_take_text()` and dispatches it to the active module; text/IME
  behavior still depends on the engine text backend and must be tested per
  platform before a product relies on it.
- The optional setting page is owned by the independent setting page container.
  Runtime owns the host window and loop, not setting page content.
- Desktop integration covers Windows, Linux and macOS. Linux builds the player
  at `${CMAKE_BINARY_DIR}/zcsr_player` for single-config generators and
  `${CMAKE_BINARY_DIR}/Debug/zcsr_player` for multi-config generators; Run uses
  the same CMake build that produced the editor and launches that executable.
  macOS uses the Cocoa host backend; it still needs real-Mac build/startup
  smoke before it is treated as release-verified.

## License

本项目按 GNU Affero General Public License v3.0（AGPL-3.0）授权，完整许可证文本见仓库根目录 `LICENSE`。版权行：Copyright (C) 2026 Talmaidev。
