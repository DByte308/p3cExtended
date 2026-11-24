#!/usr/bin/env python3
"""Fullscreen look-here targets for gaze checks. targets.py <log.csv> [start_delay_s]
Shows a dot at known screen positions for 4 s each and logs  wall_time,name,nx,ny  (nx,ny in -1..1, +x right, +y up)
at the start/end of each phase. View it in the headset through WayVR (desktop overlay)."""
import sys, time, tkinter as tk
POS = [("centre", 0, 0), ("left", -.8, 0), ("right", .8, 0), ("up", 0, .8), ("down", 0, -.8),
       ("up-left", -.7, .7), ("up-right", .7, .7), ("down-left", -.7, -.7), ("down-right", .7, -.7), ("centre2", 0, 0)]
SECS, GAP = 4.0, 0.0
log = open(sys.argv[1], "w"); delay = float(sys.argv[2]) if len(sys.argv) > 2 else 0
root = tk.Tk(); root.attributes("-fullscreen", True); root.configure(bg="black")
c = tk.Canvas(root, bg="black", highlightthickness=0); c.pack(fill="both", expand=True)
i = [-1]; t_end = [0.0]
def show(idx):
    c.delete("all"); w, h = root.winfo_width(), root.winfo_height()
    if idx >= len(POS):
        c.create_text(w/2, h/2, text="done", fill="white", font=("sans", 60)); root.after(1500, root.destroy); return
    name, nx, ny = POS[idx]; x, y = w/2 + nx*w/2*0.9, h/2 - ny*h/2*0.9
    c.create_oval(x-40, y-40, x+40, y+40, fill="white"); c.create_oval(x-8, y-8, x+8, y+8, fill="red")
    c.create_text(w/2, 60, text=f"look at the dot: {name}", fill="white", font=("sans", 40))
    log.write(f"{time.time():.3f},{name},{nx},{ny}\n"); log.flush()
    root.after(int(SECS*1000), lambda: show(idx+1))
def start():
    c.create_text(root.winfo_width()/2, root.winfo_height()/2, text="get ready", fill="white", font=("sans", 60)); root.after(int(delay*1000), lambda: show(0))
root.after(300, start); root.mainloop()
