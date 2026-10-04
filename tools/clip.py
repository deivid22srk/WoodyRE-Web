"""Records a video clip of the port with its own sound: fixed 1/25 s frames (WOODY_FIXDT), a screenshot per frame
(WOODY_SHOTSEQ) and the mixer's output dumped in step with the frames (WOODY_AUDIODUMP + WOODY_FIXDT = offline mixing),
then muxed into an H.264/AAC MP4 and, with --webp, an animated WebP (silent, for a README).
Usage: python tools/clip.py NAME START COUNT [--webp WIDTH] -- ENGINE ARGS...
  START = seconds on the level clock (the same clock as --shot and WOODY_KEYS), COUNT = frames at 25 fps.
  Writes out/clips/NAME.mp4 (frames and raw audio in out/clips/NAME/). Needs out/woody.exe (build.bat dev),
  Pillow and imageio-ffmpeg (pip install imageio-ffmpeg). Scripted input and other hooks go in the environment.
Examples:
  python tools/clip.py rocket 3.6 132 -- W1A --pos 8845 1160 385 --yaw 180 --peck 0.7 0.1 --jump 6.8
  python tools/clip.py special 0.92 132 -- W2A --pos 7659 1332 -13074 --yaw 0 --special 1.5
  python tools/clip.py intro 23.8 275 -- --newgame --enter 1
  WOODY_SETVAR="1 1 1" python tools/clip.py boss 2.0 210 -- W2B"""
import glob, os, re, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FPS, RATE = 25, 44100

def record(d, name, start, count, args):
    for f in glob.glob(os.path.join(d, "a_*.ppm")): os.remove(f)
    raw = os.path.join(d, "audio.raw")
    env = dict(os.environ, WOODY_FIXDT=str(FPS), WOODY_AUDIODUMP=raw, WOODY_SHOTSEQ=f"{d}/a {start} {1 / FPS} {count}")
    env.pop("WOODY_NOSOUND", None)
    end = start + (count + 2) / FPS                          # --shot ends the run just after the last frame
    out = subprocess.run([os.path.join(root, "out", "woody.exe"), os.path.join(root, "extract", "Data"), *args,
                          "--res", "1280x720", "--shot", os.path.join(d, "end.ppm"), f"{end:.2f}"],
                         cwd=d, env=env, capture_output=True, text=True, errors="replace").stdout
    m = re.search(r"shotseq: frame 0 at audio frame (\d+)", out)
    if not m: sys.exit("no 'shotseq: frame 0' line in the engine's log - did the level reach START?")
    return raw, int(m.group(1))

def mp4(d, out, raw, a0):
    import imageio_ffmpeg
    frames = sorted(glob.glob(os.path.join(d, "a_*.ppm")))
    pcm = open(raw, "rb").read()[a0 * 4:(a0 + round(len(frames) * RATE / FPS)) * 4]   # s16 stereo: 4 bytes a frame
    seg = os.path.join(d, "audio_clip.raw"); open(seg, "wb").write(pcm)
    lst = os.path.join(d, "frames.txt")
    with open(lst, "w") as f:
        for p in frames: f.write(f"file '{p}'\nduration {1 / FPS}\n")
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                    "-f", "s16le", "-ar", str(RATE), "-ac", "2", "-i", seg,
                    "-r", str(FPS), "-c:v", "libx264", "-preset", "slow", "-crf", "20", "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-b:a", "160k", "-shortest", "-movflags", "+faststart", out], check=True)
    print(out, len(frames), "frames", os.path.getsize(out) // 1024, "KB")

def webp(d, out, width):                                     # animated WebP: a fifth of a GIF's size for 3D footage
    from PIL import Image
    fr = [Image.open(f).convert("RGB") for f in sorted(glob.glob(os.path.join(d, "a_*.ppm")))]
    h = round(fr[0].height * width / fr[0].width)
    fr = [f.resize((width, h), Image.LANCZOS) for f in fr]
    fr[0].save(out, save_all=True, append_images=fr[1:], duration=1000 // FPS, loop=0, quality=75, method=4)
    print(out, len(fr), "frames", os.path.getsize(out) // 1024, "KB")

if __name__ == "__main__":
    if "--" not in sys.argv or len(sys.argv[:sys.argv.index("--")]) < 4: sys.exit(__doc__)
    own, args = sys.argv[1:sys.argv.index("--")], sys.argv[sys.argv.index("--") + 1:]
    name, start, count = own[0], float(own[1]), int(own[2])
    width = int(own[own.index("--webp") + 1]) if "--webp" in own else 0
    base = os.path.join(root, "out", "clips"); d = os.path.join(base, name); os.makedirs(d, exist_ok=True)
    raw, a0 = record(d, name, start, count, args)
    mp4(d, os.path.join(base, name + ".mp4"), raw, a0)
    if width: webp(d, os.path.join(base, name + ".webp"), width)
