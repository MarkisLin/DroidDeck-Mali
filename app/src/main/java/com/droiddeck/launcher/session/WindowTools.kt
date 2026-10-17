package dev.hayash1.steamoslite.session

import android.content.Context
import android.util.Log
import java.io.File

/** 把随 APK 打包的窗口工具（xraise/xhide）放进 guest 的 /usr/local/bin。 */
object WindowTools {
    private const val TAG = "WindowTools"
    private val TOOLS = listOf("libxraise.so" to "xraise", "libxhide.so" to "xhide")

    fun stage(context: Context, root: File) {
        val bin = File(root, "usr/local/bin").apply { mkdirs() }
        val libDir = File(context.applicationInfo.nativeLibraryDir)
        for ((lib, name) in TOOLS) {
            val src = File(libDir, lib)
            if (!src.isFile) { Log.w(TAG, "$lib missing from ${libDir.path}"); continue }
            val dst = File(bin, name)
            try {
                src.copyTo(dst, overwrite = true)
                dst.setExecutable(true, false)
            } catch (e: Exception) {
                Log.w(TAG, "could not stage $name", e)
            }
        }
    }
}
