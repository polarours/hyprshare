package com.hyprshare.app.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Send
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.hyprshare.app.network.HyprshareClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

data class SelectedFile(
    val uri: Uri,
    val name: String,
    val size: Long,
    val mimeType: String
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FileTransferScreen(
    client: HyprshareClient,
    onBack: () -> Unit,
    onNavigateToScreen: () -> Unit,
    onNavigateToClipboard: () -> Unit
) {
    var selectedFiles by remember { mutableStateOf<List<SelectedFile>>(emptyList()) }
    var isSending by remember { mutableStateOf(false) }
    var sendProgress by remember { mutableFloatStateOf(0f) }
    var statusMessage by remember { mutableStateOf("") }
    var currentFileName by remember { mutableStateOf("") }
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        uri?.let {
            val name = HyprshareClient.getFileName(context, it)
            val size = HyprshareClient.getFileSize(context, it)
            val mimeType = HyprshareClient.getFileMimeType(context, it)
            selectedFiles = selectedFiles + SelectedFile(it, name, size, mimeType)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("File Transfer") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
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
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Button(
                    onClick = { launcher.launch("*/*") },
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Default.Add, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Select Files")
                }
                OutlinedButton(
                    onClick = onNavigateToScreen,
                    modifier = Modifier.weight(1f)
                ) {
                    Text("Screen")
                }
                OutlinedButton(
                    onClick = onNavigateToClipboard,
                    modifier = Modifier.weight(1f)
                ) {
                    Text("Clipboard")
                }
            }

            if (statusMessage.isNotEmpty()) {
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = if (statusMessage.startsWith("Error"))
                            MaterialTheme.colorScheme.errorContainer
                        else MaterialTheme.colorScheme.secondaryContainer
                    )
                ) {
                    Text(
                        text = statusMessage,
                        modifier = Modifier.padding(12.dp),
                        color = if (statusMessage.startsWith("Error"))
                            MaterialTheme.colorScheme.onErrorContainer
                        else MaterialTheme.colorScheme.onSecondaryContainer
                    )
                }
            }

            if (selectedFiles.isNotEmpty()) {
                Text(
                    text = "Selected Files (${selectedFiles.size})",
                    style = MaterialTheme.typography.titleMedium
                )

                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    items(selectedFiles) { file ->
                        FileItem(
                            file = file,
                            onRemove = { selectedFiles = selectedFiles - file }
                        )
                    }
                }

                if (isSending) {
                    Column {
                        if (currentFileName.isNotEmpty()) {
                            Text(
                                text = "Sending: $currentFileName",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        LinearProgressIndicator(
                            progress = { sendProgress },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                } else {
                    Button(
                        onClick = {
                            isSending = true
                            statusMessage = ""
                            scope.launch {
                                var successCount = 0
                                selectedFiles.forEachIndexed { index, file ->
                                    currentFileName = file.name
                                    sendProgress = (index.toFloat() / selectedFiles.size)

                                    try {
                                        val sha256 = withContext(Dispatchers.IO) {
                                            HyprshareClient.computeFileSha256(context, file.uri)
                                        }
                                        val fileId = UUID.randomUUID().toString()

                                        val accepted = client.sendFileOffer(fileId, file.name, file.size, file.mimeType, sha256)
                                        if (!accepted) {
                                            statusMessage = "Error: ${file.name} rejected by receiver"
                                            return@forEachIndexed
                                        }

                                        withContext(Dispatchers.IO) {
                                            context.contentResolver.openInputStream(file.uri)?.use { input ->
                                                val buffer = ByteArray(65536)
                                                var offset = 0L
                                                var bytesRead: Int
                                                while (input.read(buffer).also { bytesRead = it } != -1) {
                                                    val chunk = if (bytesRead == buffer.size) buffer else buffer.copyOf(bytesRead)
                                                    client.sendFileData(fileId, chunk, offset)
                                                    offset += bytesRead
                                                    sendProgress = (index.toFloat() + offset.toFloat() / file.size) / selectedFiles.size
                                                }
                                            }
                                        }

                                        client.sendFileComplete(fileId)
                                        successCount++
                                    } catch (e: Exception) {
                                        statusMessage = "Error sending ${file.name}: ${e.message}"
                                    }
                                }
                                sendProgress = 1f
                                if (successCount > 0) {
                                    statusMessage = "Sent $successCount/${selectedFiles.size} files successfully"
                                }
                                isSending = false
                                currentFileName = ""
                                if (successCount == selectedFiles.size) {
                                    selectedFiles = emptyList()
                                }
                            }
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Default.Send, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Send ${selectedFiles.size} File(s)")
                    }
                }
            } else {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text = "No files selected",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Tap 'Select Files' to add files to send",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            if (!client.isConnected()) {
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer
                    )
                ) {
                    Text(
                        text = "Not connected. Go back and connect to a device first.",
                        modifier = Modifier.padding(12.dp),
                        color = MaterialTheme.colorScheme.onErrorContainer
                    )
                }
            }
        }
    }
}

@Composable
fun FileItem(
    file: SelectedFile,
    onRemove: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = file.name,
                    style = MaterialTheme.typography.bodyMedium
                )
                Text(
                    text = "${formatFileSize(file.size)} · ${file.mimeType}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            IconButton(onClick = onRemove) {
                Icon(
                    Icons.Default.Delete,
                    contentDescription = "Remove",
                    tint = MaterialTheme.colorScheme.error
                )
            }
        }
    }
}

fun formatFileSize(size: Long): String {
    val units = arrayOf("B", "KB", "MB", "GB")
    var value = size.toDouble()
    var unitIndex = 0
    while (value >= 1024 && unitIndex < units.size - 1) {
        value /= 1024
        unitIndex++
    }
    return "%.1f %s".format(value, units[unitIndex])
}
