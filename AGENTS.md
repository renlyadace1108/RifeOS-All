# Autonomous Agent Instructions for RifeOS

This repository contains **RifeOS**, a lightweight, subpixel fluid desktop workspace host written in pure C11 for Windows.

All AI coding assistants (Antigravity, Claude Code, Cursor, Copilot) must consult:
- **`AI_CONTINUITY.md`**: Master handover guide with architectural rules, toolchain paths, build & packaging commands, and design tokens.
- **`GEMINI.md`**: Antigravity contextual rule definitions.

## Key Rules:
1. Pure C11/C99 only; static CRT `/MT`; strictly zero frame allocations (`malloc/free`); memory set < 5MB.
2. Must compile with 0 errors and 0 warnings under MSVC `/W4 /utf-8`.
3. Subsystem MUST be `WIN32` with `WinMain` entry (NO console black window).
4. Author: **Renly** (Email: `renly20061108@gmail.com`, Douyin: `陈连山`). NEVER put real Chinese names in UI or code.
5. Always test compilation before finishing and push to GitHub `origin main`.
