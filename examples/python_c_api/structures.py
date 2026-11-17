"""Fields follow include/types/{base,quote,trade}_types.h. Checked against native C++ at startup."""
import ctypes as C

class SecurityInfo(C.Structure):
    _fields_ = [
        ('code', C.c_char * 32),
        ('market', C.c_int),
        ('name', C.c_char * 64),
        ('security_type', C.c_char * 16),
    ]

class AccountInfo(C.Structure):
    _fields_ = [
        ('login_id', C.c_char * 32),
        ('cust_id', C.c_char * 32),
        ('fund_account_id', C.c_char * 32),
        ('account_id_sh', C.c_char * 32),
        ('account_id_sz', C.c_char * 32),
        ('branch_id', C.c_char * 32),
        ('password', C.c_char * 64),
    ]

class DepthData(C.Structure):
    _fields_ = [
        ('security_info', SecurityInfo),
        ('timestamp', C.c_uint64),
        ('update_time', C.c_uint32),
        ('trading_phase', C.c_int),
        ('instrument_status', C.c_char * 16),
        ('last_price', C.c_double),
        ('pre_close_price', C.c_double),
        ('open_price', C.c_double),
        ('high_price', C.c_double),
        ('low_price', C.c_double),
        ('close_price', C.c_double),
        ('upper_limit_price', C.c_double),
        ('lower_limit_price', C.c_double),
        ('avg_price', C.c_double),
        ('total_volume', C.c_uint64),
        ('total_value', C.c_uint64),
        ('num_trades', C.c_uint64),
        ('total_bid_qty', C.c_uint64),
        ('total_ask_qty', C.c_uint64),
        ('weighted_avg_bid_price', C.c_double),
        ('weighted_avg_ask_price', C.c_double),
        ('total_bid_number', C.c_uint32),
        ('total_ask_number', C.c_uint32),
        ('bid_price', C.c_double * 10),
        ('bid_qty', C.c_uint64 * 10),
        ('ask_price', C.c_double * 10),
        ('ask_qty', C.c_uint64 * 10),
        ('total_position', C.c_uint64),
        ('reference_price', C.c_double),
        ('iopv', C.c_double),
        ('pre_iopv', C.c_double),
        ('channel_no', C.c_uint16),
        ('sequence', C.c_uint32),
    ]

class TradeOrderRequest(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('security_code', C.c_char * 32),
        ('market', C.c_int),
        ('side', C.c_int),
        ('order_type', C.c_int),
        ('price', C.c_double),
        ('qty', C.c_uint64),
        ('batch_id', C.c_uint64),
    ]

class TradeOrderRspData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('security_info', SecurityInfo),
        ('timestamp', C.c_uint64),
        ('current_account_id', C.c_char * 32),
        ('price', C.c_double),
        ('qty', C.c_uint64),
        ('request_id', C.c_int64),
        ('order_id', C.c_int64),
        ('batch_id', C.c_uint64),
        ('side', C.c_int),
        ('order_type', C.c_int),
        ('business_type', C.c_uint8),
        ('response_time', C.c_int64),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
    ]

class TradeOrderCancelRsqData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('timestamp', C.c_uint64),
        ('current_account_id', C.c_char * 32),
        ('request_id', C.c_int64),
        ('order_id', C.c_int64),
        ('batch_id', C.c_uint64),
        ('response_time', C.c_int64),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
    ]

class TradeOrderData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('security_info', SecurityInfo),
        ('timestamp', C.c_uint64),
        ('request_id', C.c_int64),
        ('current_account_id', C.c_char * 32),
        ('current_market', C.c_int),
        ('order_id', C.c_int64),
        ('exchange_order_id', C.c_char * 32),
        ('batch_id', C.c_uint64),
        ('side', C.c_int),
        ('order_type', C.c_int),
        ('sign', C.c_int),
        ('status', C.c_int),
        ('status_msg', C.c_char * 256),
        ('price', C.c_double),
        ('qty', C.c_uint64),
        ('leaves_qty', C.c_uint64),
        ('cum_qty', C.c_uint64),
        ('avg_price', C.c_double),
        ('cum_amt', C.c_double),
        ('order_time', C.c_uint64),
        ('update_time', C.c_uint64),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
        ('business_type', C.c_uint8),
        ('opu_id', C.c_uint16),
        ('is_last', C.c_bool),
    ]

class TradeFillData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('security_info', SecurityInfo),
        ('timestamp', C.c_uint64),
        ('request_id', C.c_int64),
        ('current_account_id', C.c_char * 32),
        ('current_market', C.c_int),
        ('trade_id', C.c_char * 32),
        ('order_id', C.c_int64),
        ('exchange_order_id', C.c_char * 32),
        ('batch_id', C.c_uint64),
        ('side', C.c_int),
        ('order_type', C.c_int),
        ('business_type', C.c_uint8),
        ('price', C.c_double),
        ('qty', C.c_uint64),
        ('value', C.c_double),
        ('fee', C.c_double),
        ('trade_time', C.c_uint64),
        ('currency', C.c_char * 8),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
        ('is_last', C.c_bool),
    ]

class PositionData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('security_info', SecurityInfo),
        ('timestamp', C.c_uint64),
        ('request_id', C.c_int64),
        ('current_account_id', C.c_char * 32),
        ('current_market', C.c_int),
        ('total_qty', C.c_uint64),
        ('available_qty', C.c_uint64),
        ('frozen_qty', C.c_uint64),
        ('today_buy_qty', C.c_uint64),
        ('today_sell_qty', C.c_uint64),
        ('cost_price', C.c_double),
        ('market_price', C.c_double),
        ('market_value', C.c_double),
        ('profit_loss', C.c_double),
        ('profit_loss_ratio', C.c_double),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
        ('update_time', C.c_uint64),
        ('security_type', C.c_uint16),
        ('is_last', C.c_bool),
    ]

class AccountData(C.Structure):
    _fields_ = [
        ('account', AccountInfo),
        ('timestamp', C.c_uint64),
        ('request_id', C.c_int64),
        ('currency', C.c_char * 8),
        ('current_account_id', C.c_char * 32),
        ('current_market', C.c_int),
        ('total_asset', C.c_double),
        ('cash_balance', C.c_double),
        ('available_cash', C.c_double),
        ('frozen_cash', C.c_double),
        ('market_value', C.c_double),
        ('buying_power', C.c_double),
        ('withdrawal_cash', C.c_double),
        ('profit_loss', C.c_double),
        ('profit_loss_ratio', C.c_double),
        ('error_code', C.c_int32),
        ('error_msg', C.c_char * 256),
        ('update_time', C.c_uint64),
        ('is_last', C.c_bool),
    ]
