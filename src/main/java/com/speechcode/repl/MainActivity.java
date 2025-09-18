package com.speechcode.repl;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebView;
import android.webkit.WebSettings;
import android.webkit.JavascriptInterface;
import android.webkit.WebViewClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebChromeClient;
import android.util.Log;
import android.annotation.SuppressLint;

public class MainActivity extends Activity {
    private static final String TAG = "repl";
    private WebView webView;

    // Load native library.
    static {
        System.loadLibrary("repl");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "MainActivity onCreate started.");

        // Initialize Chibi Scheme in native code.
        try {
            initializeScheme();
            Log.i(TAG, "Chibi Scheme initialized successfully.");
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize Chibi Scheme: " + e.getMessage());
        }

        // Create and set up WebView.
        setupWebView();
        Log.i(TAG, "MainActivity onCreate completed.");
    }

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    private void setupWebView() {
        // Create WebView.
        webView = new WebView(this);

        // Enable JavaScript.
        WebSettings webSettings = webView.getSettings();
        webSettings.setJavaScriptEnabled(true);
        webSettings.setDomStorageEnabled(true);
        webSettings.setAllowFileAccess(true);
        webSettings.setAllowContentAccess(true);

        // Test JNI methods directly from Java.
        try {
            String testResult = evaluateScheme("(+ 2 3)");
            Log.i(TAG, "Direct JNI test result: " + testResult);

            testResult = evaluateScheme("(* 4 5)");
            Log.i(TAG, "Direct JNI test result: " + testResult);

            testResult = evaluateScheme("(list 1 2 3)");
            Log.i(TAG, "Direct JNI test result: " + testResult);
        } catch (Exception e) {
            Log.e(TAG, "JNI test failed: " + e.getMessage());
        }

        // Set WebView as content view (full screen).
        setContentView(webView);

        // Add JavaScript interface using separate class. This avoids inner class compilation issues.
        try {
            SchemeInterface schemeInterface = new SchemeInterface(this);
            webView.addJavascriptInterface(schemeInterface, "Scheme");
            Log.i(TAG, "JavaScript interface added successfully.");
        } catch (Exception e) {
            Log.e(TAG, "Failed to add JavaScript interface: " + e.getMessage());
        }

        // Load the HTML file from assets.
        webView.loadUrl("file:///android_asset/test.html");

        Log.i(TAG, "WebView setup completed.");
    }

    // TODO: JavaScript interface will be added later once API compatibility is resolved.

    // Native method declarations.
    public native void initializeScheme();
    public native String evaluateScheme(String expression);
}