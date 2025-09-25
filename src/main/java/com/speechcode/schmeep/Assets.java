package com.speechcode.schmeep;

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
import java.nio.file.Files;

public class Assets {
    private static final String TAG = "schmeep";

    private static final String[] ESSENTIAL_FILES = {
	"schmeep/exception-formatter.sld",
	"chibi/ast.scm",
	"chibi/ast.sld",
	"chibi/ast.so",
	"chibi/disasm.sld",
	"chibi/disasm.so",
	"chibi/equiv.scm",
	"chibi/equiv.sld",
	"chibi/heap-stats.sld",
	"chibi/heap-stats.so",
	"chibi/io.sld",
	"chibi/io/io.scm",
	"chibi/io/port.so",
	"chibi/json.scm",
	"chibi/json.sld",
	"chibi/json.so",
	"chibi/optimize.scm",
	"chibi/optimize/profile.scm",
	"chibi/optimize/profile.so",
	"chibi/optimize/rest.scm",
	"chibi/optimize/rest.so",
	"chibi/string.scm",
	"chibi/string.sld",
	"chibi/weak.sld",
	"chibi/weak.so",
	"init-7.scm",
	"meta-7.scm",
	"scheme/base.sld",
	"scheme/case-lambda.sld",
	"scheme/char.sld",
	"scheme/complex.sld",
	"scheme/cxr.scm",
	"scheme/cxr.sld",
	"scheme/eval.sld",
	"scheme/file.sld",
	"scheme/inexact.scm",
	"scheme/inexact.sld",
	"scheme/lazy.sld",
	"scheme/load.sld",
	"scheme/process-context.sld",
	"scheme/r5rs.sld",
	"scheme/read.sld",
	"scheme/repl.sld",
	"scheme/time.sld",
	"scheme/time.so",
	"scheme/write.sld",
	"srfi/1.sld",
	"srfi/1/alists.scm",
	"srfi/1/constructors.scm",
	"srfi/1/deletion.scm",
	"srfi/1/fold.scm",
	"srfi/1/lset.scm",
	"srfi/1/misc.scm",
	"srfi/1/predicates.scm",
	"srfi/1/search.scm",
	"srfi/1/selectors.scm",
	"srfi/9.scm",
	"srfi/9.sld",
	"srfi/11.sld",
	"srfi/18.sld",
	"srfi/18/interface.scm",
	"srfi/18/threads.so",
	"srfi/18/types.scm",
	"srfi/27.sld",
	"srfi/27/constructors.scm",
	"srfi/27/rand.so",
	"srfi/39.sld",
	"srfi/39/param.so",
	"srfi/39/syntax.scm",
	"srfi/69.sld",
	"srfi/69/hash.so",
	"srfi/69/interface.scm",
	"srfi/69/type.scm",
	"srfi/95.sld",
	"srfi/95/qsort.so",
	"srfi/95/sort.scm",
	"srfi/98.sld",
	"srfi/98/env.so",
	"srfi/144.sld",
	"srfi/144/flonum.scm",
	"srfi/144/lgamma_r.so",
	"srfi/151.sld",
	"srfi/151/bit.so",
	"srfi/151/bitwise.scm"};

    private static boolean emptyDirectory(File dir) {
	File[] files = dir.listFiles();

	if (files != null) {
	    for (File file : files) {
		if (file.isDirectory()) {
		    if (!emptyDirectory(file)) {
			Log.e(TAG, "Failed to delete: " + file.getAbsolutePath());
			return false;
		    }
		}
		if (!file.delete()) {
		    Log.e(TAG, "Failed to delete: " + file.getAbsolutePath());
		    return false;
		}
	    }
	}
	return true;
    }

    public static boolean extractAssets(Context context) {
	AssetManager assetManager = context.getAssets();
	String targetBase = "/data/data/com.speechcode.schmeep/lib";

	File baseDir = new File(targetBase);

	if (baseDir.exists()) {
	    if (!emptyDirectory(baseDir)) {
		Log.i(TAG, "Failed to empty directory: " + baseDir.getAbsolutePath());
		return false;
	    }
	    Log.i(TAG, "Emptied directory: " + baseDir.getAbsolutePath());
	} else if (!baseDir.mkdirs()) {
	    Log.e(TAG, "Failed to create base directory: " + targetBase);
	    return false;
	}

	Log.i(TAG, "Starting essential Scheme library extraction.");

	int count = 0;
	for (String assetPath : ESSENTIAL_FILES) {
	    String targetPath = targetBase + "/" + assetPath;

	    try {
		if (extractAssetFile(assetManager, "lib/" + assetPath,
				     targetPath)) {
		    count++;
		    if (assetPath.endsWith(".so")) {
			Log.i(TAG, "Extracted shared library: " + assetPath);
		    } else {
			Log.i(TAG, "Extracted essential file: " + assetPath);
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
	    long totalBytes = inputStream.transferTo(outputStream);

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
	    File libDir = new File("/data/data/com.speechcode.schmeep/lib");

	    if (!libDir.exists()) {
		libDir.mkdirs();
	    }

	    File markerFile = new File(libDir, ".assets_timestamp");

	    try (FileOutputStream fos = new FileOutputStream(markerFile)) {
		fos.write(String.valueOf(currentVersionCode).getBytes());
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
		"/data/data/com.speechcode.schmeep/lib/.assets_timestamp");
	    String storedVersionString;

	    try (FileInputStream fis = new FileInputStream(markerFile)) {
		byte[] data = new byte[(int)markerFile.length()];
		int totalRead = 0;
		int bytesRead;

		while (totalRead < data.length &&
		       (bytesRead = fis.read(data, totalRead,
					     data.length - totalRead)) != -1) {
		    totalRead += bytesRead;
		}
		storedVersionString = new String(data, 0, totalRead).trim();
	    }

	    if (storedVersionString.isEmpty()) {
		Log.i(TAG, "Marker file is empty.  Must extract assets.");
		return true;
	    }

	    long storedVersionCode = Long.parseLong(storedVersionString);

	    if (currentVersionCode != storedVersionCode) {
		Log.i(TAG, "Version changed from " + storedVersionCode +
			       " to " + currentVersionCode +
			       ".  Must extract assets.");
		return true;
	    } else {
		Log.i(TAG, "Version unchanged (" + currentVersionCode +
			       ").  Skipping asset extraction.");
		return false;
	    }
	} catch (Exception e) {
	    Log.w(TAG,
		  "Error checking asset extraction status: " + e.getMessage());
	    return true;
	}
    }
}