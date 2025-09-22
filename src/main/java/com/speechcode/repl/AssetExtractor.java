package com.speechcode.repl;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class AssetExtractor {
    private static final String TAG = "repl";

    private static final String[] ESSENTIAL_FILES = {
	"lib/chb/exception-formatter.sld",
	"lib/chibi/ast.scm",
	"lib/chibi/ast.so",
        "lib/chibi/ast.sld",
        "lib/chibi/equiv.sld",
        "lib/chibi/io.sld",
        "lib/chibi/string.sld",
        "lib/init-7.scm",
        "lib/meta-7.scm",
        "lib/scheme/base.sld",
        "lib/scheme/case-lambda.sld",
        "lib/scheme/char.sld",
        "lib/scheme/complex.sld",
        "lib/scheme/cxr.sld",
        "lib/scheme/eval.sld",
        "lib/scheme/file.sld",
        "lib/scheme/inexact.sld",
        "lib/scheme/lazy.sld",
        "lib/scheme/load.sld",
        "lib/scheme/process-context.sld",
        "lib/scheme/r5rs.sld",
        "lib/scheme/read.sld",
        "lib/scheme/repl.sld",
        "lib/scheme/time.sld",
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
        "lib/srfi/11.sld",
        "lib/srfi/27.sld",
        "lib/srfi/27/constructors.scm",
        "lib/srfi/27/rand.so",
        "lib/srfi/39.sld",
        "lib/srfi/9.sld"
    };

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
                Log.e(TAG, "Error extracting " + assetPath + ": " + e.getMessage());
                return false;
            }
        }

        if (count > 0) {
            Log.i(TAG, "Essential file extraction complete: " + count + " files extracted");
            return true;
        } else {
            Log.e(TAG, "No essential files extracted.");
            return false;
        }
    }

    private static boolean extractAssetFile(AssetManager assetManager, String assetPath, String targetPath) throws IOException {
        File targetFile = new File(targetPath);
        File parentDir = targetFile.getParentFile();

        if (parentDir != null && !parentDir.exists() && !parentDir.mkdirs()) {
            Log.e(TAG, "Failed to create parent directory: " + parentDir.getAbsolutePath());
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
                    Log.w(TAG, "Failed to set executable permissions on: " + targetPath);
                }
            }

            Log.d(TAG, "Extracted " + assetPath + " (" + totalBytes + " bytes)");
            return true;

        } catch (IOException e) {
            Log.e(TAG, "IOException extracting " + assetPath + ": " + e.getMessage());
            if (targetFile.exists()) {
                targetFile.delete();
            }
            throw e;
        }
    }
}