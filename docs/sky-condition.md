# Sky Condition Classifier

This page documents how the `sky_condition` text sensor and the `/json` `sky_delta_median` field are computed: the underlying physics, the math, the thresholds, and how to calibrate them for your site.

## TL;DR

```
delta = mean(thermal_frame) − ambient_air_temperature
```

That single value, classified into 6 buckets, gives you a Boltwood-style sky-state label. The thresholds are conventional; the physics is well established.

## Why thermal IR sees clouds

The MLX90640 measures incoming infrared radiation in the 8–14 µm band — the **atmospheric IR window** where the atmosphere itself is largely transparent. What the sensor reads as "temperature" is the brightness temperature of whatever is in the FOV.

| Target | What it actually is | Apparent temperature |
|---|---|---|
| Clear sky | A view through the atmosphere into space (effectively −270 °C) | −15 to −40 °C, depending on humidity & elevation |
| Thin cirrus | High, cold ice crystals | −10 to −20 °C |
| Mid-level cloud | Water droplets at altitude | −10 to +5 °C |
| Low/thick cloud | Water droplets near surface | Close to ambient air |
| Fog / heavy overcast | Water at surface | Equal to or warmer than ambient |
| Ground / building | Solid object near surface | ≈ ambient |

The key insight: **clear sky is dramatically colder than the air below it** (often 25–35 °C colder). Clouds break that contrast. So the gap between "what the IR camera sees" and "what the air thermometer reads" *is* the cloud signal.

That's why we use Δ rather than raw sky temperature. A clear winter night might read T_sky = −25 °C; a clear summer day might read T_sky = −10 °C. Their raw values differ by 15 °C, but their Δ values are similar (~−25 to −35 °C). Δ cancels out the seasonal/diurnal baseline.

## The formula

In `sky_thermal.h`, inside the `update()` cycle:

```cpp
last_sky_delta_median_ = NAN;
if (mlx_found_ && mlx_status == 0 && !std::isnan(b_temp) && !std::isnan(sum)) {
  last_sky_delta_median_ = (sum / 768.0f) - b_temp;
}
last_sky_condition_ = classify_delta(last_sky_delta_median_);
```

Where:

- `sum` = sum of all 768 MLX90640 pixels in °C
- `sum / 768` = mean sky temperature across the FOV
- `b_temp` = BME280 ambient air temperature in °C
- Result = the Δ in °C, written to `last_sky_delta_median_`

Field name carries `_median_` for HA entity-history continuity, but the value is the **mean** — see [Mean vs median](#mean-vs-median) below.

## Classification

```cpp
static const char *classify_delta(float delta) {
  if (std::isnan(delta)) return "unknown";
  if (delta < -25) return "very_clear";
  if (delta < -15) return "clear";
  if (delta <  -8) return "hazy";
  if (delta <  -3) return "light_cloud";
  if (delta <   3) return "cloudy";
  return "heavy_cloud";
}
```

| Δ range (°C) | Label | Astronomy interpretation | Color in `/thermal.bmp` |
|---|---|---|---|
| Δ < −25 | `very_clear` | Driest, coldest sky — best imaging conditions | Deep blue |
| −25 ≤ Δ < −15 | `clear` | Normal clear sky, fine for imaging | Cyan |
| −15 ≤ Δ < −8 | `hazy` | Thin cirrus / high humidity — sky still "open" but not pristine | Green |
| −8 ≤ Δ < −3 | `light_cloud` | Patchy clouds, thin haze | Yellow |
| −3 ≤ Δ < +3 | `cloudy` | Solid cloud cover, sky temperature ≈ ambient | Orange |
| Δ ≥ +3 | `heavy_cloud` | Thick low cloud / fog, sometimes warmer than air | Red |
| Δ = NaN | `unknown` | Either MLX or BME280 isn't reporting | Gray (BMP) |

The `/thermal.bmp` endpoint applies the same six buckets *per pixel*, so a single bright cloud drifting through clear sky shows up as a yellow/orange blob on a blue background — visible without any post-processing.

## Mean vs median

The first implementation used `std::nth_element` for a true median, which is more robust to single-pixel outliers (a bird, plane, satellite passing through). It crashed the ESP32: a 768-float stack array (~3 kB) plus the partition recursion was enough to overflow the 8 kB main task stack alongside the live JSON/env stringstreams.

We switched to mean. For cloud detection the difference is negligible:

- Clouds are large patches → mean and median agree to within fractions of a degree.
- A single hot pixel pushes the mean by ~30 °C / 768 ≈ **0.04 °C**, which is a rounding error against the 5–10 °C threshold gaps.
- Mean costs nothing — `sum` is already computed for `thermal_avg`.

## Limitations

1. **Humidity raises Δ** — water vapor partially fills the IR window, making clear sky look warmer. Desert sites typically see Δ ≈ −40 °C on a clear night; humid coastal sites might only reach Δ ≈ −15 °C even with no clouds. **Recalibrate thresholds for your site.**
2. **Pointing matters** — sky temperature warms toward the horizon (longer atmospheric path). At zenith you get the coldest reading; at 60° from zenith the same clear sky might read 10 °C warmer. Mount the camera pointing as close to zenith as practical.
3. **Sun in FOV** will saturate pixels and skew the mean. Not a problem at night; can mislead in daytime if FOV catches the sun. The 110° MLX90640 has a wider footprint, so the sun spends less time in any one pixel — it's still worth being aware of.
4. **Thresholds are heuristics** — the −25/−15/−8/−3/+3 cutoffs are conventional but not derived from first principles. Tune them to your dataset (see below).
5. **Thermal mass / shielding** — if the BME280 is mounted somewhere with different airflow than the thermal camera (e.g. inside a vented enclosure), the ambient reading can lag or differ from the air the sky is "seen against." Mount them as close together as practical with similar exposure to ambient air.

## Calibrating for your site

The default thresholds are a reasonable starting point but may not be optimal for your altitude/climate. To tune:

1. **Log Δ continuously** — your `sky_delta_median` is published to MQTT/HA every 5 s. Pipe it into a database (InfluxDB, TimescaleDB, etc.).
2. **Collect ground truth** — use one of:
   - Visual classification from your all-sky camera (review images, label hourly)
   - METAR cloud reports from your nearest airport (cloud cover in oktas, plus base height)
   - Manual eyeballing on representative nights
3. **Plot Δ histogram colored by ground truth.** You'll see distinct distributions: clear, partly cloudy, overcast each cluster around different Δ values.
4. **Pick thresholds where the distributions separate cleanly** for your site.

After a few weeks of mixed weather you'll have enough data to derive site-specific cutoffs. Edit `classify_delta` in `external_components_local/sky_thermal/sky_thermal.h` and reflash.

## Hardware FOV considerations

The MLX90640 comes in two variants. **For sky monitoring, the wide-angle (110°) variant is strongly preferred.** See the table:

| Variant | FOV | Sky coverage from zenith | Per-pixel angular size | Use case |
|---|---|---|---|---|
| `MLX90640BAA` | 110° × 75° | ~40 % of hemisphere | 3.4° / px | Sky / cloud monitoring (recommended) |
| `MLX90640BAB` | 55° × 35° | ~9 % of hemisphere | 1.7° / px | Targeted high-res patches |

Clouds are large thermal targets with ~25 °C contrast, so 3.4°/pixel is plenty of resolution for cloud detection. The 4× larger sky coverage matters more — it lets you see clouds drifting toward your imaging zone before they arrive.

## References

The "Δ-from-ambient" cloud-detection technique is widely used in observatory operations but mostly documented in commercial product manuals and conference proceedings rather than peer-reviewed journals. The underlying physics is rigorously established.

**Atmospheric IR / sky temperature physics:**
- Berdahl, P. & Fromberg, R. (1982). *The thermal radiance of clear skies.* Solar Energy 29 (4): 299–314. — Empirical model relating sky brightness temperature to dewpoint and elevation.
- Berdahl, P. & Martin, M. (1984). *Emissivity of clear skies.* Solar Energy 32 (5): 663–664. — Refines the model with hourly variations.
- Idso, S. B. (1981). *A set of equations for full spectrum and 8–14 µm and 10.5–12.5 µm thermal radiation from cloudless skies.* Water Resources Research 17 (2): 295–304.

**Commercial/observatory implementations using this approach:**
- **Boltwood Cloud Sensor** (Diffraction Limited) — single-pixel thermopile (MLX90614), de facto reference for ΔT-based cloud classification thresholds.
- **AAG CloudWatcher** (Lunatico) — same approach, similar thresholds; manual is publicly downloadable.

**Astronomy / observatory papers:**
- Pierre Auger Observatory cloud-monitoring publications (search: "Pierre Auger cloud monitor IR")
- VERITAS / CTA atmospheric monitoring (FRAM cloud monitor)
- Maghrabi, A. (various, *Atmospheric Research*) — thermal sky-temperature measurements from desert sites.

**Search terms for further reading:**
- `"sky temperature" cloud detection IR thermopile`
- `Boltwood cloud sensor astronomy`
- `MLX90614 cloud sensor observatory`
- `infrared cloud monitor astronomical site`

## Implementation locations

| Concern | File | Notes |
|---|---|---|
| Δ computation | `external_components_local/sky_thermal/sky_thermal.h` (`update()`) | One-line mean-minus-ambient |
| Classifier | `external_components_local/sky_thermal/sky_thermal.h` (`classify_delta`) | Edit thresholds here |
| BMP coloring | `external_components_local/sky_thermal/sky_thermal.h` (`delta_color_bgr` + `ThermalHandler::handleRequest`) | Same buckets per pixel |
| Text sensor wiring | `external_components_local/sky_thermal/text_sensor.py` | Schema |
| YAML | `sensortest.yaml` | `text_sensor.sky_condition` block |
