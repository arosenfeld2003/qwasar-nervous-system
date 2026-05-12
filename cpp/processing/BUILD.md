# Processing Engine Build Script for Windows

## Prerequisites
# Install CMake: https://cmake.org/download/
# Install a C++ compiler (MSVC, MinGW, or Clang)

## Building

### Using CMake with Visual Studio
```powershell
cd c:\Users\timot\Projects\Qwasar\nervous-system\qwasar-nervous-system

# Configure
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build cpp/build --config Debug --parallel
```

### Using CMake with MinGW
```powershell
cd c:\Users\timot\Projects\Qwasar\nervous-system\qwasar-nervous-system

# Configure
cmake -S cpp -B cpp/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build cpp/build --parallel
```

### Using CMake with Unix Makefiles (Requires make.exe)
```powershell
cd c:\Users\timot\Projects\Qwasar\nervous-system\qwasar-nervous-system

# Configure
cmake -S cpp -B cpp/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build cpp/build --parallel
```

## Running Tests

After building, run the tests:

```powershell
# From project root
.\cpp\build\Debug\test_processing_engine.exe

# Or
.\cpp\build\test_processing_engine.exe
```

## Quick Verification

To verify the code compiles with your installed compiler:

```powershell
# List available generators
cmake --help

# Choose one that matches your system and run configure
cmake -S cpp -B cpp/build -G "<Your Generator>"
```

## Recommended Setup

For Windows:
1. Install Visual Studio 2022 Community (includes MSVC compiler and CMake)
2. Or install MinGW-w64 for GCC
3. Install CMake if not already included

Then run:
```powershell
cmake -S cpp -B cpp/build
cmake --build cpp/build
```

## After Build

Test results should show all 11 tests passing:
- ✓ Load rules without error
- ✓ Matching event triggers callback
- ✓ Alarm published to RabbitMQ
- ✓ Event matching condition triggers callback
- ✓ Multiple rules triggering multiple alarms
- ... (more tests)

Expected: **11 passed, 0 failed**
