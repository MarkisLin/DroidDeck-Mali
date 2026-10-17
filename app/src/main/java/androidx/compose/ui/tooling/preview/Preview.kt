package androidx.compose.ui.tooling.preview

@Retention(AnnotationRetention.BINARY)
annotation class Preview(
    val name: String = "",
    val group: String = "",
    val apiLevel: Int = -1,
    val widthDp: Int = -1,
    val heightDp: Int = -1,
    val fontScale: Float = 1f,
)
