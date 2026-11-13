# Baby-Cry Classifier Evaluation

## Scope

This evaluation measures the pretrained YAMNet adapter used by the backend. It is a classroom prototype evaluation, not a medical or safety certification. The firmware does not run YAMNet on the STM32F407; the backend performs inference because the TensorFlow model does not fit the board's flash/RAM budget.

## Data

- **Source:** ESC-50 (Karol J. Piczak), https://github.com/karolpiczak/ESC-50.
- **Licence:** CC BY-NC 3.0. The audio is kept outside Git under `.dev-tools/downloads/esc50-eval/`; the repository records the metadata, split, and results.
- **Cry clips:** all 40 `crying_baby` clips.
- **Non-cry clips:** 40 clips selected from ten categories: `clapping`, `laughing`, `coughing`, `sneezing`, `dog`, `cat`, `rain`, `sea_waves`, `clock_alarm`, `door_wood_knock` (four per category).
- **Split:** source-file-disjoint 20 validation + 20 test clips for cry; source-file-disjoint 20 validation + 20 test clips for non-cry. No source recording appears in both halves. Cry test source groups: `187207`, `211527`, `22694`, `60997`, `107351`, `151079`, `50665`, `50666`, `66637`, `152007`.
- **Window:** each 5 s clip is resampled to 16 kHz mono; the 2 s window with the highest RMS is selected without using the label. This is a signal-based preprocessing choice and can favour loud cry segments; document it when comparing against other work.
- **Format:** 16 kHz mono signed PCM16, exactly 64,000 bytes per window.

## Model and decision rule

- YAMNet via TensorFlow Hub (`https://tfhub.dev/google/yamnet/1`), class `baby cry, infant cry`.
- A window is positive when at least two adjacent YAMNet frames have a score at or above the threshold.
- Threshold selection used only the 20 + 20 validation clips, over a grid from 0.05 to 0.95 in 0.01 steps, maximising F1 then recall.
- The held-out 20 + 20 test clips were evaluated once at the selected threshold and once at the backend default of 0.35.

## Results

| Split | Threshold | TP | FP | TN | FN | Precision | Recall | F1 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Validation | 0.05 (selected) | 18 | 0 | 20 | 2 | 1.000 | 0.900 | 0.947 |
| Test | 0.05 | 17 | 0 | 20 | 3 | 1.000 | 0.850 | 0.919 |
| Test | 0.35 (default) | 16 | 0 | 20 | 4 | 1.000 | 0.800 | 0.889 |

Confusion matrix at 0.05:

| | Predicted cry | Predicted non-cry |
|---|---:|---:|
| Actual cry | 17 (TP) | 3 (FN) |
| Actual non-cry | 0 (FP) | 20 (TN) |

At 0.35, the matrix is 16 TP / 4 FN / 0 FP / 20 TN.

### Failure examples

At 0.05:

- `1-211527-B-20.wav` — cry, missed.
- `2-107351-A-20.wav` — cry, missed.
- `2-66637-A-20.wav` — cry, missed.

At 0.35, `1-187207-A-20.wav` is also missed. No non-cry clip in this test set produced a false positive.

The low selected threshold reflects the small validation set and the absence of speech/music/silence confusers in it. The backend default remains 0.35 as a conservative margin; this choice must be re-evaluated once broader confusers are tested.

## Deployment rationale

- YAMNet is a large TensorFlow model; keeping it on the backend avoids an infeasible MCU deployment and keeps the firmware responsible only for bounded 2 s capture and upload.
- Only a triggered, bounded window may be uploaded; the backend processes it in memory and does not persist raw audio.
- Device upload must use authenticated TLS; the local serial bridge used during bring-up is not the final transport.

## Limitations and next checks

- ESC-50 is small and source-dependent; these results are not a claim of real-world accuracy.
- Speech, music, and silence were not included in this 20/20 run. Add them as a separate confuser set before freezing the threshold.
- The max-RMS window selection is label-free but may overstate performance versus a fixed centrefold window.
- No confidence calibration or cross-dataset validation has been performed.
- The live microphone path is verified, but live cry recordings and backend upload latency are not yet part of the 20/20 evidence.
