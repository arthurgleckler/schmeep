package com.speechcode.schmeep;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothServerSocket;
import android.bluetooth.BluetoothSocket;
import android.content.pm.PackageManager;
import android.Manifest;
import android.os.Build;
import android.util.Log;
import android.webkit.WebView;
import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.UUID;

public class Bluetooth {
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private static final int MAX_MESSAGE_LENGTH = 1048576;
    private static final UUID SCHMEEP_UUID =
	UUID.fromString("611a1a1a-94ba-11f0-b0a8-5f754c08f133");
    private static final String SERVICE_NAME = "schmeep";
    private static final UUID SPP_UUID =
	UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final String TAG = "schmeep";

    private static final byte CMD_EVALUATE = (byte) 254;
    private static final byte CMD_INTERRUPT = (byte) 255;
    private static final byte CMD_EVALUATION_COMPLETE = (byte) 255;

    private final AtomicBoolean isRunning;
    private final ChibiScheme chibiScheme;
    private final ExecutorService executorService;
    private final MainActivity mainActivity;
    private final WebView webView;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothServerSocket serverSocket;
    private BluetoothSocket clientSocket;
    private InputStream inputStream;
    private OutputStream outputStream;
    private String connectionStatus;
    private StringBuilder expressionBuffer;

    public Bluetooth(MainActivity activity, ChibiScheme chibiScheme,
		     WebView webView) {
	this.chibiScheme = chibiScheme;
	this.connectionStatus = "Bluetooth disabled";
	this.executorService = Executors.newSingleThreadExecutor();
	this.isRunning = new AtomicBoolean(false);
	this.mainActivity = activity;
	this.webView = webView;
	this.expressionBuffer = new StringBuilder();
    }

    public void handleBluetoothPermissionsResult(int requestCode,
						 String[] permissions,
						 int[] grantResults) {
	if (requestCode == BLUETOOTH_REQUEST_CODE) {
	    boolean allGranted = true;
	    for (int result : grantResults) {
		if (result != PackageManager.PERMISSION_GRANTED) {
		    allGranted = false;
		    break;
		}
	    }
	    if (allGranted) {
		Log.i(TAG, "Bluetooth permissions granted.");
		start();
	    } else {
		Log.w(TAG, "Bluetooth permissions denied.");
	    }
	}
    }

    public void requestBluetoothPermissions() {
	if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
	    if (hasBluetoothPermissions()) {
		Log.i(TAG, "Bluetooth permissions already granted.");
		start();
	    }
	} else {
	    String[] permissions = {Manifest.permission.BLUETOOTH_CONNECT,
				    Manifest.permission.BLUETOOTH_ADVERTISE};
	    boolean needsPermission = false;

	    for (String permission : permissions) {
		if (mainActivity.checkSelfPermission(permission) !=
		    PackageManager.PERMISSION_GRANTED) {
		    needsPermission = true;
		    break;
		}
	    }
	    if (needsPermission) {
		mainActivity.requestPermissions(permissions,
						BLUETOOTH_REQUEST_CODE);
	    } else {
		Log.i(TAG, "Bluetooth permissions already granted.");
		start();
	    }
	}
    }

    public void start() {
	if (isRunning.compareAndSet(false, true)) {
	    bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

	    if (bluetoothAdapter == null) {
		updateConnectionStatus("bluetooth-not-supported",
				       "Bluetooth not supported.");
		isRunning.set(false);
		return;
	    }

	    if (!bluetoothAdapter.isEnabled()) {
		updateConnectionStatus("bluetooth-disabled",
				       "Bluetooth disabled.");
		isRunning.set(false);
		return;
	    }

	    if (!hasBluetoothPermissions()) {
		updateConnectionStatus("bluetooth-permissions-required",
				       "Bluetooth permissions required.");
		isRunning.set(false);
		return;
	    }

	    try {
		IOException lastException = null;
		boolean serviceStarted = false;

		for (UUID uuid : new UUID[] {SCHMEEP_UUID, SPP_UUID}) {
		    try {
			serverSocket =
			    bluetoothAdapter.listenUsingRfcommWithServiceRecord(
				SERVICE_NAME, uuid);
			Log.i(
			    TAG,
			    "Started Bluetooth REPL service (secure) with UUID: " +
				uuid);
			serviceStarted = true;
			break;
		    } catch (IOException e) {
			lastException = e;
			Log.w(TAG, "Failed to start with UUID " + uuid + ": " +
				       e.getMessage());
		    }
		}

		if (!serviceStarted) {
		    throw lastException != null
			? lastException
			: new IOException(
			      "Could not start any Bluetooth service");
		}

		updateConnectionStatus(
		    "awaiting-connection",
		    "Bluetooth server started.  Waiting for connections.");
		executorService.execute(this::handleIncomingConnections);

	    } catch (IOException e) {
		Log.e(TAG,
		      "Failed to start Bluetooth server: " + e.getMessage());
		updateConnectionStatus("failed-to-start",
				       "Failed to start server: " +
					   e.getMessage());
		isRunning.set(false);
	    }
	}
    }

    public void stop() {
	if (isRunning.compareAndSet(true, false)) {
	    Log.i(TAG, "Stopping Bluetooth REPL service");
	    updateConnectionStatus("disconnected", "Disconnected.");

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

    private void closeClientConnection() {
	try {
	    if (inputStream != null) {
		inputStream.close();
		inputStream = null;
	    }
	} catch (IOException e) {
	    Log.w(TAG, "Error closing input stream: " + e.getMessage());
	}

	try {
	    if (outputStream != null) {
		outputStream.close();
		outputStream = null;
	    }
	} catch (IOException e) {
	    Log.w(TAG, "Error closing output stream: " + e.getMessage());
	}

	try {
	    if (clientSocket != null) {
		clientSocket.close();
		clientSocket = null;
	    }
	} catch (IOException e) {
	    Log.w(TAG, "Error closing client socket: " + e.getMessage());
	}

	expressionBuffer.setLength(0);
	if (isRunning.get()) {
	    updateConnectionStatus(
		"awaiting-connection",
		"Client disconnected.  Waiting for new connection.");
	}
    }

    private void displayExpression(String expression) {
	executeJavaScriptOnWebView(
	    String.format("displayBluetoothExpression(\"%s\");",
			  escapeForJavaScript(expression)),
	    "Executing JavaScript for received Bluetooth expression: " +
		expression,
	    "displayExpression");
    }

    private void displayResult(String expression, String result) {
	executeJavaScriptOnWebView(
	    String.format("displayBluetoothResult(\"%s\", \"%s\");",
			  escapeForJavaScript(expression),
			  escapeForJavaScript(result)),
	    "Executing JavaScript for Bluetooth result: " + expression + " = " +
		result,
	    "displayResult");
    }

    private String escapeForJavaScript(String input) {
	return input.replace("\"", "\\\"");
    }

    private void executeJavaScriptOnWebView(String javascript,
					    String logMessage,
					    String methodName) {
	webView.post(() -> {
	    try {
		Log.d(TAG, logMessage);
		webView.evaluateJavascript(javascript, null);
	    } catch (Exception e) {
		Log.e(TAG, "Error in " + methodName + ": " + e.getMessage());
	    }
	});
    }

    private void handleClientSession() throws IOException {
	while (isRunning.get()) {
	    try {
		int lengthByte = inputStream.read();

		if (lengthByte == -1) {
		    Log.i(TAG, "Client disconnected normally.");
		    break;
		}

		if (lengthByte == 254) {
		    handleEvaluateCommand();
		} else if (lengthByte == 255) {
		    handleInterruptCommand();
		} else if (lengthByte >= 1 && lengthByte <= 251) {
		    handleDataBlock(lengthByte);
		} else {
		    Log.w(TAG, "Invalid length byte: " + lengthByte);
		}
	    } catch (IOException e) {
		Log.e(TAG, "Error in message handling: " + e.getMessage());
		throw e;
	    }
	}
    }


    private void handleIncomingConnections() {
	while (isRunning.get()) {
	    try {
		updateConnectionStatus("waiting-for-connection",
				       "Waiting for client connection.");
		Log.i(TAG, "Waiting for Bluetooth client connection.");
		clientSocket = serverSocket.accept();
		Log.i(TAG, "Client connected: " +
			       clientSocket.getRemoteDevice().getAddress());
		inputStream = clientSocket.getInputStream();
		outputStream = clientSocket.getOutputStream();
		updateConnectionStatus("connected", "Client connected.");
		handleClientSession();
	    } catch (IOException e) {
		if (isRunning.get()) {
		    Log.e(TAG, "Connection error: " + e.getMessage());
		    updateConnectionStatus("connection-failed",
					   "Connection failed - " +
					       e.getMessage());
		}

		closeClientConnection();

		if (isRunning.get()) {
		    try {
			Log.i(TAG, "Waiting for RFCOMM cleanup before accepting new connections...");
			Thread.sleep(3000);
		    } catch (InterruptedException ie) {
			Thread.currentThread().interrupt();
			break;
		    }
		}
	    }
	}
    }

    private boolean hasBluetoothPermissions() {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
	    return mainActivity.checkSelfPermission(
		       Manifest.permission.BLUETOOTH_CONNECT) ==
		PackageManager.PERMISSION_GRANTED &&
		mainActivity.checkSelfPermission(
		    Manifest.permission.BLUETOOTH_ADVERTISE) ==
		    PackageManager.PERMISSION_GRANTED;
	} else {
	    return mainActivity.checkSelfPermission(
		       Manifest.permission.BLUETOOTH) ==
		PackageManager.PERMISSION_GRANTED &&
		mainActivity.checkSelfPermission(
		    Manifest.permission.BLUETOOTH_ADMIN) ==
		    PackageManager.PERMISSION_GRANTED;
	}
    }

    private void handleDataBlock(int length) throws IOException {
	byte[] buffer = new byte[length];
	int bytesRead = 0;

	while (bytesRead < length) {
	    int result = inputStream.read(buffer, bytesRead, length - bytesRead);
	    if (result == -1) {
		throw new IOException("Connection closed while reading data block.");
	    }
	    bytesRead += result;
	}

	String data = new String(buffer, StandardCharsets.UTF_8);
	expressionBuffer.append(data);
	Log.d(TAG, "Received data block: " + data.replace("\n", "\\n"));
    }

    private void handleEvaluateCommand() {
	String expression = expressionBuffer.toString().trim();
	expressionBuffer.setLength(0);

	if (expression.isEmpty()) {
	    streamToClient("");
	    return;
	}

	Log.i(TAG, "Executing expression: " + expression.replace("\n", "\\n"));
	displayExpression(expression);

	new Thread(() -> {
	    try {
		updateConnectionStatus("evaluating", "Evaluating expression.");
		String result = chibiScheme.evaluateScheme(expression);
		Log.i(TAG, "Evaluation result: " + result.replace("\n", "\\n"));
		updateConnectionStatus("connected", "Client connected.");
		streamToClient(result);
		displayResult(expression, result);
	    } catch (Exception e) {
		Log.e(TAG, "Error during evaluation: " + e.getMessage());
		streamToClient("Error: " + e.getMessage());
		updateConnectionStatus("connected", "Client connected.");
	    }
	}).start();
    }

    private void handleInterruptCommand() {
	Log.i(TAG, "Interrupt command received.");
	expressionBuffer.setLength(0);

	new Thread(() -> {
	    try {
		String result = chibiScheme.interruptScheme();
		Log.i(TAG, "Interrupt result: " + result);
	    } catch (Exception e) {
		Log.e(TAG, "Error during interrupt: " + e.getMessage());
	    }
	}).start();
    }


    private void sendDataBlockToClient(byte[] data, int length) throws IOException {
	if (outputStream != null) {
	    outputStream.write(length);
	    outputStream.write(data, 0, length);
	    outputStream.flush();
	}
    }

    private void sendEvaluationCompleteToClient() throws IOException {
	if (outputStream != null) {
	    outputStream.write(CMD_EVALUATION_COMPLETE);
	    outputStream.flush();
	}
    }

    private void streamToClient(String message) {
	try {
	    if (outputStream != null) {
		String messageWithNewline = message + "\n";
		byte[] messageBytes = messageWithNewline.getBytes(StandardCharsets.UTF_8);

		int sent = 0;
		while (sent < messageBytes.length) {
		    int blockSize = Math.min(254, messageBytes.length - sent);
		    outputStream.write(blockSize);
		    outputStream.write(messageBytes, sent, blockSize);
		    outputStream.flush();
		    sent += blockSize;
		}

		sendEvaluationCompleteToClient();
	    }
	} catch (IOException e) {
	    Log.e(TAG, "Error streaming to client: " + e.getMessage());
	}
    }

    private void updateConnectionStatus(String statusType, String message) {
	this.connectionStatus = message;
	webView.post(() -> {
	    String javascript =
		String.format("updateConnectionStatus(\"%s\", \"%s\");",
			      statusType.replace("\"", "\\\""),
			      message.replace("\"", "\\\""));
	    webView.evaluateJavascript(javascript, null);
	});
    }
}