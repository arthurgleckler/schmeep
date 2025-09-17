#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <byteswap.h>
#include <android/log.h>
#include <jni.h>
#include <sys/stat.h>
#include <pthread.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "repl", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "repl", __VA_ARGS__)

#include "chibi/eval.h"
#include "chibi/sexp.h"

sexp scheme_ctx = NULL;
sexp scheme_env = NULL;
static pthread_mutex_t scheme_mutex = PTHREAD_MUTEX_INITIALIZER;

void extract_chibi_scheme_assets() {
  LOGI("extract_chibi_scheme_assets: Starting asset extraction for JNI mode.");

  char target_base[] = "/data/data/com.speechcode.repl/lib";
  char mkdir_cmd[256];

  snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", target_base);
  system(mkdir_cmd);
  LOGI("extract_chibi_scheme_assets: Completed (JNI mode - assets should be extracted by build system).");
}

int init_scheme() {
  LOGI("init_scheme: Starting Scheme initialization.");
  extract_chibi_scheme_assets();
  sexp_scheme_init();
  scheme_ctx = sexp_make_eval_context(NULL, NULL, NULL, 1024*1024, 8*1024*1024);
  if (!scheme_ctx) {
    LOGE("init_scheme: Failed to create Scheme context.");
    return -1;
  }
  scheme_env = sexp_context_env(scheme_ctx);
  if (!scheme_env) {
    LOGE("init_scheme: Failed to get Scheme environment.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    return -1;
  }
  LOGI("init_scheme: Setting up Android-specific module paths.");

  sexp module_path_string = sexp_c_string(scheme_ctx, "/data/data/com.speechcode.repl/lib", -1);

  sexp_global(scheme_ctx, SEXP_G_MODULE_PATH) = sexp_list1(scheme_ctx, module_path_string);
  LOGI("init_scheme: Set module directory to: /data/data/com.speechcode.repl/lib.");
  sexp_load_standard_ports(scheme_ctx, scheme_env, stdin, stdout, stderr, 1);
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

  const char* set_path_expr = "(current-module-path (cons \"/data/data/com.speechcode.repl/lib\" (current-module-path)))";
  sexp path_sexp = sexp_read_from_string(scheme_ctx, set_path_expr, -1);

  if (path_sexp && !sexp_exceptionp(path_sexp)) {
    sexp path_result = sexp_eval(scheme_ctx, path_sexp, scheme_env);
    if (path_result && !sexp_exceptionp(path_result)) {
      LOGI("init_scheme: Library search path configured.");
    } else {
      LOGE("init_scheme: Failed to set library search path.");
    }
  }
  LOGI("init_scheme: Scheme context initialized successfully.");
  return 0;
}

void cleanup_scheme() {
  if (scheme_ctx) {
    LOGI("cleanup_scheme: Destroying Scheme context.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
  }
}

int extract_chibi_assets_jni(JNIEnv *env, jobject activity) {
  jclass activityClass = (*env)->GetObjectClass(env, activity);
  jmethodID getAssetsMethod = (*env)->GetMethodID(env, activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
  jobject assetManager = (*env)->CallObjectMethod(env, activity, getAssetsMethod);

  if (!assetManager) {
    LOGE("Failed to get AssetManager.");
    return -1;
  }

  char target_base[] = "/data/data/com.speechcode.repl/lib";
  char mkdir_cmd[256];

  snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", target_base);
  system(mkdir_cmd);
  LOGI("Starting essential Scheme library extraction.");

  const char* essential_files[] = {
    "lib/chibi/ast.sld",
    "lib/chibi/equiv.sld",
    "lib/chibi/io.sld",
    "lib/chibi/string.sld",
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
    "lib/srfi/11.sld",
    "lib/srfi/27.sld",
    "lib/srfi/27/constructors.scm",
    "lib/srfi/27/rand.so",
    "lib/srfi/39.sld",
    "lib/srfi/9.sld",
    NULL
  };

  jclass assetManagerClass = (*env)->GetObjectClass(env, assetManager);
  jmethodID openMethod = (*env)->GetMethodID(env, assetManagerClass, "open", "(Ljava/lang/String;)Ljava/io/InputStream;");

  int count = 0;
  for (int i = 0; essential_files[i] != NULL; i++) {
    const char* asset_path = essential_files[i];
    const char* extract_path = asset_path + 4; // Skip "lib/" prefix

    jstring assetPath = (*env)->NewStringUTF(env, asset_path);
    jobject inputStream = (*env)->CallObjectMethod(env, assetManager, openMethod, assetPath);

    if (inputStream) {
      jclass inputStreamClass = (*env)->GetObjectClass(env, inputStream);
      jmethodID availableMethod = (*env)->GetMethodID(env, inputStreamClass, "available", "()I");
      jmethodID readMethod = (*env)->GetMethodID(env, inputStreamClass, "read", "([B)I");
      jmethodID closeMethod = (*env)->GetMethodID(env, inputStreamClass, "close", "()V");

      jint available = (*env)->CallIntMethod(env, inputStream, availableMethod);
      if (available > 0) {
	jbyteArray buffer = (*env)->NewByteArray(env, available);
	jint bytesRead = (*env)->CallIntMethod(env, inputStream, readMethod, buffer);

	if (bytesRead > 0) {
	  jbyte* bufferPtr = (*env)->GetByteArrayElements(env, buffer, NULL);
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

	  FILE* fp = fopen(target_path, "wb");

	  if (fp) {
	    fwrite(bufferPtr, 1, bytesRead, fp);
	    fclose(fp);

	    if (strstr(target_path, ".so") != NULL) {
	      chmod(target_path, 0755);
	      LOGI("Extracted shared library: %s (%d bytes)", extract_path, bytesRead);
	    } else {
	      LOGI("Extracted essential file: %s (%d bytes)", extract_path, bytesRead);
	    }
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


JNIEXPORT void JNICALL Java_com_speechcode_repl_MainActivity_initializeScheme(JNIEnv *env, jobject thiz)
{
  LOGI("JNI: initializeScheme called.");

  pthread_mutex_lock(&scheme_mutex);

  if (scheme_ctx == NULL) {
    LOGI("JNI: Initializing Chibi Scheme.");
    if (extract_chibi_assets_jni(env, thiz) == 0) {
      LOGI("JNI: Asset extraction successful.");
    } else {
      LOGE("JNI: Asset extraction failed. Continuing with basic environment.");
    }
    if (init_scheme() == 0) {
      LOGI("JNI: Chibi Scheme initialized successfully.");
    } else {
      LOGE("JNI: Failed to initialize Chibi Scheme.");
    }
  } else {
    LOGI("JNI: Chibi Scheme already initialized.");
  }

  pthread_mutex_unlock(&scheme_mutex);
}

JNIEXPORT jstring JNICALL Java_com_speechcode_repl_MainActivity_evaluateScheme(JNIEnv *env, jobject thiz, jstring expression)
{
  LOGI("JNI: evaluateScheme called.");

  // Lock mutex to ensure thread-safe access to Scheme context
  pthread_mutex_lock(&scheme_mutex);

  if (scheme_ctx == NULL || scheme_env == NULL) {
    LOGE("JNI: Scheme not initialized - ctx=%p env=%p", scheme_ctx, scheme_env);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Scheme not initialized");
  }

  LOGI("JNI: Context validation passed - ctx=%p env=%p", scheme_ctx, scheme_env);

  const char *expr_cstr = (*env)->GetStringUTFChars(env, expression, NULL);

  if (!expr_cstr) {
    LOGE("JNI: Failed to convert expression string.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Invalid expression string");
  }

  LOGI("JNI: Evaluating Scheme expression: %s", expr_cstr);
  LOGI("JNI: About to call sexp_read_from_string - ctx=%p", scheme_ctx);

  sexp code_sexp = sexp_read_from_string(scheme_ctx, expr_cstr, -1);

  LOGI("JNI: sexp_read_from_string returned - code_sexp=%p", code_sexp);

  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  if (!code_sexp || sexp_exceptionp(code_sexp)) {
    LOGE("JNI: Failed to parse Scheme expression - code_sexp=%p exception=%d",
         code_sexp, code_sexp ? sexp_exceptionp(code_sexp) : -1);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Parse error");
  }

  LOGI("JNI: About to call sexp_eval - ctx=%p code_sexp=%p env=%p", scheme_ctx, code_sexp, scheme_env);

  sexp result = sexp_eval(scheme_ctx, code_sexp, scheme_env);

  LOGI("JNI: sexp_eval returned - result=%p", result);

  if (!result || sexp_exceptionp(result)) {
    LOGE("JNI: Failed to evaluate Scheme expression.");
    if (result && sexp_exceptionp(result)) {
      sexp msg = sexp_exception_message(result);

      if (msg && sexp_stringp(msg)) {
        LOGE("JNI: Scheme error: %s", sexp_string_data(msg));
      }
    }
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Evaluation error");
  }

  LOGI("JNI: About to call sexp_write_to_string - ctx=%p result=%p", scheme_ctx, result);

  sexp result_str = sexp_write_to_string(scheme_ctx, result);

  LOGI("JNI: sexp_write_to_string returned - result_str=%p", result_str);

  if (!result_str || sexp_exceptionp(result_str)) {
    LOGE("JNI: Failed to convert result to string - result_str=%p exception=%d",
         result_str, result_str ? sexp_exceptionp(result_str) : -1);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Result conversion error");
  }

  LOGI("JNI: About to call sexp_string_data - result_str=%p", result_str);

  const char *result_cstr = sexp_string_data(result_str);

  LOGI("JNI: sexp_string_data returned - result_cstr=%p", result_cstr);

  if (!result_cstr) {
    LOGE("JNI: sexp_string_data returned NULL for valid result_str.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: String data extraction failed");
  }

  LOGI("JNI: Scheme result: %s", result_cstr);

  jstring java_result = (*env)->NewStringUTF(env, result_cstr);

  // Release mutex before returning
  pthread_mutex_unlock(&scheme_mutex);

  return java_result;
}