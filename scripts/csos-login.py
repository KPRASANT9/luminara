#!/usr/bin/env python3
"""
CSOS Login — Zero-password browser authentication.

Opens a browser window for human to log in. Captures cookies.
Adapts to environment: headed (display), headless (Xvfb), or manual (no browser).

Usage:
  python3 scripts/csos-login.py https://linkedin.com/login
  python3 scripts/csos-login.py https://indeed.com/login --headless
  python3 scripts/csos-login.py https://site.com --manual
"""
import sys, json, os, time
from pathlib import Path
from urllib.parse import urlparse

def detect_display():
    """Check if a display server is available."""
    return bool(os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'))

def detect_playwright():
    """Check if Playwright + Chromium are available."""
    try:
        from playwright.sync_api import sync_playwright
        return True
    except ImportError:
        return False

def session_paths(url):
    """Derive session file path and domain from URL."""
    dom = urlparse(url).hostname or ''
    parent_dom = '.'.join(dom.split('.')[-2:]) if '.' in dom else dom
    sid = dom.replace('.', '_')
    sd = Path('.csos/sessions')
    sd.mkdir(parents=True, exist_ok=True)
    return dom, parent_dom, sid, sd / f'{sid}.json'

def save_auth_mapping(parent_dom, sid):
    """Update human.json with auth mapping."""
    hf = Path('.csos/sessions/human.json')
    hf.parent.mkdir(parents=True, exist_ok=True)
    hd = json.loads(hf.read_text()) if hf.exists() else {}
    hd[f'auth:{parent_dom}'] = f'cookie:SESSION:{sid}'
    hf.write_text(json.dumps(hd, indent=2))

def login_browser(url, headless=False):
    """Launch browser, wait for human login, capture cookies."""
    from playwright.sync_api import sync_playwright

    dom, parent_dom, sid, sf = session_paths(url)

    with sync_playwright() as pw:
        launch_opts = {'headless': headless}
        # Xvfb support: if no display but Xvfb is running
        if headless:
            launch_opts['args'] = ['--no-sandbox', '--disable-setuid-sandbox']

        br = pw.chromium.launch(**launch_opts)
        ctx = br.new_context(
            user_agent='Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36',
            viewport={'width': 1280, 'height': 720},
        )

        # Load existing cookies if any
        if sf.exists():
            try:
                prev = json.loads(sf.read_text())
                if prev.get('cookies'):
                    ctx.add_cookies(prev['cookies'])
            except:
                pass

        pg = ctx.new_page()

        # Navigate with timeout
        try:
            pg.goto(url, timeout=30000, wait_until='domcontentloaded')
        except Exception as e:
            br.close()
            return {'error': True, 'message': f'Cannot reach {url}: {str(e)[:100]}',
                    'fix': 'Check network connection. Ensure the URL is correct.'}

        initial_url = pg.url
        initial_cookies = len(ctx.cookies())

        if not headless:
            print(f'Browser opened: {url}', file=sys.stderr)
            print('Log in manually. The browser will close when login is detected.', file=sys.stderr)

        # Wait for login detection (multiple heuristics)
        deadline = time.time() + 300
        detected = False

        while time.time() < deadline:
            time.sleep(2)
            current_url = pg.url
            current_cookies = ctx.cookies()

            # Heuristic 1: URL changed significantly (not just fragment)
            url_changed = urlparse(current_url).path != urlparse(initial_url).path

            # Heuristic 2: Cookies increased
            cookies_grew = len(current_cookies) > initial_cookies

            # Heuristic 3: Login-related elements disappeared
            try:
                has_password = pg.query_selector('input[type="password"]') is not None
            except:
                has_password = False

            # Auth detected if: URL changed OR cookies grew OR password field gone
            if url_changed or cookies_grew or (not has_password and initial_cookies == 0):
                time.sleep(3)  # Wait for final redirects
                detected = True
                break

        # Capture final cookies
        cookies = ctx.cookies()
        br.close()

        if not detected and len(cookies) <= initial_cookies:
            return {'error': True, 'message': 'Login not detected within 5 minutes.',
                    'fix': 'Try again. Ensure you completed the login process.'}

        # Save cookies
        sf.write_text(json.dumps({
            'cookies': cookies,
            'at': time.strftime('%Y-%m-%d %H:%M'),
            'url': url,
            'domain': dom,
        }))

        # Save auth mapping
        save_auth_mapping(parent_dom, sid)

        return {
            'login': True,
            'domain': dom,
            'cookies': len(cookies),
            'session': sid,
            'file': str(sf),
            'detected_by': 'url_change' if url_changed else ('cookies_grew' if cookies_grew else 'password_gone'),
        }

def login_manual(url):
    """Manual cookie import for environments without browser."""
    dom, parent_dom, sid, sf = session_paths(url)

    print(f'\nNo browser available for {url}', file=sys.stderr)
    print('Manual cookie import:', file=sys.stderr)
    print(f'  1. Open {url} in YOUR browser', file=sys.stderr)
    print(f'  2. Log in normally', file=sys.stderr)
    print(f'  3. Open DevTools → Application → Cookies', file=sys.stderr)
    print(f'  4. Copy all cookies as JSON (or use EditThisCookie extension)', file=sys.stderr)
    print(f'  5. Paste the JSON below, then press Enter twice:', file=sys.stderr)
    print(file=sys.stderr)

    lines = []
    try:
        while True:
            line = input()
            if not line and lines:
                break
            lines.append(line)
    except EOFError:
        pass

    raw = '\n'.join(lines)
    if not raw.strip():
        return {'error': True, 'message': 'No cookie data provided.',
                'fix': 'Run again and paste your cookies.'}

    try:
        cookies = json.loads(raw)
        if isinstance(cookies, dict):
            cookies = [{'name': k, 'value': str(v), 'domain': f'.{parent_dom}', 'path': '/'}
                       for k, v in cookies.items()]
        elif isinstance(cookies, list):
            pass
        else:
            return {'error': True, 'message': 'Expected JSON object or array.'}
    except json.JSONDecodeError as e:
        return {'error': True, 'message': f'Invalid JSON: {e}'}

    sf.write_text(json.dumps({
        'cookies': cookies,
        'at': time.strftime('%Y-%m-%d %H:%M'),
        'url': url,
        'domain': dom,
        'method': 'manual',
    }))
    save_auth_mapping(parent_dom, sid)

    return {'login': True, 'domain': dom, 'cookies': len(cookies),
            'session': sid, 'method': 'manual'}


def main():
    if len(sys.argv) < 2:
        print(json.dumps({'error': True, 'message': 'Usage: csos-login.py <url> [--headless|--manual]'}))
        sys.exit(1)

    url = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else 'auto'

    # Auto-detect best mode
    if mode == 'auto':
        if not detect_playwright():
            mode = '--manual'
        elif not detect_display():
            mode = '--manual'
        else:
            mode = '--headed'

    if mode == '--manual':
        result = login_manual(url)
    elif mode == '--headless':
        if detect_playwright():
            result = login_browser(url, headless=True)
        else:
            result = {'error': True, 'message': 'Playwright not installed.',
                      'fix': 'pip install playwright && playwright install chromium'}
    else:  # --headed
        if detect_playwright() and detect_display():
            result = login_browser(url, headless=False)
        elif detect_playwright():
            # Has Playwright but no display — try Xvfb or fall back to manual
            try:
                os.environ['DISPLAY'] = ':99'
                result = login_browser(url, headless=False)
            except:
                result = login_manual(url)
        else:
            result = login_manual(url)

    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
