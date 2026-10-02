#!/usr/bin/env python3
"""Prepare both signed release feeds offline. This never publishes updates."""
import argparse
import sys
from release_feeds import make_feeds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--output", required=True, help="New directory with an existing parent; never overwritten")
    parser.add_argument("--signer", required=True, help="Trusted pinned Sparkle sign_update executable; uses Keychain defaults")
    parser.add_argument("--previous-manifest", help="Last published manifest for offline numeric-build ordering")
    args = parser.parse_args()
    try:
        make_feeds(args.manifest, args.output, args.signer, args.previous_manifest)
    except (ValueError, OSError) as error:
        print("Feed preparation refused: " + str(error), file=sys.stderr)
        return 1
    print("Prepared two signed feeds offline; no publication performed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
