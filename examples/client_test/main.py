"""Read-only health check for an already running local server."""
import argparse
import json
from apis import TradingAPI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="http://127.0.0.1:8080")
    args = parser.parse_args()
    print(json.dumps(TradingAPI(args.host).health_check(), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
