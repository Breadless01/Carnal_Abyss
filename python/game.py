import engine

def on_event(name, a=0, b=0, c=0):
    if name == "resize":
        engine.log(f"resize {a}x{b}")
    elif name == "quit":
        engine.log("quit event")

def update(dt: float):
    # pro Frame
    # engine.log(f"dt={dt}")  # optional
    pass

# DEBUG muss NACH den defs stehen
engine.log(f"DEBUG type(update) = {type(update)}")
engine.log(f"DEBUG type(on_event) = {type(on_event)}")
