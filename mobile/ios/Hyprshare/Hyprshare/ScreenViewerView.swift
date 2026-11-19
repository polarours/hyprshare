import SwiftUI

struct ScreenViewerView: View {
    let device: DiscoveredDevice
    @EnvironmentObject var client: HyprshareClient
    @State private var scale: CGFloat = 1.0
    @State private var lastScale: CGFloat = 1.0
    @State private var offset: CGSize = .zero
    @State private var lastOffset: CGSize = .zero
    @State private var isStreaming = false
    @State private var remoteControlMode = RemoteControlMode.touch
    @State private var remoteCursorX: CGFloat = 0
    @State private var remoteCursorY: CGFloat = 0
    @State private var showCursor = false

    enum RemoteControlMode: String, CaseIterable {
        case touch = "Touch"
        case keyboard = "Keyboard"
        case gesture = "Gesture"

        var icon: String {
            switch self {
            case .touch: return "hand.tap"
            case .keyboard: return "keyboard"
            case .gesture: return "hand.draw"
            }
        }
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if let frameData = client.screenFrameData,
               let uiImage = UIImage(data: frameData) {
                Image(uiImage: uiImage)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .scaleEffect(scale)
                    .offset(offset)
                    .gesture(magnificationGesture)
                    .gesture(dragGesture)
                    .gesture(tapGesture)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "display.trianglebadge.exclamationmark")
                        .font(.system(size: 48))
                        .foregroundStyle(.secondary)
                    Text(isStreaming ? "Waiting for frames..." : "Tap Start to begin viewing")
                        .foregroundStyle(.secondary)
                }
            }

            if showCursor {
                Circle()
                    .fill(.white)
                    .frame(width: 8, height: 8)
                    .position(x: remoteCursorX, y: remoteCursorY)
                    .shadow(color: .black, radius: 2)
                    .allowsHitTesting(false)
            }

            VStack {
                HStack {
                    Spacer()
                    if isStreaming {
                        Text("LIVE")
                            .font(.caption.bold())
                            .foregroundStyle(.white)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(.red)
                            .clipShape(Capsule())
                    }
                }
                .padding(.horizontal)
                .padding(.top, 8)

                Spacer()

                HStack(spacing: 16) {
                    ForEach(RemoteControlMode.allCases, id: \.self) { mode in
                        Button(action: { remoteControlMode = mode }) {
                            VStack(spacing: 4) {
                                Image(systemName: mode.icon)
                                    .font(.title3)
                                Text(mode.rawValue)
                                    .font(.caption2)
                            }
                            .frame(width: 72, height: 56)
                            .background(remoteControlMode == mode ? Color.blue : Color.gray.opacity(0.4))
                            .foregroundStyle(.white)
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                        }
                    }
                }
                .padding(.bottom, 8)

                HStack(spacing: 16) {
                    Button(action: { Task { await toggleStreaming() } }) {
                        Label(isStreaming ? "Stop" : "Start",
                              systemImage: isStreaming ? "stop.fill" : "play.fill")
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 10)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(isStreaming ? .red : .green)

                    Menu {
                        ForEach(["Low", "Medium", "High"], id: \.self) { quality in
                            Button(quality) {
                                Task {
                                    await client.sendScreenStop()
                                    await client.sendScreenStart(
                                        quality: quality.lowercased(),
                                        fps: quality == "High" ? 60 : 30
                                    )
                                }
                            }
                        }
                    } label: {
                        Image(systemName: "slider.horizontal.3")
                            .frame(width: 44, height: 44)
                    }
                    .buttonStyle(.bordered)
                    .disabled(!isStreaming)
                }
                .padding(.horizontal)
                .padding(.bottom, 12)
            }
        }
        .navigationTitle(device.name)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear {
            client.connect(host: device.host, port: UInt16(device.port))
        }
        .onDisappear {
            Task {
                await client.sendScreenStop()
                isStreaming = false
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .cursorUpdate)) { notification in
            if let x = notification.userInfo?["x"] as? CGFloat,
               let y = notification.userInfo?["y"] as? CGFloat {
                remoteCursorX = x
                remoteCursorY = y
                showCursor = true
            }
        }
    }

    private var magnificationGesture: some Gesture {
        MagnificationGesture()
            .onChanged { value in
                scale = lastScale * value
            }
            .onEnded { _ in
                lastScale = scale
            }
    }

    private var dragGesture: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let newX = lastOffset.width + value.translation.width
                let newY = lastOffset.height + value.translation.height
                offset = CGSize(width: newX, height: newY)
            }
            .onEnded { value in
                lastOffset = offset
            }
    }

    private var tapGesture: some Gesture {
        SpatialTapGesture()
            .onEnded { value in
                guard isStreaming else { return }
                Task {
                    await client.sendInputEvent(
                        eventType: "touch",
                        x: value.location.x,
                        y: value.location.y,
                        action: "tap"
                    )
                }
            }
    }

    private func toggleStreaming() async {
        if isStreaming {
            await client.sendScreenStop()
            isStreaming = false
        } else {
            await client.sendScreenStart(quality: "medium", fps: 30)
            isStreaming = true
        }
    }
}

extension Notification.Name {
    static let cursorUpdate = Notification.Name("hyprshareCursorUpdate")
}

#Preview {
    NavigationStack {
        ScreenViewerView(device: DiscoveredDevice(
            name: "Desktop",
            host: "192.168.1.100",
            port: 28556,
            capabilities: ["screen_share", "remote_control"]
        ))
    }
    .environmentObject(HyprshareClient())
}
