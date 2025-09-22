package com.speechcode.repl;

import android.util.Log;
import android.webkit.JavascriptInterface;

public class ChibiScheme {
    private static final String TAG = "repl";
    private MainActivity mainActivity;

    public ChibiScheme(MainActivity activity) {
	this.mainActivity = activity;
    }

    @JavascriptInterface
    public String eval(String expression) {
	Log.i(TAG, "JavaScript Interface: Local evaluation: " + expression);
	return mainActivity.evaluateScheme(expression);
    }
}