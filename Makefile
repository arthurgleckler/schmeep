.PHONY: logs push run test

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

CHIBI_LIB_C_FILES := $(shell find chibi-scheme/lib -name "*.c" 2>/dev/null)
CHIBI_LIB_SO_FILES := $(patsubst chibi-scheme/lib/%.c,makecapk/lib/arm64-v8a/%.so,$(CHIBI_LIB_C_FILES))

$(CHIBI_TARGET_ARM64):
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

chibi-lib-sos: $(CHIBI_LIB_SO_FILES)

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

$(CHIBI_ASSETS_DIR): $(CHIBI_SCHEME_DIR)/lib $(CHIBI_TARGET_ARM64) chibi-lib-sos lib/schmeep/exception-formatter.sld
	mkdir -p $@
	cd $(CHIBI_SCHEME_DIR)/lib && find . \( -name "*.scm" -o -name "*.sld" \) \
		-exec cp --parents {} ../../$@/ \;
	mkdir -p $@/schmeep
	cp lib/schmeep/exception-formatter.sld $@/schmeep/
	@for so_file in $(CHIBI_LIB_SO_FILES); do \
		rel_path=$$(echo $$so_file | sed 's|makecapk/lib/arm64-v8a/||' | sed 's|\.so$$||'); \
		target_dir=$@/$$(dirname $$rel_path); \
		target_file=$$target_dir/$$(basename $$rel_path).so; \
		mkdir -p $$target_dir; \
		if [ -f $$so_file ]; then cp $$so_file $$target_file; fi; \
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