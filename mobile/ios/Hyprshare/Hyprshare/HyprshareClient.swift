import Foundation
import Network
import CryptoKit
import SwiftUI

struct DiscoveredDevice: Identifiable {
    let id = UUID()
    let name: String
    let host: String
    let port: Int
    let capabilities: [String]

    func supports(_ cap: String) -> Bool {
        capabilities.contains(cap)
    }
}

struct SelectedFile: Identifiable {
    let id = UUID()
    let url: URL
    let name: String
    let size: Int
}

enum ConnectionState {
    case disconnected
    case connecting
    case connected
    case error(String)
}

@MainActor
class HyprshareClient: ObservableObject {
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isConnected = false
    @Published var devices: [DiscoveredDevice] = []
    @Published var screenFrameData: Data?
    @Published var lastClipboardContent: String?
    @Published var lastError: String?

    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.hyprshare.client")
    private var readBuffer = Data()

    private var host: String
    private var port: UInt16

    init(host: String = "192.168.1.100", port: UInt16 = 28556) {
        self.host = host
        self.port = port
    }

    func connect(host: String? = nil, port: UInt16? = nil) {
        if let h = host { self.host = h }
        if let p = port { self.port = p }

        connectionState = .connecting

        let params = NWParameters.tcp
        params.allowLocalEndpointReuse = true

        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(self.host),
            port: NWEndpoint.Port(rawValue: self.port)!
        )
        connection = NWConnection(to: endpoint, using: params)

        connection?.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                guard let self else { return }
                switch state {
                case .ready:
                    self.connectionState = .connected
                    self.isConnected = true
                    self.startReading()
                case .failed(let error):
                    self.connectionState = .error(error.localizedDescription)
                    self.isConnected = false
                    self.lastError = error.localizedDescription
                case .cancelled:
                    self.connectionState = .disconnected
                    self.isConnected = false
                default:
                    break
                }
            }
        }

        connection?.start(queue: queue)
    }

    func disconnect() {
        connection?.cancel()
        connection = nil
        connectionState = .disconnected
        isConnected = false
        devices = []
        screenFrameData = nil
    }

    func sendDiscover() async -> [DiscoveredDevice] {
        let message: [String: Any] = [
            "type": "discover",
            "payload": ["protocol_version": "1.0"]
        ]
        guard let response = await sendAndReceive(message),
              let payload = response["payload"] as? [String: Any],
              let services = payload["services"] as? [[String: Any]] else {
            return []
        }
        let found = services.map { svc in
            DiscoveredDevice(
                name: svc["name"] as? String ?? "Unknown",
                host: svc["host"] as? String ?? "",
                port: svc["port"] as? Int ?? 28556,
                capabilities: svc["capabilities"] as? [String] ?? []
            )
        }
        self.devices = found
        return found
    }

    func sendAuthRequest(deviceId: String, deviceName: String) async -> Bool {
        let message: [String: Any] = [
            "type": "auth_request",
            "payload": [
                "device_id": deviceId,
                "device_name": deviceName
            ]
        ]
        guard let response = await sendAndReceive(message),
              let payload = response["payload"] as? [String: Any],
              payload["status"] as? String == "ok" else {
            return false
        }
        return true
    }

    func sendCapabilities(_ caps: [String: Bool]) async {
        let message: [String: Any] = [
            "type": "capabilities",
            "payload": caps
        ]
        _ = await sendAndReceive(message)
    }

    func sendFileOffer(fileId: String, name: String, size: Int, mimeType: String, sha256: String) async -> Bool {
        let message: [String: Any] = [
            "type": "file_offer",
            "payload": [
                "file_id": fileId,
                "name": name,
                "size": size,
                "mime_type": mimeType,
                "sha256": sha256
            ]
        ]
        guard let response = await sendAndReceive(message),
              response["type"] as? String == "file_accept" else {
            return false
        }
        return true
    }

    func sendFileData(fileId: String, data: Data, offset: Int) async -> Bool {
        let header: [String: Any] = [
            "type": "file_data",
            "payload": [
                "file_id": fileId,
                "offset": offset,
                "size": data.count
            ]
        ]
        guard let headerData = try? JSONSerialization.data(withJSONObject: header),
              let headerStr = (String(data: headerData, encoding: .utf8)! + "\n").data(using: .utf8) else {
            return false
        }

        var combined = Data()
        combined.append(headerStr)
        combined.append(data)

        return await sendData(combined)
    }

    func sendFileComplete(fileId: String) async {
        let message: [String: Any] = [
            "type": "file_complete",
            "payload": ["file_id": fileId, "status": "ok"]
        ]
        _ = await sendAndReceive(message)
    }

    func sendScreenStart(quality: String = "medium", fps: Int = 30) async {
        let message: [String: Any] = [
            "type": "screen_start",
            "payload": ["quality": quality, "fps": fps]
        ]
        _ = await sendAndReceive(message)
    }

    func sendScreenStop() async {
        let message: [String: Any] = [
            "type": "screen_stop",
            "payload": [:]
        ]
        _ = await sendAndReceive(message)
    }

    func sendInputEvent(eventType: String, x: CGFloat, y: CGFloat, action: String, extras: [String: Any] = [:]) async {
        var payload: [String: Any] = [
            "event_type": eventType,
            "x": Double(x),
            "y": Double(y),
            "action": action
        ]
        for (k, v) in extras { payload[k] = v }
        let message: [String: Any] = [
            "type": "input_event",
            "payload": payload
        ]
        _ = await sendAndReceive(message)
    }

    func sendClipboard(content: String) async {
        let message: [String: Any] = [
            "type": "clipboard_sync",
            "payload": [
                "content": content,
                "timestamp": Int(Date().timeIntervalSince1970 * 1000)
            ]
        ]
        _ = await sendAndReceive(message)
    }

    func sendPing() async {
        let message: [String: Any] = [
            "type": "ping",
            "payload": ["timestamp": Int(Date().timeIntervalSince1970 * 1000)]
        ]
        _ = await sendAndReceive(message)
    }

    // MARK: - Private helpers

    private func sendAndReceive(_ message: [String: Any]) async -> [String: Any]? {
        guard let data = try? JSONSerialization.data(withJSONObject: message),
              let line = (String(data: data, encoding: .utf8)! + "\n").data(using: .utf8) else {
            return nil
        }

        return await withCheckedContinuation { continuation in
            let msgId = UUID().uuidString
            self.pendingContinuations[msgId] = continuation

            self.sendRaw(line) { [weak self] in
                self?.readNextMessage(msgId: msgId)
            }
        }
    }

    private var pendingContinuations: [String: CheckedContinuation<[String: Any]?, Never>] = [:]

    private func sendRaw(_ data: Data, completion: (() -> Void)? = nil) {
        connection?.send(content: data, completion: .contentProcessed { error in
            if let error = error {
                Task { @MainActor in
                    self.lastError = error.localizedDescription
                }
            }
            completion?()
        })
    }

    private func readNextMessage(msgId: String? = nil) {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self else { return }

            if let error = error {
                Task { @MainActor in
                    self.lastError = error.localizedDescription
                }
                return
            }

            if let data = data {
                self.readBuffer.append(data)

                while let newlineRange = self.readBuffer.range(of: Data("\n".utf8)) {
                    let lineData = self.readBuffer.subdata(in: self.readBuffer.startIndex..<newlineRange.lowerBound)
                    self.readBuffer.removeSubrange(self.readBuffer.startIndex...newlineRange.lowerBound)

                    if let json = try? JSONSerialization.jsonObject(with: lineData) as? [String: Any] {
                        let type = json["type"] as? String ?? ""

                        Task { @MainActor in
                            self.handleMessage(type: type, message: json)
                        }

                        if let msgId, let cont = self.pendingContinuations.removeValue(forKey: msgId) {
                            cont.resume(returning: json)
                            return
                        }
                    }
                }
            }

            if isComplete {
                Task { @MainActor in
                    self.connectionState = .disconnected
                    self.isConnected = false
                }
                return
            }

            if msgId != nil {
                self.readNextMessage(msgId: msgId)
            }
        }
    }

    private func handleMessage(type: String, message: [String: Any]) {
        let payload = message["payload"] as? [String: Any] ?? [:]
        switch type {
        case "screen_frame":
            if let b64 = payload["data"] as? String,
               let data = Data(base64Encoded: b64) {
                self.screenFrameData = data
            }
        case "clipboard_sync":
            self.lastClipboardContent = payload["content"] as? String
        case "cursor_update":
            break
        case "error":
            self.lastError = payload["message"] as? String ?? "Unknown error"
        case "pong":
            break
        default:
            break
        }
    }

    private func startReading() {
        readNextMessage()
    }
}

extension HyprshareClient {
    static func deviceId() -> String {
        let key = "hyprshare_device_id"
        if let existing = UserDefaults.standard.string(forKey: key) {
            return existing
        }
        let newId = UUID().uuidString
        UserDefaults.standard.set(newId, forKey: key)
        return newId
    }

    static func deviceName() -> String {
        UserDefaults.standard.string(forKey: "hyprshare_device_name") ?? UIDevice.current.name
    }

    static func computeSHA256(_ data: Data) -> String {
        let hash = SHA256.hash(data: data)
        return hash.map { String(format: "%02x", $0) }.joined()
    }

    static func mimeType(for url: URL) -> String {
        let ext = url.pathExtension.lowercased()
        switch ext {
        case "jpg", "jpeg": return "image/jpeg"
        case "png": return "image/png"
        case "gif": return "image/gif"
        case "pdf": return "application/pdf"
        case "mp4", "mov": return "video/quicktime"
        case "mp3", "wav": return "audio/mpeg"
        case "txt": return "text/plain"
        default: return "application/octet-stream"
        }
    }
}
