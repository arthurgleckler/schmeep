package com.speechcode.schmeep;

import android.util.Log;
import android.webkit.JavascriptInterface;

public class ChibiScheme {
    private static final String LOG_TAG = "schmeep";

    public native void cleanupScheme();
    public native String evaluateScheme(String expression);
    public native void initializeScheme();
    public native String interruptScheme();

    public ChibiScheme(MainActivity activity) {
	try {
	    Assets.handleAssetExtraction(activity);
	    initializeScheme();
	    Log.i(LOG_TAG, "Chibi Scheme initialized successfully.");
	} catch (Exception e) {
	    Log.e(LOG_TAG,
		  "Failed to initialize Chibi Scheme: " + e.getMessage());
	    cleanupScheme();
	    throw e;
	}
    }

    @JavascriptInterface
    public String eval(String expression) {
	Log.i(LOG_TAG, "Chibi Scheme: local evaluation: " + expression);
	return evaluateScheme(expression);
    }
}