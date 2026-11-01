import asyncio
from playwright.async_api import async_playwright
from pathlib import Path

async def test():
    web_root = Path('../data/web')
    full_path = (web_root / 'test/index.gtml').resolve()
    file_url = full_path.as_uri()
    
    playwright = await async_playwright().start()
    browser = await playwright.chromium.launch(headless=True)
    context = await browser.new_context(viewport={'width': 1280, 'height': 720})
    page = await context.new_page()
    
    await page.goto(file_url, wait_until='networkidle', timeout=30000)
    await page.wait_for_timeout(500)
    
    # Check what elements exist
    elements = await page.evaluate('''() => {
        const all = document.querySelectorAll('*');
        const result = [];
        for (const el of all) {
            const tag = el.tagName.toLowerCase();
            const rect = el.getBoundingClientRect();
            if (rect.width > 0 && rect.height > 0) {
                result.push({
                    tag: tag,
                    id: el.id,
                    class: el.className,
                    text: el.textContent ? el.textContent.trim().substring(0, 100) : '',
                    children: el.children.length,
                    x: rect.x,
                    y: rect.y,
                    w: rect.width,
                    h: rect.height
                });
            }
        }
        return result;
    }''')
    
    for el in elements:
        tag = el['tag']
        id_val = el['id']
        class_val = str(el['class'])[:30]
        children = el['children']
        x = el['x']
        y = el['y']
        w = el['w']
        h = el['h']
        text = el['text'][:50]
        print(f'{tag:10s} id={id_val:20s} class={class_val:30s} children={children:2d} ({x:.0f},{y:.0f}) {w:.0f}x{h:.0f} text={text}')
    
    await browser.close()
    await playwright.stop()

asyncio.run(test())