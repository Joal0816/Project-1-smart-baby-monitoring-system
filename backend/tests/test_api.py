from datetime import datetime, timedelta, timezone
import json

from fastapi.testclient import TestClient

from backend.app.config import Settings
from backend.app.db import Database
from backend.app.main import create_app
from backend.app.classifier import InferenceResult, InferenceUnavailable, confirms_cry, longest_positive_streak


def make_settings(path=":memory:"):
    return Settings(
        database_path=path,
        app_token="test-app-token",
        device_tokens={"rt-spark-01": "test-device-token"},
        allowed_origins=("http://localhost:4174",),
        temperature_min_c=18,
        temperature_max_c=30,
        humidity_min_pct=30,
        humidity_max_pct=70,
        cry_score_threshold=0.4,
        yamnet_model_handle="unused",
        yamnet_class_map_url="unused",
        firebase_project_id="",
    )


class ConfirmingClassifier:
    def predict_pcm16(self, pcm_bytes, sample_rate_hz):
        assert len(pcm_bytes) == 64_000
        assert sample_rate_hz == 16_000
        return type("Result", (), {
            "crying": True,
            "confidence": 0.81,
            "label": "baby_crying",
            "consecutive_windows": 2,
        })()


def test_health_and_authentication():
    client = TestClient(create_app(make_settings(), Database(":memory:"), classifier=None))
    assert client.get("/health").json()["status"] == "ok"
    response = client.get("/api/v1/events", headers={"Authorization": "Bearer wrong"})
    assert response.status_code == 401


def test_telemetry_stores_and_environment_alerts_are_debounced():
    app = create_app(make_settings(), Database(":memory:"), classifier=None,
                     push_sender=lambda *_: 0)
    client = TestClient(app)
    headers = {"Authorization": "Bearer test-device-token"}
    for sequence in range(3):
        response = client.post("/api/v1/devices/rt-spark-01/telemetry", headers=headers, json={
            "captured_at": datetime.now(timezone.utc).isoformat(),
            "sequence": sequence,
            "temperature_c": 32,
            "humidity_pct": 55,
            "light_lux": 45,
            "proximity_state": "clear",
            "tilt_deg": 1.5,
        })
        assert response.status_code == 200
    events = client.get("/api/v1/events?device_id=rt-spark-01",
                        headers={"Authorization": "Bearer test-app-token"}).json()["items"]
    temperature_events = [event for event in events if event["kind"] == "environment"]
    assert len(temperature_events) == 1
    assert json.loads(temperature_events[0]["details_json"])["field"] == "temperature_c"


def test_audio_window_is_bounded_authenticated_and_evented():
    app = create_app(make_settings(), Database(":memory:"), classifier=ConfirmingClassifier(),
                     push_sender=lambda *_: 0)
    client = TestClient(app)
    headers = {
        "Authorization": "Bearer test-device-token",
        "X-Captured-At": datetime.now(timezone.utc).isoformat(),
        "X-Sequence": "5",
        "X-Sample-Rate-Hz": "16000",
        "Content-Type": "application/octet-stream",
    }
    result = client.post("/api/v1/devices/rt-spark-01/audio-windows", headers=headers,
                         content=b"\x00" * 64_000)
    assert result.status_code == 200
    assert result.json()["confirmed"] is True
    assert result.json()["event"]["kind"] == "cry"
    assert client.post("/api/v1/devices/rt-spark-01/audio-windows", headers=headers,
                       content=b"\x00" * 12).status_code == 422


def test_acknowledgement_and_app_history():
    database = Database(":memory:")
    database.ensure_device("rt-spark-01")
    event, _ = database.create_event("rt-spark-01", "cry", "Test cry", datetime.now(timezone.utc).isoformat())
    client = TestClient(create_app(make_settings(), database, classifier=None))
    auth = {"Authorization": "Bearer test-app-token"}
    response = client.post(f"/api/v1/events/{event['event_id']}/acknowledgement", headers=auth, json={})
    assert response.status_code == 200
    assert response.json()["event"]["acknowledged_at"] is not None
    assert len(client.get("/api/v1/events", headers=auth).json()["items"]) == 1


def test_event_idempotency_prevents_duplicate_rows():
    database = Database(":memory:")
    event_one, created_one = database.create_event(
        "rt-spark-01", "cry", "Cry", datetime.now(timezone.utc).isoformat(),
        idempotency_key="rt-spark-01:audio:7",
    )
    event_two, created_two = database.create_event(
        "rt-spark-01", "cry", "Cry retry", datetime.now(timezone.utc).isoformat(),
        idempotency_key="rt-spark-01:audio:7",
    )
    assert created_one is True
    assert created_two is False
    assert event_one["event_id"] == event_two["event_id"]


def test_consecutive_window_confirmation():
    assert confirms_cry([0.7, 0.75, 0.1], 0.4, required=2)
    assert not confirms_cry([0.7, 0.1, 0.8], 0.4, required=2)
    assert longest_positive_streak([0.7, 0.75, 0.1], 0.4) == 2


APP_AUTH = {"Authorization": "Bearer test-app-token"}
DEVICE_URL = "/api/v1/devices/rt-spark-01"


class MutableClassifier:
    crying = True
    unavailable = False

    def predict_pcm16(self, *_):
        if self.unavailable:
            raise InferenceUnavailable("test model failure")
        return InferenceResult(self.crying, 0.8 if self.crying else 0.1,
                               "baby_crying" if self.crying else "background", 2 if self.crying else 0)


def upload_audio(client, sequence, captured_at):
    return client.post(DEVICE_URL + "/audio-windows", content=b"\0" * 64_000, headers={
        "Authorization": "Bearer test-device-token",
        "X-Captured-At": captured_at.isoformat(),
        "X-Sequence": str(sequence), "X-Sample-Rate-Hz": "16000",
    })


def test_sustained_cry_groups_events_even_after_acknowledgement_and_restart(tmp_path):
    path = str(tmp_path / "monitor.sqlite3")
    pushes = []
    model = MutableClassifier()
    client = TestClient(create_app(make_settings(path), classifier=model,
                                  push_sender=lambda _, event, __: pushes.append(event["event_id"]) or 1))
    start = datetime.now(timezone.utc) - timedelta(seconds=50)
    first = upload_audio(client, 0, start).json()["event"]["event_id"]
    client.post(f"/api/v1/events/{first}/acknowledgement", headers=APP_AUTH, json={})
    for sequence in range(1, 5):
        assert upload_audio(client, sequence, start + timedelta(seconds=sequence * 2)).json()["event"]["event_id"] == first
    restarted = TestClient(create_app(make_settings(path), classifier=model,
                                     push_sender=lambda *_: pushes.append("unexpected") or 1))
    assert upload_audio(restarted, 5, start + timedelta(seconds=10)).json()["event"]["event_id"] == first
    assert pushes == [first]


def test_three_background_windows_clear_episode_and_rearm_without_retry_duplicates():
    db = Database(":memory:")
    model = MutableClassifier()
    pushes = []
    client = TestClient(create_app(make_settings(), db, classifier=model,
                                  push_sender=lambda _, event, __: pushes.append(event["event_id"]) or 1))
    start = datetime.now(timezone.utc) - timedelta(seconds=30)
    first = upload_audio(client, 0, start).json()["event"]["event_id"]
    model.crying = False
    for seq in range(1, 4):
        assert upload_audio(client, seq, start + timedelta(seconds=2 * seq)).status_code == 200
    assert db.event(first)["resolved_at"] is not None
    assert client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "background"
    # A delayed retry must not re-open an already cleared episode.
    assert upload_audio(client, 0, start).json()["event"]["event_id"] == first
    assert len(pushes) == 1
    model.crying = True
    second = upload_audio(client, 0, start + timedelta(seconds=10)).json()["event"]["event_id"]
    assert second != first  # Sequence may reset after a device restart.
    assert len(pushes) == 2


def test_out_of_order_audio_does_not_overwrite_current_state_or_notify():
    db = Database(":memory:")
    model = MutableClassifier()
    model.crying = False
    client = TestClient(create_app(make_settings(), db, classifier=model, push_sender=lambda *_: 0))
    now = datetime.now(timezone.utc)
    assert upload_audio(client, 2, now - timedelta(seconds=2)).status_code == 200
    model.crying = True
    assert upload_audio(client, 1, now - timedelta(seconds=10)).json()["event"] is None
    assert db.inference_status("rt-spark-01")["status"] == "background"
    assert db.events("rt-spark-01") == []


def test_audio_gap_starts_new_episode():
    db = Database(":memory:")
    client = TestClient(create_app(make_settings(), db, classifier=MutableClassifier(), push_sender=lambda *_: 0))
    now = datetime.now(timezone.utc)
    first = upload_audio(client, 0, now - timedelta(seconds=300)).json()["event"]["event_id"]
    second = upload_audio(client, 1, now - timedelta(seconds=2)).json()["event"]["event_id"]
    assert first != second
    assert db.event(first)["resolved_at"] is not None


def test_inference_fault_and_recovery_are_visible_and_retryable():
    db = Database(":memory:")
    model = MutableClassifier()
    client = TestClient(create_app(make_settings(), db, classifier=model, push_sender=lambda *_: 0))
    assert client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "unknown"
    now = datetime.now(timezone.utc) - timedelta(seconds=2)
    model.unavailable = True
    assert upload_audio(client, 0, now).status_code == 503
    assert client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "unavailable"
    model.unavailable = False
    assert upload_audio(client, 0, now).status_code == 200
    assert client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "crying"


def test_disabled_and_old_inference_are_not_reported_as_current_background():
    disabled = TestClient(create_app(make_settings(), Database(":memory:"), classifier=None))
    assert disabled.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "unavailable"
    model = MutableClassifier()
    model.crying = False
    client = TestClient(create_app(make_settings(), Database(":memory:"), classifier=model))
    assert upload_audio(client, 0, datetime.now(timezone.utc) - timedelta(minutes=10)).status_code == 200
    assert client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()["inference"]["status"] == "stale"


def test_invalid_audio_sequence_and_future_capture_are_rejected():
    client = TestClient(create_app(make_settings(), Database(":memory:"), classifier=MutableClassifier()))
    assert upload_audio(client, -1, datetime.now(timezone.utc)).status_code == 422
    assert upload_audio(client, 0, datetime.now(timezone.utc) + timedelta(hours=1)).status_code == 422


def test_phone_registration_does_not_mark_device_online():
    db = Database(":memory:")
    client = TestClient(create_app(make_settings(), db, classifier=None))
    assert client.post("/api/v1/app/installations", headers=APP_AUTH, json={
        "installation_id": "test-installation", "device_id": "rt-spark-01", "fcm_token": "x" * 32,
    }).status_code == 201
    latest = client.get(DEVICE_URL + "/latest", headers=APP_AUTH).json()
    assert latest["device"]["network_status"] == "offline"
    assert latest["alert_settings"]["temperature_max_c"] == 30
    assert latest["alert_settings"]["cry_clear_windows"] == 3


def test_concurrent_positive_windows_create_one_episode(tmp_path):
    from concurrent.futures import ThreadPoolExecutor
    path = str(tmp_path / "concurrent.sqlite3")
    Database(path)
    start = datetime.now(timezone.utc) - timedelta(seconds=30)
    result = MutableClassifier().predict_pcm16()
    def record(index):
        return Database(path).record_audio_result(
            "rt-spark-01", (start + timedelta(seconds=2 * index)).isoformat(timespec="microseconds"), index, result
        )[1]
    with ThreadPoolExecutor(max_workers=4) as pool:
        created = list(pool.map(record, range(8)))
    assert sum(created) == 1
    assert len(Database(path).events("rt-spark-01")) == 1
