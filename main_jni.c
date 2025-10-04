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

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "schmeep", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "schmeep", __VA_ARGS__)

#include "chibi/eval.h"
#include "chibi/sexp.h"

static jobject bluetooth_instance = NULL;
static jobject main_activity_instance = NULL;
static JavaVM *cached_jvm = NULL;
sexp scheme_ctx = NULL;
sexp scheme_env = NULL;
static pthread_mutex_t scheme_mutex = PTHREAD_MUTEX_INITIALIZER;

char *format_exception(sexp exception_obj, sexp ctx, const char *prefix,
		       const char *original_expression);

void bluetooth_output_write(const char *data, size_t length);
sexp bluetooth_port_writer(sexp ctx, sexp self, sexp_sint_t n, sexp str,
			   sexp start, sexp end);
sexp sexp_set_element_outer_html(sexp ctx, sexp self, sexp_sint_t n,
				 sexp selector, sexp html);

static bool attach_jni_env(JNIEnv **env, bool *detach_needed,
			   const char *caller) {
  if (!cached_jvm) {
    LOGE("%s: cached_jvm is NULL.", caller);
    return false;
  }

  int attach_status =
      (*cached_jvm)->GetEnv(cached_jvm, (void **)env, JNI_VERSION_1_6);

  *detach_needed = false;

  if (attach_status == JNI_EDETACHED) {
    if ((*cached_jvm)->AttachCurrentThread(cached_jvm, env, NULL) != 0) {
      LOGE("%s: Failed to attach thread.", caller);
      return false;
    }
    *detach_needed = true;
  }

  return true;
}

static void maybe_detach_jni_env(bool detach_needed) {
  if (detach_needed) {
    (*cached_jvm)->DetachCurrentThread(cached_jvm);
  }
}

void bluetooth_output_write(const char *data, size_t length) {
  LOGI("bluetooth_output_write called: bluetooth_instance=%p cached_jvm=%p "
       "length=%zu",
       bluetooth_instance, cached_jvm, length);

  JNIEnv *env;
  bool detach_needed;

  if (!attach_jni_env(&env, &detach_needed, "bluetooth_output_write")) {
    return;
  }

  if (bluetooth_instance) {
    jclass bluetooth_class = (*env)->GetObjectClass(env, bluetooth_instance);
    jmethodID streamPartialOutput = (*env)->GetMethodID(
	env, bluetooth_class, "streamPartialOutput", "(Ljava/lang/String;)V");

    if (streamPartialOutput) {
      jstring jdata = (*env)->NewStringUTF(env, data);

      (*env)->CallVoidMethod(env, bluetooth_instance, streamPartialOutput,
			     jdata);
      (*env)->DeleteLocalRef(env, jdata);
    } else {
      LOGE("bluetooth_output_write: Method streamPartialOutput not found.");
    }

    (*env)->DeleteLocalRef(env, bluetooth_class);
  }

  if (main_activity_instance) {
    jclass activity_class = (*env)->GetObjectClass(env, main_activity_instance);
    jmethodID displayCapturedOutput = (*env)->GetMethodID(
	env, activity_class, "displayCapturedOutput", "(Ljava/lang/String;)V");

    if (displayCapturedOutput) {
      jstring jdata = (*env)->NewStringUTF(env, data);

      (*env)->CallVoidMethod(env, main_activity_instance, displayCapturedOutput,
			     jdata);
      (*env)->DeleteLocalRef(env, jdata);
    } else {
      LOGE("bluetooth_output_write: Method displayCapturedOutput not found.");
    }

    (*env)->DeleteLocalRef(env, activity_class);
  }

  maybe_detach_jni_env(detach_needed);
}

sexp bluetooth_port_writer(sexp ctx, sexp self, sexp_sint_t n, sexp str,
			   sexp start, sexp end) {
  sexp_sint_t start_idx = sexp_unbox_fixnum(start);
  sexp_sint_t end_idx = sexp_unbox_fixnum(end);
  sexp_sint_t length = end_idx - start_idx;

  LOGI("bluetooth_port_writer called: start=%ld end=%ld length=%ld", start_idx,
       end_idx, length);

  if (length <= 0) {
    return sexp_make_fixnum(0);
  }

  const char *str_data = sexp_string_data(str);
  char *buffer = malloc(length + 1);

  if (!buffer) {
    LOGE("bluetooth_port_writer: malloc failed.");
    return sexp_make_fixnum(0);
  }

  memcpy(buffer, str_data + start_idx, length);
  buffer[length] = '\0';

  LOGI("bluetooth_port_writer: Calling bluetooth_output_write with \"%s\"",
       buffer);
  bluetooth_output_write(buffer, length);

  free(buffer);
  return sexp_make_fixnum(length);
}

sexp sexp_set_element_outer_html(sexp ctx, sexp self, sexp_sint_t n,
				 sexp selector, sexp html) {
  if (!sexp_stringp(selector)) {
    return sexp_user_exception(
	ctx, self, "set-element-outer-html!: Selector must be a string.",
	selector);
  }
  if (!sexp_stringp(html)) {
    return sexp_user_exception(
	ctx, self, "set-element-outer-html!: HTML must be a string.", html);
  }

  const char *selector_cstr = sexp_string_data(selector);
  const char *html_cstr = sexp_string_data(html);

  LOGI("set-element-outer-html! called: selector=%s", selector_cstr);

  if (!main_activity_instance) {
    LOGE("set-element-outer-html!: MainActivity instance not available.");
    return SEXP_VOID;
  }

  JNIEnv *env;
  bool detach_needed;
  if (!attach_jni_env(&env, &detach_needed, "set-element-outer-html!")) {
    return SEXP_VOID;
  }

  jclass activity_class = (*env)->GetObjectClass(env, main_activity_instance);
  jmethodID replaceElementHTML =
      (*env)->GetMethodID(env, activity_class, "replaceElementHTML",
			  "(Ljava/lang/String;Ljava/lang/String;)V");

  if (replaceElementHTML) {
    jstring jselector = (*env)->NewStringUTF(env, selector_cstr);
    jstring jhtml = (*env)->NewStringUTF(env, html_cstr);

    (*env)->CallVoidMethod(env, main_activity_instance, replaceElementHTML,
			   jselector, jhtml);

    (*env)->DeleteLocalRef(env, jselector);
    (*env)->DeleteLocalRef(env, jhtml);
  } else {
    LOGE("set-element-outer-html!: Method replaceElementHTML not found.");
  }

  (*env)->DeleteLocalRef(env, activity_class);

  maybe_detach_jni_env(detach_needed);

  return SEXP_VOID;
}

void cleanup_scheme() {
  if (scheme_ctx) {
    LOGI("cleanup_scheme: Destroying Scheme context.");
    sexp_destroy_context(scheme_ctx);
    scheme_ctx = NULL;
    scheme_env = NULL;
  }

  if (cached_jvm) {
    JNIEnv *env;
    int attach_status =
	(*cached_jvm)->GetEnv(cached_jvm, (void **)&env, JNI_VERSION_1_6);

    if (attach_status == JNI_OK) {
      if (bluetooth_instance) {
	(*env)->DeleteGlobalRef(env, bluetooth_instance);
	bluetooth_instance = NULL;
      }
      if (main_activity_instance) {
	(*env)->DeleteGlobalRef(env, main_activity_instance);
	main_activity_instance = NULL;
      }
    }
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
      sexp_c_string(scheme_ctx, "/data/data/com.speechcode.schmeep/lib", -1);

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
      "(current-module-path (cons \"/data/data/com.speechcode.schmeep/lib\" "
      "(current-module-path)))";

  sexp path_result =
      sexp_eval_string(scheme_ctx, set_path_expr, -1, scheme_env);

  if (path_result && !sexp_exceptionp(path_result)) {
    LOGI("init_scheme: Library search path configured.");
  } else {
    LOGE("init_scheme: Failed to set library search path.");
  }

  sexp_define_foreign(scheme_ctx, scheme_env, "set-element-outer-html!", 2,
		      sexp_set_element_outer_html);
  LOGI("init_scheme: Registered set-element-outer-html! native function.");

  sexp import_result = sexp_eval_string(
      scheme_ctx, "(import (schmeep exception-formatter))", -1, scheme_env);

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
    sexp call_args = sexp_list2(ctx, exception_obj, prefix_str);
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

JNIEXPORT void JNICALL Java_com_speechcode_schmeep_ChibiScheme_initializeScheme(
    JNIEnv *env, jobject object) {
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

JNIEXPORT jstring JNICALL
Java_com_speechcode_schmeep_ChibiScheme_interruptScheme(JNIEnv *env,
							jobject object) {
  LOGI("JNI: interruptScheme called.");

  if (scheme_ctx != NULL) {
    sexp child_ctx = sexp_context_child(scheme_ctx);
    if (child_ctx != NULL) {
      sexp_context_interruptp(child_ctx) = 1;
    }
  }
  return (*env)->NewStringUTF(env, "Interrupted.");
}

JNIEXPORT void JNICALL Java_com_speechcode_schmeep_ChibiScheme_cleanupScheme(
    JNIEnv *env, jobject object) {
  LOGI("JNI: cleanupScheme called.");
  cleanup_scheme();
}

JNIEXPORT jstring JNICALL
Java_com_speechcode_schmeep_ChibiScheme_evaluateScheme(JNIEnv *env,
						       jobject object,
						       jstring expression) {
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

  sexp old_output_port = sexp_current_output_port(scheme_ctx);
  sexp output_port = sexp_open_output_string(scheme_ctx);
  sexp param_symbol = sexp_global(scheme_ctx, SEXP_G_CUR_OUT_SYMBOL);

  sexp_set_parameter(scheme_ctx, scheme_env, param_symbol, output_port);
  sexp_gc_var3(input_port, expr_obj, result);
  sexp_gc_preserve3(scheme_ctx, input_port, expr_obj, result);

  sexp expr_string = sexp_c_string(scheme_ctx, expr_cstr, -1);

  input_port = sexp_open_input_string(scheme_ctx, expr_string);
  result = SEXP_VOID;
  if (sexp_exceptionp(input_port)) {
    result = input_port;
  } else {
    while ((expr_obj = sexp_read(scheme_ctx, input_port)) != SEXP_EOF) {
      if (sexp_exceptionp(expr_obj)) {
	result = expr_obj;
	break;
      }
      result = sexp_eval(scheme_ctx, expr_obj, scheme_env);
      if (sexp_exceptionp(result)) {
	break;
      }
    }
    sexp_close_port(scheme_ctx, input_port);
  }
  sexp_gc_release3(scheme_ctx);

  sexp output_str = sexp_get_output_string(scheme_ctx, output_port);
  const char *captured_output = NULL;

  if (output_str && sexp_stringp(output_str)) {
    captured_output = sexp_string_data(output_str);

    size_t length = captured_output ? strlen(captured_output) : 0;

    if (captured_output && length > 0) {
      LOGI("JNI: Sending captured output to Bluetooth");
      bluetooth_output_write(captured_output, length);
    }
  }

  sexp_set_parameter(scheme_ctx, scheme_env, param_symbol, old_output_port);

  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  if (!result) {
    LOGE("JNI: Failed to evaluate Scheme expression.");
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Unknown evaluation error.");
  }

  if (sexp_exceptionp(result)) {
    if (result == sexp_global(scheme_ctx, SEXP_G_INTERRUPT_ERROR)) {
      LOGI("JNI: Interrupt error detected - evaluation was interrupted "
	   "successfully.");
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
    LOGE(
	"JNI: Failed to convert result to string - result_str=%p exception=%d.",
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

JNIEXPORT jboolean JNICALL
Java_com_speechcode_schmeep_ChibiScheme_isCompleteExpression(
    JNIEnv *env, jobject object, jstring expression) {
  LOGI("JNI: isCompleteExpression called.");

  pthread_mutex_lock(&scheme_mutex);
  if (scheme_ctx == NULL || scheme_env == NULL) {
    LOGE("JNI: Scheme not initialized - ctx=%p env=%p", scheme_ctx, scheme_env);
    pthread_mutex_unlock(&scheme_mutex);
    return JNI_FALSE;
  }

  const char *expr_cstr = (*env)->GetStringUTFChars(env, expression, NULL);

  if (!expr_cstr) {
    LOGE("JNI: Failed to convert expression string.");
    pthread_mutex_unlock(&scheme_mutex);
    return JNI_FALSE;
  }

  sexp_gc_var2(input_port, expr_string);
  sexp_gc_preserve2(scheme_ctx, input_port, expr_string);

  expr_string = sexp_c_string(scheme_ctx, expr_cstr, -1);
  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  input_port = sexp_open_input_string(scheme_ctx, expr_string);

  if (sexp_exceptionp(input_port)) {
    LOGE("JNI: Failed to create input port.");
    sexp_gc_release2(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return JNI_FALSE;
  }

  sexp expr_obj = sexp_read(scheme_ctx, input_port);

  while (expr_obj != SEXP_EOF && !sexp_exceptionp(expr_obj)) {
    expr_obj = sexp_read(scheme_ctx, input_port);
  }

  sexp_close_port(scheme_ctx, input_port);

  jboolean result;

  if (sexp_exceptionp(expr_obj)) {
    sexp kind = sexp_exception_kind(expr_obj);
    sexp incomplete_symbol = sexp_intern(scheme_ctx, "read-incomplete", -1);

    if (kind == incomplete_symbol) {
      LOGI("JNI: Expression is incomplete.");
      result = JNI_FALSE;
    } else {
      LOGI("JNI: Expression is malformed (not incomplete).");
      result = JNI_TRUE;
    }
  } else {
    LOGI("JNI: Expression is complete.");
    result = JNI_TRUE;
  }

  sexp_gc_release2(scheme_ctx);
  pthread_mutex_unlock(&scheme_mutex);
  return result;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  cached_jvm = vm;
  LOGI("JNI: Library loaded.  JavaVM cached.");
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  LOGI("JNI: Library unloading.  Cleaning up Scheme context.");
  cleanup_scheme();
}

JNIEXPORT void JNICALL
Java_com_speechcode_schmeep_Bluetooth_setNativeOutputCallback(JNIEnv *env,
							      jobject object) {
  LOGI("JNI: setNativeOutputCallback called.");

  if (bluetooth_instance) {
    (*env)->DeleteGlobalRef(env, bluetooth_instance);
  }

  bluetooth_instance = (*env)->NewGlobalRef(env, object);

  if (bluetooth_instance) {
    LOGI("JNI: Bluetooth instance registered for output capture.");
  } else {
    LOGE("JNI: Failed to create global reference to Bluetooth instance.");
  }
}

JNIEXPORT void JNICALL
Java_com_speechcode_schmeep_MainActivity_registerForOutputCapture(
    JNIEnv *env, jobject object) {
  LOGI("JNI: registerForOutputCapture called.");

  if (main_activity_instance) {
    (*env)->DeleteGlobalRef(env, main_activity_instance);
  }

  main_activity_instance = (*env)->NewGlobalRef(env, object);

  if (main_activity_instance) {
    LOGI("JNI: MainActivity instance registered for output capture.");
  } else {
    LOGE("JNI: Failed to create global reference to MainActivity instance.");
  }
}
