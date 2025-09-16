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
    private static final int CONNECTION_TIMEOUT = 300000; // 5 minutes
    private static final int READ_TIMEOUT = 300000; // 5 minutes
    private static final int MAX_RETRY_DELAY = 60000; // 1 minute max

    private final MainActivity mainActivity;
    private final WebView webView;
    private final ExecutorService executorService;
    private final AtomicBoolean isRunning;

    private String serverUrl;
    private int retryDelay;
    private String lastConnectionStatus;

    public InternetReplService(MainActivity activity, WebView webView) {
        this.mainActivity = activity;
        this.webView = webView;
        this.executorService = Executors.newSingleThreadExecutor();
        this.isRunning = new AtomicBoolean(false);
        this.serverUrl = DEFAULT_SERVER_URL;
        this.retryDelay = 1000; // Start with 1 second
        this.lastConnectionStatus = "Starting...";
        // Initial status will be set when service starts
    }

    public void start() {
        if (isRunning.compareAndSet(false, true)) {
            Log.i(TAG, "Starting Internet REPL service");
            updateConnectionStatus("Connecting...");
            updateStatusDisplay("Connecting...", "warning");

            // Start the incoming message thread
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
        this.lastConnectionStatus = "Connecting to new server...";
        updateStatusDisplay("Connecting to new server...", "warning");
        Log.i(TAG, "Server URL updated to: " + url);
    }

    private void handleIncomingMessages() {
        while (isRunning.get()) {
            try {
                String expression = waitForExpression();
                if (expression != null && !expression.trim().isEmpty()) {
                    Log.i(TAG, "Received expression from server: " + expression);

                    // Evaluate the expression
                    String result = mainActivity.evaluateScheme(expression.trim());
                    Log.i(TAG, "Evaluated result: " + result);

                    // Display in WebView
                    displayRemoteResult(expression.trim(), result);

                    // Send result back to server
                    sendResult(result);

                    // Reset retry delay on successful communication
                    retryDelay = 1000;
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
        URL url = new URL(serverUrl + "/wait-for-expression");
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();

        connection.setRequestMethod("GET");
        connection.setConnectTimeout(CONNECTION_TIMEOUT);
        connection.setReadTimeout(READ_TIMEOUT);
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
            URL url = new URL(serverUrl + "/result");
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
        // Use main thread to update WebView
        webView.post(() -> {
            String escapedExpression = expression.replace("'", "\\'").replace("\"", "\\\"");
            String escapedResult = result.replace("'", "\\'").replace("\"", "\\\"");

            String javascript = String.format(
                "const schemeContent = document.getElementById('scheme-content');" +
                "schemeContent.innerHTML += '<br>🌐 %s = %s';" +
                "schemeContent.scrollTop = schemeContent.scrollHeight;",
                escapedExpression, escapedResult
            );

            webView.evaluateJavascript(javascript, null);
        });
    }

    private void updateConnectionStatus(String status) {
        webView.post(() -> {
            String javascript = String.format(
                "const statusElement = document.querySelector('.status-bar div');" +
                "if (statusElement) statusElement.textContent = 'WebView + Internet REPL: %s';",
                status.replace("'", "\\'")
            );
            webView.evaluateJavascript(javascript, null);
        });
    }

    private void handleConnectionError(String errorMessage) {
        lastConnectionStatus = errorMessage + " - retrying in " + (retryDelay / 1000) + "s";
        updateConnectionStatus("Reconnecting in " + (retryDelay / 1000) + "s...");
        updateStatusDisplay(errorMessage + " - retrying in " + (retryDelay / 1000) + "s", "error");

        try {
            Thread.sleep(retryDelay);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return;
        }

        // Exponential backoff
        retryDelay = Math.min(retryDelay * 2, MAX_RETRY_DELAY);
    }

    private void updateStatusDisplay(String status, String statusType) {
        webView.post(() -> {
            String javascript = String.format(
                "if (typeof updateConnectionStatusDisplay === 'function') {" +
                "  updateConnectionStatusDisplay('%s', '%s');" +
                "}",
                status.replace("'", "\\'").replace("\"", "\\\""),
                statusType.replace("'", "\\'")
            );
            webView.evaluateJavascript(javascript, null);
        });
    }

}