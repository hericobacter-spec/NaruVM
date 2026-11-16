package dev.henge.research;

import android.app.Instrumentation;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.util.ArrayList;
import java.util.Arrays;

/** Real Android dispatch with authored events; no physical HID or guest is used. */
public final class HardwareInputInstrumentation extends Instrumentation {
    private int checks;
    private void check(boolean value, String name) {
        if (!value) throw new AssertionError(name);
        checks++;
    }
    private MotionEvent event(int action, float x, float y, int buttons, float sx, float sy) {
        MotionEvent.PointerProperties p = new MotionEvent.PointerProperties(); p.id = 0; p.toolType = MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords c = new MotionEvent.PointerCoords(); c.x = x; c.y = y;
        c.setAxisValue(MotionEvent.AXIS_HSCROLL, sx); c.setAxisValue(MotionEvent.AXIS_VSCROLL, sy);
        return MotionEvent.obtain(0, 0, action, 1, new MotionEvent.PointerProperties[]{p},
            new MotionEvent.PointerCoords[]{c}, 0, buttons, 1, 1, 0, 0, InputDevice.SOURCE_MOUSE, 0);
    }
    private void dispatch(MacInputView view, int action, float x, float y, int buttons) {
        MotionEvent e = event(action, x, y, buttons, 0, 0);
        try {
            boolean touch = action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_UP;
            check(touch ? view.dispatchTouchEvent(e) : view.dispatchGenericMotionEvent(e), "mouse event consumed: " + action);
        } finally { e.recycle(); }
    }
    private void runChecks() {
        ArrayList<String> lines = new ArrayList<>();
        MacInputView view = new MacInputView(getTargetContext(), lines::add);
        Bitmap bitmap = Bitmap.createBitmap(1280, 800, Bitmap.Config.ARGB_8888);
        view.setImageBitmap(bitmap); view.layout(0, 0, 1280, 800);
        check(view.requestFocus(), "Mac display accepts focus");
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 100, 100, 0);
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 110, 104, 0);
        dispatch(view, MotionEvent.ACTION_DOWN, 110, 104, 1);
        dispatch(view, MotionEvent.ACTION_MOVE, 120, 110, 1);
        dispatch(view, MotionEvent.ACTION_BUTTON_PRESS, 120, 110, 3);
        dispatch(view, MotionEvent.ACTION_BUTTON_RELEASE, 120, 110, 1);
        dispatch(view, MotionEvent.ACTION_UP, 120, 110, 0);
        check(lines.equals(Arrays.asList("live move 0 0", "live move 10 4", "live button 0 1", "live move 10 6", "live button 1 1", "live button 1 0", "live button 0 0")),
            "actual hover/touch/button dispatch preserves drag and right button");
        lines.clear();
        for (int i = 0; i < 2; ++i) {
            MotionEvent e = event(MotionEvent.ACTION_SCROLL, 120, 110, 0, .5f, -.5f);
            check(view.dispatchGenericMotionEvent(e), "wheel consumed"); e.recycle();
        }
        check(lines.equals(Arrays.asList("live scroll 1 -1")), "fractional horizontal and vertical wheel accumulate");
        lines.clear();
        check(view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_META_LEFT)), "Command down consumed");
        check(view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_C)), "C down consumed");
        view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_C));
        view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_C));
        view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_META_LEFT));
        check(lines.equals(Arrays.asList("live hid 227 1", "live hid 6 1", "live hid 6 0", "live hid 227 0")), "Command+C key lifecycle and duplicate-down suppression");
        check(MacInputView.keyHid(KeyEvent.KEYCODE_ALT_LEFT) == 226, "Alt maps to Option");
        check(MacInputView.keyHid(KeyEvent.KEYCODE_NUMPAD_ENTER) == 88, "numpad Enter maps separately");
        check(MacInputView.keyHid(KeyEvent.KEYCODE_BACK) == 0, "Android back is not a guest key");
        lines.clear();
        view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_CTRL_LEFT));
        dispatch(view, MotionEvent.ACTION_BUTTON_PRESS, 120, 110, 4);
        view.onWindowFocusChanged(false); view.onWindowFocusChanged(false);
        check(lines.equals(Arrays.asList("live hid 224 1", "live button 2 1", "live button 2 0", "live hid 224 0")),
            "window loss releases middle and Control exactly once");
        lines.clear();
        view.touchInputStarted();
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 600, 400, 0);
        check(lines.isEmpty(), "touch-to-mouse switch resets origin instead of jumping");
        lines.clear(); view.layout(0, 0, 2560, 1600); view.touchInputStarted();
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 100, 100, 0);
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 120, 110, 0);
        check(lines.equals(Arrays.asList("live move 0 0", "live move 20 10")),
            "large display must not divide physical mouse travel by image zoom");
        lines.clear(); view.setMouseSpeed(2f);
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 120, 110, 0);
        dispatch(view, MotionEvent.ACTION_HOVER_MOVE, 140, 120, 0);
        check(lines.equals(Arrays.asList("live move 0 0", "live move 40 20")), "2x sensitivity doubles travel");
        lines.clear(); view.touchInputStarted(); view.setMouseSpeed(1f);
        MotionEvent captured = event(MotionEvent.ACTION_MOVE, 3, 2, 0, 0, 0);
        captured.setSource(InputDevice.SOURCE_MOUSE_RELATIVE);
        captured.addBatch(1, 5, 4, 1, 1, 0);
        check(view.onCapturedPointerEvent(captured), "captured relative event accepted"); captured.recycle();
        check(lines.equals(Arrays.asList("live move 0 0", "live move 3 2", "live move 5 4")),
            "batched captured deltas preserve history without scaling or dropping motion");
        view.setImageBitmap(null); bitmap.recycle();
    }
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            Throwable[] failure = new Throwable[1];
            runOnMainSync(() -> { try { runChecks(); } catch (Throwable error) { failure[0] = error; } });
            if (failure[0] != null) throw new AssertionError(failure[0]);
            result.putString("result", "HARDWARE_INPUT_ANDROID checks=" + checks + " failures=0 source=authored no_vm=1 physical_hid=0");
            finish(-1, result);
        } catch (Throwable error) {
            result.putString("error", error.toString()); finish(0, result);
        }
    }
}
