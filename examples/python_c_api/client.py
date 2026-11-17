"""Call the existing core C functions from Python."""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import time
from structures import (AccountInfo, DepthData, TradeOrderRequest, TradeOrderRspData,
                        TradeOrderCancelRsqData, TradeOrderData, TradeFillData,
                        PositionData, AccountData)
import structures


class Core:
    """Own an SDK-free mock core in this Python process; close releases native resources."""
    def __init__(self, library_dir, config_dir):
        self.library_dir = Path(library_dir).resolve()
        self.config_dir = Path(config_dir).resolve()
        self.quote = self.trade = None
        self.calls = []
        self._old_cwd = None
        self._old_env = {}
        self._validate_layout()
        for file, module in [('quote_config.json', 'ft_mock_quote_parser'),
                             ('trade_config.json', 'ft_mock_trade_handler')]:
            values = json.loads((self.config_dir / file).read_text())
            active = [p for p in values['parsers'] if p.get('active')]
            if len(active) != 1 or active[0]['module'] != module:
                raise ValueError('This example requires the supplied mock plugin configuration')
        self.quote = C.CDLL(str(self.library_dir / 'libquote_service.so'))
        self.trade = C.CDLL(str(self.library_dir / 'libtrade_service.so'))
        self._bind()

    def _validate_layout(self):
        # Check the complete structures against the same headers/compiler as the core.
        executable = self.library_dir.parent / 'bin/python_c_api_layout'
        native = json.loads(subprocess.check_output([str(executable)], text=True))
        for name, layout in native.items():
            cls = getattr(structures, name)
            if C.sizeof(cls) != layout['size']:
                raise ValueError(f'{name}: native/Python size mismatch')
            for field, offset in layout['offsets'].items():
                if getattr(cls, field).offset != offset:
                    raise ValueError(f'{name}.{field}: native/Python offset mismatch')

    def _bind(self):
        def bind(lib, name, result, *args):
            fn = getattr(lib, name); fn.restype = result; fn.argtypes = list(args)
        for lib, prefix, label in [(self.quote, 'quote', 'parser_id'), (self.trade, 'trade', 'interface_id')]:
            for suffix in ['init', 'start', 'stop', 'is_running', 'is_connected']:
                bind(lib, prefix + '_' + suffix, C.c_int)
            bind(lib, prefix + '_release', None)
            bind(lib, prefix + '_get_last_error', C.c_char_p)
            bind(lib, prefix + '_get_' + label, C.c_char_p)
        bind(self.quote, 'quote_get_depth_data_by_code', C.c_int, C.c_char_p, C.POINTER(DepthData))
        bind(self.trade, 'trade_get_account_info_by_id', C.c_int, C.c_char_p, C.POINTER(AccountInfo))
        bind(self.trade, 'trade_send_order_sync', C.c_int32, C.POINTER(TradeOrderRequest), C.POINTER(TradeOrderRspData), C.c_int)
        bind(self.trade, 'trade_cancel_order_sync', C.c_int32, C.POINTER(AccountInfo), C.c_int64, C.c_uint64, C.c_int, C.POINTER(TradeOrderCancelRsqData), C.c_int)
        for name, cls, extra in [
            ('funds', AccountData, [C.c_char_p]), ('positions', PositionData, [C.c_char_p]),
            ('orders', TradeOrderData, [C.c_char_p, C.c_int64]),
            ('trades', TradeFillData, [C.c_char_p, C.c_int64])]:
            bind(self.trade, 'trade_query_' + name + '_sync', C.c_int32,
                 C.POINTER(AccountInfo), C.c_int, *extra, C.c_void_p,
                 C.POINTER(C.POINTER(cls)), C.POINTER(C.c_int), C.c_int, C.c_char_p)
        bind(self.trade, 'trade_free_query_results', None, C.c_void_p, C.c_int)

    def _check(self, result, name, lib):
        self.calls.append({'function': name, 'result': result})
        if result != 0:
            detail = getattr(lib, name.split('_')[0] + '_get_last_error')()
            raise RuntimeError(f'{name}: {result}, {(detail or b"").decode()}')

    def __enter__(self):
        runtime = self.config_dir.parents[1] / 'build/python-c-api-run'
        runtime.mkdir(parents=True, exist_ok=True)
        self._old_cwd = Path.cwd()
        settings = {'QUOTE_CONFIG_FILE': str(self.config_dir / 'quote_config.json'),
                    'TRADE_CONFIG_FILE': str(self.config_dir / 'trade_config.json'),
                    'LOG_ENABLED': 'false'}
        self._old_env = {key: os.environ.get(key) for key in settings}
        os.environ.update(settings)
        (runtime / '.env').write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
        # dotenv in the core reads the working directory. Isolate it from real configs.
        os.chdir(runtime)
        try:
            for lib, prefix in [(self.quote, 'quote'), (self.trade, 'trade')]:
                for suffix in ['init', 'start']:
                    name = prefix + '_' + suffix
                    self._check(getattr(lib, name)(), name, lib)
            self.account = AccountInfo()
            self._check(self.trade.trade_get_account_info_by_id(b'MOCK_DEMO', C.byref(self.account)),
                        'trade_get_account_info_by_id', self.trade)
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        errors = []
        for lib, prefix in [(self.trade, 'trade'), (self.quote, 'quote')]:
            if lib is None:
                continue
            try:
                self._check(getattr(lib, prefix + '_stop')(), prefix + '_stop', lib)
            except RuntimeError as exc:
                errors.append(str(exc))
            finally:
                getattr(lib, prefix + '_release')()
        if self._old_cwd is not None:
            os.chdir(self._old_cwd)
            self._old_cwd = None
            for key, value in self._old_env.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
        if errors:
            raise RuntimeError('; '.join(errors))

    def __exit__(self, exc_type, exc, traceback):
        self.close()

    def status(self):
        return {
            'quote': {'running': bool(self.quote.quote_is_running()),
                      'connected': bool(self.quote.quote_is_connected()),
                      'plugin': self.quote.quote_get_parser_id().decode()},
            'trade': {'running': bool(self.trade.trade_is_running()),
                      'connected': bool(self.trade.trade_is_connected()),
                      'plugin': self.trade.trade_get_interface_id().decode()},
        }

    def depth(self, code):
        deadline = time.monotonic() + 3
        value = DepthData()
        while time.monotonic() < deadline:
            result = self.quote.quote_get_depth_data_by_code(code.encode(), C.byref(value))
            if result == 0 and value.last_price > 0:
                self.calls.append({'function': 'quote_get_depth_data_by_code', 'result': result, 'symbol': code})
                return value
            time.sleep(0.01)
        raise RuntimeError('No generated quote received for ' + code)

    def buy(self, shares, limit):
        order = TradeOrderRequest()
        order.account = self.account; order.security_code = b'000001'
        order.market = 2; order.side = ord('B'); order.order_type = 1
        order.price = limit; order.qty = shares
        result = TradeOrderRspData()
        rc = self.trade.trade_send_order_sync(C.byref(order), C.byref(result), 1000)
        self.calls.append({'function': 'trade_send_order_sync', 'result': rc})
        if rc:
            raise RuntimeError(f'Order rejected: {result.error_msg.decode()}')
        return result

    def cancel(self, order_id):
        result = TradeOrderCancelRsqData()
        rc = self.trade.trade_cancel_order_sync(C.byref(self.account), order_id, 0, 2, C.byref(result), 1000)
        self.calls.append({'function': 'trade_cancel_order_sync', 'result': rc})
        if rc:
            raise RuntimeError(f'Cancellation rejected: {result.error_msg.decode()}')
        return result

    def query(self, name):
        cls = {'funds': AccountData, 'positions': PositionData,
               'orders': TradeOrderData, 'trades': TradeFillData}[name]
        pointer = C.POINTER(cls)(); count = C.c_int(); error = C.create_string_buffer(256)
        extra = [b'CNY'] if name == 'funds' else [None]
        if name in ('orders', 'trades'):
            extra.append(0)
        fn_name = 'trade_query_' + name + '_sync'
        rc = getattr(self.trade, fn_name)(C.byref(self.account), 2, *extra, None,
                                        C.byref(pointer), C.byref(count), 1000, error)
        self.calls.append({'function': fn_name, 'result': rc})
        try:
            if rc in (100008, 100009, 100010):
                return []
            if rc:
                raise RuntimeError(f'{fn_name}: {error.value.decode()}')
            # Copy before returning memory to the core's allocator.
            return [cls.from_buffer_copy(C.string_at(C.byref(pointer[i]), C.sizeof(cls))) for i in range(count.value)]
        finally:
            if pointer:
                self.trade.trade_free_query_results(pointer, count.value)
