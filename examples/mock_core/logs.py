#!/usr/bin/env python3
"""Show selected, unmodified lines from the mock core's Docker logs."""
import argparse
import re
import subprocess

PATTERNS = {
    'startup': r'Loaded (parser|trade handler) library|MOCK .*plugin ready|Mock replay started|API server listening|=== Service Status|Quote Service:|Trade Service:|Strategy Service:|Database Service:|Network Service:',
    'quotes': r'MOCK quote [0-9]',
    'execution': r'Sending Order -|MOCK order |Canceling Order -|MOCK cancelled order ',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('section', choices=PATTERNS)
    parser.add_argument('--container', default='funiq-trading-mock')
    parser.add_argument('--lines', type=int, default=18)
    args = parser.parse_args()
    if args.lines < 1:
        parser.error('lines must be positive')
    result = subprocess.run(['docker', 'logs', args.container], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(result.stdout)
    lines = [line for line in result.stdout.splitlines() if re.search(PATTERNS[args.section], line)]
    if not lines:
        raise SystemExit('No matching records yet. Start the core or run the Python mock order exercise.')
    print('\n'.join(lines[-args.lines:]))


if __name__ == '__main__':
    main()
