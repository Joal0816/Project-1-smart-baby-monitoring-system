package com.littlemonitor.app.data

import com.littlemonitor.app.BuildConfig
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

data class RoomReading(
    val temperature: Double?,
    val humidity: Double?,
    val light: Double?,
    val receivedAt: String?,
    val faults: List<String>,
    val capturedAt: String? = null,
    val proximity: String? = null,
    val tilt: Double? = null,
)

data class MonitorEvent(
    val id: String,
    val kind: String,
    val summary: String,
    val createdAt: String,
    val acknowledged: Boolean,
    val confidence: Double?,
    val resolved: Boolean = false,
)

data class AlertSettings(
    val temperatureMin: Double,
    val temperatureMax: Double,
    val humidityMin: Double,
    val humidityMax: Double,
    val cryThreshold: Double,
    val confirmFrames: Int,
    val clearWindows: Int,
    val staleAfterSeconds: Long,
)

data class MonitorSnapshot(
    val reading: RoomReading?,
    val events: List<MonitorEvent>,
    val online: Boolean,
    val readings: List<RoomReading> = emptyList(),
    val inferenceStatus: String = "unknown",
    val inferenceCapturedAt: String? = null,
    val inferenceCheckedAt: String? = null,
    val alertSettings: AlertSettings? = null,
)

class MonitorApi(private val config: MonitorConfig) {
    fun load(): MonitorSnapshot {
        require(isAllowedBaseUrl(config.baseUrl)) {
            "Configure the backend URL and HTTPS app token in Settings."
        }
        require(config.appToken.isNotBlank()) { "Add the backend app token in Settings." }
        val latest = get("/api/v1/devices/${path(config.deviceId)}/latest")
        val readingJson = latest.optJSONObject("reading")
        val device = latest.optJSONObject("device")
        val reading = readingJson?.let(::parseReading)
        val readings = get("/api/v1/readings?device_id=${path(config.deviceId)}&limit=50")
            .optJSONArray("items")?.let { array ->
                List(array.length()) { parseReading(array.getJSONObject(it)) }
            }.orEmpty()
        val events = get("/api/v1/events?device_id=${path(config.deviceId)}&limit=50")
            .optJSONArray("items")
            ?.let { array -> List(array.length()) { index ->
                val item = array.getJSONObject(index)
                MonitorEvent(
                    id = item.optString("event_id"),
                    kind = item.optString("kind", "event"),
                    summary = item.optString("summary", "Monitor event"),
                    createdAt = item.optString("created_at"),
                    acknowledged = !item.isNull("acknowledged_at"),
                    confidence = item.optNullableDouble("confidence"),
                    resolved = !item.isNull("resolved_at"),
                )
            } }.orEmpty()
        val online = device?.optString("network_status") == "online"
        val inference = latest.optJSONObject("inference")
        val settings = latest.optJSONObject("alert_settings")?.let { item ->
            runCatching {
                AlertSettings(
                    item.getDouble("temperature_min_c"), item.getDouble("temperature_max_c"),
                    item.getDouble("humidity_min_pct"), item.getDouble("humidity_max_pct"),
                    item.getDouble("cry_score_threshold"), item.getInt("cry_confirm_frames"),
                    item.getInt("cry_clear_windows"), item.getLong("stale_after_seconds"),
                )
            }.getOrNull()
        }
        return MonitorSnapshot(
            reading, events, online, readings,
            inference?.optString("status", "unknown") ?: "unknown",
            inference?.optString("captured_at")?.takeUnless { it.isBlank() || it == "null" },
            inference?.optString("checked_at")?.takeUnless { it.isBlank() || it == "null" },
            settings,
        )
    }

    fun acknowledge(eventId: String) {
        request(
            "/api/v1/events/${path(eventId)}/acknowledgement",
            method = "POST",
            body = "{}",
        )
    }

    fun registerInstallation(installationId: String, fcmToken: String) {
        val body = JSONObject()
            .put("installation_id", installationId)
            .put("device_id", config.deviceId)
            .put("fcm_token", fcmToken)
            .toString()
        request("/api/v1/app/installations", method = "POST", body = body)
    }

    private fun get(path: String): JSONObject = JSONObject(request(path, method = "GET"))

    private fun request(path: String, method: String, body: String? = null): String {
        val connection = URL(config.baseUrl.trimEnd('/') + path).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = method
            connection.connectTimeout = 5_000
            connection.readTimeout = 5_000
            connection.setRequestProperty("Authorization", "Bearer ${config.appToken}")
            connection.setRequestProperty("Accept", "application/json")
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            }
            val code = connection.responseCode
            val stream = if (code in 200..299) connection.inputStream else connection.errorStream
            val response = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            check(code in 200..299) { "Backend returned HTTP $code" }
            return response
        } finally {
            connection.disconnect()
        }
    }

    private fun path(value: String) = URLEncoder.encode(value, Charsets.UTF_8.name())
}

private fun isAllowedBaseUrl(url: String): Boolean =
    url.startsWith("https://", ignoreCase = true) ||
        (BuildConfig.DEBUG && (url.startsWith("http://10.0.2.2:") ||
            url.startsWith("http://127.0.0.1:")))

private fun parseReading(item: JSONObject): RoomReading {
    val faults = runCatching {
        val array = org.json.JSONArray(item.optString("sensor_faults_json", "[]"))
        List(array.length()) { array.getString(it) }
    }.getOrDefault(emptyList())
    return RoomReading(
        item.optNullableDouble("temperature_c"), item.optNullableDouble("humidity_pct"),
        item.optNullableDouble("light_lux"), item.optString("received_at").takeIf(String::isNotBlank),
        faults, item.optString("captured_at").takeIf(String::isNotBlank),
        item.optString("proximity_state").takeUnless { it.isBlank() || it == "null" },
        item.optNullableDouble("tilt_deg"),
    )
}

private fun JSONObject.optNullableDouble(key: String): Double? =
    if (isNull(key) || !has(key)) null else optDouble(key).takeIf(Double::isFinite)
