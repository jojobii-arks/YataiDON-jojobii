import * as ToggleGroupPrimitive from "@radix-ui/react-toggle-group"
import * as React from "react"
import { cn } from "../../lib/utils"

export const ToggleGroup = React.forwardRef<
  React.ElementRef<typeof ToggleGroupPrimitive.Root>,
  React.ComponentPropsWithoutRef<typeof ToggleGroupPrimitive.Root>
>(({ className, ...props }, ref) => (
  <ToggleGroupPrimitive.Root
    ref={ref}
    className={cn("flex flex-wrap gap-1.5", className)}
    {...props}
  />
))
ToggleGroup.displayName = ToggleGroupPrimitive.Root.displayName

export const ToggleGroupItem = React.forwardRef<
  React.ElementRef<typeof ToggleGroupPrimitive.Item>,
  React.ComponentPropsWithoutRef<typeof ToggleGroupPrimitive.Item>
>(({ className, ...props }, ref) => (
  <ToggleGroupPrimitive.Item
    ref={ref}
    className={cn(
      "inline-flex h-8 items-center justify-center rounded-[var(--radius-md)] border border-[var(--color-hairline)] bg-[var(--color-surface-card)] px-2.5 text-[12px] font-medium text-[var(--color-body)] transition-colors",
      "hover:border-[var(--color-hairline-strong)] hover:text-[var(--color-ink)]",
      "data-[state=on]:border-[var(--color-ink)] data-[state=on]:bg-[var(--color-ink)] data-[state=on]:text-[var(--color-canvas)]",
      "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--color-primary)]/35",
      "disabled:pointer-events-none disabled:opacity-40",
      className,
    )}
    {...props}
  />
))
ToggleGroupItem.displayName = ToggleGroupPrimitive.Item.displayName
