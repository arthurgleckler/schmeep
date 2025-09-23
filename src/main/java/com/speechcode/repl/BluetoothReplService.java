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
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class BluetoothReplService {
    private static final String TAG = "repl";
    private static final UUID SCHEME_REPL_UUID =
	UUID.fromString("611a1a1a-94ba-11f0-b0a8-5f754c08f133");
    private static final UUID SPP_UUID =
	UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final String SERVICE_NAME = "CHB";
    private static final int MAX_MESSAGE_LENGTH = 1048576;
    private static final int BLUETOOTH_REQUEST_CODE = 1001;

    private static final byte MSG_TYPE_EXPRESSION = 0x00;
    private static final byte MSG_TYPE_INTERRUPT = 0x01;

    private final ChibiScheme chibiScheme;
    private final BlockingQueue<EvaluationRequest> evaluationQueue;
    private final ExecutorService evaluatorService;
    private final ExecutorService executorService;
    private final AtomicBoolean isRunning;
    private final MainActivity mainActivity;
    private final WebView webView;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothSocket clientSocket;
    private String connectionStatus;
    private InputStream inputStream;
    private OutputStream outputStream;
    private BluetoothServerSocket serverSocket;

    public BluetoothReplService(MainActivity activity, ChibiScheme chibiScheme,
				WebView webView) {
	this.mainActivity = activity;
	this.chibiScheme = chibiScheme;
	this.webView = webView;
	this.executorService = Executors.newSingleThreadExecutor();
	this.evaluatorService = Executors.newSingleThreadExecutor();
	this.isRunning = new AtomicBoolean(false);
	this.evaluationQueue = new LinkedBlockingQueue<>();
	this.connectionStatus = "Bluetooth disabled";
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
		Log.i(TAG, "Bluetooth permissions granted");
		start();
	    } else {
		Log.w(TAG, "Bluetooth permissions denied");
	    }
	}
    }

    public void requestBluetoothPermissions() {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
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
		Log.i(TAG, "Bluetooth permissions already granted");
		start();
	    }
	} else {
	    if (hasBluetoothPermissions()) {
		Log.i(TAG, "Bluetooth permissions already granted");
		start();
	    }
	}
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
		IOException lastException = null;
		boolean serviceStarted = false;

		for (UUID uuid : new UUID[] {SCHEME_REPL_UUID, SPP_UUID}) {
		    for (boolean secure : new boolean[] {true, false}) {
			try {
			    if (secure) {
				serverSocket =
				    bluetoothAdapter
					.listenUsingRfcommWithServiceRecord(
					    SERVICE_NAME, uuid);
				Log.i(
				    TAG,
				    "Started Bluetooth REPL service (secure) with UUID: " +
					uuid);
			    } else {
				serverSocket =
				    bluetoothAdapter
					.listenUsingInsecureRfcommWithServiceRecord(
					    SERVICE_NAME, uuid);
				Log.i(
				    TAG,
				    "Started Bluetooth REPL service (insecure) with UUID: " +
					uuid);
			    }
			    serviceStarted = true;
			    break;
			} catch (IOException e) {
			    lastException = e;
			    Log.w(TAG, "Failed to start with UUID " + uuid +
					   " (secure=" + secure +
					   "): " + e.getMessage());
			}
		    }
		    if (serviceStarted)
			break;
		}

		if (!serviceStarted) {
		    throw lastException != null
			? lastException
			: new IOException(
			      "Could not start any Bluetooth service");
		}

		updateConnectionStatus(
		    "Bluetooth server started - waiting for connections");
		executorService.execute(this::handleIncomingConnections);
		evaluatorService.execute(this::handleEvaluations);

	    } catch (IOException e) {
		Log.e(TAG,
		      "Failed to start Bluetooth server: " + e.getMessage());
		updateConnectionStatus("Failed to start server: " +
				       e.getMessage());
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
	    evaluationQueue.clear();
	    executorService.shutdown();
	    evaluatorService.shutdown();
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

    private void closeClientConnection() {
	try {
	    if (inputStream != null)
		inputStream.close();
	    if (outputStream != null)
		outputStream.close();
	    if (clientSocket != null)
		clientSocket.close();
	} catch (IOException e) {
	    Log.w(TAG, "Error closing connection: " + e.getMessage());
	} finally {
	    inputStream = null;
	    outputStream = null;
	    clientSocket = null;
	    evaluationQueue.clear();
	    if (isRunning.get()) {
		updateConnectionStatus(
		    "Client disconnected - waiting for new connection");
	    }
	}
    }

    private void displayReceivedExpression(String expression) {
	webView.post(() -> {
	    try {
		String escapedExpression = expression.replace("\"", "\\\"");
		String javascript = String.format(
		    "console.log(\"Displaying received Bluetooth expression: %s\"); "
			+ "if (typeof startEvaluation === \"function\") { "
			+
			"  window.currentEvalId = startEvaluation(\"%s\", \"🔗\"); "
			+ "} else { "
			+
			"  console.error(\"startEvaluation function not found\"); "
			+ "}",
		    escapedExpression, escapedExpression);

		Log.d(
		    TAG,
		    "Executing JavaScript for received Bluetooth expression: " +
			expression);
		webView.evaluateJavascript(javascript, jsResult -> {
		    if (jsResult != null) {
			Log.d(TAG, "JavaScript execution result: " + jsResult);
		    }
		});
	    } catch (Exception e) {
		Log.e(TAG,
		      "Error in displayReceivedExpression: " + e.getMessage());
	    }
	});
    }

    private void displayRemoteResult(String expression, String result) {
	webView.post(() -> {
	    try {
		String escapedExpression = expression.replace("\"", "\\\"");
		String escapedResult = result.replace("\"", "\\\"");
		String javascript = String.format(
		    "console.log(\"Displaying Bluetooth result: %s = %s\"); "
			+
			"if (typeof completeEvaluation === \"function\" && window.currentEvalId) { "
			+
			"  completeEvaluation(window.currentEvalId, \"%s\", \"%s\", \"remote\", \"🔗\"); "
			+ "  window.currentEvalId = null; "
			+ "} else { "
			+
			"  console.error(\"completeEvaluation function not found or no currentEvalId\"); "
			+ "}",
		    escapedExpression, escapedResult, escapedExpression,
		    escapedResult);

		Log.d(TAG, "Executing JavaScript for Bluetooth result: " +
			       expression + " = " + result);
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

    private String getStatusType(String status) {
	if (status.contains("connected") || status.contains("Connected")) {
	    return "connected";
	} else if (status.contains("Evaluating") ||
		   status.contains("evaluating")) {
	    return "evaluating";
	} else if (status.contains("failed") || status.contains("error") ||
		   status.contains("disabled")) {
	    return "error";
	} else {
	    return "warning";
	}
    }

    private void handleClientSession() throws IOException {
	while (isRunning.get()) {
	    try {
		byte msgType = readMessageType();

		if (msgType == MSG_TYPE_EXPRESSION) {
		    String expression = readExpressionMessage();
		    if (expression != null && !expression.trim().isEmpty()) {
			Log.i(TAG, "Received expression: " +
				       expression.replace("\n", "\\n"));

			displayReceivedExpression(expression.trim());

			try {
			    evaluationQueue.put(new EvaluationRequest(
				expression.trim(), outputStream));
			} catch (InterruptedException e) {
			    Thread.currentThread().interrupt();
			    break;
			}
		    }
		} else if (msgType == MSG_TYPE_INTERRUPT) {
		    Log.i(TAG, "*** INTERRUPT MESSAGE RECEIVED ***");
		    Log.i(
			TAG,
			"About to submit handleInterrupt() to executor (non-blocking)");
		    Log.i(TAG,
			  "ExecutorService state: shutdown=" +
			      executorService.isShutdown() +
			      " terminated=" + executorService.isTerminated());
		    Log.i(TAG, "MainActivity reference: " +
				   (mainActivity != null ? "VALID" : "NULL"));

		    // Don't block the receiver thread with interrupt handling.
		    final OutputStream currentOutputStream = outputStream;
		    new Thread(() -> {
			try {
			    Log.i(
				TAG,
				"*** EXECUTOR TASK STARTED - About to call interruptScheme() ***");
			    Log.i(TAG,
				  "*** CALLING interruptScheme() (async) ***");
			    String result = chibiScheme.interruptScheme();
			    Log.i(TAG, "*** interruptScheme() returned: " +
					   result + " ***");
			    writeMessage(currentOutputStream, result);
			    Log.i(TAG, "*** Sent interrupt response: " +
					   result + " ***");
			} catch (Exception e) {
			    Log.e(TAG,
				  "Exception in interrupt thread: " +
				      e.getMessage(),
				  e);
			}
		    }).start();
		    Log.i(
			TAG,
			"handleInterrupt() submitted to executor, receiver thread continuing");
		}
	    } catch (IOException e) {
		Log.e(TAG, "Error in message handling: " + e.getMessage());
		throw e;
	    }
	}
    }

    private void handleEvaluations() {
	Log.i(TAG, "Evaluation thread started");
	while (isRunning.get()) {
	    try {
		Log.i(TAG, "Waiting for evaluation request.");
		EvaluationRequest request = evaluationQueue.take();

		updateConnectionStatus("Evaluating expression.");
		Log.i(TAG, "Evaluating expression: " +
			       request.expression.replace("\n", "\\n"));
		String result = chibiScheme.evaluateScheme(request.expression);
		Log.i(TAG, "Evaluated result: " + result.replace("\n", "\\n"));
		updateConnectionStatus("Client connected");

		writeMessage(request.responseStream, result);
		Log.i(TAG, "Response sent successfully");
		displayRemoteResult(request.expression, result);

	    } catch (InterruptedException e) {
		Log.i(TAG, "Evaluation thread interrupted");
		Thread.currentThread().interrupt();
		break;
	    } catch (IOException e) {
		Log.e(TAG, "Error in evaluation handling: " + e.getMessage());
		updateConnectionStatus("Client connected");
		continue;
	    } catch (Exception e) {
		Log.e(TAG, "Unexpected error in evaluation handling: " +
			       e.getMessage());
		e.printStackTrace();
		updateConnectionStatus("Client connected");
	    }
	}
	Log.i(TAG, "Evaluation thread ended");
    }

    private void handleIncomingConnections() {
	while (isRunning.get()) {
	    try {
		updateConnectionStatus("Waiting for client connection");
		Log.i(TAG, "Waiting for Bluetooth client connection.");

		clientSocket = serverSocket.accept();
		Log.i(TAG, "Client connected: " +
			       clientSocket.getRemoteDevice().getAddress());

		inputStream = clientSocket.getInputStream();
		outputStream = clientSocket.getOutputStream();

		updateConnectionStatus("Client connected");
		handleClientSession();

	    } catch (IOException e) {
		if (isRunning.get()) {
		    Log.e(TAG, "Connection error: " + e.getMessage());
		    updateConnectionStatus("Connection failed - " +
					   e.getMessage());
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

    private String readExpressionMessage() throws IOException {
	byte[] lengthBytes = new byte[4];
	int bytesRead = 0;
	while (bytesRead < 4) {
	    int result =
		inputStream.read(lengthBytes, bytesRead, 4 - bytesRead);
	    if (result == -1) {
		throw new IOException("Connection closed while reading length");
	    }
	    bytesRead += result;
	}

	int messageLength =
	    ((lengthBytes[0] & 0xFF) << 24) | ((lengthBytes[1] & 0xFF) << 16) |
	    ((lengthBytes[2] & 0xFF) << 8) | (lengthBytes[3] & 0xFF);

	if (messageLength < 0 || messageLength > MAX_MESSAGE_LENGTH) {
	    throw new IOException("Invalid message length: " + messageLength);
	}

	byte[] messageBytes = new byte[messageLength];
	bytesRead = 0;

	while (bytesRead < messageLength) {
	    int result = inputStream.read(messageBytes, bytesRead,
					  messageLength - bytesRead);
	    if (result == -1) {
		throw new IOException(
		    "Connection closed while reading message");
	    }
	    bytesRead += result;
	}

	return new String(messageBytes, StandardCharsets.UTF_8);
    }

    private byte readMessageType() throws IOException {
	int msgType = inputStream.read();
	if (msgType == -1) {
	    throw new IOException(
		"Connection closed while reading message type");
	}

	Log.i(TAG, "*** RECEIVED MESSAGE TYPE: " + msgType + " ***");
	return (byte)msgType;
    }

    private void writeMessage(OutputStream stream, String message)
	throws IOException {
	byte[] messageBytes = message.getBytes(StandardCharsets.UTF_8);
	int length = messageBytes.length;

	byte[] lengthBytes = new byte[4];
	lengthBytes[0] = (byte)((length >> 24) & 0xFF);
	lengthBytes[1] = (byte)((length >> 16) & 0xFF);
	lengthBytes[2] = (byte)((length >> 8) & 0xFF);
	lengthBytes[3] = (byte)(length & 0xFF);

	stream.write(lengthBytes);
	stream.write(messageBytes);
	stream.flush();
    }

    private void updateConnectionStatus(String status) {
	this.connectionStatus = status;
	webView.post(() -> {
	    String javascript = String.format(
		"(function() { "
		    +
		    "var statusElement = document.querySelector(\".status-bar div\");"
		    +
		    "if (statusElement) statusElement.textContent = \"WebView + Bluetooth REPL: %s\";"
		    + "})();",
		status.replace("\"", "\\\""));
	    webView.evaluateJavascript(javascript, null);
	});

	webView.post(() -> {
	    String javascript = String.format(
		"if (typeof updateConnectionStatusDisplay === \"function\") {"
		    + "  updateConnectionStatusDisplay(\"%s\", \"%s\");"
		    + "}",
		status.replace("\"", "\\\""), getStatusType(status));
	    webView.evaluateJavascript(javascript, null);
	});
    }
}