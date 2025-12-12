.PHONY: logs push run test chibi-lib-sos $(CHIBI_ASSETS_DIR)

ADB ?= adb
ANDROID_VERSION ?= 33
ANDROID_SRCS := main_jni.c
ANDROID_TARGET ?= $(ANDROID_VERSION)
APPNAME ?= schmeep
APKFILE ?= $(APPNAME).apk
CHIBI_ASSETS_DIR := Sources/assets/lib
CHIBI_SCHEME_DIR := chibi-scheme
CHIBI_SCHEME_LIB_NAME := libchibi-scheme.so
CHIBI_TARGET_ARM64 := makecapk/lib/arm64-v8a/$(CHIBI_SCHEME_LIB_NAME)
CFLAGS ?= -ffunction-sections -Os -fdata-sections -Wall -fvisibility=hidden -g \
	-DANDROID -DAPPNAME=\"$(APPNAME)\" -DANDROIDVERSION=$(ANDROID_VERSION) \
	-fPIC -I. -I$(CHIBI_SCHEME_DIR)/include
CHIBI_SCHEME_LIB := $(CHIBI_SCHEME_DIR)/$(CHIBI_SCHEME_LIB_NAME)
LDFLAGS ?= -Wl,--gc-sections -Wl,-Map=output.map -lm -lGLESv3 -lEGL -landroid -llog -lOpenSLES -shared
PACKAGE_NAME ?= com.speechcode.$(APPNAME)
BUILD_TIMESTAMP := $(shell date +%s)
BUILD_VERSION := 1.0.$(BUILD_TIMESTAMP)

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

# Note: CHIBI_LIB_C_FILES is evaluated at parse time, before stub .c files are generated
# The chibi-lib-sos target will dynamically find all .c files after generation
CHIBI_LIB_C_FILES := $(shell find chibi-scheme/lib -name "*.c" 2>/dev/null)
CHIBI_LIB_SO_FILES := $(patsubst chibi-scheme/lib/%.c,makecapk/lib/arm64-v8a/%.so,$(CHIBI_LIB_C_FILES))

# Sentinel file to track whether stub .c files have been generated
CHIBI_STUB_SENTINEL := chibi-scheme/.stub-files-generated

# Sentinel file to track whether .so files have been built
CHIBI_SO_SENTINEL := chibi-scheme/.so-files-built

$(CHIBI_STUB_SENTINEL):
	@echo "Generating .c files from .stub files..."
	@cd chibi-scheme && \
		if $(MAKE) all-c-files 2>/dev/null; then \
			echo "Generated using chibi-scheme Makefile"; \
		else \
			echo "Chibi Makefile target not found, building native chibi-scheme..."; \
			$(MAKE) chibi-scheme; \
			for stub in $$(find lib -name "*.stub"); do \
				echo "Generating $${stub%.stub}.c"; \
				./chibi-scheme -q tools/chibi-ffi "$$stub" > "$${stub%.stub}.c" || true; \
			done; \
			echo "Cleaning native chibi-scheme build artifacts..."; \
			rm -f chibi-scheme libchibi-scheme.so* *.o; \
		fi
	@touch $@

$(CHIBI_TARGET_ARM64): $(CHIBI_STUB_SENTINEL)
	mkdir -p makecapk/lib/arm64-v8a
	mkdir -p $(CHIBI_SCHEME_DIR)/lib/chibi/io
	cp io-stub.c $(CHIBI_SCHEME_DIR)/lib/chibi/io/io-stub.c
	$(MAKE) -C $(CHIBI_SCHEME_DIR) libchibi-scheme.so \
		CC='$(CC_ARM64)' \
		CFLAGS='$(CHIBI_CFLAGS) $(CFLAGS_ARM64)' \
		LDFLAGS='$(LDFLAGS)' \
		LIBCHIBI_FLAGS='-Wl,-soname,libchibi-scheme.so' \
		PLATFORM=android ARCH=aarch64
	cp $(CHIBI_SCHEME_LIB) $@

$(CHIBI_SO_SENTINEL): $(CHIBI_STUB_SENTINEL) $(CHIBI_TARGET_ARM64)
	@echo "Building .so files from all .c files (including generated ones)..."
	@for c_file in $$(find chibi-scheme/lib -name "*.c" 2>/dev/null); do \
		so_file=$$(echo $$c_file | sed 's|chibi-scheme/lib/|makecapk/lib/arm64-v8a/|' | sed 's|\.c$$|.so|'); \
		if [ ! -f "$$so_file" ] || [ "$$c_file" -nt "$$so_file" ]; then \
			$(MAKE) "$$so_file"; \
		fi; \
	done
	@touch $@

chibi-lib-sos: $(CHIBI_SO_SENTINEL)

logs:
	adb logcat -s schmeep

makecapk/lib/arm64-v8a/%.so: chibi-scheme/lib/%.c $(CHIBI_TARGET_ARM64)
	@mkdir -p $(dir $@)
	@echo "Building $@..."
	@if $(CC_ARM64) -fPIC -shared $(CHIBI_CFLAGS) $(CFLAGS_ARM64) \
		-Ichibi-scheme/include \
		-o $@ $< \
		-L$(dir $(CHIBI_TARGET_ARM64)) -lchibi-scheme 2>/dev/null; then \
		echo "Successfully built $@"; \
	else \
		echo "Failed to build $@ (skipping)"; \
		touch $@; \
	fi

makecapk/lib/arm64-v8a/lib$(APPNAME).so: $(ANDROID_SRCS) $(CHIBI_TARGET_ARM64)
	$(CC_ARM64) $(CFLAGS) $(CFLAGS_ARM64) -o $@ $(filter %.c,$^) -L$(dir $@) \
	-L$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/sysroot/usr/lib/aarch64-linux-android/$(ANDROID_VERSION) \
	$(LDFLAGS) -lchibi-scheme

$(CHIBI_ASSETS_DIR): $(CHIBI_SCHEME_DIR)/lib $(CHIBI_SO_SENTINEL) lib/schmeep/exception-formatter.sld lib/eg.scm
	mkdir -p $@
	cd $(CHIBI_SCHEME_DIR)/lib && find . \( -name "*.scm" -o -name "*.sld" \) \
		! -name "*~" -exec cp --parents {} ../../$@/ \;
	mkdir -p $@/schmeep
	cp lib/schmeep/exception-formatter.sld $@/schmeep/
	cp lib/eg.scm $@/
	@echo "Copying .so files (including those from generated .c files)..."
	@for so_file in $$(find makecapk/lib/arm64-v8a -name "*.so" 2>/dev/null); do \
		rel_path=$$(echo $$so_file | sed 's|makecapk/lib/arm64-v8a/||'); \
		target_dir=$@/$$(dirname $$rel_path); \
		target_file=$$target_dir/$$(basename $$rel_path); \
		mkdir -p $$target_dir; \
		if [ -f "$$so_file" ]; then \
			cp "$$so_file" "$$target_file"; \
		fi; \
	done

all: makecapk.apk schmeep

AndroidManifest.xml:
	rm -rf AndroidManifest.xml
	PACKAGE_NAME=$(PACKAGE_NAME) \
		ANDROID_TARGET=$(ANDROID_TARGET) \
		ANDROID_VERSION=$(ANDROID_VERSION) \
		APPNAME=$(APPNAME) \
		BUILD_TIMESTAMP=$(BUILD_TIMESTAMP) \
		BUILD_VERSION=$(BUILD_VERSION) \
		envsubst '$$ANDROID_TARGET $$ANDROID_VERSION $$APPNAME $$PACKAGE_NAME $$BUILD_TIMESTAMP $$BUILD_VERSION' \
		< AndroidManifest.xml.template > AndroidManifest.xml

classes.dex: src/main/java/com/speechcode/schmeep/*.java
	mkdir -p build/classes
	javac -cp $(ANDROID_JAR) -d build/classes src/main/java/com/speechcode/schmeep/*.java
	$(BUILD_TOOLS)/d8 --classpath $(ANDROID_JAR) --output . build/classes/com/speechcode/schmeep/*.class

clean:
	rm -rf AndroidManifest.xml $(APKFILE) schmeep classes.dex build/ makecapk.apk makecapk temp.apk
	rm -f $(CHIBI_SCHEME_DIR)/libchibi-scheme.so*
	rm -f $(CHIBI_SCHEME_DIR)/*.o
	rm -f $(CHIBI_STUB_SENTINEL)
	rm -f $(CHIBI_SO_SENTINEL)

distclean: clean
	@echo "Removing generated .c files from .stub files..."
	@cd $(CHIBI_SCHEME_DIR) && \
		for stub in $$(find lib -name "*.stub" 2>/dev/null); do \
			c_file="$${stub%.stub}.c"; \
			if [ -f "$$c_file" ]; then \
				echo "Removing $$c_file"; \
				rm -f "$$c_file"; \
			fi; \
		done
	@echo "Removing chibi-scheme build artifacts..."
	@cd $(CHIBI_SCHEME_DIR) && $(MAKE) clean 2>/dev/null || true

format: format-c format-java

format-c: schmeep.c main_jni.c
	for file in $^; do \
	  clang-format --style='{ColumnLimit: 80, IndentWidth: 2}' "$$file" | \
	  unexpand -t 8 --first-only > "$$file.tmp" && \
	  mv "$$file.tmp" "$$file"; \
	done

format-java: src/main/java/com/speechcode/schmeep/*.java
	for file in $^; do \
	  clang-format --style='{ColumnLimit: 80, IndentWidth: 4}' "$$file" | \
	  unexpand -t 8 --first-only > "$$file.tmp" && \
	  mv "$$file.tmp" "$$file"; \
	done

makecapk.apk: $(TARGETS) $(CHIBI_ASSETS_DIR) AndroidManifest.xml classes.dex
	rm -f $(APKFILE)
	mkdir -p makecapk/assets
	rsync -a --exclude='*~' Sources/assets/ makecapk/assets/
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
	$(ADB) install -r $(APKFILE)

run: push
	$(eval ACTIVITYNAME:=$(shell $(AAPT) dump badging $(APKFILE) | \
		grep "launchable-activity" | cut -f 2 -d"'"))
	$(ADB) shell am start -n $(PACKAGE_NAME)/$(ACTIVITYNAME)

schmeep: schmeep.c
	gcc -o schmeep schmeep.c -lbluetooth -lpthread

test: schmeep
	./tests/schmeep.expect

uninstall:
	($(ADB) uninstall $(PACKAGE_NAME))||true