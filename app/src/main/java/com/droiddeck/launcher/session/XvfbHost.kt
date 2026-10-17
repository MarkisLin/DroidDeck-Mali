package dev.hayash1.steamoslite.session

import android.content.Context
import android.util.Log
import dev.hayash1.steamoslite.runtime.GuestCommand
import java.io.File

/**
 * 在独立 fake-root 会话里启动 Xvfb。
 *
 * Xvfb 的 -nolock 只允许 root 使用，而 guest 会话本身不是 root；proot 的 -0 只对这一条命令生效，
 * 所以 Xvfb 必须自己起一个 proot 进程。它需要比会话先就绪，因此由 App 在会话启动时拉起。
 *
 * 每次按 socket + 帧缓冲文件 + xdpyinfo 存活探测校验是否真的可用；不可用就清掉残留进程重新起，
 * 避免进程还在、socket 已被删除的僵尸状态挡住后续会话。
 */
object XvfbHost {
    private const val TAG = "XvfbHost"
    private const val DISPLAY = ":7"
    private const val FB_DIR = "/tmp/xvfb-fb"

    fun start(context: Context) {
        Thread({ startOne(context, DISPLAY, FB_DIR) }, "xvfb-host").start()
    }

    private fun startOne(context: Context, display: String, fbDir: String) {
        val num = display.trimStart(':')
        val socket = File(context.filesDir, "linuxfs/tmp/.X11-unix/X$num")
        val fb = File(context.filesDir, "linuxfs$fbDir/Xvfb_screen0")
        if (socket.exists() && fb.exists() && responds(context, display)) {
            Log.i(TAG, "Xvfb $display already usable")
            return
        }
        clean(context, num, fbDir)
        var attempt = 0
        while (attempt < 3) {
            attempt++
            try {
                Log.i(TAG, "starting Xvfb $display (attempt $attempt)")
                val cmd = "mkdir -p $fbDir; " +
                    "rm -f /tmp/.X${num}-lock /tmp/.X11-unix/X$num $fbDir/Xvfb_screen0; " +
                    "exec /usr/bin/Xvfb $display -screen 0 1280x720x24 -fbdir $fbDir -nolisten tcp -nolock"
                GuestCommand.run(
                    context,
                    listOf("/bin/sh", "-c", cmd),
                    fakeRoot = true,
                    logName = "xvfb$num",
                ) { line -> if (line.contains("Fatal")) Log.w(TAG, line) }
                Log.w(TAG, "Xvfb $display exited (attempt $attempt)")
            } catch (e: Exception) {
                Log.w(TAG, "Xvfb $display failed", e)
            }
            if (socket.exists() && fb.exists() && responds(context, display)) return
            Thread.sleep(1500)
        }
        Log.e(TAG, "Xvfb $display could not be started")
    }

    // A stale server can leave socket + framebuffer files behind while its display is gone.
    private fun responds(context: Context, display: String): Boolean = try {
        GuestCommand.run(
            context,
            listOf("/bin/sh", "-c", "xdpyinfo -display $display >/dev/null 2>&1"),
            fakeRoot = true,
            logName = "xvfb-probe",
        ) { } == 0
    } catch (e: Exception) {
        Log.w(TAG, "Xvfb liveness probe failed", e)
        false
    }

    private fun clean(context: Context, num: String, fbDir: String) {
        try {
            GuestCommand.run(
                context,
                listOf(
                    "/bin/sh", "-c",
                    "pkill -f 'Xvfb :${num}' 2>/dev/null; sleep 1; " +
                        "rm -f /tmp/.X${num}-lock /tmp/.X11-unix/X$num $fbDir/Xvfb_screen0 2>/dev/null; exit 0",
                ),
                fakeRoot = true,
                logName = "xvfb-clean",
            ) { }
        } catch (e: Exception) {
            Log.w(TAG, "could not clean stale Xvfb", e)
        }
    }
}
