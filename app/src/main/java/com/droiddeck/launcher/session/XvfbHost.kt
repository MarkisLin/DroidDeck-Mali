package dev.hayash1.steamoslite.session

import android.content.Context
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import dev.hayash1.steamoslite.runtime.GuestCommand
import java.io.File

/**
 * 在独立 fake-root 会话里启动 Xvfb。
 *
 * Xvfb 的 -nolock 只允许 root 使用，而 guest 会话本身不是 root；proot 的 -0 只对这一条命令生效，
 * 所以 Xvfb 必须自己起一个 proot 进程。它需要比会话先就绪，因此由 App 在会话启动时拉起。
 *
 * start() 会被会话重启并发调用（runSession 可能跑第二次），所以这里必须单飞：一次 start 还没结束
 * 就直接返回。已经在服务的 Xvfb 永远不会被清掉——clean() 只在确认该 display 既连不上、也没有存活
 * 进程时才执行，否则第二次 start 会杀掉 xpresent 正在呈现的那个 server。
 */
object XvfbHost {
    private const val TAG = "XvfbHost"
    private const val DISPLAY = ":7"
    private const val FB_DIR = "/tmp/xvfb-fb"
    private const val TAIL_LINES = 12

    private val startLock = Any()
    private var starting = false

    fun start(context: Context) {
        synchronized(startLock) {
            if (starting) {
                Log.i(TAG, "start ignored: a start for $DISPLAY is already in flight")
                return
            }
            starting = true
        }
        Thread({
            try {
                startOne(context, DISPLAY, FB_DIR)
            } finally {
                synchronized(startLock) { starting = false }
            }
        }, "xvfb-host").start()
    }

    private fun startOne(context: Context, display: String, fbDir: String) {
        val num = display.trimStart(':')
        val socket = File(context.filesDir, "linuxfs/tmp/.X11-unix/X$num")
        val fb = File(context.filesDir, "linuxfs$fbDir/Xvfb_screen0")

        if (serverIsServing(context, display)) {
            Log.i(TAG, "Xvfb $display already serving (socket=${socket.exists()}, fb=${fb.exists()}); leaving it alone")
            return
        }

        Log.i(TAG, "starting Xvfb $display (socket=${socket.exists()}, fb=${fb.exists()})")
        clean(context, num, fbDir)

        var attempt = 0
        while (attempt < 3) {
            attempt++
            val tail = ArrayDeque<String>()
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
                ) { line ->
                    // The previous version kept only lines containing "Fatal", so the real
                    // reason for a failed start never reached the log bundle. Keep a tail of
                    // the whole output instead.
                    tail.addLast(line)
                    if (tail.size > TAIL_LINES) tail.removeFirst()
                    if (line.contains("Fatal")) Log.w(TAG, line)
                }
                Log.w(TAG, "Xvfb $display exited (attempt $attempt): ${tail.joinToString(" | ")}")
            } catch (e: Exception) {
                Log.w(TAG, "Xvfb $display failed (attempt $attempt): ${tail.joinToString(" | ")}", e)
            }
            if (serverIsServing(context, display)) return
            Thread.sleep(1500)
        }
        Log.e(TAG, "Xvfb $display could not be started")
    }

    /**
     * True when this display must be left running.
     *
     * Connecting to the socket is the cheapest proof of a live server and, unlike `xdpyinfo`,
     * it does not depend on a guest tool that may not be staged. If the connect fails but an
     * Xvfb process for the display is still alive, treat it as serving too: restarting a display
     * that a client is already presenting on kills a working session.
     */
    private fun serverIsServing(context: Context, display: String): Boolean {
        val num = display.trimStart(':')
        val socket = File(context.filesDir, "linuxfs/tmp/.X11-unix/X$num")
        if (!socket.exists()) return false
        if (connectProbe(socket)) return true

        val alive = try {
            GuestCommand.run(
                context,
                listOf("/bin/sh", "-c", "pidof Xvfb >/dev/null 2>&1"),
                fakeRoot = true,
                logName = "xvfb-probe",
            ) { } == 0
        } catch (e: Exception) {
            Log.w(TAG, "liveness probe failed", e)
            false
        }
        if (alive) Log.w(TAG, "socket probe failed but Xvfb is running for $display; not restarting it")
        return alive
    }

    private fun connectProbe(socket: File): Boolean = try {
        LocalSocket().use { s ->
            s.connect(LocalSocketAddress(socket.absolutePath, LocalSocketAddress.Namespace.FILESYSTEM))
        }
        true
    } catch (e: Exception) {
        Log.w(TAG, "socket connect to $socket failed: ${e.message}")
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