package dev.hayash1.steamoslite.stores.epic

import android.content.Context
import android.content.Intent
import dev.hayash1.steamoslite.R
import dev.hayash1.steamoslite.stores.CatalogItem
import dev.hayash1.steamoslite.stores.EpicLaunchSupport
import dev.hayash1.steamoslite.stores.InstalledStoreGame
import dev.hayash1.steamoslite.stores.LoginFlows
import dev.hayash1.steamoslite.stores.Store
import dev.hayash1.steamoslite.stores.StoreBackend
import dev.hayash1.steamoslite.stores.StoreInstallRoot
import dev.hayash1.steamoslite.stores.StoreLoginActivity
import dev.hayash1.steamoslite.stores.StoresState
import dev.hayash1.steamoslite.stores.download.DownloadEntry
import dev.hayash1.steamoslite.stores.download.DownloadQueue
import dev.hayash1.steamoslite.stores.mergeItems

object EpicBackend : StoreBackend, EpicLaunchSupport {
    override val store: Store = Store.EPIC

    init {
        LoginFlows.epicFlow = { EpicLogin.create() }
    }

    override fun signIn(context: Context) {
        context.startActivity(
            Intent(context, StoreLoginActivity::class.java)
                .putExtra(StoreLoginActivity.EXTRA_STORE, store.id)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    override fun signOut(context: Context) {
        runCatching { EpicLibrary.forget(context) }
    }

    override fun syncLibrary(context: Context, force: Boolean) {
        val app = context.applicationContext
        Thread({
            val cached = EpicLibrary.cached(app).map { EpicLibrary.toCatalogItem(app, it) }
            val quiet = cached.isNotEmpty() && !force
            StoresState.post {
                if (cached.isNotEmpty()) StoresState.library[store] = mergeItems(StoresState.library[store], cached)
                if (!quiet) StoresState.status[store] = app.getString(R.string.stores_sync_library)
            }
            val result = EpicLibrary.sync(app, force) { line -> if (!quiet) StoresState.post { StoresState.status[store] = line } }
            StoresState.post {
                StoresState.status.remove(store)
                when (result) {
                    is EpicLibrary.SyncResult.Ok -> {
                        StoresState.library[store] = mergeItems(StoresState.library[store], result.games.map { EpicLibrary.toCatalogItem(app, it) })
                        StoresState.problems.remove(store)
                    }
                    is EpicLibrary.SyncResult.Failed -> StoresState.problems[store] = result.message
                    EpicLibrary.SyncResult.NotLoggedIn -> StoresState.problems[store] = StoresState.notSignedInLine(store)
                    EpicLibrary.SyncResult.Throttled -> if (!StoresState.library.containsKey(store)) StoresState.library[store] = cached
                    EpicLibrary.SyncResult.Busy -> {}
                }
            }
        }, "epic-sync").start()
    }

    override fun loadShelves(context: Context, force: Boolean) {
        val app = context.applicationContext
        EpicShelves.cached(app)?.let { s -> StoresState.post { StoresState.shelves[store] = EpicShelves.markOwned(s, StoresState.library[store]) } }
        Thread({
            val shelves = runCatching { EpicShelves.featured(app, force) }.getOrNull()
            if (shelves != null) StoresState.post { StoresState.shelves[store] = EpicShelves.markOwned(shelves, StoresState.library[store]) }
        }, "epic-shelves").start()
    }

    override fun install(context: Context, item: CatalogItem, root: java.io.File) {
        val app = context.applicationContext
        if (item.extra["namespace"].isNullOrEmpty() || item.extra["catalogItemId"].isNullOrEmpty()) {
            StoresState.logLine("Epic: \"${item.title}\" is not in your library")
            return
        }
        val folder = StoreInstallRoot.folderFor(app, Store.EPIC, item.id, item.title, root)
        val entry = DownloadEntry(
            Store.EPIC, item.id, item.title, cover = item.imageUrl, bytesTotal = item.sizeBytes,
            location = StoreInstallRoot.labelFor(app, folder),
        )
        DownloadQueue.enqueue(app, entry) { EpicInstaller(app, item, folder) }
    }

    override fun uninstall(context: Context, game: InstalledStoreGame) {}

    override fun exchangeCode(context: Context): String? {
        val token = EpicCredentialStore.getValidAccessToken(context) ?: return null
        return EpicApiClient.exchangeCode(token)
    }

    override fun signInExpired(context: Context): Boolean = EpicCredentialStore.expired(context)
}
