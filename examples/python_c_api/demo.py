#!/usr/bin/env python3
"""Run the core through its C interface inside Python, without an HTTP server."""
import argparse
import json
from pathlib import Path
import time
import sys
from client import Core


def main():
    sys.stdout.reconfigure(line_buffering=True)
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library-dir', type=Path, default=root / 'build/mock/build_x64/Debug/lib')
    parser.add_argument('--output', type=Path, default=root / 'build/python-c-api-capture/report.json')
    args = parser.parse_args()
    output = args.output.resolve()
    print('Funiq Trading | Python -> ctypes -> existing C interface')
    print('Core services and the mock account run inside this Python process.')
    print('Structure fields verified against the core headers.\n')
    with Core(args.library_dir, root / 'examples/mock_core') as core:
        print(json.dumps(core.status(), indent=2))
        samples = []
        for _ in range(3):
            quote = core.depth('000001')
            samples.append({'symbol': '000001', 'last': quote.last_price, 'sequence': quote.sequence})
            print(f'C quote_get_depth_data_by_code: 000001 last={quote.last_price:.2f} sequence={quote.sequence}')
            time.sleep(0.2)
        buy = core.buy(1000, 20)
        print(f'\nC trade_send_order_sync: bought 1000 shares, order={buy.order_id}')
        resting = core.buy(100, 1)
        before = core.query('funds')[0]
        assert before.frozen_cash == 100
        core.cancel(resting.order_id)
        print(f'C trade_cancel_order_sync: cancelled order={resting.order_id}, released 100.00 CNY')
        funds = core.query('funds')[0]
        positions = core.query('positions')
        orders = core.query('orders')
        fills = core.query('trades')
        assert funds.cash_balance == 985000 and funds.frozen_cash == 0
        assert len(positions) == 1 and positions[0].total_qty == 1000
        assert [o.status for o in orders] == [3, 4] and len(fills) == 1
        assert len({q['sequence'] for q in samples}) > 1
        print(f'C trade_query_funds_sync: cash={funds.cash_balance:,.2f} CNY, positions={funds.market_value:,.2f} CNY')
        print(f'C trade_query_positions_sync: 000001 shares={positions[0].total_qty}')
        report = {'path': 'Python -> ctypes -> C interface -> core', 'samples': samples,
                  'cash': funds.cash_balance, 'frozen_cash': funds.frozen_cash,
                  'shares': positions[0].total_qty, 'order_states': [o.status for o in orders],
                  'calls': core.calls}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: C-interface quotes, orders, cancellation, account checks and clean shutdown')
    print('Saved C-interface results: ' + str(output))


if __name__ == '__main__':
    main()
