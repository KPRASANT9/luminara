#!/bin/bash
# CSOS Health Watchdog — runs via cron or systemd timer
# Reads organism ring health → auto-corrects → alerts if critical
#
# Install: crontab -e → */15 * * * * /path/to/watchdog.sh
# Or:      opencode run "@csos-living diagnose and correct"
#
# This is NOT substrate code. This is system infrastructure.
# Like a database health check or a k8s liveness probe.

set -euo pipefail
cd "$(dirname "$0")/.."

LOG=".csos/watchdog.log"
mkdir -p .csos

echo "$(date -Iseconds) WATCHDOG START" >> "$LOG"

# ── Step 1: Run diagnose ──
DIAG=$(python3 -c "
import sys,json,os
from pathlib import Path
sys.path.insert(0,'.')
issues=[];fixes=[];auto_fixed=[]

# Core importable?
try:
    from src.core.core import Core
except Exception as e:
    issues.append('core.py import failed')
    print(json.dumps({'status':'critical','issues':issues,'auto_fixed':[]}))
    sys.exit(0)

# .csos writable?
csos=Path('.csos')
try: csos.mkdir(parents=True,exist_ok=True)
except: issues.append('.csos not writable')

# Rings valid?
for f in (csos/'rings').glob('*.json'):
    try: json.loads(f.read_text())
    except:
        issues.append(f'corrupted: {f.name}')
        f.unlink()
        auto_fixed.append(f'Deleted corrupted {f.name}')

# Ecosystem rings exist?
c=Core()
for r in ['eco_domain','eco_cockpit','eco_organism']:
    if r not in c.rings_list():
        c.grow(r)
        auto_fixed.append(f'Created {r}')

# Ring health
o=c.ring('eco_organism')
d=c.ring('eco_domain')
k=c.ring('eco_cockpit')

if o:
    rw=o.atoms[0].resonance_width if o.atoms else 0.9
    if o.F > 50:
        issues.append(f'organism F={o.F:.0f} (predictions failing)')
        # Auto-correct: diffuse to reset
        c.diffuse('eco_domain','eco_organism')
        auto_fixed.append('Diffused domain→organism to stabilize F')
    if o.cycles > 20 and o.gradient == 0:
        issues.append('organism zero gradient after many cycles')

# Session expiry check
sess=csos/'sessions'
if sess.exists():
    import time
    for f in sess.glob('*.json'):
        if f.name == 'human.json': continue
        try:
            sd=json.loads(f.read_text())
            cap=sd.get('captured_at','')
            # If captured_at exists and is old, flag it
            if cap:
                from datetime import datetime
                try:
                    dt=datetime.strptime(cap,'%Y-%m-%d %H:%M')
                    age_days=(datetime.now()-dt).days
                    if age_days > 25:
                        issues.append(f'Session {f.stem} is {age_days} days old (may expire soon)')
                except: pass
        except: pass

status='healthy' if not issues else 'degraded' if len(issues)<3 else 'critical'
print(json.dumps({
    'status':status,
    'issues':issues,
    'auto_fixed':auto_fixed,
    'organism':{'speed':round(o.speed,3),'F':round(o.F,4),'gradient':o.gradient} if o else {},
    'domain':{'gradient':d.gradient,'atoms':len(d.atoms)} if d else {},
    'cockpit':{'gradient':k.gradient} if k else {},
},default=str))
" 2>&1)

STATUS=$(echo "$DIAG" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('status','unknown'))" 2>/dev/null || echo "error")
ISSUES=$(echo "$DIAG" | python3 -c "import sys,json;d=json.load(sys.stdin);print(len(d.get('issues',[])))" 2>/dev/null || echo "?")
AUTOFIXED=$(echo "$DIAG" | python3 -c "import sys,json;d=json.load(sys.stdin);[print(f'  AUTO-FIXED: {f}') for f in d.get('auto_fixed',[])]" 2>/dev/null)

echo "$(date -Iseconds) STATUS=$STATUS ISSUES=$ISSUES" >> "$LOG"
[ -n "$AUTOFIXED" ] && echo "$AUTOFIXED" >> "$LOG"

# ── Step 2: Course-correct based on status ──
case "$STATUS" in
  healthy)
    echo "$(date -Iseconds) HEALTHY — no action needed" >> "$LOG"
    ;;
  degraded)
    echo "$(date -Iseconds) DEGRADED — running absorb to stabilize" >> "$LOG"
    # Feed a health-check signal through the membrane to keep gradient alive
    python3 -c "
import sys;sys.path.insert(0,'.')
from src.core.core import Core
c=Core()
for r in ['eco_domain','eco_cockpit','eco_organism']:
    if r not in c.rings_list(): c.grow(r)
d=c.ring('eco_domain');c.fly('eco_cockpit',[float(d.gradient),float(d.speed),float(d.F)])
k=c.ring('eco_cockpit');o=c.ring('eco_organism')
c.fly('eco_organism',[float(d.gradient),float(k.gradient),float(o.gradient)])
print(f'Stabilization cycle: organism speed={o.speed:.3f}')
" >> "$LOG" 2>&1
    ;;
  critical)
    echo "$(date -Iseconds) CRITICAL — alerting" >> "$LOG"
    # If SLACK_WEBHOOK is set, send alert
    if [ -n "${SLACK_WEBHOOK:-}" ]; then
      curl -s -X POST "$SLACK_WEBHOOK" \
        -H 'Content-Type: application/json' \
        -d "{\"text\":\"🔴 CSOS CRITICAL: $ISSUES issues detected. Run: csos-core diagnose\"}" \
        >> "$LOG" 2>&1
    fi
    # If headless, write alert file for external monitoring
    echo "$DIAG" > .csos/ALERT.json
    ;;
esac

echo "$(date -Iseconds) WATCHDOG END" >> "$LOG"
