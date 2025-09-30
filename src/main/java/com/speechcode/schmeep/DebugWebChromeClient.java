package com.speechcode.schmeep;

import android.util.Log;
import android.webkit.ConsoleMessage;
import android.webkit.WebChromeClient;

public class DebugWebChromeClient extends WebChromeClient {
    private static final String LOG_TAG = "schmeep";

    @Override
    public boolean onConsoleMessage(ConsoleMessage consoleMessage) {
	Log.d(LOG_TAG, "WebView Console [" + consoleMessage.messageLevel() +
		       "]: " + consoleMessage.message() + " (line " +
		       consoleMessage.lineNumber() + ")");
	return true;
    }
}