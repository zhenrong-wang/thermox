import { expect, test, type Page } from '@playwright/test'

const components = [
  ['source.material.fixed_composition', 'air_source', 'Ambient air'],
  ['compressor.material.isentropic_efficiency', 'compressor', 'Main compressor'],
  ['source.material.fixed_composition', 'fuel_source', 'Natural gas'],
  ['combustor.material.equilibrium_declared_lhv', 'combustor', 'Combustor'],
  ['turbine.material.isentropic_efficiency', 'turbine', 'Gas turbine'],
  ['sink.material.boundary', 'exhaust_sink', 'Exhaust boundary'],
] as const

const connections = [
  ['air_source', 'outlet', 'compressor', 'inlet'],
  ['compressor', 'outlet', 'combustor', 'air_inlet'],
  ['fuel_source', 'outlet', 'combustor', 'fuel_inlet'],
  ['combustor', 'outlet', 'turbine', 'inlet'],
  ['turbine', 'outlet', 'exhaust_sink', 'inlet'],
  ['turbine', 'shaft', 'compressor', 'shaft'],
] as const

async function placeComponent(
  page: Page,
  kind: string,
  id: string,
  label: string,
  position: { x: number; y: number },
) {
  await page.getByLabel('Filter component library').fill(kind)
  const card = page.locator('button.component-card').first()
  await expect(card).toBeVisible()
  await card.dragTo(page.locator('.react-flow__pane'), {
    targetPosition: position,
  })
  await expect(page.getByRole('heading', { name: 'Place component template' }))
    .toBeVisible()
  await page.getByLabel('Component ID').fill(id)
  await page.getByLabel('Display label').fill(label)
  const published = page.waitForResponse((response) =>
    response.request().method() === 'POST' && response.url().includes('/edits'),
  )
  await page.getByRole('button', { name: 'Place draft component' }).click()
  expect((await published).status()).toBe(201)
  await expect(page.locator('.component-dialog')).toBeHidden()
  await expect(page.locator(`.react-flow__node[data-id="${id}"]`)).toBeVisible()
}

async function connect(
  page: Page,
  sourceId: string,
  sourcePort: string,
  targetId: string,
  targetPort: string,
) {
  const source = page.locator(
    `.react-flow__handle.source[data-nodeid="${sourceId}"][data-handleid="${sourcePort}"]`,
  )
  const target = page.locator(
    `.react-flow__handle.target[data-nodeid="${targetId}"][data-handleid="${targetPort}"]`,
  )
  await expect(source).toBeVisible()
  await expect(target).toBeVisible()
  const from = await source.boundingBox()
  const to = await target.boundingBox()
  expect(from).not.toBeNull()
  expect(to).not.toBeNull()
  const published = page.waitForResponse((response) =>
    response.request().method() === 'POST' && response.url().includes('/edits'),
  )
  await page.mouse.move(from!.x + from!.width / 2, from!.y + from!.height / 2)
  await page.mouse.down()
  await page.mouse.move(to!.x + to!.width / 2, to!.y + to!.height / 2, {
    steps: 20,
  })
  await page.mouse.up()
  expect((await published).status()).toBe(201)
}

async function publishComponentDefinition(
  page: Page,
  label: string,
  configure: (dialog: ReturnType<Page['locator']>) => Promise<void>,
) {
  const row = page.locator('.physical-component-list article').filter({
    hasText: label,
  })
  await row.getByRole('button', { name: 'Define', exact: true }).click()
  const dialog = page.locator('.component-dialog')
  await expect(dialog.getByRole('heading', { name: 'Define component' }))
    .toBeVisible()
  await configure(dialog)
  const published = page.waitForResponse((response) =>
    response.request().method() === 'POST' && response.url().includes('/edits'),
  )
  await dialog.getByRole('button', { name: 'Publish updated revision' }).click()
  expect((await published).status()).toBe(201)
  await expect(dialog).toBeHidden()
}

test('builds and reloads a multi-domain gas-turbine topology through the UI', async ({
  page,
}, testInfo) => {
  const suffix = Date.now().toString(36)
  const projectName = `Playwright gas turbine ${suffix}`
  await page.goto('/')
  await page.getByRole('button', { name: '+ Project' }).click()
  await page.getByLabel('Project name').fill(projectName)
  await page.getByLabel('Description').fill('Isolated browser E2E topology audit')
  const created = page.waitForResponse((response) =>
    response.request().method() === 'POST' &&
    response.url().endsWith('/api/v1/projects'),
  )
  await page.getByRole('button', { name: 'Create project', exact: true }).click()
  expect((await created).status()).toBe(201)
  await expect(page.getByLabel('Project')).toHaveValue(/project-/)
  await expect(page.getByText(/Create the first immutable revision/)).toBeVisible()
  const initialRevision = page.waitForResponse((response) =>
    response.request().method() === 'POST' &&
    response.url().includes('/model-revisions'),
  )
  await page.getByRole('button', { name: 'Create topology' }).click()
  expect((await initialRevision).status()).toBe(201)
  await expect(page.getByRole('heading', { name: projectName })).toBeVisible()

  const positions = [
    { x: 130, y: 180 },
    { x: 360, y: 180 },
    { x: 590, y: 180 },
    { x: 130, y: 560 },
    { x: 360, y: 560 },
    { x: 590, y: 560 },
  ]
  for (let index = 0; index < components.length; index += 1) {
    const [kind, id, label] = components[index]
    await placeComponent(page, kind, id, label, positions[index])
  }

  await page.getByLabel('Filter component library').fill('')
  await page.getByRole('button', { name: 'Arrange flow' }).click()
  await page.waitForTimeout(700)
  for (const [sourceId, sourcePort, targetId, targetPort] of connections) {
    await connect(page, sourceId, sourcePort, targetId, targetPort)
  }

  await expect(page.locator('.react-flow__node')).toHaveCount(6)
  await expect(page.locator('.react-flow__edge')).toHaveCount(6)
  await page.screenshot({
    path: testInfo.outputPath('gas-turbine-topology.png'),
    fullPage: true,
  })

  await page.reload({ waitUntil: 'networkidle' })
  await page.getByLabel('Project').selectOption({ label: projectName })
  await expect(page.locator('.react-flow__node')).toHaveCount(6)
  await expect(page.locator('.react-flow__edge')).toHaveCount(6)

  await page.getByRole('button', { name: 'JSON', exact: true }).click()
  const source = await page.getByLabel('Topology JSON document').inputValue()
  const topology = JSON.parse(source)
  expect(topology.model.components).toHaveLength(6)
  expect(topology.model.connections).toHaveLength(6)
  expect(topology.model.components.map((component: { id: string }) => component.id))
    .toEqual(expect.arrayContaining(components.map(([, id]) => id)))
  await page.getByRole('button', { name: 'Cancel' }).click()

  await page.getByRole('button', { name: /Define system/ }).click()
  await expect(page.getByRole('heading', { name: 'Component definitions' }))
    .toBeVisible()

  await page.getByRole('button', { name: '+ Reacting mixture' }).click()
  const materialDialog = page.locator('.component-dialog')
  await materialDialog.getByLabel('Mixture ID').fill('working_gas')
  await materialDialog.getByLabel('Mechanism').fill('gri30.yaml')
  await materialDialog.getByLabel('Phase').fill('gri30')
  await materialDialog.getByLabel('Model species').fill('N2, O2, CH4, CO2, H2O')
  const materialPublished = page.waitForResponse((response) =>
    response.request().method() === 'POST' && response.url().includes('/edits'),
  )
  await materialDialog.getByRole('button', { name: 'Publish mixture revision' }).click()
  expect((await materialPublished).status()).toBe(201)
  await expect(materialDialog).toBeHidden()

  await publishComponentDefinition(page, 'Ambient air', async (dialog) => {
    await dialog.getByLabel('outlet · material').selectOption('working_gas')
    await dialog.getByLabel(/^mass_fraction\[N2\]/).fill('0.767')
    await dialog.getByLabel(/^mass_fraction\[O2\]/).fill('0.233')
  })
  await publishComponentDefinition(page, 'Natural gas', async (dialog) => {
    await dialog.getByLabel('outlet · material').selectOption('working_gas')
    await dialog.getByLabel(/^mass_fraction\[CH4\]/).fill('1')
  })
  await publishComponentDefinition(page, 'Main compressor', async (dialog) => {
    await dialog.getByLabel('inlet · material').selectOption('working_gas')
    await dialog.getByLabel('outlet · material').selectOption('working_gas')
    await dialog.getByLabel(/^pressure_ratio/).fill('18')
    await dialog.getByLabel(/^eta_is/).fill('0.88')
  })
  await publishComponentDefinition(page, 'Combustor', async (dialog) => {
    await dialog.getByLabel('air_inlet · material').selectOption('working_gas')
    await dialog.getByLabel('fuel_inlet · material').selectOption('working_gas')
    await dialog.getByLabel('outlet · material').selectOption('working_gas')
    await dialog.getByLabel(/^pressure_ratio/).fill('0.966')
    await dialog.getByLabel(/^combustion_efficiency/).fill('0.9995')
    await dialog.getByLabel(/^fuel_lower_heating_value/).fill('50000000')
  })
  await publishComponentDefinition(page, 'Gas turbine', async (dialog) => {
    await dialog.getByLabel('inlet · material').selectOption('working_gas')
    await dialog.getByLabel('outlet · material').selectOption('working_gas')
    await dialog.getByLabel(/^pressure_ratio/).fill('16')
    await dialog.getByLabel(/^eta_is/).fill('0.895')
  })
  await publishComponentDefinition(page, 'Exhaust boundary', async (dialog) => {
    await dialog.getByLabel('inlet · material').selectOption('working_gas')
  })

  await expect(page.getByText('6/6', { exact: true })).toBeVisible()
  const validationResponse = page.waitForResponse((response) =>
    response.request().method() === 'POST' &&
    response.url().includes('/validate-definition'),
  )
  await page.getByRole('button', { name: 'Validate definition' }).first().click()
  expect((await validationResponse).status()).toBe(200)
  await expect(page.getByRole('heading', { name: 'Physical definition validated' }))
    .toBeVisible()
  await expect(page.getByText('6 components', { exact: true })).toBeVisible()
  await page.screenshot({
    path: testInfo.outputPath('gas-turbine-definition.png'),
    fullPage: true,
  })
})
