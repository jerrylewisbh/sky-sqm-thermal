# Sky Condition Classifier

This page documents how the `sky_condition` text sensor and the related `/json` fields (`sky_delta_median`, `sky_cloud_fraction`, `sky_abs_cloud_fraction`) are computed: the underlying physics, the math, the thresholds, the BMP color mapping, and how to calibrate them for your site.

## TL;DR

Three classifiers run in parallel on every update. The most pessimistic verdict wins.

| Method | Idea | Strength | Weakness |
|---|---|---|---|
| Mean Δ | `mean(frame) − ambient` | Best for uniform skies | Cellular cloud washes out under a mean |
| Δ-fraction | % of pixels above `ambient − 10°C` | Catches broken/cellular cloud | Depends on a trustworthy ambient |
| Absolute fraction | % of pixels above `−5°C` | Works even when ambient is wrong | Misses cold cloud on cold nights |

Hybrid scoring (pessimistic of all three) covers the cases where any one method fails.

## Why thermal IR sees clouds

The MLX90640 measures incoming infrared radiation in the 8–14 µm band — the **atmospheric IR window** where the atmosphere itself is largely transparent. What the sensor reads as "temperature" is the brightness temperature of whatever is in the FOV.

| Target | What it actually is | Apparent temperature |
|---|---|---|
| Clear sky | A view through the atmosphere into space (effectively −270 °C) | −15 to −40 °C, depending on humidity & elevation |
| Thin cirrus | High, cold ice crystals | −30 to −50 °C (often colder than clear sky around it) |
| Mid-level cloud | Water droplets at altitude | −10 to +5 °C |
| Low/thick cloud | Water droplets near surface | Close to ambient air |
| Fog / heavy overcast | Water at surface | Equal to or warmer than ambient |
| Ground / building | Solid object near surface | ≈ ambient |

The key insight: **clear sky is dramatically colder than the air below it** (often 25–35 °C colder). Clouds break that contrast. So the gap between "what the IR camera sees" and "what the air thermometer reads" *is* the cloud signal — when ambient is correct.

## The three classifiers

### 1. Mean Δ

```cpp
delta = mean(frame) − ambient
```

Classified into 6 buckets:

| Δ range (°C) | Label |
|---|---|
| Δ < −25 | `very_clear` |
| −25 ≤ Δ < −15 | `clear` |
| −15 ≤ Δ < −8 | `mostly_clear` |
| −8 ≤ Δ < −3 | `partly_cloudy` |
| −3 ≤ Δ < +3 | `mostly_cloudy` |
| Δ ≥ +3 | `overcast` |

Strong when the FOV is filled uniformly (overcast or fully clear). Fails on **cellular/broken cloud** like altocumulus: warm cloud cells (+10 °C) average together with cold gaps (−10 °C) and the mean ends up looking "clear".

### 2. Δ-fraction (cloud fraction by ambient)

```cpp
cloud_fraction = (count of pixels where (pixel - ambient) > -10°C) / 768
```

Per-pixel cloud test using the same Δ-from-ambient logic, then count what fraction qualify. Classified by METAR-style coverage thresholds:

| Cloud fraction | Label | METAR analog |
|---|---|---|
| < 5 % | `very_clear` | SKC |
| 5–25 % | `clear` | FEW |
| 25–50 % | `mostly_clear` | SCT |
| 50–75 % | `partly_cloudy` | SCT–BKN |
| 75–95 % | `mostly_cloudy` | BKN |
| > 95 % | `overcast` | OVC |

Fixes the cellular-cloud failure mode of method 1 — every cloud cell counts as a cloud pixel regardless of how the gaps balance them out.

### 3. Absolute fraction (ambient-independent)

```cpp
abs_cloud_fraction = (count of pixels where pixel > -5°C) / 768
```

Same coverage thresholds as method 2, but uses an **absolute** temperature cutoff instead of one that floats with ambient. Justified by physics:

- Tropospheric water cloud is essentially never colder than ~−15 °C.
- Clear sky at zenith is essentially never warmer than ~−15 °C.
- −5 °C gives a margin of confidence: anything above this is unambiguously cloud.

This method's superpower is that **it doesn't care if the BME280 is lying about ambient**. On a hot day with a sun-heated enclosure reading 35 °C ambient, methods 1 and 2 collapse to "very_clear" no matter what the sky looks like — but method 3 keeps working.

The trade-off: high-altitude cirrus can be colder than −5 °C, so method 3 misses it. But methods 1 and 2 catch cirrus when ambient is trustworthy. The hybrid covers both ends.

### Hybrid scoring

```cpp
combined = pessimistic_label(delta_label, fraction_label, abs_fraction_label)
```

`pessimistic_label` ranks labels by cloudiness (`very_clear=0` … `overcast=5`) and returns the one with the highest rank. So if any one method says "overcast", the verdict is "overcast" — no method can talk over a more pessimistic one.

This is intentionally **conservative** for astrophotography use: it errs on the cloudy side, which is the safer error direction (better to skip a marginal session than image through cloud).

A debug log line is emitted every cycle so you can see all three votes:

```
[D][sky_thermal]: sky: Δ=-19.7→clear frac=0.85→mostly_cloudy abs_frac=1.00→overcast -> overcast
```

## BMP color mapping

The `/thermal.bmp` endpoint colors each pixel by `c = pixel_temp − cutoff`, where `cutoff = min(−5°C, ambient − 10°C)` — the same combined cutoff that defines "is this pixel cloud?" in the classifier.

Cool colors mean the pixel is *below* the cloud cutoff (clear / haze); warm colors mean it's *above* (cloud). The visual is therefore aligned with the verdict in all regimes:

| Color | `c` (°C) | Per-pixel state |
|---|---|---|
| Deep blue | < −20 | very clear (well below cutoff) |
| Cyan | −20 to −10 | clear |
| Pale green | −10 to −5 | hazy / thin |
| Green | −5 to 0 | haze (just below cutoff) |
| Yellow | 0 to +5 | light cloud (just above cutoff) |
| Orange | +5 to +10 | cloud |
| Red | > +10 | thick / low cloud |

Why "relative to cutoff" instead of raw temperature: the cutoff floats with ambient, so the "warm pixel = cloud" rule continues to make sense across temperature regimes:

| Scenario | Ambient | Sky temp | Cutoff | `c` | Color |
|---|---|---|---|---|---|
| Hot day clear | +25 °C | −15 °C | −5 °C | −10 | cyan |
| Hot day cloud | +25 °C | +5 °C | −5 °C | +10 | red |
| Cool clear night | +5 °C | −30 °C | −5 °C | −25 | deep blue |
| Cold clear night | −20 °C | −45 °C | −30 °C | −15 | cyan |
| Cold cloud night | −20 °C | −18 °C | −30 °C | +12 | red |

Without the floating cutoff, cold winter cloud (which is still well below 0 °C in absolute terms) would falsely show as cool colors.

## Limitations

1. **Humidity raises Δ across all methods using ambient** — water vapor partially fills the IR window, making clear sky look warmer. Desert sites typically see Δ ≈ −40 °C on a clear night; humid coastal sites might only reach Δ ≈ −15 °C even with no clouds. **Recalibrate thresholds for your site.**
2. **Pointing matters** — sky temperature warms toward the horizon (longer atmospheric path). At zenith you get the coldest reading; at 60° from zenith the same clear sky might read 10 °C warmer. Mount the camera pointing as close to zenith as practical.
3. **Sun in or near FOV** will warm pixels through scattering. Not a problem at night; can mislead in daytime. If the sun is *just outside* the FOV, scattered IR can still walk the nearby edge pixels.
4. **Thresholds are heuristics** — the cutoffs (−25/−15/−8/−3/+3 for delta, −10°C and −5°C for cloud-pixel detection) are conventional but not derived from first principles. Tune them to your dataset (see below).
5. **Hot enclosure / sun-heated BME280** breaks methods 1 and 2 but method 3 still works. If your ambient runs systematically hot (e.g. all your "ambient" readings are 5–10 °C above an external reference), shield the BME280 better — but in the meantime the absolute method keeps the classifier honest.

## Calibrating for your site

The default thresholds are a reasonable starting point but may not be optimal for your altitude/climate. To tune:

1. **Log all four signals continuously** — `sky_delta_median`, `sky_cloud_fraction`, `sky_abs_cloud_fraction`, and the categorical `sky_condition` are all published to MQTT/HA every 5 s. Pipe them into a database (InfluxDB, TimescaleDB, etc.).
2. **Collect ground truth** — use one of:
   - Visual classification from your all-sky camera (review images, label hourly)
   - METAR cloud reports from your nearest airport (cloud cover in oktas, plus base height)
   - Manual eyeballing on representative nights
3. **Plot each metric's histogram colored by ground truth.** You'll see where the distributions separate cleanly. Pick thresholds at the separation points.
4. **Tune the constants in `sky_thermal.h`:**
   - `CLOUD_PIXEL_DELTA_CUTOFF` (default −10°C) — the per-pixel "cloud" threshold for method 2
   - `CLOUD_PIXEL_ABS_CUTOFF` (default −5°C) — the per-pixel "cloud" threshold for method 3
   - The buckets in `classify_delta()` and `classify_fraction()`

After a few weeks of mixed weather you'll have enough data to derive site-specific cutoffs.

## Hardware FOV considerations

The MLX90640 comes in two variants. **For sky monitoring, the wide-angle (110°) variant is strongly preferred.**

| Variant | FOV | Sky coverage from zenith | Per-pixel angular size | Use case |
|---|---|---|---|---|
| `MLX90640BAA` | 110° × 75° | ~40 % of hemisphere | 3.4° / px | Sky / cloud monitoring (recommended) |
| `MLX90640BAB` | 55° × 35° | ~9 % of hemisphere | 1.7° / px | Targeted high-res patches |

Clouds are large thermal targets with ~25 °C contrast, so 3.4°/pixel is plenty of resolution for cloud detection. The 4× larger sky coverage matters more — it lets you see clouds drifting toward your imaging zone before they arrive.

## References
TODO: add links
The "Δ-from-ambient" cloud-detection technique is widely used in observatory operations but mostly documented in commercial product manuals and conference proceedings rather than peer-reviewed journals. The underlying physics is rigorously established.

**Atmospheric IR / sky temperature physics:**
- Berdahl, P. & Fromberg, R. (1982). *The thermal radiance of clear skies.* Solar Energy 29 (4): 299–314.
- Berdahl, P. & Martin, M. (1984). *Emissivity of clear skies.* Solar Energy 32 (5): 663–664.
- Idso, S. B. (1981). *A set of equations for full spectrum and 8–14 µm and 10.5–12.5 µm thermal radiation from cloudless skies.* Water Resources Research 17 (2): 295–304.

**Commercial/observatory implementations using this approach:**
- **Boltwood Cloud Sensor** (Diffraction Limited) — single-pixel thermopile (MLX90614), de facto reference for ΔT-based cloud classification thresholds.
- **AAG CloudWatcher** (Lunatico) — same approach, similar thresholds; manual is publicly downloadable.

**Astronomy / observatory papers:**
- Pierre Auger Observatory cloud-monitoring publications (search: "Pierre Auger cloud monitor IR")
- VERITAS / CTA atmospheric monitoring (FRAM cloud monitor)
- Maghrabi, A. (various, *Atmospheric Research*) — thermal sky-temperature measurements from desert sites.

The cloud-fraction approach (treating multi-pixel arrays as coverage estimators) is closer to METAR conventions and standard practice in cloud-cover satellite retrievals (search: `okta cloud cover thermal IR retrieval`). The hybrid pessimistic combination of methods is an engineering choice, not from any particular paper.

## Implementation locations

| Concern | File | Notes |
|---|---|---|
| Mean Δ + cloud fractions | `external_components_local/sky_thermal/sky_thermal.h` (`update()`) | All three computed inline |
| Per-pixel cutoff constants | `sky_thermal.h` (`CLOUD_PIXEL_DELTA_CUTOFF`, `CLOUD_PIXEL_ABS_CUTOFF`) | Tune here |
| Bucket thresholds | `sky_thermal.h` (`classify_delta`, `classify_fraction`) | Tune here |
| Hybrid combiner | `sky_thermal.h` (`pessimistic_label`, `cloudiness_rank`) | Pessimistic-of-all-three |
| BMP coloring | `sky_thermal.h` (`abs_temp_color_bgr` + `ThermalHandler::handleRequest`) | Same combined cutoff as the classifier |
| Text sensor wiring | `external_components_local/sky_thermal/text_sensor.py` | Schema |
| YAML | `sensortest.yaml` | `text_sensor.sky_condition` block |
