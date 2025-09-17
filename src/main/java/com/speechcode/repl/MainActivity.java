package com.speechcode.repl;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.webkit.ConsoleMessage;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class MainActivity extends Activity {
    private static final String TAG = "repl";
    private WebView webView;
    private InternetReplService internetReplService;

    static {
        System.loadLibrary("repl");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "MainActivity onCreate started.");

        try {
            initializeScheme();
            Log.i(TAG, "Chibi Scheme initialized successfully.");
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize Chibi Scheme: " + e.getMessage());
        }
        setupWebView();
        Log.i(TAG, "MainActivity onCreate completed.");
    }

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    private void setupWebView() {
        webView = new WebView(this);

        WebSettings webSettings = webView.getSettings();

        webSettings.setJavaScriptEnabled(true);
        webSettings.setDomStorageEnabled(true);
        webSettings.setAllowFileAccess(true);
        webSettings.setAllowContentAccess(true);
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
        setContentView(webView);
        try {
            SchemeInterface schemeInterface = new SchemeInterface(this);
            webView.addJavascriptInterface(schemeInterface, "Scheme");
            Log.i(TAG, "JavaScript interface added successfully.");
        } catch (Exception e) {
            Log.e(TAG, "Failed to add JavaScript interface: " + e.getMessage());
        }
        webView.setWebChromeClient(new DebugWebChromeClient());
        webView.loadUrl("file:///android_asset/test.html");
        internetReplService = new InternetReplService(this, webView);
        internetReplService.start();

        Log.i(TAG, "WebView setup completed.");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (internetReplService != null) {
            internetReplService.stop();
            internetReplService = null;
        }
        Log.i(TAG, "MainActivity destroyed");
    }

    public InternetReplService getInternetReplService() {
        return internetReplService;
    }

    public native void initializeScheme();

    public native String evaluateScheme(String expression);
}