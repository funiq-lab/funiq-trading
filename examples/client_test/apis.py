#!/usr/bin/env python
# -*- coding: utf-8 -*-
import urllib.request
import urllib.parse
import urllib.error
import json
from typing import Dict, Any, Union


class TradingAPI:
    def __init__(self, base_url: str = "http://127.0.0.1:8080"):
        """
        FT Trading Network API 客户端

        Args:
            base_url: API服务器地址，默认 http://127.0.0.1:8080
        """
        self.base_url = base_url.rstrip("/")

    def _make_request(self, method: str, endpoint: str, data: dict = None, params: dict = None) -> Dict[Any, Any]:
        """
        发起HTTP请求的通用方法

        Args:
            method: HTTP方法 (GET, POST, PUT, DELETE)
            endpoint: API端点
            data: POST/PUT请求体数据
            params: GET请求参数

        Returns:
            解析后的JSON响应
        """
        url = f"{self.base_url}{endpoint}"
        
        # 处理GET请求参数
        if params:
            query_string = urllib.parse.urlencode(params)
            url = f"{url}?{query_string}"

        try:
            # 创建请求
            req = urllib.request.Request(url)
            req.add_header('Content-Type', 'application/json')
            req.add_header('User-Agent', 'FT-Trading-Client/1.0')
            
            # 设置请求方法
            req.get_method = lambda: method
            
            # 处理请求体数据
            if data and method in ['POST', 'PUT']:
                json_data = json.dumps(data).encode('utf-8')
                req.add_header('Content-Length', str(len(json_data)))
            else:
                json_data = None

            # 发送请求
            with urllib.request.urlopen(req, json_data, timeout=60 * 10) as response:
                response_data = response.read().decode('utf-8')
                return json.loads(response_data)

        except urllib.error.HTTPError as e:
            # HTTP错误（4xx, 5xx）
            try:
                error_data = e.read().decode('utf-8')
                return json.loads(error_data)
            except Exception as e:
                print(f"Error parsing JSON: {e}")
                return {
                    "code": f"HTTP_{e.code}",
                    "message": f"HTTP Error {e.code}: {e.reason}",
                    "data": None,
                }

        except urllib.error.URLError as e:
            # URL错误（网络连接问题）
            return {
                "code": "CONNECTION_ERROR",
                "message": f"Cannot connect to server: {str(e.reason)}",
                "data": None,
            }

        except json.JSONDecodeError as e:
            # JSON解析错误
            return {
                "code": "INVALID_JSON",
                "message": f"Invalid JSON response: {str(e)}",
                "data": None,
            }

        except Exception as e:
            # 其他错误
            return {
                "code": "UNKNOWN_ERROR",
                "message": f"Unknown error: {str(e)}",
                "data": None,
            }

    # ===== 系统管理API =====

    def get_api_list(self) -> Dict[Any, Any]:
        """GET /api - List all API endpoints"""
        return self._make_request("GET", "/api")

    def health_check(self) -> Dict[Any, Any]:
        """GET /health - Health check endpoint"""
        return self._make_request("GET", "/health")

    def get_all_accounts(self) -> Dict[Any, Any]:
        """GET /system/accounts - Get all account information"""
        return self._make_request("GET", "/system/accounts")

    def get_account_detail(self, login_id: str) -> Dict[Any, Any]:
        """GET /system/accounts/detail?login_id={login_id} - Get specific account information"""
        return self._make_request("GET", "/system/accounts/detail", params={"login_id": login_id})

    def system_health_check(self) -> Dict[Any, Any]:
        """GET /system/health - System health check"""
        return self._make_request("GET", "/system/health")

    def get_system_status(self) -> Dict[Any, Any]:
        """GET /system/status - Get detailed service status"""
        return self._make_request("GET", "/system/status")

    def start_all_services(self) -> Dict[Any, Any]:
        """POST /system/start - Start all services"""
        return self._make_request("POST", "/system/start")

    def stop_all_services(self) -> Dict[Any, Any]:
        """POST /system/stop - Stop all services"""
        return self._make_request("POST", "/system/stop")

    # ===== 交易API =====

    def buy_stock(self, login_id: str, security_code: str, amount: float) -> Dict[Any, Any]:
        """POST /trade/buy - 买入股票"""
        data = {"login_id": login_id, "security_code": security_code, "amount": amount}
        return self._make_request("POST", "/trade/buy", data=data)

    def sell_stock(self, login_id: str, security_code: str, quantity: int) -> Dict[Any, Any]:
        """POST /trade/sell - 卖出股票"""
        data = {"login_id": login_id, "security_code": security_code, "quantity": quantity}
        return self._make_request("POST", "/trade/sell", data=data)

    def cancel_order(self, login_id: str, order_id: int, market: int, batch_id: int = 0) -> Dict[Any, Any]:
        """POST /trade/cancel - 撤销订单"""
        data = {"login_id": login_id, "order_id": order_id, "market": market}
        if batch_id > 0:
            data["batch_id"] = batch_id
        return self._make_request("POST", "/trade/cancel", data=data)

    def bank_securities_transfer(self, login_id: str, direction: int, market: int, 
                                transfer_amount: float, bank_password: str, 
                                fund_password: str, currency: str = "CNY", 
                                remark: str = "") -> Dict[Any, Any]:
        """POST /trade/bank-transfer - 银证转账"""
        data = {
            "login_id": login_id,
            "direction": direction,
            "market": market,
            "transfer_amount": transfer_amount,
            "bank_password": bank_password,
            "fund_password": fund_password,
            "currency": currency,
        }
        if remark:
            data["remark"] = remark
        return self._make_request("POST", "/trade/bank-transfer", data=data)

    def fund_transfer(self, login_id: str, from_market: int, to_market: int, 
                     transfer_amount: float, remark: str = "") -> Dict[Any, Any]:
        """POST /trade/fund-transfer - 市场间资金转账"""
        data = {
            "login_id": login_id,
            "from_market": from_market,
            "to_market": to_market,
            "transfer_amount": transfer_amount,
        }
        if remark:
            data["remark"] = remark
        return self._make_request("POST", "/trade/fund-transfer", data=data)

    # ===== 查询API =====

    def query_orders(self, login_id: str, market: int, security_code: str = "", 
                    order_id: int = 0, condition: int = 0, sort: int = 0, 
                    limit: int = 0) -> Dict[Any, Any]:
        """GET /trade/query/orders - 委托查询"""
        params = {"login_id": login_id, "market": market}
        if security_code:
            params["security_code"] = security_code
        if order_id > 0:
            params["order_id"] = order_id
        if condition > 0:
            params["condition"] = condition
        if sort > 0:
            params["sort"] = sort
        if limit > 0:
            params["limit"] = limit
        return self._make_request("GET", "/trade/query/orders", params=params)

    def query_trades(self, login_id: str, market: int, security_code: str = "", 
                    order_id: int = 0, condition: int = 0, sort: int = 0, 
                    limit: int = 0) -> Dict[Any, Any]:
        """GET /trade/query/trades - 成交查询"""
        params = {"login_id": login_id, "market": market}
        if security_code:
            params["security_code"] = security_code
        if order_id > 0:
            params["order_id"] = order_id
        if condition > 0:
            params["condition"] = condition
        if sort > 0:
            params["sort"] = sort
        if limit > 0:
            params["limit"] = limit
        return self._make_request("GET", "/trade/query/trades", params=params)

    def query_positions(self, login_id: str, market: int, security_code: str = "", 
                       limit: int = 0) -> Dict[Any, Any]:
        """GET /trade/query/positions - 持仓查询"""
        params = {"login_id": login_id, "market": market}
        if security_code:
            params["security_code"] = security_code
        if limit > 0:
            params["limit"] = limit
        return self._make_request("GET", "/trade/query/positions", params=params)

    def query_funds(self, login_id: str, market: int, currency: str = "CNY") -> Dict[Any, Any]:
        """GET /trade/query/funds - 资金查询"""
        params = {"login_id": login_id, "market": market, "currency": currency}
        return self._make_request("GET", "/trade/query/funds", params=params)

    # ===== 行情API =====

    def get_depth_data(self, security_code: str) -> Dict[Any, Any]:
        """GET /quote/depth - 深度行情查询"""
        params = {"security_code": security_code}
        return self._make_request("GET", "/quote/depth", params=params)

    def get_index_data(self, security_code: str) -> Dict[Any, Any]:
        """GET /quote/index - 指数数据查询"""
        params = {"security_code": security_code}
        return self._make_request("GET", "/quote/index", params=params)

    def get_depth_fifty_data(self, security_code: str) -> Dict[Any, Any]:
        """GET /quote/depth-fifty - 五十档深度查询"""
        params = {"security_code": security_code}
        return self._make_request("GET", "/quote/depth-fifty", params=params)

    def get_entrust_data(self, security_code: str) -> Dict[Any, Any]:
        """GET /quote/entrust - 委托数据查询"""
        params = {"security_code": security_code}
        return self._make_request("GET", "/quote/entrust", params=params)

    def get_trade_data(self, security_code: str) -> Dict[Any, Any]:
        """GET /quote/trade - 成交数据查询"""
        params = {"security_code": security_code}
        return self._make_request("GET", "/quote/trade", params=params)

    # ===== 策略API =====

    def create_strategy_pending_order(self, strategy_id: str, pending_order_info: Union[str, dict]) -> Dict[Any, Any]:
        """POST /strategy/pending-orders - 创建策略挂单"""
        data = {"strategy_id": strategy_id}
        if pending_order_info:
            if isinstance(pending_order_info, dict):
                data["pending_order_info"] = pending_order_info
            else:
                try:
                    data["pending_order_info"] = json.loads(pending_order_info)
                except:
                    data["pending_order_info"] = pending_order_info
        return self._make_request("POST", "/strategy/pending-orders", data=data)

    def batch_create_strategy_pending_orders(self, strategy_id: str, pending_orders: list) -> Dict[Any, Any]:
        """POST /strategy/batch-pending-orders - 批量创建策略挂单"""
        data = {"strategy_id": strategy_id, "pending_orders": pending_orders}
        return self._make_request("POST", "/strategy/batch-pending-orders", data=data)

    def batch_query_strategy_pending_orders(self, strategy_id: str = "", start_time: int = 0, 
                                          end_time: int = 0, limit: int = 10, offset: int = 0, 
                                          order_by: str = "create_time", desc: bool = True) -> Dict[Any, Any]:
        """GET /strategy/pending-orders - 批量查询策略挂单"""
        params = {
            "limit": limit,
            "offset": offset,
            "order_by": order_by,
            "desc": "true" if desc else "false",
        }
        if strategy_id:
            params["strategy_id"] = strategy_id
        if start_time > 0:
            params["start_time"] = start_time
        if end_time > 0:
            params["end_time"] = end_time
        return self._make_request("GET", "/strategy/pending-orders", params=params)

    def query_strategy_pending_order_by_id(self, order_id: int) -> Dict[Any, Any]:
        """GET /strategy/pending-order?id={id} - 根据ID查询单个策略挂单"""
        params = {"id": order_id}
        return self._make_request("GET", "/strategy/pending-order", params=params)

    def update_strategy_pending_order(self, order_id: int, pending_order_info: Union[str, dict]) -> Dict[Any, Any]:
        """PUT /strategy/pending-order?id={id} - 更新策略挂单"""
        data = {}
        if pending_order_info:
            if isinstance(pending_order_info, dict):
                data["pending_order_info"] = pending_order_info
            else:
                try:
                    data["pending_order_info"] = json.loads(pending_order_info)
                except:
                    data["pending_order_info"] = pending_order_info
        params = {"id": order_id}
        return self._make_request("PUT", "/strategy/pending-order", data=data, params=params)

    def delete_strategy_pending_order(self, id: int) -> Dict[Any, Any]:
        """DELETE /strategy/pending-order?id={id} - 删除策略挂单"""
        params = {"id": id}
        return self._make_request("DELETE", "/strategy/pending-order", params=params)

    def query_strategy_order_by_order_id(self, order_id: int) -> Dict[Any, Any]:
        """GET /strategy/order?order_id={order_id} - 根据order_id查询策略订单"""
        params = {"order_id": order_id}
        return self._make_request("GET", "/strategy/order", params=params)

    def batch_query_strategy_orders(self, strategy_id: str = "", pending_order_id: int = 0, 
                                   security_code: str = "", market: int = 0, status: int = -1, 
                                   start_time: int = 0, end_time: int = 0, limit: int = 10, 
                                   offset: int = 0, order_by: str = "create_time", 
                                   desc: bool = True) -> Dict[Any, Any]:
        """GET /strategy/orders - 批量查询策略订单列表"""
        params = {
            "limit": limit,
            "offset": offset,
            "order_by": order_by,
            "desc": "true" if desc else "false",
        }
        if strategy_id:
            params["strategy_id"] = strategy_id
        if pending_order_id > 0:
            params["pending_order_id"] = pending_order_id
        if security_code:
            params["security_code"] = security_code
        if market > 0:
            params["market"] = market
        if status >= 0:
            params["status"] = status
        if start_time > 0:
            params["start_time"] = start_time
        if end_time > 0:
            params["end_time"] = end_time
        return self._make_request("GET", "/strategy/orders", params=params)

    def query_strategy_orders_by_pending_order_id(self, pending_order_id: int) -> Dict[Any, Any]:
        """GET /strategy/orders-by-pending-order?pending_order_id={pending_order_id} - 根据策略挂单ID查询订单列表"""
        params = {"pending_order_id": pending_order_id}
        return self._make_request("GET", "/strategy/orders-by-pending-order", params=params)

    def query_strategy_pending_orders_with_orders(self, strategy_id: str = "") -> Dict[Any, Any]:
        """GET /strategy/pending-orders-with-orders?strategy_id={strategy_id} - 查询策略挂单及其关联订单"""
        params = {}
        if strategy_id:
            params["strategy_id"] = strategy_id
        return self._make_request("GET", "/strategy/pending-orders-with-orders", params=params)

    # 在TradingAPI类中添加TWAP拆单相关方法

    # ===== TWAP拆单API =====
    
    def twap_sell(self, login_id: str, security_code: str, quantity: int,
                  start_time: str = "", end_time: str = "", 
                  interval_seconds: int = 10, enable_price_threshold: bool = False,
                  price_threshold_ratio: float = 0.05) -> Dict[Any, Any]:
        """POST /trade/twap-sell - TWAP拆单卖出"""
        data = {
            "login_id": login_id,
            "security_code": security_code,
            "quantity": quantity,
            "interval_seconds": interval_seconds,
            "enable_price_threshold": enable_price_threshold,
            "price_threshold_ratio": price_threshold_ratio
        }
        
        if start_time:
            data["start_time"] = start_time
        if end_time:
            data["end_time"] = end_time
            
        return self._make_request("POST", "/trade/twap-sell", data=data)

    def get_twap_task_status(self, task_id: int) -> Dict[Any, Any]:
        """GET /trade/twap/task?task_id={task_id} - 查询TWAP任务状态"""
        params = {"task_id": task_id}
        return self._make_request("GET", "/trade/twap/task", params=params)

    def cancel_twap_task(self, task_id: int) -> Dict[Any, Any]:
        """POST /trade/twap/cancel?task_id={task_id} - 取消TWAP任务"""
        params = {"task_id": task_id}
        return self._make_request("POST", "/trade/twap/cancel", params=params)