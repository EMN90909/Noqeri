// Small zero-dependency JS library interop demo. Replace with your actual GSAP
// module or use the explicit GSAP import-map approach in Doc/WEB_VIEWS_V1.md.
export function arrive(selector) {
  const node = document.querySelector(selector)
  if (node?.animate) node.animate([{ opacity: 0, transform: 'translateY(8px)' },
    { opacity: 1, transform: 'translateY(0)' }], { duration: 500, easing: 'ease-out' })
}
