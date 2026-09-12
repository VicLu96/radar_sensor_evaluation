'use client';

import { useState } from 'react';
import { MAX_FRAMES, download, type Recorder } from '../lib/recorder';
import type { Config } from '../lib/protocol';

/**
 * Recording control.
 *
 * Deliberately shows the frame count, duration and projected file size WHILE
 * recording. A recording whose size you discover at the end is one you find out
 * too late was the wrong format or ran too long — and at 54x42 with three
 * planes, CSV grows at 6 MB a minute.
 */
export default function RecordPanel({
  recorder,
  config,
  tick,
}: {
  recorder: Recorder;
  config: Config | null;
  /** Bumped by the page's sampling interval so this re-renders while recording. */
  tick: number;
}) {
  void tick;
  const [csv, setCsv] = useState(true);
  const [saved, setSaved] = useState<string | null>(null);

  const frames = recorder.frameCount;
  const bytes = recorder.estimatedBytes(csv);
  const mb = bytes / 1e6;

  const save = () => {
    if (frames === 0) return;
    const stem = recorder.filenameStem();
    const name = csv ? `${stem}.csv` : `${stem}.wstof`;
    download(csv ? recorder.toCsv() : recorder.toBinary(), name);
    setSaved(name);
  };

  return (
    <div className="panel">
      <h2>Recording</h2>

      <div className="row" style={{ marginBottom: 10 }}>
        {!recorder.recording ? (
          <button
            className="primary"
            disabled={!config}
            onClick={() => {
              setSaved(null);
              if (config) recorder.start(config);
            }}
          >
            ● Record
          </button>
        ) : (
          <button className="danger" onClick={() => recorder.stop()}>
            ■ Stop
          </button>
        )}

        <button disabled={frames === 0 || recorder.recording} onClick={save}>
          Save {csv ? 'CSV' : 'binary'}
        </button>

        <label style={{ display: 'flex', gap: 5, alignItems: 'center', fontSize: 12 }}>
          <input type="checkbox" checked={csv} onChange={(e) => setCsv(e.target.checked)} />
          CSV
        </label>

        {recorder.recording && (
          <span className="pill" style={{ marginLeft: 'auto' }}>
            <span className="dot err" /> recording
          </span>
        )}
      </div>

      <div className="stats">
        <div className="stat">
          <div className="k">frames</div>
          <div className={`v ${frames >= MAX_FRAMES ? 'err' : ''}`}>{frames}</div>
        </div>
        <div className="stat">
          <div className="k">duration</div>
          <div className="v">{recorder.durationS.toFixed(1)} s</div>
        </div>
        <div className="stat">
          <div className="k">{csv ? 'csv size' : 'binary size'}</div>
          <div className={`v ${mb > 100 ? 'warn' : ''}`}>
            {mb < 1 ? `${(bytes / 1024).toFixed(0)} kB` : `${mb.toFixed(1)} MB`}
          </div>
        </div>
      </div>

      {recorder.hitCap && (
        <div className="banner warn" style={{ marginTop: 10, marginBottom: 0 }}>
          <strong>Stopped at {MAX_FRAMES} frames.</strong> The cap exists so a
          recording left running does not kill the tab. Save what you have and
          start another.
        </div>
      )}

      {saved && (
        <p className="note">
          Saved <strong>{saved}</strong>. It carries the full configuration in
          its header, including any change made mid-recording.
        </p>
      )}

      <p className="note">
        <strong>CSV</strong> opens in anything and documents itself in a{' '}
        <code>#</code> comment block &mdash;{' '}
        <code>pd.read_csv(path, comment=&quot;#&quot;)</code>.{' '}
        <strong>Binary</strong> is about <strong>3&times; smaller</strong> (0.68
        against 2.05 MB per minute at 54&times;42 distance-only) and exact.
        Use CSV unless the run is long.
      </p>
      <p className="note">
        Distances are millimetres, and <strong>&minus;1 means no valid
        measurement</strong> in that zone &mdash; which is not the same as zero
        distance. Timestamps are the <strong>host&rsquo;s</strong> clock at frame
        completion: the device deliberately has none. Format and Python decoders
        are in <code>docs/plan/recording-format.md</code>.
      </p>
    </div>
  );
}
