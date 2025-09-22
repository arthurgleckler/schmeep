package com.speechcode.repl;

import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

public class ChibiScheme {
    private static final String TAG = "repl";
    private MainActivity mainActivity;
    private BluetoothReplService bluetoothReplService;

    public native String evaluateScheme(String expression);

    public native void initializeScheme();

    public native String interruptScheme();

    public ChibiScheme(MainActivity activity) {
	this.mainActivity = activity;

	try {
	    AssetExtractor.handleAssetExtraction(activity);
	    initializeScheme();
	    Log.i(TAG, "Chibi Scheme initialized successfully.");
	} catch (Exception e) {
	    Log.e(TAG, "Failed to initialize Chibi Scheme: " + e.getMessage());
	}
    }

    @JavascriptInterface
    public String eval(String expression) {
	Log.i(TAG, "Chibi Scheme: local evaluation: " + expression);
	return evaluateScheme(expression);
    }

    public BluetoothReplService getBluetoothReplService() {
	return bluetoothReplService;
    }

    public void initializeBluetooth(WebView webView) {
	bluetoothReplService = new BluetoothReplService(mainActivity, this, webView);
	bluetoothReplService.requestBluetoothPermissions();
    }

    public void stopBluetooth() {
	if (bluetoothReplService != null) {
	    bluetoothReplService.stop();
	    bluetoothReplService = null;
	}
    }
}