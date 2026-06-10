#!/usr/bin/env python3
"""Mint a salted PBKDF2 password hash for CONFIG_JSON user accounts.

Usage:
    python3 hash_password.py                 # prompts for the password (no echo)
    python3 hash_password.py "the password"  # password as an argument (dev only)

Paste the printed string into the user's "password_hash" field in CONFIG_JSON.
"""

import getpass
import sys

import config


def main() -> int:
    if len(sys.argv) > 1:
        password = sys.argv[1]
    else:
        password = getpass.getpass("Password: ")
        confirm = getpass.getpass("Confirm:  ")
        if password != confirm:
            print("Passwords do not match", file=sys.stderr)
            return 1
    if not password:
        print("Empty password", file=sys.stderr)
        return 1
    print(config.hash_password(password))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
