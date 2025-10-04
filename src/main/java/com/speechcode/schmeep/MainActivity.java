package com.speechcode.schmeep;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;

public class MainActivity extends Activity {
    private static final String LOG_TAG = "schmeep";

    private Bluetooth bluetooth;
    private ChibiScheme chibiScheme;
    private WebView webView;

    static { System.loadLibrary("schmeep"); }

    private native void registerForOutputCapture();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
	super.onCreate(savedInstanceState);
	setTitle("Schmeep: Chibi Scheme REPL");
	Log.i(LOG_TAG, "MainActivity onCreate started.");

	registerForOutputCapture();
	chibiScheme = new ChibiScheme(this);
	setupWebView();
	Log.i(LOG_TAG, "MainActivity onCreate completed.");
    }

    @SuppressLint({"AddJavascriptInterface", "SetJavaScriptEnabled"})
    private void setupWebView() {
	webView = new WebView(this);

	WebSettings webSettings = webView.getSettings();

	webSettings.setJavaScriptEnabled(true);
	webSettings.setDomStorageEnabled(true);
	webSettings.setAllowFileAccess(true);
	webSettings.setAllowContentAccess(true);
	try {
	    Log.i(LOG_TAG, "Direct JNI test result: " +
			       chibiScheme.evaluateScheme("(+ 2 3)"));
	    Log.i(LOG_TAG, "Direct JNI test result: " +
			       chibiScheme.evaluateScheme("(* 4 5)"));
	    Log.i(LOG_TAG, "Direct JNI test result: " +
			       chibiScheme.evaluateScheme("(list 1 2 3)"));
	} catch (Exception e) {
	    Log.e(LOG_TAG, "JNI test failed: " + e.getMessage());
	}
	setContentView(webView);
	try {
	    webView.addJavascriptInterface(chibiScheme, "Scheme");
	    webView.addJavascriptInterface(this, "Display");
	    Log.i(LOG_TAG, "JavaScript interface added successfully.");
	} catch (Exception e) {
	    Log.e(LOG_TAG,
		  "Failed to add JavaScript interface: " + e.getMessage());
	}
	webView.setWebChromeClient(new DebugWebChromeClient());
	webView.setWebViewClient(new PageLoadedWebViewClient(this));
	webView.loadUrl("file:///android_asset/index.html");
	Log.i(LOG_TAG, "WebView setup completed.");
    }

    @JavascriptInterface
    public void displayExpression(String expression) {
	runOnUiThread(() -> {
	    String escapedExpr = expression.replace("\\", "\\\\")
					   .replace("\"", "\\\"")
					   .replace("\n", "\\n")
					   .replace("\r", "\\r")
					   .replace("\t", "\\t");
	    String jsCode = "if (window.nativeDisplayExpression) { " +
			    "window.nativeDisplayExpression(\"" + escapedExpr +
			    "\"); }";
	    webView.evaluateJavascript(jsCode, null);
	});
    }

    @JavascriptInterface
    public void displayResult(String text, String source, String type) {
	runOnUiThread(() -> {
	    String escapedText = text.replace("\\", "\\\\")
				     .replace("\"", "\\\"")
				     .replace("\n", "\\n")
				     .replace("\r", "\\r")
				     .replace("\t", "\\t");
	    String jsCode = "if (window.nativeDisplayResult) { " +
			    "window.nativeDisplayResult(\"" + escapedText +
			    "\", \"" + source + "\", \"" + type + "\"); }";
	    webView.evaluateJavascript(jsCode, null);
	});
    }

    @JavascriptInterface
    public void displayCapturedOutput(String output) {
	runOnUiThread(() -> {
	    String escapedOutput = output.replace("\\", "\\\\")
					 .replace("\"", "\\\"")
					 .replace("\n", "\\n")
					 .replace("\r", "\\r")
					 .replace("\t", "\\t");
	    String jsCode = "if (window.nativeDisplayCapturedOutput) { " +
			    "window.nativeDisplayCapturedOutput(\"" +
			    escapedOutput + "\"); }";
	    webView.evaluateJavascript(jsCode, null);
	});
    }

    @JavascriptInterface
    public void replaceElementHTML(String selector, String html) {
	runOnUiThread(() -> {
	    String escapedSelector = selector.replace("\\", "\\\\")
					     .replace("\"", "\\\"")
					     .replace("\n", "\\n")
					     .replace("\r", "\\r")
					     .replace("\t", "\\t");
	    String escapedHtml = html.replace("\\", "\\\\")
				     .replace("\"", "\\\"")
				     .replace("\n", "\\n")
				     .replace("\r", "\\r")
				     .replace("\t", "\\t");
	    String jsCode = "(function() { " +
			    "try { " +
			    "var el = document.querySelector(\"" + escapedSelector + "\"); " +
			    "if (el) { el.outerHTML = \"" + escapedHtml + "\"; } " +
			    "else { console.error('Element not found: " + escapedSelector + "'); } " +
			    "} catch (e) { console.error('replaceElementHTML error:', e); } " +
			    "})();";
	    webView.evaluateJavascript(jsCode, null);
	});
    }

    public void initializeBluetooth() {
	bluetooth = new Bluetooth(this, chibiScheme, webView);
	bluetooth.requestBluetoothPermissions();
    }

    @Override
    protected void onDestroy() {
	super.onDestroy();
	if (bluetooth != null) {
	    bluetooth.stop();
	}
	if (chibiScheme != null) {
	    chibiScheme.cleanupScheme();
	}
	Log.i(LOG_TAG, "MainActivity destroyed");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
					   String[] permissions,
					   int[] grantResults) {
	super.onRequestPermissionsResult(requestCode, permissions,
					 grantResults);
	if (bluetooth != null) {
	    bluetooth.handleBluetoothPermissionsResult(requestCode, permissions,
						       grantResults);
	}
    }
}