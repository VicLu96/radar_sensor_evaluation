'use client';

export interface Stats {
  fps: number;
  kbps: number;
  framesCompleted: number;
  framesDropped: number;
  fragmentsDropped: number;
  captureMs: number;
  zonesValid: number;
  zonesTotal: number;
  ampValid: number;
  ampInvalid: number;
  ambient: number;
}

/**
 * The numbers that say whether the instrument is working, as opposed to the
 * picture, which says whether the sensor is pointed at anything.
 *
 * THE DROP RATE IS THE POINT. This protocol has no retransmission — at ~2.5 fps
 * a lost frame is cheaper than a stall — so drops are expected and their rate
 * is the honest measure of whether the link keeps up. A drop rate hidden from
 * the UI is a throughput claim nobody can check.
 */
export default function StatsBar({ s }: { s: Stats }) {
  const dropPct =
    s.framesCompleted + s.framesDropped > 0
      ? (100 * s.framesDropped) / (s.framesCompleted + s.framesDropped)
      : 0;

  const validPct = s.zonesTotal > 0 ? (100 * s.zonesValid) / s.zonesTotal : 0;

  /* Invalid zones BRIGHTER than valid ones is saturation, not weak signal —
   * the opposite problem, needing the opposite response. Seen at 24x20 on
   * 2026-09-11: valid 126, invalid 259, 86 of 480 zones usable. */
  const saturated = s.ampInvalid > s.ampValid * 1.25 && s.ampValid > 0;
  const starved = s.ampValid > 0 && s.ampInvalid * 4 < s.ampValid;

  return (
    <div className="panel">
      <h2>Link and frame statistics</h2>
      <div className="stats">
        <div className="stat">
          <div className="k">frame rate</div>
          <div className="v">{s.fps.toFixed(2)} fps</div>
        </div>
        <div className="stat">
          <div className="k">throughput</div>
          <div className="v">{s.kbps.toFixed(1)} kB/s</div>
        </div>
        <div className="stat">
          <div className="k">frames</div>
          <div className="v">{s.framesCompleted}</div>
        </div>
        <div className="stat">
          <div className="k">dropped</div>
          <div className={`v ${dropPct > 5 ? 'err' : dropPct > 0 ? 'warn' : 'ok'}`}>
            {dropPct.toFixed(1)}%
          </div>
        </div>
        <div className="stat">
          <div className="k">capture</div>
          <div className="v">{s.captureMs} ms</div>
        </div>
        <div className="stat">
          <div className="k">zones valid</div>
          <div className={`v ${validPct > 60 ? 'ok' : validPct > 25 ? 'warn' : 'err'}`}>
            {validPct.toFixed(0)}%
          </div>
        </div>
      </div>

      {s.zonesTotal > 0 && (
        <p className="note">
          {s.zonesValid} of {s.zonesTotal} zones valid.
          {s.ampValid > 0 && (
            <>
              {' '}
              Amplitude: <strong>{s.ampValid}</strong> in valid zones,{' '}
              <strong>{s.ampInvalid}</strong> in invalid ones, ambient {s.ambient}.
            </>
          )}
        </p>
      )}

      {saturated && (
        <div className="banner warn" style={{ marginTop: 10, marginBottom: 0 }}>
          <strong>Saturation, not weak signal.</strong> The invalid zones are
          coming back <em>brighter</em> than the valid ones ({s.ampInvalid} vs{' '}
          {s.ampValid}) &mdash; too much return for this exposure at this range,
          so the histogram peak cannot be located and the zone is rejected.
          Lower the exposure, or move the target further away.
        </div>
      )}

      {starved && (
        <div className="banner warn" style={{ marginTop: 10, marginBottom: 0 }}>
          <strong>Little or no return</strong> in the invalid zones. Either raise
          the exposure, or accept that those directions genuinely have no target
          in range &mdash; a dark, angled or distant surface returns nothing, and
          no register fixes physics.
        </div>
      )}
    </div>
  );
}
