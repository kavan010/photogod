"use client";

import Icon from "./Icon";
import type { BrushKind } from "./PaintCanvas";

const COLORS = ["#e8453c", "#f5a623", "#2fbf5f", "#2f80ed", "#9b51e0", "#111111"];

const KINDS: { id: BrushKind; icon: string; label: string }[] = [
  { id: "brush", icon: "brush", label: "brush" },
  { id: "marker", icon: "gradient", label: "marker" },
  { id: "eraser", icon: "eraser", label: "eraser" },
];

type Props = {
  color: string;
  setColor: (c: string) => void;
  size: number;
  setSize: (n: number) => void;
  kind: BrushKind;
  setKind: (k: BrushKind) => void;
  onClear: () => void;
};

/** A tiny floating tool options bar — the page's only real chrome. */
export default function BrushBar({
  color,
  setColor,
  size,
  setSize,
  kind,
  setKind,
  onClear,
}: Props) {
  return (
    <div className="no-select fixed bottom-4 left-1/2 z-50 flex max-w-[calc(100vw-1.5rem)] -translate-x-1/2 items-center gap-2 overflow-x-auto rounded-full border border-[var(--panel-border)] bg-[var(--panel)]/85 px-3 py-2 shadow-[0_4px_20px_var(--shadow)] backdrop-blur sm:gap-3">
      <div className="flex shrink-0 items-center gap-1">
        {KINDS.map((k) => (
          <button
            key={k.id}
            onClick={() => setKind(k.id)}
            title={k.label}
            aria-label={k.label}
            aria-pressed={kind === k.id}
            className={`rounded-full p-1.5 transition ${
              kind === k.id
                ? "bg-[var(--foreground)] text-[var(--background)]"
                : "text-[var(--muted)] hover:bg-black/5 dark:hover:bg-white/10"
            }`}
          >
            <Icon name={k.icon} size={16} />
          </button>
        ))}
      </div>

      <span className="h-4 w-px shrink-0 bg-[var(--panel-border)]" />

      <div className="flex shrink-0 items-center gap-1.5">
        {COLORS.map((c) => (
          <button
            key={c}
            onClick={() => setColor(c)}
            title={c}
            aria-label={`color ${c}`}
            aria-pressed={color === c}
            style={{ background: c }}
            className={`h-4 w-4 shrink-0 rounded-full transition ${
              color === c
                ? "ring-2 ring-[var(--foreground)] ring-offset-2 ring-offset-[var(--panel)]"
                : "hover:scale-110"
            }`}
          />
        ))}
        {/* white, useful once the page goes dark */}
        <button
          onClick={() => setColor("#ffffff")}
          title="white"
          aria-label="color white"
          aria-pressed={color === "#ffffff"}
          className={`h-4 w-4 shrink-0 rounded-full border border-[var(--panel-border)] bg-white transition ${
            color === "#ffffff"
              ? "ring-2 ring-[var(--foreground)] ring-offset-2 ring-offset-[var(--panel)]"
              : "hover:scale-110"
          }`}
        />
      </div>

      <span className="hidden h-4 w-px shrink-0 bg-[var(--panel-border)] sm:block" />

      <input
        type="range"
        min={2}
        max={40}
        value={size}
        aria-label="brush size"
        onChange={(e) => setSize(Number(e.target.value))}
        className="hidden h-1 w-16 shrink-0 cursor-pointer appearance-none rounded-full bg-black/10 accent-[var(--foreground)] sm:block dark:bg-white/15"
      />

      <button
        onClick={onClear}
        className="shrink-0 rounded-full px-2.5 py-1 text-[11px] text-[var(--muted)] transition hover:bg-black/5 hover:text-[var(--foreground)] dark:hover:bg-white/10"
      >
        clear
      </button>
    </div>
  );
}
