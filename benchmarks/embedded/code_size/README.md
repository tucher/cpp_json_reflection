# Embedded Binary Size Benchmark

Measures JsonFusion's code footprint for embedded ARM Cortex-M7 targets.

## Overview

This benchmark compiles a realistic embedded configuration parser and measures the resulting binary size. The goal is to understand JsonFusion's flash memory requirements for resource-constrained embedded systems.

## Model

The test uses `EmbeddedConfig` - a realistic embedded configuration with:
- **Fixed-size arrays** (static allocation, no heap)
- **Validation constraints** (`range<>`, `min_items<>`)
- **Nested structures** (Network, Controller, Motor, Sensor, Logging)
- **Optional fields** (`std::optional`)
- **~2.5 KB data size** (16 motors, 16 sensors, various strings)

This represents a typical embedded device configuration (motor controllers, IoT devices, industrial systems).

## Target Platform

**ARM Cortex-M7** (common in STM32H7, SAMV7, etc.):
- 32-bit ARM architecture
- Thumb-2 instruction set
- Hardware floating-point unit (FPv5-D16)
- Representative of modern embedded systems

## Quick Start

**Recommended: Use the Python build script** (easier to maintain and extend):

```bash
./build.py
```



This builds **2 configurations**:
1. **size_optimized** (`-Os -flto`) - Minimize code size
3. **aggressive_opt** (`-O3 -flto`) - Maximum speed

## Results

Each build outputs:
- **Code size** (`.text` section) - Flash memory usage
- **Const data** (`.rodata` section) - Validation tables, literals
- **Initialized data** (`.data` section) - Should be minimal
- **Uninitialized data** (`.bss` section) - Global config (~2.5 KB)
- **Top symbols** - Largest functions by size



## Comparisons

To compare with alternatives:


### Hand-Written Parser
Create minimal hand-written parser (no validation, error-prone).

### Other Embedded Libraries
- ArduinoJson
- jsmn (minimal tokenizer)
- cJSON
- yajl (event-driven / SAX parser)
