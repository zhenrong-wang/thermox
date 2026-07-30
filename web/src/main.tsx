import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import App from './App'
import { DisplayUnitsProvider } from './DisplayUnitsContext'
import './styles.css'

const root = document.getElementById('root')
if (!root) {
  throw new Error('Thermox application root was not found')
}

createRoot(root).render(
  <StrictMode>
    <DisplayUnitsProvider>
      <App />
    </DisplayUnitsProvider>
  </StrictMode>,
)
