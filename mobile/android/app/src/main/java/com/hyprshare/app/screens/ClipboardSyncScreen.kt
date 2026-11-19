package com.hyprshare.app.screens

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Send
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.hyprshare.app.network.HyprshareClient
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.*

data class ClipboardItem(val text: String, val timestamp: Long, val isRemote: Boolean)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ClipboardSyncScreen(
    client: HyprshareClient,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var inputText by remember { mutableStateOf("") }
    var clipboardHistory by remember { mutableStateOf<List<ClipboardItem>>(emptyList()) }

    LaunchedEffect(Unit) {
        client.clipboardUpdates.collect { content ->
            clipboardHistory = listOf(
                ClipboardItem(text = content, timestamp = System.currentTimeMillis(), isRemote = true)
            ) + clipboardHistory
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Clipboard") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    if (clipboardHistory.isNotEmpty()) {
                        IconButton(onClick = { clipboardHistory = emptyList() }) {
                            Icon(Icons.Default.Delete, contentDescription = "Clear")
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
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedTextField(
                value = inputText,
                onValueChange = { inputText = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Text to sync") },
                minLines = 2,
                maxLines = 4
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        if (inputText.isNotBlank()) {
                            scope.launch {
                                client.sendClipboard(inputText)
                            }
                            clipboardHistory = listOf(
                                ClipboardItem(text = inputText, timestamp = System.currentTimeMillis(), isRemote = false)
                            ) + clipboardHistory
                            inputText = ""
                        }
                    },
                    modifier = Modifier.weight(1f),
                    enabled = inputText.isNotBlank()
                ) {
                    Icon(Icons.Default.Send, contentDescription = null)
                    Spacer(modifier = Modifier.width(4.dp))
                    Text("Send")
                }

                OutlinedButton(
                    onClick = {
                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        val clip = clipboard.primaryClip
                        if (clip != null && clip.itemCount > 0) {
                            val text = clip.getItemAt(0).text?.toString() ?: ""
                            if (text.isNotBlank()) {
                                scope.launch {
                                    client.sendClipboard(text)
                                }
                                clipboardHistory = listOf(
                                    ClipboardItem(text = text, timestamp = System.currentTimeMillis(), isRemote = false)
                                ) + clipboardHistory
                            }
                        }
                    },
                    modifier = Modifier.weight(1f)
                ) {
                    Text("Paste from Clipboard")
                }
            }

            HorizontalDivider()

            Text(
                text = "History (${clipboardHistory.size})",
                style = MaterialTheme.typography.titleMedium
            )

            if (clipboardHistory.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "No clipboard entries yet",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(clipboardHistory) { entry ->
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            onClick = {
                                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                clipboard.setPrimaryClip(ClipData.newPlainText("hyprshare", entry.text))
                            }
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(12.dp)
                            ) {
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    AssistChip(
                                        onClick = {},
                                        label = {
                                            Text(
                                                text = if (entry.isRemote) "Remote" else "Local",
                                                style = MaterialTheme.typography.labelSmall
                                            )
                                        },
                                        modifier = Modifier.height(24.dp)
                                    )
                                    Text(
                                        text = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(entry.timestamp)),
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                                Spacer(modifier = Modifier.height(4.dp))
                                Text(
                                    text = entry.text,
                                    style = MaterialTheme.typography.bodyMedium,
                                    maxLines = 3,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
