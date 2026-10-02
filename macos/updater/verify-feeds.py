#!/usr/bin/env python3
"""Verify signed release feeds and archives offline using only a pinned public key."""
import argparse
import sys
import xml.etree.ElementTree as ET
from release_feeds import verify_feeds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--feeds", required=True)
    parser.add_argument("--public-key", required=True, help="Pinned canonical base64 Ed25519 key, not a downloaded manifest key")
    parser.add_argument("--openssl", default="openssl", help="OpenSSL 3 executable")
    parser.add_argument("--previous-manifest", help="Last published manifest for offline numeric-build ordering")
    args = parser.parse_args()
    try:
        verify_feeds(args.manifest, args.feeds, args.public_key, args.openssl, args.previous_manifest)
    except (ValueError, OSError, ET.ParseError) as error:
        print("Feed verification refused: " + str(error), file=sys.stderr)
        return 1
    print("Verified both signed feeds and matching archives offline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
