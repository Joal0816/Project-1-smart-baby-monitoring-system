package com.littlemonitor.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build

/** Local fallback notification for a newly observed cry event.
 *  FCM remains the remote-push path; this makes the demo observable without
 *  Firebase credentials and gives the user a clear in-app alert. */
object MonitorNotifications {
    private const val CHANNEL_ID = "monitor_cry_alerts"

    fun ensureChannel(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java)
        if (manager.getNotificationChannel(CHANNEL_ID) == null) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Cry alerts",
                NotificationManager.IMPORTANCE_HIGH,
            ).apply {
                description = "Likely baby-cry events reported by the monitor"
            }
            manager.createNotificationChannel(channel)
        }
    }

    fun showCry(context: Context, eventId: String, summary: String, confidence: Double?) {
        ensureChannel(context)
        val intent = Intent(context, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }
        val pending = PendingIntent.getActivity(
            context,
            eventId.hashCode(),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val detail = if (confidence != null) {
            "$summary · model confidence ${"%.0f".format(confidence * 100)}%"
        } else {
            summary
        }
        val notification = Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_little_monitor_logo)
            .setContentTitle("Possible baby cry")
            .setContentText(detail)
            .setStyle(Notification.BigTextStyle().bigText(detail))
            .setAutoCancel(true)
            .setContentIntent(pending)
            .build()
        context.getSystemService(NotificationManager::class.java)
            .notify(eventId.hashCode(), notification)
    }
}
