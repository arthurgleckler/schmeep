package com.speechcode.repl;

import android.util.Log;
import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;

public class DebugWebChromeClient extends WebChromeClient {
    private static final String TAG = "repl";

    @Override
    public boolean onConsoleMessage(ConsoleMessage consoleMessage) {
        Log.d(TAG, "WebView Console [" + consoleMessage.messageLevel() + "]: " +
              consoleMessage.message() + " (line " + consoleMessage.lineNumber() + ")");
        return true;
    }
}