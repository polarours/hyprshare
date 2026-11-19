import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var client: HyprshareClient
    @AppStorage("hyprshare_device_name") private var deviceName = UIDevice.current.name
    @AppStorage("hyprshare_server_host") private var serverHost = "192.168.1.100"
    @AppStorage("hyprshare_server_port") private var serverPort = "28556"
    @AppStorage("hyprshare_auto_connect") private var autoConnect = false
    @AppStorage("hyprshare_tls_enabled") private var tlsEnabled = true
    @State private var showSaved = false

    var body: some View {
        NavigationStack {
            Form {
                if showSaved {
                    Section {
                        Label("Settings saved", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    }
                }

                Section("Device") {
                    TextField("Device Name", text: $deviceName)
                        .autocorrectionDisabled()
                }

                Section("Connection") {
                    TextField("Server Host", text: $serverHost)
                        .textContentType(.URL)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)

                    TextField("Server Port", text: $serverPort)
                        .keyboardType(.numberPad)

                    Toggle("Auto-connect on start", isOn: $autoConnect)
                }

                Section("Security") {
                    Toggle(isOn: $tlsEnabled) {
                        VStack(alignment: .leading) {
                            Text("TLS Encryption")
                            Text("Encrypt connections to desktop")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }

                Section("Connection Status") {
                    LabeledContent("Status", value: client.connectionStateLabel)
                    LabeledContent("Device ID", value: String(HyprshareClient.deviceId().prefix(8)) + "...")

                    Button("Connect") {
                        Task {
                            client.connect(host: serverHost, port: UInt16(serverPort) ?? 28556)
                            try? await Task.sleep(for: .seconds(1))
                            if client.isConnected {
                                _ = await client.sendAuthRequest(
                                    deviceId: HyprshareClient.deviceId(),
                                    deviceName: deviceName
                                )
                            }
                        }
                    }
                    .disabled(client.isConnected)

                    Button("Disconnect", role: .destructive) {
                        client.disconnect()
                    }
                    .disabled(!client.isConnected)
                }

                Section("About") {
                    LabeledContent("Version", value: "0.1.0")
                    LabeledContent("Protocol", value: "hyprshare/1.0")
                    Text("Linux-native sharing platform for Hyprland")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Save") {
                        saveSettings()
                        showSaved = true
                        Task {
                            try? await Task.sleep(for: .seconds(2))
                            showSaved = false
                        }
                    }
                }
            }
        }
    }

    private func saveSettings() {
        UserDefaults.standard.set(deviceName, forKey: "hyprshare_device_name")
        UserDefaults.standard.set(serverHost, forKey: "hyprshare_server_host")
        UserDefaults.standard.set(serverPort, forKey: "hyprshare_server_port")
        UserDefaults.standard.set(autoConnect, forKey: "hyprshare_auto_connect")
        UserDefaults.standard.set(tlsEnabled, forKey: "hyprshare_tls_enabled")
    }
}

extension ConnectionState {
    var label: String {
        switch self {
        case .disconnected: return "Disconnected"
        case .connecting: return "Connecting..."
        case .connected: return "Connected"
        case .error(let msg): return "Error: \(msg)"
        }
    }
}

extension HyprshareClient {
    var connectionStateLabel: String {
        connectionState.label
    }
}

#Preview {
    SettingsView()
        .environmentObject(HyprshareClient())
}
