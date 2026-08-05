# Types as Intent, Not Just Representation

*Disclaimer: This is the story of how I never wanted a DOM — and why you might not need one either.*

**Target audience**: This document is written for C++-centric applications where the domain model naturally lives in C++ types. If you're building polyglot systems where multiple languages need to share schemas, you likely need an external IDL (Protobuf, OpenAPI, etc.) as your source of truth, and C++ types would be generated from that. See the section "When C++ Types Should NOT Be the Source of Truth" below for more context.


JsonFusion is built on a simple idea:

        Your C++ types are more than memory layout 
            — they encode what you actually care about.

Many JSON libraries throw that information away early and try to compensate with:
- DOM trees,
- dynamic lookups,
- and heavily optimized low-level parsers (SIMD, hand-tuned FSMs, etc).

JsonFusion goes in the opposite direction:
- keep as much static type information as possible,
- push as much work as possible into compile time,
- and let the compiler specialize the parser for each concrete C++ type.

The result is not just “faster” in a vacuum; it changes what kinds of optimizations are even possible.


## The DOM Detour: Doing Work You Never Needed

**Important scope note**: This section is about situations where a DOM is built but never actually needed — where you knew your target types from the start. There are legitimate use cases for DOM-based parsing: debugging unknown JSON, building proxies or middleware that forwards data without interpreting it, handling truly dynamic schemas, or quick one-off tools. The problem is when DOM-plus-manual-mapping becomes the default pattern even for well-structured, statically-known data models.

A significant amount of C++ JSON code in the wild follows this pattern:
1.	Parse everything into a generic DOM:
- QJsonDocument / QJsonObject in Qt,
- rapidjson::Document with dom.Parse(...),
- nlohmann::json trees,
- etc.
2.	Then write hundreds or thousands of lines of manual mapping code to turn that DOM into real C++ types.

```c++
QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
QJsonObject root = doc.object();

Config cfg;
cfg.id         = root["id"].toInt();
cfg.threshold  = root["threshold"].toDouble();
cfg.user.name  = root["user"].toObject()["name"].toString();
// …repeat for every field, in every struct, in every model…
```

Forget about performance — you run it once at app startup, it's not a bottleneck. But what even is this code? 

You didn’t want a DOM.
You just wanted a Config.

The cost of this “DOM detour” is threefold:

1. Work You Never Needed

    By default, a DOM-based API encourages you to parse and allocate everything:
    - Every object and array becomes a node.
    - Every number and string is materialized.
    - You pay for all fields, even if you only ever use a few.

    Even in Qt’s own examples, the recommended flow is: parse into QJsonDocument, get a QJsonObject, 
    then convert to a QVariantMap, then fish values out of it.

    For something like canada.json, that means:
    - every coordinate is parsed,
    - every value is kept in memory,
    - even if your actual goal is “just count how many points/rings/features there are”.

    With JsonFusion, the type can say “I don’t care about these values” and the generated parser simply doesn’t parse those
    numbers at all. That’s how we end up ~2× faster than a hand-written RapidJSON SAX handler 
    on the “count features/rings/points” task for canada.json — without any SIMD tricks, just by not doing unnecessary work.

    A generic DOM has no way to express that intent. It has to parse everything first.

2. Two Schemas, Forever Out of Sync

    In the DOM style, you effectively maintain two separate schemas:
    - Your C++ types (struct Config { ... })
    - A forest of stringly-typed accesses:

        ```c++
        root["user"].toObject()["name"].toString();
        root["settings"].toObject()["network"].toObject()["port"].toInt();
        ```
    Every time the JSON shape changes, you must:
    - update the C++ structs,
    - and separately update all those access chains.

    There’s nothing tying them together. No compiler help. Every typo is a runtime bug.

    In JsonFusion, the struct is the schema:
    ```c++
    struct Config {
        A<int,    key<"id">>         id;
        A<float,  key<"threshold">>  threshold;
        A<User,   key<"user">>       user;
    };
    // (The key<"string"> syntax uses C++23 string literals as template parameters)
    ```
    
    If the JSON changes and you don’t update the struct, it fails at parse/validation time.
    There is no second manual mapping layer to forget.

3. Cognitive Load and Boilerplate

A DOM-first style also explodes cognitive complexity:
- You spend your time thinking in terms of:
    - “is this QJsonObject or QJsonArray here?”,
    - “did I already call .toObject()?”,
    - “do I need .toDouble() or .toInt() here?”.
- You re-encode the shape of your data over and over in ad-hoc access chains.

It’s 2025 and the official Qt JSON docs still teach “parse into QJsonDocument, convert to QJsonObject / QVariantMap, then manually pull values out” to be a standard approach.
It works, but it’s a low-level, DOM-centric mindset that made sense when C++ didn’t have powerful compile-time machinery.

JsonFusion’s stance is:

    Don’t build a generic tree and then shovel it into your types.
    Let your types drive the parser directly.

- No intermediate DOM.
- No second mapping layer.
- No parsing of fields you’ll just throw away.
- And, thanks to annotations, you can encode intent like “skip this”, “enforce this range”, “minimum items”, etc., right in the type — and the generated parser will honor that at full speed.


In practice, most C++ code that talks to JSON or other “flexible” formats still follows one of a few familiar patterns.

The first is the DOM-then-map style: parse everything into a generic tree
(QJsonDocument/QJsonObject, rapidjson::Document, nlohmann::json, …) and then write manual code to walk
that tree and assign into “real” C++ types. It works, but it means you always do a lot of work you
didn’t strictly need: you parse and keep every field by default, even when the application only cares
about a small subset. And you effectively maintain two schemas: one in your struct Config { … }, and
another encoded in stringly-typed lookups like root["network"]["port"].toInt(). Keeping those two in
sync over time is entirely a human discipline problem.

The second pattern is macro-based or intrusive metadata, where you try to reflect your types through
registration macros or special functions: REFLECT(Foo, field1, field2), BOOST_FUSION_ADAPT_STRUCT,
custom to_json/from_json pairs, and so on. This avoids the full DOM detour, but you still end up
duplicating field lists and wiring, just in a different form. The compiler can help a bit more here,
but you’re still re-describing the structure of your data next to the type instead of having “the type
is the schema” as a single source of truth.

The third common approach pushes the schema completely outside the C++ code into an IDL or external
generator: Protobuf, FlatBuffers, Thrift, JSON-schema-to-C++ tools, in-house code generators. In that
world, the "real" data model lives in `.proto`/`.fbs`/etc. files, and C++ sees whatever the generator emits. 
You then either 
(a) accept those generated types as your domain model and live with the fact that your core types are
effectively owned by the toolchain, or 
(b) build another layer of mapping between “generated types” and “real types” and end up with models
for models.

There are modern libraries that try to break out of this triangle by being more type-driven from the
start – Glaze, jsonifier and a few others generate serializers/deserializers directly from C++ types
and metadata, often avoiding a DOM entirely. Conceptually they move in the same direction: treat C++
types as the source of truth instead of treating JSON as the centre of the universe. But if you look at
a lot of production C++ today – especially GUI apps, services, tools – the dominant habits are still
DOM-plus-manual-mapping, or external schema generators, with all the extra parsing, duplication and
cognitive overhead that implies.

## No Macros, No Hidden Magic

A quick but crucial point: **JsonFusion is pure, first-class C++ code.** No macros. No preprocessor magic. No code generation step.

Many reflection-like libraries use macros that look simple but expand into invisible complexity:
```cpp
BOOST_FUSION_ADAPT_STRUCT(Config, (int, port), (std::string, hostname))
REFLECT(Config, port, hostname)
```

These create "magic" that:
- Hides implementation details from IDEs and developers
- Can't be inspected or navigated as normal C++
- Expands into generated code you never see
- Can't be tested in constexpr contexts

JsonFusion's `A<int, key<"port">, range<1, 65535>>` might look verbose, but it's **exactly what it appears to be**: a template instantiation. Your IDE can navigate it. Debuggers see it. Constexpr tests validate it. There's no preprocessing step turning terse syntax into hidden machinery.

### Why Annotated<> Exists (And What It Actually Is)

The `Annotated<T, ...>` pattern (aliased as `A<>`) is a compromise born from language limitations. We need a way to:
- Attach metadata to fields **declaratively**, in-place where the field is defined
- Make that metadata **composable** (nest annotated types, reuse them)
- Avoid global registration, detached mapping functions, or "magic at a distance"

This enables the key property: **locality and refactoring safety**. If a field moves from `A.B.C.D` to `A.B.E` in your JSON schema, you do exactly the same in your C++ structs. You don't update detached imperative code searching for `["A"]["B"]` in scattered locations. When you change the struct, the compiler immediately shows every place that depends on it.

What's actually in `Annotated`? About 200 lines of straightforward code:
```cpp
template<typename T, typename... Annotations>
struct Annotated {
    T value;
    // ... implicit conversion operators to behave (mostly) like T
    // ... no inheritance tricks, no runtime overhead
};
```

It's **optional** — plain `std::string field;` works fine. You only use `Annotated` when you need extra metadata (JSON key mapping, validation constraints, etc.). And because it's just a template, not a macro, you can read `annotated.hpp` in five minutes and understand exactly what it does.


## Performance Is Not Just About SIMD

Low-level optimizations (SIMD, branchless parsing, fast float algorithms) absolutely matter. RapidJSON is an excellent example of what you can get by optimizing the tokenizer and numeric parser to the limit.

But there’s a hard constraint for any generic JSON library:

**The tokenizer has no idea what you intend to do with each value.**

When RapidJSON parses a number in SAX mode, it must:
- recognize the token as a number,
- validate its syntax,
- convert it to a C++ numeric type,
- hand it to your handler.

Whether you then:
- store it,
- partially use it,
- or immediately ignore it,

is outside the tokenizer’s view. It can’t decide to “not bother” parsing a valid JSON number.

JsonFusion can, because the schema is in the type.

### The canada.json Example

Take the classic canada.json benchmark: a large GeoJSON file where almost everything is numeric coordinates.

We tried several modes:
- RapidJSON DOM parse + populate
- RapidJSON SAX handler that just counts features/rings/points
- JsonFusion Parse + Populate into a strongly typed model
- JsonFusion streaming consumer that counts objects
- JsonFusion streaming with skip on point coordinates
- JsonFusion streaming with “numbers-tokenizing-only” (scan and validate number text, but skip numeric conversion)

When JsonFusion and RapidJSON do the same semantic work (scan + fully parse all numbers), their performance is in the same ballpark as RapidJSON’s SAX handler. That’s already a good sign: the template machinery isn’t costing you anything at runtime.

But the interesting part is this:

```c++
    struct Pt_ {
        Annotated<float, skip> x;
        Annotated<float, skip> y;
    };
    using Point = Annotated<Pt_, as_array>;
```

For a task like “just count features/rings/points”, the numeric values are irrelevant. You care about structure, not coordinates.

With skip:
- JsonFusion still enforces structure:
    - `[x, y]` arrays of size 2,
    - nested arrays have the expected shape,
- but it does not parse the numbers at all:
    - it reads each number token as a JSON value and skips over it,
    - no float conversion backend is called.

On canada.json, that simple type change:
- turns JsonFusion streaming from "similar to RapidJSON SAX"
into **roughly 2× faster than a hand-written RapidJSON SAX handler**,
- **without** any SIMD or hand-rolled tokenizer tricks (just a clean, forward-only parser).

(Benchmarks run on specific hardware with specific compiler flags; your mileage may vary. The key point is not the exact multiplier, but that type-level intent enables optimizations that are structurally impossible for generic parsers.)

You can’t express this optimization in a generic SAX API without writing your own tokenizer. JsonFusion can do it because:

    The schema is a set of C++ types and annotations, not just a runtime callback.

Types give the compiler precise hints about what work is unnecessary.

**And the performance was never a goal. We just wanted to stop writing absurd boilerplate.**

## Compile-Time Reflection and Modern C++

JsonFusion relies on compile-time reflection in the broad sense: the ability to inspect and manipulate types at compile time in order to generate specialized code.

Today, this is built on top of libraries like Boost.PFR, which can:
- treat aggregates as tuples at compile time,
- iterate over fields,
- and give you field counts/types without manual registration.

Boost.PFR does have limitations: it only works with aggregates (no user-defined constructors), doesn't support private/protected fields, and has no direct support for inheritance. However, these limitations align naturally with the use case of JSON data models. Configuration and data-transfer structures are almost always plain aggregates: public fields, default construction, no complex invariants or encapsulation. These are *data*, not *objects* in the OOP sense — they're meant to be transparent bags of values that map directly to JSON structure. For this domain, PFR's constraints are not limitations; they're actually a good match for the problem space.


C++26 standardizes native reflection and better structured bindings, e.g.:
- static reflection [P2996](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html) — adopted into C++26 and already usable in JsonFusion today with GCC 16 (`-std=c++26 -freflection`)
- variadic structured bindings [P1061R10](https://isocpp.org/files/papers/P1061R10.html)

These are all pushing in the same direction:
Make it easy and standard to introspect aggregates and generate code at compile time.

For JsonFusion, the details don’t change the philosophy:
- whether the library uses Boost.PFR tricks,
- PFR with variadic structured bindings,
- or standard C++26 reflection (available today on GCC 16),

the user-facing story is the same:

```c++
Config cfg{};
Parse(cfg, json);  // the struct *is* the schema
```

The PFR usage is well-isolated in JsonFusion. Basically, all we need is: count a struct's fields, for each field get its name and type at compile time, and get field reference by compile-time index at runtime.

Recent versions of Boost.PFR can extract field names through some rather ingenious (if admittedly hacky) compile-time tricks. It's not standard-blessed, but it's proven reliable enough to sit in Boost, which gives it a certain level of trust.

For people from other ecosystems, it's actually quite a surprise that this task is so difficult in the C++ world.

## What Moves to Compile Time (And Why That’s Good)

When the parser is driven by types instead of dynamic metadata, a lot of work happens at compile time:
- Field count and order
- Field types and nested structures
- Which fields are required / optional
- Constraints (ranges, min/max items, string lengths, etc.)
- Container kinds (fixed std::array vs dynamic std::vector)
- Annotations like skip or “tokenize-only, no materialization”

At runtime, the parser doesn’t “figure out the schema”, it just:
- walks the JSON once,
- performs structural checks and conversions based on a fixed plan,
- and populates your C++ object directly.

This has several concrete benefits:

1. Optimization Opportunities

    Because everything is known at compile time, the compiler can:
    - inline the entire parse path,
    - delete unused branches (if constexpr),
    - constant-fold offsets and sizes,
    - DCE unused features (e.g. map support if you never use maps),
    - share common helper code when code size is more important than inlining.

    **You effectively get a per-type generated parser without running an external code generator.**

2. Better Code Size Scaling

    In well-structured template code, binary size typically scales with:

        the semantic complexity of your models
        (how many fields/containers you have),

    not

        the depth or cleverness of your metaprogramming.

    That’s ideal for embedded:
    - a small config struct pulls in very little code,
    - adding more models adds code roughly proportional to how much new structure you describe,
    - unused features (like dynamic maps, or certain annotations) don’t land in the binary at all.

3. Static Memory and Predictability

    For embedded systems, you often want:
    - No heap,
    - Fixed-size buffers,
    - Known stack usage,
    - Deterministic behavior.

JsonFusion works naturally with things like:

```c++
using SmallString = std::array<char, 64>;
using Motors     = std::array<Motor, 4>;

struct Controller {
    SmallString name;
    Motors      motors;
    // ...
};
```

At compile time, the compiler knows:
- exact sizes and alignments,
- that no dynamic allocation is needed,
- that the parser only ever touches these static buffers.

This is very different from a DOM-based approach, where:
- you allocate nodes,
- build a tree,
- then walk it again to map into C++ types.

**With JsonFusion, mapping and validation happen while parsing, in one pass, with no DOM in between.**

## Types Propagate: Unexpected Benefits

When you commit to "types as schema," benefits ripple into unexpected places. Error reporting is a concrete example.

### Discovery: Types Enable Zero-Overhead Error Paths

How do you report `$.statuses[3].user.name` instead of "error at byte 45231"? Traditional approaches allocate dynamically (`std::vector<PathElement>`), pre-allocate wastefully (128-level fixed buffer), or skip path tracking entirely.

**But we have types.** Walk the type structure at compile time to calculate maximum schema depth:

```cpp
consteval std::size_t calc_type_depth<Type, SeenTypes...>()
```

For a `TwitterData` structure, the compiler determines: "worst case is 8 levels deep" and allocates `std::array<PathElement, 8>` on the stack. **No runtime allocation. No arbitrary limits. The type told us the exact size.** Cyclic recursive types are detected automatically; only then does a macro flag switch to dynamic allocation.

Similar analysis detects map-like containers (`schema_analyzis::has_maps<T>()`). If your model has no maps, the path tracking needs no key buffers—buffer size is set to 1 byte. **Pay for what you use, automatically, with no configuration.**

### The Cascading Benefits

1. **Zero overhead**: Path tracking on success is just counter increments in a stack array + std::string_view assignments
2. **Constexpr-compatible**: Error paths work in compile-time tests (`static_assert`)
3. **Type-safe messages**: `$.statuses[3].user.name` maps directly to your struct definitions
4. **Programmatic navigation**: `visit_by_path()` provides type-safe access to any location—a compile-time-aware analog to `std::variant::visit`, useful for applying defaults or inspecting partial state after errors
5. **Automatic optimization**: Simple structs get 3-4 level stacks; deep structures get what they need

None of this required configuration. **The types contained all the information** for sophisticated error reporting with zero runtime overhead.

This is the deeper promise: when you encode intent in types, the compiler becomes your ally in ways you didn't predict. Features seemingly unrelated to parsing (error infrastructure, path navigation) naturally derive optimal implementations from the same type information. There are no global knobs because they're simply not needed.

## Embedded Philosophy: Pay for What You Use

JsonFusion is intentionally structured so that features are pay-as-you-go:
- Only use fixed-size containers?
        → dynamic container handling code is never instantiated.
- Only use basic scalars and arrays?
        → map/object extras don’t get pulled in.
- Don’t use annotations beyond a few simple ranges?
        → constraint machinery beyond that doesn’t appear in the binary.

This makes it a good fit for:
- microcontrollers with reasonable C++ toolchains (Cortex-M4+, ESP32, RP2040, etc.),
- firmware that needs JSON for configs or telemetry,
- systems where you want type safety and validation without carrying a DOM or reflection runtime.

The “types as intent” story is especially important here: in an MCU, you really want to skip all unnecessary work and storage. The skip example on canada.json is exactly that philosophy applied to a big dataset:
- Just by saying “I don’t care about these numbers” in the type system,
- you let the compiler erase a huge amount of work in the generated parser.


## Where Does the Source of Truth Actually Live?

Whenever you suggest using C++ types as configuration models, the same objections pop up:
- "But then everything is hardcoded."
- "But we’ll have to recompile whenever the config changes."
- "But I don’t want limits (like max columns) baked into templates."

Underneath all of these is one simple fact that is easy to forget:

There is always a fixed, limited “truth” somewhere in your system.
You can move it around, but you can’t make it disappear.

Even in the most dynamic setups:
- Python scripts that rewrite themselves,
- hot-reloaded configs,
- plugins discovered at runtime,

at some level there is still a concrete set of data structures and algorithms that implement the behavior. Those are fixed in the binary you ship right now. Flexibility is always relative to something.

### “But everything is hardcoded!”

Your core application data structures and algorithms are already hardcoded. That’s what code is.

When you describe a config as a C++ struct:

```c++
struct TableConfig {
    std::size_t max_columns;
    bool        allow_nulls;
    // ...
};
```

you are not “hardcoding the config”; you are hardcoding the shape and rules of the config:
- what fields exist,
- what types they have,
- what constraints they obey.

The values still come from JSON/TOML/YAML/whatever at runtime. The user can still change max_columns in a file. You’re just saying, “there is such a concept as max_columns, and it is an integer, and it lives here.”

That is not less flexible than reading root["max_columns"] out of a DOM. **It’s more honest**: the shape is visible in one place, instead of implicitly encoded in a jungle of accessor calls.

### “But we don’t want to recompile every time”

If your config schema is genuinely changing so often that compiling a tiny translation unit with a few structs is your bottleneck, that’s not really a JSON vs templates problem. That’s a sign your domain model is still actively evolving, and you’d probably feel that pain no matter how you read the data.

In a stable system, the shape of the config changes rarely. And when it does, you usually want the compiler on your side to tell you:
- where those fields are used,
- which code paths need updates,
- what assumptions are now invalid.

A strongly typed config is exactly what gives you that.

### “But now limits are ‘baked into templates’”

Example objection:

“I encoded max column count in my template-based model and now it propagated everywhere — I can’t change it without refactoring.”

The real issue there is where the limit is defined, not that templates exist.

If something like kMaxColumns is truly a global architectural limit, it should be:
- defined once, in a meaningful place (config header, traits, etc.),
- reused both in your parsing model and in your algorithms.

```c++
inline constexpr std::size_t kMaxColumns = 16;

struct TableConfig {
    std::array<Column, kMaxColumns> columns;
    // ...
};

// somewhere else
static_assert(kMaxColumns <= 64, "UI grid only supports up to 64 columns");
```

If you instead hardcode 16 in three different files, you are not “more flexible” because it’s not in a template — you are just more fragile. The problem is duplication, not compile-time vs run-time.

**Templates don’t force you to scatter constants; they actually make it easier to propagate a single constant everywhere in a type-safe way.**


### “Config” vs “Code”: You Can’t Outsmart Reality

There’s a popular illusion that you can get more flexibility by:
1.	Making your configs very dynamic and untyped (big JSON blobs), and
2.	Walking a DOM with a forest of accessor calls and if/switch logic.

In reality, that just means:
- the source of truth moved from a declarative place (a schema or a struct)
- into an imperative forest of parsing and dispatch code.

You didn’t eliminate constraints. You just hid them:
- “We support up to 16 columns” is now implicit in how many elements you push into a vector,
- “this field is required” is now implicit in a branch that throws/logs if it’s missing,
- “these two options are mutually exclusive” is now encoded in some if deep in the mapper.

From an architectural point of view, that’s much worse:
- harder to audit,
- harder to test in isolation,
- harder to evolve safely.

A declarative schema — whether it’s a .proto file, a JSON schema, or a set of C++ structs + annotations — is simply a decision to make the source of truth explicit and local:
- “Here is what the config can express.”
- “Here are the constraints.”
- “Here is how it maps to my domain types.”

Everything else (parsing, validation, defaulting) should ideally be generic machinery that follows that declaration, not a custom handwritten state machine for every project.

At some point the real world always reappears in the form of finite CPU, finite memory, and finite
human attention; declarative models don’t remove those limits, but they make them visible – 
you can see what is actually hardcoded, what is negotiable, and how to reshape the architecture 
if you really do need more flexibility


### The Architectural Point

Stepping back from JSON and C++ specifics, the general pattern looks like this:
- Good architectures have clear, concentrated sources of truth:
- one place that defines what data looks like,
- one place that defines the invariants,
- one place that defines the limits.
- Bad architectures smear those truths across:
- config files,
- ad-hoc mapping code,
- scattered constants,
- undocumented assumptions.

Using C++ templates or structs as configuration models isn’t about “hardcoding configs”. 
It’s about acknowledging that:
- the shape of your data is part of your code,
- pretending otherwise doesn’t add real flexibility,
- and having a declarative, type-checked description of that shape usually makes the system more maintainable, not less.

You don't gain flexibility by pushing everything into a flexible JSON blob and then rebuilding the schema imperatively in a forest of accessors. You just move the source of truth to the noisiest possible place.

Declarative models – whether written in C++ types, external IDLs, or schemas – are an attempt to keep that truth:
- visible,
- local,
- and free from unrelated control flow.

Everything else is just plumbing.

### When C++ Types Should NOT Be the Source of Truth

None of this means that "everything must be C++ templates and structs, always". Context matters enormously.

**The type-driven approach described here is primarily for C++-centric applications**: desktop tools, embedded firmware, native services, or systems where C++ is the dominant implementation language and the natural place for the domain model to live.

**In heterogeneous, polyglot systems** — where C++, Rust, Python, JavaScript, Go, and databases all need to agree on shapes and protocols — **keeping the source of truth in C++ types would be actively wrong**. These systems need an external, language-neutral schema as the single source of truth: Protobuf definitions, OpenAPI specs, JSON Schema, FlatBuffers IDL, or similar. In that world:
- The `.proto` or schema file is the one declarative description everyone shares,
- Generated C++ types are just one of several language projections of that truth,
- C++ is a consumer of the schema, not its owner.

This is the correct design for cross-language systems, and nothing in this document argues against it. The problem isn't that the schema is external; the problem starts when you then build yet another "domain model" and more mapping layers on top of the generated types **and pretend those extra layers are free**. If you need external schemas for interoperability, embrace them fully — don't add unnecessary indirection on top.

**But even when external schemas are the source of truth**, the choice between code generation and manual declarative models is not as obvious as it seems. The conventional wisdom is: external schema → generate C++ types → use them everywhere. Code generation ensures mechanical correctness by construction.

**In practice, this often doesn't work out cleanly.** Generated types from OpenAPI, or JSON Schema generators often end up verbose, awkward, or carry baggage from the generator: special accessor methods, required inheritance hierarchies, unwieldy optional wrappers, naming conventions that don't match your codebase style. As a result, many projects end up building yet another layer:

```
External Schema (the true contract)
  ↓
Auto-generated C++ types (mechanically correct but ugly)
  ↓
Manual mapping/wrapper layer (because generated types aren't nice to use)
  ↓
"True internal" domain models (what you actually want to work with)
```

Once you've committed to that manual mapping layer, the "mechanical correctness" argument is already weakened. Code generation ensures the generated types match the schema, yes—but it doesn't ensure **you use them correctly** in your mapping code. That still requires discipline, testing, and manual updates when the schema changes.

At that point, an alternative becomes viable:

```
External Schema (still the contract, still the interop source of truth)
  ↓
Manually maintained declarative C++ models (using JsonFusion or similar)
  (no intermediate generated types, no runtime mapping layer)
```

**Why might this be simpler?**

1. **One layer instead of two.** You maintain the external schema (for interop, documentation, contracts) and clean C++ structs (for internal use). No awkward generated middle layer. No mapping code between two C++ type systems.

2. **When the schema changes** — not just a field rename, but a structural change (nested object moves, array becomes object, field splits into two):
   - With generated+mapping: Regenerate types (potentially breaking existing code), then update all the mapping code, hope you didn't miss anything.
   - With declarative models: Update the struct shape once. The compiler immediately shows every place that accessed `cfg.network.timeout` and now needs to use `cfg.services.api.timeout`. Compiler enforcement, not grep-and-hope, because your domain model is what your code actually talks to, not a generated adapter type..

3. **Cleaner internal code.** You work with natural C++ types that match your domain, not whatever conventions the code generator imposes.

The trade-off is honest: you give up the automatic synchronization that code generation provides. Keeping the declarative C++ models in sync with the external schema is now your responsibility, enforced through testing and validation. But **if you were already writing and maintaining manual mapping code**, you haven't actually given up much—you've just moved where the manual work happens, and arguably made it simpler and safer (declarative structures + compiler checking vs. imperative mapping logic + runtime testing).

This isn't a universal rule. If your generated types are ergonomic enough to use directly throughout your codebase, use them. But if you find yourself building "models for models" on top of generated code, consider whether manually maintained declarative structs might be less overall complexity.

Likewise, there are plenty of situations where a quick, messy DOM walk is the correct answer.
Not everything justifies a fully modeled schema. One-off tools, tiny admin utilities, scripts that
inspect or tweak a couple of fields and then exit — here, a bit of `json["foo"]["bar"]` is perfectly
fine, even healthy. The trouble is when this style quietly becomes the default for everything,
including core configuration paths and long-lived subsystems, just because it was easy the first time.
Context and scale matter: what is a pragmatic shortcut in a 200-line tool becomes architectural debt in
a 200k-line service.

And, yes, the “types as schema” approach has its own very real costs. Wrapping fields in a templated
Annotated<T, …> or similar to attach metadata is a workaround for language limitations, not some
aesthetic ideal. It’s powerful, but it’s not everyone’s idea of readable C++. Compile times can suffer;
error messages can be intimidating; some developers simply don’t want to think in heavy templates and
constexpr logic. That’s a legitimate concern. The point is not that this style is universally “better”,
but that for certain domains – configuration, protocols, structured data – the payoff in clarity,
validation and optimization can justify the complexity, if you manage it carefully.

Underneath all of this is a simple belief: in a typed language, types are your strongest tool, not a
nuisance to work around. The compiler, the IDE, static analysis, refactoring tools, and your own mental
model of the architecture all lean on types as the backbone. If you push essential truths about your
data and invariants out of the type system and into ad-hoc JSON accessors and runtime checks, you lose
a lot of that help. It is almost always better for C++ code to “not build” with a clear compile-time
error than to compile happily and then misinterpret a config at runtime. Using types (and, when
necessary, templates) as the place where you state “what is allowed” is really just a way of letting
the compiler enforce the same truths you already rely on in your head.


## Trade-offs and Limitations (Honest Edition)

This approach has real upsides, but also real costs.

✅ Upsides
- No unmaintainable boilerplate.
- Performance competitive with (or exceeding) hand-written parsing + mapping.
- Strong type safety: the struct is the schema.
- No DOM, no runtime reflection, no virtual dispatch.
- Static memory usage; very friendly to embedded systems.
- Ability to express semantic intent (skip, ranges, etc.) that drives real optimizations.
- Validation and constraints are checked during parsing, not as a separate step.

JsonFusion provides rich error reporting with:
- Current input iterator (precise byte offset in the JSON stream)
- JSON path tracking (e.g., `$.statuses[3].user.name`, encoded inside a `Path` object)
- Parse error codes
- Validator error codes with failed validator option index

The path tracking is compile-time sized based on schema depth analysis, avoiding runtime allocation overhead. This works in
both runtime and constexpr contexts. Schemas with recursive types can be handled: non-cyclic recursive structures use
fixed-size stacks, while cyclic structures can explicitly enable `std::vector` usage via macro option for dynamic path
tracking.

⚠️ Trade-offs
- `Annotated` is clearly a workaround, a way to attach metainfo to types. We just don't have a less-intrusive way in the language to create something like annotations, which behaves the way we need.

- **Compile times**: Heavy template instantiation and constexpr logic are not free. This is a real cost and can be 
significant in large projects. However, JsonFusion's design helps contain this: all template instantiation happens 
at `Parse()` and `Serialize()` call sites, which are naturally scoped. You can easily isolate these calls in separate
translation units (e.g., a dedicated `config_parser.cpp`), preventing the template machinery from "poisoning" your entire
codebase. The rest of your code just works with the plain C++ types. That said, if you have dozens of complex models and
parse them in many places, compile times will add up — there's no magic solution, just good hygiene about where and how
often you instantiate the machinery.

- Binary size: poor factoring or excessive inlining can bloat code if you're not careful (JsonFusion is designed to avoid this, but physics still apply). With proper linker optimization (LTO) and thoughtful model design, binary size typically scales with the semantic complexity of your models rather than the depth of metaprogramming. Still, each instantiation does generate code, and you'll want to monitor this on resource-constrained platforms.

- Toolchain requirements: you want a reasonably modern C++ compiler with good optimization and template support. C++23 is recommended for the cleanest syntax (especially non-type template parameters for string literals).

- **Debugging and error messages**: Template error messages can be extremely verbose — we're talking hundreds of lines pointing deep into library internals. While C++23 concepts and improved diagnostics help, this remains a genuine pain point. Stepping through template-heavy parsing code in a debugger is also much harder than debugging straightforward procedural code. You often need to rely on static_asserts and compile-time diagnostics rather than runtime debugging. If your team isn't comfortable with template debugging techniques, this can be a significant barrier to adoption.

- **Foreign types in your domain structures**: The `Annotated<T, ...>` wrapper is lightweight (~200 lines of code), purely optional, and straightforward to remove if you define types via `using MyType = A<T, ...>;` aliases. But it's still a templated wrapper type in your core data structures. It's not the underlying `T` — it's a distinct type with conversion operators to behave similarly (but not identically) to `T`. This is a deliberate architectural decision: putting template machinery into your structs. For some codebases, this is unacceptable; for others, it's a reasonable trade-off for declarative metadata. Either way, it's not a trivial choice.

- **What users actually need to know**: To define and use models, you don't need to understand template metaprogramming. The public API is intentionally simple:
  ```cpp
  struct Config {
      A<int, key<"port">, range<1, 65535>> port;
      std::string hostname;  // plain types work fine
  };
  Config cfg;
  JsonFusion::Parse(cfg, jsonData);
  ```
  This is comparable to annotated models in Python, Java, or C# — attach metadata to fields, let tools use it. Junior developers can add fields by following existing patterns.
  
  The real barriers are practical, not conceptual:
  1. **Template error messages**: Type mismatches in nested structures produce error waterfalls (hundreds of lines pointing into library internals). We mitigate this with concepts and early static_asserts, but it's still painful compared to simple runtime errors with clear messages.
  2. **Mental model shift**: Thinking of the struct as the schema itself, not just storage filled by separate imperative code. This is more of a perspective change than a technical hurdle, but it's real.
  
  That said, compare the alternative: imperative DOM traversal code that compiles fine when you make mistakes, then fails at runtime (if you're lucky) or silently corrupts data (if you're not). When a field moves or a type changes, JsonFusion gives you compile errors showing exactly what's affected. In imperative mapping code, you hunt through forests of accessors hoping you didn't miss one.


## Looking Forward

The broader C++ ecosystem is clearly moving toward:
- richer compile-time computation,
- standard reflection,
- and more type-driven design.

JsonFusion is built around that trajectory — and as of 2026, that future has arrived:
- The C++20/23 path: Boost.PFR + templates + constexpr to generate specialized parsers from your types.
- The C++26 path: the same philosophy on top of standard reflection — available today with GCC 16 (`-std=c++26 -freflection`), with zero external dependencies.

The core idea doesn’t change:

    Give the compiler as much information as possible about what you want, as early as possible.
    Then let it erase everything you don’t need.

High-level types and annotations aren't an obstacle to performance and flexibility — they're the source
of the best optimizations, because they encode what the program is allowed to ignore and provide the 
source of truth for data structures and constraints.