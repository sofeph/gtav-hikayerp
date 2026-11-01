with open('webv2_backend/gtml_compiler.py', 'r') as f:
    content = f.read()

# Fix the indentation
old = '''            # Load the GTML file via HTTP server to ensure proper MIME type
                        file_url = f"http://localhost:8766/webv2/{gtml_path}"
            await page.goto(file_url, wait_until='networkidle', timeout=30000)'''

new = '''            # Load the GTML file via HTTP server to ensure proper MIME type
            file_url = f"http://localhost:8766/webv2/{gtml_path}"
            await page.goto(file_url, wait_until='networkidle', timeout=30000)'''

if old in content:
    content = content.replace(old, new)
    with open('webv2_backend/gtml_compiler.py', 'w') as f:
        f.write(content)
    print('Fixed!')
else:
    print('Pattern not found')