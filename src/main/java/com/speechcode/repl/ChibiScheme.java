package com.speechcode.repl;

import android.Manifest;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class ChibiScheme {
    private static final String TAG = "repl";
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private MainActivity mainActivity;
    private BluetoothReplService bluetoothReplService;

    public ChibiScheme(MainActivity activity) {
	this.mainActivity = activity;
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

    public boolean shouldExtractAssets() {
	try {
	    PackageInfo packageInfo =
		mainActivity.getPackageManager().getPackageInfo(mainActivity.getPackageName(), 0);
	    long currentVersionCode = packageInfo.versionCode;

	    File markerFile = new File(
		"/data/data/com.speechcode.repl/lib/.assets_timestamp");

	    if (!markerFile.exists()) {
		Log.i(TAG, "Marker file doesn't exist, need to extract assets");
		return true;
	    }

	    try (FileInputStream fis = new FileInputStream(markerFile)) {
		byte[] buffer = new byte[20];
		int bytesRead = fis.read(buffer);

		if (bytesRead <= 0) {
		    Log.i(TAG, "Marker file is empty, need to extract assets");
		    return true;
		}

		String storedVersionString =
		    new String(buffer, 0, bytesRead).trim();
		long storedVersionCode = Long.parseLong(storedVersionString);

		if (currentVersionCode != storedVersionCode) {
		    Log.i(TAG, "Version changed from " + storedVersionCode +
			       " to " + currentVersionCode +
			       ", need to extract assets");
		    return true;
		} else {
		    Log.i(TAG, "Version unchanged (" + currentVersionCode +
			       "), skipping asset extraction");
		    return false;
		}
	    }
	} catch (Exception e) {
	    Log.w(TAG,
		  "Error checking asset extraction status: " + e.getMessage());
	    return true;
	}
    }

    public void markAssetsExtracted() {
	try {
	    PackageInfo packageInfo =
		mainActivity.getPackageManager().getPackageInfo(mainActivity.getPackageName(), 0);
	    long currentVersionCode = packageInfo.versionCode;

	    File libDir = new File("/data/data/com.speechcode.repl/lib");
	    if (!libDir.exists()) {
		libDir.mkdirs();
	    }

	    File markerFile = new File(libDir, ".assets_timestamp");
	    try (FileOutputStream fos = new FileOutputStream(markerFile)) {
		fos.write(String.valueOf(currentVersionCode).getBytes());
		fos.flush();
	    }

	    Log.i(TAG, "Asset extraction completed for version " +
		       currentVersionCode);
	} catch (Exception e) {
	    Log.e(TAG, "Error marking assets extracted: " + e.getMessage());
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