"""Start the mock HTTP core with file logging, then verify a clean shutdown."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import socket

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('config_dir', type=Path)
args = parser.parse_args()
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='funiq-backend-test-') as directory:
    root = Path(directory)
    (root / 'data/backup').mkdir(parents=True)
    settings = {
        'QUOTE_CONFIG_FILE': str(args.config_dir / 'quote_config.json'),
        'TRADE_CONFIG_FILE': str(args.config_dir / 'trade_config.json'),
        'STRATEGY_CONFIG_FILE': str(args.config_dir / 'strategy_config.json'),
        'SYSTEM_CONFIG_FILE': str(args.config_dir / 'system_config.json'),
        'FT_DB_PATH': str(root / 'data/trading.db'),
        'FT_DB_BACKUP_PATH': str(root / 'data/backup'),
        'HTTP_SERVER_ADDRESS': '127.0.0.1', 'HTTP_SERVER_PORT': str(port),
        'LOG_ENABLED': 'true', 'LOG_LEVEL': 'info', 'LOG_CONSOLE': 'true',
        'LOG_FILE_ENABLED': 'true', 'LOG_FILE_DIR': str(root / 'logs'),
    }
    (root / '.env').write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
    with (root / 'console.txt').open('w+') as log:
        process = subprocess.Popen([str(args.executable)], cwd=root,
                                   env={**os.environ, **settings}, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError('Backend exited before readiness')
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/system/health', timeout=1) as response:
                        if json.load(response)['data']['overall_status'] == 'healthy':
                            break
                except (OSError, urllib.error.HTTPError):
                    time.sleep(0.1)
            else:
                raise RuntimeError('Backend readiness timed out')
            process.send_signal(signal.SIGTERM)
            result = process.wait(timeout=8)
            log.flush()
            output = (root / 'console.txt').read_text(errors='replace')
            if result != 0 or 'shut down successfully' not in output:
                raise RuntimeError(f'Backend shutdown failed: exit {result}\n{output[-5000:]}')
            assert any((root / 'logs').glob('*.log'))
            print('PASS: mock backend startup, file logging and clean shutdown')
        except BaseException:
            log.flush()
            print((root / 'console.txt').read_text(errors='replace')[-6000:])
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
