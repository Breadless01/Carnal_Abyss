import engine

_running = True

def init():
    engine.log("game.init()")
    engine.set_window_title("Carnal Abyss (Python gameplay online)")

def on_event(type, a, b):
    engine.log(f"event: {type} {a} {b}")
    if type == "resize":
        # show live size in title
        engine.set_window_title(f"Carnal Abyss | {a}x{b}")
    elif type == "quit":
        global _running
        _running = False

def update(dt):
    # Tiny heartbeat so you know the loop is real.
    # You can remove this once you start doing actual gameplay.
    t = engine.time_seconds()
    if int(t) % 2 == 0:
        engine.log(f"tick t={t:.2f} dt={dt:.4f}")
    if not _running:
        engine.request_quit()

def shutdown():
    engine.log("game.shutdown()")
