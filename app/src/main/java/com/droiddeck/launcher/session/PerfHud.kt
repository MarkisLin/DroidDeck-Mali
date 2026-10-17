package dev.hayash1.steamoslite.session

import android.content.Context
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import dev.hayash1.steamoslite.R
import dev.hayash1.steamoslite.gpu.FrameGen
import dev.hayash1.steamoslite.gpu.Lossless
import dev.hayash1.steamoslite.wayland.WaylandCompositor
import java.io.File
import java.util.Locale

class PerfHud(context: Context) {
    var text by mutableStateOf("")
        private set

    private val context = context.applicationContext
    private var lastTick = 0L
    private var running = false
    private val handler = Handler(Looper.getMainLooper())

    private val enabled: Boolean
        get() = SessionPrefs.hudEnabled(context)
            && !File(Environment.getExternalStorageDirectory(), "Download/droiddeck-no-hud").exists()

    var onPresentingWindowChanged: (() -> Unit)? = null
    private var lastWindow: String? = null

    private val listener = object : WaylandCompositor.GameListener {
        override fun onGameSurface(window: String?, gpuName: String?) {
            if (window != null && window != lastWindow) {
                lastWindow = window
                handler.post { onPresentingWindowChanged?.invoke() }
            }
        }
        override fun onGameFrame() {}
    }

    private val tick = object : Runnable {
        override fun run() {
            if (!running) return
            lastTick = SystemClock.elapsedRealtime()
            text = line(runCatching { WaylandCompositor.nativeSessionFps() }.getOrDefault(0f))
            handler.postDelayed(this, 1000)
        }
    }

    fun refresh() {
        if (enabled) start() else stop()
    }

    fun start() {
        if (!enabled || running) return
        running = true
        lastTick = SystemClock.elapsedRealtime()
        WaylandCompositor.setGameListener(listener)
        handler.post(tick)
    }

    fun stop() {
        running = false
        WaylandCompositor.clearGameListener(listener)
        text = ""
    }

    private fun line(base: Float): String {
        val mode = FrameGen.mode(context)
        if (mode.engine == FrameGen.ENGINE_OFF) return fps(base)
        val lsfg = mode.engine == FrameGen.ENGINE_LSFG
        val name = if (lsfg) "LSFG" else "Win-FG"
        val label = FrameGen.label(context, mode)
        if (lsfg && Lossless.cacheFile(context)?.isFile != true) return context.getString(R.string.hud_fg_needs_lossless, name)
        val stats = try { WaylandCompositor.nativeFrameGenStats() } catch (t: Throwable) { null }
        val problem = try { WaylandCompositor.nativeFrameGenProblem() } catch (t: Throwable) { -1 }
        return when {
            problem == 1 -> context.getString(R.string.hud_fg_unsupported, name)
            problem == 2 -> context.getString(R.string.hud_fg_failed, name)
            stats != null && stats[3] > 1f -> String.format(Locale.US, "%s  %.0f → %.0f fps", label, base, stats[3])
            else -> context.getString(R.string.hud_fg_starting, label, fps(base))
        }
    }

    private fun fps(value: Float) =
        if (value > 0.05f) String.format(Locale.US, "%.0f fps", value) else "— fps"
}
