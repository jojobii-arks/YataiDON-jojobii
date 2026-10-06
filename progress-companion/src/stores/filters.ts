import { create } from "zustand"
import type { CourseFilter } from "../../shared/types"

interface FilterState {
  course: CourseFilter
  star: number | null
  tier: string | null
  setCourse: (course: CourseFilter) => void
  setStar: (star: number | null) => void
  setTier: (tier: string | null) => void
  /** Scope to a ★ level and optional letter in one update. */
  setLevelTier: (level: number, letter: string | null) => void
}

export const useFilterStore = create<FilterState>((set) => ({
  course: "oni_ura",
  star: null,
  tier: null,
  setCourse: (course) =>
    set((s) => ({
      course,
      // Tier list is Oni/Ura-only; clear when leaving those courses
      tier:
        course === "oni_ura" || course === 3 || course === 4 ? s.tier : null,
    })),
  setStar: (star) =>
    set((s) => ({
      star,
      // Level "All" clears letter filter; otherwise drop mismatched letters
      tier:
        star == null
          ? null
          : !s.tier || s.tier.startsWith(`${star}:`)
            ? s.tier
            : null,
    })),
  setTier: (tier) =>
    set((s) => {
      if (!tier) return { tier: null }
      const level = Number(tier.split(":")[0])
      return {
        tier,
        star: Number.isFinite(level) ? level : s.star,
      }
    }),
  setLevelTier: (level, letter) =>
    set({
      star: level,
      tier: letter ? `${level}:${letter}` : null,
    }),
}))
