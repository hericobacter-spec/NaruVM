package dev.henge.research;

import android.content.Context;
import android.graphics.Rect;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.ImageView;

/** External HID events are forwarded only while the Mac display owns focus. */
class MacInputView extends ImageView {
    private final MacInput.Hardware hardware;
    private final MacInput.Sink sink;
    private float mouseSpeed = 1f;
    MacInputView(Context context, MacInput.Sink sink) {
        super(context); this.sink = sink; hardware = new MacInput.Hardware(sink);
        setFocusable(true); setFocusableInTouchMode(true);
    }
    final void setMouseSpeed(float speed) {
        mouseSpeed = Float.isFinite(speed) && speed >= .5f && speed <= 3f ? speed : 1f;
        hardware.resetPointer();
    }
    static int keyHid(int code) {
        if (code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z) return 4 + code - KeyEvent.KEYCODE_A;
        if (code >= KeyEvent.KEYCODE_1 && code <= KeyEvent.KEYCODE_9) return 30 + code - KeyEvent.KEYCODE_1;
        if (code >= KeyEvent.KEYCODE_F1 && code <= KeyEvent.KEYCODE_F12) return 58 + code - KeyEvent.KEYCODE_F1;
        if (code >= KeyEvent.KEYCODE_NUMPAD_1 && code <= KeyEvent.KEYCODE_NUMPAD_9) return 89 + code - KeyEvent.KEYCODE_NUMPAD_1;
        switch (code) {
        case KeyEvent.KEYCODE_0: return 39;
        case KeyEvent.KEYCODE_ENTER: return 40; case KeyEvent.KEYCODE_ESCAPE: return 41;
        case KeyEvent.KEYCODE_DEL: return 42; case KeyEvent.KEYCODE_TAB: return 43;
        case KeyEvent.KEYCODE_SPACE: return 44; case KeyEvent.KEYCODE_MINUS: return 45;
        case KeyEvent.KEYCODE_EQUALS: return 46; case KeyEvent.KEYCODE_LEFT_BRACKET: return 47;
        case KeyEvent.KEYCODE_RIGHT_BRACKET: return 48; case KeyEvent.KEYCODE_BACKSLASH: return 49;
        case KeyEvent.KEYCODE_SEMICOLON: return 51; case KeyEvent.KEYCODE_APOSTROPHE: return 52;
        case KeyEvent.KEYCODE_GRAVE: return 53; case KeyEvent.KEYCODE_COMMA: return 54;
        case KeyEvent.KEYCODE_PERIOD: return 55; case KeyEvent.KEYCODE_SLASH: return 56;
        case KeyEvent.KEYCODE_CAPS_LOCK: return 57; case KeyEvent.KEYCODE_SYSRQ: return 70;
        case KeyEvent.KEYCODE_SCROLL_LOCK: return 71; case KeyEvent.KEYCODE_BREAK: return 72;
        case KeyEvent.KEYCODE_INSERT: return 73; case KeyEvent.KEYCODE_FORWARD_DEL: return 76;
        case KeyEvent.KEYCODE_MOVE_HOME: return 74; case KeyEvent.KEYCODE_PAGE_UP: return 75;
        case KeyEvent.KEYCODE_MOVE_END: return 77; case KeyEvent.KEYCODE_PAGE_DOWN: return 78;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return 79; case KeyEvent.KEYCODE_DPAD_LEFT: return 80;
        case KeyEvent.KEYCODE_DPAD_DOWN: return 81; case KeyEvent.KEYCODE_DPAD_UP: return 82;
        case KeyEvent.KEYCODE_NUM_LOCK: return 83; case KeyEvent.KEYCODE_NUMPAD_DIVIDE: return 84;
        case KeyEvent.KEYCODE_NUMPAD_MULTIPLY: return 85; case KeyEvent.KEYCODE_NUMPAD_SUBTRACT: return 86;
        case KeyEvent.KEYCODE_NUMPAD_ADD: return 87; case KeyEvent.KEYCODE_NUMPAD_ENTER: return 88;
        case KeyEvent.KEYCODE_NUMPAD_0: return 98; case KeyEvent.KEYCODE_NUMPAD_DOT: return 99;
        case KeyEvent.KEYCODE_NUMPAD_EQUALS: return 103;
        case KeyEvent.KEYCODE_CTRL_LEFT: return 224; case KeyEvent.KEYCODE_SHIFT_LEFT: return 225;
        case KeyEvent.KEYCODE_ALT_LEFT: return 226; case KeyEvent.KEYCODE_META_LEFT: return 227;
        case KeyEvent.KEYCODE_CTRL_RIGHT: return 228; case KeyEvent.KEYCODE_SHIFT_RIGHT: return 229;
        case KeyEvent.KEYCODE_ALT_RIGHT: return 230; case KeyEvent.KEYCODE_META_RIGHT: return 231;
        default: return 0;
        }
    }
    @Override public boolean onKeyDown(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK && event.isFromSource(InputDevice.SOURCE_MOUSE)) return true;
        if (code == KeyEvent.KEYCODE_ESCAPE && hasPointerCapture()) {
            releasePointerCapture(); hardware.release(); return true;
        }
        if (code == KeyEvent.KEYCODE_LANGUAGE_SWITCH) {
            if (event.getRepeatCount() == 0) sink.send("input_source"); return true;
        }
        int hid = keyHid(code); if (hid == 0) return super.onKeyDown(code, event);
        hardware.key(hid, true); return true;
    }
    @Override public boolean onKeyUp(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK && event.isFromSource(InputDevice.SOURCE_MOUSE)) return true;
        if (code == KeyEvent.KEYCODE_LANGUAGE_SWITCH) return true;
        int hid = keyHid(code); if (hid == 0) return super.onKeyUp(code, event);
        hardware.key(hid, false); return true;
    }
    private boolean mouse(MotionEvent event) {
        return event.isFromSource(InputDevice.SOURCE_MOUSE) || event.isFromSource(InputDevice.SOURCE_TOUCHPAD);
    }
    private boolean pointer(MotionEvent event, boolean captured) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) { hardware.release(); return true; }
        if (action == MotionEvent.ACTION_HOVER_EXIT) { hardware.resetPointer(); return true; }
        if (getDrawable() == null) return true; // no guest display yet
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_BUTTON_PRESS) requestFocus();
        if (!hasFocus()) return true;
        if (captured) {
            if (action == MotionEvent.ACTION_MOVE) {
                boolean touchpad = event.isFromSource(InputDevice.SOURCE_TOUCHPAD);
                for (int i = 0; i < event.getHistorySize(); i++)
                    hardware.delta(touchpad ? event.getHistoricalAxisValue(MotionEvent.AXIS_RELATIVE_X, i) : event.getHistoricalX(i),
                                   touchpad ? event.getHistoricalAxisValue(MotionEvent.AXIS_RELATIVE_Y, i) : event.getHistoricalY(i), 1f / mouseSpeed);
                hardware.delta(touchpad ? event.getAxisValue(MotionEvent.AXIS_RELATIVE_X) : event.getX(),
                               touchpad ? event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y) : event.getY(), 1f / mouseSpeed);
            }
        } else hardware.position(event.getX(), event.getY(), 1f / mouseSpeed);
        hardware.buttons(event.getButtonState());
        if (action == MotionEvent.ACTION_SCROLL)
            hardware.scroll(event.getAxisValue(MotionEvent.AXIS_HSCROLL), event.getAxisValue(MotionEvent.AXIS_VSCROLL));
        return true;
    }
    protected final boolean mouseTouch(MotionEvent event) { return mouse(event) && pointer(event, false); }
    protected final void touchInputStarted() { hardware.buttons(0); hardware.resetPointer(); }
    @Override public boolean onTouchEvent(MotionEvent event) {
        return mouseTouch(event) || super.onTouchEvent(event);
    }
    @Override public boolean onHoverEvent(MotionEvent event) {
        return mouse(event) ? pointer(event, false) : super.onHoverEvent(event);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        return mouse(event) ? pointer(event, false) : super.onGenericMotionEvent(event);
    }
    @Override public boolean onCapturedPointerEvent(MotionEvent event) { return pointer(event, true); }
    @Override public void onPointerCaptureChange(boolean captured) {
        super.onPointerCaptureChange(captured);
        if (!captured) hardware.release(); else hardware.resetPointer();
    }
    @Override protected void onFocusChanged(boolean focused, int direction, Rect old) {
        super.onFocusChanged(focused, direction, old);
        if (!focused && hardware != null) hardware.release();
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused) hardware.release();
    }
    @Override protected void onDetachedFromWindow() { hardware.release(); super.onDetachedFromWindow(); }
}
