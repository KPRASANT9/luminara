#!/usr/bin/env python3
"""
CSOS Daemon — The Chloroplast. Operate only. Never build.

ONE process. ONE Core(). ALL capabilities. Auto-absorption on every operation.
STRUCTURAL ENFORCEMENT: exec rejects commands that create substrate code files.

NATIVE DELEGATION: Physics operations (fly, grow, see, diffuse, lint, absorb, batch)
are delegated to the native binary (csos-native) when available. Falls back to Python.
"""
import sys, json, hashlib, re, os, subprocess, time, shutil, threading, queue
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
sys.path.insert(0, '.')

from src.core.core import Core
from pathlib import Path

# ═══ NATIVE DELEGATION ═══
# Try unified membrane binary first, then legacy, then Python fallback
_native_bin = (shutil.which('csos') or str(Path('.') / 'csos')
               if Path(shutil.which('csos') or str(Path('.') / 'csos')).is_file()
               else shutil.which('csos-native') or str(Path('.') / 'csos-native'))
_has_native = Path(_native_bin).is_file() and os.access(_native_bin, os.X_OK)

def _try_native(req):
    """Delegate to native binary for physics operations. Returns None on failure."""
    if not _has_native:
        return None
    try:
        r = subprocess.run([_native_bin], input=json.dumps(req) + '\n',
                          capture_output=True, text=True, timeout=10)
        if r.returncode == 0 and r.stdout.strip():
            lines = r.stdout.strip().split('\n')
            for line in reversed(lines):
                if line.startswith('{'):
                    return json.loads(line)
        return None
    except Exception:
        return None

# ═══ LAW ENFORCEMENT: file patterns that CANNOT be created ═══
FORBIDDEN_EXTENSIONS = {'.py', '.js', '.ts', '.jsx', '.tsx', '.mjs', '.cjs'}
FORBIDDEN_PATTERNS = [
    r'>\s*\S+\.(py|js|ts|jsx|tsx)\b',          # redirect to .py/.js/.ts
    r'cat\s*>\s*\S+\.(py|js|ts)',               # cat > file.py
    r'tee\s+\S+\.(py|js|ts)',                   # tee file.py
    r'echo\s.*>\s*\S+\.(py|js|ts)',             # echo > file.py
    r'cp\s.*\s+\S+\.(py|js|ts)\b',             # cp x file.py
    r'mv\s.*\s+\S+\.(py|js|ts)\b',             # mv x file.py
    r'touch\s+\S+\.(py|js|ts)\b',              # touch file.py
    r'python3?\s+-c\s+.*open\s*\(',             # python -c "open('file','w')"
    r'curl\s.*-o\s*\S+\.(py|js|ts)',            # curl -o file.py
    r'wget\s.*-O\s*\S+\.(py|js|ts)',            # wget -O file.py
]
ALLOWED_WRITE_EXTENSIONS = {'.md', '.csv', '.txt', '.json', '.docx', '.pdf', '.xlsx', '.html', '.xml', '.yaml', '.yml', '.env', '.sh', '.log'}

def is_build_command(cmd):
    """Returns (True, reason) if command attempts to create substrate code."""
    cmd_lower = cmd.lower()
    for pattern in FORBIDDEN_PATTERNS:
        m = re.search(pattern, cmd_lower)
        if m:
            return True, f"Command creates code file (matched: {m.group()[:40]})"
    return False, ""

# ═══ CORE: loaded ONCE ═══
core = Core()
for r in ['eco_domain', 'eco_cockpit', 'eco_organism']:
    if r not in core.rings_list():
        core.grow(r)

# ═══ WEB SESSION ═══
try:
    import requests as _req
    web = _req.Session()
    web.headers['User-Agent'] = 'CSOS/1.0'
    HAS_WEB = True
except ImportError:
    web = None
    HAS_WEB = False

# ═══ MEMBRANE (with motor context coupling + chain tracking) ═══
_observed_substrates = []  # Track substrate names for hash→name resolution

def absorb(substrate, raw):
    nums = [float(x) for x in re.findall(r'[-+]?\d*\.?\d+', str(raw)) if len(x) < 15][:20]
    h = 1000 + (int(hashlib.md5(substrate.encode()).hexdigest()[:8], 16) % 9000)
    g0 = core.ring('eco_domain').gradient
    rd = core.fly('eco_domain', [float(h)] + nums)
    d = core.ring('eco_domain')
    core.fly('eco_cockpit', [float(d.gradient), float(d.speed), float(d.F)])
    k = core.ring('eco_cockpit')
    o = core.ring('eco_organism')
    core.fly('eco_organism', [float(d.gradient), float(k.gradient), float(o.gradient)])
    o = core.ring('eco_organism')
    rw = o.atoms[0].resonance_width if o.atoms else 0.9
    if o.cycles > 0 and o.cycles % 10 == 0:
        core.diffuse('eco_organism', 'eco_domain')

    delta = core.ring('eco_domain').gradient - g0

    # ── Motor Context: the synapse between physics and LLM ──
    # Track observed substrates for name resolution
    if substrate not in _observed_substrates:
        _observed_substrates.append(substrate)
    # Keep bounded
    if len(_observed_substrates) > 50:
        _observed_substrates[:] = _observed_substrates[-50:]

    # Motor context reads from eco_domain (where substrate signals live)
    # not eco_organism (which only sees aggregate gradient/speed numbers)
    motor = d.motor_context(_observed_substrates)

    # ── Tool-Chain Calvin: learn successful action sequences ──
    chain_result = core.chain_absorb('eco_domain', h, delta)

    result = {
        'substrate': substrate, 'signals': len(nums),
        'resonated': rd.get('produced', 0), 'calvin': rd.get('synthesized', 0),
        'delta': delta,
        'domain': {'grad': d.gradient, 'speed': round(d.speed, 3), 'F': round(d.F, 4)},
        'cockpit': {'grad': k.gradient, 'speed': round(k.speed, 3)},
        'organism': {'grad': o.gradient, 'speed': round(o.speed, 3), 'rw': round(rw, 3)},
        'decision': motor['decision'],
        # ── NEW: Motor Context for LLM prompt coupling ──
        'motor': {
            'observe_next': motor['observe_next'],
            'confident_in': motor['confident_in'],
            'coverage': motor['coverage'],
            'calvin_patterns': motor['calvin_patterns'],
            'chain': chain_result,
        },
    }
    # SSE: push ring state to canvas on every absorb
    sse_broadcast('absorb', result)
    return result

# ═══ COOKIES ═══
def load_cookies(sid):
    sf = Path(f'.csos/sessions/{sid}.json')
    if sf.exists() and web:
        try:
            d = json.loads(sf.read_text())
            ck = d.get('cookies', d)
            if isinstance(ck, list):
                for c in ck:
                    if isinstance(c, dict) and 'name' in c:
                        web.cookies.set(c['name'], c['value'], domain=c.get('domain', ''), path=c.get('path', '/'))
            elif isinstance(ck, dict):
                for n, v in ck.items():
                    if n not in ('at', 'captured_at'): web.cookies.set(n, str(v))
        except: pass

def save_cookies(sid):
    sf = Path(f'.csos/sessions/{sid}.json')
    sf.parent.mkdir(parents=True, exist_ok=True)
    if web: sf.write_text(json.dumps({'cookies': dict(web.cookies)}))

def resolve_auth(url):
    from urllib.parse import urlparse
    dom = urlparse(url).hostname or ''
    hf = Path('.csos/sessions/human.json')
    if not hf.exists(): return 'none', '', ''
    hd = json.loads(hf.read_text())
    pts = dom.split('.')
    for ak in [f'auth:{dom}'] + [f"auth:{'.'.join(pts[i:])}" for i in range(1, len(pts) - 1)]:
        if ak in hd:
            p = hd[ak].split(':')
            method = p[0]
            if method == 'cookie' and len(p) > 1 and p[1] == 'SESSION':
                load_cookies(p[2] if len(p) > 2 else dom.replace('.', '_'))
                return 'none', '', ''
            return method, p[1] if len(p) > 1 else '', ':'.join(p[2:]) if len(p) > 2 else ''
    return 'none', '', ''

# ═══ HANDLER ═══
def handle(req):
    action = req.get('action', '')

    # ── Native delegation for physics operations ──
    if action in ('fly', 'grow', 'see', 'diffuse', 'lint', 'absorb', 'batch', 'ping'):
        native_result = _try_native(req)
        if native_result is not None:
            return native_result

    # ── Physics (Python fallback) ──
    if action == 'absorb':
        return absorb(req.get('substrate', 'unknown'), req.get('output', ''))

    elif action == 'batch':
        results = [absorb(it.get('substrate', 'x'), it.get('output', '')) for it in req.get('items', [])]
        o = core.ring('eco_organism')
        rw = o.atoms[0].resonance_width if o.atoms else 0.9
        return {'batch': True, 'items': len(results),
                'results': [{'substrate': r['substrate'], 'delta': r['delta']} for r in results],
                'decision': 'EXECUTE' if o.speed > rw else 'EXPLORE'}

    elif action == 'see':
        return core.see(req.get('ring'), req.get('detail', 'minimal'))

    elif action == 'fly':
        s = req.get('signals', [50])
        if isinstance(s, str): s = [float(x) for x in s.split(',')]
        return core.fly(req.get('ring', 'eco_domain'), s)

    elif action == 'grow':
        return core.grow(req.get('ring', 'new'))

    elif action == 'diffuse':
        return core.diffuse(req.get('source', ''), req.get('target', ''))

    elif action == 'lint':
        return core.lint(req.get('ring'))

    elif action == 'hash':
        s = req.get('substrate', 'x')
        return {'substrate': s, 'hash': 1000 + (int(hashlib.md5(s.encode()).hexdigest()[:8], 16) % 9000)}

    # ── Bash: OPERATE ONLY (auto-absorbed) ──
    elif action == 'exec':
        cmd = req.get('command', 'echo ok')
        sub = req.get('substrate', 'bash')

        # ── LAW ENFORCEMENT: reject build commands ──
        is_build, reason = is_build_command(cmd)
        if is_build:
            return {
                'error': True,
                'law_violation': 'I',
                'message': f'BLOCKED: {reason}',
                'command': cmd[:100],
                'guidance': 'This system operates on substrates. It does not create code files. Use exec for CLI commands that READ or INTERACT, not for writing .py/.js/.ts files.',
            }

        try:
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
            data = r.stdout.strip()
            err = r.stderr.strip()
            physics = absorb(sub, data + ' ' + err)
            return {'data': data[:5000], 'exit': r.returncode,
                    'error': err[:500] if err else None, 'physics': physics}
        except Exception as e:
            return {'error': True, 'message': str(e)[:300]}

    # ── Web: OPERATE ONLY (auto-absorbed) ──
    elif action == 'web':
        if not HAS_WEB:
            return {'error': True, 'message': 'requests not installed', 'fix': 'pip install requests'}
        url = req.get('url', '')
        sub = req.get('substrate', url.split('/')[2] if '/' in url else 'web')
        sid = re.sub(r'[^a-zA-Z0-9]', '_', sub)[:40]
        steps = req.get('steps', [])

        load_cookies(sid)
        method, env_var, auth_url = resolve_auth(url)
        if env_var and method != 'none':
            creds = json.loads(os.environ.get(env_var, '{}'))
            if method == 'form' and auth_url: web.post(auth_url, data=creds)
            elif method == 'bearer': web.headers['Authorization'] = f"Bearer {creds.get('token', '')}"
            elif method == 'basic': web.auth = (creds.get('user', ''), creds.get('pass', ''))

        r = web.get(url)
        pt = r.text
        result = {'status': r.status_code, 'url': r.url, 'authenticated': len(web.cookies) > 0}

        if not steps:
            result['discovery'] = {
                'title': (re.findall(r'<title>(.*?)</title>', pt, re.I) or [''])[0][:100],
                'links': len(re.findall(r'href=["\']([^"\']+)', pt)),
                'forms': len(re.findall(r'<form', pt, re.I)),
                'tables': len(re.findall(r'<table', pt, re.I)),
                'inputs': re.findall(r'name=["\']([^"\']+)', pt)[:20],
                'text_length': len(re.sub(r'<[^>]+>', '', pt)),
            }
        else:
            from urllib.parse import urljoin
            result['extracted'] = {}; result['errors'] = []; fd = {}
            for step in steps:
                act = step.get('action', '')
                try:
                    if act == 'navigate':
                        tgt = step['url']
                        if not tgt.startswith('http'): tgt = urljoin(url, tgt)
                        r = web.get(tgt); pt = r.text; fd = {}
                    elif act == 'type':
                        nm = re.search(r'name=["\'](\w+)', step.get('selector', ''))
                        if nm: fd[nm.group(1)] = step.get('text', '')
                    elif act == 'select':
                        nm = re.search(r'name=["\'](\w+)', step.get('selector', ''))
                        if nm: fd[nm.group(1)] = step.get('value', '')
                    elif act == 'check':
                        nm = re.search(r'name=["\'](\w+)', step.get('selector', ''))
                        if nm: fd[nm.group(1)] = 'on'
                    elif act in ('click', 'submit'):
                        tgt = step.get('url', url)
                        if not tgt.startswith('http'): tgt = urljoin(url, tgt)
                        if fd: r = web.post(tgt, data=fd); fd = {}
                        else: r = web.get(tgt)
                        pt = r.text
                    elif act == 'extract':
                        sel = step.get('selector', '')
                        fld = step.get('field', f"f{len(result['extracted'])}")
                        if sel.startswith('regex:'): result['extracted'][fld] = re.findall(sel[6:], pt)[:20]
                        else: result['extracted'][fld] = [re.sub(r'<[^>]+>', '', m).strip()[:500] for m in re.findall(f'<{sel}[^>]*>(.*?)</{sel}>', pt, re.DOTALL)[:20]]
                    elif act == 'wait': time.sleep(step.get('seconds', 1))
                except Exception as e:
                    result['errors'].append(f"{act}:{str(e)[:80]}")

        save_cookies(sid)
        result['physics'] = absorb(sub, json.dumps(result))
        return result

    # ── Human data ──
    elif action == 'remember':
        hf = Path('.csos/sessions/human.json')
        hf.parent.mkdir(parents=True, exist_ok=True)
        d = json.loads(hf.read_text()) if hf.exists() else {}
        d[req.get('key', '')] = req.get('value', '')
        hf.write_text(json.dumps(d, indent=2))
        hh = 1000 + (int(hashlib.md5(b'human_profile').hexdigest()[:8], 16) % 9000)
        fh = int(hashlib.md5(req.get('key', '').encode()).hexdigest()[:4], 16)
        core.fly('eco_cockpit', [float(hh), float(fh), float(len(req.get('value', '')))])
        return {'remembered': req.get('key'), 'fields': len(d)}

    elif action == 'recall':
        hf = Path('.csos/sessions/human.json')
        d = json.loads(hf.read_text()) if hf.exists() else {}
        return {'fields': d, 'count': len(d)}

    # ── Diagnostics ──
    elif action == 'diagnose':
        issues = []
        rh = {}
        for name in ['eco_domain', 'eco_cockpit', 'eco_organism']:
            r = core.ring(name)
            if r:
                rh[name] = {'grad': r.gradient, 'speed': round(r.speed, 3),
                            'F': round(r.F, 4), 'atoms': len(r.atoms), 'cycles': r.cycles}
                if r.F > 100: issues.append(f'{name}: F={r.F:.0f}')
                if r.cycles > 20 and r.gradient == 0: issues.append(f'{name}: zero gradient')
            else:
                core.grow(name); issues.append(f'created {name}')
        # Check for Law I violations (code files outside src/core/)
        for p in Path('.').rglob('*.py'):
            pstr = str(p)
            if pstr.startswith('src/') or pstr.startswith('scripts/') or pstr.startswith('tests/'): continue
            if '__init__' in pstr: continue
            if pstr.startswith('.') or 'node_modules' in pstr or '__pycache__' in pstr: continue
            issues.append(f'Law I violation: {pstr} (code file outside allowed directories)')
        return {'status': 'healthy' if not issues else 'degraded',
                'issues': issues, 'rings': rh, 'web': HAS_WEB}

    elif action == 'ping':
        o = core.ring('eco_organism')
        return {'alive': True, 'rings': len(core.rings_list()),
                'speed': round(o.speed, 3) if o else 0}

    else:
        return {'error': f'unknown: {action}'}

# ═══ SSE: canvas is a WINDOW into daemon, not a house next to it ═══
_sse_clients = []
_sse_lock = threading.Lock()

def sse_broadcast(event_type, data):
    """Fire SSE event to all connected canvas clients."""
    msg = f"event: {event_type}\ndata: {json.dumps(data, default=str)}\n\n".encode()
    dead = []
    with _sse_lock:
        for q in _sse_clients:
            try: q.put_nowait(msg)
            except queue.Full: dead.append(q)
        for q in dead: _sse_clients.remove(q)

class _CanvasHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        p = self.path.split('?')[0]
        if p == '/events':
            # SSE stream — canvas connects here via EventSource
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            q = queue.Queue(maxsize=100)
            with _sse_lock: _sse_clients.append(q)
            try:
                # Send initial full state on connect
                state = _full_state()
                self.wfile.write(f"event: state\ndata: {json.dumps(state, default=str)}\n\n".encode()); self.wfile.flush()
                while True:
                    try:
                        msg = q.get(timeout=25)
                        self.wfile.write(msg); self.wfile.flush()
                    except queue.Empty:
                        self.wfile.write(b": keepalive\n\n"); self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError): pass
            finally:
                with _sse_lock:
                    if q in _sse_clients: _sse_clients.remove(q)
        elif p == '/api/state':
            # Full daemon state — same as what SSE pushes on connect
            self._json(_full_state())
        elif p == '/' or p == '/index.html':
            hp = Path('.canvas-tui/index.html')
            if hp.exists():
                self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Cache-Control', 'no-cache'); self.end_headers()
                self.wfile.write(hp.read_bytes())
            else:
                self.send_response(200); self.send_header('Content-Type', 'text/plain'); self.end_headers()
                self.wfile.write(b'Canvas HTML not found at .canvas-tui/index.html')
        else:
            self.send_response(404); self.end_headers()

    def do_POST(self):
        p = self.path.split('?')[0]
        if p == '/api/command':
            # Bidirectional: canvas → daemon → physics → SSE → canvas
            cl = int(self.headers.get('Content-Length', 0))
            body = json.loads(self.rfile.read(cl)) if cl > 0 else {}
            result = handle(body)
            sse_broadcast('response', result)
            self._json(result)
        else:
            self._json({'error': 'unknown endpoint'}, 404)

    def do_HEAD(self):
        # Browsers send HEAD for favicon, preflight — just return 200
        self.send_response(200); self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def _json(self, data, status=200):
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data, default=str).encode())

    def log_message(self, *a): pass

def _full_state():
    """Full daemon state snapshot — sent on SSE connect and via GET /api/state."""
    rings = {}
    for name in core.rings_list():
        r = core.ring(name)
        if r:
            rw = r.atoms[0].resonance_width if r.atoms else 0.9
            rings[name] = {
                'gradient': r.gradient, 'speed': round(r.speed, 3),
                'F': round(r.F, 4), 'rw': round(rw, 3),
                'cycles': r.cycles, 'atoms': len(r.atoms),
                'decision': 'EXECUTE' if r.speed > rw else 'EXPLORE',
            }
    # Calvin atoms from eco_domain
    calvins = []
    d = core.ring('eco_domain')
    if d:
        for a in d.atoms:
            if hasattr(a, 'name') and a.name.startswith('calvin'):
                calvins.append({'name': a.name, 'formula': getattr(a, 'formula', '')})
    # Human skills
    skills = {}
    hf = Path('.csos/sessions/human.json')
    if hf.exists():
        try:
            hd = json.loads(hf.read_text())
            skills = {k: v for k, v in hd.items() if k.startswith('skill:')}
        except: pass
    # Specs
    specs = []
    if Path('specs').is_dir():
        specs = [f for f in os.listdir('specs') if f.endswith('.csos')]
    return {
        'rings': rings, 'calvins': calvins, 'skills': skills,
        'specs': specs, 'clients': len(_sse_clients),
    }

class _ThreadedHTTP(ThreadingMixIn, HTTPServer):
    daemon_threads = True
    # Allow IPv6 + IPv4 dual-stack so browsers don't hang on [::1]
    import socket as _sock
    address_family = _sock.AF_INET6
    allow_reuse_address = True

def _start_sse_server(port=4200):
    import socket
    try:
        server = _ThreadedHTTP(('::', port), _CanvasHandler)
    except (OSError, socket.error):
        # Fallback to IPv4 if IPv6 not available
        _ThreadedHTTP.address_family = socket.AF_INET
        server = _ThreadedHTTP(('0.0.0.0', port), _CanvasHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return port

# ═══ MAIN ═══
if __name__ == '__main__':
    sse_port = None
    if '--sse' in sys.argv or os.environ.get('CSOS_SSE'):
        sse_port = int(os.environ.get('CSOS_SSE_PORT', '4200'))
        _start_sse_server(sse_port)
    print(json.dumps({'daemon': True, 'rings': len(core.rings_list()), 'web': HAS_WEB,
                       'sse': sse_port, 'ready': True}), flush=True)
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line: continue
            try:
                result = handle(json.loads(line))
                print(json.dumps(result, default=str), flush=True)
                # SSE: broadcast every response to connected canvas clients
                if sse_port and result:
                    sse_broadcast('response', result)
            except Exception as e:
                print(json.dumps({'error': str(e)[:300]}), flush=True)
    except (EOFError, KeyboardInterrupt):
        pass
    # If SSE is active, keep process alive for canvas clients
    if sse_port:
        try:
            threading.Event().wait()
        except KeyboardInterrupt:
            pass
