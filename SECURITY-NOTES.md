# Security Notes

A record of the source-code safety review performed on this private fork, so the
findings don't have to be re-derived later.

## Audit summary

- **Date reviewed:** 2026-06-25
- **Commit reviewed:** `3e1d466e97864f8bb107bca42b4733f0aed9a5dc` (`main` tip at time of review)
- **Scope:** All tracked source (C++ ACSIL study, `.vscode` build scripts, the
  Streamlit visualizer, bundled `nlohmann/json.hpp`, config, and git contents).
- **Verdict:** **Low risk / clean.** No malicious or suspicious behavior found.

## What was checked

| Check | Result |
| --- | --- |
| Shell / command execution | Only two calls — `ShellExecuteA(NULL, "open", <dir>, ...)` to open the generated-config and results folders in Explorer (`StrategyOptimizerHelpers.cpp:319`, `StrategyOptimizer.cpp:242`). Benign. |
| Network calls (sockets, HTTP, downloaders) | None. No `URLDownload`, `InternetOpen`, `WinHttp`, `socket`, `curl`. |
| Process injection / dynamic loading | None. No `LoadLibrary`, `GetProcAddress`, `VirtualAlloc`, `WriteProcessMemory`. |
| Registry tampering | None. |
| Committed binaries (opaque DLL/EXE) | None tracked. `.gitignore` excludes `*.dll`; nothing slipped in. |
| Obfuscated / minified code | None. Plain, readable C++ and Python. |
| Bundled dependency integrity | `nlohmann/json.hpp` is the genuine v3.12.0 (MIT), unmodified, no injected calls. |
| File writes | All scoped to subfolders relative to the user-specified config path (`/results/`, `/StrategyOptimizerGeneratedConfig/`). No writes to system paths. |
| Python visualizer (`visualizer/app.py`) | Pure pandas / plotly / streamlit; reads local JSON only. No network or shell. |

## Residual risks (ordinary, not specific to this code)

1. **Do not run a precompiled DLL.** The README offers a "Download DLL" path. A
   binary loaded by Sierra Chart runs with full user privileges and cannot be
   verified against this source. **Always compile from source** (`BUILDING.md`).
2. **Native DLL, no sandbox.** Bugs (not malice) could crash Sierra Chart. Test
   against the trade simulator / replay, not a live account, until trusted.
3. **Build scripts assume specific local paths** (Visual Studio, `C:\SierraChart\Data`).
   They invoke `cl.exe` only — adjust paths for your machine.
4. **Standard Python supply-chain hygiene** — install `requirements.txt` into a
   virtualenv. (Note: `visualizer/requirements.txt` is UTF-16 encoded — harmless.)

## Provenance

- License: MIT (`LICENSE`), Copyright (c) 2025 Chek Wei Tan.
- This private fork's `main` matched the upstream tip at the reviewed commit —
  a faithful 1:1 copy with no post-fork modifications.
