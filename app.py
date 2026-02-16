from flask import Flask, request, jsonify, send_from_directory, Response, redirect
from datetime import datetime, date
from datetime import time as dtime
from datetime import timedelta
import json
import os
import time
import re
import sys
from urllib.request import Request, urlopen, HTTPPasswordMgrWithDefaultRealm, HTTPBasicAuthHandler, build_opener, HTTPCookieProcessor
from urllib.error import URLError, HTTPError
from http.cookiejar import CookieJar
from base64 import b64encode
import hashlib
from urllib.parse import urlencode

try:
    from zoneinfo import ZoneInfo
except Exception:  # pragma: no cover
    ZoneInfo = None

try:
    import requests
except Exception:  # pragma: no cover
    requests = None

try:
    from astral import LocationInfo
    from astral.sun import sunrise, sunset
    from astral.moon import moonrise, moonset
except Exception:  # pragma: no cover
    LocationInfo = None
    sunrise = None
    sunset = None
    moonrise = None
    moonset = None

APP_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(APP_DIR, "data")
os.makedirs(DATA_DIR, exist_ok=True)
LATEST_PATH = os.path.join(DATA_DIR, "latest.json")
LATEST_NODES_DIR = os.path.join(DATA_DIR, "latest")
EVENTS_DIR = os.path.join(DATA_DIR, "events")
LEAK_LOG_PATH = os.path.join(EVENTS_DIR, "leak.log")
os.makedirs(LATEST_NODES_DIR, exist_ok=True)
os.makedirs(EVENTS_DIR, exist_ok=True)

CO2_WARN_PPM = 1200
LOW_BATT_PCT = 20
NODE_OFFLINE_MINUTES = 10

app = Flask(__name__, static_folder="static", static_url_path="")

# --- Persistent cookie jar for Blue Iris session ---
blue_iris_cookie_jar = CookieJar()

# --- Location (Newark, NJ) ---
LOC_NAME = "Newark, NJ"
LOC_REGION = "US"
LOC_TZ = "America/New_York"
LOC_LAT = 40.7357
LOC_LON = -74.1724

# --- Blue Iris Proxy ---
BLUE_IRIS_HOST = "192.168.4.45"
BLUE_IRIS_PORT = 9098
BLUE_IRIS_USER = "Monitor"
BLUE_IRIS_PASS = ""

def _get_blueiris_opener():
    """Get opener with persistent cookie jar for Blue Iris sessions."""
    return build_opener(HTTPCookieProcessor(blue_iris_cookie_jar))

def _get_blueiris_auth_headers():
    credentials = f"{BLUE_IRIS_USER}:{BLUE_IRIS_PASS}"
    encoded = b64encode(credentials.encode()).decode()
    return {"Authorization": f"Basic {encoded}"}

def _blueiris_json_request(opener, params, use_auth=False, method="POST"):
    base_url = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/json"
    headers = {
        "Accept": "application/json, text/plain, */*",
        "Content-Type": "application/x-www-form-urlencoded; charset=UTF-8",
        "X-Requested-With": "XMLHttpRequest",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
    }
    if use_auth:
        headers.update(_get_blueiris_auth_headers())
    if method == "GET":
        url = f"{base_url}?{urlencode(params)}"
        data = None
    else:
        url = base_url
        data = urlencode(params).encode("utf-8")

    req = Request(url, data=data, headers=headers, method=method)
    try:
        with opener.open(req, timeout=5) as resp:
            raw = resp.read()
        try:
            return json.loads(raw.decode("utf-8", errors="ignore"))
        except Exception:
            snippet = raw.decode("utf-8", errors="ignore")[:200]
            print(f"[BI LOGIN] JSON decode failed for {url} body={snippet}")
            return None
    except HTTPError as exc:
        try:
            body = exc.read().decode("utf-8", errors="ignore")
        except Exception:
            body = ""
        print(f"[BI LOGIN] HTTPError {exc.code} for {url} body={body[:200]}")
        return None
    except URLError as exc:
        print(f"[BI LOGIN] URLError for {url}: {exc}")
        return None
    except Exception as exc:
        print(f"[BI LOGIN] Request error for {url}: {type(exc).__name__}: {exc}")
        return None

def _blueiris_fetch_existing_session(opener):
    url = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/login.htm"
    req = Request(url, headers=_get_blueiris_auth_headers())
    try:
        with opener.open(req, timeout=5) as resp:
            raw = resp.read()
        html = raw.decode("utf-8", errors="ignore")
        match = re.search(r'var\s+existingSession\s*=\s*"([^"]+)"', html)
        if match:
            return match.group(1)
        snippet = html[:200]
        print(f"[BI LOGIN] existingSession not found in login.htm body={snippet}")
        return None
    except Exception as exc:
        print(f"[BI LOGIN] Error fetching login.htm: {type(exc).__name__}: {exc}")
        return None

def _blueiris_login_session():
    global _blueiris_login_backoff_until
    if time.time() < _blueiris_login_backoff_until:
        return None

    opener = _get_blueiris_opener()
    existing_session = _blueiris_fetch_existing_session(opener)
    if existing_session:
        digest = hashlib.md5(f"{BLUE_IRIS_USER}:{existing_session}:{BLUE_IRIS_PASS}".encode()).hexdigest()
        second = _blueiris_json_request(
            opener,
            {"cmd": "login", "session": existing_session, "response": digest, "user": BLUE_IRIS_USER},
            use_auth=False,
            method="POST",
        )
        if not second or second.get("result") != "success":
            second = _blueiris_json_request(
                opener,
                {"cmd": "login", "session": existing_session, "response": digest, "user": BLUE_IRIS_USER},
                use_auth=True,
                method="POST",
            )
        if second and second.get("result") == "success":
            _blueiris_login_backoff_until = 0.0
            return second.get("session") or existing_session
        print(f"[BI LOGIN] Login with existingSession failed: {second}")
        # Continue with a full login handshake instead of returning a likely invalid token.
    attempts = [
        ({"cmd": "login"}, False, "GET"),
        ({"cmd": "login"}, True, "GET"),
        ({"cmd": "login"}, False, "POST"),
        ({"cmd": "login", "user": BLUE_IRIS_USER}, True, "POST"),
        ({"cmd": "login", "user": BLUE_IRIS_USER}, False, "POST"),
        ({"cmd": "login"}, True, "POST"),
    ]
    first = None
    for params, use_auth, method in attempts:
        first = _blueiris_json_request(opener, params, use_auth=use_auth, method=method)
        if first and "session" in first:
            break
        print(f"[BI LOGIN] First login response (auth={use_auth} method={method} params={params}): {first}")
    if not first or "session" not in first:
        _blueiris_login_backoff_until = time.time() + BLUE_IRIS_LOGIN_BACKOFF_SECONDS
        return None
    session = first.get("session")
    digest = hashlib.md5(f"{BLUE_IRIS_USER}:{session}:{BLUE_IRIS_PASS}".encode()).hexdigest()
    second = _blueiris_json_request(
        opener,
        {"cmd": "login", "session": session, "response": digest, "user": BLUE_IRIS_USER},
        use_auth=False,
        method="POST",
    )
    if not second or second.get("result") != "success":
        second = _blueiris_json_request(
            opener,
            {"cmd": "login", "session": session, "response": digest, "user": BLUE_IRIS_USER},
            use_auth=True,
            method="POST",
        )
    if not second or second.get("result") != "success":
        print(f"[BI LOGIN] Second login response: {second}")
        _blueiris_login_backoff_until = time.time() + BLUE_IRIS_LOGIN_BACKOFF_SECONDS
        return None
    _blueiris_login_backoff_until = 0.0
    return session

def _get_nav_bar_html():
    """Return HTML for the navigation bar that can be injected into pages."""
    nav_html = """
<!-- Injected Navigation Bar -->
<div id="injectedNavBar" class="navBar" style="position: fixed; bottom: 0; left: 0; right: 0; height: 50px; background: #161a22; border-top: 1px solid #252b36; display: flex; justify-content: center; gap: 10px; padding: 8px; z-index: 9999; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;">
  <button class="navBtn" data-screen="weatherScreen" onclick="navClickHandler(this)" style="flex: 1; max-width: 200px; padding: 10px 20px; font-size: 16px; font-weight: 700; background: #252b36; color: #e7eaf0; border: 1px solid #3d444f; border-radius: 8px; cursor: pointer; transition: all 0.2s; font-family: inherit;">Weather</button>
  <button class="navBtn" data-screen="camerasScreen" onclick="navClickHandler(this)" style="flex: 1; max-width: 200px; padding: 10px 20px; font-size: 16px; font-weight: 700; background: #252b36; color: #e7eaf0; border: 1px solid #3d444f; border-radius: 8px; cursor: pointer; transition: all 0.2s; font-family: inherit;">Cameras</button>
  <button class="navBtn" data-screen="monitorScreen" onclick="navClickHandler(this)" style="flex: 1; max-width: 200px; padding: 10px 20px; font-size: 16px; font-weight: 700; background: #252b36; color: #e7eaf0; border: 1px solid #3d444f; border-radius: 8px; cursor: pointer; transition: all 0.2s; font-family: inherit;">Monitor</button>
</div>

<style>
#injectedNavBar button:hover { background: #3d444f !important; }
#injectedNavBar button.active { background: #0f8dff !important; border-color: #0f8dff !important; }
body { margin-bottom: 60px !important; }
</style>

<script type="text/javascript">
function navClickHandler(btn) {
  var screenId = btn.getAttribute('data-screen');
  
  // Update button styles
  var buttons = document.querySelectorAll('#injectedNavBar .navBtn');
  for (var i = 0; i < buttons.length; i++) {
    buttons[i].classList.remove('active');
  }
  btn.classList.add('active');
  
  // Navigate
    if (screenId === 'weatherScreen') {
        window.location.href = 'http://192.168.1.233:8080/';
    } else if (screenId === 'camerasScreen') {
        window.location.href = 'http://192.168.1.233:8080/cameras';
    } else if (screenId === 'monitorScreen') {
        window.location.href = 'http://192.168.1.233:8080/monitor';
    }
}

// Set active button on page load
(function() {
  var currentUrl = window.location.href;
  var activeBtn = 'weatherScreen';
    if (currentUrl.indexOf('/cameras') > -1 || currentUrl.indexOf('192.168.4.45') > -1) {
    activeBtn = 'camerasScreen';
  }
  var btn = document.querySelector('#injectedNavBar [data-screen="' + activeBtn + '"]');
  if (btn) btn.classList.add('active');
})();
</script>
"""
    return nav_html


FORECAST_TTL_SECONDS = 60 * 60
_forecast_cache = {
    "ts": 0.0,
    "data": None,  # list[dict] five-day summaries
    "meta": None,  # string
    "error": None,
}

BLUE_IRIS_LOGIN_BACKOFF_SECONDS = 300
_blueiris_login_backoff_until = 0.0


SERVER_BUILD = datetime.now().isoformat(timespec="seconds")


def _fmt_ampm(dt):
    if dt is None:
        return None
    try:
        h = dt.hour
        m = dt.minute
        ampm = "PM" if h >= 12 else "AM"
        h = h % 12
        if h == 0:
            h = 12
        return f"{h}:{m:02d} {ampm}"
    except Exception:
        return None


def _get_tzinfo():
    if ZoneInfo is None:
        return None
    try:
        return ZoneInfo(LOC_TZ)
    except Exception:
        return None


def _astro_times_for_today() -> dict:
    """Return sun/moon times for today's local date at LOC_*.

    Values are formatted strings like '7:12 AM' or None.
    """
    tz = _get_tzinfo()
    d = date.today()

    # Preferred: Astral (more accurate, handles edge cases better)
    if LocationInfo is not None and sunrise is not None and sunset is not None:
        loc = LocationInfo(LOC_NAME, LOC_REGION, LOC_TZ, LOC_LAT, LOC_LON)

        def _safe(callable_fn):
            try:
                return callable_fn()
            except Exception:
                return None

        sr = _safe(lambda: sunrise(loc.observer, date=d, tzinfo=tz))
        ss = _safe(lambda: sunset(loc.observer, date=d, tzinfo=tz))
        mr = _safe(lambda: moonrise(loc.observer, date=d, tzinfo=tz)) if moonrise else None
        ms = _safe(lambda: moonset(loc.observer, date=d, tzinfo=tz)) if moonset else None

        return {
            "sunrise": _fmt_ampm(sr),
            "sunset": _fmt_ampm(ss),
            "moonrise": _fmt_ampm(mr),
            "moonset": _fmt_ampm(ms),
        }

    # Fallback: built-in approximation (no dependencies)
    sr, ss = _sun_times_for_date(d, LOC_LAT, LOC_LON, tz)
    mr, ms = _moon_times_for_date(d, LOC_LAT, LOC_LON, tz)

    return {
        "sunrise": _fmt_ampm(sr),
        "sunset": _fmt_ampm(ss),
        "moonrise": _fmt_ampm(mr),
        "moonset": _fmt_ampm(ms),
    }


# --- Sun/Moon fallback math (ported from the iPad JS version; approximate) ---
RAD = 3.141592653589793 / 180.0
J0 = 0.0009


def _to_julian(dt: datetime) -> float:
    return (dt.timestamp() / 86400.0) + 2440587.5


def _from_julian(j: float, tz) -> datetime:
    dt = datetime.fromtimestamp((j - 2440587.5) * 86400.0)
    if tz is not None:
        try:
            return dt.replace(tzinfo=None).astimezone(tz)  # may raise if dt naive
        except Exception:
            # safer conversion
            return datetime.fromtimestamp(dt.timestamp(), tz=tz)
    return dt


def _to_days(dt: datetime) -> float:
    return _to_julian(dt) - 2451545.0


def _solar_mean_anomaly(d: float) -> float:
    return RAD * (357.5291 + 0.98560028 * d)


def _ecliptic_longitude(M: float) -> float:
    C = RAD * (1.9148 * _sin(M) + 0.02 * _sin(2 * M) + 0.0003 * _sin(3 * M))
    P = RAD * 102.9372
    return M + C + P + 3.141592653589793


def _declination(L: float) -> float:
    e = RAD * 23.4397
    return _asin(_sin(e) * _sin(L))


def _julian_cycle(d: float, lw: float) -> float:
    return round(d - J0 - lw / (2 * 3.141592653589793))


def _approx_transit(Ht: float, lw: float, n: float) -> float:
    return J0 + (Ht + lw) / (2 * 3.141592653589793) + n


def _solar_transit_j(ds: float, M: float, L: float) -> float:
    return 2451545.0 + ds + 0.0053 * _sin(M) - 0.0069 * _sin(2 * L)


def _hour_angle(h: float, phi: float, dec: float) -> float:
    return _acos((_sin(h) - _sin(phi) * _sin(dec)) / (_cos(phi) * _cos(dec)))


def _sidereal_time(d: float, lw: float) -> float:
    return RAD * (280.16 + 360.9856235 * d) - lw


def _sun_times_for_date(d0: date, lat: float, lon: float, tz):
    # Calculate for local noon to land on the intended local date.
    base = datetime(d0.year, d0.month, d0.day, 12, 0, 0)
    if tz is not None:
        base = base.replace(tzinfo=tz)

    lw = RAD * -lon
    phi = RAD * lat
    d = _to_days(base)
    n = _julian_cycle(d, lw)
    ds = _approx_transit(0.0, lw, n)
    M = _solar_mean_anomaly(ds)
    L = _ecliptic_longitude(M)
    dec = _declination(L)

    h0 = RAD * (-0.833)
    w0 = _hour_angle(h0, phi, dec)
    if w0 != w0:  # NaN check
        return None, None

    Jrise = _solar_transit_j(_approx_transit(-w0, lw, n), M, L)
    Jset = _solar_transit_j(_approx_transit(w0, lw, n), M, L)
    # Convert from Julian to local
    rise = datetime.fromtimestamp((_from_julian(Jrise, tz).timestamp()), tz=tz) if tz is not None else _from_julian(Jrise, None)
    sett = datetime.fromtimestamp((_from_julian(Jset, tz).timestamp()), tz=tz) if tz is not None else _from_julian(Jset, None)
    return rise, sett


def _moon_coords(d: float):
    L = RAD * (218.316 + 13.176396 * d)
    M = RAD * (134.963 + 13.064993 * d)
    F = RAD * (93.272 + 13.229350 * d)

    l = L + RAD * 6.289 * _sin(M)
    b = RAD * 5.128 * _sin(F)

    e = RAD * 23.4397
    ra = _atan2(_sin(l) * _cos(e) - _tan(b) * _sin(e), _cos(l))
    dec = _asin(_sin(b) * _cos(e) + _cos(b) * _sin(e) * _sin(l))
    return ra, dec


def _moon_altitude(dt: datetime, lat: float, lon: float) -> float:
    lw = RAD * -lon
    phi = RAD * lat
    d = _to_days(dt)
    ra, dec = _moon_coords(d)
    H = _sidereal_time(d, lw) - ra
    return _asin(_sin(phi) * _sin(dec) + _cos(phi) * _cos(dec) * _cos(H))


def _moon_times_for_date(d0: date, lat: float, lon: float, tz):
    """Approx moonrise/moonset within the local date.

    Returns (rise_dt, set_dt) as datetimes in tz (or naive if tz is None).
    """
    start = datetime(d0.year, d0.month, d0.day, 0, 0, 0, tzinfo=tz)
    hc = RAD * 0.133

    def alt(dt):
        return _moon_altitude(dt, lat, lon) - hc

    prev_t = start
    prev_a = alt(prev_t)
    rise = None
    sett = None

    # Scan up to 48 hours from local midnight; sometimes moonset (or moonrise)
    # falls shortly after midnight the next day.
    for hr in range(1, 49):
        cur_t = start + timedelta(hours=hr)
        cur_a = alt(cur_t)

        crossed = (prev_a <= 0 < cur_a) or (prev_a >= 0 > cur_a)
        if crossed:
            lo = prev_t
            hi = cur_t
            lo_a = prev_a
            hi_a = cur_a
            for _ in range(20):
                mid_ts = (lo.timestamp() + hi.timestamp()) / 2.0
                mid = datetime.fromtimestamp(mid_ts, tz=tz) if tz is not None else datetime.fromtimestamp(mid_ts)
                mid_a = alt(mid)

                if (lo_a <= 0 < mid_a) or (lo_a >= 0 > mid_a):
                    hi = mid
                    hi_a = mid_a
                else:
                    lo = mid
                    lo_a = mid_a

            evt = datetime.fromtimestamp((lo.timestamp() + hi.timestamp()) / 2.0, tz=tz) if tz is not None else datetime.fromtimestamp((lo.timestamp() + hi.timestamp()) / 2.0)

            if prev_a <= 0 < cur_a:
                if rise is None:
                    rise = evt
            else:
                if sett is None:
                    sett = evt

            if rise is not None and sett is not None:
                break

        prev_t = cur_t
        prev_a = cur_a

    return rise, sett


def _moon_phase_label(now_dt: datetime) -> str:
    """Return a readable moon phase name (approx, no dependencies)."""
    # Known reference new moon near J2000 (2000-01-06 18:14 UTC) ≈ JD 2451550.1
    synodic = 29.53058867
    jd = _to_julian(now_dt)
    age = (jd - 2451550.1) % synodic
    phase = age / synodic  # 0..1

    # Boundaries are approximate; good enough for a dashboard label.
    if phase < 0.03 or phase > 0.97:
        return "New Moon"
    if phase < 0.22:
        return "Waxing Crescent"
    if phase < 0.28:
        return "First Quarter"
    if phase < 0.47:
        return "Waxing Gibbous"
    if phase < 0.53:
        return "Full Moon"
    if phase < 0.72:
        return "Waning Gibbous"
    if phase < 0.78:
        return "Last Quarter"
    return "Waning Crescent"


# Minimal trig wrappers (avoid repeated math. prefix)
def _sin(x):
    import math

    return math.sin(x)


def _cos(x):
    import math

    return math.cos(x)


def _tan(x):
    import math

    return math.tan(x)


def _asin(x):
    import math

    return math.asin(x)


def _acos(x):
    import math

    return math.acos(x)


def _atan2(y, x):
    import math

    return math.atan2(y, x)


def _http_get_json(url: str, timeout: int = 6):
    """Fetch JSON using requests if available, else urllib."""
    ua = "WS2000LocalWeather/1.0 (LAN dashboard)"
    headers = {
        "User-Agent": ua,
        "Accept": "application/geo+json",
    }

    if requests is not None:
        r = requests.get(url, headers=headers, timeout=timeout)
        r.raise_for_status()
        return r.json()

    req = Request(url, headers=headers, method="GET")
    with urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8")
        return json.loads(raw)


def _nws_condition_label(text):
    if not text:
        return None
    s = text.lower()
    # Priority: precip > fog > clouds > clear/sun
    if re.search(r"thunder|t-storm|storm", s):
        return "Storm"
    if re.search(r"snow|sleet|flurr", s):
        return "Snow"
    if re.search(r"rain|shower|drizzle", s):
        return "Rain"
    if re.search(r"fog|mist|haze", s):
        return "Fog"
    if re.search(r"overcast", s):
        return "Overcast"
    if re.search(r"cloud", s):
        return "Cloudy"
    if re.search(r"sun|clear|fair", s):
        return "Sunny"
    # fallback: short first word capitalized
    return text.split()[0].capitalize() if text.split() else None


def _fetch_nws_5day():
    """Fetch NWS forecast and reduce to 5 days: {label, hi, lo, cond}.

    Uses the NWS "forecast" endpoint (not hourly).
    """
    points_url = f"https://api.weather.gov/points/{LOC_LAT},{LOC_LON}"
    try:
        points = _http_get_json(points_url, timeout=6)
        forecast_url = points.get("properties", {}).get("forecast")
    except Exception as e:
        return None, f"points fetch failed: {e}"

    if not forecast_url:
        return None, "no forecast url"

    try:
        fc = _http_get_json(forecast_url, timeout=6)
        periods = fc.get("properties", {}).get("periods")
    except Exception as e:
        return None, f"forecast fetch failed: {e}"

    if not isinstance(periods, list):
        return None, "invalid periods"

    tz = _get_tzinfo()
    by_day = {}

    for p in periods[:14]:  # typically 7 days day/night
        if not isinstance(p, dict):
            continue
        st = p.get("startTime")
        if not st:
            continue
        try:
            dt = datetime.fromisoformat(st)
            if tz is not None:
                dt = dt.astimezone(tz)
            day = dt.date()
        except Exception:
            continue

        slot = by_day.get(day)
        if slot is None:
            slot = {"date": day, "hi": None, "lo": None, "cond": None}
            by_day[day] = slot

        temp = p.get("temperature")
        try:
            temp = int(temp) if temp is not None else None
        except Exception:
            temp = None

        is_day = bool(p.get("isDaytime"))
        if temp is not None:
            if is_day:
                slot["hi"] = temp if slot["hi"] is None else max(slot["hi"], temp)
            else:
                slot["lo"] = temp if slot["lo"] is None else min(slot["lo"], temp)

        cond = _nws_condition_label(p.get("shortForecast"))
        if cond:
            # upgrade condition based on priority by re-evaluating combined string
            existing = slot.get("cond")
            if existing is None:
                slot["cond"] = cond
            else:
                # pick higher priority by applying label function to concatenated text
                # (keeps Storm/Rain/Snow winning if present anywhere)
                slot["cond"] = _nws_condition_label(existing + " " + cond) or existing

    # Build next 5 days starting today (local)
    today = datetime.now(tz).date() if tz is not None else date.today()
    out = []
    for offset in range(0, 10):
        d0 = today.fromordinal(today.toordinal() + offset)
        if d0 not in by_day:
            continue
        slot = by_day[d0]
        out.append(
            {
                "label": d0.strftime("%a"),
                "hi": slot.get("hi"),
                "lo": slot.get("lo"),
                "cond": slot.get("cond") or "--",
            }
        )
        if len(out) >= 5:
            break

    if not out:
        return None, "no days"

    return out, "NWS 5-Day"


def _get_cached_forecast():
    now = time.time()
    if _forecast_cache["data"] is not None and (now - _forecast_cache["ts"]) < FORECAST_TTL_SECONDS:
        return _forecast_cache["data"], _forecast_cache["meta"]

    periods, meta_or_err = _fetch_nws_5day()
    if periods is None:
        # keep last good data if any
        _forecast_cache["error"] = meta_or_err
        if _forecast_cache["data"] is not None:
            return _forecast_cache["data"], _forecast_cache["meta"]
        return None, meta_or_err

    _forecast_cache["ts"] = now
    _forecast_cache["data"] = periods
    _forecast_cache["meta"] = meta_or_err
    _forecast_cache["error"] = None
    return periods, meta_or_err

def _load_latest():
    if os.path.exists(LATEST_PATH):
        try:
            with open(LATEST_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {"timestamp": None}
    return {"timestamp": None}


def _to_num(v):
    try:
        return float(v)
    except Exception:
        return None


def _sanitize_node_id(raw):
    node_id = str(raw or "").strip()
    if not node_id:
        return None
    node_id = re.sub(r"[^A-Za-z0-9_-]", "_", node_id)
    return node_id[:64] if node_id else None


def _atomic_write_json(path: str, payload: dict):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def _append_jsonl(path: str, payload: dict):
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(payload, ensure_ascii=False) + "\n")


def _epoch_now():
    return int(time.time())


def _to_epoch(value):
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return int(value)
    try:
        s = str(value).strip()
        if not s:
            return None
        if s.isdigit():
            return int(s)
        dt = datetime.fromisoformat(s.replace("Z", "+00:00"))
        return int(dt.timestamp())
    except Exception:
        return None


def _load_latest_nodes_and_base():
    nodes = []
    base = None
    try:
        names = sorted(os.listdir(LATEST_NODES_DIR))
    except Exception:
        names = []

    for name in names:
        if not name.lower().endswith(".json"):
            continue
        path = os.path.join(LATEST_NODES_DIR, name)
        try:
            with open(path, "r", encoding="utf-8") as f:
                item = json.load(f)
        except Exception:
            continue

        if name.lower() == "base.json":
            base = item
        else:
            nodes.append(item)

    return nodes, base


def _build_monitor_state():
    now_epoch = _epoch_now()
    offline_seconds = NODE_OFFLINE_MINUTES * 60
    alerts = []
    sensors = []

    nodes, base = _load_latest_nodes_and_base()

    for rec in nodes:
        payload = rec.get("payload") if isinstance(rec, dict) else None
        if not isinstance(payload, dict):
            continue

        node_id = str(payload.get("id") or rec.get("node_id") or "unknown")
        batt_pct = _to_num(payload.get("batt_pct"))
        vbatt = _to_num(payload.get("vbatt"))
        t_c = _to_num(payload.get("t_c"))
        rh = _to_num(payload.get("rh"))
        leak = int(_to_num(payload.get("leak")) or 0)
        wet = _to_num(payload.get("wet"))
        rssi = _to_num(rec.get("rssi"))
        snr = _to_num(rec.get("snr"))
        rx_ts = _to_epoch(rec.get("rx_ts"))
        if rx_ts is None:
            rx_ts = _to_epoch(rec.get("received_at"))

        offline = bool(rx_ts is None or (now_epoch - rx_ts) > offline_seconds)

        battery_label = "--"
        if batt_pct is not None and vbatt is not None:
            battery_label = f"{int(round(batt_pct))}% ({vbatt:.2f}V)"
        elif batt_pct is not None:
            battery_label = f"{int(round(batt_pct))}%"
        elif vbatt is not None:
            battery_label = f"{vbatt:.2f}V"

        item = {
            "sensor_id": node_id,
            "name": node_id,
            "temperature_c": t_c,
            "temperature_f": (t_c * 9.0 / 5.0 + 32.0) if t_c is not None else None,
            "humidity": rh,
            "leak": leak,
            "wet": wet,
            "vbatt": vbatt,
            "batt_pct": batt_pct,
            "battery": battery_label,
            "rssi": rssi,
            "snr": snr,
            "signal": rssi,
            "seq": payload.get("seq"),
            "last_rx_ts": rx_ts,
            "offline": offline,
        }
        sensors.append(item)

        if leak == 1:
            alerts.append({"severity": "critical", "type": "leak", "node_id": node_id, "message": f"Leak detected at {node_id}"})
        if batt_pct is not None and batt_pct < LOW_BATT_PCT:
            alerts.append({"severity": "warning", "type": "battery", "node_id": node_id, "message": f"Low battery at {node_id}: {int(round(batt_pct))}%"})
        if offline:
            alerts.append({"severity": "warning", "type": "offline", "node_id": node_id, "message": f"Node offline: {node_id}"})

    base_out = None
    if isinstance(base, dict):
        co2 = _to_num(base.get("co2_ppm"))
        t_c = _to_num(base.get("t_c"))
        rh = _to_num(base.get("rh"))
        voc = _to_num(base.get("voc_index"))
        rx_ts = _to_epoch(base.get("rx_ts"))
        if rx_ts is None:
            rx_ts = _to_epoch(base.get("received_at"))

        base_out = {
            "src": base.get("src") or "base",
            "co2_ppm": co2,
            "t_c": t_c,
            "temperature_f": (t_c * 9.0 / 5.0 + 32.0) if t_c is not None else None,
            "rh": rh,
            "voc_index": voc,
            "last_rx_ts": rx_ts,
        }
        if co2 is not None and co2 > CO2_WARN_PPM:
            alerts.append({"severity": "warning", "type": "co2", "node_id": "base", "message": f"High CO₂ at base: {int(round(co2))} ppm"})

    sensors.sort(key=lambda x: str(x.get("name") or ""))
    return {
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "sensor_count": len(sensors),
        "sensors": sensors,
        "base": base_out,
        "alerts": alerts,
        "thresholds": {
            "co2_warn_ppm": CO2_WARN_PPM,
            "battery_warn_pct": LOW_BATT_PCT,
            "offline_minutes": NODE_OFFLINE_MINUTES,
        },
    }


def _build_monitor_sensors(payload: dict):
    sensors = {}

    def _slot(sensor_id: int):
        slot = sensors.get(sensor_id)
        if slot is None:
            slot = {
                "sensor_id": sensor_id,
                "name": f"Sensor {sensor_id}",
                "temperature_f": None,
                "humidity": None,
                "battery": None,
                "signal": None,
            }
            sensors[sensor_id] = slot
        return slot

    for key, raw in payload.items():
        k = str(key).lower()

        m = re.fullmatch(r"temp(\d+)f", k)
        if m:
            sensor_id = int(m.group(1))
            _slot(sensor_id)["temperature_f"] = _to_num(raw)
            continue

        m = re.fullmatch(r"humidity(\d+)", k)
        if m:
            sensor_id = int(m.group(1))
            _slot(sensor_id)["humidity"] = _to_num(raw)
            continue

        m = re.fullmatch(r"batt(\d+)", k)
        if m:
            sensor_id = int(m.group(1))
            _slot(sensor_id)["battery"] = raw
            continue

        m = re.fullmatch(r"rssi(\d+)", k)
        if m:
            sensor_id = int(m.group(1))
            _slot(sensor_id)["signal"] = _to_num(raw)
            continue

    return [sensors[sid] for sid in sorted(sensors.keys())]

def _save_latest(payload: dict):
    tmp = LATEST_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    os.replace(tmp, LATEST_PATH)

def _ingest_request(extra_params: str = ""):
    """
    Ingest data from either:
      - normal query string (?a=b&c=d) via request.args / request.values
      - weird WS-2000 form where params are appended to path: /weather&k=v&k2=v2
    """
    # Start with whatever Flask parsed normally
    latest = dict(request.values.items())

    # If station appended params to the PATH after an '&', parse them too
    # extra_params comes in like "PASSKEY=...&tempf=..." (no leading & after split)
    if extra_params:
        for pair in extra_params.split("&"):
            if not pair:
                continue
            if "=" in pair:
                k, v = pair.split("=", 1)
                latest[k] = v
            else:
                # handle key-only flags just in case
                latest[pair] = ""

    latest["timestamp"] = datetime.now().isoformat(timespec="seconds")
    _save_latest(latest)
    return "OK"

# Normal case: /weather?PASSKEY=...&tempf=...
@app.route("/weather", methods=["POST", "GET"])
def weather():
    return _ingest_request()

# WS-2000 weird case: /weather&PASSKEY=...&tempf=...
@app.route("/weather<path:rest>", methods=["POST", "GET"])
def weather_with_rest(rest):
    # rest will start with "&PASSKEY=..." or similar
    if rest.startswith("&"):
        rest = rest[1:]
    return _ingest_request(rest)

@app.route("/data", methods=["GET"])
def data():
    payload = _load_latest()

    # Always include keys so the dashboard can rely on them.
    payload.setdefault("sunrise", None)
    payload.setdefault("sunset", None)
    payload.setdefault("moonrise", None)
    payload.setdefault("moonset", None)
    payload.setdefault("moon_phase", None)

    # Add server-side computed extras (do NOT persist to latest.json)
    try:
        payload.update(_astro_times_for_today())
    except Exception:
        pass

    try:
        tz = _get_tzinfo()
        now_dt = datetime.now(tz) if tz is not None else datetime.now()
        payload["moon_phase"] = _moon_phase_label(now_dt)
    except Exception:
        pass

    try:
        periods, meta = _get_cached_forecast()
        if periods is not None:
            payload["forecast_5day"] = periods
            payload["forecast_meta"] = meta
        else:
            # Meta is an error string in this case.
            payload["forecast_error"] = meta
    except Exception:
        payload["forecast_error"] = "forecast exception"

    # Lightweight diagnostics (helps when files are transferred but deps/internet differ).
    try:
        payload["server_build"] = SERVER_BUILD
        payload["server_python"] = sys.version.split()[0]
        payload["server_executable"] = sys.executable
        payload["server_has_requests"] = bool(requests)
        payload["server_has_astral"] = bool(LocationInfo)
    except Exception:
        pass

    return jsonify(payload)


@app.route("/api/lora", methods=["POST"])
def api_lora():
    packet = request.get_json(silent=True)
    if not isinstance(packet, dict):
        return jsonify({"ok": False, "error": "invalid json"}), 400

    payload = packet.get("payload")
    if not isinstance(payload, dict):
        return jsonify({"ok": False, "error": "missing payload object"}), 400

    node_id = _sanitize_node_id(payload.get("id"))
    if not node_id:
        return jsonify({"ok": False, "error": "missing payload.id"}), 400

    rec = {
        "gateway_id": packet.get("gateway_id") or "gw-main",
        "node_id": node_id,
        "rx_ts": _to_epoch(packet.get("rx_ts")) or _epoch_now(),
        "rssi": _to_num(packet.get("rssi")),
        "snr": _to_num(packet.get("snr")),
        "payload": payload,
        "received_at": datetime.now().isoformat(timespec="seconds"),
    }

    path = os.path.join(LATEST_NODES_DIR, f"{node_id}.json")
    _atomic_write_json(path, rec)

    leak = int(_to_num(payload.get("leak")) or 0)
    if leak == 1:
        _append_jsonl(
            LEAK_LOG_PATH,
            {
                "ts": rec["received_at"],
                "node_id": node_id,
                "gateway_id": rec["gateway_id"],
                "seq": payload.get("seq"),
                "wet": _to_num(payload.get("wet")),
                "vbatt": _to_num(payload.get("vbatt")),
                "batt_pct": _to_num(payload.get("batt_pct")),
                "rssi": rec.get("rssi"),
                "snr": rec.get("snr"),
            },
        )

    return jsonify({"ok": True, "node_id": node_id})


@app.route("/api/base", methods=["POST"])
def api_base():
    body = request.get_json(silent=True)
    if not isinstance(body, dict):
        return jsonify({"ok": False, "error": "invalid json"}), 400

    rec = {
        "src": body.get("src") or "base",
        "rx_ts": _epoch_now(),
        "co2_ppm": _to_num(body.get("co2_ppm")),
        "t_c": _to_num(body.get("t_c")),
        "rh": _to_num(body.get("rh")),
        "voc_index": _to_num(body.get("voc_index")),
        "wifi_rssi": _to_num(body.get("wifi_rssi")),
        "uptime_s": _to_num(body.get("uptime_s")),
        "received_at": datetime.now().isoformat(timespec="seconds"),
    }

    _atomic_write_json(os.path.join(LATEST_NODES_DIR, "base.json"), rec)
    return jsonify({"ok": True, "src": rec["src"]})


@app.route("/monitor_data", methods=["GET"])
def monitor_data():
    state = _build_monitor_state()

    if state.get("sensor_count", 0) == 0:
        payload = _load_latest()
        sensors = _build_monitor_sensors(payload)
        if sensors:
            state["timestamp"] = payload.get("timestamp")
            state["sensor_count"] = len(sensors)
            state["sensors"] = sensors

    return jsonify(state)

@app.route("/", methods=["GET"])
def index():
    # If there's a Blue Iris session parameter, forward to the proxy
    session_param = request.args.get("session")
    if session_param:
        print(f"[BI] Detected session parameter in root path, redirecting to /blueiris/")
        target = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/ui3.htm?session={session_param}"
        return redirect(target, code=302)
    # Otherwise serve the main dashboard
    return send_from_directory(app.static_folder, "index.html")


@app.route("/monitor", methods=["GET"])
def monitor():
    return send_from_directory(app.static_folder, "monitor.html")

@app.route("/health", methods=["GET"])
def health():
    return "OK"

@app.route("/blueiris_login", methods=["POST"])
def blueiris_login():
    """Authenticate with Blue Iris via HTTP request."""
    try:
        print(f"\n[BI API] Login attempt started")
        print(f"[BI API] User: {BLUE_IRIS_USER}, Pass: {'(empty)' if not BLUE_IRIS_PASS else '(set)'}")
        
        # Simple request to establish session
        index_url = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/login.htm"
        print(f"[BI API] Target URL: {index_url}")
        
        # Use persistent opener with cookies
        opener = _get_blueiris_opener()
        print(f"[BI API] Opener created with cookie jar")
        
        req = Request(index_url)
        print(f"[BI API] Sending GET request...")
        
        with opener.open(req, timeout=5) as resp:
            content = resp.read()
            print(f"[BI API] ✓ Response: HTTP {resp.status} ({len(content)} bytes)")
            print(f"[BI API] Cookies in jar: {len(blue_iris_cookie_jar)} cookies")
            for cookie in blue_iris_cookie_jar:
                print(f"[BI API]   - {cookie.name} = {cookie.value[:50]}..." if len(cookie.value) > 50 else f"[BI API]   - {cookie.name} = {cookie.value}")
        
        print(f"[BI API] ✓ Session established, returning success")
        return jsonify({"status": "logged_in"})
    except Exception as e:
        print(f"[BI API] ✗ Login error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route("/blueiris/login", methods=["GET"])
def blueiris_login_page():
        """Server-side login to Blue Iris, then redirect to UI3 with session."""
        print(f"\n[BI LOGIN] Attempting server-side login for http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}")
        session = _blueiris_login_session()
        if session:
            target = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/ui3.htm?session={session}"
            print(f"[BI LOGIN] Success, redirecting to {target}")
            return redirect(target, code=302)
        fallback = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/ui3.htm"
        print(f"[BI LOGIN] Login failed, redirecting to {fallback}")
        return redirect(fallback, code=302)

@app.route("/cameras", methods=["GET"])
def cameras():
    """Camera page with overlay buttons and an embedded UI3 view."""
    session = _blueiris_login_session()
    if session:
        ui3_src = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/ui3.htm?session={session}"
    else:
        ui3_src = f"http://{BLUE_IRIS_HOST}:{BLUE_IRIS_PORT}/ui3.htm"
    html = f"""<!DOCTYPE html>
<html lang=\"en\">
    <head>
        <meta charset=\"utf-8\" />
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
        <title>Cameras</title>
        <style>
            html, body {{ height: 100%; margin: 0; background: #0b0d12; }}
            #cameraFrame {{ position: fixed; top: 0; left: 0; right: 0; bottom: 0; border: 0; width: 100%; height: 100%; }}
            #overlayButtons {{ position: fixed; top: 50%; left: 12px; transform: translateY(-50%); display: flex; flex-direction: column; gap: 10px; z-index: 9999; }}
            #overlayButtons button {{ padding: 10px 12px; background: rgba(22, 26, 34, 0.92); color: #e7eaf0; border: 1px solid #3d444f; border-radius: 10px; font-size: 14px; font-weight: 700; cursor: pointer; min-width: 110px; }}
            #overlayButtons button:hover {{ background: rgba(61, 68, 79, 0.92); }}
        </style>
    </head>
    <body>
        <iframe id=\"cameraFrame\" src=\"{ui3_src}\" allow=\"autoplay; fullscreen\"></iframe>
        <div id=\"overlayButtons\">
            <button onclick="window.location.href='/'">Weather</button>
            <button onclick="window.location.href='/monitor'">Monitor</button>
            <button onclick=\"document.getElementById('cameraFrame').src = document.getElementById('cameraFrame').src;\">Refresh</button>
        </div>
    </body>
</html>"""
    return Response(html, content_type="text/html")


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
