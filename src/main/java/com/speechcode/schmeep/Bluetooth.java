package com.speechcode.schmeep;

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
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class Bluetooth {
    private static final int BLUETOOTH_REQUEST_CODE = 1001;
    private static final int CMD_C2A_EVALUATE = 254;
    private static final int CMD_C2A_INTERRUPT = 255;
    private static final int CMD_C2A_MIN_COMMAND = CMD_C2A_EVALUATE;
    private static final byte CMD_A2C_EVALUATION_COMPLETE = (byte)255;
    private static final int MAX_MESSAGE_LENGTH = 1048576;
    private static final UUID SCHMEEP_UUID =
	UUID.fromString("611a1a1a-94ba-11f0-b0a8-5f754c08f133");
    private static final String SERVICE_NAME = "schmeep";
    private static final UUID SPP_UUID =
	UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final String LOG_TAG = "schmeep";

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
	setNativeOutputCallback();
    }

    private native void setNativeOutputCallback();

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
		Log.i(LOG_TAG, "Bluetooth permissions granted.");
		start();
	    } else {
		Log.w(LOG_TAG, "Bluetooth permissions denied.");
	    }
	}
    }

    public void requestBluetoothPermissions() {
	if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
	    if (hasBluetoothPermissions()) {
		Log.i(LOG_TAG, "Bluetooth permissions already granted.");
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
		Log.i(LOG_TAG, "Bluetooth permissions already granted.");
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
			    LOG_TAG,
			    "Started Bluetooth REPL service (secure) with UUID: " +
				uuid);
			serviceStarted = true;
			break;
		    } catch (IOException e) {
			lastException = e;
			Log.w(LOG_TAG, "Failed to start with UUID " + uuid +
					   ": " + e.getMessage());
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
		Log.e(LOG_TAG,
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
	    Log.i(LOG_TAG, "Stopping Bluetooth REPL service");
	    updateConnectionStatus("disconnected", "Disconnected.");

	    try {
		if (serverSocket != null) {
		    serverSocket.close();
		}
	    } catch (IOException e) {
		Log.w(LOG_TAG,
		      "Error closing server socket: " + e.getMessage());
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
	    Log.w(LOG_TAG, "Error closing input stream: " + e.getMessage());
	}

	try {
	    if (outputStream != null) {
		outputStream.close();
		outputStream = null;
	    }
	} catch (IOException e) {
	    Log.w(LOG_TAG, "Error closing output stream: " + e.getMessage());
	}

	try {
	    if (clientSocket != null) {
		clientSocket.close();
		clientSocket = null;
	    }
	} catch (IOException e) {
	    Log.w(LOG_TAG, "Error closing client socket: " + e.getMessage());
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
	return input.replace("\\", "\\\\")
	    .replace("\"", "\\\"")
	    .replace("\n", "\\n")
	    .replace("\r", "\\r")
	    .replace("\t", "\\t");
    }

    private void executeJavaScriptOnWebView(String javascript,
					    String logMessage,
					    String methodName) {
	webView.post(() -> {
	    try {
		Log.d(LOG_TAG, logMessage);
		webView.evaluateJavascript(javascript, null);
	    } catch (Exception e) {
		Log.e(LOG_TAG,
		      "Error in " + methodName + ": " + e.getMessage());
	    }
	});
    }

    private void handleClientSession() throws IOException {
	while (isRunning.get()) {
	    try {
		int commandOrLength = inputStream.read();

		if (commandOrLength == -1) {
		    Log.i(LOG_TAG, "Client disconnected normally.");
		    break;
		}

		if (commandOrLength == CMD_C2A_EVALUATE) {
		    handleEvaluateCommand();
		} else if (commandOrLength == CMD_C2A_INTERRUPT) {
		    handleInterruptCommand();
		} else {
		    handleDataBlock(commandOrLength);
		}
	    } catch (IOException e) {
		Log.e(LOG_TAG, "Error in message handling: " + e.getMessage());
		throw e;
	    }
	}
    }

    private void handleIncomingConnections() {
	while (isRunning.get()) {
	    try {
		updateConnectionStatus("waiting-for-connection",
				       "Waiting for client connection.");
		Log.i(LOG_TAG, "Waiting for Bluetooth client connection.");
		clientSocket = serverSocket.accept();
		Log.i(LOG_TAG, "Client connected: " +
				   clientSocket.getRemoteDevice().getAddress());
		inputStream = clientSocket.getInputStream();
		outputStream = clientSocket.getOutputStream();
		updateConnectionStatus("connected", "Client connected.");
		handleClientSession();
	    } catch (IOException e) {
		if (isRunning.get()) {
		    Log.e(LOG_TAG, "Connection error: " + e.getMessage());
		    updateConnectionStatus("connection-failed",
					   "Connection failed - " +
					       e.getMessage());
		}

		closeClientConnection();

		if (isRunning.get()) {
		    try {
			Log.i(
			    LOG_TAG,
			    "Waiting for RFCOMM cleanup before accepting new connections...");
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
	if (length == 0)
	    return;

	byte[] buffer = new byte[length];
	int bytesRead = 0;

	while (bytesRead < length) {
	    int result =
		inputStream.read(buffer, bytesRead, length - bytesRead);
	    if (result == -1) {
		throw new IOException(
		    "Connection closed while reading data block.");
	    }
	    bytesRead += result;
	}

	String data = new String(buffer, StandardCharsets.UTF_8);

	expressionBuffer.append(data);
	Log.d(LOG_TAG, "Received data block: " + data.replace("\n", "\\n"));
    }

    private void handleEvaluateCommand() {
	String expression = expressionBuffer.toString();

	if (expression.isEmpty()) {
	    return;
	}

	boolean isComplete = chibiScheme.isCompleteExpression(expression);

	if (!isComplete) {
	    Log.i(LOG_TAG, "Expression incomplete.  Waiting for more input: " +
			       expression.replace("\n", "\\n"));
	    return;
	}

	expressionBuffer.setLength(0);

	Log.i(LOG_TAG,
	      "Executing expression: " + expression.replace("\n", "\\n"));
	displayExpression(expression);

	new Thread(() -> {
	    try {
		updateConnectionStatus("evaluating", "Evaluating expression.");
		String result = chibiScheme.evaluateScheme(expression);

		Log.i(LOG_TAG,
		      "Evaluation result: " + result.replace("\n", "\\n"));
		updateConnectionStatus("connected", "Client connected.");
		streamToClient(result);
		displayResult(expression, result);
	    } catch (Exception e) {
		Log.e(LOG_TAG, "Error during evaluation: " + e.getMessage());
		streamToClient("Error: " + e.getMessage());
		updateConnectionStatus("connected", "Client connected.");
	    }
	}).start();
    }

    private void handleInterruptCommand() {
	Log.i(LOG_TAG, "Interrupt command received.");
	expressionBuffer.setLength(0);

	new Thread(() -> {
	    try {
		String result = chibiScheme.interruptScheme();

		Log.i(LOG_TAG, "Interrupt result: " + result);
	    } catch (Exception e) {
		Log.e(LOG_TAG, "Error during interrupt: " + e.getMessage());
	    }
	}).start();
    }

    private void sendDataBlockToClient(byte[] data, int length)
	throws IOException {
	if (outputStream != null) {
	    outputStream.write(length);
	    outputStream.write(data, 0, length);
	    outputStream.flush();
	}
    }

    private void sendEvaluationCompleteToClient() throws IOException {
	if (outputStream != null) {
	    outputStream.write(CMD_A2C_EVALUATION_COMPLETE);
	    outputStream.flush();
	}
    }

    public void streamPartialOutput(String output) {
	try {
	    if (output != null && !output.isEmpty()) {
		byte[] outputBytes = output.getBytes(StandardCharsets.UTF_8);

		writeBlocks(outputBytes, true);
	    }
	} catch (IOException e) {
	    Log.e(LOG_TAG, "Error streaming partial output to client: " +
			       e.getMessage());
	}
    }

    private void streamToClient(String message) {
	try {
	    if (outputStream != null) {
		String messageWithNewline = message + "\n";
		byte[] messageBytes =
		    messageWithNewline.getBytes(StandardCharsets.UTF_8);

		writeBlocks(messageBytes, false);
		sendEvaluationCompleteToClient();
	    }
	} catch (IOException e) {
	    Log.e(LOG_TAG, "Error streaming to client: " + e.getMessage());
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

    private void writeBlocks(byte[] data, boolean useSynchronization)
	throws IOException {
	if (outputStream == null || data == null || data.length == 0) {
	    return;
	}

	int sent = 0;

	while (sent < data.length) {
	    int blockSize =
		Math.min(CMD_C2A_MIN_COMMAND - 1, data.length - sent);

	    if (useSynchronization) {
		synchronized (this) {
		    if (outputStream != null) {
			outputStream.write(blockSize);
			outputStream.write(data, sent, blockSize);
			outputStream.flush();
		    }
		}
	    } else {
		outputStream.write(blockSize);
		outputStream.write(data, sent, blockSize);
		outputStream.flush();
	    }
	    sent += blockSize;
	}
    }
}