#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <android/log.h>
#include <jni.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include <ctype.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "repl", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "repl", __VA_ARGS__)

#include "chibi/eval.h"
#include "chibi/sexp.h"

sexp scheme_ctx = NULL;
sexp scheme_env = NULL;
static pthread_mutex_t scheme_mutex = PTHREAD_MUTEX_INITIALIZER;

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

void log_scheme_exception_details(sexp exception_obj, sexp ctx, const char* prefix, const char* original_expression) {
  if (!exception_obj || !sexp_exceptionp(exception_obj)) {
    LOGE("%s: Not a valid exception object", prefix);
    return;
  }

  LOGE("%s: Exception object type: %d", prefix, sexp_type_tag(exception_obj));

  sexp msg = sexp_exception_message(exception_obj);
  sexp kind = sexp_exception_kind(exception_obj);
  sexp irritants = sexp_exception_irritants(exception_obj);
  sexp source = sexp_exception_source(exception_obj);

  if (msg && sexp_stringp(msg)) {
    LOGE("%s: Error message: %s", prefix, sexp_string_data(msg));
  } else {
    LOGE("%s: Error message is NULL or not a string (msg=%p)", prefix, msg);
  }

  if (kind && sexp_symbolp(kind)) {
    sexp kind_str = sexp_symbol_to_string(ctx, kind);
    if (kind_str && sexp_stringp(kind_str)) {
      LOGE("%s: Error kind: %s", prefix, sexp_string_data(kind_str));
    }
  } else {
    LOGE("%s: Error kind is NULL or not a symbol (kind=%p)", prefix, kind);
  }

  if (irritants) {
    LOGE("%s: Error irritants present (irritants=%p)", prefix, irritants);
    if (sexp_pairp(irritants)) {
      sexp first_irritant = sexp_car(irritants);
      if (first_irritant && sexp_stringp(first_irritant)) {
        LOGE("%s: First irritant string: %s", prefix, sexp_string_data(first_irritant));
      } else {
        LOGE("%s: First irritant: %p type=%d", prefix, first_irritant,
             first_irritant ? sexp_type_tag(first_irritant) : -1);
      }
    }
  }

  if (source) {
    LOGE("%s: Error source present (source=%p) type=%d", prefix, source, sexp_type_tag(source));
  }

  if (msg && sexp_stringp(msg)) {
    const char* error_msg = sexp_string_data(msg);

    if (strstr(error_msg, "dotted list")) {
      LOGE("%s: MEMORY CORRUPTION DETECTED - Dotted list error indicates corrupted input", prefix);
      if (original_expression) {
        LOGE("%s: Original expression was: %s", prefix, original_expression);

        LOGE("%s: Expression string pointer: %p", prefix, original_expression);
        int len = strlen(original_expression);

        LOGE("%s: Expression length: %d", prefix, len);
        for (int i = 0; i < len && i < 32; i++) {
          LOGE("%s: expr[%d] = 0x%02x ('%c')", prefix, i, (unsigned char)original_expression[i],
               isprint(original_expression[i]) ? original_expression[i] : '?');
        }
      }
    }
  }
}

void log_scheme_stack_context_info(sexp ctx, sexp env, const char* prefix) {
  sexp trace = sexp_global(ctx, SEXP_G_ERR_HANDLER);

  if (trace) {
    LOGE("%s: Error handler present (trace=%p)", prefix, trace);
  }

  sexp stack = sexp_global(ctx, SEXP_STACK);

  if (stack) {
    LOGE("%s: Stack object present (stack=%p)", prefix, stack);
    if (sexp_vectorp(stack)) {
      int stack_depth = sexp_vector_length(stack);
      LOGE("%s: Stack depth: %d", prefix, stack_depth);

      for (int i = 0; i < stack_depth && i < 10; i++) {
        sexp frame = sexp_vector_ref(stack, sexp_make_fixnum(i));
        if (frame) {
          LOGE("%s: Stack frame %d: %p", prefix, i, frame);

          if (sexp_vectorp(frame)) {
            int frame_size = sexp_vector_length(frame);

            LOGE("%s: Frame %d size: %d", prefix, i, frame_size);

            for (int j = 0; j < frame_size && j < 5; j++) {
              sexp element = sexp_vector_ref(frame, sexp_make_fixnum(j));
              if (element) {
                LOGE("%s: Frame %d[%d]: %p type=%d", prefix, i, j, element, sexp_type_tag(element));

                if (sexp_procedurep(element)) {
                  LOGE("%s: Frame %d[%d] is a procedure", prefix, i, j);
                } else if (sexp_symbolp(element)) {
                  sexp name_str = sexp_symbol_to_string(ctx, element);
                  if (name_str && sexp_stringp(name_str)) {
                    LOGE("%s: Frame %d[%d] symbol: %s", prefix, i, j, sexp_string_data(name_str));
                  }
                }
              }
            }
          } else if (sexp_pairp(frame)) {
            LOGE("%s: Frame %d is a pair", prefix, i);
            sexp car = sexp_car(frame);
            sexp cdr = sexp_cdr(frame);

            if (car) LOGE("%s: Frame %d car: %p type=%d", prefix, i, car, sexp_type_tag(car));
            if (cdr) LOGE("%s: Frame %d cdr: %p type=%d", prefix, i, cdr, sexp_type_tag(cdr));
          }
        }
      }
    } else {
      LOGE("%s: Stack is not a vector, type=%d", prefix, sexp_type_tag(stack));
    }

    sexp ctx_stack = sexp_context_stack(ctx);

    if (ctx_stack) {
      LOGE("%s: Context stack present (ctx_stack=%p)", prefix, ctx_stack);
      if (sexp_vectorp(ctx_stack)) {
        int ctx_depth = sexp_vector_length(ctx_stack);
        LOGE("%s: Context stack depth: %d", prefix, ctx_depth);
      }
    }

    sexp current_env = env;
    int env_depth = 0;

    while (current_env && env_depth < 10) {
      LOGE("%s: Environment level %d: %p type=%d", prefix, env_depth, current_env, sexp_type_tag(current_env));
      if (sexp_envp(current_env)) {
        sexp parent = sexp_env_parent(current_env);
        if (parent == current_env) break; // Avoid infinite loop
        current_env = parent;
      } else {
        break;
      }
      env_depth++;
    }
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

  sexp_gc_var1(code_sexp);
  sexp_gc_preserve1(scheme_ctx, code_sexp);


  code_sexp = sexp_read_from_string(scheme_ctx, expr_cstr, -1);

  LOGI("JNI: sexp_read_from_string returned - code_sexp=%p", code_sexp);

  if (!code_sexp || sexp_exceptionp(code_sexp)) {
    LOGE("JNI: Failed to parse Scheme expression - code_sexp=%p exception=%d",
         code_sexp, code_sexp ? sexp_exceptionp(code_sexp) : -1);
    LOGE("JNI: INPUT EXPRESSION WAS: '%s'", expr_cstr);

    if (code_sexp && sexp_exceptionp(code_sexp)) {
      log_scheme_exception_details(code_sexp, scheme_ctx, "JNI Parse", expr_cstr);
    }

    (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Evaluation error");
  }

  (*env)->ReleaseStringUTFChars(env, expression, expr_cstr);

  LOGI("JNI: About to call sexp_eval - ctx=%p code_sexp=%p env=%p", scheme_ctx, code_sexp, scheme_env);


  sexp result = sexp_eval(scheme_ctx, code_sexp, scheme_env);


  LOGI("JNI: sexp_eval returned - result=%p", result);


  if (!result || sexp_exceptionp(result)) {
    LOGE("JNI: Failed to evaluate Scheme expression.");
    LOGE("JNI: INPUT EXPRESSION WAS: '%s'", expr_cstr);
    if (result && sexp_exceptionp(result)) {
      log_scheme_exception_details(result, scheme_ctx, "JNI Eval", expr_cstr);

      log_scheme_stack_context_info(scheme_ctx, scheme_env, "JNI Eval");

      sexp proc = sexp_exception_procedure(result);

      if (proc) {
        LOGE("JNI Eval: Error procedure present (proc=%p)", proc);
        if (sexp_procedurep(proc)) {
          LOGE("JNI Eval: Error procedure is a procedure object");
        }
      }
    } else {
      LOGE("JNI: Result is NULL or not an exception (result=%p)", result);
    }
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Evaluation error");
  }

  LOGI("JNI: About to call sexp_write_to_string - ctx=%p result=%p", scheme_ctx, result);

  sexp result_str = sexp_write_to_string(scheme_ctx, result);


  LOGI("JNI: sexp_write_to_string returned - result_str=%p", result_str);

  if (!result_str || sexp_exceptionp(result_str)) {
    LOGE("JNI: Failed to convert result to string - result_str=%p exception=%d",
         result_str, result_str ? sexp_exceptionp(result_str) : -1);
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: Result conversion error");
  }

  LOGI("JNI: About to call sexp_string_data - result_str=%p", result_str);

  const char *result_cstr = sexp_string_data(result_str);


  LOGI("JNI: sexp_string_data returned - result_cstr=%p", result_cstr);

  if (!result_cstr) {
    LOGE("JNI: sexp_string_data returned NULL for valid result_str.");
    sexp_gc_release1(scheme_ctx);
    pthread_mutex_unlock(&scheme_mutex);
    return (*env)->NewStringUTF(env, "Error: String data extraction failed");
  }

  LOGI("JNI: Scheme result: %s", result_cstr);

  jstring java_result = (*env)->NewStringUTF(env, result_cstr);


  sexp_gc_release1(scheme_ctx);
  pthread_mutex_unlock(&scheme_mutex);

  return java_result;
}