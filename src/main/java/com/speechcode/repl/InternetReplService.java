package com.speechcode.repl;

import android.util.Log;
import android.webkit.WebView;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class InternetReplService {
    private static final String TAG = "repl";
    private static final String DEFAULT_SERVER_URL = "https://speechcode.com/chb-eval";
    private static final int CONNECTION_TIMEOUT_MILLIS = 300000;
    private static final int READ_TIMEOUT_MILLIS = 300000;
    private static final int MAX_RETRY_DELAY_MILLIS = 60000;

    private final MainActivity mainActivity;
    private final WebView webView;
    private final ExecutorService executorService;
    private final AtomicBoolean isRunning;

    private String serverUrl;
    private int retryDelayMillis;
    private String lastConnectionStatus;

    public InternetReplService(MainActivity activity, WebView webView) {
        this.mainActivity = activity;
        this.webView = webView;
        this.executorService = Executors.newSingleThreadExecutor();
        this.isRunning = new AtomicBoolean(false);
        this.serverUrl = DEFAULT_SERVER_URL;
        this.retryDelayMillis = 1000;
        this.lastConnectionStatus = "Starting.";
    }

    public void start() {
        if (isRunning.compareAndSet(false, true)) {
            Log.i(TAG, "Starting Internet REPL service");
            updateConnectionStatus("Connecting.");
            updateStatusDisplay("Connecting.", "warning");
            executorService.execute(this::handleIncomingMessages);
        }
    }

    public void stop() {
        if (isRunning.compareAndSet(true, false)) {
            Log.i(TAG, "Stopping Internet REPL service");
            updateConnectionStatus("Disconnected");
            updateStatusDisplay("Disconnected", "error");
            executorService.shutdown();
        }
    }

    public void setServerUrl(String url) {
        this.serverUrl = url;
        this.lastConnectionStatus = "Connecting to new server.";
        updateStatusDisplay("Connecting to new server.", "warning");
        Log.i(TAG, "Server URL updated to: " + url);
    }

    private void handleIncomingMessages() {
        while (isRunning.get()) {
            try {
                String expression = waitForExpression();
                if (expression != null && !expression.trim().isEmpty()) {
                    Log.i(TAG, "Received expression from server: " + expression);

                    String result = mainActivity.evaluateScheme(expression.trim());

                    Log.i(TAG, "Evaluated result: " + result);
                    displayRemoteResult(expression.trim(), result);
                    sendResult(result);
                    retryDelayMillis = 1000;
                    lastConnectionStatus = "Connected - waiting for expressions";
                    updateConnectionStatus("Connected");
                    updateStatusDisplay("Connected - waiting for expressions", "connected");
                }
            } catch (Exception e) {
                Log.e(TAG, "Error in incoming message handling: " + e.getMessage());
                handleConnectionError(e.getMessage());
            }
        }
    }


    private String waitForExpression() throws IOException {
        URL url = new URL(serverUrl);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();

        connection.setRequestMethod("GET");
        connection.setConnectTimeout(CONNECTION_TIMEOUT_MILLIS);
        connection.setReadTimeout(READ_TIMEOUT_MILLIS);
        connection.setRequestProperty("Connection", "keep-alive");
        connection.setRequestProperty("User-Agent", "SchemeREPL/1.0");

        int responseCode = connection.getResponseCode();
        if (responseCode == 200) {
            BufferedReader reader = new BufferedReader(
						       new InputStreamReader(connection.getInputStream(), StandardCharsets.UTF_8));
            StringBuilder response = new StringBuilder();
            String line;

            while ((line = reader.readLine()) != null) {
                response.append(line).append("\n");
            }
            reader.close();

            return response.toString().trim();
        } else {
            String errorMsg = "HTTP " + responseCode + " from server";
            lastConnectionStatus = errorMsg;
            updateStatusDisplay(errorMsg, "error");
            throw new IOException(errorMsg);
        }
    }

    private void sendResult(String result) {
        try {
            URL url = new URL(serverUrl);
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();

            connection.setRequestMethod("POST");
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(10000);
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "text/plain; charset=UTF-8");
            connection.setRequestProperty("Connection", "keep-alive");

            byte[] resultBytes = result.getBytes(StandardCharsets.UTF_8);
            connection.setRequestProperty("Content-Length", String.valueOf(resultBytes.length));

            OutputStream outputStream = connection.getOutputStream();
            outputStream.write(resultBytes);
            outputStream.flush();
            outputStream.close();

            int responseCode = connection.getResponseCode();
            if (responseCode != 200) {
                Log.w(TAG, "Result send got HTTP " + responseCode);
            }

        } catch (Exception e) {
            Log.e(TAG, "Error sending result to server: " + e.getMessage());
        }
    }


    private void displayRemoteResult(String expression, String result) {
        webView.post(() -> {
            try {
                String escapedExpression = expression.replace("'", "\\'").replace("\"", "\\\"");
                String escapedResult = result.replace("'", "\\'").replace("\"", "\\\"");
                String javascript = String.format(
                    "console.log('Displaying remote result: %s = %s'); " +
                    "if (typeof displayLocalResult === 'function') { " +
                    "  displayLocalResult('🌐 %s = %s', 'remote'); " +
                    "} else { " +
                    "  console.error('displayLocalResult function not found'); " +
                    "}",
                    escapedExpression, escapedResult, escapedExpression, escapedResult
                );

                Log.d(TAG, "Executing JavaScript for remote result: " + expression + " = " + result);
                webView.evaluateJavascript(javascript, jsResult -> {
                    if (jsResult != null) {
                        Log.d(TAG, "JavaScript execution result: " + jsResult);
                    }
                });
            } catch (Exception e) {
                Log.e(TAG, "Error in displayRemoteResult: " + e.getMessage());
            }
        });
    }

    private void updateConnectionStatus(String status) {
        webView.post(() -> {
		String javascript
		    = String.format("(function() { " +
				    "var statusElement = document.querySelector('.status-bar div');" +
				    "if (statusElement) statusElement.textContent = 'WebView + Internet REPL: %s';" +
				    "})();",
				    status.replace("'", "\\'"));
		webView.evaluateJavascript(javascript, null);
	    });
    }

    private void handleConnectionError(String errorMessage) {
        lastConnectionStatus = errorMessage + " - retrying in " + (retryDelayMillis / 1000) + "s";
        updateConnectionStatus("Reconnecting in " + (retryDelayMillis / 1000) + "s.");
        updateStatusDisplay(errorMessage + " - retrying in " + (retryDelayMillis / 1000) + "s", "error");

        try {
            Thread.sleep(retryDelayMillis);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return;
        }
        retryDelayMillis = Math.min(retryDelayMillis * 2, MAX_RETRY_DELAY_MILLIS);
    }

    private void updateStatusDisplay(String status, String statusType) {
        webView.post(() -> {
		String javascript
		    = String.format("if (typeof updateConnectionStatusDisplay === 'function') {" +
				    "  updateConnectionStatusDisplay('%s', '%s');" +
				    "}",
				    status.replace("'", "\\'").replace("\"", "\\\""),
				    statusType.replace("'", "\\'"));
		webView.evaluateJavascript(javascript, null);
	    });
    }
}