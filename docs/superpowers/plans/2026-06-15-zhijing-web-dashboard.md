# Zhijing Web Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a lightweight local Web dashboard to the existing C++ edge gateway.

**Architecture:** Keep the gateway as one executable. It reads serial data, stores the latest sample and recent history in memory, and serves a small HTTP dashboard plus JSON APIs through Winsock.

**Tech Stack:** C++17, Windows API serial port, Winsock HTTP server, HTML/CSS/JavaScript embedded in the executable.

---

## Task 1: Add Data Formatting and Self Test

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`

- [ ] Add timestamp to `SensorData`.
- [ ] Add JSON formatting helpers for latest data and history.
- [ ] Add `--self-test` mode for parser and JSON formatter.

## Task 2: Add In-Memory State

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`

- [ ] Add mutex-protected latest data state.
- [ ] Store the latest 120 sensor samples.
- [ ] Update state whenever a valid `[SENSOR]` line is parsed.

## Task 3: Add Web Server

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`
- Modify: `zhijing_edge_gateway/CMakeLists.txt`

- [ ] Link `ws2_32`.
- [ ] Add a simple blocking Winsock HTTP server running in a background thread.
- [ ] Serve `/`, `/api/latest`, and `/api/history`.

## Task 4: Add Demo Web Mode

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`
- Modify: `zhijing_edge_gateway/README.md`

- [ ] Add `--demo-web 8080` mode.
- [ ] Generate changing fake data every 5 seconds.
- [ ] Document real and demo web commands.

## Verification

- [ ] Build Release with CMake.
- [ ] Run `zhijing_edge_gateway.exe --self-test`.
- [ ] Run `zhijing_edge_gateway.exe --demo-web 8080` and verify `/api/latest` returns JSON.
- [ ] Run `zhijing_edge_gateway.exe COM14 115200 --web 8080` with the board connected.
