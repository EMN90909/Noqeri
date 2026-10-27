# Noqeri Motion (experimental)

Noqeri Motion is an independent, browser-native motion layer for the C++
build-client compiler and the older experimental Node-hosted view compiler.
Its API takes inspiration from standard animation concepts used by GSAP,
without copying GSAP code or relying on a GSAP dependency. The browser
executes standard Web Animations API and IntersectionObserver primitives.

## Getting started

Create main.noqeri:

~~~noqeri
motion hero {
  target: ".hero"
  trigger: "timeline"
  fromOpacity: 0
  fromY: 30
  duration: 650
  ease: "smooth"
}

motion cards {
  target: ".card"
  trigger: "scroll"
  fromOpacity: 0
  fromY: 48
  duration: 500
  stagger: 80
}

timeline intro { hero }

view {
  main {
    h1 class: "hero" { "Built with Noqeri Motion" }
    div class: "card" { "Scroll into view" }
  }
}
~~~

Build with:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target noqeri-web -j2
./build/noqeri-web build-client examples/native_web_motion /tmp/noqeri-motion
~~~

The native build uses the C++ compiler, not Node/npm. Serve the generated
HTML, CSS and ES modules from a static host. The Node-hosted experimental
view-project compiler accepts the same motion declarations.

## Motion declarations

Each named motion block has a required target, matching only a simple .class
or #id selector within the rendered Noqeri application root.

| Property | Range / values | Default |
| --- | --- | --- |
| trigger | mount, scroll, timeline | mount |
| fromOpacity, toOpacity | 0–100, integer percentage | 100 |
| fromX, toX, fromY, toY | -2000 to 2000, integer pixels | 0 |
| fromScale, toScale | 1–400, integer percentage | 100 |
| fromRotation, toRotation | -1080 to 1080, integer degrees | 0 |
| duration | 0–30000, integer milliseconds | 500 |
| delay | 0–30000, integer milliseconds | 0 |
| stagger | 0–10000, integer milliseconds | 0 |
| ease | linear, in, out, inOut, back, smooth | smooth |
| repeat | 0–10 | 0 |
| yoyo | true, false | false |
| once | true, false | true |

Timelines sequence named steps from left to right. Every referenced motion
must set trigger to "timeline" and may belong to only one timeline. Animations
can run in parallel by omitting timelines and using trigger "mount". Scroll
motions start upon entering the viewport; they do not alter page scroll.

Browser host integrations can access app.motion.play(name),
app.motion.pause(name), app.motion.reverse(name), app.motion.refresh(),
and app.motion.dispose(). These controls are not yet exposed as arbitrary
Noqeri language function calls.

## Safety and limits

- 128 motions and 32 timelines per view, with up to 32 steps per timeline.
- A selector can match up to 256 elements. Names and values are checked
  at compile time and again by the runtime. Dynamic JavaScript, URLs, and
  arbitrary callbacks are not allowed in the motion declaration.
- No-JavaScript static content remains readable. Motion is progressive
  enhancement, never a requirement for initially displaying text.
- Respects prefers-reduced-motion: reduce by keeping unanimated content
  readable. Animation lifecycle is cancelled on app disposal.
- Scroll reveals use IntersectionObserver; new matching nodes are discovered
  after SPA DOM changes using MutationObserver.
- Native server-rendered serve-app and JS-free static build reject motion
  declarations explicitly. Use build-client for motion-enabled websites.
- Timelines do not yet implement labels, overlap offsets, scrub, pinning,
  SVG morphing, motion paths, FLIP layout, arbitrary object tweening,
  spring solvers or GSAP ScrollTrigger feature parity.
- This does not imply Stage-2 native frontend parity or a production-certified
  cross-browser animation framework. Safari and Firefox need independent E2E
  qualification.

Regression commands:

~~~sh
node --test tests/web-motion.test.mjs
python3 tests/native_web_motion.py ./build/noqeri-web
node tests/browser_native_motion_e2e.mjs ./build/noqeri-web
~~~

The last command uses Playwright test dependencies (only for testing).
