package org.kobalt.android;

import android.os.Build;
import android.util.Log;
import java.lang.reflect.Method;
import org.lsposed.hiddenapibypass.HiddenApiBypass;

/** Optional vendor interfaces. Failure leaves the ordinary Android reader available. */
final class BooxDevice {
    private static final int BRIGHTNESS = 7, WARMTH = 6;
    private Method read, maximum, write;
    private boolean available;

    BooxDevice() {
        if (!"ONYX".equalsIgnoreCase(Build.MANUFACTURER) && !"ONYX".equalsIgnoreCase(Build.BRAND)) return;
        try {
            if (!HiddenApiBypass.addHiddenApiExemptions("Landroid/onyx/")) return;
            Class<?> controller = Class.forName("android.onyx.hardware.DeviceController");
            if (!(Boolean)controller.getMethod("checkCTM").invoke(null)) return;
            read = controller.getMethod("getLightValue", int.class);
            maximum = controller.getMethod("getMaxLightValue", int.class);
            write = controller.getMethod("setLightValue", int.class, int.class, int.class);
            // Probe both ranges and values without changing the frontlight.
            available = valid(BRIGHTNESS) && valid(WARMTH);
        } catch (ReflectiveOperationException | RuntimeException | LinkageError error) {
            Log.w("KobaltBOOX", "Light controls unavailable", error);
        }
    }
    private boolean valid(int type) throws ReflectiveOperationException {
        int limit = (Integer)maximum.invoke(null, type), value = (Integer)read.invoke(null, type);
        return limit > 0 && limit <= 10000 && value >= 0 && value <= limit;
    }
    synchronized boolean available() { return available; }
    synchronized int light(int operation, int type, int value) {
        if (!available || (type != BRIGHTNESS && type != WARMTH)) return -1;
        try {
            int limit = (Integer)maximum.invoke(null, type);
            if (limit <= 0 || limit > 10000) throw new IllegalStateException("Invalid light range");
            if (operation == 1) return limit;
            if (operation == 2) {
                if (value < 0 || value > limit) return -1;
                // Flags zero suppresses the BOOX system slider. No global settings grant is needed.
                write.invoke(null, type, value, 0);
            } else if (operation != 0) return -1;
            int actual = (Integer)read.invoke(null, type);
            if (actual < 0 || actual > limit) throw new IllegalStateException("Invalid light value");
            return actual;
        } catch (ReflectiveOperationException | RuntimeException | LinkageError error) {
            available = false;
            Log.w("KobaltBOOX", "Light controls failed", error);
            return -1;
        }
    }
}
