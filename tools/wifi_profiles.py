#!/usr/bin/env python3
"""Manage the ordered SD Wi-Fi JSON without exposing credentials in arguments/output."""
import argparse
import json
import os
from pathlib import Path
from provision import load_env, load_profiles, validate_network


def read(path: Path):
    if path.exists():
        return load_profiles(path)
    backup = Path(str(path) + '.bak')
    return load_profiles(backup) if backup.exists() else []


def upsert(profiles, ssid, password):
    validate_network(ssid, password)
    result = list(profiles)
    for index, item in enumerate(result):
        if item[0] == ssid:
            result[index] = (ssid, password)
            return result
    if len(result) >= 10:
        raise ValueError('at most 10 networks; existing list preserved')
    return result + [(ssid, password)]


def save(path: Path, profiles):
    if len(profiles) > 10 or len({s for s, _ in profiles}) != len(profiles):
        raise ValueError('invalid network list')
    for ssid, password in profiles:
        validate_network(ssid, password)
    body = json.dumps({'version': 1, 'networks': [dict(ssid=s, password=p) for s, p in profiles]},
                      ensure_ascii=False, indent=2) + '\n'
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary, backup = Path(str(path) + '.tmp'), Path(str(path) + '.bak')
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        stream.write(body)
        stream.flush()
        os.fsync(stream.fileno())
    # If publication is interrupted, firmware and this tool recover the .bak.
    if path.exists():
        os.replace(path, backup)
    os.replace(temporary, path)
    backup.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['status', 'upsert', 'remove', 'move'])
    parser.add_argument('--file', type=Path, required=True, help='SD config/wifi.json or a private local copy')
    parser.add_argument('--config', type=Path, help='private KEY=VALUE file containing WIFI_SSID and WIFI_PASSWORD')
    parser.add_argument('--index', type=int, help='one-based position')
    parser.add_argument('--to', type=int, help='one-based new priority')
    args = parser.parse_args()
    profiles = read(args.file)
    if args.action == 'upsert':
        if args.config is None:
            parser.error('upsert requires --config; never pass a password on the command line')
        values = load_env(args.config)
        profiles = upsert(profiles, values.get('WIFI_SSID', ''), values.get('WIFI_PASSWORD', ''))
    elif args.action in ('remove', 'move'):
        if args.index is None or not 1 <= args.index <= len(profiles):
            parser.error('--index must address an existing position')
        if args.action == 'move' and (args.to is None or not 1 <= args.to <= len(profiles)):
            parser.error('--to must address an existing position')
        item = profiles.pop(args.index - 1)
        if args.action == 'move':
            profiles.insert(args.to - 1, item)
    if args.action != 'status':
        save(args.file, profiles)
    print(json.dumps({'operation': args.action, 'networks': len(profiles)}))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError):
        raise SystemExit('Wi-Fi file operation failed; inspect the protected files locally. No credentials were printed.') from None
