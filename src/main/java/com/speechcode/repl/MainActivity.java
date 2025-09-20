package com.speechcode.repl;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
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
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private WebView webView;
    private BluetoothReplService bluetoothReplService;

    static {
        System.loadLibrary("repl");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setTitle("CHB: Chibi Scheme REPL");
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

        bluetoothReplService = new BluetoothReplService(this, webView);

        requestBluetoothPermissions();

        if (hasBluetoothPermissions()) {
            bluetoothReplService.start();
        }

        Log.i(TAG, "WebView setup completed.");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (bluetoothReplService != null) {
            bluetoothReplService.stop();
            bluetoothReplService = null;
        }
        Log.i(TAG, "MainActivity destroyed");
    }

    public BluetoothReplService getBluetoothReplService() {
        return bluetoothReplService;
    }

    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
                   checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED;
        } else {
            return checkSelfPermission(Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED &&
                   checkSelfPermission(Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED;
        }
    }

    private void requestBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            String[] permissions = {
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_ADVERTISE
            };
            for (String permission : permissions) {
                if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(permissions, BLUETOOTH_REQUEST_CODE);
                    break;
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
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

    private boolean shouldExtractAssets() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
            long currentVersionCode = packageInfo.versionCode;

            File markerFile = new File("/data/data/com.speechcode.repl/lib/.assets_timestamp");

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

                String storedVersionString = new String(buffer, 0, bytesRead).trim();
                long storedVersionCode = Long.parseLong(storedVersionString);

                if (currentVersionCode != storedVersionCode) {
                    Log.i(TAG, "Version changed from " + storedVersionCode + " to " + currentVersionCode + ", need to extract assets");
                    return true;
                } else {
                    Log.i(TAG, "Version unchanged (" + currentVersionCode + "), skipping asset extraction");
                    return false;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Error checking asset extraction status: " + e.getMessage());
            return true;
        }
    }

    private void markAssetsExtracted() {
        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
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

            Log.i(TAG, "Asset extraction completed for version " + currentVersionCode);
        } catch (Exception e) {
            Log.e(TAG, "Error marking assets extracted: " + e.getMessage());
        }
    }

    public native boolean shouldExtractAssetsJni();

    public native void initializeScheme();

    public native String evaluateScheme(String expression);

    public native String interruptScheme();
}