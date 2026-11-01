"""GTML Compiler - Headless layout extraction for WebV2 thin-client model.

Compiles .gtml files (HTML5 + CSS3 + JS) into Display List JSON (2-5 KB)
containing only box/text coordinates for native ImGui rendering in GTA V.
"""

import asyncio
import json
import logging
import os
import re
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
from urllib.parse import urljoin, urlparse

try:
    from playwright.async_api import async_playwright, Browser, Page, BrowserContext
    PLAYWRIGHT_AVAILABLE = True
except ImportError:
    PLAYWRIGHT_AVAILABLE = False

log = logging.getLogger("webv2.gtml")

# GTML Display List types
GTMLNodeType = Union[
    Dict[str, Any],  # box
    Dict[str, Any],  # text
    Dict[str, Any],  # button
]

class GTMLCompiler:
    """Headless GTML compiler using Playwright for layout extraction."""
    
    def __init__(self, web_root: Path, headless: bool = True):
        self.web_root = Path(web_root).resolve()
        self.headless = headless
        self._browser: Optional["Browser"] = None
        self._context: Optional["BrowserContext"] = None
        self._playwright = None
        
    async def __aenter__(self):
        await self._ensure_browser()
        return self
        
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.close()
        
    async def _ensure_browser(self):
        """Initialize Playwright browser."""
        if not PLAYWRIGHT_AVAILABLE:
            raise RuntimeError("Playwright not installed. Run: pip install playwright && playwright install chromium")
            
        if self._browser is None:
            self._playwright = await async_playwright().start()
            self._browser = await self._playwright.chromium.launch(
                headless=self.headless,
                args=[
                    '--disable-gpu',
                    '--disable-dev-shm-usage',
                    '--no-sandbox',
                    '--disable-setuid-sandbox',
                    '--disable-web-security',
                    '--disable-features=VizDisplayCompositor',
                ]
            )
            self._context = await self._browser.new_context(
                viewport={'width': 1280, 'height': 720},
                device_scale_factor=1.0,
                is_mobile=False,
                has_touch=False,
            )
            log.info("Playwright browser started for GTML compilation")
            
    async def close(self):
        """Clean up browser resources."""
        if self._context:
            await self._context.close()
            self._context = None
        if self._browser:
            await self._browser.close()
            self._browser = None
        if self._playwright:
            await self._playwright.stop()
            self._playwright = None
            
    async def compile(self, gtml_path: str, base_url: str = "http://localhost:8766") -> Dict[str, Any]:
        """Compile a .gtml file into Display List JSON.
        
        Args:
            gtml_path: Relative path from web_root (e.g., "test/index.gtml")
            base_url: Base URL for resolving relative assets
            
        Returns:
            Display List JSON dict ready for WebSocket transmission
        """
        await self._ensure_browser()
        
        # Resolve file path
        full_path = (self.web_root / gtml_path).resolve()
        try:
            full_path.relative_to(self.web_root)
        except ValueError:
            raise ValueError(f"Path traversal attempt: {gtml_path}")
            
        if not full_path.is_file():
            raise FileNotFoundError(f"GTML file not found: {gtml_path}")
            
        # Create page and inject gta bridge
        page = await self._context.new_page()
        
        try:
            # Inject gta.trigger bridge before page load
            await page.add_init_script("""
                window.gta = {
                    trigger: function(eventName, data) {
                        // Send to Python host via postMessage
                        if (window._py_bridge) {
                            window._py_bridge.postMessage(JSON.stringify({
                                type: 'gtml_event',
                                event: eventName,
                                payload: data
                            }));
                        }
                    }
                };
                
                // Expose bridge for Python to inject
                window._py_bridge = {
                    postMessage: function(msg) {
                        window.chrome.webview.postMessage(msg);
                    }
                };
            """)
            
            # Load the GTML file via HTTP server to ensure proper MIME type
            file_url = f"http://localhost:8766/webv2/{gtml_path}"
            await page.goto(file_url, wait_until='networkidle', timeout=30000)
            
            # Wait for any dynamic content (JS execution, fetch, etc.)
            await page.wait_for_timeout(500)
            
            # Extract layout via injected script
            layout = await self._extract_layout(page, gtml_path)
            
            return layout
            
        finally:
            await page.close()
            
    async def _extract_layout(self, page: "Page", source_url: str) -> Dict[str, Any]:
        """Extract computed layout from page using getBoundingClientRect + getComputedStyle."""
        
        layout_json = await page.evaluate("""(sourceUrl) => {
            // Helper: convert CSS color to 0xRRGGBB integer
            function parseColor(cssColor) {
                if (!cssColor || cssColor === 'transparent' || cssColor === 'rgba(0, 0, 0, 0)') return -1;
                const canvas = document.createElement('canvas');
                const ctx = canvas.getContext('2d');
                ctx.fillStyle = cssColor;
                ctx.fillRect(0, 0, 1, 1);
                const data = ctx.getImageData(0, 0, 1, 1).data;
                return (data[0] << 16) | (data[1] << 8) | data[2];
            }
            
            // Helper: get computed style value
            function getStyle(el, prop) {
                return window.getComputedStyle(el).getPropertyValue(prop);
            }
            
            // Helper: parse border radius
            function parseRadius(el) {
                const r = getStyle(el, 'border-radius');
                const match = r.match(/^([\\d.]+)px$/);
                return match ? parseFloat(match[1]) : 0;
            }
            
            // Helper: parse box-shadow for border color approximation
            function parseBorderColor(el) {
                const bc = getStyle(el, 'border-color');
                if (bc && bc !== 'currentcolor' && bc !== 'transparent') {
                    return parseColor(bc);
                }
                return -1;
            }
            
            const nodes = [];
            const allElements = document.querySelectorAll('*');
            
            for (const el of allElements) {
                            // Skip script, style, head, meta, link, html, body, etc.
                const tag = el.tagName.toLowerCase();
                            if (['script', 'style', 'head', 'meta', 'link', 'title', 'noscript', 'html', 'body'].includes(tag)) {
                    continue;
                }
                
                            // Skip elements that are direct children of body but contain the whole page
                            if (el.parentElement && el.parentElement.tagName.toLowerCase() === 'body' && 
                                (tag === 'div' || tag === 'main' || tag === 'section')) {
                                // Check if this element contains the entire page content
                                const rect = el.getBoundingClientRect();
                                if (rect.width >= window.innerWidth * 0.9 && rect.height >= window.innerHeight * 0.9) {
                                    continue; // Skip full-page containers
                                }
                            }
                
                const rect = el.getBoundingClientRect();
                // Skip zero-size or off-screen elements
                if (rect.width <= 0 || rect.height <= 0) continue;
                if (rect.right <= 0 || rect.bottom <= 0) continue;
                
                const style = window.getComputedStyle(el);
                const display = style.display;
                const visibility = style.visibility;
                const opacity = parseFloat(style.opacity);
                
                if (display === 'none' || visibility === 'hidden' || opacity === 0) continue;
                
                const bgColor = parseColor(style.backgroundColor);
                const borderColor = parseBorderColor(el);
                const radius = parseRadius(el);
                const color = parseColor(style.color);
                const fontSize = parseFloat(style.fontSize);
                const fontWeight = style.fontWeight;
                const fontFamily = style.fontFamily;
                
                const node = {
                    id: el.id || `elem_${Math.random().toString(36).substr(2, 9)}`,
                    x: Math.round(rect.x),
                    y: Math.round(rect.y),
                    w: Math.round(rect.width),
                    h: Math.round(rect.height),
                };
                
                // Determine node type
                const isButton = tag === 'button' || 
                                 el.getAttribute('role') === 'button' ||
                                 el.classList.contains('btn') ||
                                 el.onclick !== null;
                
                const isInput = ['input', 'textarea', 'select'].includes(tag);
                const isLink = tag === 'a' && el.href;
                
                if (isButton || isInput || isLink) {
                    node.type = 'button';
                    node.clickable = true;
                    node.text = el.textContent?.trim() || el.value || '';
                    node.bg = bgColor !== -1 ? bgColor : 0x2A2D3E;
                    node.border = borderColor !== -1 ? borderColor : 0x3A3D4E;
                    node.radius = radius;
                    node.color = color !== -1 ? color : 0xFFFFFFFF;
                    node.size = fontSize || 16;
                    node.weight = fontWeight >= 600 || fontWeight === 'bold' ? 'bold' : 'normal';
                } else if (el.textContent && el.textContent.trim() && el.children.length === 0) {
                    // Leaf text node
                    node.type = 'text';
                    node.text = el.textContent.trim();
                    node.color = color !== -1 ? color : 0xFFFFFFFF;
                    node.size = fontSize || 16;
                    node.weight = fontWeight >= 600 || fontWeight === 'bold' ? 'bold' : 'normal';
                    // Text nodes don't have bg/border/radius
                } else {
                    // Container/box
                    node.type = 'box';
                    node.bg = bgColor !== -1 ? bgColor : -1;
                    node.border = borderColor !== -1 ? borderColor : -1;
                    node.radius = radius;
                }
                
                nodes.push(node);
            }
            
            // Get page title and background
            const title = document.title || 'GTML Page';
            const bodyStyle = window.getComputedStyle(document.body);
            const bg = parseColor(bodyStyle.backgroundColor);
            
            return {
                type: 'gtml_layout',
                url: sourceUrl,
                title: title,
                bg: bg !== -1 ? bg : 0x121622,
                nodes: nodes
            };
        }""", source_url)
        
        return layout_json


async def compile_gtml(web_root: Path, gtml_path: str, headless: bool = True) -> Dict[str, Any]:
    """Convenience function to compile a single GTML file."""
    async with GTMLCompiler(web_root, headless) as compiler:
        return await compiler.compile(gtml_path)


# Synchronous wrapper for use in non-async contexts
def compile_gtml_sync(web_root: Path, gtml_path: str, headless: bool = True) -> Dict[str, Any]:
    """Synchronous wrapper for GTML compilation."""
    return asyncio.run(compile_gtml(web_root, gtml_path, headless))


# CLI for testing
if __name__ == "__main__":
    import sys
    if len(sys.argv) < 3:
        print("Usage: python gtml_compiler.py <web_root> <gtml_path>")
        sys.exit(1)
        
    web_root = Path(sys.argv[1])
    gtml_path = sys.argv[2]
    
    logging.basicConfig(level=logging.INFO)
    
    try:
        result = compile_gtml_sync(web_root, gtml_path)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except Exception as e:
        log.error("Compilation failed: %s", e)
        sys.exit(1)