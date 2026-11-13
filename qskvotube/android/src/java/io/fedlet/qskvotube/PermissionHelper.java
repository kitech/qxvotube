package io.fedlet.qskvotube;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;

/**
 * 权限/白名单助手(移植自 qsktox,去掉 androidx 依赖):
 * 用 API 23+ 原生 Activity#requestPermissions / checkSelfPermission,
 * 避免引入 androidx.core gradle 依赖(minSdk 23 已满足)。
 */
public class PermissionHelper {
    private static final int REQ_NOTIFICATION = 9001;

    public static boolean hasNotificationPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return true;
        return activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
            == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestNotificationPermission(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return;
        if (hasNotificationPermission(activity))
            return;
        activity.requestPermissions(
            new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFICATION);
    }

    public static boolean isBatteryWhitelisted(Context ctx) {
        PowerManager pm = (PowerManager) ctx.getSystemService(Context.POWER_SERVICE);
        return pm != null && pm.isIgnoringBatteryOptimizations(ctx.getPackageName());
    }

    // 电池白名单一次性引导(qsktox 同款):已白名单/已提示过 → 直接返回,
    // 用 SharedPreferences 记忆,避免每次启动骚扰用户
    public static void requestBatteryWhitelist(Activity activity) {
        if (isBatteryWhitelisted(activity))
            return;
        SharedPreferences sp = activity.getSharedPreferences(
            "qskvotube_prefs", Context.MODE_PRIVATE);
        if (sp.getBoolean("battery_whitelist_prompted", false))
            return;
        sp.edit().putBoolean("battery_whitelist_prompted", true).apply();
        try {
            activity.startActivity(new Intent(
                Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        } catch (Exception e) {
            // 极端 OEM 无设置页:静默跳过(保活退回 FGS 路径仍可工作)
            android.util.Log.w("qskvotube",
                "[PermissionHelper] battery whitelist settings page failed: " + e);
        }
    }
}
