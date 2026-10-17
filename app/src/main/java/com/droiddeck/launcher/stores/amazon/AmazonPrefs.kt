package dev.hayash1.steamoslite.stores.amazon

import android.content.Context
import android.content.SharedPreferences

object AmazonPrefs {
    const val FILE = "stores.amazon"

    @JvmStatic
    fun get(context: Context): SharedPreferences = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
}
