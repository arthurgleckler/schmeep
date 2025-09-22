#include <android/log.h>
#include <ctype.h>
#include <execinfo.h>
#include <jni.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "repl", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "repl", __VA_ARGS__)

#include "chibi/eval.h"
#include "chibi/sexp.h"

sexp scheme_ctx = NULL;
sexp scheme_env = NULL;
static pthread_mutex_t scheme_mutex = PTHREAD_MUTEX_INITIALIZER;

char *format_exception(sexp exception_obj, sexp ctx, const char *prefix,
			   const char *original_expression);

void cleanup_scheme() {
  if (scheme_ctx) {
    LOGI("cleanup_scheme: Destroying Scheme context.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
  }
}

void crash_handler(int sig, siginfo_t *info, void *context) {
  LOGE("JNI: CRASH DETECTED - Signal %d at address %p", sig, info->si_addr);
  LOGE("JNI: Crash occurred in PID %d, TID %d", getpid(), gettid());

  if (scheme_ctx) {
    LOGE("JNI: Scheme context available at crash: %p", scheme_ctx);

    sexp stack = sexp_global(scheme_ctx, SEXP_STACK);

    if (stack) {
      LOGE("JNI: Scheme stack at crash: %p", stack);
      if (sexp_vectorp(stack)) {
	int stack_depth = sexp_vector_length(stack);

	LOGE("JNI: Scheme stack depth at crash: %d", stack_depth);
      }
    }
  } else {
    LOGE("JNI: No Scheme context available at crash");
  }

  void *buffer[16];
  int count = backtrace(buffer, 16);

  LOGE("JNI: Native backtrace has %d frames", count);

  char **symbols = backtrace_symbols(buffer, count);

  if (symbols) {
    for (int i = 0; i < count; i++) {
      LOGE("JNI: Frame %d: %s", i, symbols[i]);
    }
    free(symbols);
  }

  LOGE("JNI: Crash analysis complete - terminating");

  signal(sig, SIG_DFL);
  raise(sig);
}

int init_scheme() {
  LOGI("init_scheme: Starting Scheme initialization.");
  sexp_scheme_init();
  scheme_ctx =
      sexp_make_eval_context(NULL, NULL, NULL, 1024 * 1024, 8 * 1024 * 1024);
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

  sexp module_path_string =
      sexp_c_string(scheme_ctx, "/data/data/com.speechcode.repl/lib", -1);

  sexp_global(scheme_ctx, SEXP_G_MODULE_PATH) =
      sexp_list1(scheme_ctx, module_path_string);
  sexp_load_standard_ports(scheme_ctx, scheme_env, stdin, stdout, stderr, 1);

  sexp std_env = sexp_load_standard_env(scheme_ctx, scheme_env, SEXP_SEVEN);

  if (sexp_exceptionp(std_env)) {
    LOGE("init_scheme: Failed to load R7RS standard environment.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
    return -1;
  } else {
    LOGI("init_scheme: R7RS environment loaded successfully.");
    scheme_env = std_env;
  }

  const char *set_path_expr =
      "(current-module-path (cons \"/data/data/com.speechcode.repl/lib\" "
      "(current-module-path)))";

  sexp path_result = sexp_eval_string(scheme_ctx, set_path_expr, -1, scheme_env);

  if (path_result && !sexp_exceptionp(path_result)) {
    LOGI("init_scheme: Library search path configured.");
  } else {
    LOGE("init_scheme: Failed to set library search path.");
  }

  sexp import_result
    = sexp_eval_string(scheme_ctx, "(import (chb exception-formatter))", -1,
		       scheme_env);

  if (import_result && !sexp_exceptionp(import_result)) {
    LOGI("init_scheme: Exception formatter imported.");
  } else {
    LOGE("init_scheme: Failed to import exception formatter.");
  }
  LOGI("init_scheme: Scheme context initialized successfully.");
  return 0;
}

char *format_exception(sexp exception_obj, sexp ctx, const char *prefix,
		       const char *original_expression) {
  static _Thread_local char error_message[2048];

  if (!ctx || !scheme_env) {
    return "Error: Scheme not available.";
  }

  sexp formatter_symbol = sexp_intern(ctx, "format-exception", -1);
  sexp formatter = sexp_env_ref(ctx, scheme_env, formatter_symbol, SEXP_FALSE);

  if (formatter && sexp_procedurep(formatter)) {
    sexp prefix_str = sexp_c_string(ctx, prefix, -1);
    sexp original_str = original_expression ? sexp_c_string(ctx, original_expression, -1) : SEXP_FALSE;
    sexp call_args = sexp_list3(ctx, exception_obj, prefix_str, original_str);
    sexp result = sexp_apply(ctx, formatter, call_args);

    if (!sexp_exceptionp(result) && result && sexp_stringp(result)) {
      const char *result_str = sexp_string_data(result);

      if (result_str) {
	strncpy(error_message, result_str, sizeof(error_message) - 1);
	error_message[sizeof(error_message) - 1] = '\0';
	return error_message;
      }
    }
  }
  return "Error: Scheme formatter failed.";
}

JNIEXPORT void JNICALL Java_com_speechcode_repl_MainActivity_initializeScheme(
    JNIEnv *env, jobject thiz) {
  LOGI("JNI: initializeScheme called.");

  struct sigaction sa;

  sa.sa_sigaction = crash_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGSEGV, &sa, NULL) == 0) {
    LOGI("JNI: SIGSEGV signal handler installed successfully");
  } else {
    LOGE("JNI: Failed to install SIGSEGV signal handler");
  }

  if (sigaction(SIGABRT, &sa, NULL) == 0) {
    LOGI("JNI: SIGABRT signal handler installed successfully");
  } else {
    LOGE("JNI: Failed to install SIGABRT signal handler");
  }

  pthread_mutex_lock(&scheme_mutex);

  if (scheme_ctx == NULL) {
    LOGI("JNI: Initializing Chibi Scheme.");

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

JNIEXPORT jstring JNICALL Java_com_speechcode_repl_MainActivity_interruptScheme(
    JNIEnv *env, jobject thiz) {
  LOGI("JNI: interruptScheme called.");

  sexp thread = scheme_ctx;

  for (; thread && sexp_contextp(thread); thread = sexp_context_child(thread)) {
    sexp_context_interruptp(thread) = 1;
  }

  return (*env)->NewStringUTF(env, "Interrupted");
}

JNIEXPORT jstring JNICALL Java_com_speechcode_repl_MainActivity_evaluateScheme(
    JNIEnv *env, jobject thiz, jstring expression) {
  LOGI("JNI: evaluateScheme called.");

  pthread_mutex_lock(&scheme_mutex);
  if (scheme_ctx == NULL || scheme_env == NULL) {
    LOGE("JNI: Scheme not initialized - ctx=%p env=%p", scheme_ctx, scheme_env);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Scheme not initialized.");
  }

  const char *expr_cstr = (*env)->GetStringUTFChars(env, expression, NULL);

  if (!expr_cstr) {
    LOGE("JNI: Failed to convert expression string.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Invalid expression string.");
  }

  sexp result = sexp_eval_string(scheme_ctx, expr_cstr, -1, scheme_env);

  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  if (!result) {
    LOGE("JNI: Failed to evaluate Scheme expression.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Unknown evaluation error.");
  }

  if (sexp_exceptionp(result)) {
    if (result == sexp_global(scheme_ctx, SEXP_G_INTERRUPT_ERROR)) {
      LOGI("JNI: Interrupt error detected - evaluation was interrupted successfully.");
      pthread_mutex_unlock(&scheme_mutex);
      return (*env)->NewStringUTF(env, "Interrupted.");
    }

    char *error_msg = format_exception(result, scheme_ctx, "JNI", expr_cstr);

    LOGE("JNI: %s", error_msg);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, error_msg);
  }

  sexp result_str = sexp_write_to_string(scheme_ctx, result);

  if (!result_str || sexp_exceptionp(result_str)) {
    LOGE("JNI: Failed to convert result to string - result_str=%p exception=%d.",
	 result_str, result_str ? sexp_exceptionp(result_str) : -1);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Result conversion error.");
  }

  const char *result_cstr = sexp_string_data(result_str);

  if (!result_cstr) {
    LOGE("JNI: sexp_string_data returned NULL for valid result_str.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: String data extraction failed.");
  }

  LOGI("JNI: Scheme result: %s", result_cstr);

  jstring java_result = (*env)->NewStringUTF(env, result_cstr);

  pthread_mutex_unlock(&scheme_mutex);
  return java_result;
}
