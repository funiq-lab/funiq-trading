#!/usr/bin/env python
# -*- coding: utf-8 -*-

import argparse
import json
from apis import TradingAPI


def print_json(d):
    print(json.dumps(d, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser(description="FT Quote/Trade/Strategy/TWAP CLI")
    parser.add_argument("--host", default="http://127.0.0.1:8080", help="network base url")
    subparsers = parser.add_subparsers(dest="group", required=True)

    # ===== api =====
    api_parser = subparsers.add_parser("api", help="API meta operations")
    api_sp = api_parser.add_subparsers(dest="command", required=True)
    api_sp.add_parser("list", help="GET /api")
    api_sp.add_parser("health", help="GET /health")

    # ===== system =====
    system_parser = subparsers.add_parser("system", help="System control/status")
    system_sp = system_parser.add_subparsers(dest="command", required=True)
    system_sp.add_parser("start", help="POST /system/start")
    system_sp.add_parser("stop", help="POST /system/stop")
    system_sp.add_parser("health", help="GET /system/health")
    system_sp.add_parser("status", help="GET /system/status")
    system_sp.add_parser("accounts", help="GET /system/accounts")
    acc_detail = system_sp.add_parser("account-detail", help="GET /system/accounts/detail")
    acc_detail.add_argument("--login-id", required=True, help="account login id")

    # ===== quote =====
    quote_parser = subparsers.add_parser("quote", help="Quote queries")
    quote_sp = quote_parser.add_subparsers(dest="command", required=True)
    for cmd, help_text in [
        ("depth", "GET /quote/depth"),
        ("index", "GET /quote/index"),
        ("depth50", "GET /quote/depth-fifty"),
        ("entrust", "GET /quote/entrust"),
        ("trade", "GET /quote/trade"),
    ]:
        p = quote_sp.add_parser(cmd, help=help_text)
        p.add_argument("--code", required=True, help="security code")

    # ===== trade =====
    trade_parser = subparsers.add_parser("trade", help="Trade operations and queries")
    trade_sp = trade_parser.add_subparsers(dest="command", required=True)

    # trade buy
    p_buy = trade_sp.add_parser("buy", help="POST /trade/buy")
    p_buy.add_argument("--login-id", required=True)
    p_buy.add_argument("--code", required=True, help="security code")
    p_buy.add_argument("--amount", required=True, type=float)

    # trade sell
    p_sell = trade_sp.add_parser("sell", help="POST /trade/sell")
    p_sell.add_argument("--login-id", required=True)
    p_sell.add_argument("--code", required=True, help="security code")
    p_sell.add_argument("--quantity", required=True, type=int)

    # trade cancel
    p_cancel = trade_sp.add_parser("cancel", help="POST /trade/cancel")
    p_cancel.add_argument("--login-id", required=True)
    p_cancel.add_argument("--order-id", required=True, type=int)
    p_cancel.add_argument("--market", required=True, type=int)
    p_cancel.add_argument("--batch-id", type=int, default=0)

    # bank-securities transfer
    p_bk = trade_sp.add_parser("bank-transfer", help="POST /trade/bank-transfer")
    p_bk.add_argument("--login-id", required=True)
    p_bk.add_argument("--direction", required=True, type=int)
    p_bk.add_argument("--market", required=True, type=int)
    p_bk.add_argument("--transfer-amount", required=True, type=float)
    p_bk.add_argument("--bank-password", required=True)
    p_bk.add_argument("--fund-password", required=True)
    p_bk.add_argument("--currency", default="CNY")
    p_bk.add_argument("--remark", default="")

    # fund transfer between markets
    p_ft = trade_sp.add_parser("fund-transfer", help="POST /trade/fund-transfer")
    p_ft.add_argument("--login-id", required=True)
    p_ft.add_argument("--from-market", required=True, type=int)
    p_ft.add_argument("--to-market", required=True, type=int)
    p_ft.add_argument("--transfer-amount", required=True, type=float)
    p_ft.add_argument("--remark", default="")

    # trade query group
    q_parser = trade_sp.add_parser("query", help="GET /trade/query/*")
    q_sp = q_parser.add_subparsers(dest="q", required=True)

    # orders
    q_orders = q_sp.add_parser("orders", help="GET /trade/query/orders")
    q_orders.add_argument("--login-id", required=True)
    q_orders.add_argument("--market", required=True, type=int)
    q_orders.add_argument("--code", default="")
    q_orders.add_argument("--order-id", type=int, default=0)
    q_orders.add_argument("--condition", type=int, default=0)
    q_orders.add_argument("--sort", type=int, default=0)
    q_orders.add_argument("--limit", type=int, default=0)

    # trades
    q_trades = q_sp.add_parser("trades", help="GET /trade/query/trades")
    q_trades.add_argument("--login-id", required=True)
    q_trades.add_argument("--market", required=True, type=int)
    q_trades.add_argument("--code", default="")
    q_trades.add_argument("--order-id", type=int, default=0)
    q_trades.add_argument("--condition", type=int, default=0)
    q_trades.add_argument("--sort", type=int, default=0)
    q_trades.add_argument("--limit", type=int, default=0)

    # positions
    q_positions = q_sp.add_parser("positions", help="GET /trade/query/positions")
    q_positions.add_argument("--login-id", required=True)
    q_positions.add_argument("--market", required=True, type=int)
    q_positions.add_argument("--code", default="")
    q_positions.add_argument("--limit", type=int, default=0)

    # funds
    q_funds = q_sp.add_parser("funds", help="GET /trade/query/funds")
    q_funds.add_argument("--login-id", required=True)
    q_funds.add_argument("--market", required=True, type=int)
    q_funds.add_argument("--currency", default="CNY")

    # ===== strategy =====
    strategy_parser = subparsers.add_parser("strategy", help="Strategy pending orders and orders")
    strategy_sp = strategy_parser.add_subparsers(dest="command", required=True)

    # pending-orders create
    sp_po_create = strategy_sp.add_parser("pending-create", help="POST /strategy/pending-orders")
    sp_po_create.add_argument("--strategy-id", required=True)
    sp_po_create.add_argument("--pending-order-info", required=True, help="JSON string or plain text")

    # pending-orders batch-create
    sp_po_batch = strategy_sp.add_parser("pending-batch-create", help="POST /strategy/batch-pending-orders")
    sp_po_batch.add_argument("--strategy-id", required=True)
    sp_po_batch.add_argument("--pending-orders", required=True, help="JSON array string")

    # pending-orders batch-query
    sp_po_query = strategy_sp.add_parser("pending-batch-query", help="GET /strategy/pending-orders")
    sp_po_query.add_argument("--strategy-id", default="")
    sp_po_query.add_argument("--start-time", type=int, default=0)
    sp_po_query.add_argument("--end-time", type=int, default=0)
    sp_po_query.add_argument("--limit", type=int, default=10)
    sp_po_query.add_argument("--offset", type=int, default=0)
    sp_po_query.add_argument("--order-by", default="create_time")
    sp_po_query.add_argument("--desc", action="store_true", help="sort desc (default true if provided)")

    # pending-order get
    sp_po_get = strategy_sp.add_parser("pending-get", help="GET /strategy/pending-order")
    sp_po_get.add_argument("--id", required=True, type=int)

    # pending-order update
    sp_po_update = strategy_sp.add_parser("pending-update", help="PUT /strategy/pending-order")
    sp_po_update.add_argument("--id", required=True, type=int)
    sp_po_update.add_argument("--pending-order-info", required=True, help="JSON string or plain text")

    # pending-order delete
    sp_po_delete = strategy_sp.add_parser("pending-delete", help="DELETE /strategy/pending-order")
    sp_po_delete.add_argument("--id", required=True, type=int)

    # order by order_id
    sp_order_get = strategy_sp.add_parser("order-get", help="GET /strategy/order")
    sp_order_get.add_argument("--order-id", required=True, type=int)

    # orders batch-query
    sp_orders_query = strategy_sp.add_parser("orders-query", help="GET /strategy/orders")
    sp_orders_query.add_argument("--strategy-id", default="")
    sp_orders_query.add_argument("--pending-order-id", type=int, default=0)
    sp_orders_query.add_argument("--code", default="")
    sp_orders_query.add_argument("--market", type=int, default=0)
    sp_orders_query.add_argument("--status", type=int, default=-1)
    sp_orders_query.add_argument("--start-time", type=int, default=0)
    sp_orders_query.add_argument("--end-time", type=int, default=0)
    sp_orders_query.add_argument("--limit", type=int, default=10)
    sp_orders_query.add_argument("--offset", type=int, default=0)
    sp_orders_query.add_argument("--order-by", default="create_time")
    sp_orders_query.add_argument("--desc", action="store_true")

    # orders by pending order id
    sp_orders_by_po = strategy_sp.add_parser("orders-by-pending", help="GET /strategy/orders-by-pending-order")
    sp_orders_by_po.add_argument("--pending-order-id", required=True, type=int)

    # pending-orders-with-orders
    sp_powo = strategy_sp.add_parser("pending-with-orders", help="GET /strategy/pending-orders-with-orders")
    sp_powo.add_argument("--strategy-id", default="")

    # ===== twap =====
    twap_parser = subparsers.add_parser("twap", help="TWAP split-order operations")
    twap_sp = twap_parser.add_subparsers(dest="command", required=True)

    sp_twap_sell = twap_sp.add_parser("sell", help="POST /trade/twap-sell")
    sp_twap_sell.add_argument("--login-id", required=True)
    sp_twap_sell.add_argument("--code", required=True)
    sp_twap_sell.add_argument("--quantity", required=True, type=int)
    sp_twap_sell.add_argument("--start-time", default="")
    sp_twap_sell.add_argument("--end-time", default="")
    sp_twap_sell.add_argument("--interval-seconds", type=int, default=10)
    sp_twap_sell.add_argument("--enable-price-threshold", action="store_true")
    sp_twap_sell.add_argument("--price-threshold-ratio", type=float, default=0.05)

    sp_twap_task = twap_sp.add_parser("task", help="GET /trade/twap/task")
    sp_twap_task.add_argument("--task-id", required=True, type=int)

    sp_twap_cancel = twap_sp.add_parser("cancel", help="POST /trade/twap/cancel")
    sp_twap_cancel.add_argument("--task-id", required=True, type=int)

    args = parser.parse_args()
    api = TradingAPI(args.host)

    if args.group == "api":
        if args.command == "list":
            print_json(api.get_api_list())
        elif args.command == "health":
            print_json(api.health_check())

    elif args.group == "system":
        if args.command == "start":
            print_json(api.start_all_services())
        elif args.command == "stop":
            print_json(api.stop_all_services())
        elif args.command == "health":
            print_json(api.system_health_check())
        elif args.command == "status":
            print_json(api.get_system_status())
        elif args.command == "accounts":
            print_json(api.get_all_accounts())
        elif args.command == "account-detail":
            print_json(api.get_account_detail(args.login_id))

    elif args.group == "quote":
        if args.command == "depth":
            print_json(api.get_depth_data(args.code))
        elif args.command == "index":
            print_json(api.get_index_data(args.code))
        elif args.command == "depth50":
            print_json(api.get_depth_fifty_data(args.code))
        elif args.command == "entrust":
            print_json(api.get_entrust_data(args.code))
        elif args.command == "trade":
            print_json(api.get_trade_data(args.code))

    elif args.group == "trade":
        if args.command == "buy":
            print_json(api.buy_stock(args.login_id, args.code, args.amount))
        elif args.command == "sell":
            print_json(api.sell_stock(args.login_id, args.code, args.quantity))
        elif args.command == "cancel":
            print_json(api.cancel_order(args.login_id, args.order_id, args.market, args.batch_id))
        elif args.command == "bank-transfer":
            print_json(api.bank_securities_transfer(
                args.login_id, args.direction, args.market, args.transfer_amount,
                args.bank_password, args.fund_password, args.currency, args.remark
            ))
        elif args.command == "fund-transfer":
            print_json(api.fund_transfer(
                args.login_id, args.from_market, args.to_market, args.transfer_amount, args.remark
            ))
        elif args.command == "query":
            if args.q == "orders":
                print_json(api.query_orders(
                    args.login_id, args.market, args.code, args.order_id, args.condition, args.sort, args.limit
                ))
            elif args.q == "trades":
                print_json(api.query_trades(
                    args.login_id, args.market, args.code, args.order_id, args.condition, args.sort, args.limit
                ))
            elif args.q == "positions":
                print_json(api.query_positions(args.login_id, args.market, args.code, args.limit))
            elif args.q == "funds":
                print_json(api.query_funds(args.login_id, args.market, args.currency))

    elif args.group == "strategy":
        if args.command == "pending-create":
            print_json(api.create_strategy_pending_order(args.strategy_id, args.pending_order_info))
        elif args.command == "pending-batch-create":
            try:
                pending_orders = json.loads(args.pending_orders)
            except Exception:
                print_json({"code": "INVALID_INPUT", "message": "pending-orders must be a JSON array string", "data": None})
                return
            print_json(api.batch_create_strategy_pending_orders(args.strategy_id, pending_orders))
        elif args.command == "pending-batch-query":
            print_json(api.batch_query_strategy_pending_orders(
                args.strategy_id, args.start_time, args.end_time, args.limit, args.offset, args.order_by, args.desc
            ))
        elif args.command == "pending-get":
            print_json(api.query_strategy_pending_order_by_id(args.id))
        elif args.command == "pending-update":
            print_json(api.update_strategy_pending_order(args.id, args.pending_order_info))
        elif args.command == "pending-delete":
            print_json(api.delete_strategy_pending_order(args.id))
        elif args.command == "order-get":
            print_json(api.query_strategy_order_by_order_id(args.order_id))
        elif args.command == "orders-query":
            print_json(api.batch_query_strategy_orders(
                args.strategy_id, args.pending_order_id, args.code, args.market, args.status,
                args.start_time, args.end_time, args.limit, args.offset, args.order_by, args.desc
            ))
        elif args.command == "orders-by-pending":
            print_json(api.query_strategy_orders_by_pending_order_id(args.pending_order_id))
        elif args.command == "pending-with-orders":
            print_json(api.query_strategy_pending_orders_with_orders(args.strategy_id))

    elif args.group == "twap":
        if args.command == "sell":
            print_json(api.twap_sell(
                args.login_id, args.code, args.quantity, args.start_time, args.end_time,
                args.interval_seconds, args.enable_price_threshold, args.price_threshold_ratio
            ))
        elif args.command == "task":
            print_json(api.get_twap_task_status(args.task_id))
        elif args.command == "cancel":
            print_json(api.cancel_twap_task(args.task_id))


if __name__ == "__main__":
    main()


