import { componentVisualFamily } from './topologyVisuals'
import type { CatalogComponent } from './types'

interface ComponentSymbolProps {
  component: Pick<CatalogComponent, 'kind' | 'template_kind' | 'category'>
}

export function ComponentSymbol({ component }: ComponentSymbolProps) {
  const family = componentVisualFamily(component)
  return (
    <span className={`component-symbol symbol-${family}`} aria-hidden="true">
      <svg viewBox="0 0 44 32" focusable="false">
        {family === 'boundary' && (
          <><path d="M5 16h24" /><path d="m23 8 10 8-10 8" /></>
        )}
        {family === 'compressor' && (
          <path d="M8 7 36 3v26L8 25Z" />
        )}
        {family === 'turbine' && (
          <path d="m8 3 28 4v18L8 29Z" />
        )}
        {family === 'combustor' && (
          <><circle cx="22" cy="16" r="12" /><path d="M22 24c-6-4-3-8 0-12 1 4 5 4 3 9 4-3 5-7 2-12 9 7 8 14-5 15Z" /></>
        )}
        {family === 'pump' && (
          <><circle cx="22" cy="16" r="12" /><path d="m18 9 11 7-11 7Z" /></>
        )}
        {family === 'heat_exchanger' && (
          <><circle cx="22" cy="16" r="12" /><path d="m14 8 16 16M30 8 14 24" /></>
        )}
        {family === 'valve' && (
          <><path d="m7 7 15 9L7 25Zm30 0-15 9 15 9Z" /><path d="M22 6v20" /></>
        )}
        {family === 'transport' && (
          <><path d="M5 12h25v-5l9 9-9 9v-5H5Z" /></>
        )}
        {family === 'junction' && (
          <><path d="M6 8h12l8 8h12M6 24h12l8-8" /><circle cx="26" cy="16" r="3" /></>
        )}
        {family === 'vessel' && (
          <><path d="M13 7c0-5 18-5 18 0v18c0 5-18 5-18 0Z" /><path d="M13 8c0 5 18 5 18 0" /></>
        )}
        {family === 'shaft' && (
          <><circle cx="11" cy="16" r="6" /><circle cx="33" cy="16" r="6" /><path d="M17 16h10" /></>
        )}
        {family === 'generator' && (
          <><circle cx="22" cy="16" r="12" /><text x="22" y="20">G</text></>
        )}
        {family === 'instrument' && (
          <><rect x="9" y="5" width="26" height="22" rx="3" /><path d="M14 20h4l3-9 4 12 3-7h3" /></>
        )}
        {family === 'assembly' && (
          <><rect x="6" y="6" width="13" height="9" /><rect x="25" y="17" width="13" height="9" /><path d="M19 11h6v11" /></>
        )}
        {family === 'generic' && (
          <><rect x="8" y="5" width="28" height="22" rx="3" /><path d="M14 16h16" /></>
        )}
      </svg>
    </span>
  )
}
