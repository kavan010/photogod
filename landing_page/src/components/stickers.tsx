"use client";

import Icon from "./Icon";

/* Little desktop-junk pieces that get scattered across the hero. */

export type Chrome = "mac" | "win" | "linux";

/**
 * A window frame that can wear mac, windows, or linux clothes — the point is
 * that photogod isn't a mac-only app.
 */
export function WindowFrame({
  title,
  width = 190,
  chrome = "mac",
  dark = false,
  children,
}: {
  title?: string;
  width?: number;
  chrome?: Chrome;
  dark?: boolean;
  children?: React.ReactNode;
}) {
  const bar = dark ? "bg-[#33333a]" : "bg-[#e9e9e6]";
  // light frames follow the theme so they don't glow white in dark mode
  const body = dark ? "bg-[#1f1f23]" : "bg-[var(--panel)]";
  const text = dark ? "text-neutral-400" : "text-neutral-500";

  return (
    <div
      style={{ width }}
      className="overflow-hidden rounded-md border border-black/5 shadow-sm"
    >
      <div className={`flex items-center gap-1.5 px-2 py-1.5 ${bar}`}>
        {chrome === "mac" && (
          <>
            <span className="h-2.5 w-2.5 rounded-full bg-[#ff5f57]" />
            <span className="h-2.5 w-2.5 rounded-full bg-[#febc2e]" />
            <span className="h-2.5 w-2.5 rounded-full bg-[#28c840]" />
          </>
        )}

        {chrome === "linux" && (
          <span className={`text-[9px] ${text}`}>◆</span>
        )}

        {title && (
          <span className={`ml-1 flex-1 truncate text-[9px] ${text}`}>
            {title}
          </span>
        )}

        {/* windows puts its controls on the right, as thin glyphs */}
        {chrome === "win" && (
          <span className={`ml-auto flex items-center gap-2 text-[9px] ${text}`}>
            <span>─</span>
            <span>▢</span>
            <span>✕</span>
          </span>
        )}

        {chrome === "linux" && (
          <span className={`ml-auto flex items-center gap-1.5 ${text}`}>
            <span className="h-2 w-2 rounded-full border border-current" />
            <span className="h-2 w-2 rounded-full border border-current" />
            <span className="h-2 w-2 rounded-full bg-current" />
          </span>
        )}
      </div>
      <div className={body}>{children}</div>
    </div>
  );
}

/** the classic "layers" stack */
export function LayersSticker() {
  const layers = [
    { name: "sky", op: "100%" },
    { name: "the guy", op: "84%" },
    { name: "bg copy", op: "60%" },
  ];
  return (
    <WindowFrame title="layers" width={168} chrome="mac" dark>
      <div className="space-y-px p-1.5">
        {layers.map((l, i) => (
          <div
            key={l.name}
            className={`flex items-center gap-2 rounded px-1.5 py-1 ${
              i === 1 ? "bg-[#3d5a80]" : "bg-[#2c2c2c]"
            }`}
          >
            <span className="checker h-4 w-5 shrink-0 rounded-[2px]" />
            <span className="flex-1 truncate text-[9px] text-neutral-200">
              {l.name}
            </span>
            <span className="text-[8px] text-neutral-500">{l.op}</span>
          </div>
        ))}
      </div>
    </WindowFrame>
  );
}

/** git-style history, on a linux frame */
export function HistorySticker() {
  const items = ["open", "brush", "mask", "levels"];
  return (
    <WindowFrame title="history" width={152} chrome="linux" dark>
      <div className="space-y-1.5 p-2">
        {items.map((h, i) => (
          <div key={h} className="flex items-center gap-2">
            <span
              className={`h-1.5 w-1.5 rounded-full ${
                i === items.length - 1 ? "bg-[#6ea8fe]" : "bg-neutral-600"
              }`}
            />
            <span className="text-[9px] text-neutral-300">{h}</span>
          </div>
        ))}
      </div>
    </WindowFrame>
  );
}

/** curves panel — the most photoshop thing there is */
export function CurvesSticker() {
  return (
    <WindowFrame title="curves" width={140} chrome="win" dark>
      <div className="p-2">
        <div className="relative h-[76px] w-full rounded-[3px] bg-[#151518]">
          <svg viewBox="0 0 100 100" className="absolute inset-0 h-full w-full">
            <path d="M0 100 L100 0" stroke="#3a3a42" strokeWidth="1.5" fill="none" />
            <path
              d="M0 100 C30 88 45 30 100 0"
              stroke="#6ea8fe"
              strokeWidth="2.5"
              fill="none"
            />
            <circle cx="38" cy="58" r="3.5" fill="#6ea8fe" />
            <circle cx="72" cy="20" r="3.5" fill="#6ea8fe" />
          </svg>
        </div>
      </div>
    </WindowFrame>
  );
}

/** the vertical toolbar */
export function ToolbarSticker() {
  const tools = ["move", "marquee-rect", "lasso", "brush", "eraser", "text"];
  return (
    <div className="grid grid-cols-2 gap-1 rounded-md bg-[#2b2b2b] p-1.5 shadow-sm">
      {tools.map((t, i) => (
        <span
          key={t}
          className={`rounded p-1 ${
            i === 3 ? "bg-[#4a4a4a] text-white" : "text-neutral-400"
          }`}
        >
          <Icon name={t} size={16} />
        </span>
      ))}
    </div>
  );
}

/** universal search, but as a bare hint */
export function SearchSticker() {
  return (
    <div className="flex w-[178px] items-center gap-2 rounded-lg border border-[var(--panel-border)] bg-[var(--panel)] px-2.5 py-2 shadow-sm">
      <span className="text-[var(--muted)]">
        <Icon name="search" size={13} />
      </span>
      <span className="text-[10px] text-[var(--muted)]">gaussian blur…</span>
      <kbd className="ml-auto rounded bg-black/5 px-1 text-[8px] text-[var(--muted)] dark:bg-white/10">
        ⌘K
      </kbd>
    </div>
  );
}

/** a hand-scrawled name tag, heyclicky style */
export function NameTag({ color = "#1f5fe0" }: { color?: string }) {
  return (
    <div className="w-[112px] rounded-[3px] bg-white shadow-sm">
      <div
        style={{ background: color }}
        className="rounded-t-[3px] px-2 py-1 text-center text-[6px] font-bold tracking-[0.18em] text-white"
      >
        HELLO
        <div className="text-[5px] font-normal tracking-normal opacity-90">
          my name is
        </div>
      </div>
      <div className="px-2 py-2 text-center text-[13px] font-semibold text-neutral-800">
        photogod
      </div>
      <div style={{ background: color }} className="h-1.5 rounded-b-[3px]" />
    </div>
  );
}

export function SwatchesSticker() {
  const colors = [
    "#e8453c",
    "#f5a623",
    "#f8e71c",
    "#2fbf5f",
    "#2f80ed",
    "#9b51e0",
    "#111111",
  ];
  return (
    <div className="flex gap-[3px] rounded-md border border-[var(--panel-border)] bg-[var(--panel)] p-1.5 shadow-sm">
      {colors.map((c) => (
        <span
          key={c}
          style={{ background: c }}
          className="h-4 w-4 rounded-[2px]"
        />
      ))}
    </div>
  );
}

/** the .psd file you finally don't need */
export function FileSticker({
  label,
  ext = "psd",
  crossed = false,
}: {
  label: string;
  ext?: string;
  crossed?: boolean;
}) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="relative flex h-11 w-9 items-center justify-center rounded-[3px] bg-[#0f1b2d] shadow-sm">
        <span className="text-[8px] font-bold tracking-tight text-[#5fb0ff]">
          {ext}
        </span>
        {crossed && (
          <span className="absolute inset-0 flex items-center justify-center text-2xl text-[#e8453c]">
            ✕
          </span>
        )}
      </div>
      <span className="text-[8px] text-[var(--muted)]">{label}</span>
    </div>
  );
}

export function Kaomoji({ children }: { children: React.ReactNode }) {
  return (
    <span className="text-[15px] tracking-tight text-[var(--muted)]">
      {children}
    </span>
  );
}

export function ToolChip({ tool, label }: { tool: string; label?: string }) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="rounded-md border border-[var(--panel-border)] bg-[var(--panel)] p-2 text-[var(--muted)] shadow-sm">
        <Icon name={tool} size={18} />
      </div>
      {label && (
        <span className="text-[8px] text-[var(--muted)]">{label}</span>
      )}
    </div>
  );
}
