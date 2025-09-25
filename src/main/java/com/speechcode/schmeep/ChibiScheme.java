package com.speechcode.schmeep;

import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

public class ChibiScheme {
    private static final String TAG = "schmeep";
    private MainActivity mainActivity;

    public native void cleanupScheme();
    public native String evaluateScheme(String expression);
    public native void initializeScheme();
    public native String interruptScheme();

    public ChibiScheme(MainActivity activity) {
	this.mainActivity = activity;

	try {
	    Assets.handleAssetExtraction(activity);
	    initializeScheme();
	    Log.i(TAG, "Chibi Scheme initialized successfully.");
	} catch (Exception e) {
	    Log.e(TAG, "Failed to initialize Chibi Scheme: " + e.getMessage());
	    cleanupScheme();
	    throw e;
	}
    }

    @JavascriptInterface
    public String eval(String expression) {
	Log.i(TAG, "Chibi Scheme: local evaluation: " + expression);
	return evaluateScheme(expression);
    }
}