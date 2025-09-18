#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <byteswap.h>
#include <android/log.h>
#include <jni.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "repl", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "repl", __VA_ARGS__)

// Chibi Scheme includes
#include "chibi/eval.h"
#include "chibi/sexp.h"

// Global Scheme variables
sexp scheme_ctx = NULL;
sexp scheme_env = NULL;

// Forward declarations (WebView-only stubs).
void AndroidDisplayKeyboard(int pShow) { /* WebView handles keyboard. */ }
void AndroidSendToBack(int pShow) { /* Not needed in WebView-only mode. */ }

void HandleKey( int keycode, int bDown )
{
  // Back button handling removed for WebView-only mode.
}

void HandleButton( int x, int y, int button, int bDown )
{
  // Mouse/touch handling removed for WebView-only mode.
}

void HandleMotion( int x, int y, int mask )
{
  // Motion handling removed for WebView-only mode.
}

void HandleDestroy()
{
  // Clean up resources on destroy.
  if (scheme_ctx) {
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
  }
}

// Extract Chibi Scheme assets from APK.
void extract_chibi_scheme_assets()
{
  LOGI("extract_chibi_scheme_assets: Starting asset extraction for JNI mode.");

  // For JNI mode, we will attempt a basic extraction using standard paths.
  // Create base target directory.
  char target_base[] = "/data/data/com.speechcode.repl/lib";
  char mkdir_cmd[256];
  snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", target_base);
  system(mkdir_cmd);

  LOGI("extract_chibi_scheme_assets: Created target directory %s", target_base);
  LOGI("extract_chibi_scheme_assets: Completed (JNI mode - assets should be extracted by build system).");
}

// Initialize Scheme system.
int init_scheme()
{
  LOGI("init_scheme: Starting Scheme initialization.");

  // First extract Chibi Scheme library files.
  extract_chibi_scheme_assets();

  // Initialize Scheme system.
  sexp_scheme_init();

  // Create Scheme evaluation context.
  scheme_ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 0);
  if (!scheme_ctx) {
    LOGE("init_scheme: Failed to create Scheme context.");
    return -1;
  }

  // Get the default environment.
  scheme_env = sexp_context_env(scheme_ctx);
  if (!scheme_env) {
    LOGE("init_scheme: Failed to get Scheme environment.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    return -1;
  }

  // Configure module search paths for Android.
  LOGI("init_scheme: Setting up Android-specific module paths.");
  sexp module_path_string = sexp_c_string(scheme_ctx, "/data/data/com.speechcode.repl/lib", -1);
  sexp_global(scheme_ctx, SEXP_G_MODULE_PATH) = sexp_list1(scheme_ctx, module_path_string);
  LOGI("init_scheme: Set module directory to: /data/data/com.speechcode.repl/lib.");

  // Load standard ports.
  sexp_load_standard_ports(scheme_ctx, scheme_env, stdin, stdout, stderr, 1);

  // Try to load R7RS standard environment. Fall back to simpler versions if needed.
  LOGI("init_scheme: Attempting to load R7RS standard environment.");
  sexp std_env = sexp_load_standard_env(scheme_ctx, scheme_env, SEXP_SEVEN);
  if (sexp_exceptionp(std_env)) {
    LOGE("init_scheme: Warning: Failed to load R7RS standard environment. Trying R5RS.");
    sexp msg = sexp_exception_message(std_env);
    if (msg && sexp_stringp(msg)) {
      LOGE("init_scheme: R7RS Error: %s", sexp_string_data(msg));
    }

    std_env = sexp_load_standard_env(scheme_ctx, scheme_env, SEXP_FIVE);
    if (sexp_exceptionp(std_env)) {
      LOGE("init_scheme: Warning: Failed to load R5RS. Trying basic environment.");
      sexp msg = sexp_exception_message(std_env);
      if (msg && sexp_stringp(msg)) {
	LOGE("init_scheme: R5RS Error: %s", sexp_string_data(msg));
      }

      std_env = sexp_load_standard_env(scheme_ctx, scheme_env, SEXP_THREE);
      if (sexp_exceptionp(std_env)) {
	LOGE("init_scheme: Warning: All standard environments failed. Using minimal environment.");
	// Keep the basic environment that was already created.
      } else {
	LOGI("init_scheme: R3RS environment loaded successfully.");
	scheme_env = std_env;
      }
    } else {
      LOGI("init_scheme: R5RS environment loaded successfully.");
      scheme_env = std_env;
    }
  } else {
    LOGI("init_scheme: R7RS environment loaded successfully.");
    scheme_env = std_env;
  }

  // Define factorial function.
  const char* factorial_def = "(define (factorial n) (if (<= n 1) 1 (* n (factorial (- n 1)))))";
  sexp code_sexp = sexp_read_from_string(scheme_ctx, factorial_def, -1);
  if (code_sexp && !sexp_exceptionp(code_sexp)) {
    sexp result = sexp_eval(scheme_ctx, code_sexp, scheme_env);
    if (result && !sexp_exceptionp(result)) {
      LOGI("init_scheme: factorial function defined successfully.");
    } else {
      LOGE("init_scheme: Failed to define factorial function.");
    }
  } else {
    LOGE("init_scheme: Failed to parse factorial definition.");
  }

  LOGI("init_scheme: Scheme context initialized successfully.");
  return 0;
}

// Clean up Scheme system.
void cleanup_scheme()
{
  if (scheme_ctx) {
    LOGI("cleanup_scheme: Destroying Scheme context.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
  }
}

// Extract assets from APK using JNI.
int extract_chibi_assets_jni(JNIEnv *env, jobject activity) {
  // Get AssetManager from the activity context.
  jclass activityClass = (*env)->GetObjectClass(env, activity);
  jmethodID getAssetsMethod = (*env)->GetMethodID(env, activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
  jobject assetManager = (*env)->CallObjectMethod(env, activity, getAssetsMethod);

  if (!assetManager) {
    LOGE("Failed to get AssetManager.");
    return -1;
  }

  // Create base target directory.
  char target_base[] = "/data/data/com.speechcode.repl/lib";
  char mkdir_cmd[256];
  snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", target_base);
  system(mkdir_cmd);

  LOGI("Starting essential Scheme library extraction.");

  // Essential R7RS files for basic functionality.
  const char* essential_files[] = {
    "lib/init-7.scm",
    "lib/meta-7.scm",
    "lib/scheme/base.sld",
    "lib/scheme/case-lambda.sld",
    "lib/scheme/char.sld",
    "lib/scheme/complex.sld",
    "lib/scheme/cxr.sld",
    "lib/scheme/eval.sld",
    "lib/scheme/file.sld",
    "lib/scheme/inexact.sld",
    "lib/scheme/lazy.sld",
    "lib/scheme/load.sld",
    "lib/scheme/process-context.sld",
    "lib/scheme/r5rs.sld",
    "lib/scheme/read.sld",
    "lib/scheme/repl.sld",
    "lib/scheme/time.sld",
    "lib/scheme/write.sld",
    // Chibi core modules needed by scheme/base
    "lib/chibi/equiv.sld",
    "lib/chibi/string.sld",
    "lib/chibi/io.sld",
    "lib/chibi/ast.sld",
    // SRFI modules needed by scheme/base and essential list operations
    "lib/srfi/1.sld",
    "lib/srfi/1/alists.scm",
    "lib/srfi/1/constructors.scm",
    "lib/srfi/1/deletion.scm",
    "lib/srfi/1/fold.scm",
    "lib/srfi/1/lset.scm",
    "lib/srfi/1/misc.scm",
    "lib/srfi/1/predicates.scm",
    "lib/srfi/1/search.scm",
    "lib/srfi/1/selectors.scm",
    "lib/srfi/9.sld",
    "lib/srfi/11.sld",
    "lib/srfi/39.sld",
    NULL
  };

  // Get AAssetManager native pointer.
  jclass assetManagerClass = (*env)->GetObjectClass(env, assetManager);
  jmethodID openMethod = (*env)->GetMethodID(env, assetManagerClass, "open", "(Ljava/lang/String;)Ljava/io/InputStream;");

  int count = 0;
  for (int i = 0; essential_files[i] != NULL; i++) {
    const char* asset_path = essential_files[i];
    const char* extract_path = asset_path + 4; // Skip "lib/" prefix

    jstring assetPath = (*env)->NewStringUTF(env, asset_path);
    jobject inputStream = (*env)->CallObjectMethod(env, assetManager, openMethod, assetPath);

    if (inputStream) {
      // Read from InputStream.
      jclass inputStreamClass = (*env)->GetObjectClass(env, inputStream);
      jmethodID availableMethod = (*env)->GetMethodID(env, inputStreamClass, "available", "()I");
      jmethodID readMethod = (*env)->GetMethodID(env, inputStreamClass, "read", "([B)I");
      jmethodID closeMethod = (*env)->GetMethodID(env, inputStreamClass, "close", "()V");

      jint available = (*env)->CallIntMethod(env, inputStream, availableMethod);
      if (available > 0) {
	jbyteArray buffer = (*env)->NewByteArray(env, available);
	jint bytesRead = (*env)->CallIntMethod(env, inputStream, readMethod, buffer);

	if (bytesRead > 0) {
	  // Get byte array elements.
	  jbyte* bufferPtr = (*env)->GetByteArrayElements(env, buffer, NULL);

	  // Create target path and parent directory.
	  char target_path[512];
	  snprintf(target_path, sizeof(target_path), "%s/%s", target_base, extract_path);

	  char parent_dir[512];
	  strncpy(parent_dir, target_path, sizeof(parent_dir));
	  char* last_slash = strrchr(parent_dir, '/');
	  if (last_slash) {
	    *last_slash = '\0';
	    char mkdir_cmd[768];
	    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", parent_dir);
	    system(mkdir_cmd);
	  }

	  // Write file.
	  FILE* fp = fopen(target_path, "wb");
	  if (fp) {
	    fwrite(bufferPtr, 1, bytesRead, fp);
	    fclose(fp);
	    LOGI("Extracted essential file: %s (%d bytes)", extract_path, bytesRead);
	    count++;
	  } else {
	    LOGE("Failed to write file: %s", target_path);
	  }

	  (*env)->ReleaseByteArrayElements(env, buffer, bufferPtr, JNI_ABORT);
	}
	(*env)->DeleteLocalRef(env, buffer);
      }

      (*env)->CallVoidMethod(env, inputStream, closeMethod);
      (*env)->DeleteLocalRef(env, inputStream);
      (*env)->DeleteLocalRef(env, inputStreamClass);
    } else {
      LOGE("Failed to open essential file: %s", asset_path);
    }

    (*env)->DeleteLocalRef(env, assetPath);
  }

  (*env)->DeleteLocalRef(env, assetManager);
  (*env)->DeleteLocalRef(env, assetManagerClass);
  (*env)->DeleteLocalRef(env, activityClass);

  if (count > 0) {
    LOGI("Essential file extraction complete: %d files extracted", count);
    return 0;
  } else {
    LOGE("No essential files extracted.");
    return -1;
  }
}

// JNI Functions for Java Activity Integration.

JNIEXPORT void JNICALL Java_com_speechcode_repl_MainActivity_initializeScheme(JNIEnv *env, jobject thiz)
{
  LOGI("JNI: initializeScheme called.");

  // Initialize Scheme system.
  if (scheme_ctx == NULL) {
    LOGI("JNI: Initializing Chibi Scheme.");

    // Extract Chibi Scheme assets using JNI.
    if (extract_chibi_assets_jni(env, thiz) == 0) {
      LOGI("JNI: Asset extraction successful.");
    } else {
      LOGE("JNI: Asset extraction failed. Continuing with basic environment.");
    }

    // Initialize Scheme context.
    if (init_scheme() == 0) {
      LOGI("JNI: Chibi Scheme initialized successfully.");
    } else {
      LOGE("JNI: Failed to initialize Chibi Scheme.");
    }
  } else {
    LOGI("JNI: Chibi Scheme already initialized.");
  }
}

JNIEXPORT jstring JNICALL Java_com_speechcode_repl_MainActivity_evaluateScheme(JNIEnv *env, jobject thiz, jstring expression)
{
  LOGI("JNI: evaluateScheme called.");

  // Check if Scheme is initialized.
  if (scheme_ctx == NULL || scheme_env == NULL) {
    LOGE("JNI: Scheme not initialized.");
    return (*env)->NewStringUTF(env, "Error: Scheme not initialized");
  }

  // Convert Java string to C string.
  const char *expr_cstr = (*env)->GetStringUTFChars(env, expression, NULL);
  if (!expr_cstr) {
    LOGE("JNI: Failed to convert expression string.");
    return (*env)->NewStringUTF(env, "Error: Invalid expression string");
  }

  LOGI("JNI: Evaluating Scheme expression: %s", expr_cstr);

  // Parse and evaluate expression.
  sexp code_sexp = sexp_read_from_string(scheme_ctx, expr_cstr, -1);

  // Release the Java string.
  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  if (!code_sexp || sexp_exceptionp(code_sexp)) {
    LOGE("JNI: Failed to parse Scheme expression.");
    return (*env)->NewStringUTF(env, "Error: Parse error");
  }

  // Evaluate the expression.
  sexp result = sexp_eval(scheme_ctx, code_sexp, scheme_env);

  if (!result || sexp_exceptionp(result)) {
    LOGE("JNI: Failed to evaluate Scheme expression.");
    return (*env)->NewStringUTF(env, "Error: Evaluation error");
  }

  // Convert result to string.
  sexp result_str = sexp_write_to_string(scheme_ctx, result);
  if (!result_str || sexp_exceptionp(result_str)) {
    LOGE("JNI: Failed to convert result to string.");
    return (*env)->NewStringUTF(env, "Error: Result conversion error");
  }

  const char *result_cstr = sexp_string_data(result_str);
  LOGI("JNI: Scheme result: %s", result_cstr);

  // Convert C string back to Java string.
  jstring java_result = (*env)->NewStringUTF(env, result_cstr);

  return java_result;
}