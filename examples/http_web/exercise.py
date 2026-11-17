#!/usr/bin/env python3
"""Exercise a freshly started mock server and save its actual HTTP responses."""
import argparse
import datetime as dt
import json
from pathlib import Path
import time
from http_client import FuniqClient


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:18080')
    parser.add_argument('--output', type=Path, default=Path('build/mock-capture'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = FuniqClient(args.url)
    records = client.records
    request = client.request
    status = request('/system/status')
    if (status['services']['quote'].get('parser_id') != 'mock_quotes' or
            status['services']['trade'].get('interface_id') != 'mock_trading'):
        raise RuntimeError('This order exercise requires the mock_core example (mock_quotes + mock_trading).')

    # Explicit start also refreshes dependencies if the all-day scheduler already ran.
    request('/system/start', {})
    health = request('/system/health')
    status = request('/system/status')
    assert health['overall_status'] == 'healthy', health
    for service in ['quote', 'trade', 'strategy']:
        assert status['services'][service]['running'], status
    quotes = []
    for _ in range(3):
        for code in ['600000', '000001']:
            quotes.append(request('/quote/depth?security_code=' + code))
        time.sleep(0.3)
    starting_funds = request('/trade/query/funds?login_id=MOCK_DEMO&market=2')
    buy = request('/trade/buy', {'login_id': 'MOCK_DEMO', 'security_code': '000001', 'amount': 2, 'price': 20})
    resting = request('/trade/buy', {'login_id': 'MOCK_DEMO', 'security_code': '000001', 'amount': 0.01, 'price': 1})
    reserved = request('/trade/query/funds?login_id=MOCK_DEMO&market=2')
    request('/trade/cancel', {'login_id': 'MOCK_DEMO', 'market': 2, 'order_id': resting['order_id']})
    orders = request('/trade/query/orders?login_id=MOCK_DEMO&market=2')
    trades = request('/trade/query/trades?login_id=MOCK_DEMO&market=2')
    positions = request('/trade/query/positions?login_id=MOCK_DEMO&market=2')
    funds = request('/trade/query/funds?login_id=MOCK_DEMO&market=2')
    before = starting_funds['funds'][0]
    after = funds['funds'][0]
    assert buy['qty'] == 1000 and resting['qty'] == 100
    assert reserved['funds'][0]['frozen_cash'] == before['frozen_cash'] + 100
    assert after['frozen_cash'] == before['frozen_cash']
    assert after['cash_balance'] == before['cash_balance'] - 15000
    by_id = {o['order_id']: o for o in orders['orders']}
    assert by_id[buy['order_id']]['status'] == 3
    assert by_id[resting['order_id']]['status'] == 4
    assert len({q['sequence'] for q in quotes}) > 1
    report = {'captured_at' : dt.datetime.now(dt.timezone.utc).isoformat(),
              'mode': 'MOCK — synthetic quotes and in-memory execution',
              'health': health, 'status': status, 'quotes': quotes,
              'starting_funds': starting_funds, 'buy': buy, 'resting': resting, 'funds_before_cancel': reserved,
              'orders': orders, 'trades': trades, 'positions': positions, 'funds': funds}
    (args.output / 'responses.json').write_text(json.dumps(records, indent=2, ensure_ascii=False) + '\n')
    (args.output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(report, indent=2, ensure_ascii=False))
    print('PASS: live quotes, filled order, cash reservation, cancellation and fund reconciliation')
    print(f'Captured {len(records)} real HTTP responses in {args.output}')


if __name__ == '__main__':
    main()
