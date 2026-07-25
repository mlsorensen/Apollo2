// Apollo 2 shot-history web app. Served by the device itself (one embedded,
// gzipped file) and styled from the DEVICE'S ACTIVE THEME: /api/summary
// carries the palette the screen is currently using, and the MUI theme is
// built from it, so the page always matches the device.
import React, { useEffect, useMemo, useState } from 'react';
import {
  AppBar, Box, Card, CardContent, Chip, CircularProgress, Container,
  CssBaseline, Dialog, DialogContent, DialogTitle, IconButton, Link, Stack,
  Table, TableBody, TableCell, TableHead, TableRow, ThemeProvider, Toolbar,
  Typography, createTheme,
} from '@mui/material';

const hex = (v) => '#' + (v >>> 0).toString(16).padStart(6, '0');

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
                'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function fmtWhen(unix) {
  const d = new Date(unix * 1000);
  return d.toLocaleString([], {
    month: 'numeric', day: 'numeric', year: 'numeric',
    hour: 'numeric', minute: '2-digit',
  });
}

function monthKey(unix) {
  const d = new Date(unix * 1000);
  return d.getFullYear() * 100 + d.getMonth() + 1;
}

function monthLabel(ym) {
  return `${MONTHS[(ym % 100) - 1]} ${Math.floor(ym / 100)}`;
}

function shotFileBase(s) {
  const d = new Date(s.unix * 1000);
  const p = (v) => String(v).padStart(2, '0');
  return `shot-${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-` +
         `${p(d.getHours())}${p(d.getMinutes())}`;
}

function fmtBytes(b) {
  if (b >= 1e9) return (b / 1e9).toFixed(1) + ' GB';
  return Math.round(b / 1e6) + ' MB';
}

// The shot graph, drawn live in the device's current palette (stored PNGs
// keep whatever theme was active when they were captured; this doesn't).
// Same design as the on-device card: one plot, weight on the left axis,
// flow on the right, quarter gridlines with exact tick values.
function ShotChart({ samples, target, theme }) {
  const W = 640, H = 300, L = 44, R = 48, T = 26, B = 22;
  const pw = W - L - R, ph = H - T - B;
  if (!samples || samples.length < 2) return null;
  const tEnd = samples[samples.length - 1].t;
  const nice = (v) => {
    const steps = [1, 2, 3, 4, 5, 6, 8, 10, 15, 20, 30, 40, 50, 60, 80, 100];
    for (const s of steps) if (v <= s) return s;
    return Math.ceil(v / 50) * 50;
  };
  const wmax = nice(Math.max(...samples.map((s) => s.w), target || 0) * 1.05);
  const fmax = nice(Math.max(...samples.map((s) => s.f)) * 1.05);
  const x = (t) => L + (t / tEnd) * pw;
  const yw = (w) => T + ph - (Math.max(w, 0) / wmax) * ph;
  const yf = (f) => T + ph - (Math.max(f, 0) / fmax) * ph;
  const path = (fn, key) =>
    samples.map((s, i) => `${i ? 'L' : 'M'}${x(s.t).toFixed(1)},${fn(s[key]).toFixed(1)}`).join('');
  const tick = (v) => (Number.isInteger(v) ? v : +v.toFixed(2));
  const grid = [0, 1, 2, 3, 4];
  return (
    <svg viewBox={`0 0 ${W} ${H}`} style={{ width: '100%', height: 'auto' }}>
      {grid.map((i) => (
        <line key={i} x1={L} x2={W - R} y1={T + (ph * i) / 4} y2={T + (ph * i) / 4}
              stroke={theme.rail} strokeWidth="1" />
      ))}
      {grid.map((i) => (
        <text key={'l' + i} x={L - 6} y={T + (ph * i) / 4 + 4} textAnchor="end"
              fontSize="11" fill={theme.muted}>{tick(wmax * (4 - i) / 4)}</text>
      ))}
      {grid.map((i) => (
        <text key={'r' + i} x={W - R + 6} y={T + (ph * i) / 4 + 4} textAnchor="start"
              fontSize="11" fill={theme.muted}>{tick(fmax * (4 - i) / 4)}</text>
      ))}
      <path d={path(yf, 'f') + `L${x(tEnd)},${T + ph}L${L},${T + ph}Z`}
            fill={theme.ok} opacity="0.1" />
      <path d={path(yw, 'w') + `L${x(tEnd)},${T + ph}L${L},${T + ph}Z`}
            fill={theme.accent} opacity="0.1" />
      {target > 0 && target <= wmax && (
        <line x1={L} x2={W - R} y1={yw(target)} y2={yw(target)}
              stroke={theme.warn} strokeWidth="2" strokeDasharray="6 4" />
      )}
      <path d={path(yf, 'f')} fill="none" stroke={theme.ok} strokeWidth="2" />
      <path d={path(yw, 'w')} fill="none" stroke={theme.accent} strokeWidth="2.5" />
      <circle cx={x(tEnd)} cy={yf(samples[samples.length - 1].f)} r="4"
              fill={theme.ok} stroke={theme.bg} strokeWidth="2" />
      <circle cx={x(tEnd)} cy={yw(samples[samples.length - 1].w)} r="4"
              fill={theme.accent} stroke={theme.bg} strokeWidth="2" />
      <text x={L} y={14} fontSize="11" fill={theme.muted}>
        <tspan fill={theme.accent}>■</tspan> Weight g
      </text>
      <text x={W - R} y={14} fontSize="11" fill={theme.muted} textAnchor="end">
        Flow g/s <tspan fill={theme.ok}>■</tspan>
      </text>
      <text x={L} y={H - 4} fontSize="11" fill={theme.muted}>0 s</text>
      <text x={W - R} y={H - 4} fontSize="11" fill={theme.muted} textAnchor="end">
        {(tEnd / 1000).toFixed(0)} s
      </text>
    </svg>
  );
}

function StatCard({ value, caption }) {
  return (
    <Card sx={{ flex: 1, textAlign: 'center', minWidth: 0 }}>
      <CardContent sx={{ py: 1.5, px: 0.5, '&:last-child': { pb: 1.5 } }}>
        <Typography noWrap sx={{ fontWeight: 500,
                                 fontSize: 'clamp(1.15rem, 5.5vw, 2.125rem)' }}>
          {value}
        </Typography>
        <Typography variant="body2" color="text.secondary"
                    sx={{ lineHeight: 1.2, fontSize: { xs: '0.72rem', sm: '0.875rem' } }}>
          {caption}
        </Typography>
      </CardContent>
    </Card>
  );
}

export default function App() {
  const [summary, setSummary] = useState(null);
  const [shots, setShots] = useState(null);
  const [error, setError] = useState(null);
  const [filter, setFilter] = useState(0); // 0 = all, else year*100+month
  const [open, setOpen] = useState(null); // shot summary object
  const [samples, setSamples] = useState(null);

  useEffect(() => {
    Promise.all([
      fetch('/api/summary').then((r) => r.json()),
      fetch('/api/shots').then((r) => r.json()),
    ])
      .then(([s, sh]) => { setSummary(s); setShots(sh.shots); })
      .catch(() => setError('Could not reach the device.'));
  }, []);

  useEffect(() => {
    if (!open) { setSamples(null); return; }
    fetch(`/api/shot.csv?id=${open.id}`)
      .then((r) => r.text())
      .then((text) => {
        const rows = text.trim().split('\n').slice(1).map((l) => {
          const [t, w, f] = l.split(',').map(Number);
          return { t, w, f };
        });
        setSamples(rows);
      })
      .catch(() => setSamples([]));
  }, [open]);

  const devTheme = summary?.theme;
  const muiTheme = useMemo(() => {
    const t = devTheme || {};
    const c = (k, fb) => (t[k] != null ? hex(t[k]) : fb);
    return createTheme({
      palette: {
        mode: 'dark',
        primary: { main: c('accent', '#2f95dd') },
        success: { main: c('ok', '#4caf50') },
        warning: { main: c('warn', '#ffb300') },
        error: { main: c('alert', '#ef5350') },
        background: { default: c('bg', '#0e1116'), paper: c('card', '#161b22') },
        text: { primary: c('text', '#eceff4'), secondary: c('muted', '#8b949e') },
      },
      shape: { borderRadius: 10 },
    });
  }, [devTheme]);

  const chart = {
    bg: devTheme ? hex(devTheme.bg) : '#0e1116',
    rail: devTheme ? hex(devTheme.rail) : '#22282f',
    accent: devTheme ? hex(devTheme.accent) : '#2f95dd',
    ok: devTheme ? hex(devTheme.ok) : '#4caf50',
    warn: devTheme ? hex(devTheme.warn) : '#ffb300',
    muted: devTheme ? hex(devTheme.muted) : '#8b949e',
  };

  const months = useMemo(() => {
    if (!shots) return [];
    const seen = [];
    for (const s of shots) {
      const ym = monthKey(s.unix);
      if (!seen.includes(ym)) seen.push(ym);
    }
    return seen;
  }, [shots]);

  const visible = useMemo(() => {
    if (!shots) return [];
    return filter ? shots.filter((s) => monthKey(s.unix) === filter) : shots;
  }, [shots, filter]);

  const diffOf = (s) => (s.target_g > 0 ? s.final_g - s.target_g : null);

  return (
    <ThemeProvider theme={muiTheme}>
      <CssBaseline />
      <AppBar position="static" color="transparent" elevation={0}
              sx={{ borderBottom: 1, borderColor: 'divider' }}>
        <Toolbar sx={{ gap: 1 }}>
          <Typography noWrap variant="h6" sx={{ flexGrow: 1, minWidth: 0 }}>
            {summary?.name || 'Apollo 2'}
            <Box component="span" sx={{ display: { xs: 'none', sm: 'inline' } }}>
              {' — Shot History'}
            </Box>
          </Typography>
          {summary?.storage?.total > 0 && (
            <Typography noWrap variant="body2" sx={{ flexShrink: 0,
                          fontSize: { xs: '0.72rem', sm: '0.875rem' } }}
                        color={summary.storage.full ? 'error' : 'text.secondary'}>
              {summary.storage.full
                ? 'SD FULL — not saving'
                : `SD: ${fmtBytes(summary.storage.free)} free of ${fmtBytes(summary.storage.total)}`}
            </Typography>
          )}
        </Toolbar>
      </AppBar>
      <Container maxWidth="md" sx={{ py: 3 }}>
        {error && <Typography color="error">{error}</Typography>}
        {!error && (!summary || !shots) && (
          <Box sx={{ textAlign: 'center', py: 8 }}><CircularProgress /></Box>
        )}
        {summary && shots && (
          <Stack spacing={2}>
            <Stack direction="row" spacing={2}>
              <StatCard value={summary.stats.total}
                        caption={summary.stats.since > 0
                          ? `Shots since ${fmtWhen(summary.stats.since).split(',')[0]}`
                          : 'Total shots'} />
              <StatCard value={summary.stats.life > 0 ? summary.stats.life.toFixed(1) + '%' : '—'}
                        caption="Lifetime accuracy" />
              <StatCard value={summary.stats.d30 > 0 ? summary.stats.d30.toFixed(1) + '%' : '—'}
                        caption="30-day accuracy" />
            </Stack>
            <Stack direction="row" spacing={1} sx={{ flexWrap: 'wrap', gap: 1 }}>
              <Chip label="All" color={filter === 0 ? 'primary' : 'default'}
                    onClick={() => setFilter(0)} />
              {months.map((ym) => (
                <Chip key={ym} label={monthLabel(ym)}
                      color={filter === ym ? 'primary' : 'default'}
                      onClick={() => setFilter(ym)} />
              ))}
            </Stack>
            <Card>
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell>When</TableCell>
                    <TableCell align="right">Result</TableCell>
                    <TableCell align="right">Diff</TableCell>
                    <TableCell align="right">Time</TableCell>
                    <TableCell align="right">Avg flow</TableCell>
                  </TableRow>
                </TableHead>
                <TableBody>
                  {visible.map((s) => {
                    const d = diffOf(s);
                    return (
                      <TableRow key={s.id} hover sx={{ cursor: 'pointer' }}
                                onClick={() => setOpen(s)}>
                        <TableCell>{fmtWhen(s.unix)}</TableCell>
                        <TableCell align="right">
                          {s.final_g.toFixed(1)}{s.target_g > 0 ? ` / ${s.target_g.toFixed(0)}` : ''} g
                        </TableCell>
                        <TableCell align="right"
                                   sx={{ color: d == null ? 'text.secondary'
                                       : Math.abs(d) <= 2 ? 'success.main' : 'warning.main' }}>
                          {d == null ? '—' : (d >= 0 ? '+' : '') + d.toFixed(1)}
                        </TableCell>
                        <TableCell align="right">{Math.round(s.duration_ms / 1000)} s</TableCell>
                        <TableCell align="right">{s.avg_gps.toFixed(2)} g/s</TableCell>
                      </TableRow>
                    );
                  })}
                  {visible.length === 0 && (
                    <TableRow><TableCell colSpan={5}>
                      <Typography color="text.secondary" align="center" sx={{ py: 2 }}>
                        No shots {filter ? 'in this month' : 'recorded yet'}
                      </Typography>
                    </TableCell></TableRow>
                  )}
                </TableBody>
              </Table>
            </Card>
          </Stack>
        )}
      </Container>
      <Dialog open={!!open} onClose={() => setOpen(null)} maxWidth="md" fullWidth>
        {open && (
          <>
            <DialogTitle sx={{ display: 'flex', justifyContent: 'space-between' }}>
              <span>{fmtWhen(open.unix)}</span>
              <Typography component="span" color="text.secondary">
                {open.wired ? 'Auto shot' : open.mode === 'detect' ? 'Detected' : 'Manual'}
              </Typography>
            </DialogTitle>
            <DialogContent>
              <Stack direction="row" spacing={3} sx={{ mb: 2, flexWrap: 'wrap' }}>
                <Typography><b>{open.final_g.toFixed(1)} g</b> result</Typography>
                {open.target_g > 0 && <Typography><b>{open.target_g.toFixed(0)} g</b> target</Typography>}
                {diffOf(open) != null && (
                  <Typography sx={{ color: Math.abs(diffOf(open)) <= 2 ? 'success.main' : 'warning.main' }}>
                    <b>{(diffOf(open) >= 0 ? '+' : '') + diffOf(open).toFixed(1)} g</b> diff
                  </Typography>
                )}
                <Typography><b>{(open.duration_ms / 1000).toFixed(1)} s</b> time</Typography>
                <Typography><b>{open.avg_gps.toFixed(2)} g/s</b> avg flow</Typography>
              </Stack>
              {samples == null && <Box sx={{ textAlign: 'center', py: 4 }}><CircularProgress size={28} /></Box>}
              {samples != null && <ShotChart samples={samples} target={open.target_g} theme={chart} />}
              <Stack direction="row" spacing={2} sx={{ mt: 1 }}>
                <Link href={`/api/shot.png?id=${open.id}`}
                      download={`${shotFileBase(open)}.png`}>
                  Download card (PNG)
                </Link>
                <Link href={`/api/shot.csv?id=${open.id}`}
                      download={`${shotFileBase(open)}.csv`}>
                  Download data (CSV)
                </Link>
              </Stack>
            </DialogContent>
          </>
        )}
      </Dialog>
    </ThemeProvider>
  );
}
