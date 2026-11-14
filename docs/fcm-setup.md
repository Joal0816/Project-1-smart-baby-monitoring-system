# Firebase Cloud Messaging setup and delivery evidence

## Project

| Item | Value |
|---|---|
| Firebase/Cloud project | `little-monitor` |
| Android package | `com.littlemonitor.app` |
| Android config | `android/app/google-services.json` (ignored by Git) |
| Server credential | local service-account JSON, path supplied through `GOOGLE_APPLICATION_CREDENTIALS` (outside Git) |

The Firebase project and Android app were created locally on 2026-11-14. Project IDs, package names, and the public client config are not secret. The service-account private key is secret and was never written into a tracked file.

## Backend configuration

```bash
export GOOGLE_APPLICATION_CREDENTIALS=/absolute/path/service-account.json
export FIREBASE_PROJECT_ID=little-monitor
```

`firebase-admin` is installed in the ML backend environment. The backend registers FCM tokens at `POST /api/v1/app/installations` and sends one notification per unacknowledged event to every registered installation. The app receives pushes while backgrounded and uses the system notification channel; polling remains the fallback.

The Android build applies `com.google.gms.google-services` only when `android/app/google-services.json` exists. Without both local files, FCM is skipped and the app continues with its local notification fallback.

## Verified delivery — 2026-11-14

Ten distinct backend events were sent through the real Firebase Admin SDK while the Android app was backgrounded. All ten were accepted by FCM (`push_sent=1`) and all ten notification titles were observed in `dumpsys notification`:

| Trial | Event ID | FCM sent | Notification observed |
|---:|---|---:|---:|
| 1 | `46520b5a-1fdb-453b-8b3c-7cd63439059d` | yes | `FCM delivery trial 1` |
| 2 | `f44be332-c780-4744-bd13-121321608bd4` | yes | `FCM delivery trial 2` |
| 3 | `1bd508ea-812e-43aa-b650-f56e6165c9eb` | yes | `FCM delivery trial 3` |
| 4 | `ea0d4074-50d9-43d1-93a9-9f37b396d36c` | yes | `FCM delivery trial 4` |
| 5 | `693f799c-7a80-41bd-af6d-bdd71ac76069` | yes | `FCM delivery trial 5` |
| 6 | `c26d02b3-ccae-4920-8368-e4af43b67bb4` | yes | `FCM delivery trial 6` |
| 7 | `05ef6f0c-b91c-462d-869e-20cee96a83fb` | yes | `FCM delivery trial 7` |
| 8 | `87b27c24-eea5-4efe-b53e-bb1d0c5ccde0` | yes | `FCM delivery trial 8` |
| 9 | `c8d4c672-409d-48ac-99b2-0dff2e63c6e5` | yes | `FCM delivery trial 9` |
| 10 | `3fd09b1b-a972-4af9-b07e-77dd50ebd601` | yes | `FCM delivery trial 10` |

Two additional API/classifier-created cry events (`7d5c1dc8-979f-43bc-9251-442f50fc2c16` and `700863ea-9b33-4bce-b7a7-058c7a50370c`) also produced `push_sent=1` and visible `Likely baby cry detected` notifications. The ten-trial batch above is the delivery count; it is **not** the microphone-to-app capture latency set, which is recorded separately in `test-evidence.md`.

Verification commands used:

```bash
sqlite3 data/monitor.sqlite3 \
  "select count(*), sum(push_sent) from events where summary like 'FCM delivery trial%';"
adb shell dumpsys notification --noredact | grep -a 'FCM delivery trial'
```

Observed result: 10 events, 10 `push_sent=1`, and ten distinct notification titles.

## Limitations

- These ten are backend-event-to-FCM delivery trials, not ten new microphone captures. The separately recorded ten microphone-to-App trials used the local 15 s polling fallback.
- FCM acceptance (`push_sent=1`) proves the server sent the message; `dumpsys` proves the notification was displayed. Neither proves user acknowledgement.
- The service-account key and `google-services.json` are local and ignored; a fresh checkout must create its own Firebase project or obtain equivalent credentials.
