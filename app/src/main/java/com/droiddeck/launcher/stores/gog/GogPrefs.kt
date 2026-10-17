package dev.hayash1.steamoslite.stores.gog

import android.content.Context
import android.content.SharedPreferences

object GogPrefs {
    @JvmStatic
    fun get(context: Context): SharedPreferences = context.applicationContext.getSharedPreferences("stores.gog", Context.MODE_PRIVATE)
}
