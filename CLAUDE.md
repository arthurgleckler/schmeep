# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.

## Architecture Overview

This is a WebView-only Android Java Activity application that
combines:

1. **Chibi Scheme Integration**: Embedded R7RS Scheme interpreter with
   full standard library support and dynamic loading
2. **WebView-Only UI**: Full-screen WebView for complete user
   interface rendering
3. **JavaScript-to-Native Bridge**: Thread-safe JavaScript-to-JNI
   communication for Scheme evaluation
4. **JNI Asset Extraction**: Runtime extraction of R7RS library files
   from APK to device filesystem
5. **Bluetooth SPP REPL Service**: Two-way Bluetooth Serial Port Profile
   communication for remote Scheme expression evaluation

### Core Components

- **MainActivity.java**: Primary Android Activity with WebView setup,
  JNI integration, and WebView console logging
- **SchemeInterface.java**: Separate class providing JavaScript
  interface to avoid inner class compilation issues
- **BluetoothReplService.java**: Background Bluetooth SPP server for
  client communication and result display with thread-safe operations
- **DebugWebChromeClient.java**: Separate WebView console logging
  implementation for JavaScript debugging
- **main_jni.c**: JNI implementation with Chibi Scheme integration,
  comprehensive asset extraction, and pthread mutex synchronization
- **chibi-scheme/**: Full R7RS Scheme implementation with 583+ library
  files and dynamic shared library support
- **Sources/assets/test.html**: Interactive WebView HTML interface
  with real-time local and remote Scheme evaluation

### JavaScript Bridge Architecture

The application uses a robust JavaScript-to-native communication
system:
- **JavaScript Layer**: WebView executes
  `window.Scheme.eval(expression)` calls with consistent double-quote
  syntax
- **JNI Bridge**: `SchemeInterface.eval()` method bridges JavaScript
  to native code with thread-safe evaluation
- **Native Evaluation**: Chibi Scheme interpreter processes
  expressions with pthread mutex synchronization
- **Asset Management**: JNI extracts essential R7RS library files
  including SRFI 27 shared libraries on first launch
- **Result Display**: `displayResult()` function handles both local
  and remote results with proper styling and IIFE scope isolation

### Bluetooth SPP Communication System

The application implements a complete two-way Bluetooth Serial Port Profile
(SPP) communication system for remote Scheme expression evaluation:

**Android Server Components:**
- **BluetoothReplService.java**: SPP server implementing custom UUID service
  registration with both secure and insecure connection modes for maximum
  compatibility
- **Custom UUID**: `611a1a1a-94ba-11f0-b0a8-5f754c08f133` for reliable
  service discovery
- **Length-Prefixed Protocol**: 4-byte big-endian length prefix + UTF-8
  message content for multi-line expression support
- **Service Discovery**: Registers as "CHB" service in SDP for client
  discovery
- **Bluetooth Permissions**: Complete Android 12+ permission handling with
  runtime permission requests

**Linux Client:**
- **chb.c**: C client with UUID-based service discovery
- **Automatic Port Discovery**: Uses SDP queries with custom UUID to find
  correct RFCOMM channel automatically
- **Interactive REPL**: Full command-line interface for real-time Scheme
  evaluation
- **Robust Protocol**: Implements same length-prefixed binary protocol as
  Android server

**Protocol Specification:**
```
Message Format: [4-byte length][UTF-8 content]
Length Encoding: Big-endian 32-bit unsigned integer
Content Encoding: UTF-8 text (supports multi-line expressions)
Connection Type: RFCOMM (Bluetooth SPP)
Service UUID: 611a1a1a-94ba-11f0-b0a8-5f754c08f133
```

**Usage:**
```bash
# Compile client
make chb

# Interactive mode
./chb AA:BB:CC:DD:EE:FF

# Auto-discovery mode
./chb

# Pipe expressions
echo "(+ 2 3)" | ./chb AA:BB:CC:DD:EE:FF
```

### Build System Integration

- **makecapk**: Command-line Android build system using Android NDK
  and SDK tools
- **Separate Class Compilation**: Avoids d8 compiler inner class "nest
  mate" issues with DebugWebChromeClient as separate file
- **Multi-NDK Support**: Automatic NDK version detection (supports NDK
  27+ and 29+)
- **Dynamic Library Support**: Builds SRFI 27 and other shared
  libraries for Android ARM64 with proper linking
- **Asset Packaging**: 583 Scheme library files automatically included
  in APK with .so files properly placed

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

### Bluetooth Testing and Debugging
```bash
# Test Bluetooth client (replace with actual device address)
./chb AA:BB:CC:DD:EE:FF

# Auto-discovery mode (scans paired/connected devices)
./chb

# Check Bluetooth service discovery
python3 -c "
import bluetooth
services = bluetooth.find_service(address='AA:BB:CC:DD:EE:FF')
for s in services:
    if s.get('name') == 'CHB':
        print('CHB found on port:', s.get('port'))
"

# Monitor Bluetooth connections during testing
timeout 10s adb logcat -s repl | grep -E "(Client connected|Waiting for|Bluetooth)"

# Test expressions via Bluetooth
echo "(+ 2 3)" | ./chb AA:BB:CC:DD:EE:FF
echo -e "(define x 42)\nx\n(* x 2)" | ./chb AA:BB:CC:DD:EE:FF
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
- **Dynamic Library Loading**: SRFI 27 and other extensions loaded as
  shared libraries using `include-shared` directive
- **JNI Asset Extraction**: 38+ essential R7RS files extracted from
  APK using JNI AssetManager including .so files
- **Complete R7RS Support**: Full standard library including
  `scheme/base.sld`, `chibi/` modules, and `srfi/` extensions
- **Fallback Loading**: R7RS → R5RS → R3RS environment loading with
  comprehensive error handling
- **Thread-Safe Evaluation**: Direct `sexp_eval()` calls with pthread
  mutex protection and proper context management
- **SRFI 27 Support**: Random number generation with `(random-integer
  n)` fully functional through dynamic loading

### JavaScript/Native Communication
- **JavaScript to Native**: `window.Scheme.eval(expression)` using
  `addJavascriptInterface` with consistent double-quote syntax
- **Separate Class Design**: `SchemeInterface.java` and
  `DebugWebChromeClient.java` avoid inner class compilation issues
- **JNI Bridge**: Direct method calls to `evaluateScheme()` in
  main_jni.c with thread safety
- **Real-time Evaluation**: Synchronous JavaScript calls with
  immediate Scheme results
- **Bluetooth REPL**: Background SPP service handles client communication
  with proper result display using `displayResult()`
- **IIFE Scope Isolation**: JavaScript variable declarations wrapped
  in immediately invoked function expressions to prevent conflicts

## Project Structure Notes

### Configuration Files
- **Makefile**: Build configuration with Android NDK toolchain setup
- **AndroidManifest.xml.template**: Template for app manifest (gets
  processed during build)
- **Sources/res/**: Android resources (icons, etc.)
- **Sources/assets/**: Files packaged into APK and accessible via
  AssetManager

### Key Variables
- `APPNAME`: Application name (default: "repl")
- `PACKAGENAME`: Android package identifier (default:
  "com.speechcode.repl")
- `ANDROIDVERSION`: Target Android API level (default: 33)

### Bluetooth Configuration
- **Service UUID**: `611a1a1a-94ba-11f0-b0a8-5f754c08f133` (hardcoded in both
  Android server and C client)
- **Service Name**: "CHB" (registered in SDP for discovery)
- **Required Permissions**: BLUETOOTH, BLUETOOTH_ADMIN, BLUETOOTH_CONNECT,
  BLUETOOTH_ADVERTISE (automatically added via AndroidManifest.xml.template)
- **Protocol**: Length-prefixed binary (4-byte big-endian length + UTF-8 content)
- **Client Dependencies**: libbluetooth-dev package on Linux

### Asset Management
**Comprehensive JNI Asset Extraction**: On first launch, the app
extracts 38+ essential Scheme library files from the APK to
`/data/data/com.speechcode.repl/lib/` using JNI AssetManager
calls. This includes:
- **R7RS Core**: `init-7.scm` (52KB), `meta-7.scm` (17KB), complete
  `scheme/*` modules
- **Chibi Extensions**: `chibi/equiv.sld`, `chibi/string.sld`,
  `chibi/io.sld`, `chibi/ast.sld`
- **SRFI Libraries**: `srfi/9.sld`, `srfi/11.sld`, `srfi/27.sld`,
  `srfi/39.sld` for record types, parameter objects, and random
  numbers
- **Dynamic Libraries**: `srfi/27/rand.so` (28KB) shared library for
  random number generation with proper executable permissions
- **Full APK Integration**: All 583 Scheme files packaged and
  available for extraction with automatic directory structure creation

## Important Debugging Notes

- Always check that the build has completed successfully before trying
  to install or run the app
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

- **Mutex Protection**: All Scheme evaluation is protected by pthread
  mutex to prevent race conditions from multiple threads
- **IIFE Pattern**: JavaScript variable declarations should use
  immediately invoked function expressions `(function() { ... })()` to
  prevent variable naming conflicts
- **Consistent Quotes**: Always use double quotes in JavaScript string
  literals for consistency
- **WebView Console Logging**: Use separate
  `DebugWebChromeClient.java` file to avoid D8 compiler nest mate
  issues
- **Result Display**: Use unified `displayResult()` function for both
  local and remote results with proper type indicators

## Client Threading Architecture (chb.c)

### Thread Structure Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        MAIN PROCESS                         │
│                                                             │
│  ┌─────────────────┐              ┌─────────────────────┐   │
│  │   INPUT THREAD  │              │   MAIN THREAD       │   │
│  │                 │              │   (Response)        │   │
│  │ • Read stdin    │◄────sync────►│ • Receive messages  │   │
│  │ • Send messages │              │ • Handle signals    │   │
│  │ • Handle EINTR  │              │ • Coordinate state  │   │
│  └─────────────────┘              └─────────────────────┘   │
│           │                                   │             │
│           │                                   │             │
│  ┌────────────────────────────────────────────────────────┐ │
│  │              SIGNAL HANDLER                            │ │
│  │         (sigint_handler - async)                       │ │
│  │      • Writes to signal_pipe[1]                        │ │
│  │      • Sets interrupt_requested = 1                    │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### State Variables & Synchronization

**Global State Variables:**
```c
static volatile sig_atomic_t interrupt_requested = 0;
static volatile sig_atomic_t input_complete = 0;
static volatile sig_atomic_t message_sent = 0;
static volatile sig_atomic_t response_received = 0;
```

**Synchronization Primitives:**
```c
static pthread_mutex_t message_mutex;
static pthread_cond_t message_cond;
static pthread_cond_t response_cond;
static int signal_pipe[2];  // Self-pipe trick for signal handling
```

### Thread State Machines

#### INPUT THREAD States:

```
┌─────────────┐
│   READING   │ ◄────┐
│   INPUT     │      │
└─────────────┘      │
      │ getline()    │
      │              │
      ▼              │
┌─────────────┐      │
│  PROCESSING │      │
│   INPUT     │      │
└─────────────┘      │
      │              │
      ▼              │
┌─────────────┐      │
│  SENDING    │      │
│  MESSAGE    │      │
└─────────────┘      │
      │              │
      ▼              │
┌─────────────┐      │
│   SIGNAL    │      │
│  MAIN THREAD│      │
└─────────────┘      │
      │              │
      └──────────────┘

Special Case: SIGINT during getline()
      │ EINTR
      ▼
┌─────────────┐
│   WAITING   │
│ FOR INTERRUPT│
│  RESPONSE   │
└─────────────┘
      │ response_received
      │
      └─────────► READING INPUT
```

#### MAIN THREAD States:

```
┌─────────────┐
│   WAITING   │ ◄────────────┐
│ FOR MESSAGE │              │
└─────────────┘              │
      │ message_sent         │
      │                      │
      ▼                      │
┌─────────────┐              │
│   CHECK     │              │
│  SIGNALS    │              │
└─────────────┘              │
      │                      │
      ├─ signal ──┐          │
      │           ▼          │
      │    ┌─────────────┐   │
      │    │  INTERRUPT  │   │
      │    │  PROCESSING │   │
      │    └─────────────┘   │
      │           │          │
      ▼           │          │
┌─────────────┐   │          │
│  RECEIVE    │   │          │
│  RESPONSE   │ ◄─┘          │
└─────────────┘              │
      │                      │
      ▼                      │
┌─────────────┐              │
│   SIGNAL    │              │
│ INPUT THREAD│              │
└─────────────┘              │
      │                      │
      ▼                      │
┌─────────────┐              │
│   RESET     │              │
│   FLAGS     │              │
└─────────────┘              │
      │                      │
      └──────────────────────┘
```

### Critical Synchronization Points

#### Normal Message Flow:
```
INPUT THREAD                    MAIN THREAD
     │                              │
1.   │ getline() → message          │
2.   │ send_expression_message()    │
3.   │ message_sent = 1             │
4.   │ signal(message_cond) ────────┼─► wakes up
5.   │                              │ receive_message()
6.   │                              │ response_received = 1
7.   │                              │ signal(response_cond)
8.   │ ◄────────────────────────────┼── back to loop
```

#### Interrupt Handling Flow:
```
INPUT THREAD                    SIGNAL HANDLER               MAIN THREAD
     │                              │                           │
1.   │ getline() (blocking)         │                           │
2.   │           ◄──── SIGINT ──────┼─ write(signal_pipe[1])    │
3.   │ EINTR                        │ interrupt_requested = 1   │
4.   │ wait(response_cond) ─┐       │                           │
5.   │                      │       │                           │ check signal_pipe[0]
6.   │                      │       │                           │ send_interrupt_message()
7.   │                      │       │                           │ receive_message()
8.   │                      │       │                           │ response_received = 1
9.   │                      └───────┼───────────────────────────┼─ signal(response_cond)
10.  │ wakes up                     │                           │
11.  │ continue (next iteration)    │                           │ reset flags + drain signals
```

### Race Condition Analysis

**The Core Problem** is in the interrupt handling synchronization between steps 8-11:

```
MAIN THREAD                           INPUT THREAD
│                                     │
│ response_received = 1               │ wait(response_cond)
│ if (was_interrupt) {                │     while (!response_received &&
│     interrupt_requested = 0   ◄─────┼─────── !input_complete &&
│     response_received = 0           │           interrupt_requested)
│ }                                   │
│ signal(response_cond) ──────────────┼─► SHOULD wake up
│                                     │
│ [drain signals]                     │ BUT: condition may evaluate
│                                     │      incorrectly due to
│                                     │      flag reset timing
```

**The Issue:** The input thread's wait condition includes
`interrupt_requested`, but the main thread resets this flag to 0
during interrupt processing. This creates a race where:

1. Input thread starts waiting with `interrupt_requested = 1`
2. Main thread processes interrupt, sets `interrupt_requested = 0`
3. Input thread's condition becomes false BEFORE `response_received = 1`
4. Input thread may exit the wait prematurely or never properly synchronize

**The Solution:** The interrupt handling needs to ensure proper
sequencing. The input thread should wait until it receives the proper
signal indicating the interrupt response is fully processed, rather
than relying on the `interrupt_requested` flag which gets reset during
processing.