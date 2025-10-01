package com.speechcode.schmeep;

import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

public class ChibiScheme {
    private static final String LOG_TAG = "schmeep";

    private MainActivity mainActivity;
    private WebView webView;

    public native void cleanupScheme();
    public native String evaluateScheme(String expression);
    public native void initializeScheme();
    public native String interruptScheme();

    public ChibiScheme(MainActivity activity) {
	this.mainActivity = activity;

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

    public void setWebView(WebView webView) {
	this.webView = webView;
    }

    public void displayCapturedOutput(String output) {
	if (webView == null) {
	    return;
	}

	mainActivity.runOnUiThread(() -> {
	    String escapedOutput = output.replace("\\", "\\\\")
					 .replace("\"", "\\\"")
					 .replace("\n", "\\n")
					 .replace("\r", "\\r")
					 .replace("\t", "\\t");
	    String jsCode = "if (window.displayCapturedOutput) { " +
			    "window.displayCapturedOutput(\"" + escapedOutput +
			    "\"); }";

	    webView.evaluateJavascript(jsCode, null);
	});
    }

    @JavascriptInterface
    public String eval(String expression) {
	Log.i(LOG_TAG, "Chibi Scheme: local evaluation: " + expression);
	return evaluateScheme(expression);
    }
}