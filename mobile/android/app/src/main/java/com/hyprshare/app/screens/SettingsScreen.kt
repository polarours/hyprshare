package com.hyprshare.app.screens

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("hyprshare", Context.MODE_PRIVATE) }

    var deviceName by remember { mutableStateOf(prefs.getString("device_name", android.os.Build.MODEL) ?: android.os.Build.MODEL) }
    var serverHost by remember { mutableStateOf(prefs.getString("server_host", "192.168.1.100") ?: "192.168.1.100") }
    var serverPort by remember { mutableStateOf(prefs.getString("server_port", "28556") ?: "28556") }
    var autoConnect by remember { mutableStateOf(prefs.getBoolean("auto_connect", false)) }
    var tlsEnabled by remember { mutableStateOf(prefs.getBoolean("tls_enabled", true)) }
    var saved by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(onClick = {
                        prefs.edit()
                            .putString("device_name", deviceName)
                            .putString("server_host", serverHost)
                            .putString("server_port", serverPort)
                            .putBoolean("auto_connect", autoConnect)
                            .putBoolean("tls_enabled", tlsEnabled)
                            .apply()
                        saved = true
                    }) {
                        Icon(Icons.Default.Save, contentDescription = "Save")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            if (saved) {
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer
                    )
                ) {
                    Text(
                        text = "Settings saved",
                        modifier = Modifier.padding(16.dp),
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
            }

            Text(
                text = "Device",
                style = MaterialTheme.typography.titleMedium
            )

            OutlinedTextField(
                value = deviceName,
                onValueChange = { deviceName = it },
                label = { Text("Device Name") },
                modifier = Modifier.fillMaxWidth()
            )

            HorizontalDivider()

            Text(
                text = "Connection",
                style = MaterialTheme.typography.titleMedium
            )

            OutlinedTextField(
                value = serverHost,
                onValueChange = { serverHost = it },
                label = { Text("Server Host") },
                modifier = Modifier.fillMaxWidth()
            )

            OutlinedTextField(
                value = serverPort,
                onValueChange = { serverPort = it },
                label = { Text("Server Port") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "Auto-connect on start",
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.weight(1f)
                )
                Switch(
                    checked = autoConnect,
                    onCheckedChange = { autoConnect = it }
                )
            }

            HorizontalDivider()

            Text(
                text = "Security",
                style = MaterialTheme.typography.titleMedium
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "TLS Encryption",
                        style = MaterialTheme.typography.bodyLarge
                    )
                    Text(
                        text = "Encrypt connections to desktop",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = tlsEnabled,
                    onCheckedChange = { tlsEnabled = it }
                )
            }

            HorizontalDivider()

            Text(
                text = "About",
                style = MaterialTheme.typography.titleMedium
            )

            Text(
                text = "hyprshare v0.1.0",
                style = MaterialTheme.typography.bodyMedium
            )

            Text(
                text = "Linux-native sharing platform for Hyprland",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
