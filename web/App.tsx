import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";

interface GameEntry {
  id: number;
  name: string;
}

type Phase = "init" | "ready" | "error";

// Faro milestone 2 (see GUI_redesign_investigacion.md): real game library -
// `games` comes from CafeTitleList through the Rust bridge (same source the
// wx game list shows), not a mock. Clicking a card fires launch_title,
// which opens the title in its own native GLFW window (Vulkan) on a
// dedicated C++ thread - fire and forget, the Tauri window stays usable.
function App() {
  const [phase, setPhase] = useState<Phase>("init");
  const [coreVersion, setCoreVersion] = useState<string>("");
  const [games, setGames] = useState<GameEntry[]>([]);
  const [launchingId, setLaunchingId] = useState<number | null>(null);
  const [launchError, setLaunchError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const version = await invoke<string>("get_core_info");
        if (cancelled) return;
        setCoreVersion(version);
        const ok = await invoke<boolean>("core_init");
        if (cancelled) return;
        if (!ok) {
          setPhase("error");
          return;
        }
        const list = await invoke<GameEntry[]>("get_game_list");
        if (cancelled) return;
        setGames(list);
        setPhase("ready");
      } catch {
        if (!cancelled) setPhase("error");
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  const onLaunch = async (id: number) => {
    setLaunchingId(id);
    setLaunchError(null);
    try {
      const ok = await invoke<boolean>("launch_title", { id });
      if (!ok) setLaunchError("Ya hay un juego abierto en otra ventana.");
    } catch (err) {
      setLaunchError(String(err));
    } finally {
      setLaunchingId(null);
    }
  };

  const toHex = (id: number) =>
    `00000000${id.toString(16)}`.slice(-16).toUpperCase();

  return (
    <main className="min-h-screen bg-neutral-950 text-neutral-100">
      <header className="border-b border-neutral-800 px-8 py-5">
        <p className="text-xs font-medium uppercase tracking-widest text-neutral-500">
          Faro · Wii U
        </p>
        <div className="mt-1 flex items-baseline gap-3">
          <h1 className="text-2xl font-semibold text-white">Biblioteca</h1>
          <span className="font-mono text-xs text-neutral-500">
            {coreVersion || "…"}
          </span>
          <span className="ml-auto text-sm text-neutral-400">
            {phase === "ready" ? `${games.length} juegos` : "cargando…"}
          </span>
        </div>
      </header>

      <div className="px-8 py-6">
        {phase === "error" && (
          <div className="rounded-lg border border-red-900 bg-red-950/40 p-4 text-sm text-red-300">
            No se pudo inicializar el núcleo (revisa la ruta del MLC y los
            juegos en Ajustes del Cemu portable).
          </div>
        )}

        {phase === "init" && (
          <p className="text-sm text-neutral-500">
            Inicializando núcleo y buscando juegos…
          </p>
        )}

        {phase === "ready" && games.length === 0 && (
          <p className="text-sm text-neutral-500">
            No se encontraron juegos. Agrega rutas de búsqueda como en el
            Cemu wx y reinicia.
          </p>
        )}

        {launchError && (
          <div className="mb-4 rounded-lg border border-amber-900 bg-amber-950/40 p-3 text-sm text-amber-300">
            {launchError}
          </div>
        )}

        <div className="grid grid-cols-2 gap-4 md:grid-cols-3 lg:grid-cols-4">
          {games.map((g) => (
            <button
              key={g.id}
              onClick={() => onLaunch(g.id)}
              disabled={launchingId !== null}
              className="group rounded-xl border border-neutral-800 bg-neutral-900 p-5 text-left shadow transition hover:border-emerald-700 hover:bg-neutral-850 disabled:opacity-60"
            >
              <div className="flex h-16 items-center justify-center rounded-lg bg-neutral-950 text-3xl">
                🎮
              </div>
              <p className="mt-3 truncate text-sm font-medium text-white group-hover:text-emerald-300">
                {g.name || "(sin nombre)"}
              </p>
              <p className="mt-1 font-mono text-xs text-neutral-500">
                {toHex(g.id)}
              </p>
              <p className="mt-2 text-xs text-neutral-500">
                {launchingId === g.id ? "Abriendo…" : "Click para jugar →"}
              </p>
            </button>
          ))}
        </div>
      </div>
    </main>
  );
}

export default App;
