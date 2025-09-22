package com.speechcode.repl;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.webkit.WebSettings;
import android.webkit.WebView;

public class MainActivity extends Activity {
    private static final String TAG = "repl";
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private WebView webView;
    private ChibiScheme chibiScheme;

    static { System.loadLibrary("repl"); }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
	super.onCreate(savedInstanceState);
	setTitle("CHB: Chibi Scheme REPL");
	Log.i(TAG, "MainActivity onCreate started.");

	chibiScheme = new ChibiScheme(this);
	try {
	    if (chibiScheme.shouldExtractAssets()) {
		Log.i(TAG, "Extracting assets based on version check.");
		if (AssetExtractor.extractAssets(this)) {
		    chibiScheme.markAssetsExtracted();
		    Log.i(TAG, "Asset extraction successful.");
		} else {
		    Log.e(TAG, "Asset extraction failed. Continuing with basic environment.");
		}
	    } else {
		Log.i(TAG, "Skipping asset extraction - version unchanged.");
	    }

	    chibiScheme.initializeScheme();
	    Log.i(TAG, "Chibi Scheme initialized successfully.");
	} catch (Exception e) {
	    Log.e(TAG, "Failed to initialize Chibi Scheme: " + e.getMessage());
	}
	setupWebView();
	Log.i(TAG, "MainActivity onCreate completed.");
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
	    Log.i(TAG, "Direct JNI test result: " + chibiScheme.evaluateScheme("(+ 2 3)"));
	    Log.i(TAG, "Direct JNI test result: " + chibiScheme.evaluateScheme("(* 4 5)"));
	    Log.i(TAG, "Direct JNI test result: " + chibiScheme.evaluateScheme("(list 1 2 3)"));
	} catch (Exception e) {
	    Log.e(TAG, "JNI test failed: " + e.getMessage());
	}
	setContentView(webView);
	try {
	    webView.addJavascriptInterface(chibiScheme, "Scheme");
	    Log.i(TAG, "JavaScript interface added successfully.");
	} catch (Exception e) {
	    Log.e(TAG, "Failed to add JavaScript interface: " + e.getMessage());
	}
	webView.setWebChromeClient(new DebugWebChromeClient());
	webView.loadUrl("file:///android_asset/test.html");
	chibiScheme.initializeBluetooth(webView);
	Log.i(TAG, "WebView setup completed.");
    }

    @Override
    protected void onDestroy() {
	super.onDestroy();
	if (chibiScheme != null) {
	    chibiScheme.stopBluetooth();
	}
	Log.i(TAG, "MainActivity destroyed");
    }

    public BluetoothReplService getBluetoothReplService() {
	return chibiScheme.getBluetoothReplService();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
					   String[] permissions,
					   int[] grantResults) {
	super.onRequestPermissionsResult(requestCode, permissions,
					 grantResults);
	if (chibiScheme != null) {
	    chibiScheme.handleBluetoothPermissionsResult(requestCode, permissions, grantResults);
	}
    }
}