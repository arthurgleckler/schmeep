package com.speechcode.repl;

import android.webkit.JavascriptInterface;
import android.util.Log;

public class SchemeInterface {
    private static final String TAG = "repl";
    private MainActivity mainActivity;

    public SchemeInterface(MainActivity activity) {
        this.mainActivity = activity;
    }

    @JavascriptInterface
    public String eval(String expression) {
        Log.i(TAG, "JavaScript Interface: Local evaluation: " + expression);
        return mainActivity.evaluateScheme(expression);
    }

    @JavascriptInterface
    public void sendToServer(String expression) {
        Log.i(TAG, "JavaScript Interface: Sending to server: " + expression);
        if (mainActivity.getInternetReplService() != null) {
            mainActivity.getInternetReplService().queueExpression(expression);
        } else {
            Log.w(TAG, "Internet REPL service not available");
        }
    }

    @JavascriptInterface
    public void setServerUrl(String url) {
        Log.i(TAG, "JavaScript Interface: Setting server URL: " + url);
        if (mainActivity.getInternetReplService() != null) {
            mainActivity.getInternetReplService().setServerUrl(url);
        }
    }

}