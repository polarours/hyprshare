import SwiftUI
import PhotosUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject var client: HyprshareClient

    var body: some View {
        TabView {
            DeviceListView()
                .tabItem {
                    Label("Devices", systemImage: "network")
                }

            ClipboardSyncView()
                .tabItem {
                    Label("Clipboard", systemImage: "doc.on.clipboard")
                }

            SettingsView()
                .tabItem {
                    Label("Settings", systemImage: "gear")
                }
        }
    }
}

#Preview {
    ContentView()
        .environmentObject(HyprshareClient())
}
