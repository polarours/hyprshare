package com.hyprshare.app.screens

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.hyprshare.app.network.DiscoveredDevice

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceListScreen(
    onDeviceClick: (DiscoveredDevice) -> Unit,
    onSettingsClick: () -> Unit,
    onRefresh: () -> Unit
) {
    val context = LocalContext.current
    var devices by remember { mutableStateOf<List<DiscoveredDevice>>(emptyList()) }
    var isLoading by remember { mutableStateOf(false) }
    var showManualDialog by remember { mutableStateOf(false) }
    var manualHost by remember { mutableStateOf("192.168.1.100:28556") }

    fun discoverDevices() {
        isLoading = true
        val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
        val discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {
                isLoading = true
            }
            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                nsdManager.resolveService(serviceInfo, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {}
                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        val host = serviceInfo.host.hostAddress ?: return
                        val device = DiscoveredDevice(
                            name = serviceInfo.serviceName,
                            host = host,
                            port = serviceInfo.port,
                            capabilities = listOf("file_transfer", "screen_share", "remote_control", "clipboard")
                        )
                        devices = devices.filter { it.host != host || it.port != serviceInfo.port } + device
                    }
                })
            }
            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                devices = devices.filter { it.name != serviceInfo.serviceName }
            }
            override fun onDiscoveryStopped(serviceType: String) {
                isLoading = false
            }
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                isLoading = false
            }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }
        try {
            nsdManager.discoverServices("_hyprshare._tcp.", NsdManager.PROTOCOL_DNS_SD, discoveryListener)
        } catch (e: Exception) {
            isLoading = false
        }
    }

    LaunchedEffect(Unit) {
        discoverDevices()
    }

    if (showManualDialog) {
        AlertDialog(
            onDismissRequest = { showManualDialog = false },
            title = { Text("Connect Manually") },
            text = {
                OutlinedTextField(
                    value = manualHost,
                    onValueChange = { manualHost = it },
                    label = { Text("Host:Port") },
                    placeholder = { Text("192.168.1.100:28556") }
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val parts = manualHost.split(":")
                    if (parts.isNotEmpty()) {
                        val host = parts[0]
                        val port = if (parts.size > 1) parts[1].toIntOrNull() ?: 28556 else 28556
                        onDeviceClick(DiscoveredDevice("Manual", host, port, listOf("file_transfer", "screen_share", "remote_control")))
                    }
                    showManualDialog = false
                }) {
                    Text("Connect")
                }
            },
            dismissButton = {
                TextButton(onClick = { showManualDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("hyprshare") },
                actions = {
                    IconButton(onClick = onSettingsClick) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                    IconButton(onClick = {
                        devices = emptyList()
                        discoverDevices()
                    }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showManualDialog = true }) {
                Icon(Icons.Default.Add, contentDescription = "Add device")
            }
        }
    ) { padding ->
        if (isLoading && devices.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator()
                    Spacer(modifier = Modifier.height(16.dp))
                    Text("Discovering devices...")
                }
            }
        } else if (devices.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = "No devices found",
                        style = MaterialTheme.typography.titleMedium
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Make sure hyprshare is running on your desktop",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Button(onClick = {
                        manualHost = "192.168.1.100:28556"
                        showManualDialog = true
                    }) {
                        Text("Connect Manually")
                    }
                }
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(devices) { device ->
                    DeviceCard(
                        device = device,
                        onClick = { onDeviceClick(device) }
                    )
                }
            }
        }
    }
}

@Composable
fun DeviceCard(
    device: DiscoveredDevice,
    onClick: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        onClick = onClick
    ) {
        Column(
            modifier = Modifier.padding(16.dp)
        ) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleMedium
            )
            Text(
                text = "${device.host}:${device.port}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(8.dp))
            Row(
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                device.capabilities.forEach { capability ->
                    AssistChip(
                        onClick = {},
                        label = { Text(capability.replace("_", " ")) }
                    )
                }
            }
        }
    }
}
