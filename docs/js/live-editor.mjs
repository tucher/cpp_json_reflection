// ── Lazy-loaded CodeMirror modules ─────────────────────────────────
let cmLoaded = null; // Promise<{ EditorView, EditorState, cpp, oneDark, ... }>

async function loadCodeMirror() {
  if (cmLoaded) return cmLoaded;
  cmLoaded = (async () => {
    const [
      { EditorView, basicSetup },
      { EditorState },
      { cpp },
      { oneDark },
    ] = await Promise.all([
      import("codemirror"),
      import("@codemirror/state"),
      import("@codemirror/lang-cpp"),
      import("@codemirror/theme-one-dark"),
    ]);
    return { EditorView, EditorState, basicSetup, cpp, oneDark };
  })();
  return cmLoaded;
}

// ── Lazy-loaded CompilerEngine singleton ───────────────────────────
let enginePromise = null;

function getEngine() {
  if (enginePromise) return enginePromise;
  enginePromise = (async () => {
    const { CompilerEngine } = await import("./compiler-engine.mjs");
    const engine = new CompilerEngine({
      wasmBaseUrl: `https://raw.githubusercontent.com/tucher/JsonFusion/wasm-v1/wasm-v1/`,
      githubOwner: "tucher",
      githubRepo: "JsonFusion",
      githubBranch: "master",
      workerCount: 2, // fewer workers for docs — save memory
    });
    await engine.init();
    await engine.loadStandardHeaders();
    return engine;
  })();
  return enginePromise;
}

// ── Public API ─────────────────────────────────────────────────────

export async function initLiveEditors(container) {
  const blocks = container.querySelectorAll(".live-editor");
  if (blocks.length === 0) return;

  const cm = await loadCodeMirror();

  for (const block of blocks) {
    // Decode the source code from data attribute
    const code = block.dataset.code
      .replace(/&amp;/g, "&")
      .replace(/&lt;/g, "<")
      .replace(/&gt;/g, ">")
      .replace(/&quot;/g, '"');
    const mode = block.dataset.mode || "eval";
    const resultName = block.dataset.resultName || "__result";

    // Create editor container
    const editorWrap = document.createElement("div");

    // Create CodeMirror editor
    const state = cm.EditorState.create({
      doc: code,
      extensions: [
        cm.basicSetup,
        cm.cpp(),
        cm.oneDark,
        cm.EditorView.lineWrapping,
      ],
    });

    const view = new cm.EditorView({ state, parent: editorWrap });

    // Create toolbar
    const toolbar = document.createElement("div");
    toolbar.className = "live-editor-toolbar";

    const btn = document.createElement("button");
    btn.className = "compile-btn";
    btn.textContent = "\u25B6 Compile";

    const status = document.createElement("span");
    status.className = "compile-status";

    toolbar.appendChild(btn);
    toolbar.appendChild(status);

    // Create result area
    const resultEl = document.createElement("div");
    resultEl.className = "live-editor-result";

    // Assemble
    block.innerHTML = "";
    block.appendChild(editorWrap);
    block.appendChild(toolbar);
    block.appendChild(resultEl);

    // Compile handler
    btn.addEventListener("click", () => {
      onCompile(view, mode, resultName, btn, status, resultEl);
    });
  }
}

async function onCompile(view, mode, resultName, btn, statusEl, resultEl) {
  btn.disabled = true;
  btn.classList.add("running");
  btn.textContent = "\u25B6 Compiling...";
  statusEl.textContent = "";
  resultEl.classList.remove("visible", "pass", "fail");

  try {
    // Lazy init engine — shows status while loading
    let engine;
    try {
      statusEl.textContent = "Loading compiler...";
      engine = await getEngine();
      statusEl.textContent = "";
    } catch (err) {
      statusEl.textContent = "";
      resultEl.textContent = "Failed to load compiler: " + err.message;
      resultEl.classList.add("visible", "fail");
      btn.disabled = false;
      btn.classList.remove("running");
      btn.textContent = "\u25B6 Compile";
      return;
    }

    const code = view.state.doc.toString();

    const result = await engine.compile({
      code,
      path: "/input.cpp",
      mode,
      std: "c++23",
      resultName,
      includeDirs: [],
    });

    if (result.ok) {
      if (mode === "check-only") {
        resultEl.textContent = "\u2713 Compilation successful";
      } else {
        const val = result.result || "(no output)";
        resultEl.textContent = "\u2713 Result: " + val;
      }
      resultEl.classList.add("visible", "pass");
    } else {
      const diag = result.diagnostics || result.error || "Unknown error";
      resultEl.textContent = diag;
      resultEl.classList.add("visible", "fail");
    }

  } catch (err) {
    resultEl.textContent = "Error: " + err.message;
    resultEl.classList.add("visible", "fail");
  }

  btn.disabled = false;
  btn.classList.remove("running");
  btn.textContent = "\u25B6 Compile";
}
