import { useEffect } from "react"
import { AppShell } from "./components/layout/AppShell"
import { useProgressStore } from "./stores/progress"

export default function App() {
  const bootstrap = useProgressStore((s) => s.bootstrap)

  useEffect(() => bootstrap(), [bootstrap])

  return <AppShell />
}
