import { existsSync } from 'node:fs'
import { defineConfig } from '@playwright/test'

const systemChrome = process.env.THERMOX_E2E_CHROME ??
  (existsSync('/usr/bin/google-chrome') ? '/usr/bin/google-chrome' : undefined)

export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  workers: 1,
  timeout: 90_000,
  retries: 0,
  reporter: [['list']],
  use: {
    baseURL: process.env.THERMOX_E2E_BASE_URL ?? 'http://127.0.0.1:25173',
    viewport: { width: 1600, height: 1000 },
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'off',
    launchOptions: systemChrome ? { executablePath: systemChrome } : {},
  },
})
