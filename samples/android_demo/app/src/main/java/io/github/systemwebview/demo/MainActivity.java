package io.github.systemwebview.demo;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import io.github.systemwebview.SystemWebViewHostView;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;

/**
 * Android demo host. It mirrors the desktop Qt sample: the native library owns
 * the page, policy, and bridge, while the activity supplies the app shell and a
 * local bundle directory extracted from APK assets.
 */
public final class MainActivity extends Activity {
    static {
        System.loadLibrary("system_webview");
        System.loadLibrary("system_webview_android_demo");
    }

    private static final String BUNDLE_ASSET = "demo";
    private static final String BUNDLE_ID = "demo";

    private SystemWebViewHostView hostView;
    private TextView statusView;
    private TextView titleView;
    private Button backButton;
    private boolean started;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final File bundleDirectory = new File(new File(getFilesDir(), "webview"), BUNDLE_ID);
        try {
            AssetExtractor.extractIfNeeded(this, BUNDLE_ASSET, bundleDirectory);
        } catch (IOException failure) {
            showFatal("资源解包失败：" + failure.getMessage());
            return;
        }
        setContentView(buildContentView(bundleDirectory));
        startSession(bundleDirectory);
    }

    @Override
    protected void onDestroy() {
        if (started) {
            started = false;
            nativeStop();
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (started && nativeCanGoBack()) {
            nativeGoBack();
            updateNavigationState();
            return;
        }
        super.onBackPressed();
    }

    private View buildContentView(File bundleDirectory) {
        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setFitsSystemWindows(true);
        root.setBackgroundColor(Color.WHITE);

        final LinearLayout toolbar = new LinearLayout(this);
        toolbar.setOrientation(LinearLayout.HORIZONTAL);
        toolbar.setGravity(Gravity.CENTER_VERTICAL);
        toolbar.setPadding(dp(8), dp(4), dp(8), dp(4));
        toolbar.setBackgroundColor(Color.rgb(248, 250, 251));

        backButton = new Button(this);
        backButton.setText(R.string.back);
        backButton.setAllCaps(false);
        backButton.setOnClickListener(view -> {
            if (started && nativeCanGoBack()) {
                nativeGoBack();
                updateNavigationState();
            }
        });
        toolbar.addView(backButton);

        final Button reload = new Button(this);
        reload.setText(R.string.reload);
        reload.setAllCaps(false);
        reload.setOnClickListener(view -> {
            if (started) {
                nativeReload();
            }
        });
        toolbar.addView(reload);

        titleView = new TextView(this);
        titleView.setText(R.string.app_name);
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        titleView.setGravity(Gravity.CENTER_VERTICAL);
        titleView.setPadding(dp(12), 0, 0, 0);
        toolbar.addView(titleView, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));

        root.addView(toolbar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)));

        hostView = new SystemWebViewHostView(this);
        hostView.setListener(new SystemWebViewHostView.Listener() {
            @Override
            public void onLoad(int state, String url, String error) {
                updateStatus(state, url, error);
                updateNavigationState();
            }

            @Override
            public void onBridgeEvent(String type, String payloadJson) {
                handleBridgeEvent(type, payloadJson);
            }

            @Override
            public void onExternalUrl(String url) {
                statusView.setText("外部链接：" + url);
            }
        });
        root.addView(hostView, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        statusView = new TextView(this);
        statusView.setText(R.string.status_ready);
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        statusView.setPadding(dp(12), dp(4), dp(12), dp(4));
        statusView.setBackgroundColor(Color.rgb(238, 241, 244));
        root.addView(statusView, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(28)));
        return root;
    }

    private void startSession(File bundleDirectory) {
        started = true;
        nativeStart(hostView, bundleDirectory.getAbsolutePath());
    }

    private void updateStatus(int state, String url, String error) {
        Log.i("SystemWebViewDemo", "load state=" + state + " url=" + url + (error.isEmpty() ? "" : " error=" + error));
        switch (state) {
            case 0:
                statusView.setText("加载中 " + shorten(url));
                break;
            case 1:
                statusView.setText("重定向 " + shorten(url));
                break;
            case 2:
                statusView.setText("已提交 " + shorten(url));
                break;
            case 3:
                statusView.setText("已加载 " + shorten(url));
                break;
            case 4:
                statusView.setText("加载失败：" + error);
                break;
            default:
                break;
        }
    }

    private void handleBridgeEvent(String type, String payloadJson) {
        if (!"window".equals(type)) {
            statusView.setText("页面消息：" + type + " " + payloadJson);
            return;
        }
        try {
            final JSONObject payload = new JSONObject(payloadJson);
            final String action = payload.optString("action");
            switch (action) {
                case "close":
                    finish();
                    break;
                case "minimize":
                    moveTaskToBack(true);
                    break;
                default:
                    statusView.setText("窗口操作：" + action);
                    break;
            }
        } catch (JSONException failure) {
            statusView.setText("页面消息：" + payloadJson);
        }
    }

    private void updateNavigationState() {
        if (backButton != null) {
            backButton.setEnabled(started && nativeCanGoBack());
        }
    }

    private void showFatal(String message) {
        final TextView view = new TextView(this);
        view.setText(message);
        view.setPadding(dp(16), dp(16), dp(16), dp(16));
        setContentView(view);
    }

    private static String shorten(String url) {
        if (url == null) {
            return "";
        }
        return url.length() > 72 ? url.substring(0, 69) + "..." : url;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private native void nativeStart(SystemWebViewHostView host, String directory);

    private native void nativeStop();

    private native boolean nativeCanGoBack();

    private native void nativeGoBack();

    private native void nativeReload();
}
