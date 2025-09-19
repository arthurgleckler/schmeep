package com.speechcode.repl;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
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
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private WebView webView;
    private BluetoothReplService bluetoothReplService;

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

        bluetoothReplService = new BluetoothReplService(this, webView);

        requestBluetoothPermissions();

        // Only start if we already have permissions, otherwise wait for callback
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

    public native void initializeScheme();

    public native String evaluateScheme(String expression);

    public native String interruptScheme();
}