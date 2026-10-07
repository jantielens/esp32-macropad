import importlib.util
import sys
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
TOOLS = ROOT.parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("portal_dev", TOOLS / "portal-dev-server.py")
portal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portal)


class ThemeHandler(portal.PortalHandler):
    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/lab" or path.startswith("/lab/"):
            relative = "gallery.html" if path in ("/lab", "/lab/") else path.removeprefix("/lab/")
            target = (ROOT / relative).resolve()
            if not target.is_relative_to(ROOT) or not target.is_file():
                self.send_error(404)
                return
            self._serve_file(target)
            return
        super().do_GET()

    def _serve_bytes(self, data, content_type, status=200):
        if urlparse(self.path).path == "/" and content_type.startswith("text/html"):
            data = data.replace(b"</head>", b'<link rel="stylesheet" href="/lab/styles.css"><script defer src="/lab/themes.js"></script></head>')
        super()._serve_bytes(data, content_type, status)


portal.PortalHandler = ThemeHandler
if __name__ == "__main__":
    portal.main()