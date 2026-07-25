/**
 * Mask-painted icon: takes its color from the surrounding `color`, so the same
 * file works on light panels, dark panels, and either theme.
 */
export default function Icon({
  name,
  size = 20,
  className = "",
}: {
  name: string;
  size?: number;
  className?: string;
}) {
  const url = `url(/icons/${name}.svg)`;
  return (
    <span
      aria-hidden
      className={`icon ${className}`}
      style={{
        width: size,
        height: size,
        WebkitMaskImage: url,
        maskImage: url,
      }}
    />
  );
}
