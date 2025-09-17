# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Architecture Overview

This is a WebView-only Android Java Activity application that combines:

1. **Chibi Scheme Integration**: Embedded R7RS Scheme interpreter with full standard library support
2. **WebView-Only UI**: Full-screen WebView for complete user interface rendering
3. **JavaScript-to-Native Bridge**: Direct JavaScript-to-JNI communication for Scheme evaluation
4. **JNI Asset Extraction**: Runtime extraction of R7RS library files from APK to device filesystem

### Core Components

- **MainActivity.java**: Primary Android Activity with WebView setup and JNI integration
- **SchemeInterface.java**: Separate class providing JavaScript interface to avoid inner class compilation issues
- **main_jni.c**: JNI implementation with Chibi Scheme integration and comprehensive asset extraction
- **chibi-scheme/**: Full R7RS Scheme implementation with 583+ library files
- **Sources/assets/test.html**: Interactive WebView HTML interface with real-time Scheme evaluation

### JavaScript Bridge Architecture

The application uses a robust JavaScript-to-native communication system:
- **JavaScript Layer**: WebView executes `window.Scheme.eval(expression)` calls
- **JNI Bridge**: `SchemeInterface.eval()` method bridges JavaScript to native code
- **Native Evaluation**: Chibi Scheme interpreter processes expressions and returns results
- **Asset Management**: JNI extracts essential R7RS library files on first launch

### Build System Integration

- **makecapk**: Command-line Android build system using Android NDK and SDK tools
- **Separate Class Compilation**: Avoids d8 compiler inner class "nest mate" issues
- **Multi-NDK Support**: Automatic NDK version detection (supports NDK 27+ and 29+)
- **Asset Packaging**: 583 Scheme library files automatically included in APK

## Common Development Commands

### Build and Deploy
```bash
# Clean build, install, and run
adb logcat --buffer=all --clear
make clean && make run

# Quick rebuild, install, and run
adb logcat --buffer=all --clear
make run

# Build specific targets
make makecapk/lib/arm64-v8a/librepl.so  # ARM64 only
make keystore                           # Generate signing key
```

### Debugging and Monitoring
```bash
# Monitor app logs with timeout (always use timeout)
timeout 10s adb logcat -s repl

# Monitor crashes and errors
timeout 15s adb logcat | grep -E "(FATAL|AndroidRuntime|SIGSEGV|SIGABRT|tombstone|crashed|repl)"

# Check specific errors
adb logcat | grep UnsatisfiedLinkError

# Clear logs before testing
date && adb logcat --buffer=all --clear
```

### Development Workflow
- Always check build completion before installing.
- Always check timestamps before examining adb logcat output to avoid
  mistaking earlier runs for current output.


## Critical Development Guidelines

### Never leave whitespace at the end of a source code line.

### Always use complete sentences in comments and messages.
### Never use run-on sentences in comments or messages.

### Scheme Integration
- **JNI Asset Extraction**: 25+ essential R7RS files extracted from APK using JNI AssetManager
- **Complete R7RS Support**: Full standard library including `scheme/base.sld`, `chibi/` modules, and `srfi/` extensions
- **Fallback Loading**: R7RS → R5RS → R3RS environment loading with comprehensive error handling
- **Real Scheme Evaluation**: Direct `sexp_eval()` calls with proper context and error management

### JavaScript/Native Communication
- **JavaScript to Native**: `window.Scheme.eval(expression)` using `addJavascriptInterface`
- **Separate Class Design**: `SchemeInterface.java` avoids inner class compilation issues
- **JNI Bridge**: Direct method calls to `evaluateScheme()` in main_jni.c
- **Real-time Evaluation**: Synchronous JavaScript calls with immediate Scheme results

## Project Structure Notes

### Configuration Files
- **Makefile**: Build configuration with Android NDK toolchain setup
- **AndroidManifest.xml.template**: Template for app manifest (gets processed during build)
- **Sources/res/**: Android resources (icons, etc.)
- **Sources/assets/**: Files packaged into APK and accessible via AssetManager

### Key Variables
- `APPNAME`: Application name (default: "repl")
- `PACKAGENAME`: Android package identifier (default: "com.speechcode.repl")
- `ANDROIDVERSION`: Target Android API level (default: 30)

### Asset Management
**Comprehensive JNI Asset Extraction**: On first launch, the app extracts 25+ essential Scheme library files from the APK to `/data/data/com.speechcode.repl/lib/` using JNI AssetManager calls. This includes:
- **R7RS Core**: `init-7.scm` (52KB), `meta-7.scm` (17KB), complete `scheme/*` modules
- **Chibi Extensions**: `chibi/equiv.sld`, `chibi/string.sld`, `chibi/io.sld`, `chibi/ast.sld`
- **SRFI Libraries**: `srfi/9.sld`, `srfi/11.sld`, `srfi/27.sld`,
  `srfi/39.sld` for record types, parameter objects, and random numbers
- **Full APK Integration**: All 583 Scheme files packaged and available for extraction

## Important Debugging Notes

- Always check that the build has completed successfully before trying to install or run the app
- Always check the time before examining the output of adb logcat -
  otherwise, you may mistake the output of an earlier run for the most
  recent output
- Whenever you run adb logcat, set a timeout so that you don't wait
  infinitely for output that never arrives
- WebView threading violations appear as "A WebView method was called
  on thread 'Thread-X'.  All WebView methods must be called on the
  same thread"
- Early app suspension (blank screen then exit) usually indicates
  blocking waits on the main thread