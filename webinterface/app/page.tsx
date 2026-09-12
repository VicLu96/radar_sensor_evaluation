'use client';

import { useCallback, useEffect, useRef, useState } from 'react';
import { Node, bluetoothAvailable } from '../lib/ble';
import {
  MODE,
  OPCODE,
  RANGE,
  RESOLUTIONS,
  modeFor,
  type Config,
  type Energy,
  type Frame,
  type Health,
} from '../lib/protocol';
import FrameCanvas from '../components/FrameCanvas';
import ConfigPanel, { FIELD_NAMES } from '../components/ConfigPanel';
import HealthPanel from '../components/HealthPanel';
import StatsBar, { type Stats } from '../components/StatsBar';

const EMPTY_STATS: Stats = {
  fps: 0,
  kbps: 0,
  framesCompleted: 0,
  framesDropped: 0,
  fragmentsDropped: 0,
  captureMs: 0,
  zonesValid: 0,
  zonesTotal: 0,
  ampValid: 0,
  ampInvalid: 0,
  ambient: 0,
};

export default function Page() {
  /*
   * The newest frame lives in a REF, not in state. At 54x42 a frame is 2,268
   * zones arriving up to ~2.5 times a second; re-rendering the tree on each one
   * would drop frames and measure the renderer instead of the link. The canvas
   * reads this ref on requestAnimationFrame; only the summary numbers below,
   * sampled four times a second, ever reach React state.
   */
  const frameRef = useRef<Frame | null>(null);
  const nodeRef = useRef<Node | null>(null);

  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [config, setConfig] = useState<Config | null>(null);
  const [health, setHealth] = useState<Health | null>(null);
  const [energy, setEnergy] = useState<Energy | null>(null);
  const [stats, setStats] = useState<Stats>(EMPTY_STATS);
  const [log, setLog] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [supported, setSupported] = useState(true);
  const [hasFrames, setHasFrames] = useState(true);

  /* No `navigator` during SSR — this is the only safe place to look. */
  useEffect(() => setSupported(bluetoothAvailable()), []);

  const addLog = useCallback((line: string) => {
    const t = new Date().toLocaleTimeString();
    setLog((l) => [...l.slice(-200), `${t}  ${line}`]);
  }, []);

  /* --- sampled statistics ------------------------------------------------ */
  useEffect(() => {
    let lastFrames = 0;
    let lastBytes = 0;
    let lastAt = performance.now();

    const id = setInterval(() => {
      const node = nodeRef.current;
      if (!node) return;
      const a = node.assembler;
      const now = performance.now();
      const dt = (now - lastAt) / 1000;
      lastAt = now;

      const fps = dt > 0 ? (a.framesCompleted - lastFrames) / dt : 0;
      const kbps = dt > 0 ? (a.bytesReceived - lastBytes) / dt / 1024 : 0;
      lastFrames = a.framesCompleted;
      lastBytes = a.bytesReceived;

      const f = frameRef.current;
      let zonesValid = 0;
      let ampValid = 0;
      let ampInvalid = 0;
      let ambient = 0;
      let nValid = 0;
      let nInvalid = 0;

      if (f) {
        const n = f.info.cols * f.info.rows;
        for (let i = 0; i < n; i++) {
          if (f.valid[i]) {
            zonesValid++;
            nValid++;
            if (f.amplitude) ampValid += f.amplitude[i];
          } else {
            nInvalid++;
            if (f.amplitude) ampInvalid += f.amplitude[i];
          }
          if (f.ambient) ambient += f.ambient[i];
        }
        ampValid = nValid ? Math.round(ampValid / nValid) : 0;
        ampInvalid = nInvalid ? Math.round(ampInvalid / nInvalid) : 0;
        ambient = n ? Math.round(ambient / n) : 0;
      }

      setStats({
        fps,
        kbps,
        framesCompleted: a.framesCompleted,
        framesDropped: a.framesDropped,
        fragmentsDropped: a.fragmentsDropped,
        captureMs: f?.info.captureMs ?? 0,
        zonesValid,
        zonesTotal: f ? f.info.cols * f.info.rows : 0,
        ampValid,
        ampInvalid,
        ambient,
      });
    }, 250);

    return () => clearInterval(id);
  }, []);

  /* --- connect ----------------------------------------------------------- */
  const connect = useCallback(async (showAll = false) => {
    setError(null);
    const node = new Node({
      onFrame: (f) => {
        frameRef.current = f;
      },
      onHealth: setHealth,
      onEnergy: setEnergy,
      onConfig: setConfig,
      onResult: (r) => {
        if (r.status === 0) {
          addLog(r.opcode ? `command ${r.opcode} applied` : 'config applied');
        } else {
          const field = FIELD_NAMES[r.detail] ?? `field ${r.detail}`;
          addLog(
            `REJECTED (errno ${-r.status})${r.detail ? ` — ${field}` : ''}`,
          );
        }
      },
      onDisconnect: () => {
        setConnected(false);
        addLog('link lost');
      },
      onLog: addLog,
    });

    nodeRef.current = node;
    setBusy(true);
    try {
      await node.connect(showAll);
      setConnected(true);
      setHasFrames(node.hasFrameService);
      node.assembler.reset();
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      /* A cancelled chooser is not an error worth a red banner. */
      if (!/cancelled|User cancelled/i.test(msg)) setError(msg);
      addLog(`connect failed: ${msg}`);
    } finally {
      setBusy(false);
    }
  }, [addLog]);

  const command = useCallback(
    async (opcode: number) => {
      const node = nodeRef.current;
      if (!node) return;
      setBusy(true);
      try {
        await node.command(opcode);
        await node.readConfig();
      } catch (e) {
        addLog(`command failed: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy(false);
      }
    },
    [addLog],
  );

  const writeConfig = useCallback(
    async (c: Config) => {
      const node = nodeRef.current;
      if (!node) return;
      setBusy(true);
      try {
        await node.writeConfig(c);
      } catch (e) {
        addLog(`config write failed: ${e instanceof Error ? e.message : String(e)}`);
      } finally {
        setBusy(false);
      }
    },
    [addLog],
  );

  /*
   * The heatmap's colour scale follows the SELECTED MODE'S BAND.
   *
   * It was fixed at 0-4 m, which makes a close-object demo look almost flat: a
   * hand at 20 cm and a desk at 60 cm land in the same 10% of the ramp. The
   * scale has to follow the measurement, or the picture understates exactly the
   * thing being demonstrated. A hand-edited configuration falls back to a
   * sensible default for its context.
   */
  const mode = config ? modeFor(config) : null;
  const rangeMm = mode
    ? { min: 0, max: mode.bandMaxMm }
    : config?.rangeMode === RANGE.near
      ? { min: 0, max: 1500 }
      : { min: 0, max: 4000 };

  const streaming = config?.mode === MODE.streaming;
  const res = config ? RESOLUTIONS[config.resolution] : null;

  return (
    <div className="wrap">
      <header className="top">
        <h1>
          water-sense ToF node
          <small>
            VL53L9CX over BLE &middot; frames, configuration and health
          </small>
        </h1>
        <div className="row" style={{ marginLeft: 'auto' }}>
          <span className="pill">
            <span className={`dot ${connected ? 'ok' : ''}`} />
            {connected ? nodeRef.current?.device?.name ?? 'connected' : 'not connected'}
          </span>
          {!connected ? (
            <>
              <button
                className="primary"
                onClick={() => connect(false)}
                disabled={busy || !supported}
              >
                Connect
              </button>
              <button
                onClick={() => connect(true)}
                disabled={busy || !supported}
                title="List every BLE device in range, ignoring the name filter"
              >
                Show all devices
              </button>
            </>
          ) : (
            <button className="danger" onClick={() => nodeRef.current?.disconnect()}>
              Disconnect
            </button>
          )}
        </div>
      </header>

      {!supported && (
        <div className="banner">
          <strong>Web Bluetooth is not available in this browser.</strong>
          <br />
          It exists only in Chromium browsers &mdash; Chrome and Edge. Firefox
          and Safari do not implement it and there is no polyfill. The page also
          needs a secure context, which <code>localhost</code> satisfies, so{' '}
          <code>npm run dev</code> is fine as it is.
        </div>
      )}

      {error && <div className="banner">{error}</div>}

      {connected && !hasFrames && (
        <div className="banner warn">
          <strong>This node has no frame service.</strong> It is a deployed-profile
          build &mdash; <code>CONFIG_APP_BLE_FRAME_SERVICE=n</code> &mdash; so the
          streaming code is not in the binary at all and frames physically cannot
          leave it. Configuration and health still work.
        </div>
      )}

      <div className="grid">
        <div>
          <div className="panel">
            <div className="row" style={{ marginBottom: 12 }}>
              <button
                className="primary"
                disabled={!connected || busy || streaming || !hasFrames}
                onClick={() => command(OPCODE.start)}
              >
                Start streaming
              </button>
              <button
                disabled={!connected || busy || !streaming}
                onClick={() => command(OPCODE.stop)}
              >
                Stop
              </button>
              <button
                disabled={!connected || busy || streaming}
                onClick={() => command(OPCODE.singleShot)}
              >
                Single shot
              </button>
              <button
                disabled={!connected || busy}
                onClick={() => command(OPCODE.rebootSensor)}
              >
                Reboot sensor
              </button>
              {config && (
                <span className="pill" style={{ marginLeft: 'auto' }}>
                  {mode ? (
                    <>
                      <strong>{mode.name}</strong> &middot; {mode.band}
                    </>
                  ) : (
                    <>custom</>
                  )}
                  &nbsp;&middot; {res?.label} &middot; {config.exposureMs} ms
                </span>
              )}
            </div>

            <FrameCanvas frameRef={frameRef} rangeMm={rangeMm} />
          </div>

          <StatsBar s={stats} />

          <div className="panel">
            <h2>Event log</h2>
            <div className="log">{log.length ? log.join('\n') : 'nothing yet'}</div>
          </div>
        </div>

        <div>
          <ConfigPanel config={config} onWrite={writeConfig} busy={busy} />
          <HealthPanel health={health} energy={energy} />

          <div className="panel">
            <h2>Notes</h2>
            <p className="note">
              <strong>Cannot see the node?</strong> Press <em>Show all
              devices</em>. If it appears there but not under <em>Connect</em>,
              the radio is fine and the name is not reaching Chrome. If it
              appears in neither, check RTT &mdash; the heartbeat says
              &ldquo;advertising &hellip; discoverable now&rdquo; every ten
              beats while it is findable.
            </p>
            <p className="note">
              The sensor starts <strong>idle</strong> and does not range until
              asked. At 450&ndash;800&nbsp;mW for this profile (UM3683 Table 23)
              that is not a detail &mdash; leaving it streaming into a link
              nobody is watching is the most expensive thing this board can do.
            </p>
            <p className="note">
              Configuration is <strong>not persisted</strong>. There is no NVS by
              decision; the node comes up in a known state every time.
            </p>
            <p className="note">
              This page is the <strong>instrument</strong>, not the product. The
              deployed profile counts people on the MCU and sends 8 bytes in an
              advertisement &mdash; counts leave the device, frames never do.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
