#!/usr/bin/env python3
"""Back up or update SD Wi-Fi over the authenticated LAN API; never print credentials."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from module import NoRedirect, token_from_config
from provision import load_profiles

LIMIT = 8192


def transfer(base, token, payload=None):
    url = urllib.parse.urlsplit(base)
    if (url.scheme not in ('http', 'https') or not url.hostname or url.username or
            url.password or url.path not in ('', '/') or url.query or url.fragment):
        raise ValueError('expected device origin')
    if payload is not None and len(payload) > LIMIT:
        raise ValueError('oversized JSON')
    req = urllib.request.Request(base.rstrip('/') + '/api/wifi', data=payload,
        method='GET' if payload is None else 'POST',
        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    with opener.open(req, timeout=60) as response:
        body = response.read(LIMIT + 1)
        if response.status != 200 or len(body) > LIMIT:
            raise ValueError('unexpected response')
        return body


def backup(base, token, destination):
    # Refuse overwrites and symlinks; only publish a completely validated file.
    if destination.exists() or destination.is_symlink():
        raise ValueError('backup destination already exists')
    body = transfer(base, token)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
            temporary = Path(stream.name)
            os.fchmod(stream.fileno(), 0o600)
            stream.write(body)
            stream.flush()
            os.fsync(stream.fileno())
        count = len(load_profiles(temporary))
        os.link(temporary, destination)  # atomic publication, never replace a prior backup
        return count
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def update(base, token, source):
    pairs = load_profiles(source)
    payload = json.dumps({'version': 1, 'networks': [dict(ssid=s, password=p) for s,p in pairs]},
                         ensure_ascii=False).encode()
    transfer(base, token, payload)
    return len(pairs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['backup', 'update'])
    parser.add_argument('--url', required=True)
    parser.add_argument('--file', type=Path, required=True, help='private backup destination or update source')
    parser.add_argument('--config', type=Path, default=Path(__file__).resolve().parent.parent/'secrets.env')
    args = parser.parse_args()
    token = token_from_config(args.config)
    count = (backup if args.action == 'backup' else update)(args.url, token, args.file)
    print(json.dumps({'operation': args.action, 'status': 200, 'networks': count,
                      'reboot_required': args.action == 'update'}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError):
        raise SystemExit('SD Wi-Fi operation failed; no credentials or response body printed.') from None
