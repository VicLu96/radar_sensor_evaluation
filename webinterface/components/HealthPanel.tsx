'use client';

import { ERROR_STATUS_BITS, errorCodeText, type Health, type Energy } from '../lib/protocol';

/**
 * The device's own verdict on itself, which on a board with no known-good
 * reference is the only second opinion available.
 *
 * ERROR_STATUS is a register of ERROR bits (UM3683 Table 16): a SET bit means
 * that error OCCURRED. Two separate places in the firmware had this inverted
 * and announced "PLL NOT LOCKED" precisely when the clock was fine, which sent
 * a day of debugging at the wrong subsystem. Here the bits are listed by name
 * and only highlighted when actually set.
 */
export default function HealthPanel({
  health,
  energy,
}: {
  health: Health | null;
  energy: Energy | null;
}) {
  return (
    <div className="panel">
      <h2>Device health</h2>

      {!health ? (
        <p className="note">Connect to read the node&rsquo;s health.</p>
      ) : (
        <>
          <div className="row" style={{ marginBottom: 12 }}>
            <span className="pill">
              <span className={`dot ${health.sensorReady ? 'ok' : 'err'}`} />
              sensor {health.sensorReady ? 'ready' : 'not ready'}
            </span>
            <span className="pill">
              <span className={`dot ${health.streaming ? 'ok' : ''}`} />
              {health.streaming ? 'streaming' : 'idle'}
            </span>
          </div>

          <div className="stats" style={{ marginBottom: 12 }}>
            <div className="stat">
              <div className="k">captures ok</div>
              <div className="v ok">{health.captureOk}</div>
            </div>
            <div className="stat">
              <div className="k">failed</div>
              <div className={`v ${health.captureFailed ? 'err' : ''}`}>
                {health.captureFailed}
              </div>
            </div>
            <div className="stat">
              <div className="k">error code</div>
              <div className={`v ${health.errorCode ? 'err' : 'ok'}`}>
                0x{health.errorCode.toString(16).padStart(4, '0')}
              </div>
            </div>
          </div>

          {health.errorCode !== 0 && (
            <div className="banner">
              <strong>ERROR_CODE 0x{health.errorCode.toString(16).padStart(4, '0')}</strong>
              <br />
              {errorCodeText(health.errorCode)}
            </div>
          )}

          <table className="bits">
            <tbody>
              {ERROR_STATUS_BITS.map((name, i) => {
                const set = (health.errorStatus & (1 << i)) !== 0;
                return (
                  <tr key={name} className={set ? 'set' : ''}>
                    <td>
                      bit {i} &middot; {name}
                    </td>
                    <td>{set ? 'SET' : '—'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>

          {health.errorStatus === 0 && health.errorCode === 0 && (
            <p className="note">
              All clear &mdash; PLL locked, supplies holding, no firmware fault.
            </p>
          )}
        </>
      )}

      {energy && (
        <>
          <h2 style={{ marginTop: 18 }}>Energy</h2>
          <div className="stats">
            <div className="stat">
              <div className="k">capture</div>
              <div className="v">{energy.lastCaptureMs} ms</div>
            </div>
            <div className="stat">
              <div className="k">live exposure</div>
              <div className="v">{energy.exposureMs} ms</div>
            </div>
            <div className="stat">
              <div className="k">blob upload</div>
              <div className="v">{energy.blobUploadMs} ms</div>
            </div>
            <div className="stat">
              <div className="k">uptime</div>
              <div className="v">{energy.uptimeS} s</div>
            </div>
          </div>
          <p className="note">
            <strong>Live exposure, not configured.</strong> The laser-fault
            backoff halves it on each fault, so these can differ &mdash; and a
            frame whose exposure is unknown is not a usable energy measurement.
            Blob upload is the cost of a cold start, which sets the idle period
            beyond which powering the sensor down beats keeping it in standby.
          </p>
        </>
      )}
    </div>
  );
}
