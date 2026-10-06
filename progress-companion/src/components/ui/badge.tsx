import * as React from "react"
import { cn } from "../../lib/utils"

export function Badge({
  className,
  ...props
}: React.HTMLAttributes<HTMLSpanElement>) {
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-[var(--radius-pill)] bg-[var(--color-surface-strong)] px-2.5 py-1 text-[11px] font-semibold uppercase tracking-[0.08em] text-[var(--color-ink)]",
        className,
      )}
      {...props}
    />
  )
}
