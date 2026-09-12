'use client';

import { useState, useEffect } from 'react';
import {
  RESOLUTIONS,
  PLANE,
  MODE,
  RANGE,
  RANGE_PRESETS,
  type Config,
} from '../lib/protocol';

/**
 * Tier 1 of the configuration surface: the tuning loop.
 *
 * EVERY PARAMETER HERE REQUIRES THE DEVICE IN STANDBY. The firmware handles
 * that — stop, wait for standby, apply, restart — and the reason it is worth
 * saying twice is that the missing wait is exactly what broke on 2026-09-10:
 * STANDBY-only registers written while the device was still winding down were
 * accepted and silently ignored, leaving reset values in place.
 *
 * Writes are transactional on the firmware side: everything validates before
 * anything applies. A rejected write changes nothing at all, and the Config
 * Result notification says which field was at fault.
 */

const FIELD_NAMES = ['', 'resolution', 'planes', 'exposure', 'mode', 'range mode', 'switchover'];

export default function ConfigPanel({
  config,
  onWrite,
  busy,
}: {
  config: Config | null;
  onWrite: (c: Config) => void;
  busy: boolean;
}) {
  const [draft, setDraft] = useState<Config | null>(config);

  useEffect(() => setDraft(config), [config]);

  if (!draft) {
    return (
      <div className="panel">
        <h2>Configuration</h2>
        <p className="note">Connect to read the node&rsquo;s configuration.</p>
      </div>
    );
  }

  const set = <K extends keyof Config>(k: K, v: Config[K]) =>
    setDraft({ ...draft, [k]: v });

  const res = RESOLUTIONS[draft.resolution];
  const nPlanes =
    (draft.planes & PLANE.distance ? 1 : 0) +
    (draft.planes & PLANE.amplitude ? 1 : 0) +
    (draft.planes & PLANE.ambient ? 1 : 0);

  /* Bus time, not BLE time. At 400 kHz the I2C read is the ceiling on frame
   * rate — BLE has roughly 3x the headroom — so this is the number that says
   * what rate is achievable, and it is worth showing before you press apply. */
  const frameBytes = res ? res.zones * 2 * nPlanes : 0;
  const i2cMs = res ? Math.round(((res.zones * 6 + res.zones / 2 + 100) * 9) / 400) : 0;

  const dirty = JSON.stringify(draft) !== JSON.stringify(config);

  return (
    <div className="panel">
      <h2>Configuration</h2>

      <div className="field">
        Measurement range
        <div className="row">
          {RANGE_PRESETS.map((p) => {
            const active =
              draft.rangeMode === p.range &&
              draft.exposureMs === p.exposureMs &&
              draft.switchoverMm === p.switchoverMm;
            return (
              <button
                key={p.label}
                className={active ? 'primary' : ''}
                title={p.hint}
                onClick={() =>
                  setDraft({
                    ...draft,
                    rangeMode: p.range,
                    exposureMs: p.exposureMs,
                    switchoverMm: p.switchoverMm,
                  })
                }
              >
                {p.label.split(' — ')[0]}
              </button>
            );
          })}
        </div>
      </div>

      <p className="note">
        {RANGE_PRESETS.find(
          (p) =>
            draft.rangeMode === p.range &&
            draft.exposureMs === p.exposureMs &&
            draft.switchoverMm === p.switchoverMm,
        )?.hint ??
          'Custom — range context, exposure and switchover set independently below.'}
      </p>

      <label className="field">
        Ranging context
        <select
          value={draft.rangeMode}
          onChange={(e) => set('rangeMode', Number(e.target.value))}
        >
          <option value={RANGE.far}>far — LONG context (default)</option>
          <option value={RANGE.near}>near — SHORT context (ST precision)</option>
        </select>
      </label>

      <label className="field">
        Switchover distance (mm) &mdash; 0 leaves the device&rsquo;s value
        <input
          type="number"
          min={0}
          max={9600}
          step={50}
          value={draft.switchoverMm}
          onChange={(e) => set('switchoverMm', Number(e.target.value))}
        />
      </label>

      <label className="field">
        Resolution
        <select
          value={draft.resolution}
          onChange={(e) => set('resolution', Number(e.target.value))}
        >
          {RESOLUTIONS.map((r) => (
            <option key={r.value} value={r.value}>
              {r.label} — {r.zones} zones ({r.family})
            </option>
          ))}
        </select>
      </label>

      <label className="field">
        Exposure (ms)
        <input
          type="number"
          min={1}
          max={100}
          value={draft.exposureMs}
          onChange={(e) => set('exposureMs', Number(e.target.value))}
        />
      </label>

      <label className="field">
        Frame period (ms) &mdash; 0 runs as fast as the bus allows
        <input
          type="number"
          min={0}
          max={60000}
          step={50}
          value={draft.framePeriodMs}
          onChange={(e) => set('framePeriodMs', Number(e.target.value))}
        />
      </label>

      <div className="field">
        Planes streamed
        <div className="row">
          {(
            [
              ['distance', PLANE.distance],
              ['amplitude', PLANE.amplitude],
              ['ambient', PLANE.ambient],
            ] as const
          ).map(([name, bit]) => (
            <label key={name} style={{ display: 'flex', gap: 5, alignItems: 'center' }}>
              <input
                type="checkbox"
                checked={(draft.planes & bit) !== 0}
                disabled={bit === PLANE.distance}
                onChange={(e) =>
                  set('planes', e.target.checked ? draft.planes | bit : draft.planes & ~bit)
                }
              />
              {name}
            </label>
          ))}
        </div>
      </div>

      <p className="note">
        {res?.label} &times; {nPlanes} plane{nPlanes === 1 ? '' : 's'} ={' '}
        <strong>{frameBytes.toLocaleString()} B</strong> per frame on the link.
        The sensor&rsquo;s own I&sup2;C read is <strong>~{i2cMs} ms</strong> at
        400 kHz regardless of planes &mdash; that, not BLE, is the frame-rate
        ceiling.
      </p>

      <div className="row" style={{ marginTop: 12 }}>
        <button
          className="primary"
          disabled={!dirty || busy}
          onClick={() => onWrite({ ...draft, mode: config?.mode ?? MODE.idle })}
        >
          Apply
        </button>
        <button disabled={!dirty} onClick={() => setDraft(config)}>
          Revert
        </button>
      </div>

      <p className="note">
        Applying stops ranging, waits for STANDBY, writes, and restarts. Amplitude
        and ambient cost bus time but no extra exposure &mdash; they are already
        measured, just not transmitted.
      </p>

      <p className="note">
        <strong>Range is not only the context.</strong> Exposure matters as much
        close up, and in the opposite direction: a near target returns a lot of
        light, and too much exposure saturates the histogram so the zone is{' '}
        <em>rejected</em> rather than reported near. If the valid-zone count
        drops when you move something closer, lower the exposure before
        anything else &mdash; the amplitude split below says which way to go.
      </p>
      <p className="note">
        <strong>9.6 m is a hard ceiling</strong>, not a guideline. UM3683 fixes
        the ranging period at 64 ns and the device cannot see past it at any
        setting.
      </p>
    </div>
  );
}

export { FIELD_NAMES };
