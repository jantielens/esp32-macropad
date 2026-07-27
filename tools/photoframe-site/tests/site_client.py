"""TestClient wrapper that submits the CSRF token rendered by the site."""

from __future__ import annotations

import re

from starlette.testclient import TestClient as StarletteTestClient

_CSRF_PATTERN = re.compile(r'name="csrf_token" value="([^"]+)"')


class TestClient(StarletteTestClient):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._csrf_token: str | None = None

    def request(self, *args, **kwargs):
        response = super().request(*args, **kwargs)
        match = _CSRF_PATTERN.search(response.text)
        if match:
            self._csrf_token = match.group(1)
        return response

    def post(self, url, *args, **kwargs):
        data = kwargs.get("data")
        if not isinstance(data, dict) or "csrf_token" not in data:
            if self._csrf_token is None:
                self.get("/")
            if self._csrf_token is None:
                raise AssertionError("site did not render a CSRF token")
            updated = dict(data) if isinstance(data, dict) else {}
            updated["csrf_token"] = self._csrf_token
            kwargs["data"] = updated
        return super().post(url, *args, **kwargs)