package com.speechcode.repl;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.webkit.WebSettings;
import android.webkit.WebView;

public class MainActivity extends Activity {
    private static final String TAG = "repl";
    private WebView webView;
    private ChibiScheme chibiScheme;
    private BluetoothReplService bluetoothReplService;

    static { System.loadLibrary("repl"); }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
	super.onCreate(savedInstanceState);
	setTitle("CHB: Chibi Scheme REPL");
	Log.i(TAG, "MainActivity onCreate started.");

	chibiScheme = new ChibiScheme(this);
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
	    Log.i(TAG, "Direct JNI test result: " +
			   chibiScheme.evaluateScheme("(+ 2 3)"));
	    Log.i(TAG, "Direct JNI test result: " +
			   chibiScheme.evaluateScheme("(* 4 5)"));
	    Log.i(TAG, "Direct JNI test result: " +
			   chibiScheme.evaluateScheme("(list 1 2 3)"));
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
	initializeBluetooth();
	Log.i(TAG, "WebView setup completed.");
    }

    private void initializeBluetooth() {
	bluetoothReplService =
	    new BluetoothReplService(this, chibiScheme, webView);
	bluetoothReplService.requestBluetoothPermissions();
    }

    @Override
    protected void onDestroy() {
	super.onDestroy();
	if (bluetoothReplService != null) {
	    bluetoothReplService.stop();
	}
	Log.i(TAG, "MainActivity destroyed");
    }

    public BluetoothReplService getBluetoothReplService() {
	return bluetoothReplService;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
					   String[] permissions,
					   int[] grantResults) {
	super.onRequestPermissionsResult(requestCode, permissions,
					 grantResults);
	if (bluetoothReplService != null) {
	    bluetoothReplService.handleBluetoothPermissionsResult(
		requestCode, permissions, grantResults);
	}
    }
}