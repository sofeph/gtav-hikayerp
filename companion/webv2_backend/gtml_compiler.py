"""GTML Compiler - Static HTML layout extraction for WebV2 thin-client model.

Parses .gtml files (HTML5 + inline CSS) with BeautifulSoup4 and extracts
Display List JSON (boxes, text, buttons) for native ImGui rendering in GTA V.

No headless browser required — fast, zero cold-start.
"""

import json
import logging
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    from bs4 import BeautifulSoup, Tag
    BS4_AVAILABLE = True
except ImportError:
    BS4_AVAILABLE = False

log = logging.getLogger("webv2.gtml")

_SKIP_TAGS = {"script", "style", "head", "meta", "link", "title", "noscript"}

_HEADING_SIZES = {"h1": 32.0, "h2": 24.0, "h3": 19.0, "h4": 16.0, "small": 13.0}

_NAMED_COLORS: Dict[str, int] = {
    "white": 0xFFFFFF, "black": 0x000000, "red": 0xFF0000,
    "green": 0x008000, "blue": 0x0000FF, "yellow": 0xFFFF00,
    "gray": 0x808080, "grey": 0x808080, "transparent": -1,
}


def _parse_color(value: Optional[str]) -> int:
    if not value:
        return -1
    v = value.strip().lower()
    if v in ("transparent", "inherit", "initial", "unset", ""):
        return -1
    if v in _NAMED_COLORS:
        return _NAMED_COLORS[v]
    # search (not match) so shorthands like "1px solid #333" or gradients yield their first color
    m = re.search(r"#([0-9a-f]{6})\b", v)
    if m:
        return int(m.group(1), 16)
    m = re.search(r"#([0-9a-f]{3})\b", v)
    if m:
        r, g, b = m.group(1)
        return int(r * 2 + g * 2 + b * 2, 16)
    m = re.search(r"rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)", v)
    if m:
        r, g, b = int(m.group(1)), int(m.group(2)), int(m.group(3))
        return (r << 16) | (g << 8) | b
    for word in re.findall(r"[a-z]+", v):
        if word in _NAMED_COLORS:
            return _NAMED_COLORS[word]
    return -1


def _parse_px(value: Optional[str], default: float = 0.0) -> float:
    """First length in a value; unitless numbers (e.g. "0") count as px."""
    if not value:
        return default
    m = re.match(r"\s*(-?[\d.]+)\s*(px)?(?![\w%])", value)
    return float(m.group(1)) if m else default


def _css_props(style_attr: Optional[str]) -> Dict[str, str]:
    if not style_attr:
        return {}
    result: Dict[str, str] = {}
    for part in style_attr.split(";"):
        if ":" in part:
            k, _, v = part.partition(":")
            result[k.strip().lower()] = v.strip()
    return result


def _collect_stylesheet(soup: Any) -> Dict[int, Dict[str, str]]:
    """Apply <style> rules to elements (source order; pseudo-classes and @rules skipped)."""
    sheet: Dict[int, Dict[str, str]] = {}
    for style in soup.find_all("style"):
        text = re.sub(r"/\*.*?\*/", "", style.get_text(), flags=re.S)
        for m in re.finditer(r"([^{}]+)\{([^{}]*)\}", text):
            props = _css_props(m.group(2))
            for sel in m.group(1).split(","):
                sel = sel.strip()
                if not sel or ":" in sel or sel.startswith("@"):
                    continue
                try:
                    matched = soup.select(sel)
                except Exception:
                    continue
                for el in matched:
                    sheet.setdefault(id(el), {}).update(props)
    return sheet


class GTMLCompiler:
    """GTML compiler using BeautifulSoup4 for static layout extraction."""

    def __init__(self, web_root: Path, **_kwargs):
        if not BS4_AVAILABLE:
            raise RuntimeError(
                "BeautifulSoup4 not installed. Run: pip install beautifulsoup4 lxml"
            )
        self.web_root = Path(web_root).resolve()

    def _resolve_gtml_file(self, gtml_path: str) -> Path:
        full_path = (self.web_root / gtml_path).resolve()
        if full_path.suffix.lower() != ".gtml":
            raise ValueError(f"Expected a .gtml file: {gtml_path}")
        try:
            full_path.relative_to(self.web_root)
        except ValueError:
            raise ValueError(f"Path traversal attempt: {gtml_path}") from None
        if not full_path.is_file():
            raise FileNotFoundError(f"GTML file not found: {gtml_path}")
        return full_path

    def compile(self, gtml_path: str) -> Dict[str, Any]:
        """Parse a .gtml file and return Display List JSON."""
        path = self._resolve_gtml_file(gtml_path)
        html = path.read_text(encoding="utf-8", errors="replace")
        try:
            soup = BeautifulSoup(html, "lxml")
        except Exception:
            soup = BeautifulSoup(html, "html.parser")

        sheet = _collect_stylesheet(soup)
        body = soup.find("body")
        body_css = {**sheet.get(id(body), {}), **_css_props(body.get("style") if body else None)}
        bg_color = _parse_color(body_css.get("background-color") or body_css.get("background"))
        if bg_color == -1:
            bg_color = 0x121622
        body_pad = _parse_px(body_css.get("padding"), 0.0)
        body_color = _parse_color(body_css.get("color"))
        body_fs = _parse_px(body_css.get("font-size"), 16.0)

        title_tag = soup.find("title")
        title = title_tag.get_text(strip=True) if title_tag else "GTML Page"

        nodes: List[Dict[str, Any]] = []
        page_w = 1280.0
        content_h = self._walk(body or soup, nodes, sheet, body_pad, body_pad,
                               page_w - 2 * body_pad, body_fs,
                               body_color if body_color != -1 else 0xFFFFFF)

        return {
            "type": "gtml_layout",
            "url": gtml_path,
            "title": title,
            "width": int(page_w),
            "height": max(720, int(content_h)),
            "bg_color": bg_color,
            "nodes": nodes,
            "scripts": self._collect_scripts(soup, path),
        }

    def _collect_scripts(self, soup: Any, page_path: Path) -> List[str]:
        """Inline <script> text plus local src= files from the same site; run in the ASI (QuickJS)."""
        scripts: List[str] = []
        for tag in soup.find_all("script"):
            kind = (tag.get("type") or "").strip().lower()
            if kind and kind not in ("text/javascript", "application/javascript", "module"):
                continue
            src = tag.get("src")
            if not src:
                code = tag.string or tag.get_text()
                if code and code.strip():
                    scripts.append(code)
                continue
            if "://" in src or src.startswith("//"):
                log.info("Remote script skipped: %s", src)
                continue
            target = (page_path.parent / src).resolve()
            try:
                target.relative_to(self.web_root)
            except ValueError:
                log.warning("Script outside web root skipped: %s", src)
                continue
            if target.is_file():
                scripts.append(target.read_text(encoding="utf-8", errors="replace"))
        return scripts

    def _walk(self, parent: Any, nodes: List[Dict], sheet: Dict[int, Dict[str, str]],
              x: float, y: float, avail_w: float, font_size: float, inherited_color: int) -> float:
        """Simple vertical block-flow layout. Returns the y cursor after the last child."""
        for el in parent.children:
            if not isinstance(el, Tag):
                continue
            tag = el.name.lower() if el.name else ""
            if tag in _SKIP_TAGS:
                continue

            css = {**sheet.get(id(el), {}), **_css_props(el.get("style"))}
            hidden = css.get("display", "").strip() == "none"
            # Hidden elements are kept only when scripts can reach them by id (they may be shown later)
            if hidden and not el.get("id"):
                continue
            bg = _parse_color(css.get("background-color") or css.get("background"))
            own_color = _parse_color(css.get("color"))
            color = own_color if own_color != -1 else inherited_color
            radius = _parse_px(css.get("border-radius"))
            border_color = _parse_color(css.get("border-color") or css.get("border"))
            fs = _parse_px(css.get("font-size"), _HEADING_SIZES.get(tag, font_size))
            margin = _parse_px(css.get("margin-bottom") or css.get("margin"), 4.0)
            pad = _parse_px(css.get("padding"), 0.0)
            width = min(_parse_px(css.get("width"), avail_w), avail_w)
            if "%" in css.get("width", ""):
                width = avail_w
            width = min(width, _parse_px(css.get("max-width"), width))

            absolute = css.get("position", "").strip() in ("absolute", "fixed")
            nx = _parse_px(css.get("left")) + x if absolute else x
            ny = _parse_px(css.get("top")) + y if absolute else y
            if not absolute and "auto" in css.get("margin", "") and width < avail_w:
                nx = x + (avail_w - width) * 0.5

            text = el.get_text(separator=" ", strip=True)
            has_tag_children = not hidden and any(isinstance(c, Tag) for c in el.children)
            is_input = tag in ("input", "textarea", "select")
            is_interactive = is_input or tag in ("button", "a") or \
                el.get("role") in ("button", "link") or el.get("onclick") is not None

            node: Dict[str, Any] = {
                "id": el.get("id") or f"elem_{id(el) & 0xFFFFFF:06x}",
                "x": int(nx),
                "y": int(ny),
                "w": int(width),
            }
            if hidden:
                node["visible"] = False

            if is_interactive:
                h = _parse_px(css.get("height"), fs + (2 * pad if pad else 16.0))
                node.update({
                    "type": 4 if is_input else 2,
                    "interactive": True,
                    "text": (el.get("value", "") or text) if is_input else text,
                    "bg": bg if bg != -1 else 0x2A2D3E,
                    "border": border_color if border_color != -1 else 0x3A3D4E,
                    "r": radius,
                    "color": color if color != -1 else 0xFFFFFF,
                    "font_size": fs,
                })
                if is_input:
                    node["placeholder"] = el.get("placeholder", "")
                    node["disabled"] = el.has_attr("disabled")
                if tag == "a" and el.get("href"):
                    node["href"] = el["href"]
                node["h"] = int(h)
                nodes.append(node)
            elif not has_tag_children:
                if not text and not el.get("id"):
                    continue
                chars_per_line = max(1, int((width - 2 * pad) / (fs * 0.55)))
                lines = max(1, -(-len(text) // chars_per_line))
                h = _parse_px(css.get("height"), lines * fs * 1.3 + 2 * pad)
                node.update({
                    "type": 1,
                    "text": text,
                    "color": color if color != -1 else 0xFFFFFF,
                    "font_size": fs,
                    "h": int(h),
                })
                if bg != -1:
                    node["bg"] = bg
                nodes.append(node)
            else:
                # Container: box node goes before its children so it is drawn behind them
                box_index = len(nodes)
                inner_bottom = self._walk(el, nodes, sheet, nx + pad, ny + pad, width - 2 * pad, fs, color)
                h = _parse_px(css.get("height"), (inner_bottom - ny) + pad)
                if bg != -1 or border_color != -1:
                    node.update({"type": 0, "bg": bg, "border": border_color, "r": radius, "h": int(h)})
                    nodes.insert(box_index, node)

            if not absolute:
                y = ny + h + margin
        return y

    # Async shim so callers that use `await compiler.compile(...)` still work
    async def compile_async(self, gtml_path: str) -> Dict[str, Any]:
        return self.compile(gtml_path)

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        pass

    def shutdown(self):
        pass


# ---------------------------------------------------------------------------
# Module-level singleton for engine.py (no re-import needed)
# ---------------------------------------------------------------------------
_singleton: Optional[GTMLCompiler] = None
_singleton_root: Optional[Path] = None


def _get_singleton(web_root: Path) -> GTMLCompiler:
    global _singleton, _singleton_root
    root = Path(web_root).resolve()
    if _singleton is None or _singleton_root != root:
        _singleton = GTMLCompiler(root)
        _singleton_root = root
        log.info("GTMLCompiler singleton created for %s", root)
    return _singleton


async def compile_gtml(web_root: Path, gtml_path: str, **_kwargs) -> Dict[str, Any]:
    """Async-compatible convenience function (no coroutine overhead)."""
    return _get_singleton(web_root).compile(gtml_path)


def compile_gtml_sync(web_root: Path, gtml_path: str, **_kwargs) -> Dict[str, Any]:
    """Synchronous convenience function used by engine.py."""
    return _get_singleton(web_root).compile(gtml_path)


# CLI for testing
if __name__ == "__main__":
    import sys
    logging.basicConfig(level=logging.INFO)
    if len(sys.argv) < 3:
        print("Usage: python gtml_compiler.py <web_root> <gtml_path>")
        sys.exit(1)
    result = compile_gtml_sync(Path(sys.argv[1]), sys.argv[2])
    print(json.dumps(result, indent=2, ensure_ascii=False))
