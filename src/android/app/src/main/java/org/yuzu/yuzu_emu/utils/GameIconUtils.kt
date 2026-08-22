// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.utils

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.drawable.LayerDrawable
import android.widget.ImageView
import androidx.core.content.res.ResourcesCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.core.graphics.drawable.toBitmap
import androidx.core.graphics.drawable.toDrawable
import androidx.lifecycle.LifecycleOwner
import coil.ImageLoader
import coil.decode.DataSource
import coil.fetch.DrawableResult
import coil.fetch.FetchResult
import coil.fetch.Fetcher
import coil.key.Keyer
import coil.memory.MemoryCache
import coil.request.ImageRequest
import coil.request.Options
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.model.Game
import java.util.Collections
import java.util.WeakHashMap

private val gameIconHashes = Collections.synchronizedMap(mutableMapOf<String, Int>())
private val gameIconTargets = Collections.synchronizedMap(WeakHashMap<ImageView, GameIconTarget>())

private fun Game.iconCacheKey(): String = "$path|$version"

private data class GameIconTarget(val game: Game, var iconHash: Int? = null)

class GameIconFetcher(
    private val game: Game,
    private val options: Options
) : Fetcher {
    override suspend fun fetch(): FetchResult {
        return DrawableResult(
            drawable = decodeGameIcon(game)!!.toDrawable(options.context.resources),
            isSampled = false,
            dataSource = DataSource.DISK
        )
    }

    private fun decodeGameIcon(game: Game): Bitmap? {
        val data = GameMetadata.getIcon(game.path)
        gameIconHashes[game.iconCacheKey()] = data.contentHashCode()
        return BitmapFactory.decodeByteArray(
            data,
            0,
            data.size,
            BitmapFactory.Options()
        )
    }

    class Factory : Fetcher.Factory<Game> {
        override fun create(data: Game, options: Options, imageLoader: ImageLoader): Fetcher =
            GameIconFetcher(data, options)
    }
}

class GameIconKeyer : Keyer<Game> {
    override fun key(data: Game, options: Options): String = data.iconCacheKey()
}

object GameIconUtils {
    private val imageLoader = ImageLoader.Builder(YuzuApplication.appContext)
        .components {
            add(GameIconKeyer())
            add(GameIconFetcher.Factory())
        }
        .memoryCache {
            MemoryCache.Builder(YuzuApplication.appContext)
                .maxSizePercent(0.25)
                .build()
        }
        .build()

    fun loadGameIcon(game: Game, imageView: ImageView) {
        gameIconTargets[imageView] = GameIconTarget(game)
        val request = ImageRequest.Builder(YuzuApplication.appContext)
            .data(game)
            .target(imageView)
            .error(R.drawable.default_icon)
            .listener(
                onSuccess = { _, _ ->
                    val target = gameIconTargets[imageView]
                    if (target?.game?.iconCacheKey() == game.iconCacheKey()) {
                        gameIconHashes[game.iconCacheKey()]?.let {
                            target.iconHash = it
                        }
                    }
                },
                onError = { _, _ ->
                    gameIconTargets[imageView]?.iconHash = null
                }
            )
            .build()
        imageLoader.enqueue(request)
    }

    fun refreshGameIcon(game: Game) {
        val targets = synchronized(gameIconTargets) {
            gameIconTargets
                .filterValues { it.game.path == game.path && it.game.programId == game.programId }
                .keys
                .toList()
        }
        if (targets.isEmpty()) {
            return
        }

        val iconHash = GameMetadata.getIcon(game.path).contentHashCode()
        val targetsToRefresh = targets.filter { gameIconTargets[it]?.iconHash != iconHash }
        if (targetsToRefresh.isEmpty()) {
            return
        }

        imageLoader.memoryCache?.remove(MemoryCache.Key(game.iconCacheKey()))
        targetsToRefresh.forEach { imageView ->
            imageView.post {
                val target = gameIconTargets[imageView] ?: return@post
                if (target.game.path == game.path && target.game.programId == game.programId) {
                    if (target.iconHash != iconHash) {
                        loadGameIcon(game, imageView)
                    }
                }
            }
        }
    }

    suspend fun getGameIcon(lifecycleOwner: LifecycleOwner, game: Game): Bitmap {
        val request = ImageRequest.Builder(YuzuApplication.appContext)
            .data(game)
            .lifecycle(lifecycleOwner)
            .error(R.drawable.default_icon)
            .build()
        return imageLoader.execute(request)
            .drawable!!.toBitmap(config = Bitmap.Config.ARGB_8888)
    }

    suspend fun getShortcutIcon(lifecycleOwner: LifecycleOwner, game: Game): IconCompat {
        val layerDrawable = ResourcesCompat.getDrawable(
            YuzuApplication.appContext.resources,
            R.drawable.shortcut,
            null
        ) as LayerDrawable
        layerDrawable.setDrawableByLayerId(
            R.id.shortcut_foreground,
            getGameIcon(lifecycleOwner, game).toDrawable(YuzuApplication.appContext.resources)
        )
        val inset = YuzuApplication.appContext.resources
            .getDimensionPixelSize(R.dimen.icon_inset)
        layerDrawable.setLayerInset(1, inset, inset, inset, inset)
        return IconCompat.createWithAdaptiveBitmap(
            layerDrawable.toBitmap(config = Bitmap.Config.ARGB_8888)
        )
    }
}
