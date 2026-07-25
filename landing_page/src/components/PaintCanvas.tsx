"use client";

import { useCallback, useEffect, useRef } from "react";

export type BrushKind = "brush" | "eraser" | "marker";

type Props = {
  color: string;
  size: number;
  kind: BrushKind;
  /** exposes a clear() to the parent */
  onReady?: (api: { clear: () => void }) => void;
};

/**
 * Full-viewport paint layer. The pointer draws continuously (no click needed),
 * like a brush that's always down. Strokes persist, then fade very slowly so
 * the page never turns into mud.
 */
export default function PaintCanvas({ color, size, kind, onReady }: Props) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const last = useRef<{ x: number; y: number } | null>(null);
  /** smoothed control point, keeps fast strokes from going polygonal */
  const ctrl = useRef<{ x: number; y: number } | null>(null);
  const width = useRef(0);
  const raf = useRef<number>(0);
  const dpr = useRef(1);

  const clear = useCallback(() => {
    const c = canvasRef.current;
    const ctx = c?.getContext("2d");
    if (!c || !ctx) return;
    ctx.clearRect(0, 0, c.width, c.height);
  }, []);

  useEffect(() => {
    onReady?.({ clear });
  }, [onReady, clear]);

  // size / resize
  useEffect(() => {
    const c = canvasRef.current;
    if (!c) return;

    const resize = () => {
      const ctx = c.getContext("2d");
      if (!ctx) return;
      // preserve what's already painted across resizes
      const prev = document.createElement("canvas");
      prev.width = c.width;
      prev.height = c.height;
      if (c.width && c.height) prev.getContext("2d")?.drawImage(c, 0, 0);

      dpr.current = Math.min(window.devicePixelRatio || 1, 2);
      c.width = Math.floor(window.innerWidth * dpr.current);
      c.height = Math.floor(window.innerHeight * dpr.current);
      c.style.width = `${window.innerWidth}px`;
      c.style.height = `${window.innerHeight}px`;
      ctx.setTransform(dpr.current, 0, 0, dpr.current, 0, 0);
      if (prev.width && prev.height) {
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.drawImage(prev, 0, 0);
        ctx.restore();
      }
    };

    resize();
    window.addEventListener("resize", resize);
    return () => window.removeEventListener("resize", resize);
  }, []);

  // slow fade so the canvas breathes
  useEffect(() => {
    let stop = false;
    let lastTick = performance.now();

    const tick = (now: number) => {
      if (stop) return;
      if (now - lastTick > 60) {
        lastTick = now;
        const c = canvasRef.current;
        const ctx = c?.getContext("2d");
        if (c && ctx) {
          ctx.save();
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.globalCompositeOperation = "destination-out";
          ctx.fillStyle = "rgba(0,0,0,0.035)";
          ctx.fillRect(0, 0, c.width, c.height);
          ctx.restore();
        }
      }
      raf.current = requestAnimationFrame(tick);
    };

    raf.current = requestAnimationFrame(tick);
    return () => {
      stop = true;
      cancelAnimationFrame(raf.current);
    };
  }, []);

  // painting
  useEffect(() => {
    const c = canvasRef.current;
    if (!c) return;
    const ctx = c.getContext("2d");
    if (!ctx) return;

    const paint = (x: number, y: number) => {
      const prev = last.current;
      if (!prev) {
        last.current = { x, y };
        ctrl.current = { x, y };
        width.current = size;
        return;
      }

      const dist = Math.hypot(x - prev.x, y - prev.y);
      if (dist < 0.5) return;

      // midpoint smoothing: curve through the midpoints, using the previous
      // sample as the control point. kills the polygonal look on fast moves.
      const c = ctrl.current ?? prev;
      const midA = { x: (c.x + prev.x) / 2, y: (c.y + prev.y) / 2 };
      const midB = { x: (prev.x + x) / 2, y: (prev.y + y) / 2 };

      let target = size;
      ctx.save();
      ctx.lineCap = "round";
      ctx.lineJoin = "round";

      if (kind === "eraser") {
        ctx.globalCompositeOperation = "destination-out";
        ctx.strokeStyle = "#000";
        target = size * 1.8;
      } else if (kind === "marker") {
        ctx.globalCompositeOperation = "source-over";
        ctx.strokeStyle = color;
        ctx.globalAlpha = 0.18;
        target = size * 1.7;
      } else {
        ctx.globalCompositeOperation = "source-over";
        ctx.strokeStyle = color;
        ctx.globalAlpha = 0.92;
        // faster movement = thinner stroke, like a real brush
        target = Math.max(size * 0.45, size - Math.min(dist, 40) * 0.2);
      }

      // ease the width so it tapers instead of stepping
      width.current += (target - width.current) * 0.35;
      ctx.lineWidth = width.current;

      ctx.beginPath();
      ctx.moveTo(midA.x, midA.y);
      ctx.quadraticCurveTo(prev.x, prev.y, midB.x, midB.y);
      ctx.stroke();
      ctx.restore();

      ctrl.current = prev;
      last.current = { x, y };
    };

    // coalesced events give us the samples the browser batched between frames,
    // which is what actually makes fast strokes look continuous
    const onMove = (e: PointerEvent) => {
      const events = e.getCoalescedEvents?.() ?? [];
      if (events.length) {
        for (const ev of events) paint(ev.clientX, ev.clientY);
      } else {
        paint(e.clientX, e.clientY);
      }
    };
    const onLeave = () => {
      last.current = null;
      ctrl.current = null;
    };

    window.addEventListener("pointermove", onMove, { passive: true });
    window.addEventListener("pointerleave", onLeave);
    window.addEventListener("pointerdown", onMove, { passive: true });
    return () => {
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerleave", onLeave);
      window.removeEventListener("pointerdown", onMove);
    };
  }, [color, size, kind]);

  return (
    <canvas
      ref={canvasRef}
      aria-hidden
      className="pointer-events-none fixed inset-0 z-10"
    />
  );
}
