"use client";

import { useCallback, useSyncExternalStore } from "react";

/** listeners fire whenever we flip the class, so the icon stays in sync */
const listeners = new Set<() => void>();

const subscribe = (cb: () => void) => {
  listeners.add(cb);
  return () => {
    listeners.delete(cb);
  };
};

const isDark = () => document.documentElement.classList.contains("dark");

export default function ThemeToggle() {
  // server renders the light glyph; the pre-hydration script has already set
  // the real class by the time this reads it on the client
  const dark = useSyncExternalStore(subscribe, isDark, () => false);

  const toggle = useCallback(() => {
    const next = !isDark();
    document.documentElement.classList.toggle("dark", next);
    try {
      localStorage.setItem("pg-theme", next ? "dark" : "light");
    } catch {
      /* private mode, no big deal */
    }
    listeners.forEach((cb) => cb());
  }, []);

  return (
    <button
      onClick={toggle}
      aria-label="toggle dark mode"
      title={dark ? "light mode" : "dark mode"}
      className="rounded-full border border-[var(--panel-border)] bg-[var(--panel)] p-2 text-[var(--muted)] shadow-sm transition hover:text-[var(--foreground)]"
    >
      {dark ? (
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none">
          <circle cx="12" cy="12" r="4" fill="currentColor" />
          <path
            d="M12 2.5v2m0 15v2M4.6 4.6l1.4 1.4m11.9 11.9l1.4 1.4M2.5 12h2m15 0h2M4.6 19.4l1.4-1.4M17.9 6.1l1.4-1.4"
            stroke="currentColor"
            strokeWidth="1.8"
            strokeLinecap="round"
          />
        </svg>
      ) : (
        <svg width="15" height="15" viewBox="0 0 24 24" fill="currentColor">
          <path d="M20 14.5A8.5 8.5 0 019.5 4a8.5 8.5 0 1010.5 10.5z" />
        </svg>
      )}
    </button>
  );
}
