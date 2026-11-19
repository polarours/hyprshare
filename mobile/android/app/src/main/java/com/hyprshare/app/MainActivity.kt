package com.hyprshare.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.hyprshare.app.network.DiscoveredDevice
import com.hyprshare.app.network.HyprshareClient
import com.hyprshare.app.screens.*
import com.hyprshare.app.ui.theme.HyprshareTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            HyprshareTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    HyprshareNavigation()
                }
            }
        }
    }
}

@Composable
fun HyprshareNavigation() {
    val navController = rememberNavController()
    val scope = rememberCoroutineScope()
    var selectedClient by remember { mutableStateOf<HyprshareClient?>(null) }
    var selectedDevice by remember { mutableStateOf<DiscoveredDevice?>(null) }

    DisposableEffect(Unit) {
        onDispose {
            selectedClient?.disconnect()
        }
    }

    NavHost(
        navController = navController,
        startDestination = "devices"
    ) {
        composable("devices") {
            DeviceListScreen(
                onDeviceClick = { device ->
                    selectedDevice = device
                    val client = HyprshareClient(device.host, device.port)
                    selectedClient = client
                    scope.launch {
                        if (client.connect()) {
                            client.sendAuthRequest(
                                HyprshareClient.getDeviceId(navController.context),
                                HyprshareClient.getDeviceName(navController.context)
                            )
                            client.sendCapabilities(mapOf(
                                "file_transfer" to true,
                                "screen_share" to true,
                                "remote_control" to true,
                                "clipboard" to true
                            ))
                            navController.navigate("transfer") {
                                popUpTo("devices")
                            }
                        }
                    }
                },
                onSettingsClick = {
                    navController.navigate("settings")
                },
                onRefresh = { /* NSD discovery triggers automatically */ }
            )
        }

        composable("transfer") {
            selectedClient?.let { client ->
                FileTransferScreen(
                    client = client,
                    onBack = {
                        client.disconnect()
                        selectedClient = null
                        navController.popBackStack()
                    },
                    onNavigateToScreen = {
                        navController.navigate("viewer")
                    },
                    onNavigateToClipboard = {
                        navController.navigate("clipboard")
                    }
                )
            }
        }

        composable("viewer") {
            selectedClient?.let { client ->
                ScreenViewerScreen(
                    client = client,
                    onBack = { navController.popBackStack() }
                )
            }
        }

        composable("clipboard") {
            selectedClient?.let { client ->
                ClipboardSyncScreen(
                    client = client,
                    onBack = { navController.popBackStack() }
                )
            }
        }

        composable("settings") {
            SettingsScreen(
                onBack = { navController.popBackStack() }
            )
        }
    }
}
