
def write(book):
    """Format a book dict into readable text."""
    if not isinstance(book, dict):
        return str(book)

    lines = []
    title = book.get("title", "Untitled")
    lines.append(f"# {title}")
    lines.append("")

    pages = book.get("pages", [])
    for i, page in enumerate(pages):
        if isinstance(page, dict):
            short = page.get("short", "")
            lines.append(f"## Page {i+1}: {short}")
            for ill in page.get("illustration", []):
                lines.append(f"  [Illustration: {ill}]")
            for content in page.get("content", []):
                lines.append(f"  {content}")
            lines.append("")
        else:
            lines.append(f"## Page {i+1}")
            lines.append(f"  {page}")
            lines.append("")

    return "\n".join(lines)
