package io.github.systemwebview;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.webkit.CookieManager;
import android.webkit.WebStorage;
import android.webkit.WebView;
import android.webkit.WebViewDatabase;

import androidx.webkit.WebViewFeature;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/**
 * Small JNI surface shared by the native Android backend and the Java host
 * adapter. Native methods receive an opaque view handle created by the backend;
 * unknown handles are ignored so late callbacks cannot resurrect a closed view.
 */
public final class SystemWebViewJni {
    static {
        System.loadLibrary("system_webview");
    }

    private static final int CLEAR_CACHE = 0;
    private static final int CLEAR_COOKIES = 1;
    private static final int CLEAR_ALL = 2;

    private static final List<WeakReference<WebView>> LIVE_VIEWS = new ArrayList<>();
    private static Context applicationContext;

    private SystemWebViewJni() {
    }

    // Native callbacks. They must run on the Java main thread.
    static native void nativeOnPageStarted(long handle, String url);

    static native void nativeOnPageCommitVisible(long handle, String url);

    static native void nativeOnPageFinished(long handle, String url);

    static native void nativeOnReceivedError(long handle, String url, boolean mainFrame, int code, String description);

    static native int nativeShouldOverrideUrlLoading(
            long handle, String url, boolean mainFrame, boolean userInitiated, boolean redirect);

    static native void nativeOnMessage(long handle, String message, String sourceOrigin, boolean mainFrame);

    static native Object[] nativeIntercept(long handle, String url, boolean mainFrame);

    static native void nativeOnRenderProcessGone(long handle, int code);

    static native void nativeOnClearComplete(long requestId, boolean success);

    static boolean isMainThread() {
        return Looper.myLooper() == Looper.getMainLooper();
    }

    static boolean supportsFeature(String feature) {
        return WebViewFeature.isFeatureSupported(feature);
    }

    static void attachContext(Context context) {
        if (applicationContext == null && context != null) {
            applicationContext = context.getApplicationContext();
        }
    }

    static void registerWebView(WebView webView) {
        synchronized (LIVE_VIEWS) {
            final Iterator<WeakReference<WebView>> iterator = LIVE_VIEWS.iterator();
            while (iterator.hasNext()) {
                if (iterator.next().get() == null) {
                    iterator.remove();
                }
            }
            LIVE_VIEWS.add(new WeakReference<>(webView));
        }
    }

    static void clearBrowsingData(long requestId, int kind) {
        final Handler main = new Handler(Looper.getMainLooper());
        main.post(() -> {
            if (kind == CLEAR_CACHE || kind == CLEAR_ALL) {
                clearCaches();
            }
            if (kind == CLEAR_COOKIES || kind == CLEAR_ALL) {
                final CookieManager cookies = CookieManager.getInstance();
                if (kind == CLEAR_ALL) {
                    cookies.removeAllCookies(success -> {
                        WebStorage.getInstance().deleteAllData();
                        clearWebViewDatabase();
                        cookies.flush();
                        nativeOnClearComplete(requestId, success);
                    });
                } else {
                    cookies.removeAllCookies(success -> nativeOnClearComplete(requestId, success));
                }
                cookies.flush();
            }
            if (kind == CLEAR_CACHE) {
                nativeOnClearComplete(requestId, true);
            }
        });
    }

    private static void clearCaches() {
        synchronized (LIVE_VIEWS) {
            for (WeakReference<WebView> reference : LIVE_VIEWS) {
                final WebView webView = reference.get();
                if (webView != null) {
                    webView.clearCache(true);
                }
            }
        }
    }

    private static void clearWebViewDatabase() {
        if (applicationContext == null) {
            return;
        }
        try {
            final WebViewDatabase database = WebViewDatabase.getInstance(applicationContext);
            database.clearHttpAuthUsernamePassword();
            database.clearFormData();
        } catch (RuntimeException ignored) {
        }
    }
}
