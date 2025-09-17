# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Architecture Overview

This is a WebView-only Android Java Activity application that combines:

1. **Chibi Scheme Integration**: Embedded R7RS Scheme interpreter with full standard library support and dynamic loading
2. **WebView-Only UI**: Full-screen WebView for complete user interface rendering
3. **JavaScript-to-Native Bridge**: Thread-safe JavaScript-to-JNI communication for Scheme evaluation
4. **JNI Asset Extraction**: Runtime extraction of R7RS library files from APK to device filesystem
5. **Internet REPL Service**: Real-time server communication for remote Scheme expression evaluation

### Core Components

- **MainActivity.java**: Primary Android Activity with WebView setup, JNI integration, and WebView console logging
- **SchemeInterface.java**: Separate class providing JavaScript interface to avoid inner class compilation issues
- **InternetReplService.java**: Background service for server communication and result display with thread-safe operations
- **DebugWebChromeClient.java**: Separate WebView console logging implementation for JavaScript debugging
- **main_jni.c**: JNI implementation with Chibi Scheme integration, comprehensive asset extraction, and pthread mutex synchronization
- **chibi-scheme/**: Full R7RS Scheme implementation with 583+ library files and dynamic shared library support
- **Sources/assets/test.html**: Interactive WebView HTML interface with real-time local and remote Scheme evaluation

### JavaScript Bridge Architecture

The application uses a robust JavaScript-to-native communication system:
- **JavaScript Layer**: WebView executes `window.Scheme.eval(expression)` calls with consistent double-quote syntax
- **JNI Bridge**: `SchemeInterface.eval()` method bridges JavaScript to native code with thread-safe evaluation
- **Native Evaluation**: Chibi Scheme interpreter processes expressions with pthread mutex synchronization
- **Asset Management**: JNI extracts essential R7RS library files including SRFI 27 shared libraries on first launch
- **Result Display**: `displayResult()` function handles both local and remote results with proper styling and IIFE scope isolation

### Build System Integration

- **makecapk**: Command-line Android build system using Android NDK and SDK tools
- **Separate Class Compilation**: Avoids d8 compiler inner class "nest
  mate" issues with DebugWebChromeClient as separate file
- **Multi-NDK Support**: Automatic NDK version detection (supports NDK 27+ and 29+)
- **Dynamic Library Support**: Builds SRFI 27 and other shared libraries for Android ARM64 with proper linking
- **Asset Packaging**: 583 Scheme library files automatically included in APK with .so files properly placed

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
- **Dynamic Library Loading**: SRFI 27 and other extensions loaded as shared libraries using `include-shared` directive
- **JNI Asset Extraction**: 38+ essential R7RS files extracted from APK using JNI AssetManager including .so files
- **Complete R7RS Support**: Full standard library including `scheme/base.sld`, `chibi/` modules, and `srfi/` extensions
- **Fallback Loading**: R7RS → R5RS → R3RS environment loading with comprehensive error handling
- **Thread-Safe Evaluation**: Direct `sexp_eval()` calls with pthread mutex protection and proper context management
- **SRFI 27 Support**: Random number generation with `(random-integer n)` fully functional through dynamic loading

### JavaScript/Native Communication
- **JavaScript to Native**: `window.Scheme.eval(expression)` using `addJavascriptInterface` with consistent double-quote syntax
- **Separate Class Design**: `SchemeInterface.java` and `DebugWebChromeClient.java` avoid inner class compilation issues
- **JNI Bridge**: Direct method calls to `evaluateScheme()` in main_jni.c with thread safety
- **Real-time Evaluation**: Synchronous JavaScript calls with immediate Scheme results
- **Internet REPL**: Background service handles server communication with proper result display using `displayResult()`
- **IIFE Scope Isolation**: JavaScript variable declarations wrapped in immediately invoked function expressions to prevent conflicts

## Project Structure Notes

### Configuration Files
- **Makefile**: Build configuration with Android NDK toolchain setup
- **AndroidManifest.xml.template**: Template for app manifest (gets processed during build)
- **Sources/res/**: Android resources (icons, etc.)
- **Sources/assets/**: Files packaged into APK and accessible via AssetManager

### Key Variables
- `APPNAME`: Application name (default: "repl")
- `PACKAGENAME`: Android package identifier (default: "com.speechcode.repl")
- `ANDROIDVERSION`: Target Android API level (default: 33)

### Asset Management
**Comprehensive JNI Asset Extraction**: On first launch, the app extracts 38+ essential Scheme library files from the APK to `/data/data/com.speechcode.repl/lib/` using JNI AssetManager calls. This includes:
- **R7RS Core**: `init-7.scm` (52KB), `meta-7.scm` (17KB), complete `scheme/*` modules
- **Chibi Extensions**: `chibi/equiv.sld`, `chibi/string.sld`, `chibi/io.sld`, `chibi/ast.sld`
- **SRFI Libraries**: `srfi/9.sld`, `srfi/11.sld`, `srfi/27.sld`, `srfi/39.sld` for record types, parameter objects, and random numbers
- **Dynamic Libraries**: `srfi/27/rand.so` (28KB) shared library for random number generation with proper executable permissions
- **Full APK Integration**: All 583 Scheme files packaged and available for extraction with automatic directory structure creation

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

## Thread Safety and JavaScript Best Practices

- **Mutex Protection**: All Scheme evaluation is protected by pthread mutex to prevent race conditions from multiple threads
- **IIFE Pattern**: JavaScript variable declarations should use immediately invoked function expressions `(function() { ... })()` to prevent variable naming conflicts
- **Consistent Quotes**: Always use double quotes in JavaScript string literals for consistency
- **WebView Console Logging**: Use separate `DebugWebChromeClient.java` file to avoid D8 compiler nest mate issues
- **Result Display**: Use unified `displayResult()` function for both local and remote results with proper type indicators