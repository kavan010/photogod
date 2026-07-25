"use client";

import { useEffect, useRef, useState } from "react";

type Props = {
  /** percentage position on the viewport */
  x: number;
  y: number;
  rotate?: number;
  delay?: number;
  className?: string;
  children: React.ReactNode;
};

/**
 * A scattered element you can pick up and throw around, like the desktop junk
 * on heyclicky. Position is % based so the layout survives resizes.
 */
export default function Draggable({
  x,
  y,
  rotate = 0,
  delay = 0,
  className = "",
  children,
}: Props) {
  const ref = useRef<HTMLDivElement | null>(null);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const [z, setZ] = useState(20);
  const start = useRef({ px: 0, py: 0, ox: 0, oy: 0 });

  useEffect(() => {
    if (!dragging) return;

    const onMove = (e: PointerEvent) => {
      setOffset({
        x: start.current.ox + (e.clientX - start.current.px),
        y: start.current.oy + (e.clientY - start.current.py),
      });
    };
    const onUp = () => setDragging(false);

    window.addEventListener("pointermove", onMove);
    window.addEventListener("pointerup", onUp);
    window.addEventListener("pointercancel", onUp);
    return () => {
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
      window.removeEventListener("pointercancel", onUp);
    };
  }, [dragging]);

  return (
    <div
      ref={ref}
      onPointerDown={(e) => {
        e.preventDefault();
        start.current = {
          px: e.clientX,
          py: e.clientY,
          ox: offset.x,
          oy: offset.y,
        };
        setZ((v) => v + 1);
        setDragging(true);
      }}
      style={{
        left: `${x}%`,
        top: `${y}%`,
        transform: `translate(${offset.x}px, ${offset.y}px) rotate(${rotate}deg) scale(${
          dragging ? 1.05 : 1
        })`,
        zIndex: dragging ? 60 : z,
        animationDelay: `${delay}s`,
        cursor: dragging ? "grabbing" : "grab",
        transition: dragging ? "none" : "transform 220ms ease",
      }}
      className={`no-select sticker absolute ${className}`}
    >
      {/* float lives on an inner node so it can't fight the drag transform */}
      <div
        className={dragging ? "" : "floaty"}
        style={{ animationDelay: `${delay}s` }}
      >
        {children}
      </div>
    </div>
  );
}
