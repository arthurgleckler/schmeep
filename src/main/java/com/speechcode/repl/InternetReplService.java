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
    private final BlockingQueue<String> outgoingQueue;

    private String serverUrl;
    private int retryDelay;
    private String lastConnectionStatus;

    public InternetReplService(MainActivity activity, WebView webView) {
        this.mainActivity = activity;
        this.webView = webView;
        this.executorService = Executors.newFixedThreadPool(2);
        this.isRunning = new AtomicBoolean(false);
        this.outgoingQueue = new LinkedBlockingQueue<>();
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

            // Start the outgoing message thread
            executorService.execute(this::handleOutgoingMessages);
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

    public void queueExpression(String expression) {
        if (isRunning.get()) {
            outgoingQueue.offer(expression);
            Log.i(TAG, "Queued expression for server: " + expression);
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

    private void handleOutgoingMessages() {
        while (isRunning.get()) {
            try {
                // Wait for expression to send
                String expression = outgoingQueue.take();

                // Send to server queue
                sendExpressionToQueue(expression);
                Log.i(TAG, "Sent expression to server queue: " + expression);

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            } catch (Exception e) {
                Log.e(TAG, "Error sending expression to server: " + e.getMessage());
                lastConnectionStatus = "Send error: " + e.getMessage();
                updateStatusDisplay("Send error: " + e.getMessage(), "error");
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

    private void sendExpressionToQueue(String expression) throws IOException {
        URL url = new URL(serverUrl + "/queue");
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();

        connection.setRequestMethod("POST");
        connection.setConnectTimeout(10000);
        connection.setReadTimeout(10000);
        connection.setDoOutput(true);
        connection.setRequestProperty("Content-Type", "text/plain; charset=UTF-8");

        byte[] expressionBytes = expression.getBytes(StandardCharsets.UTF_8);
        connection.setRequestProperty("Content-Length", String.valueOf(expressionBytes.length));

        OutputStream outputStream = connection.getOutputStream();
        outputStream.write(expressionBytes);
        outputStream.flush();
        outputStream.close();

        int responseCode = connection.getResponseCode();
        if (responseCode != 200) {
            throw new IOException("Queue send got HTTP " + responseCode);
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