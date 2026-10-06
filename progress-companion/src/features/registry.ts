import type { FeatureMeta } from "../../shared/types"
import type { ComponentType } from "react"
import { SummaryView } from "./summary/SummaryView"
import { SessionPlaceholder } from "./PlaceholderView"

export interface FeatureModule extends FeatureMeta {
  component: ComponentType
}

export const FEATURES: FeatureModule[] = [
  {
    id: "summary",
    label: "Summary",
    description: "Rank & crown totals with course / ★ / tier filters",
    available: true,
    component: SummaryView,
  },
  {
    id: "session",
    label: "Session",
    description: "Deltas since the companion opened — coming soon",
    available: false,
    component: SessionPlaceholder,
  },
]

export function getFeature(id: string): FeatureModule {
  return FEATURES.find((f) => f.id === id) ?? FEATURES[0]
}
