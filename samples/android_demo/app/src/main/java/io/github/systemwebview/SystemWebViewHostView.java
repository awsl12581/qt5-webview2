package io.github.systemwebview;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.AttributeSet;
import android.view.ViewGroup;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

import androidx.webkit.JavaScriptReplyProxy;
import androidx.webkit.ScriptHandler;
import androidx.webkit.WebMessageCompat;
import androidx.webkit.WebResourceRequestCompat;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;

import java.io.ByteArrayInputStream;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Android host adapter for the portable System WebView contract.
 *
 * <p>The native backend owns navigation, bridge, and resource decisions. This
 * view only creates the real {@link WebView}, forwards callbacks, and executes
 * the operations the backend requests. Local bundle documents are served from
 * {@code https://appassets.androidplatform.net/<application-id>/...}; the native
 * side maps that origin back to {@code app://<application-id>}.
 */
public final class SystemWebViewHostView extends FrameLayout {
    /** Receives state changes forwarded from the native backend. */
    public interface Listener {
        void onLoad(int state, String url, String error);

        void onBridgeEvent(String type, String payloadJson);

        void onExternalUrl(String url);
    }

    static final String ASSET_ORIGIN = "https://appassets.androidplatform.net";
    private static final String BRIDGE_OBJECT = "systemWebViewNative";
    private static final long INTERCEPT_TIMEOUT_SECONDS = 5;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private volatile long nativeHandle;
    private WebView webView;
    private ScriptHandler documentScript;
    private String profileName;
    private Listener listener;

    public SystemWebViewHostView(Context context) {
        super(context);
        SystemWebViewJni.attachContext(context);
        setBackgroundColor(Color.WHITE);
    }

    public SystemWebViewHostView(Context context, AttributeSet attributes) {
        super(context, attributes);
        SystemWebViewJni.attachContext(context);
        setBackgroundColor(Color.WHITE);
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void setNativeHandle(long handle) {
        nativeHandle = handle;
    }

    public void setProfileName(String name) {
        profileName = name;
    }

    public String assetOrigin() {
        return ASSET_ORIGIN;
    }

    public void attachWebView() {
        ensureWebView();
        if (webView.getParent() == null) {
            addView(webView, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
        }
        webView.onResume();
    }

    public void detachWebView() {
        if (webView != null) {
            webView.onPause();
            if (webView.getParent() instanceof ViewGroup) {
                ((ViewGroup) webView.getParent()).removeView(webView);
            }
        }
    }

    public void loadUrl(String url) {
        ensureWebView();
        webView.loadUrl(url);
    }

    public void loadDataWithBaseUrl(String url, String html) {
        ensureWebView();
        webView.loadDataWithBaseURL(url, html, "text/html", "UTF-8", null);
    }

    public void stopLoading() {
        if (webView != null) {
            webView.stopLoading();
        }
    }

    public void reloadPage() {
        if (webView != null) {
            webView.reload();
        }
    }

    public boolean canGoBack() {
        return webView != null && webView.canGoBack();
    }

    public void goBack() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        }
    }

    public boolean canGoForward() {
        return webView != null && webView.canGoForward();
    }

    public void goForward() {
        if (webView != null && webView.canGoForward()) {
            webView.goForward();
        }
    }

    public void installDocumentScript(String script) {
        ensureWebView();
        if (!WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) {
            return;
        }
        if (documentScript != null) {
            documentScript.remove();
            documentScript = null;
        }
        documentScript = WebViewCompat.addDocumentStartJavaScript(webView, script, Collections.singleton(ASSET_ORIGIN));
    }

    public void postToPage(String message) {
        if (webView == null) {
            return;
        }
        webView.evaluateJavascript("window.__systemWebViewReceive(" + message + ");", null);
    }

    public void destroyWebView() {
        if (documentScript != null) {
            try {
                documentScript.remove();
            } catch (RuntimeException ignored) {
            }
            documentScript = null;
        }
        final WebView current = webView;
        if (current == null) {
            return;
        }
        webView = null;
        if (current.getParent() instanceof ViewGroup) {
            ((ViewGroup) current.getParent()).removeView(current);
        }
        if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            try {
                WebViewCompat.removeWebMessageListener(current, BRIDGE_OBJECT);
            } catch (RuntimeException ignored) {
            }
        }
        current.stopLoading();
        current.setWebViewClient(new WebViewClient());
        current.setWebChromeClient(new WebChromeClient());
        current.destroy();
    }

    // Called by the native backend on the Java main thread.
    public void dispatchLoad(int state, String url, String error) {
        final Listener current = listener;
        if (current != null) {
            current.onLoad(state, url, error);
        }
    }

    public void dispatchBridgeEvent(String type, String payloadJson) {
        final Listener current = listener;
        if (current != null) {
            current.onBridgeEvent(type, payloadJson);
        }
    }

    public void openExternal(String url) {
        final Listener current = listener;
        if (current != null) {
            current.onExternalUrl(url);
        }
        try {
            final Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(intent);
        } catch (ActivityNotFoundException | IllegalArgumentException ignored) {
        }
    }

    private void ensureWebView() {
        if (webView != null) {
            return;
        }
        final WebView created = new WebView(getContext());
        if (profileName != null && WebViewFeature.isFeatureSupported(WebViewFeature.MULTI_PROFILE)) {
            WebViewCompat.setProfile(created, profileName);
        }
        final WebSettings settings = created.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        created.setBackgroundColor(Color.WHITE);
        created.setWebViewClient(new HostClient());
        created.setWebChromeClient(new HostChromeClient());
        if (WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            WebViewCompat.addWebMessageListener(
                    created, BRIDGE_OBJECT, Collections.singleton(ASSET_ORIGIN), this::onWebMessage);
        }
        webView = created;
        SystemWebViewJni.registerWebView(created);
    }

    private void onWebMessage(
            WebView view, WebMessageCompat message, Uri sourceOrigin, boolean isMainFrame, JavaScriptReplyProxy replyProxy) {
        if (message.getType() != WebMessageCompat.TYPE_STRING) {
            return;
        }
        final String data = message.getData();
        final long handle = nativeHandle;
        if (data == null || handle == 0) {
            return;
        }
        SystemWebViewJni.nativeOnMessage(
                handle, data, sourceOrigin == null ? "" : sourceOrigin.toString(), isMainFrame);
    }

    private Object[] interceptOnMainThread(String url, boolean mainFrame) {
        final long handle = nativeHandle;
        if (handle == 0) {
            return null;
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            return SystemWebViewJni.nativeIntercept(handle, url, mainFrame);
        }
        final Object[][] holder = new Object[1][];
        final CountDownLatch latch = new CountDownLatch(1);
        final boolean posted = mainHandler.post(() -> {
            try {
                holder[0] = nativeHandle == 0 ? null : SystemWebViewJni.nativeIntercept(nativeHandle, url, mainFrame);
            } finally {
                latch.countDown();
            }
        });
        if (!posted) {
            return null;
        }
        try {
            if (!latch.await(INTERCEPT_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                return null;
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            return null;
        }
        return holder[0];
    }

    private static WebResourceResponse buildResponse(Object[] result) {
        if (result == null) {
            return null;
        }
        final int status = result[0] instanceof Integer ? (Integer) result[0] : 404;
        final String mime = result[1] instanceof String ? (String) result[1] : "application/octet-stream";
        final byte[] body = result[2] instanceof byte[] ? (byte[]) result[2] : new byte[0];
        final String policy = result[3] instanceof String ? (String) result[3] : null;
        final Map<String, String> headers = new HashMap<>();
        headers.put("Cache-Control", "no-store");
        headers.put("X-Content-Type-Options", "nosniff");
        if (policy != null && !policy.isEmpty()) {
            headers.put("Content-Security-Policy", policy);
        }
        final String encoding = mime.startsWith("text/") || mime.equals("application/json")
                        || mime.equals("application/xml") || mime.equals("image/svg+xml")
                ? "UTF-8"
                : null;
        return new WebResourceResponse(
                mime, encoding, status, reasonPhrase(status), headers, new ByteArrayInputStream(body));
    }

    private static String reasonPhrase(int status) {
        switch (status) {
            case 200:
                return "OK";
            case 206:
                return "Partial Content";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 410:
                return "Gone";
            case 416:
                return "Range Not Satisfiable";
            case 500:
                return "Read Error";
            default:
                return "Response";
        }
    }

    private final class HostClient extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            final long handle = nativeHandle;
            if (handle == 0) {
                return true;
            }
            final boolean redirect = WebViewFeature.isFeatureSupported(WebViewFeature.WEB_RESOURCE_REQUEST_IS_REDIRECT)
                    && WebResourceRequestCompat.isRedirect(request);
            final int decision = SystemWebViewJni.nativeShouldOverrideUrlLoading(
                    handle,
                    request.getUrl().toString(),
                    request.isForMainFrame(),
                    request.hasGesture(),
                    redirect);
            return decision != 0;
        }

        @Override
        public void onPageStarted(WebView view, String url, Bitmap favicon) {
            final long handle = nativeHandle;
            if (handle != 0) {
                SystemWebViewJni.nativeOnPageStarted(handle, url);
            }
        }

        @Override
        public void onPageCommitVisible(WebView view, String url) {
            final long handle = nativeHandle;
            if (handle != 0) {
                SystemWebViewJni.nativeOnPageCommitVisible(handle, url);
            }
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            final long handle = nativeHandle;
            if (handle != 0) {
                SystemWebViewJni.nativeOnPageFinished(handle, url);
            }
        }

        @Override
        public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
            final long handle = nativeHandle;
            if (handle == 0 || !request.isForMainFrame()) {
                return;
            }
            final CharSequence description = error.getDescription();
            SystemWebViewJni.nativeOnReceivedError(
                    handle,
                    request.getUrl().toString(),
                    true,
                    error.getErrorCode(),
                    description == null ? "" : description.toString());
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            final Object[] result = interceptOnMainThread(request.getUrl().toString(), request.isForMainFrame());
            return buildResponse(result);
        }

        @Override
        public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
            final long handle = nativeHandle;
            if (handle != 0) {
                SystemWebViewJni.nativeOnRenderProcessGone(handle, detail.didCrash() ? 1 : 0);
            }
            mainHandler.post(SystemWebViewHostView.this::destroyWebView);
            return true;
        }
    }

    private static final class HostChromeClient extends WebChromeClient {
        @Override
        public boolean onCreateWindow(WebView view, boolean isDialog, boolean isUserGesture, Message resultMsg) {
            // The demo backend has no popup host; window.open is blocked by policy.
            return false;
        }
    }
}
