package com.speechcode.repl;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import android.webkit.WebView;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class BluetoothReplService {
    private static final String TAG = "repl";
    private static final UUID SCHEME_REPL_UUID = UUID.fromString("611a1a1a-94ba-11f0-b0a8-5f754c08f133");
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final String SERVICE_NAME = "CHB";
    private static final int MAX_MESSAGE_LENGTH = 1048576;

    private final MainActivity mainActivity;
    private final WebView webView;
    private final ExecutorService executorService;
    private final AtomicBoolean isRunning;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothServerSocket serverSocket;
    private BluetoothSocket clientSocket;
    private InputStream inputStream;
    private OutputStream outputStream;

    private String connectionStatus;

    public BluetoothReplService(MainActivity activity, WebView webView) {
        this.mainActivity = activity;
        this.webView = webView;
        this.executorService = Executors.newSingleThreadExecutor();
        this.isRunning = new AtomicBoolean(false);
        this.connectionStatus = "Bluetooth disabled";
    }

    public void start() {
        if (isRunning.compareAndSet(false, true)) {
            bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

            if (bluetoothAdapter == null) {
                updateConnectionStatus("Bluetooth not supported");
                isRunning.set(false);
                return;
            }

            if (!bluetoothAdapter.isEnabled()) {
                updateConnectionStatus("Bluetooth disabled");
                isRunning.set(false);
                return;
            }

            if (!hasBluetoothPermissions()) {
                updateConnectionStatus("Bluetooth permissions required");
                isRunning.set(false);
                return;
            }

            try {
                // Try both UUIDs and connection types for maximum compatibility
                IOException lastException = null;
                boolean serviceStarted = false;

                // Try custom UUID first
                for (UUID uuid : new UUID[]{SCHEME_REPL_UUID, SPP_UUID}) {
                    for (boolean secure : new boolean[]{true, false}) {
                        try {
                            if (secure) {
                                serverSocket = bluetoothAdapter.listenUsingRfcommWithServiceRecord(
                                    SERVICE_NAME, uuid);
                                Log.i(TAG, "Started Bluetooth REPL service (secure) with UUID: " + uuid);
                            } else {
                                serverSocket = bluetoothAdapter.listenUsingInsecureRfcommWithServiceRecord(
                                    SERVICE_NAME, uuid);
                                Log.i(TAG, "Started Bluetooth REPL service (insecure) with UUID: " + uuid);
                            }
                            serviceStarted = true;
                            break;
                        } catch (IOException e) {
                            lastException = e;
                            Log.w(TAG, "Failed to start with UUID " + uuid + " (secure=" + secure + "): " + e.getMessage());
                        }
                    }
                    if (serviceStarted) break;
                }

                if (!serviceStarted) {
                    throw lastException != null ? lastException : new IOException("Could not start any Bluetooth service");
                }

                updateConnectionStatus("Bluetooth server started - waiting for connections");
                executorService.execute(this::handleIncomingConnections);

            } catch (IOException e) {
                Log.e(TAG, "Failed to start Bluetooth server: " + e.getMessage());
                updateConnectionStatus("Failed to start server: " + e.getMessage());
                isRunning.set(false);
            }
        }
    }

    public void stop() {
        if (isRunning.compareAndSet(true, false)) {
            Log.i(TAG, "Stopping Bluetooth REPL service");
            updateConnectionStatus("Disconnected");

            try {
                if (serverSocket != null) {
                    serverSocket.close();
                }
            } catch (IOException e) {
                Log.w(TAG, "Error closing server socket: " + e.getMessage());
            }

            closeClientConnection();
            executorService.shutdown();
        }
    }

    public void setServerUrl(String ignored) {
    }

    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
                   mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED;
        } else {
            return mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED &&
                   mainActivity.checkSelfPermission(Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED;
        }
    }

    private void handleIncomingConnections() {
        while (isRunning.get()) {
            try {
                updateConnectionStatus("Waiting for client connection");
                Log.i(TAG, "Waiting for Bluetooth client connection...");

                clientSocket = serverSocket.accept();
                Log.i(TAG, "Client connected: " + clientSocket.getRemoteDevice().getAddress());

                inputStream = clientSocket.getInputStream();
                outputStream = clientSocket.getOutputStream();

                updateConnectionStatus("Client connected");
                handleClientSession();

            } catch (IOException e) {
                if (isRunning.get()) {
                    Log.e(TAG, "Connection error: " + e.getMessage());
                    updateConnectionStatus("Connection failed - " + e.getMessage());
                }
                closeClientConnection();

                if (isRunning.get()) {
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException ie) {
                        Thread.currentThread().interrupt();
                        break;
                    }
                }
            }
        }
    }

    private void handleClientSession() throws IOException {
        while (isRunning.get()) {
            try {
                String expression = readMessage();

                if (expression != null && !expression.trim().isEmpty()) {
                    Log.i(TAG, "Received expression: " + expression.replace("\n", "\\n"));

                    String result = mainActivity.evaluateScheme(expression.trim());

                    Log.i(TAG, "Evaluated result: " + result.replace("\n", "\\n"));

                    writeMessage(result);

                    displayRemoteResult(expression.trim(), result);
                }
            } catch (IOException e) {
                Log.e(TAG, "Error in message handling: " + e.getMessage());
                throw e;
            }
        }
    }

    private String readMessage() throws IOException {
        byte[] lengthBytes = new byte[4];
        int bytesRead = 0;
        while (bytesRead < 4) {
            int result = inputStream.read(lengthBytes, bytesRead, 4 - bytesRead);
            if (result == -1) {
                throw new IOException("Connection closed while reading length");
            }
            bytesRead += result;
        }

        int messageLength = ((lengthBytes[0] & 0xFF) << 24) |
                           ((lengthBytes[1] & 0xFF) << 16) |
                           ((lengthBytes[2] & 0xFF) << 8) |
                           (lengthBytes[3] & 0xFF);

        if (messageLength < 0 || messageLength > MAX_MESSAGE_LENGTH) {
            throw new IOException("Invalid message length: " + messageLength);
        }

        byte[] messageBytes = new byte[messageLength];
        bytesRead = 0;
        while (bytesRead < messageLength) {
            int result = inputStream.read(messageBytes, bytesRead, messageLength - bytesRead);
            if (result == -1) {
                throw new IOException("Connection closed while reading message");
            }
            bytesRead += result;
        }

        return new String(messageBytes, StandardCharsets.UTF_8);
    }

    private void writeMessage(String message) throws IOException {
        byte[] messageBytes = message.getBytes(StandardCharsets.UTF_8);
        int length = messageBytes.length;

        byte[] lengthBytes = new byte[4];
        lengthBytes[0] = (byte) ((length >> 24) & 0xFF);
        lengthBytes[1] = (byte) ((length >> 16) & 0xFF);
        lengthBytes[2] = (byte) ((length >> 8) & 0xFF);
        lengthBytes[3] = (byte) (length & 0xFF);

        outputStream.write(lengthBytes);
        outputStream.write(messageBytes);
        outputStream.flush();
    }

    private void displayRemoteResult(String expression, String result) {
        webView.post(() -> {
            try {
                String escapedExpression = expression.replace("\"", "\\\"");
                String escapedResult = result.replace("\"", "\\\"");
                String javascript = String.format(
                    "console.log(\"Displaying Bluetooth result: %s = %s\"); " +
                    "if (typeof displayResult === \"function\") { " +
                    "  displayResult(\"📱 %s = %s\", \"remote\"); " +
                    "} else { " +
                    "  console.error(\"displayResult function not found\"); " +
                    "}",
                    escapedExpression, escapedResult, escapedExpression, escapedResult
                );

                Log.d(TAG, "Executing JavaScript for Bluetooth result: " + expression + " = " + result);
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
        this.connectionStatus = status;
        webView.post(() -> {
            String javascript = String.format(
                "(function() { " +
                "var statusElement = document.querySelector(\".status-bar div\");" +
                "if (statusElement) statusElement.textContent = \"WebView + Bluetooth REPL: %s\";" +
                "})();",
                status.replace("\"", "\\\""));
            webView.evaluateJavascript(javascript, null);
        });

        webView.post(() -> {
            String javascript = String.format(
                "if (typeof updateConnectionStatusDisplay === \"function\") {" +
                "  updateConnectionStatusDisplay(\"%s\", \"%s\");" +
                "}",
                status.replace("\"", "\\\""),
                getStatusType(status));
            webView.evaluateJavascript(javascript, null);
        });
    }

    private String getStatusType(String status) {
        if (status.contains("connected") || status.contains("Connected")) {
            return "connected";
        } else if (status.contains("failed") || status.contains("error") || status.contains("disabled")) {
            return "error";
        } else {
            return "warning";
        }
    }

    private void closeClientConnection() {
        try {
            if (inputStream != null) inputStream.close();
            if (outputStream != null) outputStream.close();
            if (clientSocket != null) clientSocket.close();
        } catch (IOException e) {
            Log.w(TAG, "Error closing connection: " + e.getMessage());
        } finally {
            inputStream = null;
            outputStream = null;
            clientSocket = null;
            if (isRunning.get()) {
                updateConnectionStatus("Client disconnected - waiting for new connection");
            }
        }
    }
}