import { cva, type VariantProps } from "class-variance-authority"
import { Slot } from "@radix-ui/react-slot"
import * as React from "react"
import { cn } from "../../lib/utils"

const buttonVariants = cva(
  "inline-flex items-center justify-center gap-2 whitespace-nowrap text-sm font-medium transition-colors disabled:pointer-events-none disabled:opacity-50 outline-none focus-visible:ring-2 focus-visible:ring-[var(--color-primary)]/40",
  {
    variants: {
      variant: {
        primary:
          "bg-[var(--color-primary)] text-[var(--color-on-primary)] hover:bg-[var(--color-primary-active)] rounded-[var(--radius-md)] h-10 px-[18px]",
        secondary:
          "bg-[var(--color-surface-card)] text-[var(--color-ink)] border border-[var(--color-hairline-strong)] hover:bg-[var(--color-canvas-soft)] rounded-[var(--radius-md)] h-10 px-4",
        ghost:
          "text-[var(--color-body)] hover:text-[var(--color-ink)] hover:bg-[var(--color-surface-strong)]/60 rounded-[var(--radius-md)] h-9 px-3",
        ink: "bg-[var(--color-ink)] text-[var(--color-canvas)] rounded-[var(--radius-md)] h-11 px-5",
      },
      size: {
        default: "",
        sm: "h-8 px-3 text-xs",
        icon: "h-9 w-9 p-0",
      },
    },
    defaultVariants: {
      variant: "secondary",
      size: "default",
    },
  },
)

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  asChild?: boolean
}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : "button"
    return (
      <Comp
        className={cn(buttonVariants({ variant, size }), className)}
        ref={ref}
        {...props}
      />
    )
  },
)
Button.displayName = "Button"
