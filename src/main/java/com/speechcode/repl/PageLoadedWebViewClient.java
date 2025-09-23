package com.speechcode.repl;

import android.util.Log;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class PageLoadedWebViewClient extends WebViewClient {
    private static final String TAG = "repl";
    private final MainActivity mainActivity;

    public PageLoadedWebViewClient(MainActivity activity) {
        this.mainActivity = activity;
    }

    @Override
    public void onPageFinished(WebView view, String url) {
        super.onPageFinished(view, url);
        Log.i(TAG, "WebView page finished loading: " + url);
        mainActivity.initializeBluetooth();
    }
}