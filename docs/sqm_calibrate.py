#!/usr/bin/env python3
"""
sqm_calibrate.py — Calibrate a broadband visible photometer (e.g. TSL2591) against
an SQM-scale (mag/arcsec^2) reference using telescope photometry.

Method described in docs/sqm-calibration.md. Briefly:

  1. Take one or more zenith images through a known photometric filter (B or V)
     with a calibrated camera+telescope, plus the simultaneous photometer reading.
  2. Plate-solve the images (astrometry.net, ASTAP, NINA, etc.) so each FITS
     has a valid WCS.
  3. Run this script with paths to the FITS files and the matching photometer
     value(s). It computes the photometric zero point from Tycho-2 reference
     stars, the sky background per arcsec^2 per second, the resulting sky
     brightness (mpsas), and the calibration constant K such that
         mpsas = K - 2.5 * log10(reading)

Usage example:

    python3 sqm_calibrate.py \\
        --image my_b_filter_zenith.fits --filter B --reading 0.041 \\
        [--image my_l_filter_zenith.fits --filter V --reading 0.041] \\
        --bias 688

Author: Sky Thermal Weather Station project
License: MIT
"""

from __future__ import annotations
import argparse
import sys
import warnings
from dataclasses import dataclass

import numpy as np
from astropy.io import fits
from astropy.wcs import WCS
from astropy.stats import sigma_clipped_stats
from astropy.coordinates import SkyCoord
import astropy.units as u
from photutils.detection import DAOStarFinder
from photutils.aperture import (
    CircularAperture, CircularAnnulus, ApertureStats, aperture_photometry
)
from astroquery.vizier import Vizier

warnings.filterwarnings("ignore")  # silences astropy deprecation chatter


# --- Tycho-2 → Johnson photometry transformations ---------------------------
# Hog et al. 2000; ESA SP-1200. These are color-dependent linear approximations
# valid for typical stellar spectral types.
def tycho_to_johnson(BT: float, VT: float, band: str) -> float:
    """Return Johnson B or V magnitude from Tycho-2 BT, VT."""
    BT_minus_VT = BT - VT
    if band == "B":
        return BT - 0.27 * BT_minus_VT
    if band == "V":
        return VT - 0.090 * BT_minus_VT
    raise ValueError(f"unknown band {band!r}; expected 'B' or 'V'")


# --- Bias estimation from multi-exposure linearity --------------------------
# When dark/bias frames are unavailable, bias can be solved for by requiring
# (median - bias) / exptime to be equal across same-pointing exposures of
# different lengths (assuming constant sky brightness during the series).
def estimate_bias_from_exposure_series(paths: list[str]) -> float:
    """Solve for the bias that makes sky_per_second equal across all paths.

    Uses a least-squares fit if more than 2 exposures provided.
    Returns bias in ADU.
    """
    medians_and_exptimes = []
    for p in paths:
        with fits.open(p) as hdul:
            d = hdul[0].data.astype(np.float64)
            t = float(hdul[0].header["EXPTIME"])
            m = float(np.median(d))
        medians_and_exptimes.append((m, t))

    # For each exposure: sky_per_sec = (M_i - B) / t_i
    # Setting the rate equal across all gives one equation per pair.
    # Equivalently: solve the over-determined linear system:
    #     M_i = sky_per_sec * t_i + B
    # via least squares.
    M = np.array([m for m, _ in medians_and_exptimes])
    t = np.array([tt for _, tt in medians_and_exptimes])
    A = np.column_stack([t, np.ones_like(t)])
    (sky_per_sec, B), *_ = np.linalg.lstsq(A, M, rcond=None)
    return float(B)


# --- Photometric calibration of one image ----------------------------------
@dataclass
class CalibrationResult:
    label: str
    band: str            # 'B' or 'V'
    exptime: float       # seconds
    bias: float          # ADU
    sky_above_bias: float  # ADU/pixel
    pixel_scale: float   # arcsec/pixel (mean)
    arcsec2_per_pixel: float
    n_stars_used: int
    zero_point: float    # ZP, median across stars
    zero_point_std: float
    sky_rate: float      # ADU / arcsec^2 / sec
    mpsas: float


def calibrate_image(
    path: str,
    band: str,                    # 'B' or 'V' — matches your filter
    bias: float,                  # ADU
    label: str | None = None,
    aperture_radius_pix: float = 12.0,
    annulus_in_pix: float = 24.0,
    annulus_out_pix: float = 36.0,
    star_fwhm_pix: float = 4.0,
    detection_threshold_sigma: float = 10.0,
    catalog_match_arcsec: float = 5.0,
    saturation_adu: int = 55_000,
    mag_max: float = 11.5,        # exclude stars fainter than this in chosen band
) -> CalibrationResult:
    """Compute mpsas (mag/arcsec^2) for a single plate-solved FITS image.

    The image must have a valid WCS embedded in its header (CTYPE1, CRVAL1,
    CD matrix, etc.) — astrometry.net produces compatible output.

    Returns a CalibrationResult; raises RuntimeError if no usable reference
    stars are found.
    """
    if label is None:
        label = path.split("/")[-1]

    with fits.open(path) as hdul:
        data = hdul[0].data.astype(np.float64)
        hdr = hdul[0].header.copy()

    if "EXPTIME" not in hdr:
        raise ValueError(f"{label}: no EXPTIME in header")

    wcs = WCS(hdr)
    ny, nx = data.shape
    exptime = float(hdr["EXPTIME"])

    # Pixel scale from the WCS CD matrix
    cd = wcs.pixel_scale_matrix
    pxs_x = float(np.sqrt(cd[0, 0] ** 2 + cd[1, 0] ** 2)) * 3600.0
    pxs_y = float(np.sqrt(cd[0, 1] ** 2 + cd[1, 1] ** 2)) * 3600.0
    arcsec2_per_pixel = pxs_x * pxs_y
    pixel_scale = (pxs_x + pxs_y) / 2.0

    # Sky background (sigma-clipped) and noise
    _, median_total, std_total = sigma_clipped_stats(data, sigma=3.0)
    sky_per_pixel_above_bias = median_total - bias

    # Detect stars on bias-and-sky-subtracted image
    data_bs = data - bias
    finder = DAOStarFinder(
        fwhm=star_fwhm_pix,
        threshold=detection_threshold_sigma * std_total,
        exclude_border=True,
    )
    sources = finder(data_bs - sky_per_pixel_above_bias)
    if sources is None or len(sources) == 0:
        raise RuntimeError(f"{label}: no stars detected")

    # Convert detected pixel positions to sky coordinates
    src_world = wcs.pixel_to_world(
        np.asarray(sources["x_centroid"]),
        np.asarray(sources["y_centroid"]),
    )
    det_coords = SkyCoord(src_world)

    # Query Tycho-2 catalog for the field
    center = wcs.pixel_to_world(nx / 2.0, ny / 2.0)
    half_fov_arcmin = max(nx * pxs_x, ny * pxs_y) / 2.0 / 60.0

    Vizier.ROW_LIMIT = 2000
    v = Vizier(
        columns=["_RAJ2000", "_DEJ2000", "BTmag", "VTmag", "TYC1", "TYC2", "TYC3"],
        column_filters={"BTmag": "<14"},
    )
    result = v.query_region(center, radius=half_fov_arcmin * u.arcmin,
                            catalog="I/259/tyc2")
    if not result or len(result[0]) == 0:
        raise RuntimeError(f"{label}: no Tycho-2 catalog stars in field")
    cat = result[0]

    cat_coords = SkyCoord(
        ra=np.asarray(cat["_RAJ2000"].data, dtype=float),
        dec=np.asarray(cat["_DEJ2000"].data, dtype=float),
        unit=(u.deg, u.deg),
    )
    idx, d2d, _ = cat_coords.match_to_catalog_sky(det_coords)
    arcsec_dist = d2d.to_value(u.arcsec)

    # Aperture photometry on each acceptable matched star
    zps: list[float] = []
    for r, j, d in zip(cat, idx, arcsec_dist):
        if d > catalog_match_arcsec:
            continue
        s = sources[int(j)]
        BT = float(r["BTmag"])
        VT_raw = r["VTmag"]
        if np.ma.is_masked(VT_raw):
            continue
        VT = float(VT_raw)
        try:
            mag_cat = tycho_to_johnson(BT, VT, band)
        except ValueError:
            continue
        if mag_cat > mag_max:
            continue

        pos = (float(s["x_centroid"]), float(s["y_centroid"]))

        # Saturation check on the *raw* image, not bias-subtracted
        y0, x0 = int(pos[1]), int(pos[0])
        cutout = data[max(0, y0 - 12): y0 + 13, max(0, x0 - 12): x0 + 13]
        if cutout.size and cutout.max() > saturation_adu:
            continue

        # Aperture + annulus photometry on bias-subtracted data
        aper = CircularAperture(pos, r=aperture_radius_pix)
        annulus = CircularAnnulus(
            pos, r_in=annulus_in_pix, r_out=annulus_out_pix
        )
        ann_stats = ApertureStats(data_bs, annulus, sigma_clip=None)
        local_sky = float(ann_stats.median)
        ap_sum = float(aperture_photometry(data_bs, aper)["aperture_sum"][0])
        F_star = ap_sum - local_sky * aper.area
        if F_star <= 0:
            continue

        zps.append(mag_cat + 2.5 * np.log10(F_star / exptime))

    if not zps:
        raise RuntimeError(f"{label}: no usable reference stars after filtering")

    zp_med = float(np.median(zps))
    zp_std = float(np.std(zps))
    sky_rate = sky_per_pixel_above_bias / arcsec2_per_pixel / exptime
    mpsas = zp_med - 2.5 * np.log10(sky_rate)

    return CalibrationResult(
        label=label,
        band=band,
        exptime=exptime,
        bias=bias,
        sky_above_bias=float(sky_per_pixel_above_bias),
        pixel_scale=pixel_scale,
        arcsec2_per_pixel=arcsec2_per_pixel,
        n_stars_used=len(zps),
        zero_point=zp_med,
        zero_point_std=zp_std,
        sky_rate=float(sky_rate),
        mpsas=float(mpsas),
    )


# --- Convert mpsas + reading → K --------------------------------------------
def compute_K(mpsas: float, reading: float) -> float:
    """K such that mpsas = K - 2.5 * log10(reading)."""
    return mpsas + 2.5 * np.log10(reading)


# --- Command-line interface -------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Calibrate a broadband photometer to the SQM scale "
                    "using telescope photometry."
    )
    p.add_argument(
        "--image", "-i", action="append", required=True,
        help="Path to a plate-solved FITS image. Repeat for multiple images."
    )
    p.add_argument(
        "--filter", "-f", action="append", required=True, choices=["B", "V"],
        help="Photometric filter for each image (one per --image, in order)."
    )
    p.add_argument(
        "--reading", "-r", action="append", required=True, type=float,
        help="Photometer reading (e.g. lux from TSL2591) at capture time. "
             "One per --image, in order."
    )
    p.add_argument(
        "--bias", type=float, default=None,
        help="Camera bias pedestal in ADU. If omitted, estimated from "
             "exposure linearity across the supplied images (requires "
             "≥2 exposures of identical pointing/sky)."
    )
    p.add_argument(
        "--mag-max", type=float, default=11.5,
        help="Faintest catalog star (Johnson mag) used as photometric reference. "
             "Default 11.5; raise for shorter exposures."
    )
    p.add_argument(
        "--saturation", type=int, default=55_000,
        help="ADU value above which a pixel is considered saturated and the "
             "containing star is rejected. Default 55,000."
    )
    args = p.parse_args(argv)

    if not (len(args.image) == len(args.filter) == len(args.reading)):
        p.error("--image, --filter, and --reading must be supplied the same "
                "number of times and in the same order")

    bias = args.bias
    if bias is None:
        if len(args.image) < 2:
            p.error("--bias is required when only one image is supplied")
        bias = estimate_bias_from_exposure_series(args.image)
        print(f"Estimated bias from exposure linearity: {bias:.1f} ADU")
    else:
        print(f"Using supplied bias: {bias:.1f} ADU")

    print("─" * 70)
    results: list[tuple[CalibrationResult, float]] = []
    for img, band, reading in zip(args.image, args.filter, args.reading):
        try:
            res = calibrate_image(
                img, band=band, bias=bias,
                mag_max=args.mag_max,
                saturation_adu=args.saturation,
            )
        except Exception as e:
            print(f"FAILED {img}: {e}")
            continue
        K = compute_K(res.mpsas, reading)
        print(f"{res.label}")
        print(f"  filter={res.band}  exptime={res.exptime:.1f}s")
        print(f"  pixel scale = {res.pixel_scale:.4f} arcsec/pix")
        print(f"  sky above bias = {res.sky_above_bias:.1f} ADU/pix")
        print(f"  ZP = {res.zero_point:.3f} ± {res.zero_point_std:.3f} "
              f"(N={res.n_stars_used} stars)")
        print(f"  sky_rate = {res.sky_rate:.3f} ADU/arcsec²/sec")
        print(f"  mpsas    = {res.mpsas:.3f} mag/arcsec²")
        print(f"  reading  = {reading}")
        print(f"  → K = mpsas + 2.5·log₁₀(reading) = {K:.3f}")
        print("─" * 70)
        results.append((res, K))

    if len(results) >= 2:
        Ks = [k for _, k in results]
        print("\nSUMMARY across all images:")
        for res, K in results:
            print(f"  {res.band}-band: mpsas={res.mpsas:.3f}, K={K:.3f}")
        print(f"\n  Mean K   = {float(np.mean(Ks)):.3f}")
        print(f"  Median K = {float(np.median(Ks)):.3f}")
        print(f"  Stdev    = {float(np.std(Ks)):.3f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
