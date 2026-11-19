import SwiftUI

@main
struct HyprshareApp: App {
    @StateObject private var client = HyprshareClient()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(client)
                .onAppear {
                    if UserDefaults.standard.bool(forKey: "hyprshare_auto_connect") {
                        let host = UserDefaults.standard.string(forKey: "hyprshare_server_host") ?? "192.168.1.100"
                        let port = UInt16(UserDefaults.standard.string(forKey: "hyprshare_server_port") ?? "28556") ?? 28556
                        client.connect(host: host, port: port)
                    }
                }
        }
    }
}
