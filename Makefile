.PHONY: push run

ADB ?= adb
ANDROID_VERSION ?= 33
ANDROID_SRCS := main_jni.c
ANDROID_TARGET ?= $(ANDROID_VERSION)
APPNAME ?= repl
APKFILE ?= $(APPNAME).apk
CHIBI_ASSETS_DIR := Sources/assets/lib
CHIBI_SCHEME_DIR := chibi-scheme
CHIBI_TARGET_ARM64 := makecapk/lib/arm64-v8a/$(CHIBI_SCHEME_LIB_NAME)
CFLAGS ?= -ffunction-sections -Os -fdata-sections -Wall -fvisibility=hidden -g \
	-DANDROID -DAPPNAME=\"$(APPNAME)\" -DANDROIDVERSION=$(ANDROID_VERSION) \
	-fPIC -I. -I$(CHIBI_SCHEME_DIR)/include
CHIBI_SCHEME_LIB_NAME := libchibi-scheme.so
CHIBI_SCHEME_LIB := $(CHIBI_SCHEME_DIR)/$(CHIBI_SCHEME_LIB_NAME)
LDFLAGS ?= -Wl,--gc-sections -Wl,-Map=output.map -lm -lGLESv3 -lEGL -landroid -llog -lOpenSLES -shared
PACKAGE_NAME ?= com.speechcode.$(APPNAME)

UNAME := $(shell uname)
OS_NAME := $(if $(filter Linux,$(UNAME)),linux-x86_64,\
	$(if $(filter Darwin,$(UNAME)),darwin-x86_64,windows-x86_64))

SDK_LOCATIONS := $(ANDROID_HOME) $(ANDROID_SDK_ROOT) ~/Android/Sdk $(HOME)/Library/Android/sdk
ANDROID_SDK ?= $(firstword $(wildcard $(SDK_LOCATIONS)))
BUILD_TOOLS ?= $(lastword $(wildcard $(ANDROID_SDK)/build-tools/*))
AAPT := $(BUILD_TOOLS)/aapt
NDK ?= $(firstword $(ANDROID_NDK) $(ANDROID_NDK_HOME) \
	$(lastword $(wildcard $(ANDROID_SDK)/ndk/*)) $(wildcard $(ANDROID_SDK)/ndk-bundle/*))

ANDROID_JAR := $(ANDROID_SDK)/platforms/android-$(ANDROID_VERSION)/android.jar

CC_ARM64 := $(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/bin/aarch64-linux-android$(ANDROID_VERSION)-clang
CFLAGS_ARM64 := -m64
CHIBI_CFLAGS := $(filter-out -fvisibility=hidden -Os, $(CFLAGS)) -g -O0 -DSEXP_USE_GREEN_THREADS=1 -DSEXP_DEFAULT_QUANTUM=50
TARGETS += makecapk/lib/arm64-v8a/lib$(APPNAME).so

$(CHIBI_TARGET_ARM64):
	mkdir -p makecapk/lib/arm64-v8a
	# Build Android chibi-scheme with dynamic loading support
	$(MAKE) -C $(CHIBI_SCHEME_DIR) libchibi-scheme.so \
		CC='$(CC_ARM64)' \
		CFLAGS='$(CHIBI_CFLAGS) $(CFLAGS_ARM64)' \
		LDFLAGS='$(LDFLAGS)' \
		LIBCHIBI_FLAGS='-Wl,-soname,libchibi-scheme.so' \
		PLATFORM=android ARCH=aarch64
	cp $(CHIBI_SCHEME_LIB) $@
	# Build SRFI 27 shared library for Android
	$(CC_ARM64) -fPIC -shared $(CHIBI_CFLAGS) $(CFLAGS_ARM64) \
		-Ichibi-scheme/include \
		-o makecapk/lib/arm64-v8a/srfi-27-rand.so \
		chibi-scheme/lib/srfi/27/rand.c \
		-L$(dir $@) -lchibi-scheme

makecapk/lib/arm64-v8a/lib$(APPNAME).so: $(ANDROID_SRCS) $(CHIBI_TARGET_ARM64)
	$(CC_ARM64) $(CFLAGS) $(CFLAGS_ARM64) -o $@ $(filter %.c,$^) -L$(dir $@) \
	-L$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/sysroot/usr/lib/aarch64-linux-android/$(ANDROID_VERSION) \
	$(LDFLAGS) -lchibi-scheme

$(CHIBI_ASSETS_DIR): $(CHIBI_SCHEME_DIR)/lib $(CHIBI_TARGET_ARM64)
	@echo "Copying Chibi Scheme .scm, .sld, and .so files to Sources/assets/."
	mkdir -p $@
	cd $(CHIBI_SCHEME_DIR)/lib && find . \( -name "*.scm" -o -name "*.sld" \) \
		-exec cp --parents {} ../../$@/ \;
	# Copy the Android SRFI 27 shared library to the right location
	mkdir -p $@/srfi/27
	cp makecapk/lib/arm64-v8a/srfi-27-rand.so $@/srfi/27/rand.so
	@echo "Chibi Scheme assets and shared libraries copied"

all: chb makecapk.apk

AndroidManifest.xml:
	rm -rf AndroidManifest.xml
	PACKAGE_NAME=$(PACKAGE_NAME) \
		ANDROID_TARGET=$(ANDROID_TARGET) \
		ANDROID_VERSION=$(ANDROID_VERSION) \
		APPNAME=$(APPNAME) \
		envsubst '$$ANDROID_TARGET $$ANDROID_VERSION $$APPNAME $$PACKAGE_NAME' \
		< AndroidManifest.xml.template > AndroidManifest.xml

chb: chb.c
	@echo "Compiling Bluetooth client"
	gcc -o chb chb.c -lbluetooth -lpthread
	@echo "Bluetooth client compiled successfully"

classes.dex: \
	src/main/java/com/speechcode/repl/BluetoothReplService.java \
	src/main/java/com/speechcode/repl/DebugWebChromeClient.java \
	src/main/java/com/speechcode/repl/EvaluationRequest.java \
	src/main/java/com/speechcode/repl/MainActivity.java \
	src/main/java/com/speechcode/repl/SchemeInterface.java
	@echo "Compiling Java sources."
	mkdir -p build/classes
	javac -cp $(ANDROID_JAR) -d build/classes src/main/java/com/speechcode/repl/*.java
	@echo "Creating classes.dex."
	$(BUILD_TOOLS)/d8 --classpath $(ANDROID_JAR) --output . \
		build/classes/com/speechcode/repl/BluetoothReplService.class \
		build/classes/com/speechcode/repl/DebugWebChromeClient.class \
		build/classes/com/speechcode/repl/EvaluationRequest.class \
		build/classes/com/speechcode/repl/MainActivity.class \
		build/classes/com/speechcode/repl/SchemeInterface.class
	@echo "Java compilation completed"

clean:
	rm -rf AndroidManifest.xml $(APKFILE) chb classes.dex build/ makecapk.apk makecapk temp.apk

makecapk.apk: $(TARGETS) $(CHIBI_ASSETS_DIR) AndroidManifest.xml classes.dex
	rm -f $(APKFILE)
	mkdir -p makecapk/assets
	cp -r Sources/assets/* makecapk/assets
	cp classes.dex makecapk/
	$(AAPT) package -f -F temp.apk -I $(ANDROID_JAR) -M AndroidManifest.xml \
		-S Sources/res -A makecapk/assets -v --target-sdk-version $(ANDROID_TARGET)
	unzip -o temp.apk -d makecapk
	cd makecapk && zip -D4r ../makecapk.apk . && \
		zip -D0r ../makecapk.apk ./resources.arsc ./AndroidManifest.xml
	$(BUILD_TOOLS)/zipalign -v 4 makecapk.apk $(APKFILE)
	$(BUILD_TOOLS)/apksigner sign --key-pass pass:password --ks-pass pass:password \
		--ks my-release-key.keystore $(APKFILE)
	rm -rf temp.apk makecapk.apk
	@ls -l $(APKFILE)

push: makecapk.apk
	@echo "Installing" $(PACKAGE_NAME)
	$(ADB) install -r $(APKFILE)

run: push
	$(eval ACTIVITYNAME:=$(shell $(AAPT) dump badging $(APKFILE) | \
		grep "launchable-activity" | cut -f 2 -d"'"))
	$(ADB) shell am start -n $(PACKAGE_NAME)/$(ACTIVITYNAME)

uninstall:
	($(ADB) uninstall $(PACKAGE_NAME))||true