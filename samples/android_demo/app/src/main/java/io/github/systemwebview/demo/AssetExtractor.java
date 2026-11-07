package io.github.systemwebview.demo;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Copies a bundled web application from APK assets into the app files directory.
 * The portable {@code LocalBundle} source requires a real directory, so the demo
 * materializes the assets once per installed build.
 */
final class AssetExtractor {
    private static final String VERSION_FILE = ".asset-version";

    private AssetExtractor() {
    }

    static void extractIfNeeded(Context context, String assetRoot, File targetDirectory) throws IOException {
        final String version = installedVersion(context);
        final File marker = new File(targetDirectory, VERSION_FILE);
        if (marker.isFile() && version.equals(readText(marker))) {
            return;
        }
        deleteRecursively(targetDirectory);
        if (!targetDirectory.mkdirs() && !targetDirectory.isDirectory()) {
            throw new IOException("无法创建目录 " + targetDirectory);
        }
        copyTree(context.getAssets(), assetRoot, targetDirectory);
        try (OutputStream markerStream = new FileOutputStream(marker)) {
            markerStream.write(version.getBytes("UTF-8"));
        }
    }

    private static void copyTree(AssetManager assets, String assetPath, File targetDirectory) throws IOException {
        final String[] children = assets.list(assetPath);
        if (children == null || children.length == 0) {
            copyFile(assets, assetPath, targetDirectory);
            return;
        }
        for (String child : children) {
            final String childPath = assetPath.isEmpty() ? child : assetPath + "/" + child;
            final String[] nested = assets.list(childPath);
            if (nested != null && nested.length > 0) {
                final File directory = new File(targetDirectory, child);
                if (!directory.mkdirs() && !directory.isDirectory()) {
                    throw new IOException("无法创建目录 " + directory);
                }
                copyTree(assets, childPath, directory);
            } else {
                copyFile(assets, childPath, new File(targetDirectory, child));
            }
        }
    }

    private static void copyFile(AssetManager assets, String assetPath, File target) throws IOException {
        final File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("无法创建目录 " + parent);
        }
        try (InputStream input = assets.open(assetPath); OutputStream output = new FileOutputStream(target)) {
            final byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = input.read(buffer)) > 0) {
                output.write(buffer, 0, read);
            }
        }
    }

    private static String installedVersion(Context context) {
        try {
            return Long.toString(context.getPackageManager().getPackageInfo(context.getPackageName(), 0).lastUpdateTime);
        } catch (PackageManager.NameNotFoundException notFound) {
            return "0";
        }
    }

    private static String readText(File file) {
        try (InputStream input = new java.io.FileInputStream(file)) {
            final byte[] buffer = new byte[64];
            final int read = input.read(buffer);
            return read <= 0 ? "" : new String(buffer, 0, read, "UTF-8");
        } catch (IOException failure) {
            return "";
        }
    }

    private static void deleteRecursively(File file) {
        if (!file.exists()) {
            return;
        }
        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        if (!file.delete()) {
            file.deleteOnExit();
        }
    }
}
