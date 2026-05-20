# SQM Calibration Method

This document describes the procedure used to calibrate the TSL2591 ambient-light sensor in the Sky Thermal Weather Station against a reference sky-brightness scale (mag/arcsec², "mpsas") using telescope photometry. The calibration produces the constant `K` in the firmware's per-update formula:

```
mpsas = K − 2.5 · log₁₀(lux)
```

The procedure cross-validates the result against three independent references — Tycho-2 catalog photometry in two filter bands, an all-sky camera, and the VIIRS-derived light-pollution map — and is generic enough to apply to any DIY broadband visible photometer.

## 1. Background

A Sky Quality Meter (SQM) reports the brightness of the sky in **magnitudes per square arcsecond** (mag/arcsec², or mpsas) at zenith. The astronomical magnitude scale is logarithmic: lower numbers mean brighter sky. A pristine dark site reads ≈22 mpsas; an inner-city site ≈16 mpsas.

A photometric sensor that produces an analog or digital reading proportional to incoming flux (e.g., a TSL2591 outputting illuminance in lx) can be calibrated to the SQM scale via a single multiplicative+offset transform on the log of its output. With the standard form `mpsas = K − 2.5·log₁₀(lux)`, only the constant `K` needs to be determined.

`K` depends on:
- The sensor's spectral response and intrinsic responsivity
- The optical path (window, diffuser, geometry)
- The spectrum of the local sky illumination (which sets how the sensor's broadband response integrates into a single number)

It must therefore be determined per-installation, not transferred from a reference unit.

## 2. Hardware

| Component | Specification |
|---|---|
| Photometer | AMS TSL2591 broadband visible/NIR (Adafruit breakout) |
| Window | Plain borosilicate disc, 0.8″ diameter, no coating, no diffuser |
| Reference telescope | William Optics GT81, 478 mm focal length |
| Reference camera | QHY miniCam8M, 2.9 µm pixels, 3856×2180, gain 50, offset 11, 16-bit ADU |
| Filter wheel | QHYCCD LRGB+narrowband set; slots used: B (390–505 nm bandpass, approximately Johnson B but ~15 nm wider on the red side) and L (UV/IR-cut Luminance, ~390–700 nm) |
| All-sky reference | Independent calibrated all-sky camera reporting broadband mpsas |
| Light-pollution model | lightpollutionmap.info (VIIRS-derived broadband V-equivalent) |

The telescope+camera give an instrumental pixel scale of:

```
pixel_scale = 206.265 × 2.9 µm / 478 mm = 1.250 arcsec/pixel
```

Confirmed by plate-solving (astrometry.net) to ±0.001 arcsec/pixel.

## 3. Procedure

### 3.1 Image acquisition

Capture multiple exposures of the **zenith** through each filter on a clear, moonless night:

- Filter B, 30 s
- Filter L, 3 s, 5 s, and 10 s (multi-exposure required for bias estimation, see §3.3)

Save raw NINA FITS files with no calibration or normalization applied. Plate-solve each frame using astrometry.net or equivalent so that each image has a valid WCS in its FITS header.

At the **same moment** as one of the captures, log the TSL2591 lux reading from the photometer (e.g., `curl http://device/json`).

### 3.2 Reduction overview

For each image, the reduction pipeline computes:

1. The bias pedestal `B` (camera electronic offset, ADU)
2. The per-pixel sky background above bias (ADU)
3. Detected star positions (DAOStarFinder, photutils)
4. Catalog cross-match (Tycho-2 via Vizier, using the embedded WCS)
5. Aperture photometry on each matched star, with local sky subtraction in an annulus
6. The photometric zero point `ZP` (per-second instrumental flux → catalog magnitude)
7. The sky brightness in mag/arcsec² for that filter band

### 3.3 Bias estimation

Bias must be subtracted from the global sky measurement (but not from per-star aperture photometry, since the local-sky annulus already cancels bias).

A single dark/bias frame would be the gold standard. When unavailable, **bias can be derived from multiple exposures of identical sky brightness**. The relationship

```
sky_signal_per_second = (median_ADU − BIAS) / exposure_time
```

is the same for any exposure of the same sky. Solving across two exposures (`t₁`, `t₂`) with medians (`M₁`, `M₂`):

```
BIAS = (t₂·M₁ − t₁·M₂) / (t₂ − t₁)
```

For our setup, three back-to-back L-filter exposures (3, 5, 10 s) gave consistent `BIAS = 688 ADU`. This is filter-independent (it's a property of the camera's analog frontend at given gain/offset), so the same value applies to the B-band reduction.

**Pitfall:** Estimating bias from a low-percentile of a single image (e.g., the 0.5th percentile) is biased high because even the dimmest pixels contain some sky signal. Our initial use of the 0.5th percentile gave 1104 ADU instead of the true 688 ADU, leading to a ~1.2 mag systematic error in mpsas. The multi-exposure linearity method is essential when no bias frames are available.

### 3.4 Sky background

After global bias subtraction, the per-pixel sky background is the sigma-clipped median of the image (clipping rejects star pixels). Convert to per-arcsec² per second:

```
sky_rate = (median_ADU − BIAS) / pixel_area_arcsec² / exposure_time
```

where `pixel_area_arcsec² = pixel_scale_x · pixel_scale_y`.

### 3.5 Star photometry

DAOStarFinder identifies star candidates in the sigma-clipped, bias-subtracted, sky-subtracted image. Each detected position is converted to celestial coordinates via the WCS and matched to Tycho-2 entries within 5 arcsec.

For each matched, **unsaturated** (raw peak < 55,000 ADU), **isolated** star with magnitude in the linear range (typically 10 ≤ V ≤ 11.5), aperture photometry gives:

```
F_star = sum_in_aperture − local_sky_median × aperture_area
```

Local sky is measured in an annulus from 24 to 36 pixels around the star. Aperture radius is 12 pixels (3× FWHM for a typical 4-pixel FWHM star).

The instrumental magnitude per second is:

```
m_inst = −2.5 · log₁₀(F_star / exposure_time)
```

The zero point is the offset from instrumental to catalog magnitude:

```
ZP = m_catalog − m_inst = m_catalog + 2.5 · log₁₀(F_star / exposure_time)
```

Tycho-2 publishes BT and VT magnitudes in the Tycho photometric system. Standard transformations to the Johnson system used here:

```
B_johnson = BT − 0.27 · (BT − VT)
V_johnson = VT − 0.090 · (BT − VT)
```

The zero point reported is the **median** across all qualifying stars (typically 3–13 stars per frame). Standard deviation of individual ZPs (typically 0.05–0.20 mag) bounds the photometric precision.

### 3.6 Sky brightness in magnitudes

```
mpsas = ZP − 2.5 · log₁₀(sky_rate)
```

### 3.7 Calibrating K

With one or more `(filter, mpsas, lux)` tuples in hand:

```
K = mpsas + 2.5 · log₁₀(lux)
```

If multiple filters were measured, the choice of `mpsas` to anchor to depends on the application:

- For end-user displays that should agree with conventional broadband references (all-sky cameras, light-pollution maps), use V or broadband mpsas.
- For scientifically pure photometry within a single filter, anchor to that filter.

## 4. Cross-validation

Independent references provide consistency checks. For our installation, all four independent measurements at lux=0.041 produced:

| Method | mpsas | Notes |
|---|---|---|
| Light-pollution map (VIIRS V-equivalent) | 18.72 | Site model, ±0.5 mag typical accuracy |
| All-sky cam (broadband) | 18.779 | Independent calibrated photometry |
| **B-band telescope (this work)** | **18.65 ± 0.10** | 4 stars, ZP scatter 0.10 mag |
| **L-band telescope (this work)** | **17.99 ± 0.21** | 13 stars, ZP scatter 0.21 mag |

Convergence of B and V (within 0.13 mag) confirms the measurement chain is consistent. The 0.7 mag difference between L and V is a real spectral effect: the broader Luminance passband captures more red/NIR light pollution than V does, and is interpretable as a measurement of B−V color of the sky (≈+0.6 to +0.8 for our suburban site).

## 5. Final result

For our site:

```
K = 15.31  (anchored to V-band ≈ all-sky agreement at lux=0.041)
```

This gives the firmware a calibration that produces displayed mpsas matching independent broadband references. The TSL2591's spectral response (visible + NIR) is closer to the L filter than V, so the displayed value implicitly assumes the local-light-pollution spectrum is similar at calibration time and use time. Substantial changes in light-pollution composition (e.g., LED retrofits in the surrounding area) would warrant recalibration.

## 6. Limitations

- A single calibration session captures a single state of the atmosphere. Repeating across multiple nights (clear, hazy, partially cloudy) and pooling results would tighten K.
- Bias estimated from multi-exposure linearity is accurate provided sky brightness is constant across the exposure series. Cloud passing through the field invalidates this.
- Tycho-2 photometry is good to ~0.05 mag for V<11; below V=11.5 catalog uncertainties dominate the ZP error budget.
- The transformations BT,VT → Johnson B,V are color-dependent approximations valid for typical stellar spectra; very red or blue stars will have systematic errors.
- The QHY B filter is wider than Johnson B (extends ~15 nm further into green). Treating its photometry as if it were strict Johnson B introduces a color-dependent systematic estimated at ≤0.15 mag for typical stellar spectra and somewhat larger for the sky background (which is heavily red-shifted by light pollution). A fully rigorous treatment would convolve the published QHY filter transmission with the Tycho/Johnson reference response and derive a per-color color term; for a SQM-purpose calibration this is unnecessary precision.
- Differential extinction across the FOV (~1.3°) is negligible at zenith but would matter for low-altitude pointings.

## 7. Data and code

The Python pipeline implementing this calibration is in `docs/sqm_calibrate.py`. Required packages: `astropy`, `photutils`, `astroquery`, `numpy`. All calibration data files (raw FITS, plate solutions, lux values) are in this repository under their captured timestamps for reproducibility.

## References

- Tycho-2 catalog: ESA SP-1200; Hog et al. 2000, A&A 355, L27. Vizier I/259/tyc2.
- DAOPhot star finding: Stetson 1987, PASP 99, 191.
- photutils: Bradley et al. 2024, Zenodo. https://photutils.readthedocs.io
- astrometry.net plate solving: Lang et al. 2010, AJ 139, 1782.
- Light-pollution map (VIIRS): Falchi et al. 2016, Sci. Adv. 2:e1600377.
- TSL2591 datasheet: AMS-OSRAM AG TSL25911 product brief.
