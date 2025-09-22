package com.speechcode.repl;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

public class ChibiScheme {
    private static final String TAG = "repl";
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private MainActivity mainActivity;
    private BluetoothReplService bluetoothReplService;

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
	Log.i(TAG, "JavaScript Interface: Local evaluation: " + expression);
	return evaluateScheme(expression);
    }

    public BluetoothReplService getBluetoothReplService() {
	return bluetoothReplService;
    }

    public boolean hasBluetoothPermissions() {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
	    return mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
		PackageManager.PERMISSION_GRANTED &&
		mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) ==
		    PackageManager.PERMISSION_GRANTED;
	} else {
	    return mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH) ==
		PackageManager.PERMISSION_GRANTED &&
		mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_ADMIN) ==
		    PackageManager.PERMISSION_GRANTED;
	}
    }

    public void requestBluetoothPermissions() {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
	    String[] permissions = {Manifest.permission.BLUETOOTH_CONNECT,
				    Manifest.permission.BLUETOOTH_ADVERTISE};
	    for (String permission : permissions) {
		if (mainActivity.checkSelfPermission(permission) !=
		    PackageManager.PERMISSION_GRANTED) {
		    mainActivity.requestPermissions(permissions, BLUETOOTH_REQUEST_CODE);
		    break;
		}
	    }
	}
    }


    public native void initializeScheme();

    public native String evaluateScheme(String expression);

    public native String interruptScheme();

    public void initializeBluetooth(WebView webView) {
	bluetoothReplService = new BluetoothReplService(mainActivity, this, webView);
	requestBluetoothPermissions();
	if (hasBluetoothPermissions()) {
	    bluetoothReplService.start();
	}
    }

    public void stopBluetooth() {
	if (bluetoothReplService != null) {
	    bluetoothReplService.stop();
	    bluetoothReplService = null;
	}
    }

    public void handleBluetoothPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
	if (requestCode == BLUETOOTH_REQUEST_CODE) {
	    boolean allGranted = true;
	    for (int result : grantResults) {
		if (result != PackageManager.PERMISSION_GRANTED) {
		    allGranted = false;
		    break;
		}
	    }
	    if (allGranted) {
		Log.i(TAG, "Bluetooth permissions granted");
		if (bluetoothReplService != null) {
		    bluetoothReplService.start();
		}
	    } else {
		Log.w(TAG, "Bluetooth permissions denied");
	    }
	}
    }
}