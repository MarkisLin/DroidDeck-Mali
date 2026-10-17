package dev.hayash1.steamoslite

import android.app.Application
import dev.hayash1.steamoslite.runtime.BwrapSpawner
import dev.hayash1.steamoslite.session.CrashHandler
import dev.hayash1.steamoslite.session.GpuClockPin

class App : Application() {
    override fun attachBaseContext(base: android.content.Context) =
        super.attachBaseContext(dev.hayash1.steamoslite.core.AppLanguage.wrap(base))

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        dev.hayash1.steamoslite.core.AppLanguage.systemChanged(newConfig)
    }

    override fun onCreate() {
        super.onCreate()
        CrashHandler.install(this)
        dev.hayash1.steamoslite.session.QuestExperimental.initialize(this)
        dev.hayash1.steamoslite.session.SessionPrefs.settleGpuDriverMode(this)
        dev.hayash1.steamoslite.update.AppUpdates.init(this)
        GpuClockPin.clearLeftover(this)
        BwrapSpawner.start(this)
        dev.hayash1.steamoslite.frontend.GameFileSync.start(this)
        dev.hayash1.steamoslite.stores.StoresState.init(this)
        dev.hayash1.steamoslite.stores.StoresState.register(dev.hayash1.steamoslite.stores.gog.GogBackend)
        dev.hayash1.steamoslite.stores.StoresState.register(dev.hayash1.steamoslite.stores.epic.EpicBackend)
        dev.hayash1.steamoslite.stores.StoresState.register(dev.hayash1.steamoslite.stores.amazon.AmazonBackend)
    }
}
