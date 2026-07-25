"use client";

import { useState } from "react";
import Icon from "./Icon";

const UBUNTU = "sudo apt install photogod";
const ARCH = "yay -S photogod";

function AppleIcon() {
  return (
    <svg width="19" height="19" viewBox="0 0 24 24" fill="currentColor">
      <path d="M16.4 12.8c0-2.5 2-3.7 2.1-3.8-1.2-1.7-3-1.9-3.6-2-1.5-.2-3 .9-3.8.9s-2-.9-3.3-.9c-1.7 0-3.3 1-4.2 2.5-1.8 3.1-.5 7.7 1.3 10.2.9 1.2 1.9 2.6 3.2 2.5 1.3-.05 1.8-.8 3.3-.8s2 .8 3.3.8c1.4 0 2.2-1.2 3.1-2.5.7-1 1-1.9 1.2-2.4-2.6-1-2.6-4.4-2.6-4.5zM14 4.6c.7-.9 1.2-2.1 1-3.3-1 .04-2.3.7-3 1.6-.6.8-1.2 2-1.1 3.2 1.1.1 2.3-.6 3.1-1.5z" />
    </svg>
  );
}

function WindowsIcon() {
  return (
    <svg width="17" height="17" viewBox="0 0 24 24" fill="currentColor">
      <path d="M3 5.5l7.2-1v7.1H3V5.5zm0 13l7.2 1v-7H3v6zM11.4 4.3L21 3v8.6h-9.6V4.3zm0 15.4L21 21v-8.5h-9.6v7.2z" />
    </svg>
  );
}

/**
 * A download button styled like a paint swatch: the fill is a rough brush
 * stroke that sweeps across on hover, and there's a wet paint dot on the left.
 */
function PaintButton({
  href,
  paint,
  ink = "#ffffff",
  children,
}: {
  href: string;
  paint: string;
  ink?: string;
  children: React.ReactNode;
}) {
  return (
    <a
      href={href}
      style={{ ["--paint" as string]: paint, color: ink }}
      className="group relative isolate flex items-center gap-2.5 overflow-hidden rounded-full px-5 py-2.5 text-[17px] font-medium shadow-[0_2px_12px_var(--shadow)] transition-transform duration-200 hover:-translate-y-0.5 active:translate-y-0"
    >
      {/* the painted body */}
      <span
        aria-hidden
        className="absolute inset-0 -z-10 rounded-full"
        style={{ background: "var(--paint)" }}
      />
      {/* brush texture sweeping across on hover */}
      <span
        aria-hidden
        className="absolute inset-0 -z-10 rounded-full opacity-0 mix-blend-overlay transition-opacity duration-300 group-hover:opacity-100"
        style={{
          backgroundImage:
            "repeating-linear-gradient(78deg, rgba(255,255,255,.55) 0 1px, transparent 1px 4px)",
        }}
      />
      {/* wet highlight streak */}
      <span
        aria-hidden
        className="absolute inset-y-0 -left-1/3 -z-10 w-1/3 skew-x-[-18deg] bg-white/25 blur-md transition-transform duration-700 ease-out group-hover:translate-x-[420%]"
      />
      {children}
    </a>
  );
}

function CopyRow({ label, cmd }: { label: string; cmd: string }) {
  const [copied, setCopied] = useState(false);

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(cmd);
      setCopied(true);
      setTimeout(() => setCopied(false), 1400);
    } catch {
      /* clipboard blocked — the command is on screen anyway */
    }
  };

  return (
    <button
      onClick={copy}
      className="group flex w-full items-center gap-2.5 rounded-lg px-3 py-2 text-left transition hover:bg-black/[0.04] dark:hover:bg-white/[0.06]"
    >
      <span className="w-12 shrink-0 text-[10px] uppercase tracking-wider text-[var(--muted)]">
        {label}
      </span>
      <code className="flex-1 truncate font-mono text-[11.5px] text-[var(--foreground)]">
        <span className="text-[var(--muted)]">$ </span>
        {cmd}
      </code>
      <span className="shrink-0 text-[10px] text-[var(--muted)] transition group-hover:text-[var(--foreground)]">
        {copied ? "copied ✓" : "copy"}
      </span>
    </button>
  );
}

export default function Downloads() {
  return (
    <div className="flex flex-col items-center gap-5">
      <div className="flex flex-wrap items-center justify-center gap-3">
        <PaintButton href="#download-mac" paint="#2f3b8f">
          <AppleIcon />
          download for mac
        </PaintButton>
        <PaintButton href="#download-windows" paint="#e8453c">
          <WindowsIcon />
          download for windows
        </PaintButton>
      </div>

      {/* linux, always visible */}
      <div className="w-[430px] max-w-[92vw] rounded-xl border border-[var(--panel-border)] bg-[var(--panel)]/85 p-1.5 shadow-[0_4px_20px_var(--shadow)] backdrop-blur">
        <div className="flex items-center gap-2 px-3 pt-1.5 pb-1">
          <span className="text-[var(--muted)]">
            <Icon name="terminal" size={12} />
          </span>
          <span className="text-[10px] uppercase tracking-wider text-[var(--muted)]">
            or install the linux cli
          </span>
        </div>
        <CopyRow label="ubuntu" cmd={UBUNTU} />
        <CopyRow label="arch" cmd={ARCH} />
      </div>

      <p className="text-[12px] text-[var(--muted)]">
        100% free. and it actually runs on linux.
      </p>
    </div>
  );
}
