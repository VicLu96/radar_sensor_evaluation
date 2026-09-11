'use client';

import { useEffect, useRef, type MutableRefObject } from 'react';
import type { Frame } from '../lib/protocol';

/**
 * The heatmap.
 *
 * RENDERS OFF THE REACT PATH, deliberately. A 54x42 frame is 2,268 zones
 * arriving up to ~2.5 times a second; putting that in component state would
 * re-render the tree on every frame and drop frames on a fast link. Instead the
 * newest frame sits in a ref that the BLE callbacks overwrite, and this
 * component draws whatever is there on each animation frame. Rendering
 * decouples from arrival, which is what makes a dropped frame show up as a
 * number in the stats bar rather than as a stutter.
 *
 * Drawn with ImageData at native zone resolution and scaled up by CSS
 * (image-rendering: pixelated). One putImageData per frame, no per-zone
 * fillRect — at 2,268 zones that difference is real.
 */

/* Perceptually ordered ramp: near is cool, far is warm, invalid is near-black.
 * Distance rather than amplitude, because distance is the measurement; the
 * amplitude planes are diagnostics and get their own numbers, not the picture.
 */
function ramp(t: number): [number, number, number] {
  const stops: [number, number, number][] = [
    [44, 26, 94],
    [31, 111, 235],
    [46, 160, 67],
    [210, 153, 34],
    [248, 81, 73],
  ];
  const x = Math.max(0, Math.min(0.9999, t)) * (stops.length - 1);
  const i = Math.floor(x);
  const f = x - i;
  const a = stops[i];
  const b = stops[i + 1];
  return [
    Math.round(a[0] + (b[0] - a[0]) * f),
    Math.round(a[1] + (b[1] - a[1]) * f),
    Math.round(a[2] + (b[2] - a[2]) * f),
  ];
}

export default function FrameCanvas({
  frameRef,
  rangeMm,
}: {
  frameRef: MutableRefObject<Frame | null>;
  rangeMm: { min: number; max: number };
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const rangeRef = useRef(rangeMm);
  rangeRef.current = rangeMm;

  useEffect(() => {
    let raf = 0;
    let lastSeq = -1;

    const draw = () => {
      raf = requestAnimationFrame(draw);

      const f = frameRef.current;
      const canvas = canvasRef.current;
      if (!f || !canvas) return;
      if (f.info.seq === lastSeq) return; /* nothing new to draw */
      lastSeq = f.info.seq;

      const { cols, rows } = f.info;
      if (canvas.width !== cols || canvas.height !== rows) {
        canvas.width = cols;
        canvas.height = rows;
      }

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      const img = ctx.createImageData(cols, rows);
      const { min, max } = rangeRef.current;
      const span = Math.max(1, max - min);

      for (let i = 0; i < cols * rows; i++) {
        const o = i * 4;
        if (!f.valid[i]) {
          /* Not "zero distance" — NO MEASUREMENT. Drawing it as a colour on
           * the same scale would invent a reading the device refused to make. */
          img.data[o] = 12;
          img.data[o + 1] = 14;
          img.data[o + 2] = 18;
          img.data[o + 3] = 255;
          continue;
        }
        const t = (f.distance[i] - min) / span;
        const [r, g, b] = ramp(t);
        img.data[o] = r;
        img.data[o + 1] = g;
        img.data[o + 2] = b;
        img.data[o + 3] = 255;
      }

      ctx.putImageData(img, 0, 0);
    };

    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [frameRef]);

  return (
    <>
      <canvas ref={canvasRef} width={54} height={42} />
      <div className="legend">
        <span>{rangeMm.min / 10} cm</span>
        <span className="bar" />
        <span>{rangeMm.max / 10} cm</span>
      </div>
      <p className="note">
        Near is cool, far is warm. Nearly black means <strong>no valid
        measurement</strong> in that zone, which is not the same as zero
        distance &mdash; the device declined to report, and dark clothing, glass
        and grazing angles all do that at 940&nbsp;nm.
      </p>
    </>
  );
}
