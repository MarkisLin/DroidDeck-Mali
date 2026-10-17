package dev.hayash1.steamoslite

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import dev.hayash1.steamoslite.session.SessionService

class HomeActivity : Activity() {
    override fun attachBaseContext(newBase: android.content.Context) {
        super.attachBaseContext(newBase)
        dev.hayash1.steamoslite.core.AppLanguage.applyTo(this, newBase)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = dev.hayash1.steamoslite.session.SessionPrefs.orientation(this)
        routeHome()
    }

    private fun routeHome() {
        val destination = if (HomeApp.isActiveSteamSession()) {
            Intent(this, SessionActivity::class.java)
                .setAction(SessionService.ACTION_HOME_GUIDE)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        } else {
            Intent(this, MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }
        startActivity(destination)
        finish()
    }
}
