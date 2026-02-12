#!/usr/bin/env python3
"""
PhantomOS Groq AI Proxy — bridges COM2 serial port to Groq's free LLM API.

Usage:
  1. Launch QEMU with COM2 serial socket:
     qemu-system-x86_64 -cdrom phantomos.iso -m 512 -vga std \
       -hda /tmp/phantomos_data.img \
       -serial file:/tmp/phantomos-serial.log \
       -serial unix:/tmp/phantomos-groq.sock,server=on,wait=off

  2. Run this proxy:
     GROQ_API_KEY=gsk_xxx python3 tools/groq_proxy.py

The kernel sends on COM2: GROQ:user prompt text\n
This proxy returns: SCENE:theme=X;elems=a,b;style=Y;density=N;bright=N;horizon=N;comp=Z;color=RRGGBB\n
On error returns: GROQ_ERR:message\n
"""

import os
import sys
import json
import socket
import urllib.request
import urllib.error

SOCK_PATH = "/tmp/phantomos-groq.sock"
GROQ_URL = "https://api.groq.com/openai/v1/chat/completions"
MODEL = "llama-3.3-70b-versatile"

SYSTEM_PROMPT = """You are a scene descriptor for a pixel art generator. Given a user's art prompt, output EXACTLY one line in this format:
SCENE:theme=THEME;elems=ELEM1,ELEM2,...;style=STYLE;density=N;bright=N;horizon=N;comp=COMP;color=RRGGBB

Available themes: sunset, ocean, forest, desert, space, city, mountain, underwater, arctic, volcano, garden, storm, cave, meadow, ruins, swamp, canyon, island, aurora, nebula, crystal, lava, tundra, savanna, reef, dungeon, palace, sky, valley, jungle
Available elements (pick 2-6): sun, moon, stars, clouds, mountains, trees, water, rocks, flowers, birds, fish, buildings, bridge, boat, castle, lighthouse, crystals, mushrooms, coral, lava, ice, fire, rain, snow
Available styles: normal, pixel, abstract, dreamy, dark, neon, watercolor
Available compositions: panoramic, centered, diagonal, layered
- density: 5-25 (how many scene elements to scatter)
- bright: -3 to +3 (brightness adjustment)
- horizon: 50-250 (horizon line Y position, 0=top 300=bottom)
- color: hex RRGGBB for the dominant accent color

Respond with ONLY the SCENE: line. No explanation."""


def call_groq(prompt: str, api_key: str) -> str:
    body = json.dumps({
        "model": MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "max_tokens": 200,
        "temperature": 0.7,
    }).encode()

    req = urllib.request.Request(
        GROQ_URL,
        data=body,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "User-Agent": "PhantomOS-GroqProxy/1.0",
        },
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        data = json.loads(resp.read())
    text = data["choices"][0]["message"]["content"].strip()
    for line in text.splitlines():
        if line.startswith("SCENE:"):
            return line
    return f"GROQ_ERR:unexpected response: {text[:80]}"


def main():
    api_key = os.environ.get("GROQ_API_KEY", "")
    if not api_key:
        print("Error: set GROQ_API_KEY environment variable", file=sys.stderr)
        sys.exit(1)

    print(f"[groq_proxy] Connecting to {SOCK_PATH} ...")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCK_PATH)
    print("[groq_proxy] Connected. Waiting for GROQ: requests...")

    buf = b""
    while True:
        data = sock.recv(4096)
        if not data:
            print("[groq_proxy] Socket closed.")
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").strip()
            if not text.startswith("GROQ:"):
                continue
            prompt = text[5:]
            print(f"[groq_proxy] Request: {prompt}")
            try:
                response = call_groq(prompt, api_key)
            except urllib.error.HTTPError as e:
                response = f"GROQ_ERR:HTTP {e.code}"
                print(f"[groq_proxy] HTTP error: {e.code}", file=sys.stderr)
            except Exception as e:
                response = f"GROQ_ERR:{str(e)[:60]}"
                print(f"[groq_proxy] Error: {e}", file=sys.stderr)

            print(f"[groq_proxy] Response: {response}")
            sock.sendall((response + "\n").encode())

    sock.close()


if __name__ == "__main__":
    main()
