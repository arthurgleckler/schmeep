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

## Client Threading Architecture (chb.c) - Redesigned

### Clean Architecture Overview

The threading system has been redesigned to eliminate race conditions and simplify synchronization using a message-queue approach with proper interrupt handling.

### Thread Structure Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        MAIN PROCESS                         │
│                                                             │
│  ┌─────────────────┐              ┌─────────────────────┐   │
│  │   INPUT THREAD  │              │   MAIN THREAD       │   │
│  │                 │              │   (Socket Handler)  │   │
│  │ • Read stdin    │─────msg─────►│ • Process queue     │   │
│  │ • Create msgs   │              │ • Send to socket    │   │
│  │ • Wait response │◄────sync─────│ • Complete response │   │
│  └─────────────────┘              └─────────────────────┘   │
│                                             │               │
│  ┌────────────────────────────────────────────────────────┐ │
│  │              SIGNAL HANDLER                            │ │
│  │         (sigint_handler - async-safe)                  │ │
│  │      • Sets interrupt_pending = 1                      │ │
│  │      • Writes to signal_pipe[1]                        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Shared State (Minimal)

**Message Structure:**
```c
typedef struct {
    char* message;
    enum { MSG_EXPRESSION, MSG_INTERRUPT, MSG_QUIT } type;
} message_t;
```

**Synchronization Primitives:**
```c
static message_t* pending_message = NULL;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER;
static volatile sig_atomic_t interrupt_pending = 0;
static int signal_pipe[2];
```

### Communication Flow

#### Normal Operation:
```
INPUT THREAD                    MAIN THREAD
     │                              │
1.   │ getline() → create message   │
2.   │ set pending_message          │
3.   │ signal(queue_cond) ──────────┼─► wake up
4.   │ wait(response_cond) ─┐       │ process message
5.   │                      │       │ send to socket
6.   │                      │       │ complete receive
7.   │                      │       │ signal(response_cond)
8.   │ ◄────────────────────┘       │
9.   │ show next prompt             │ wait for next message
```

#### Interrupt Handling (Complete-Response-First):
```
INPUT THREAD                    SIGNAL HANDLER               MAIN THREAD
     │                              │                           │
1.   │ getline() → message sent     │                           │ recv() in progress
2.   │ wait(response_cond)          │                           │
3.   │           ◄──── SIGINT ──────┼─ interrupt_pending = 1    │
4.   │                              │ write(signal_pipe[1])     │
5.   │                              │                           │ select() detects signal
6.   │                              │                           │ continue recv() to completion
7.   │                              │                           │ check interrupt_pending
8.   │                              │                           │ send_interrupt_message()
9.   │                              │                           │ recv() interrupt response
10.  │                              │                           │ signal(response_cond)
11.  │ ◄────────────────────────────┼───────────────────────────┼─ wake up
12.  │ show next prompt             │                           │ reset interrupt_pending
```

### Key Design Principles

**1. Protocol Integrity:** Always complete current response before sending interrupt to prevent socket desynchronization.

**2. Single Message Queue:** Only one pending message at a time eliminates complex state coordination.

**3. Clear Ownership:**
- Input thread owns stdin and message creation
- Main thread owns socket communication
- Signal handler only sets flags

**4. Minimal Shared State:** Only essential data crosses thread boundaries.

### Critical Interrupt Handling

When Ctrl-C occurs during `recv()`, the system:

1. **Signal Handler:** Sets `interrupt_pending = 1` and writes to signal pipe
2. **Main Thread:** Detects signal via `select()` but continues current `recv()` to completion
3. **Complete Response:** Ensures protocol stays synchronized
4. **Send Interrupt:** Only after current response is complete
5. **Process Interrupt Response:** Receive and display interrupt result
6. **Continue:** Return to normal operation

This approach prevents protocol corruption while still providing responsive interrupt handling.

### Eliminated Complexity

The new design removes:
- Multiple volatile flags with unclear timing
- Complex condition variable coordination
- pthread_kill usage
- Race conditions in interrupt handling
- Self-pipe tricks mixed with threading primitives

**Result:** Simpler, more reliable, and easier to maintain threading architecture.