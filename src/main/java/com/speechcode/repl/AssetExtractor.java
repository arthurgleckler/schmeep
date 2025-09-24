package com.speechcode.repl;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class AssetExtractor {
    private static final String TAG = "repl";

    private static final String[] ESSENTIAL_FILES = {
	"lib/chb/exception-formatter.sld",
	"lib/chibi/ast.scm",
	"lib/chibi/ast.sld",
	"lib/chibi/ast.so",
	"lib/chibi/disasm.sld",
	"lib/chibi/disasm.so",
	"lib/chibi/equiv.scm",
	"lib/chibi/equiv.sld",
	"lib/chibi/heap-stats.sld",
	"lib/chibi/heap-stats.so",
	"lib/chibi/io.sld",
	"lib/chibi/io/io.scm",
	"lib/chibi/io/port.so",
	"lib/chibi/json.scm",
	"lib/chibi/json.sld",
	"lib/chibi/json.so",
	"lib/chibi/optimize.scm",
	"lib/chibi/optimize/profile.scm",
	"lib/chibi/optimize/profile.so",
	"lib/chibi/optimize/rest.scm",
	"lib/chibi/optimize/rest.so",
	"lib/chibi/string.scm",
	"lib/chibi/string.sld",
	"lib/chibi/weak.sld",
	"lib/chibi/weak.so",
	"lib/init-7.scm",
	"lib/meta-7.scm",
	"lib/scheme/base.sld",
	"lib/scheme/case-lambda.sld",
	"lib/scheme/char.sld",
	"lib/scheme/complex.sld",
	"lib/scheme/cxr.scm",
	"lib/scheme/cxr.sld",
	"lib/scheme/eval.sld",
	"lib/scheme/file.sld",
	"lib/scheme/inexact.scm",
	"lib/scheme/inexact.sld",
	"lib/scheme/lazy.sld",
	"lib/scheme/load.sld",
	"lib/scheme/process-context.sld",
	"lib/scheme/r5rs.sld",
	"lib/scheme/read.sld",
	"lib/scheme/repl.sld",
	"lib/scheme/time.sld",
	"lib/scheme/time.so",
	"lib/scheme/write.sld",
	"lib/srfi/1.sld",
	"lib/srfi/1/alists.scm",
	"lib/srfi/1/constructors.scm",
	"lib/srfi/1/deletion.scm",
	"lib/srfi/1/fold.scm",
	"lib/srfi/1/lset.scm",
	"lib/srfi/1/misc.scm",
	"lib/srfi/1/predicates.scm",
	"lib/srfi/1/search.scm",
	"lib/srfi/1/selectors.scm",
	"lib/srfi/9.scm",
	"lib/srfi/9.sld",
	"lib/srfi/11.sld",
	"lib/srfi/18.sld",
	"lib/srfi/18/interface.scm",
	"lib/srfi/18/threads.so",
	"lib/srfi/18/types.scm",
	"lib/srfi/27.sld",
	"lib/srfi/27/constructors.scm",
	"lib/srfi/27/rand.so",
	"lib/srfi/39.sld",
	"lib/srfi/39/param.so",
	"lib/srfi/39/syntax.scm",
	"lib/srfi/69.sld",
	"lib/srfi/69/hash.so",
	"lib/srfi/69/interface.scm",
	"lib/srfi/69/type.scm",
	"lib/srfi/95.sld",
	"lib/srfi/95/qsort.so",
	"lib/srfi/95/sort.scm",
	"lib/srfi/98.sld",
	"lib/srfi/98/env.so",
	"lib/srfi/144.sld",
	"lib/srfi/144/flonum.scm",
	"lib/srfi/144/lgamma_r.so",
	"lib/srfi/151.sld",
	"lib/srfi/151/bit.so",
	"lib/srfi/151/bitwise.scm"};

    public static boolean extractAssets(Context context) {
	AssetManager assetManager = context.getAssets();
	String targetBase = "/data/data/com.speechcode.repl/lib";

	File baseDir = new File(targetBase);
	if (!baseDir.exists() && !baseDir.mkdirs()) {
	    Log.e(TAG, "Failed to create base directory: " + targetBase);
	    return false;
	}

	Log.i(TAG, "Starting essential Scheme library extraction.");

	int count = 0;
	for (String assetPath : ESSENTIAL_FILES) {
	    String extractPath = assetPath.substring(4); // Skip "lib/" prefix
	    String targetPath = targetBase + "/" + extractPath;

	    try {
		if (extractAssetFile(assetManager, assetPath, targetPath)) {
		    count++;
		    if (extractPath.endsWith(".so")) {
			Log.i(TAG, "Extracted shared library: " + extractPath);
		    } else {
			Log.i(TAG, "Extracted essential file: " + extractPath);
		    }
		} else {
		    Log.e(TAG, "Failed to extract: " + assetPath);
		    return false;
		}
	    } catch (IOException e) {
		Log.e(TAG,
		      "Error extracting " + assetPath + ": " + e.getMessage());
		return false;
	    }
	}

	if (count > 0) {
	    Log.i(TAG, "Essential file extraction complete: " + count +
			   " files extracted.");
	    return true;
	} else {
	    Log.e(TAG, "No essential files extracted.");
	    return false;
	}
    }

    private static boolean extractAssetFile(AssetManager assetManager,
					    String assetPath, String targetPath)
	throws IOException {
	File targetFile = new File(targetPath);
	File parentDir = targetFile.getParentFile();

	if (parentDir != null && !parentDir.exists() && !parentDir.mkdirs()) {
	    Log.e(TAG, "Failed to create parent directory: " +
			   parentDir.getAbsolutePath());
	    return false;
	}

	try (InputStream inputStream = assetManager.open(assetPath);
	     FileOutputStream outputStream = new FileOutputStream(targetFile)) {

	    byte[] buffer = new byte[8192];
	    int totalBytes = 0;
	    int bytesRead;

	    while ((bytesRead = inputStream.read(buffer)) != -1) {
		outputStream.write(buffer, 0, bytesRead);
		totalBytes += bytesRead;
	    }

	    outputStream.flush();

	    if (targetPath.endsWith(".so")) {
		if (!targetFile.setExecutable(true)) {
		    Log.w(TAG, "Failed to set executable permissions on: " +
				   targetPath);
		}
	    }

	    Log.d(TAG,
		  "Extracted " + assetPath + " (" + totalBytes + " bytes).");
	    return true;

	} catch (IOException e) {
	    Log.e(TAG, "IOException extracting " + assetPath + ": " +
			   e.getMessage());
	    if (targetFile.exists()) {
		targetFile.delete();
	    }
	    throw e;
	}
    }

    public static void handleAssetExtraction(Context context) {
	try {
	    if (shouldExtractAssets(context)) {
		Log.i(TAG, "Extracting assets based on version check.");
		if (extractAssets(context)) {
		    markAssetsExtracted(context);
		} else {
		    Log.e(
			TAG,
			"Asset extraction failed.  Continuing with basic environment.");
		}
	    } else {
		Log.i(TAG, "Version unchanged.  Skipping asset extraction.");
	    }
	} catch (Exception e) {
	    Log.e(TAG, "Error during asset extraction: " + e.getMessage());
	}
    }

    public static void markAssetsExtracted(Context context) {
	try {
	    PackageInfo packageInfo =
		context.getPackageManager().getPackageInfo(
		    context.getPackageName(), 0);
	    long currentVersionCode = packageInfo.versionCode;

	    File libDir = new File("/data/data/com.speechcode.repl/lib");
	    if (!libDir.exists()) {
		libDir.mkdirs();
	    }

	    File markerFile = new File(libDir, ".assets_timestamp");
	    try (FileOutputStream fos = new FileOutputStream(markerFile)) {
		fos.write(String.valueOf(currentVersionCode).getBytes());
		fos.flush();
	    }

	    Log.i(TAG, "Asset extraction completed for version " +
			   currentVersionCode + ".");
	} catch (Exception e) {
	    Log.e(TAG, "Error marking assets extracted: " + e.getMessage());
	}
    }

    public static boolean shouldExtractAssets(Context context) {
	try {
	    PackageInfo packageInfo =
		context.getPackageManager().getPackageInfo(
		    context.getPackageName(), 0);
	    long currentVersionCode = packageInfo.versionCode;

	    File markerFile = new File(
		"/data/data/com.speechcode.repl/lib/.assets_timestamp");

	    if (!markerFile.exists()) {
		Log.i(TAG,
		      "Marker file doesn't exist.  Need to extract assets.");
		return true;
	    }

	    try (FileInputStream fis = new FileInputStream(markerFile)) {
		byte[] buffer = new byte[20];
		int bytesRead = fis.read(buffer);

		if (bytesRead <= 0) {
		    Log.i(TAG,
			  "Marker file is empty.  Need to extract assets.");
		    return true;
		}

		String storedVersionString =
		    new String(buffer, 0, bytesRead).trim();
		long storedVersionCode = Long.parseLong(storedVersionString);

		if (currentVersionCode != storedVersionCode) {
		    Log.i(TAG, "Version changed from " + storedVersionCode +
				   " to " + currentVersionCode +
				   ".  Need to extract assets.");
		    return true;
		} else {
		    Log.i(TAG, "Version unchanged (" + currentVersionCode +
				   ").  Skipping asset extraction.");
		    return false;
		}
	    }
	} catch (Exception e) {
	    Log.w(TAG,
		  "Error checking asset extraction status: " + e.getMessage());
	    return true;
	}
    }
}