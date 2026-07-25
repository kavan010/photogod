"use client";

import { useCallback, useRef, useState } from "react";
import PaintCanvas, { type BrushKind } from "@/components/PaintCanvas";
import BrushBar from "@/components/BrushBar";
import Downloads from "@/components/Downloads";
import Draggable from "@/components/Draggable";
import ThemeToggle from "@/components/ThemeToggle";
import {
  CurvesSticker,
  FileSticker,
  HistorySticker,
  Kaomoji,
  LayersSticker,
  NameTag,
  SearchSticker,
  SwatchesSticker,
  ToolChip,
  ToolbarSticker,
  WindowFrame,
} from "@/components/stickers";

export default function Home() {
  const [color, setColor] = useState("#2f80ed");
  const [size, setSize] = useState(14);
  const [kind, setKind] = useState<BrushKind>("brush");
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

      {/* scattered desktop junk. hidden on small screens so the hero breathes */}
      <div className="pointer-events-none absolute inset-0 z-20 hidden lg:block">
        <div className="pointer-events-auto">
          <Draggable x={4} y={21} rotate={-6} delay={0.2}>
            <LayersSticker />
          </Draggable>

          <Draggable x={17} y={8} rotate={4} delay={1.1}>
            <NameTag />
          </Draggable>

          <Draggable x={7} y={60} rotate={3} delay={0.6}>
            <WindowFrame title="untitled-1.pgd" width={210} chrome="win">
              <div className="checker h-[112px] w-full" />
            </WindowFrame>
          </Draggable>

          <Draggable x={25} y={74} rotate={-4} delay={1.7}>
            <ToolbarSticker />
          </Draggable>

          <Draggable x={28} y={16} rotate={-9} delay={0.9}>
            <ToolChip tool="wand" label="select subject" />
          </Draggable>

          <Draggable x={18} y={44} rotate={0} delay={1.4}>
            <Kaomoji>{"^ ω ^"}</Kaomoji>
          </Draggable>

          <Draggable x={31} y={64} rotate={6} delay={2.6}>
            <ToolChip tool="healing" label="heal" />
          </Draggable>

          <Draggable x={4} y={44} rotate={-3} delay={2.2}>
            <ToolChip tool="curves" />
          </Draggable>

          <Draggable x={67} y={11} rotate={5} delay={0.4}>
            <HistorySticker />
          </Draggable>

          <Draggable x={83} y={24} rotate={-5} delay={1.3}>
            <WindowFrame title="huge.psd" width={178} chrome="mac" dark>
              <div className="space-y-1.5 p-3">
                <div className="h-1.5 w-full rounded-full bg-[#3a3a3a]">
                  <div className="h-1.5 w-[18%] rounded-full bg-[#6ea8fe]" />
                </div>
                <p className="text-[8px] text-neutral-500">
                  opening… 4 min remaining
                </p>
              </div>
            </WindowFrame>
          </Draggable>

          <Draggable x={60} y={40} rotate={-3} delay={2}>
            <Kaomoji>{"¯\\_(ツ)_/¯"}</Kaomoji>
          </Draggable>

          <Draggable x={86} y={53} rotate={7} delay={0.8}>
            <SwatchesSticker />
          </Draggable>

          <Draggable x={68} y={62} rotate={-5} delay={3}>
            <ToolChip tool="levels" label="levels" />
          </Draggable>

          <Draggable x={72} y={65} rotate={-7} delay={1.6}>
            <FileSticker label="never again" ext="psd" crossed />
          </Draggable>

          <Draggable x={87} y={75} rotate={2} delay={0.5}>
            <ToolChip tool="clone" label="clone" />
          </Draggable>

          <Draggable x={55} y={82} rotate={4} delay={2.3}>
            <Kaomoji>{"{ ^-^ }"}</Kaomoji>
          </Draggable>

          <Draggable x={40} y={87} rotate={-2} delay={1}>
            <ToolChip tool="crop" />
          </Draggable>

          <Draggable x={2} y={88} rotate={3} delay={1.9}>
            <SearchSticker />
          </Draggable>

          <Draggable x={78} y={87} rotate={-4} delay={2.8}>
            <CurvesSticker />
          </Draggable>

          <Draggable x={19} y={87} rotate={5} delay={2.5}>
            <ToolChip tool="histogram" />
          </Draggable>
        </div>
      </div>

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
        <h1 className="pointer-events-none text-[clamp(3.2rem,11vw,7rem)] font-semibold leading-none tracking-[-0.045em]">
          photogod
        </h1>
        <p className="pointer-events-none mt-4 mb-9 text-[clamp(1rem,2.2vw,1.2rem)] text-[var(--foreground)]/75">
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
