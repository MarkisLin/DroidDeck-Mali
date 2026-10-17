package dev.hayash1.steamoslite.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.hayash1.steamoslite.wayland.WaylandCompositor
import kotlinx.coroutines.delay

@Composable
fun FpsHud(modifier: Modifier = Modifier) {
    var fps by remember { mutableStateOf(0f) }
    LaunchedEffect(Unit) {
        while (true) {
            fps = runCatching { WaylandCompositor.nativeSessionFps() }.getOrDefault(0f)
            delay(1000)
        }
    }
    Box(modifier.fillMaxSize().wrapContentSize(Alignment.TopEnd).padding(12.dp)) {
        Text(
            text = if (fps > 0f) "%.1f fps".format(fps) else "-- fps",
            color = Color(0xFF9BE28A),
            fontFamily = FontFamily.Monospace,
            fontSize = 15.sp,
            modifier = Modifier
                .background(Color(0xCC000000), RoundedCornerShape(6.dp))
                .padding(horizontal = 10.dp, vertical = 4.dp),
        )
    }
}
