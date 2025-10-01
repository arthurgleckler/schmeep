#include "chibi/eval.h"
#include "port.c"

sexp sexp_init_library(sexp ctx, sexp self, sexp_sint_t n, sexp env, const char* version, const sexp_abi_identifier_t abi) {
  if (!(sexp_version_compatible(ctx, version, sexp_version)
        && sexp_abi_compatible(ctx, abi, SEXP_ABI_IDENTIFIER)))
    return SEXP_ABI_ERROR;

  sexp_define_foreign(ctx, env, "%make-custom-output-port", 3, sexp_make_custom_output_port);
  sexp_define_foreign(ctx, env, "%make-custom-input-port", 3, sexp_make_custom_input_port);
  sexp_define_foreign(ctx, env, "%make-custom-binary-output-port", 3, sexp_make_custom_binary_output_port);
  sexp_define_foreign(ctx, env, "%make-custom-binary-input-port", 3, sexp_make_custom_binary_input_port);

  return SEXP_VOID;
}
