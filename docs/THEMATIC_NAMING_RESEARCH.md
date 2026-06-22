# CHROMODYNAMIC — Thematic Naming System (QCD / Particle Physics)

> **Status**: Researcher proposal, 2026-06-22. **Scope**: naming design only —
> no code, no renames executed. Adoption requires an ADR + user sign-off.
> **Premise**: the engine name CHROMODYNAMIC *is already* a QCD pun
> (quantum **chromo**dynamics = theory of the strong force; "chroma" = colour,
> perfect for a renderer). `cd::gluon` (shader SL) extends it: gluons are the
> force carriers that BIND quarks → fitting for a shader "binding/glue" layer.
> This doc asks: how far should the theme go, and which names actually AID
> comprehension rather than obscure it.

---

## (a) Theme rationale

QCD is an unusually good metaphor source for a *renderer* engine because the
physics literally trades in **colour** (red/green/blue charge), **binding**
(the strong force gluing constituents), and **fields** (gauge fields on a
lattice) — all of which have direct engine analogues (RGB, resource binding,
GPU buffers/textures as field data on a grid). The danger is buzzword soup:
QCD has ~30 evocative terms and most of them, mapped literally, would HURT a
newcomer. The guideline (section e) is therefore conservative: theme the
*flagship* layer of each library group, keep the descriptive namespace as the
canonical identifier, and reject any name where the metaphor doesn't carry real
explanatory weight.

**Physics is grounded, not decorative.** Every term below is given its real
one-line meaning (sourced) so the mapping is honest. Where the physics doesn't
map, we say so and reject the name.

---

## (b) Mapping table

Legend — **Fit**: how strongly the metaphor explains the subsystem.
**Conf**: confidence this name AIDS rather than obscures (0–100%).

| Physics term | Real meaning (1 line) | Engine subsystem | Fit rationale | Conf |
|---|---|---|---|---|
| **chromodynamics** | SU(3) gauge theory of the strong force; "chroma"=colour | the engine itself | already adopted; colour ↔ renderer | 100% (locked) |
| **gluon** | massless vector boson that BINDS quarks; itself colour-charged | shader SL (`cd::gluon`) | already adopted; binding/glue ↔ shader binding | 100% (locked) |
| **colour charge (R/G/B)** | the SU(3) charge quarks carry; comes in red/green/blue | the RGB pixel / colour pipeline | RGB ↔ colour charge is a *perfect* literal coincidence | 90% |
| **colour singlet / colour-neutral** | composite whose total colour = 0, hence observable | final composited frame / present | scene "resolves" to an observable colour-neutral image | 70% |
| **confinement** | colour force grows with distance → quarks never isolated | resource lifetime / scope ownership (RAII) | "things can't escape their scope" — apt but abstract | 55% |
| **asymptotic freedom** | force weakens at short range → quarks act ~free | hot-path / inner-loop fast path | "close in, the cost vanishes" — cute but obscure | 35% |
| **hadron** | colour-neutral bound state of quarks (baryon/meson) | a fully-assembled renderable / draw packet | a draw is a "bound state" of mesh+material+transform | 60% |
| **meson** | quark + antiquark bound pair | a 2-part binding (e.g. texture+sampler pair) | too specific; rarely a clean 2-body mapping | 30% |
| **baryon** | three-quark bound state (qqq) | a 3-part bundle (e.g. PBR albedo/normal/MRA triple) | occasionally clean but mostly forced | 30% |
| **quark (up/down/…)** | the six elementary fermions of the strong force | smallest engine primitives (handles, IDs) | "elementary unit" is right, but flavour names obscure | 40% |
| **flavour** | the quantum number distinguishing quark types | a type/variant tag (e.g. backend variant) | maps to "kind/variant" reasonably | 45% |
| **lattice (lattice QCD)** | discretised spacetime grid for numeric field evaluation | the framegraph / GPU grid (clustered light grid, DDGI probe grid) | grid-of-field-values ↔ probe/cluster grids is STRONG | 85% |
| **gauge field / field strength tensor** | the gluon field G_μν^a configured over spacetime | GPU buffer/texture resources (field data on the lattice) | resources-as-field-data is apt for an RHI | 65% |
| **jet** | collimated spray of hadrons from a fragmenting parton | particle system / VFX emitter spray | a "spray of products from one source" is a clean match | 80% |
| **hadronization / fragmentation** | partons → colour-neutral hadrons as system cools | asset bake / mesh→GPU finalisation | raw partons "condense" into shippable bound states | 65% |
| **quark-gluon plasma (QGP)** | deconfined hot soup of free quarks+gluons | the job-system / parallel work pool (everything in flight, unbound) | "hot soup of free workers before they bind into results" | 70% |
| **plasma** | (the QGP, shorthand) | concurrency / scheduler | shorter handle for the above | 65% |
| **spin** | intrinsic angular momentum (spin-½ for quarks) | rotation / quaternion / transform | spin ↔ rotation is intuitive even to non-physicists | 75% |
| **isospin** | SU(2) symmetry relating up/down flavours | a symmetry/swap abstraction (e.g. backend parity) | too niche; "isospin" reads as noise | 25% |
| **flux tube / colour string** | confining QCD string between separated charges | a binding link / pipe between two endpoints (channel, stream) | "string connecting two endpoints" maps to streams/channels | 55% |
| **Feynman diagram** | pictorial term in a scattering amplitude expansion | a render-graph node / DAG visualisation | graph-of-interactions ↔ framegraph DAG; nice for the *debug viz* | 70% |
| **vacuum / condensate** | QCD ground state with non-zero field VEV | default/empty-but-not-trivial state (allocator arena baseline) | "the empty state still has structure" — abstract | 40% |
| **parton** | generic name for a quark-or-gluon inside a hadron | a generic sub-resource / constituent | useful generic word but unfamiliar | 45% |
| **running coupling (α_s)** | strong-force strength that varies with energy scale | an LOD / quality-scaling knob | "strength that varies with scale" ↔ LOD scale | 50% |

---

## (c) Strongest shortlist (the 8–12 worth adopting)

Ordered by confidence × explanatory payoff. The first two are already shipped.

1. **CHROMODYNAMIC** — the engine. *(locked)*
2. **`cd::gluon`** — shader SL: binding/glue layer. *(locked — the gold standard
   for "themed name that teaches")*
3. **`cd::lattice`** — the **framegraph / GPU resource grid** layer. Lattice QCD
   = a discretised grid you evaluate fields on; the framegraph + clustered/probe
   grids ARE that. Strong, and "lattice" is an ordinary English word so
   newcomers aren't lost. **(adopt)**
4. **`cd::jet`** — the **particle / VFX emitter** system. A jet is literally a
   collimated spray of products from one source; reads correctly even to people
   who never heard of QCD. **(adopt)**
5. **`cd::plasma`** — the **job system / concurrency** pool. Quark-gluon plasma
   = a hot soup of free, unbound workers before they hadronise into results.
   Evocative and the word "plasma" already connotes energetic parallelism.
   **(adopt, with descriptive alias `cd::jobs`)**
6. **`cd::render::Hadron`** — a **fully-assembled renderable / draw packet** type.
   A draw is a bound state of mesh+material+transform = a colour-neutral hadron.
   **STRONG ADOPT (user-endorsed 2026-06-22):** "hadron" is *distinctive* — it
   collides with no common programming term, so a reader meets it fresh and
   learns it as "the resolved draw packet". This is the model a good themed
   identifier should follow. Best as a *type* (`cd::render::Hadron`), and could
   even anchor the draw-submission layer's vocabulary (a queue of Hadrons).
7. **~~`cd::spin`~~ — REJECTED (2026-06-22, user).** *Spin ↔ rotation* is
   intuitive in physics but **`spin` in code almost always means spinlock /
   spin-wait / busy-spin** (a concurrency primitive). A `cd::spin` or `spin()`
   reads as a busy-loop, not a quaternion — high confusion. Rotation stays
   **descriptive** (`cd::math` quaternion API). See anti-recommendations + the
   new "no-collision" hard rule. *(This is the cautionary counter-example to
   Hadron: physics-accurate but collides with a load-bearing CS word.)*
8. **`cd::bake` ↔ "hadronization"** — keep the **descriptive** name `bake` as
   canonical and use *hadronization* only in docs/comments as the flavour. The
   metaphor (raw partons condense into shippable bound states) is good for
   *explaining*, weak as an identifier. **(theme in prose, not in code)**
9. **colour-charge → RGB** — not a rename, a **documentation motif**: lean into
   "the engine's whole reason for the QCD pun is that colour charge comes in
   R/G/B, exactly like a pixel." Use in the README hero paragraph. **(adopt as
   narrative, not identifier)**
10. **`Feynman` / diagram → framegraph debug-viz codename** — the framegraph DAG
    visualiser could carry the internal codename **"Feynman view"** (a Feynman
    diagram is a graph of interactions; the framegraph is a graph of passes).
    Great for a *debug overlay label*, not for the library namespace. **(adopt
    as a tool/overlay name only)**

> **Net recommendation (revised 2026-06-22):** adopt **`cd::lattice`, `cd::jet`,
> `cd::plasma`** as real flagship library names (each with a descriptive alias),
> plus **`cd::render::Hadron`** (a type — the strongest of all, distinctive +
> teaches), and the **Feynman-view** debug-overlay codename. **`cd::spin` is
> dropped** (collides with spinlock/spin-wait). Keep hadronization/colour-charge
> as *narrative* only. That's **5** code-level themed names on top of the 2
> locked ones — deliberately small, and every survivor passes the no-collision
> test below.
>
> **Distinctiveness ranking (best → weakest themed identifier):**
> `hadron` (zero CS collision, teaches) > `gluon` (locked, zero collision) >
> `jet` (clear in a particle context) > `plasma` (mild: KDE/display, but fine in
> an engine) > `lattice` (mild: lattice-theory / lattice-crypto overlap, context
> disambiguates). **`spin` fails** the test outright — it IS a CS term already.

---

## (d) Anti-recommendations (cute-but-confusing — DO NOT adopt as identifiers)

- **`cd::quark` for "smallest primitive / handle"** — every QCD term implies
  quarks, so the word is overloaded and tells a reader *nothing* about whether
  it's a handle, an ID, an entity, or a component. **Reject.**
- **`cd::meson` / `cd::baryon`** — implying "2-part" / "3-part" bundles forces
  the engine to keep those cardinalities forever or the name lies (the exact
  "despite the name" trap the whimsical-naming critics warn about). **Reject.**
- **`cd::isospin`, `cd::vacuum`, `cd::condensate`, `cd::parton`, `cd::flavour`**
  — too obscure; a new contributor cannot guess the subsystem from the word, and
  the metaphor adds no real intuition. Pure decoration. **Reject.**
- **`cd::confinement` for RAII/scope ownership** — clever but a working C++ dev
  already has "scope/RAII/ownership" as load-bearing words; "confinement" buries
  a familiar concept under jargon. **Reject (use in a comment if you must).**
- **`cd::asymptotic_freedom` for fast-paths** — accurate physics, terrible
  identifier; nobody greps for `asymptotic_freedom` to find the hot loop.
  **Reject.**
- **`cd::spin` for rotation/quaternion (any form)** — REJECTED. Two strikes:
  (1) **name collision** — `spin` in code overwhelmingly means *spinlock /
  spin-wait / busy-spin*, a concurrency primitive; `cd::spin`/`spin()` reads as
  a busy-loop, not a rotation (high confusion, the user's exact objection).
  (2) even as a math alias it competes with the established `quat`/`rotation`
  vocabulary. Rotation stays descriptive. **Reject entirely** (not just the
  wholesale rename — the word itself is poisoned for an identifier).

General anti-pattern: **never rename a clearly-descriptive existing library**
(`cd::anim`, `cd::ecs`, `cd::vfs`, `cd::asset`, `cd::physics`) into physics
jargon. The cost (lost greppability, onboarding friction, churn across 120+
libraries) dwarfs the cuteness benefit.

---

## (e) Theming aggressiveness guideline

**Theme the flagship, keep the descriptive name canonical, theme prose freely.**

Three tiers — apply consistently:

- **Tier 1 — Themed identifier (rare).** Only when the physics term is *also a
  plain English word* AND the metaphor genuinely teaches the subsystem's job.
  Qualifiers so far: `gluon` (binding), `lattice` (grid), `jet` (spray),
  `plasma` (parallel soup). Each Tier-1 library MUST ship a **descriptive
  namespace alias** (`namespace jobs = plasma;`) and a one-line README header
  stating the literal role, so newcomers are never stranded — this is the
  documented best practice for whimsical names at scale (keep the literal name
  reachable).
- **Tier 2 — Themed type or codename (occasional).** A single class
  (`render::Hadron` = resolved draw packet) or a debug-overlay/tool codename
  ("Feynman view"). Low blast radius, easy to rename later.
- **Tier 3 — Themed prose only (default for everything else).** Colour-charge =
  RGB, hadronization = bake, confinement = RAII: live in READMEs, ADR intros,
  and the engine's "story", NOT in code. This captures 100% of the flavour with
  0% of the comprehension tax.

**Hard rules:**

1. Descriptive namespace is always the canonical, greppable identifier; themed
   name is an *alias* or *flagship label*, never the only handle.
2. Never rename an existing descriptive library into jargon — additive only.
3. A themed name must pass the "guess-the-job" test: could a competent C++ dev
   who knows zero QCD guess what the library does from the word? `gluon`/binding,
   `lattice`/grid, `jet`/spray, `plasma`/parallel pass. `isospin`/`baryon`/
   `confinement` fail — reject.
4. **No-collision test (added 2026-06-22):** reject any themed name that already
   has a load-bearing meaning as a common programming/CS term — the metaphor
   gets *overwritten* by the reader's existing association. `spin`→spinlock,
   `thread`→OS thread, `kernel`→OS/compute-kernel, `vector`→std::vector,
   `buffer`→GPU buffer all fail. Winners are words that are **distinctive in a
   code context** so the reader meets them fresh: `hadron`, `gluon` (zero
   collision); `jet`/`plasma`/`lattice` (minor, context-disambiguated). The
   gold test pair: **Hadron passes, spin fails.**
5. Cap Tier-1 themed library names at a small set (≈5–8 total including the two
   locked). Past that, the theme stops being a delightful motif and becomes the
   "whimsical-and-cryptic" liability the literature warns about.

This keeps the QCD identity strong and *visible* (engine name, gluon, lattice,
jet, plasma, the RGB-as-colour-charge story) while the 110+ workhorse libraries
keep their honest descriptive names.

---

## Adoption path (if accepted)

1. **ADR first** — `docs/ADR/ADR-YYYYMMDD-thematic-naming.md` (Iglberger format)
   locking the 3-tier guideline + the Tier-1 shortlist. No code yet.
2. **Additive aliases** for any NEW library only — e.g. when the framegraph or a
   future job-pool lib is (re)introduced, name it `cd::lattice` / `cd::plasma`
   with a `namespace <descriptive> = <themed>;` alias from day one. Existing
   descriptive libs are NOT renamed.
3. **Narrative pass** — weave colour-charge=RGB + the QCD story into the root
   README hero paragraph and per-flagship README headers. Zero code risk.
4. **Codenames** — register "Feynman view" as the framegraph debug-overlay label
   and `render::Hadron` as the resolved-draw-packet type when those land.

---

## Sources

Physics grounding:
- [Quantum chromodynamics — Wikipedia](https://en.wikipedia.org/wiki/Quantum_chromodynamics)
- [Quark — Wikipedia](https://en.wikipedia.org/wiki/Quark)
- [Flavour (particle physics) — Wikipedia](https://en.wikipedia.org/wiki/Flavour_(particle_physics))
- [Isospin — Wikipedia](https://en.wikipedia.org/wiki/Isospin)
- [Hadronization — Wikipedia](https://en.wikipedia.org/wiki/Hadronization)
- [Quark–gluon plasma — Wikipedia](https://en.wikipedia.org/wiki/Quark%E2%80%93gluon_plasma)
- [Color Charge in QCD — modern-physics.org](https://modern-physics.org/color-charge/)

Naming best practices:
- [Taming Names in Software Development — Simple Thread](https://www.simplethread.com/taming-names-in-software-development/)
- [Software Component Names Should Be Whimsical And Cryptic — Aaron Zinger](https://medium.com/better-programming/software-component-names-should-be-whimsical-and-cryptic-ca260b013de0)
- [Naming conventions — scientific literature review (Makimo)](https://makimo.com/blog/scientific-perspective-on-naming-in-programming/)
- [Unreal Engine Naming Convention Guide — Tom Looman](https://tomlooman.com/unreal-engine-naming-convention-guide/)

_Accessed 2026-06-22._
