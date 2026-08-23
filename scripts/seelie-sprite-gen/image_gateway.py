#!/usr/bin/env python3
"""Call a local image-generation gateway. Credentials come from env or a
gitignored local config file — never commit secrets or vendor hostnames.

Outputs lossless PNG by default (output_format=png). If the model returns RGB
without alpha, optional chroma-key matte runs on the lossless PNG — never a
JPEG→PNG conversion roundtrip.

Env (preferred):
  SEELIE_IMAGE_API_BASE   e.g. https://example.invalid
  SEELIE_IMAGE_API_KEY
  SEELIE_IMAGE_API_PATH   default: /api/gateway/v1beta/models/images/generations/Seedream-5.0-Lite
  SEELIE_IMAGE_API_MODEL  default: doubao-seedream-5-0-260128

Or create scripts/seelie-sprite-gen/image_gateway.local.json (gitignored):
  { "base": "...", "key": "...", "path": "...", "model": "..." }
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

from matte import auto_matte, transparency_ratio

ROOT = Path(__file__).resolve().parent
LOCAL_CFG = ROOT / "image_gateway.local.json"
DEFAULT_PATH = "/api/gateway/v1beta/models/images/generations/Seedream-5.0-Lite"
DEFAULT_MODEL = "doubao-seedream-5-0-260128"


def load_cfg() -> dict:
    cfg: dict = {}
    if LOCAL_CFG.exists():
        cfg = json.loads(LOCAL_CFG.read_text(encoding="utf-8"))
    base = os.environ.get("SEELIE_IMAGE_API_BASE") or cfg.get("base")
    key = os.environ.get("SEELIE_IMAGE_API_KEY") or cfg.get("key")
    path = os.environ.get("SEELIE_IMAGE_API_PATH") or cfg.get("path") or DEFAULT_PATH
    model = os.environ.get("SEELIE_IMAGE_API_MODEL") or cfg.get("model") or DEFAULT_MODEL
    if not base or not key:
        raise SystemExit(
            "Missing image API credentials. Set SEELIE_IMAGE_API_BASE + "
            "SEELIE_IMAGE_API_KEY, or create image_gateway.local.json "
            "(see image_gateway.local.json.example)."
        )
    return {"base": base.rstrip("/"), "key": key, "path": path, "model": model}


def post_json(url: str, key: str, body: dict, timeout: int = 180) -> dict:
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        err = e.read().decode("utf-8", errors="replace")
        raise SystemExit(f"HTTP {e.code}: {err[:800]}") from e
    except Exception as e:
        raise SystemExit(f"request failed: {type(e).__name__}: {e}") from e


def download(url: str, dest: Path) -> None:
    with urllib.request.urlopen(url, timeout=120) as resp:
        dest.write_bytes(resp.read())


def image_to_data_url(path: Path) -> str:
    raw = path.read_bytes()
    ext = path.suffix.lower()
    if ext == ".png":
        mime = "image/png"
    elif ext in (".jpg", ".jpeg"):
        mime = "image/jpeg"
    else:
        mime = "image/png"
    return f"data:{mime};base64,{base64.b64encode(raw).decode('ascii')}"


def redact(obj) -> str:
    s = json.dumps(obj, ensure_ascii=False)[:1200]
    s = re.sub(r"https?://[^\s\"']+", lambda m: m.group(0)[:48] + "...", s)
    return s


def extract_url(data: dict) -> str | None:
    if isinstance(data.get("data"), list) and data["data"]:
        first = data["data"][0]
        url = first.get("url") or first.get("image_url")
        if url:
            return url
    if isinstance(data.get("data"), dict):
        urls = data["data"].get("image_urls") or data["data"].get("urls")
        if urls:
            return urls[0]
    url = data.get("url") or data.get("image_url")
    if url:
        return url
    m = re.search(r"https://[^\s\"']+\.(?:png|jpg|jpeg|webp)", json.dumps(data))
    return m.group(0) if m else None


def finalize_png(dest: Path, *, matte: str) -> None:
    from PIL import Image

    im = Image.open(dest)
    if matte == "green":
        im = auto_matte(im)
    elif im.mode != "RGBA":
        im = im.convert("RGBA")
    im.save(dest, format="PNG", optimize=True)
    ratio = transparency_ratio(im)
    print(f"saved {dest} ({im.size[0]}x{im.size[1]} RGBA, transparent={ratio*100:.1f}%)")


def cmd_probe(args: argparse.Namespace) -> None:
    cfg = load_cfg()
    url = cfg["base"] + cfg["path"]
    body = {
        "model": cfg["model"],
        "prompt": "a simple red apple on chroma green screen #00FF00 background",
        "response_format": "url",
        "size": args.size,
        "stream": False,
        "watermark": False,
        "output_format": args.format,
    }
    print(f"POST {cfg['base']}{cfg['path']} format={args.format}")
    data = post_json(url, cfg["key"], body)
    print(redact(data))


def cmd_gen(args: argparse.Namespace) -> None:
    cfg = load_cfg()
    url = cfg["base"] + cfg["path"]
    out_fmt = args.format.lower()
    body: dict = {
        "model": cfg["model"],
        "prompt": args.prompt,
        "response_format": "url",
        "size": args.size,
        "stream": False,
        "watermark": False,
        "output_format": out_fmt,
    }
    if args.ref and args.ref_mode != "none":
        data_url = image_to_data_url(Path(args.ref))
        if args.ref_mode == "image_urls":
            body["image_urls"] = [data_url]
        elif args.ref_mode == "images":
            body["images"] = [data_url]
        else:
            body["image"] = data_url

    print(f"gen ref={bool(args.ref)} mode={args.ref_mode} format={out_fmt}")
    data = post_json(url, cfg["key"], body, timeout=180)
    print(redact(data))
    img_url = extract_url(data)
    if not (args.out and img_url):
        raise SystemExit("no image url in response; cannot save")

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    # Download to temp then finalize so we never keep a mislabeled JPEG as PNG.
    tmp = dest.with_suffix(dest.suffix + ".part")
    download(img_url, tmp)
    if out_fmt == "png":
        tmp.replace(dest)
        finalize_png(dest, matte=args.matte)
    else:
        tmp.replace(dest)
        print(f"saved {dest}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Seelie sprite image gateway client")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("probe")
    p.add_argument("--size", default="2k")
    p.add_argument("--format", default="png", choices=["png", "jpeg"])
    p.set_defaults(func=cmd_probe)

    g = sub.add_parser("gen")
    g.add_argument("--prompt", required=True)
    g.add_argument("--out", required=True)
    g.add_argument("--ref", default="")
    g.add_argument(
        "--ref-mode",
        default="image",
        choices=["image", "image_urls", "images", "none"],
    )
    g.add_argument("--size", default="2k", help="2k (2048) or 3k")
    g.add_argument("--format", default="png", choices=["png", "jpeg"])
    g.add_argument(
        "--matte",
        default="green",
        choices=["green", "none"],
        help="chroma-key matte on lossless PNG when model returns RGB (default: green)",
    )
    g.set_defaults(func=cmd_gen)

    args = ap.parse_args()
    if args.cmd == "gen" and args.ref_mode == "none":
        args.ref = ""
    args.func(args)


if __name__ == "__main__":
    main()
