"use client";

import { useCallback, useRef, useState } from "react";
import PaintCanvas, { type BrushKind } from "@/components/PaintCanvas";
import BrushBar from "@/components/BrushBar";
import Downloads from "@/components/Downloads";
import ThemeToggle from "@/components/ThemeToggle";

export default function Home() {
  // Every reload starts where the app itself starts: a black marker at the top
  // of the size range. #111111 rather than pure black so the matching swatch in
  // BrushBar reads as selected on load.
  const [color, setColor] = useState("#111111");
  const [size, setSize] = useState(40);
  const [kind, setKind] = useState<BrushKind>("marker");
  const clearRef = useRef<() => void>(() => {});

  const onReady = useCallback((api: { clear: () => void }) => {
    clearRef.current = api.clear;
  }, []);

  return (
    <main className="dotgrid relative min-h-screen w-full overflow-hidden">
      {/* paint sits behind everything so it never fights the copy */}
      <PaintCanvas color={color} size={size} kind={kind} onReady={onReady} />

      {/* nav */}
      <nav className="no-select relative z-30 flex items-center justify-between px-5 py-4">
        <span className="text-[15px] font-medium tracking-tight">photogod</span>
        <div className="flex items-center gap-3">
          <a
            href="#get"
            className="text-[13px] text-[var(--accent)] transition hover:opacity-70"
          >
            get photogod
          </a>
          <ThemeToggle />
        </div>
      </nav>

      {/* hero */}
      <section
        id="get"
        className="no-select relative z-30 flex min-h-[78vh] flex-col items-center justify-center px-5 pb-28 text-center"
      >
        {/* soft scrim so paint behind the copy never hurts legibility */}
        <div
          aria-hidden
          className="pointer-events-none absolute left-1/2 top-1/2 -z-10 h-[112%] w-[min(660px,92vw)] -translate-x-1/2 -translate-y-1/2 rounded-[50%] bg-[radial-gradient(ellipse_at_center,var(--background)_30%,transparent_70%)]"
        />
        <h1 className="pointer-events-none text-[clamp(2.4rem,7.5vw,4.6rem)] font-light leading-none tracking-[-0.03em]">
          photogod
        </h1>
        <p className="pointer-events-none mt-3 mb-7 text-[clamp(0.8rem,1.5vw,0.85rem)] font-light text-[var(--foreground)]/75">
          photoshop, but small, fast, and it runs on linux
        </p>
        <Downloads />
      </section>

      <BrushBar
        color={color}
        setColor={setColor}
        size={size}
        setSize={setSize}
        kind={kind}
        setKind={setKind}
        onClear={() => clearRef.current()}
      />
    </main>
  );
}
