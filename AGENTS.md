# Repository Guidelines

## Project Structure & Module Organization

This is an ESP-IDF firmware project. Application code lives in `main/`, organized by feature: `audio/`, `display/`, `protocols/`, `story/`, `camera_agent/`, and `led/`. Hardware integrations belong in `main/boards/<board-name>/`; reusable board support belongs in `main/boards/common/`. Register new sources and include paths in `main/CMakeLists.txt`, and expose configuration through `main/Kconfig.projbuild`.

Documentation and protocol specifications are under `docs/`. Partition layouts are in `partitions/`, release and asset utilities in `scripts/`, and downloaded ESP-IDF dependencies in `managed_components/`. Do not hand-edit generated `build/` output or vendored `managed_components/`. Treat `main_backup_before_hiwonder/` as a reference snapshot, not active source.

## Build, Test, and Development Commands

Use ESP-IDF 5.4 or newer (CI currently uses 5.5.2), with its environment exported.

- `idf.py set-target esp32s3` selects the chip; substitute `esp32`, `esp32c3`, or `esp32p4` as required.
- `idf.py menuconfig` selects the board and feature configuration.
- `idf.py build` compiles the configured firmware.
- `idf.py flash monitor` flashes a connected device and opens serial logs.
- `idf.py fullclean` removes stale configuration/build artifacts when changing targets.
- `python scripts/release.py --list-boards` lists CI-supported board variants.

Consult `main/boards/<board-name>/README.md` before building because flash size and peripherals vary.

## Coding Style & Naming Conventions

C and C++ follow the root `.clang-format`: Google-based style, four-space indentation, attached braces, and a 100-column limit. Format changed files with `clang-format -i path/to/file.cc`; verify with `clang-format --dry-run -Werror path/to/file.cc`. Use `snake_case` filenames, `PascalCase` types, and `snake_case_` private members, matching nearby code. Keep board directory names lowercase and hyphenated.

## Testing Guidelines

There is no repository-level unit-test suite. The required baseline is a clean `idf.py build` for every affected board variant. Changes under shared `main/` or `main/boards/common/` can affect all boards; run representative targets and describe hardware validation. For runtime changes, include relevant serial-monitor output and exercise provisioning, audio, display, or networking paths touched.

## Commit & Pull Request Guidelines

This source snapshot lacks Git history, so no local commit convention can be verified. Use concise, imperative subjects with an optional scope, for example `display: fix LVGL refresh race`. Keep commits focused. Pull requests should explain the problem and solution, list tested chip/board variants, identify configuration changes, and link related issues. Include logs for build/runtime fixes and screenshots or video for visible UI changes.
