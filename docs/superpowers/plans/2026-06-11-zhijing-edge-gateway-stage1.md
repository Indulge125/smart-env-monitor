# Zhijing Edge Gateway Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal C++ edge gateway that reads STM32 serial text, extracts `[SENSOR]` JSON, and prints parsed real sensor values.

**Architecture:** Keep the first version intentionally small: one C++ console program, Windows serial reading through WinAPI, and a tiny parser for the known JSON shape. The program ignores non-sensor lines so existing `[DTU]` debug output does not disturb parsing.

**Tech Stack:** C++17, CMake, Windows API serial port, standard library only.

---

## File Structure

- Create `zhijing_edge_gateway/CMakeLists.txt`: CMake project definition.
- Create `zhijing_edge_gateway/src/main.cpp`: serial open/read loop and parser.
- Create `zhijing_edge_gateway/README.md`: build and run instructions.

## Task 1: Project Skeleton

**Files:**
- Create: `zhijing_edge_gateway/CMakeLists.txt`
- Create: `zhijing_edge_gateway/src/main.cpp`
- Create: `zhijing_edge_gateway/README.md`

- [ ] Create a CMake console project named `zhijing_edge_gateway`.
- [ ] Add a placeholder `main.cpp` that prints startup help.
- [ ] Add README instructions for default command `zhijing_edge_gateway.exe COM14 115200`.

## Task 2: Sensor Parser

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`

- [ ] Add `SensorData` with `light`, `temp`, `mode`, and `servo`.
- [ ] Add a helper that extracts the substring after `[SENSOR]`.
- [ ] Add a minimal numeric field parser for the known JSON format.
- [ ] Ignore lines that do not contain `[SENSOR]`.

## Task 3: Windows Serial Reader

**Files:**
- Modify: `zhijing_edge_gateway/src/main.cpp`

- [ ] Open `COMx` as `\\\\.\\COMx`.
- [ ] Configure baud rate, 8 data bits, no parity, 1 stop bit.
- [ ] Read bytes continuously and assemble text lines.
- [ ] On each complete line, run the sensor parser.

## Task 4: Local Verification

**Files:**
- Modify: `zhijing_edge_gateway/README.md`

- [ ] Build with CMake if a compiler is available.
- [ ] If no local C++ compiler is available, document the exact build command for Visual Studio Developer PowerShell.
- [ ] Add an offline parser test mode using `--demo` so the parser can be verified without hardware.

## Success Criteria

- `zhijing_edge_gateway.exe --demo` prints parsed values from a sample `[SENSOR]` line.
- `zhijing_edge_gateway.exe COM14 115200` can read the real board serial stream when COM14 is free.
- Bad lines and non-sensor lines do not crash the program.
