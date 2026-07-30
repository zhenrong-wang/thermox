import {
  createContext,
  useContext,
  useEffect,
  useState,
  type ReactNode,
} from 'react'
import type { DisplayUnitProfile } from './displayUnits'
import type { CatalogUnitDimension } from './types'

interface DisplayUnitsContextValue {
  profile: DisplayUnitProfile
  setProfile: (profile: DisplayUnitProfile) => void
  unitDimensions: CatalogUnitDimension[]
  setUnitDimensions: (dimensions: CatalogUnitDimension[]) => void
}

const DisplayUnitsContext = createContext<DisplayUnitsContextValue | undefined>(
  undefined,
)

function initialProfile(): DisplayUnitProfile {
  try {
    return localStorage.getItem('thermox.display-unit-profile') ===
      'engineering'
      ? 'engineering'
      : 'si'
  } catch {
    return 'si'
  }
}

export function DisplayUnitsProvider({ children }: { children: ReactNode }) {
  const [profile, setProfile] = useState<DisplayUnitProfile>(initialProfile)
  const [unitDimensions, setUnitDimensions] = useState<
    CatalogUnitDimension[]
  >([])

  useEffect(() => {
    try {
      localStorage.setItem('thermox.display-unit-profile', profile)
    } catch {
      // Browser storage is optional; the in-memory preference remains valid.
    }
  }, [profile])

  return (
    <DisplayUnitsContext.Provider
      value={{
        profile,
        setProfile,
        unitDimensions,
        setUnitDimensions,
      }}
    >
      {children}
    </DisplayUnitsContext.Provider>
  )
}

export function useDisplayUnits(): DisplayUnitsContextValue {
  const value = useContext(DisplayUnitsContext)
  if (!value) {
    throw new Error('Display units require DisplayUnitsProvider.')
  }
  return value
}
