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

char *safe_sprintf_alloc(const char *format, ...) {
  va_list args1, args2;

  va_start(args1, format);
  va_copy(args2, args1);

  int size = vsnprintf(NULL, 0, format, args1) + 1;

  va_end(args1);

  char *buffer = malloc(size);

  if (!buffer) {
    va_end(args2);
    return NULL;
  }

  vsnprintf(buffer, size, format, args2);
  va_end(args2);

  return buffer;
}

char *log_scheme_exception(sexp exception_obj, sexp ctx, const char *prefix,
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
    log_scheme_exception(std_env, scheme_ctx, "init_scheme", NULL);

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

char *log_scheme_exception(sexp exception_obj, sexp ctx, const char *prefix,
			   const char *original_expression) {
  static _Thread_local char error_message[2048];
  int pos = 0;

  if (!exception_obj || !sexp_exceptionp(exception_obj)) {
    LOGE("%s: Not a valid exception object", prefix);
    snprintf(error_message, sizeof(error_message),
	     "Error: Invalid exception object.");
    return error_message;
  }

  LOGE("%s: Exception object type: %d", prefix, sexp_type_tag(exception_obj));

  sexp msg = sexp_exception_message(exception_obj);
  sexp kind = sexp_exception_kind(exception_obj);
  sexp irritants = sexp_exception_irritants(exception_obj);
  sexp source = sexp_exception_source(exception_obj);

  pos = snprintf(error_message, sizeof(error_message), "Error: ");

  if (msg && sexp_stringp(msg)) {
    const char *msg_data = sexp_string_data(msg);

    if (msg_data && strlen(msg_data) > 0 && strlen(msg_data) < 200) {
      pos += snprintf(error_message + pos, sizeof(error_message) - pos, "%s",
		      msg_data);
    } else {
      pos += snprintf(error_message + pos, sizeof(error_message) - pos,
		      "Invalid error message.");
    }
    LOGE("%s: Error message: %s", prefix, sexp_string_data(msg));
  } else {
    pos += snprintf(error_message + pos, sizeof(error_message) - pos,
		    "Unknown error.");
    LOGE("%s: Error message is NULL or not a string (msg=%p).", prefix, msg);
  }

  if (kind && sexp_symbolp(kind)) {
    sexp kind_str = sexp_symbol_to_string(ctx, kind);

    if (kind_str && sexp_stringp(kind_str)) {
      LOGE("%s: Error kind: %s", prefix, sexp_string_data(kind_str));
    }
  } else {
    LOGE("%s: Error kind is NULL or not a symbol (kind=%p).", prefix, kind);
  }

  if (irritants) {
    LOGE("%s: Error irritants present (irritants=%p).", prefix, irritants);
    if (sexp_pairp(irritants)) {
      sexp first_irritant = sexp_car(irritants);

      if (first_irritant && sexp_stringp(first_irritant)) {
	LOGE("%s: First irritant string: %s.", prefix,
	     sexp_string_data(first_irritant));
      } else {
	LOGE("%s: First irritant: %p type=%d.", prefix, first_irritant,
	     first_irritant ? sexp_type_tag(first_irritant) : -1);
      }
    }
  }

  if (source) {
    LOGE("%s: Error source present (source=%p) type=%d.", prefix, source,
	 sexp_type_tag(source));
  }

  if (msg && sexp_stringp(msg)) {
    const char *error_msg = sexp_string_data(msg);

    if (strstr(error_msg, "dotted list")) {
      LOGE("%s: MEMORY CORRUPTION DETECTED - Dotted list error indicates "
	   "corrupted input.",
	   prefix);
      if (original_expression) {
	LOGE("%s: Original expression was: %s.", prefix, original_expression);

	LOGE("%s: Expression string pointer: %p.", prefix, original_expression);

	int len = strlen(original_expression);

	LOGE("%s: Expression length: %d.", prefix, len);

	for (int i = 0; i < len && i < 32; i++) {
	  LOGE("%s: expr[%d] = 0x%02x ('%c')", prefix, i,
	       (unsigned char)original_expression[i],
	       isprint(original_expression[i]) ? original_expression[i] : '?');
	}
      }
    }
  }

  error_message[sizeof(error_message) - 1] = '\0';
  return error_message;
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

static int in_user_evaluation = 0;

__attribute__((visibility("default"))) int is_in_user_evaluation() {
  return in_user_evaluation;
}

JNIEXPORT jstring JNICALL Java_com_speechcode_repl_MainActivity_evaluateScheme(
    JNIEnv *env, jobject thiz, jstring expression) {
  LOGI("JNI: evaluateScheme called.");

  pthread_mutex_lock(&scheme_mutex);

  in_user_evaluation = 1;

  if (scheme_ctx == NULL || scheme_env == NULL) {
    LOGE("JNI: Scheme not initialized - ctx=%p env=%p", scheme_ctx, scheme_env);
    in_user_evaluation = 0;
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Scheme not initialized");
  }

  const char *expr_cstr = (*env)->GetStringUTFChars(env, expression, NULL);

  if (!expr_cstr) {
    LOGE("JNI: Failed to convert expression string.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Invalid expression string");
  }

  sexp_gc_var1(code_sexp);
  sexp_gc_preserve1(scheme_ctx, code_sexp);

  code_sexp = sexp_read_from_string(scheme_ctx, expr_cstr, -1);

  LOGI("JNI: sexp_read_from_string returned - code_sexp=%p", code_sexp);

  if (!code_sexp || sexp_exceptionp(code_sexp)) {
    LOGE("JNI: Failed to parse Scheme expression - code_sexp=%p exception=%d",
	 code_sexp, code_sexp ? sexp_exceptionp(code_sexp) : -1);
    LOGE("JNI: INPUT EXPRESSION WAS: '%s'", expr_cstr);

    char *error_msg;

    if (code_sexp && sexp_exceptionp(code_sexp)) {
      error_msg =
	  log_scheme_exception(code_sexp, scheme_ctx, "JNI Parse", expr_cstr);
    } else {
      error_msg = "Parse Error: Failed to parse expression";
    }

    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);
    return (*env)->NewStringUTF(env, error_msg);
  }

  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  LOGI("JNI: About to call sexp_eval - ctx=%p code_sexp=%p env=%p", scheme_ctx,
       code_sexp, scheme_env);

  sexp result = sexp_eval(scheme_ctx, code_sexp, scheme_env);

  LOGI("JNI: sexp_eval returned - result=%p", result);

  if (!result || sexp_exceptionp(result)) {
    if (result && sexp_exceptionp(result)) {
      sexp interrupt_error = sexp_global(scheme_ctx, SEXP_G_INTERRUPT_ERROR);

      if (result == interrupt_error) {
	LOGI("JNI: Interrupt error detected - evaluation was interrupted "
	     "successfully");
	pthread_mutex_unlock(&scheme_mutex);
	return (*env)->NewStringUTF(env, "Interrupted");
      }
    }

    LOGE("JNI: Failed to evaluate Scheme expression.");
    LOGE("JNI: INPUT EXPRESSION WAS: '%s'", expr_cstr);

    char *error_msg;

    if (result && sexp_exceptionp(result)) {
      error_msg =
	  log_scheme_exception(result, scheme_ctx, "JNI Eval", expr_cstr);
    } else {
      error_msg = "Error: Unknown evaluation error";
    }

    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, error_msg);
  }

  sexp result_str = sexp_write_to_string(scheme_ctx, result);

  if (!result_str || sexp_exceptionp(result_str)) {
    LOGE("JNI: Failed to convert result to string - result_str=%p exception=%d",
	 result_str, result_str ? sexp_exceptionp(result_str) : -1);
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Result conversion error");
  }

  const char *result_cstr = sexp_string_data(result_str);

  if (!result_cstr) {
    LOGE("JNI: sexp_string_data returned NULL for valid result_str.");
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: String data extraction failed");
  }

  LOGI("JNI: Scheme result: %s", result_cstr);

  jstring java_result = (*env)->NewStringUTF(env, result_cstr);

  sexp_gc_release1(scheme_ctx);
  in_user_evaluation = 0;
  pthread_mutex_unlock(&scheme_mutex);

  return java_result;
}
