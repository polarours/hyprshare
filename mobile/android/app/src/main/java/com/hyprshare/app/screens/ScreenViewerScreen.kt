package com.hyprshare.app.screens

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Keyboard
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.hyprshare.app.network.HyprshareClient
import kotlinx.coroutines.launch
import java.io.ByteArrayInputStream

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ScreenViewerScreen(
    client: HyprshareClient,
    onBack: () -> Unit
) {
    val scope = rememberCoroutineScope()
    var scale by remember { mutableFloatStateOf(1f) }
    var offsetX by remember { mutableFloatStateOf(0f) }
    var offsetY by remember { mutableFloatStateOf(0f) }
    var isStreaming by remember { mutableStateOf(false) }
    var currentBitmap by remember { mutableStateOf<android.graphics.ImageBitmap?>(null) }
    var frameCount by remember { mutableIntStateOf(0) }
    var showKeyboard by remember { mutableStateOf(false) }
    var keyboardType by remember { mutableStateOf("text") }
    var keyboardInput by remember { mutableStateOf("") }
    var statusMessage by remember { mutableStateOf("") }

    LaunchedEffect(isStreaming) {
        if (isStreaming) {
            client.sendScreenStart(quality = "medium", fps = 30)
            client.screenFrames.collect { frame ->
                if (frame.data.isNotEmpty()) {
                    val bitmap = BitmapFactory.decodeStream(ByteArrayInputStream(frame.data))
                    if (bitmap != null) {
                        currentBitmap = bitmap.asImageBitmap()
                        frameCount++
                    }
                }
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Remote Screen") },
                navigationIcon = {
                    IconButton(onClick = {
                        if (isStreaming) {
                            scope.launch { client.sendScreenStop() }
                            isStreaming = false
                        }
                        onBack()
                    }) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    Text(
                        text = "$frameCount frames",
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(end = 8.dp)
                    )
                    if (isStreaming) {
                        IconButton(onClick = {
                            scope.launch { client.sendScreenStop() }
                            isStreaming = false
                        }) {
                            Icon(Icons.Default.Stop, contentDescription = "Stop streaming")
                        }
                    } else {
                        IconButton(onClick = { isStreaming = true }) {
                            Icon(Icons.Default.Refresh, contentDescription = "Start streaming")
                        }
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color.Black)
                    .pointerInput(Unit) {
                        detectTransformGestures { _, pan, zoom, _ ->
                            scale = (scale * zoom).coerceIn(0.5f, 5f)
                            offsetX += pan.x
                            offsetY += pan.y
                        }
                    }
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onTap = { offset ->
                                if (isStreaming) {
                                    val x = (offset.x - offsetX) / scale
                                    val y = (offset.y - offsetY) / scale
                                    scope.launch {
                                        client.sendInputEvent("touch", x, y, "tap")
                                    }
                                }
                            },
                            onDoubleTap = { offset ->
                                if (isStreaming) {
                                    val x = (offset.x - offsetX) / scale
                                    val y = (offset.y - offsetY) / scale
                                    scope.launch {
                                        client.sendInputEvent("touch", x, y, "double_tap")
                                    }
                                }
                            },
                            onLongPress = { offset ->
                                if (isStreaming) {
                                    val x = (offset.x - offsetX) / scale
                                    val y = (offset.y - offsetY) / scale
                                    scope.launch {
                                        client.sendInputEvent("touch", x, y, "long_press")
                                    }
                                }
                            }
                        )
                    },
                contentAlignment = Alignment.Center
            ) {
                val bitmap = currentBitmap
                if (bitmap != null) {
                    Image(
                        bitmap = bitmap,
                        contentDescription = "Remote Screen",
                        contentScale = ContentScale.Fit,
                        modifier = Modifier
                            .fillMaxSize()
                            .graphicsLayer(
                                scaleX = scale,
                                scaleY = scale,
                                translationX = offsetX,
                                translationY = offsetY
                            )
                    )
                } else {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        Text(
                            text = if (isStreaming) "Waiting for screen data..." else "Tap refresh to start streaming",
                            color = Color.White,
                            style = MaterialTheme.typography.bodyLarge
                        )
                        if (!client.isConnected()) {
                            Spacer(modifier = Modifier.height(8.dp))
                            Text(
                                text = "Not connected to desktop",
                                color = Color.Red,
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }
            }

            if (statusMessage.isNotEmpty()) {
                Text(
                    text = statusMessage,
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .padding(8.dp),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(MaterialTheme.colorScheme.surface)
                    .padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (showKeyboard) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            FilterChip(
                                selected = keyboardType == "text",
                                onClick = { keyboardType = "text" },
                                label = { Text("Text") }
                            )
                            FilterChip(
                                selected = keyboardType == "special",
                                onClick = { keyboardType = "special" },
                                label = { Text("Keys") }
                            )
                        }
                    }

                    if (keyboardType == "text") {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            OutlinedTextField(
                                value = keyboardInput,
                                onValueChange = { keyboardInput = it },
                                modifier = Modifier.weight(1f),
                                placeholder = { Text("Type text...") },
                                singleLine = true
                            )
                            Button(onClick = {
                                if (keyboardInput.isNotEmpty()) {
                                    scope.launch {
                                        client.sendInputEvent("keyboard", 0f, 0f, "text", mapOf("text" to keyboardInput))
                                    }
                                    keyboardInput = ""
                                }
                            }) {
                                Text("Send")
                            }
                        }
                    } else {
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(4.dp),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            listOf("Enter", "Tab", "Esc", "Backspace", "Delete", "Up", "Down", "Left", "Right").forEach { key ->
                                OutlinedButton(
                                    onClick = {
                                        scope.launch {
                                            client.sendInputEvent("keyboard", 0f, 0f, "key", mapOf("key" to key))
                                        }
                                    },
                                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp)
                                ) {
                                    Text(key, style = MaterialTheme.typography.labelSmall)
                                }
                            }
                        }
                    }
                }

                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Button(
                        onClick = {
                            scope.launch {
                                client.sendInputEvent("gesture", 0f, 0f, "back")
                            }
                        },
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Back")
                    }

                    Button(
                        onClick = {
                            scope.launch {
                                client.sendInputEvent("gesture", 0f, 0f, "home")
                            }
                        },
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Home")
                    }

                    Button(
                        onClick = {
                            scope.launch {
                                client.sendInputEvent("gesture", 0f, 0f, "recent")
                            }
                        },
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Recent")
                    }

                    FilledTonalButton(
                        onClick = { showKeyboard = !showKeyboard },
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.Keyboard, contentDescription = null)
                    }
                }
            }
        }
    }
}
