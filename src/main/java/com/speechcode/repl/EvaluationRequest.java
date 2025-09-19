package com.speechcode.repl;

import java.io.OutputStream;

public class EvaluationRequest {
    final String expression;
    final OutputStream responseStream;

    EvaluationRequest(String expression, OutputStream responseStream) {
        this.expression = expression;
        this.responseStream = responseStream;
    }
}