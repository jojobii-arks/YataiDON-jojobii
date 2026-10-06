import { create } from "zustand"
import type { ProgressPad, StatusPayload, TierOption } from "../../shared/types"
import { api } from "../lib/api"
import { useFilterStore } from "./filters"

interface ProgressState {
  status: StatusPayload | null
  pad: ProgressPad | null
  tiers: TierOption[]
  loading: boolean
  rescanning: boolean
  error: string | null
  refresh: () => Promise<void>
  rescan: () => Promise<void>
  bootstrap: () => () => void
}

let refreshInFlight: Promise<void> | null = null

export const useProgressStore = create<ProgressState>((set, get) => ({
  status: null,
  pad: null,
  tiers: [],
  loading: false,
  rescanning: false,
  error: null,

  refresh: async () => {
    if (get().rescanning) return
    if (refreshInFlight) return refreshInFlight

    const { course, star, tier } = useFilterStore.getState()
    const silent = get().pad != null
    if (!silent) set({ loading: true, error: null })

    refreshInFlight = (async () => {
      try {
        const filters = { course, star, tier }
        const [status, pad, tiers] = await Promise.all([
          api.status(),
          api.progress(filters),
          api.tiers(filters),
        ])
        set({
          status,
          pad,
          tiers: tiers.options,
          loading: false,
          error: status.error,
        })
      } catch (err) {
        set({
          loading: false,
          error: err instanceof Error ? err.message : String(err),
        })
      } finally {
        refreshInFlight = null
      }
    })()

    return refreshInFlight
  },

  rescan: async () => {
    set({ rescanning: true, error: null })
    try {
      const status = await api.rescan()
      set({
        status,
        rescanning: false,
        error: status.error,
      })
      // Force a fresh pad read after catalog swap
      refreshInFlight = null
      await get().refresh()
    } catch (err) {
      set({
        rescanning: false,
        error: err instanceof Error ? err.message : String(err),
      })
    }
  },

  bootstrap: () => {
    void get().refresh()
    const unsubFilters = useFilterStore.subscribe(() => {
      void get().refresh()
    })
    const unsubEvents = api.subscribe((event) => {
      if (get().rescanning) return
      if (event === "scores-updated" || event === "catalog-updated") {
        void get().refresh()
      }
    })
    // Poll even when the window is in the background — that's the normal
    // second-monitor / behind-game case, and visibilityState==="hidden"
    // was skipping updates right after a play.
    const pollId = window.setInterval(() => {
      if (get().rescanning) return
      void get().refresh()
    }, 2000)
    return () => {
      unsubFilters()
      unsubEvents()
      window.clearInterval(pollId)
    }
  },
}))
