package com.hyprshare.app.network

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.provider.OpenableColumns
import com.google.gson.Gson
import com.google.gson.JsonObject
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.InputStream
import java.io.InputStreamReader
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.Socket
import java.security.MessageDigest
import java.util.UUID
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

data class Message(
    val type: String,
    val payload: JsonObject = JsonObject()
)

data class DiscoveredDevice(
    val name: String,
    val host: String,
    val port: Int,
    val capabilities: List<String>
)

data class FileOffer(
    val fileId: String,
    val name: String,
    val size: Long,
    val mimeType: String,
    val sha256: String = ""
)

data class ScreenFrame(
    val frameId: Long,
    val timestamp: Long,
    val width: Int,
    val height: Int,
    val format: String,
    val data: ByteArray
)

class HyprshareClient(
    private val host: String,
    private val port: Int = 28556
) {
    private var socket: Socket? = null
    private var reader: BufferedReader? = null
    private var writer: OutputStreamWriter? = null
    private var rawInput: InputStream? = null
    private var rawOutput: OutputStream? = null
    private val gson = Gson()
    private var connected = false

    private val _screenFrames = MutableSharedFlow<ScreenFrame>(extraBufferCapacity = 5)
    val screenFrames: SharedFlow<ScreenFrame> = _screenFrames

    private val _clipboardUpdates = MutableSharedFlow<String>(extraBufferCapacity = 5)
    val clipboardUpdates: SharedFlow<String> = _clipboardUpdates

    private val _errors = MutableSharedFlow<String>(extraBufferCapacity = 5)
    val errors: SharedFlow<String> = _errors

    fun isConnected(): Boolean = connected

    suspend fun connect(): Boolean = withContext(Dispatchers.IO) {
        try {
            val trustAllCerts = arrayOf<TrustManager>(object : X509TrustManager {
                override fun checkClientTrusted(chain: Array<out java.security.cert.X509Certificate>?, authType: String?) {}
                override fun checkServerTrusted(chain: Array<out java.security.cert.X509Certificate>?, authType: String?) {}
                override fun getAcceptedIssuers(): Array<java.security.cert.X509Certificate> = arrayOf()
            })

            val sslContext = SSLContext.getInstance("TLS")
            sslContext.init(null, trustAllCerts, java.security.SecureRandom())
            val factory: SSLSocketFactory = sslContext.socketFactory

            socket = factory.createSocket()
            socket!!.connect(InetSocketAddress(host, port), 5000)
            socket!!.soTimeout = 30000

            rawInput = socket!!.getInputStream()
            rawOutput = socket!!.getOutputStream()
            reader = BufferedReader(InputStreamReader(rawInput))
            writer = OutputStreamWriter(rawOutput)
            connected = true
            true
        } catch (e: Exception) {
            connected = false
            false
        }
    }

    suspend fun sendDiscover(): List<DiscoveredDevice> = withContext(Dispatchers.IO) {
        val payload = JsonObject().apply {
            addProperty("protocol_version", "1.0")
        }
        sendRawMessage("discover", payload)
        // Responses are envelope messages: {"type":..., "payload":{...}}.
        val response = readRawMessage()
        val body = response.getAsJsonObject("payload") ?: JsonObject()
        val services = body.getAsJsonArray("services") ?: return@withContext emptyList()
        services.map { element ->
            val obj = element.asJsonObject
            DiscoveredDevice(
                name = obj.get("name")?.asString ?: "",
                host = obj.get("host")?.asString ?: "",
                port = obj.get("port")?.asInt ?: 0,
                capabilities = obj.getAsJsonArray("capabilities")?.map { it.asString } ?: emptyList()
            )
        }
    }

    suspend fun sendAuthRequest(deviceId: String, deviceName: String): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("device_id", deviceId)
                addProperty("device_name", deviceName)
            }
            sendRawMessage("auth_request", payload)
            val response = readRawMessage()
            val body = response.getAsJsonObject("payload") ?: JsonObject()
            body.get("status")?.asString == "ok"
        }

    suspend fun sendCapabilities(capabilities: Map<String, Boolean>): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                capabilities.forEach { (key, value) -> addProperty(key, value) }
            }
            sendRawMessage("capabilities", payload)
            true
        }

    suspend fun sendFileOffer(fileId: String, name: String, size: Long, mimeType: String, sha256: String = ""): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("file_id", fileId)
                addProperty("name", name)
                addProperty("size", size)
                addProperty("mime_type", mimeType)
                addProperty("sha256", sha256)
            }
            sendRawMessage("file_offer", payload)
            val response = readRawMessage()
            response.get("type")?.asString == "file_accept"
        }

    suspend fun sendFileData(fileId: String, data: ByteArray, offset: Long): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("file_id", fileId)
                addProperty("offset", offset)
                addProperty("size", data.size)
            }
            val header = gson.toJson(Message("file_data", payload)) + "\n"
            rawOutput?.write(header.toByteArray())
            rawOutput?.write(data)
            rawOutput?.flush()
            true
        }

    suspend fun sendFileComplete(fileId: String): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("file_id", fileId)
                addProperty("status", "ok")
            }
            sendRawMessage("file_complete", payload)
            true
        }

    suspend fun sendScreenStart(quality: String = "medium", fps: Int = 30): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("quality", quality)
                addProperty("fps", fps)
            }
            sendRawMessage("screen_start", payload)
            true
        }

    suspend fun sendScreenStop(): Boolean =
        withContext(Dispatchers.IO) {
            sendRawMessage("screen_stop", JsonObject())
            true
        }

    suspend fun sendInputEvent(eventType: String, x: Float, y: Float, action: String, extras: Map<String, Any> = emptyMap()): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("event_type", eventType)
                addProperty("x", x)
                addProperty("y", y)
                addProperty("action", action)
                extras.forEach { (key, value) ->
                    when (value) {
                        is String -> addProperty(key, value)
                        is Number -> addProperty(key, value.toFloat())
                        is Boolean -> addProperty(key, value)
                    }
                }
            }
            sendRawMessage("input_event", payload)
            true
        }

    suspend fun sendClipboard(content: String): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("content", content)
                addProperty("timestamp", System.currentTimeMillis())
            }
            sendRawMessage("clipboard_sync", payload)
            true
        }

    suspend fun sendPing(): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("timestamp", System.currentTimeMillis())
            }
            sendRawMessage("ping", payload)
            true
        }

    suspend fun sendPong(): Boolean =
        withContext(Dispatchers.IO) {
            val payload = JsonObject().apply {
                addProperty("timestamp", System.currentTimeMillis())
            }
            sendRawMessage("pong", payload)
            true
        }

    suspend fun readMessageLoop() = withContext(Dispatchers.IO) {
        while (connected) {
            try {
                val line = reader?.readLine() ?: break
                if (line.isEmpty()) continue
                val json = gson.fromJson(line, JsonObject::class.java) ?: continue
                val type = json.get("type")?.asString ?: continue
                val payload = json.getAsJsonObject("payload") ?: JsonObject()

                when (type) {
                    "screen_frame" -> {
                        val frameId = payload.get("frame_id")?.asLong ?: 0
                        val timestamp = payload.get("timestamp")?.asLong ?: 0
                        val width = payload.get("width")?.asInt ?: 0
                        val height = payload.get("height")?.asInt ?: 0
                        val format = payload.get("format")?.asString ?: "raw"
                        val dataSize = payload.get("data_size")?.asInt ?: 0
                        val frameData = if (dataSize > 0) {
                            val buf = ByteArray(dataSize)
                            var read = 0
                            while (read < dataSize) {
                                val n = rawInput?.read(buf, read, dataSize - read) ?: break
                                if (n < 0) break
                                read += n
                            }
                            buf
                        } else ByteArray(0)
                        _screenFrames.emit(ScreenFrame(frameId, timestamp, width, height, format, frameData))
                    }
                    "clipboard_sync" -> {
                        val content = payload.get("content")?.asString ?: ""
                        _clipboardUpdates.emit(content)
                    }
                    "cursor_update" -> {}
                    "file_offer" -> {}
                    "file_accept" -> {}
                    "file_complete" -> {}
                    "pong" -> {}
                    "error" -> {
                        val message = payload.get("message")?.asString ?: "Unknown error"
                        _errors.emit(message)
                    }
                    "ping" -> sendPong()
                }
            } catch (e: Exception) {
                if (connected) {
                    _errors.emit("Connection error: ${e.message}")
                }
                break
            }
        }
        connected = false
    }

    private fun sendRawMessage(type: String, payload: JsonObject) {
        val msg = JsonObject().apply {
            addProperty("type", type)
            add("payload", payload)
        }
        val json = gson.toJson(msg) + "\n"
        writer?.write(json)
        writer?.flush()
    }

    private fun readRawMessage(): JsonObject {
        val line = reader?.readLine() ?: return JsonObject()
        return gson.fromJson(line, JsonObject::class.java) ?: JsonObject()
    }

    fun disconnect() {
        connected = false
        try {
            reader?.close()
            writer?.close()
            rawInput?.close()
            rawOutput?.close()
            socket?.close()
        } catch (_: Exception) {}
    }

    companion object {
        fun getDeviceId(context: Context): String {
            val prefs = context.getSharedPreferences("hyprshare", Context.MODE_PRIVATE)
            var id = prefs.getString("device_id", null)
            if (id == null) {
                id = UUID.randomUUID().toString()
                prefs.edit().putString("device_id", id).apply()
            }
            return id
        }

        fun getDeviceName(context: Context): String {
            val prefs = context.getSharedPreferences("hyprshare", Context.MODE_PRIVATE)
            return prefs.getString("device_name", android.os.Build.MODEL) ?: android.os.Build.MODEL
        }

        fun computeFileSha256(context: Context, uri: Uri): String {
            val digest = MessageDigest.getInstance("SHA-256")
            context.contentResolver.openInputStream(uri)?.use { stream ->
                val buffer = ByteArray(8192)
                var read: Int
                while (stream.read(buffer).also { read = it } != -1) {
                    digest.update(buffer, 0, read)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }

        fun getFileName(context: Context, uri: Uri): String {
            val cursor = context.contentResolver.query(uri, null, null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val nameIndex = it.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (nameIndex >= 0) return it.getString(nameIndex)
                }
            }
            return uri.lastPathSegment ?: "unknown"
        }

        fun getFileSize(context: Context, uri: Uri): Long {
            val cursor = context.contentResolver.query(uri, null, null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val sizeIndex = it.getColumnIndex(OpenableColumns.SIZE)
                    if (sizeIndex >= 0) return it.getLong(sizeIndex)
                }
            }
            return 0L
        }

        fun getFileMimeType(context: Context, uri: Uri): String {
            return context.contentResolver.getType(uri) ?: "application/octet-stream"
        }
    }
}
