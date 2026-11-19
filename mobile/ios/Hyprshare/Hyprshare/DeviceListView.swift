import SwiftUI

struct DeviceListView: View {
    @EnvironmentObject var client: HyprshareClient
    @State private var isLoading = false
    @State private var showManualConnect = false
    @State private var manualHost = ""
    @State private var manualPort = "28556"

    var body: some View {
        NavigationStack {
            Group {
                if client.devices.isEmpty && !isLoading {
                    ContentUnavailableView(
                        "No Devices Found",
                        systemImage: "wifi.slash",
                        description: Text("Tap refresh to discover devices on your network, or connect manually.")
                    )
                } else {
                    List(client.devices) { device in
                        NavigationLink(destination: DeviceDetailView(device: device)) {
                            DeviceRow(device: device)
                        }
                    }
                    .refreshable {
                        await discoverDevices()
                    }
                }
            }
            .navigationTitle("hyprshare")
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button(action: { showManualConnect = true }) {
                        Image(systemName: "plus")
                    }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button(action: { Task { await discoverDevices() } }) {
                        if isLoading {
                            ProgressView()
                        } else {
                            Image(systemName: "arrow.clockwise")
                        }
                    }
                }
            }
            .alert("Connect Manually", isPresented: $showManualConnect) {
                TextField("Host", text: $manualHost)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                TextField("Port", text: $manualPort)
                    .keyboardType(.numberPad)
                Button("Connect") {
                    Task {
                        let port = UInt16(manualPort) ?? 28556
                        client.connect(host: manualHost, port: port)
                        try? await Task.sleep(for: .seconds(1))
                        let authed = await client.sendAuthRequest(
                            deviceId: HyprshareClient.deviceId(),
                            deviceName: HyprshareClient.deviceName()
                        )
                        if authed {
                            _ = await client.sendDiscover()
                        }
                    }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Enter the host and port of a hyprshare desktop.")
            }
        }
    }

    private func discoverDevices() async {
        isLoading = true
        defer { isLoading = false }

        if !client.isConnected {
            client.connect()
            try? await Task.sleep(for: .seconds(1))
        }

        guard client.isConnected else { return }

        let authed = await client.sendAuthRequest(
            deviceId: HyprshareClient.deviceId(),
            deviceName: HyprshareClient.deviceName()
        )
        guard authed else { return }

        _ = await client.sendDiscover()
    }
}

struct DeviceRow: View {
    let device: DiscoveredDevice

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Image(systemName: deviceIcon)
                    .font(.title2)
                    .foregroundStyle(.blue)
                Text(device.name)
                    .font(.headline)
            }
            Text("\(device.host):\(device.port)")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(device.capabilities, id: \.self) { cap in
                        Text(capLabel(cap))
                            .font(.caption2)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 3)
                            .background(capColor(cap).opacity(0.12))
                            .foregroundStyle(capColor(cap))
                            .clipShape(Capsule())
                    }
                }
            }
        }
        .padding(.vertical, 4)
    }

    private var deviceIcon: String {
        if device.capabilities.contains("screen_share") { return "display" }
        if device.capabilities.contains("file_transfer") { return "desktopcomputer" }
        return "desktopcomputer"
    }

    private func capLabel(_ cap: String) -> String {
        switch cap {
        case "file_transfer": return "Files"
        case "screen_share": return "Screen"
        case "remote_control": return "Remote"
        case "clipboard": return "Clipboard"
        default: return cap
        }
    }

    private func capColor(_ cap: String) -> Color {
        switch cap {
        case "file_transfer": return .blue
        case "screen_share": return .green
        case "remote_control": return .orange
        case "clipboard": return .purple
        default: return .gray
        }
    }
}

struct DeviceDetailView: View {
    let device: DiscoveredDevice
    @EnvironmentObject var client: HyprshareClient
    @State private var isConnected = false

    var body: some View {
        List {
            if isConnected {
                Section("Connected") {
                    if device.supports("screen_share") {
                        NavigationLink {
                            ScreenViewerView(device: device)
                        } label: {
                            Label("Screen Share", systemImage: "display")
                        }
                    }
                    if device.supports("file_transfer") {
                        NavigationLink {
                            FileTransferView(device: device)
                        } label: {
                            Label("File Transfer", systemImage: "arrow.up.arrow.down.circle")
                        }
                    }
                    if device.supports("clipboard") || device.supports("remote_control") {
                        NavigationLink {
                            ClipboardSyncView(device: device)
                        } label: {
                            Label("Clipboard Sync", systemImage: "doc.on.clipboard")
                        }
                    }
                }
            }

            Section("Info") {
                LabeledContent("Host", value: device.host)
                LabeledContent("Port", value: "\(device.port)")
                LabeledContent("Capabilities", value: device.capabilities.joined(separator: ", "))
            }

            Section {
                if isConnected {
                    Button("Disconnect", role: .destructive) {
                        client.disconnect()
                        isConnected = false
                    }
                } else {
                    Button("Connect") {
                        Task {
                            client.connect(host: device.host, port: UInt16(device.port))
                            try? await Task.sleep(for: .seconds(1))
                            isConnected = client.isConnected
                            if isConnected {
                                let authed = await client.sendAuthRequest(
                                    deviceId: HyprshareClient.deviceId(),
                                    deviceName: HyprshareClient.deviceName()
                                )
                                if !authed {
                                    isConnected = false
                                }
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle(device.name)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            isConnected = client.isConnected
        }
    }
}

#Preview {
    DeviceListView()
        .environmentObject(HyprshareClient())
}
