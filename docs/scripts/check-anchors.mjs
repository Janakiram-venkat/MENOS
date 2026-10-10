// Validates in-site #fragment links against the built pages.
//
// VitePress's own dead-link check covers page links but not the heading
// fragments after them, so a link such as /planning/cost-model#calibration
// would survive the build even if that heading were renamed. This reads the
// rendered HTML, collects every element id, and checks every same-site link
// that carries a fragment.
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs'
import { join, relative, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const dist = fileURLToPath(new URL('../.vitepress/dist', import.meta.url))
if (!existsSync(dist)) {
  console.error('No built site found; run `vitepress build` first.')
  process.exit(1)
}

const htmlFiles = []
;(function walk(dir) {
  for (const name of readdirSync(dir)) {
    const full = join(dir, name)
    if (statSync(full).isDirectory()) walk(full)
    else if (name.endsWith('.html')) htmlFiles.push(full)
  }
})(dist)

const routeOf = (file) => {
  const rel = relative(dist, file).split(sep).join('/')
  if (rel === 'index.html') return '/'
  return '/' + rel.replace(/\.html$/, '').replace(/\/index$/, '')
}

const idsByRoute = new Map()
const pages = new Map()
for (const file of htmlFiles) {
  const html = readFileSync(file, 'utf8')
  pages.set(routeOf(file), html)
  idsByRoute.set(routeOf(file), new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1])))
}

const problems = []
let checked = 0
for (const [route, html] of pages) {
  for (const match of html.matchAll(/<a\s[^>]*?href="([^"]+)"/g)) {
    const href = match[1].replace(/&amp;/g, '&')
    if (!href.includes('#')) continue
    if (/^[a-z]+:/i.test(href)) continue // external
    const [path, fragment] = href.split('#')
    if (!fragment) continue
    const target = path === '' ? route : path.replace(/\/$/, '') || '/'
    const ids = idsByRoute.get(target)
    checked += 1
    if (!ids) problems.push(`${route}: link to ${href} points at a page that was not built`)
    else if (!ids.has(decodeURIComponent(fragment)))
      problems.push(`${route}: link to ${href} has no matching heading id on ${target}`)
  }
}

if (problems.length) {
  console.error('Anchor check FAILED:\n  - ' + [...new Set(problems)].join('\n  - '))
  process.exit(1)
}
console.log(`Anchor check passed: ${checked} fragment links verified across ${pages.size} pages.`)
